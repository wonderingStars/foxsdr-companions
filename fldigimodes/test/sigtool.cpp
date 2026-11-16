// ----------------------------------------------------------------------------
// sigtool.cpp -- FoxSDR fldigimodes helper: the test tool. It is NOT part of the helper.
//
// It makes test audio with fldigi's own transmitters (the only place they are compiled in: FOXSDR_TEST_TX) and
// does the small file jobs the CTest cases need. Written for this helper; GPL-3.0-only.
//
//   sigtool gen <mode> <out.wav> [--text T] [--freq Hz] [--snr dB] [--seed N] [--lead s] [--tail s] [--level L]
//                                [--rsid <name>] [--rsid-only] [--reverse]
//   sigtool wav2raw <in.wav> <out.raw>          16-bit mono WAV to raw samples
//   sigtool s16tof32 <in.raw> <out.f32>
//   sigtool feed <in.raw> <chunk-bytes>         write the file to stdout in small pieces
//   sigtool garbage <seed> <bytes> <out>
//   sigtool oddbyte <in.raw> <out.raw>          copy plus one stray byte
//   sigtool lines <file>                        print the number of lines
//   sigtool rms <in.wav>                        print the RMS in dBFS
//   sigtool jtext <file.jsonl>                  the decoded text of a helper run, joined
// SNR is signal power over noise power in a 2500 Hz bandwidth (white noise over the whole audio band), the signal
// power being the RMS of the clean transmission while it is on.
// ----------------------------------------------------------------------------
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "gen_modes.h"
#include "rsid_detector.h"

namespace {

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) {}
    uint64_t next()
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    double uni() { return ((next() >> 11) + 0.5) / 9007199254740992.0; }
    double gauss()
    {
        double u1 = uni(), u2 = uni();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * M_PI * u2);
    }
};

void put16(FILE *f, int v)
{
    fputc(v & 255, f);
    fputc((v >> 8) & 255, f);
}
void put32(FILE *f, uint32_t v)
{
    put16(f, v & 0xFFFF);
    put16(f, (v >> 16) & 0xFFFF);
}

bool write_wav(const char *path, const std::vector<int16_t> &s, int rate)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    uint32_t bytes = (uint32_t)(s.size() * 2);
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 1);
    put32(f, rate);
    put32(f, rate * 2);
    put16(f, 2);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, bytes);
    for (int16_t v : s)
        put16(f, v);
    fclose(f);
    return true;
}

bool read_all(const char *path, std::vector<unsigned char> &out)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    unsigned char b[65536];
    size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0)
        out.insert(out.end(), b, b + n);
    fclose(f);
    return true;
}

// returns the sample data of a 16-bit mono PCM WAV
bool wav_samples(const std::vector<unsigned char> &w, std::vector<int16_t> &s, int &rate)
{
    if (w.size() < 44 || memcmp(w.data(), "RIFF", 4) || memcmp(w.data() + 8, "WAVE", 4))
        return false;
    size_t p = 12;
    rate = 0;
    while (p + 8 <= w.size()) {
        uint32_t len = w[p + 4] | (w[p + 5] << 8) | (w[p + 6] << 16) | ((uint32_t)w[p + 7] << 24);
        if (!memcmp(&w[p], "fmt ", 4) && p + 8 + 16 <= w.size()) {
            int ch = w[p + 10] | (w[p + 11] << 8);
            rate = w[p + 12] | (w[p + 13] << 8) | (w[p + 14] << 16) | (w[p + 15] << 24);
            int bits = w[p + 22] | (w[p + 23] << 8);
            if (ch != 1 || bits != 16)
                return false;
        } else if (!memcmp(&w[p], "data", 4)) {
            size_t n = std::min<size_t>(len, w.size() - p - 8) / 2;
            s.resize(n);
            for (size_t i = 0; i < n; i++)
                s[i] = (int16_t)(w[p + 8 + 2 * i] | (w[p + 9 + 2 * i] << 8));
            return rate != 0;
        }
        p += 8 + len + (len & 1);
    }
    return false;
}

int cmd_gen(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: sigtool gen <mode> <out.wav> [options]\n");
        return 2;
    }
    gen::Request rq;
    rq.mode = argv[2];
    const char *out = argv[3];
    double snr = 1e9, level = 0.5, lead = 1.0, tail = 1.0;
    uint64_t seed = 1;
    rq.text = "CQ CQ CQ de FOXSDR FOXSDR K";
    rq.freq = 1500;
    for (int i = 4; i < argc; i++) {
        std::string a = argv[i];
        auto need = [&]() -> const char * {
            if (i + 1 >= argc) {
                fprintf(stderr, "sigtool: %s needs a value\n", a.c_str());
                exit(2);
            }
            return argv[++i];
        };
        if (a == "--text")
            rq.text = need();
        else if (a == "--hex") {  // the text as hexadecimal bytes, for characters above 127
            std::string h = need(), t;
            for (size_t k = 0; k + 1 < h.size(); k += 2)
                t += (char)strtol(h.substr(k, 2).c_str(), nullptr, 16);
            rq.text = t;
        }
        else if (a == "--freq")
            rq.freq = atof(need());
        else if (a == "--snr")
            snr = atof(need());
        else if (a == "--seed")
            seed = (uint64_t)atoll(need());
        else if (a == "--lead")
            lead = atof(need());
        else if (a == "--tail")
            tail = atof(need());
        else if (a == "--level")
            level = atof(need());
        else if (a == "--reverse")
            rq.reverse = true;
        else if (a == "--rsid")
            rq.rsid_name = need();
        else if (a == "--rsid-only")
            rq.rsid_only = true;
        else {
            fprintf(stderr, "sigtool: unknown option %s\n", a.c_str());
            return 2;
        }
    }
    std::vector<double> sig;
    int rate = 8000;
    std::string err;
    if (!gen::generate(rq, sig, rate, err)) {
        fprintf(stderr, "sigtool: %s\n", err.c_str());
        return 1;
    }
    // normalise the peak of the clean signal to `level`
    double peak = 0;
    for (double v : sig)
        peak = std::max(peak, std::fabs(v));
    if (peak <= 0) {
        fprintf(stderr, "sigtool: the transmitter made no audio\n");
        return 1;
    }
    for (double &v : sig)
        v *= level / peak;
    // signal power over the active part (samples above 1% of the peak envelope are "on")
    double p = 0;
    size_t cnt = 0;
    for (double v : sig)
        if (std::fabs(v) > 0.0) {
            p += v * v;
            cnt++;
        }
    double psig = cnt ? p / cnt : 1e-9;
    size_t nlead = (size_t)(lead * rate), ntail = (size_t)(tail * rate);
    std::vector<double> all(nlead, 0.0);
    all.insert(all.end(), sig.begin(), sig.end());
    all.insert(all.end(), ntail, 0.0);
    if (snr < 200) {
        // noise power in 2500 Hz = psig / 10^(snr/10); white over rate/2 so scale by (rate/2)/2500
        double pn = psig / std::pow(10.0, snr / 10.0) * ((rate / 2.0) / 2500.0);
        double sd = std::sqrt(pn);
        Rng rng(seed);
        for (double &v : all)
            v += sd * rng.gauss();
        // Strong noise would clip at 16 bit and a hard limiter changes the signal to noise ratio: scale the whole mix
        // (signal and noise together, so the ratio is kept) to a peak of 0.9 of full scale when it is above that.
        double mix_peak = 0;
        for (double v : all)
            mix_peak = std::max(mix_peak, std::fabs(v));
        if (mix_peak > 0.9) {
            double k = 0.9 / mix_peak;
            for (double &v : all)
                v *= k;
            psig *= k * k;
        }
    }
    std::vector<int16_t> s16(all.size());
    int clipped = 0;
    for (size_t i = 0; i < all.size(); i++) {
        double v = all[i] * 32767.0;
        if (v > 32767.0) {
            v = 32767.0;
            clipped++;
        }
        if (v < -32768.0) {
            v = -32768.0;
            clipped++;
        }
        s16[i] = (int16_t)std::lrint(v);
    }
    if (!write_wav(out, s16, rate)) {
        fprintf(stderr, "sigtool: cannot write %s\n", out);
        return 1;
    }
    fprintf(stderr, "sigtool: %s: %zu samples at %d Hz, signal rms %.1f dBFS, snr %s dB, %d clipped\n", out, s16.size(), rate,
            10 * std::log10(psig), snr < 200 ? std::to_string((int)snr).c_str() : "none", clipped);
    return 0;
}

int cmd_wav2raw(int argc, char **argv)
{
    if (argc != 4)
        return 2;
    std::vector<unsigned char> w;
    std::vector<int16_t> s;
    int rate;
    if (!read_all(argv[2], w) || !wav_samples(w, s, rate)) {
        fprintf(stderr, "sigtool: %s is not a 16-bit mono PCM WAV\n", argv[2]);
        return 1;
    }
    FILE *f = fopen(argv[3], "wb");
    if (!f)
        return 1;
    for (int16_t v : s)
        put16(f, v);
    fclose(f);
    fprintf(stderr, "sigtool: %zu samples, %d Hz\n", s.size(), rate);
    return 0;
}

int cmd_s16tof32(int argc, char **argv)
{
    if (argc != 4)
        return 2;
    std::vector<unsigned char> b;
    if (!read_all(argv[2], b))
        return 1;
    FILE *f = fopen(argv[3], "wb");
    if (!f)
        return 1;
    for (size_t i = 0; i + 1 < b.size(); i += 2) {
        float v = (int16_t)(b[i] | (b[i + 1] << 8)) / 32768.0f;
        fwrite(&v, 4, 1, f);
    }
    fclose(f);
    return 0;
}

int cmd_feed(int argc, char **argv)
{
    if (argc != 4)
        return 2;
    std::vector<unsigned char> b;
    if (!read_all(argv[2], b))
        return 1;
    size_t chunk = (size_t)std::max(1, atoi(argv[3]));
    for (size_t i = 0; i < b.size(); i += chunk) {
        size_t n = std::min(chunk, b.size() - i);
        if (fwrite(&b[i], 1, n, stdout) != n)
            return 1;
        fflush(stdout);
    }
    return 0;
}

int cmd_garbage(int argc, char **argv)
{
    if (argc != 5)
        return 2;
    Rng rng((uint64_t)atoll(argv[2]));
    size_t n = (size_t)atoll(argv[3]);
    FILE *f = fopen(argv[4], "wb");
    if (!f)
        return 1;
    for (size_t i = 0; i < n; i++)
        fputc((int)(rng.next() >> 24) & 255, f);
    fclose(f);
    return 0;
}

int cmd_oddbyte(int argc, char **argv)
{
    if (argc != 4)
        return 2;
    std::vector<unsigned char> b;
    if (!read_all(argv[2], b))
        return 1;
    b.push_back(0x55);
    FILE *f = fopen(argv[3], "wb");
    if (!f)
        return 1;
    fwrite(b.data(), 1, b.size(), f);
    fclose(f);
    return 0;
}

int cmd_lines(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    std::vector<unsigned char> b;
    if (!read_all(argv[2], b))
        return 1;
    size_t n = std::count(b.begin(), b.end(), (unsigned char)'\n');
    printf("%zu\n", n);
    return 0;
}

// jtext <file.jsonl>: the text of every "type":"text" line, joined (JSON string escapes undone), on standard output
int cmd_jtext(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    std::vector<unsigned char> b;
    if (!read_all(argv[2], b))
        return 1;
    std::string all(b.begin(), b.end()), out;
    size_t pos = 0;
    while (pos < all.size()) {
        size_t e = all.find('\n', pos);
        if (e == std::string::npos)
            e = all.size();
        std::string line = all.substr(pos, e - pos);
        pos = e + 1;
        if (line.find("\"type\":\"text\"") == std::string::npos)
            continue;
        size_t k = line.find("\"text\":\"");
        if (k == std::string::npos)
            continue;
        k += 8;
        while (k < line.size() && line[k] != '"') {
            if (line[k] == '\\' && k + 1 < line.size()) {
                char c = line[k + 1];
                k += 2;
                if (c == 'n') out += '\n';
                else if (c == 'r') out += '\r';
                else if (c == 't') out += '\t';
                else if (c == 'u' && k + 4 <= line.size()) {
                    out += (char)strtol(line.substr(k, 4).c_str(), nullptr, 16);
                    k += 4;
                } else out += c;
            } else
                out += line[k++];
        }
    }
    fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}

// rsid-tables: the sizes of fldigi's two RSID tables and every entry (code, name, whether fldigi has the mode)
int cmd_rsid_tables(int, char **)
{
    printf("table1 %d\ntable2 %d\n", fm::RsidDetector::table1_size(), fm::RsidDetector::table2_size());
    int known = 0, total = 0;
    for (int t = 1; t <= 2; t++) {
        int n = t == 1 ? fm::RsidDetector::table1_size() : fm::RsidDetector::table2_size();
        for (int i = 0; i < n; i++) {
            int code;
            std::string name;
            bool k;
            fm::RsidDetector::entry(t, i, code, name, k);
            printf("%d %d %s %s\n", t, code, name.c_str(), k ? "mode" : "none");
            total++;
            known += k ? 1 : 0;
        }
    }
    printf("total %d with a fldigi mode %d\n", total, known);
    return 0;
}

int cmd_rms(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    std::vector<unsigned char> w;
    std::vector<int16_t> s;
    int rate;
    if (!read_all(argv[2], w) || !wav_samples(w, s, rate))
        return 1;
    double p = 0;
    for (int16_t v : s)
        p += (double)v * v;
    p /= std::max<size_t>(1, s.size()) * 32768.0 * 32768.0;
    printf("%.1f dBFS\n", 10 * std::log10(p + 1e-30));
    return 0;
}

}  // namespace

int main(int argc, char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc < 2) {
        fprintf(stderr, "usage: sigtool gen|wav2raw|s16tof32|feed|garbage|oddbyte|lines|rms ...\n");
        return 2;
    }
    std::string c = argv[1];
    if (c == "gen") return cmd_gen(argc, argv);
    if (c == "wav2raw") return cmd_wav2raw(argc, argv);
    if (c == "s16tof32") return cmd_s16tof32(argc, argv);
    if (c == "feed") return cmd_feed(argc, argv);
    if (c == "garbage") return cmd_garbage(argc, argv);
    if (c == "oddbyte") return cmd_oddbyte(argc, argv);
    if (c == "lines") return cmd_lines(argc, argv);
    if (c == "rms") return cmd_rms(argc, argv);
    if (c == "jtext") return cmd_jtext(argc, argv);
    if (c == "rsid-tables") return cmd_rsid_tables(argc, argv);
    fprintf(stderr, "sigtool: unknown command %s\n", c.c_str());
    return 2;
}
