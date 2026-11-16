// SPDX-License-Identifier: GPL-3.0-only
//
// js8gen: TEST TOOL of the FoxSDR "js8" GPL helper. It makes JS8 audio (12 kHz, 16-bit mono, raw or WAV) so the
// helper can be tested without a radio. It is NOT part of the helper program and the helper has no transmit code:
// this tool exists only to write files. It uses the encoder of JS8Call (JS8::encode, from JS8.cpp, GPL-3.0), the
// packing functions ported from varicode.cpp, and the modulation of JS8Call's Modulator.cpp (continuous-phase 8-FSK,
// tone spacing 12000/NSPS Hz, one symbol is NSPS samples at 12 kHz), minus the sound card plumbing.
//
//   js8gen --out file.wav|file.raw [--slots N] [--noise-rms 1000] [--seed 1] [--clean]
//          --signal "mode=A,freq=1500,snr=-10,kind=hb,call=K1ABC,grid=FN42" [--signal ...]
//
// A signal spec is key=value pairs separated by commas:
//   mode  A|B|C|E|I      speed (default A)
//   slot  N              which period it is sent in (default 0)
//   freq  Hz             audio frequency of tone 0 (default 1500)
//   dt    seconds        start error against the nominal start (default 0)
//   snr   dB             signal to noise in 2500 Hz (default -10); ignored with --clean
//   kind  hb|cq|directed|compound|cmpdir|data|raw
//   call, grid, kind-specific: hb/cq: call grid [cq=CQ DX]; directed: from to cmd [num]; compound: call grid;
//   cmpdir: call cmd [num]; data: text; raw: frame=<12 characters of the 6-bit alphabet>
//   bits  0..7           frame bits (1 first, 2 last, 4 data flag); default: 3 (first+last) except data on B/C/E/I: 7
// For every signal one JSON line describing it is printed on stdout.

#include "js8_core.h"
#include "js8_varicode.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace
{
    std::map<std::string, std::string> parseSpec(std::string const & spec)
    {
        std::map<std::string, std::string> kv;
        std::size_t pos = 0;
        while (pos <= spec.size())
        {
            auto const comma = spec.find(',', pos);
            auto const part  = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            auto const eq    = part.find('=');
            if (eq != std::string::npos) kv[part.substr(0, eq)] = part.substr(eq + 1);
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        return kv;
    }

    struct Rng // xorshift64*, Box-Muller: the same noise on every platform
    {
        std::uint64_t s;
        explicit Rng(std::uint64_t seed) : s(seed * 2685821657736338717ull + 88172645463325252ull) {}
        std::uint64_t next()
        {
            s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
            return s * 2685821657736338717ull;
        }
        double uniform() { return ((next() >> 11) + 0.5) / 9007199254740992.0; }
        double normal()
        {
            double const u = uniform(), v = uniform();
            return std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * 3.14159265358979323846 * v);
        }
    };

    void writeWav(std::FILE * f, std::vector<std::int16_t> const & pcm)
    {
        auto u32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) std::fputc((v >> (8 * i)) & 255, f); };
        auto u16 = [&](std::uint16_t v) { for (int i = 0; i < 2; ++i) std::fputc((v >> (8 * i)) & 255, f); };
        std::uint32_t const bytes = static_cast<std::uint32_t>(pcm.size() * 2);
        std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
        u32(16); u16(1); u16(1); u32(12000); u32(24000); u16(2); u16(16);
        std::fwrite("data", 1, 4, f); u32(bytes);
        for (auto s : pcm) u16(static_cast<std::uint16_t>(s));
    }

    char modeLetter(int sub) { auto const * i = JS8::modeInfo(sub); return i ? i->letter : '?'; }
}

// js8gen --convert in.wav out.raw : copy the samples of a 16-bit mono WAV file to raw s16 (what the helper reads)
int convert(char const * in, char const * out)
{
    std::FILE * f = std::fopen(in, "rb");
    if (!f) { std::fprintf(stderr, "js8gen: cannot read %s\n", in); return 1; }
    std::vector<unsigned char> b;
    unsigned char tmp[65536];
    for (std::size_t n; (n = std::fread(tmp, 1, sizeof tmp, f)) > 0;) b.insert(b.end(), tmp, tmp + n);
    std::fclose(f);
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) { std::fprintf(stderr, "js8gen: not a WAV file\n"); return 1; }
    std::size_t pos = 12;
    int channels = 0, bits = 0, rate = 0;
    while (pos + 8 <= b.size())
    {
        std::size_t sz = b[pos + 4] | (b[pos + 5] << 8) | (b[pos + 6] << 16) | (static_cast<std::size_t>(b[pos + 7]) << 24);
        if (!std::memcmp(&b[pos], "fmt ", 4) && pos + 24 <= b.size())
        {
            channels = b[pos + 10] | (b[pos + 11] << 8);
            rate     = b[pos + 12] | (b[pos + 13] << 8) | (b[pos + 14] << 16);
            bits     = b[pos + 22] | (b[pos + 23] << 8);
        }
        if (!std::memcmp(&b[pos], "data", 4))
        {
            if (channels != 1 || bits != 16 || rate != 12000) { std::fprintf(stderr, "js8gen: need 12000 Hz mono 16-bit (got %d Hz, %d ch, %d bit)\n", rate, channels, bits); return 1; }
            std::size_t n = std::min(sz, b.size() - pos - 8) & ~static_cast<std::size_t>(1);
            std::FILE * o = std::fopen(out, "wb");
            if (!o) return 1;
            std::fwrite(&b[pos + 8], 1, n, o);
            std::fclose(o);
            return 0;
        }
        pos += 8 + sz + (sz & 1);
    }
    std::fprintf(stderr, "js8gen: no data chunk\n");
    return 1;
}

// js8gen --feed file chunk : write a file to standard output `chunk` bytes at a time, flushing each piece
// (so a test can feed the helper through a real pipe in small reads)
int feed(char const * in, long chunk)
{
    std::FILE * f = std::fopen(in, "rb");
    if (!f || chunk < 1) return 1;
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::vector<unsigned char> b(static_cast<std::size_t>(chunk));
    for (std::size_t n; (n = std::fread(b.data(), 1, b.size(), f)) > 0;)
    {
        if (std::fwrite(b.data(), 1, n, stdout) != n) return 1;
        std::fflush(stdout);
    }
    std::fclose(f);
    return 0;
}

// js8gen --s16tof32 in out : the same audio as little-endian 32-bit floats scaled to +-1.0 (full scale 32768)
int toFloat(char const * in, char const * out)
{
    std::FILE * f = std::fopen(in, "rb");
    std::FILE * o = std::fopen(out, "wb");
    if (!f || !o) return 1;
    std::int16_t v;
    while (std::fread(&v, 2, 1, f) == 1)
    {
        float x = static_cast<float>(v) / 32768.0f;
        std::fwrite(&x, 4, 1, o);
    }
    std::fclose(f);
    std::fclose(o);
    return 0;
}

// js8gen --garbage nbytes seed out : uniformly random bytes (not audio-like at all)
int garbage(long n, unsigned long long seed, char const * out)
{
    std::FILE * o = std::fopen(out, "wb");
    if (!o) return 1;
    Rng rng(seed);
    for (long i = 0; i < n; ++i) std::fputc(static_cast<int>(rng.next() >> 56), o);
    std::fclose(o);
    return 0;
}

int main(int argc, char ** argv)
{
    if (argc == 4 && std::string(argv[1]) == "--convert") return convert(argv[2], argv[3]);
    if (argc == 4 && std::string(argv[1]) == "--feed") return feed(argv[2], std::atol(argv[3]));
    if (argc == 4 && std::string(argv[1]) == "--s16tof32") return toFloat(argv[2], argv[3]);
    if (argc == 5 && std::string(argv[1]) == "--garbage") return garbage(std::atol(argv[2]), std::strtoull(argv[3], nullptr, 10), argv[4]);

    std::string out;
    std::vector<std::string> specs;
    int    slots = 1;
    double noiseRms = 1000.0;
    unsigned long long seed = 1;
    bool clean = false;
    double lead = 0.0;

    for (int i = 1; i < argc; ++i)
    {
        std::string const a = argv[i];
        auto val = [&]() -> char const * { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--out") out = val();
        else if (a == "--signal") specs.push_back(val());
        else if (a == "--slots") slots = std::atoi(val());
        else if (a == "--noise-rms") noiseRms = std::atof(val());
        else if (a == "--seed") seed = std::strtoull(val(), nullptr, 10);
        else if (a == "--clean") clean = true;
        else if (a == "--lead") lead = std::atof(val());
        else { std::fprintf(stderr, "js8gen: unknown option %s\n", a.c_str()); return 2; }
    }
    if (out.empty() || slots < 1)
    {
        std::fprintf(stderr, "usage: js8gen --out file.wav|.raw [--slots N] [--noise-rms R] [--seed S] [--clean] [--lead S] --signal spec ...\n");
        return 2;
    }

    // The stream is `slots` periods of the longest period among the signals (or of Normal when there are none).
    int maxPeriod = 0;
    struct Sig { std::map<std::string, std::string> kv; int sub; };
    std::vector<Sig> sigs;
    for (auto const & sp : specs)
    {
        Sig s{parseSpec(sp), 0};
        std::string const m = s.kv.count("mode") ? s.kv["mode"] : "A";
        s.sub = m == "A" ? JS8::NORMAL : m == "B" ? JS8::FAST : m == "C" ? JS8::TURBO : m == "E" ? JS8::SLOW :
                m == "I" ? JS8::ULTRA : -1;
        if (s.sub < 0) { std::fprintf(stderr, "js8gen: bad mode %s\n", m.c_str()); return 2; }
        maxPeriod = std::max(maxPeriod, JS8::modeInfo(s.sub)->periodSeconds);
        sigs.push_back(std::move(s));
    }
    if (maxPeriod == 0) maxPeriod = 15;
    // the stream length: `slots` periods of the slowest speed present (slot numbers refer to each signal's own period)
    long long const leadSamples = static_cast<long long>(std::floor(lead * 12000.0 + 0.5));
    long long const length = static_cast<long long>(slots) * maxPeriod * 12000 + leadSamples;
    std::vector<double> mix(static_cast<std::size_t>(length), 0.0);

    for (auto & s : sigs)
    {
        auto const & kv = s.kv;
        auto get = [&](char const * k, std::string const & def = "") { auto it = kv.find(k); return it == kv.end() ? def : it->second; };
        auto const * info = JS8::modeInfo(s.sub);

        std::string const kind = get("kind", "hb");
        std::string frame;
        int bits = 3;
        bool fastData = s.sub != JS8::NORMAL;

        if (kind == "hb" || kind == "cq")
        {
            std::string cq = get("cq", kind == "cq" ? "CQ CQ CQ" : "HB");
            frame = Js8::packHeartbeat(get("call"), cq, get("grid"));
        }
        else if (kind == "directed")
        {
            bool const hasNum = kv.count("num") > 0;
            frame = Js8::packDirected(get("from"), get("to"), " " + get("cmd"), hasNum ? std::atoi(get("num").c_str()) : 0, hasNum);
        }
        else if (kind == "compound") frame = Js8::packCompound(get("call"), get("grid"));
        else if (kind == "cmpdir")
        {
            bool const hasNum = kv.count("num") > 0;
            frame = Js8::packCompoundDirected(get("call"), " " + get("cmd"), hasNum ? std::atoi(get("num").c_str()) : 0, hasNum);
        }
        else if (kind == "data")
        {
            int used = 0;
            std::string text = get("text");
            for (auto & c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            frame = Js8::packData(text, fastData, &used);
            bits = fastData ? 7 : 3;
        }
        else if (kind == "raw") frame = get("frame");
        else { std::fprintf(stderr, "js8gen: bad kind %s\n", kind.c_str()); return 2; }

        if (frame.size() != 12)
        {
            std::fprintf(stderr, "js8gen: could not pack signal (kind %s)\n", kind.c_str());
            return 2;
        }
        if (kv.count("bits")) bits = std::atoi(get("bits").c_str());

        int tones[JS8_NUM_SYMBOLS];
        JS8::encode(bits & 7, JS8::Costas::array(info->costas), frame.c_str(), tones);

        double const freq = std::atof(get("freq", "1500").c_str());
        double const dt   = std::atof(get("dt", "0").c_str());
        double const snr  = std::atof(get("snr", "-10").c_str());
        int const    slot = std::atoi(get("slot", "0").c_str());

        // amplitude: sinusoid of peak A has power A^2/2; noise of rms sigma over 6000 Hz has sigma^2 * 2500/6000 in 2500 Hz
        double const amp = clean ? 10000.0 : std::sqrt(2.0 * std::pow(10.0, snr / 10.0) * noiseRms * noiseRms * 2500.0 / 6000.0);

        long long const start = leadSamples + static_cast<long long>(slot) * info->periodSeconds * 12000 +
                                static_cast<long long>(std::floor((info->startDelayMS / 1000.0 + dt) * 12000.0 + 0.5));
        double const spacing = 12000.0 / info->symbolSamples;
        double phi = 0.0;
        long long n = start;
        for (int sym = 0; sym < JS8_NUM_SYMBOLS; ++sym)
        {
            double const dphi = 2.0 * 3.14159265358979323846 * (freq + tones[sym] * spacing) / 12000.0;
            for (int k = 0; k < info->symbolSamples; ++k, ++n)
            {
                phi += dphi;
                if (phi > 2.0 * 3.14159265358979323846) phi -= 2.0 * 3.14159265358979323846;
                if (n >= 0 && n < length) mix[static_cast<std::size_t>(n)] += amp * std::sin(phi);
            }
        }

        Js8::Frame const f = Js8::interpret(frame, bits);
        std::printf("{\"mode\":\"%c\",\"slot\":%d,\"freq\":%.1f,\"dt\":%.2f,\"snr\":%.1f,\"bits\":%d,\"frame\":\"%s\",\"frame_type\":\"%s\",\"text\":\"%s\"}\n",
                    modeLetter(s.sub), slot, freq, dt, snr, bits & 7, frame.c_str(), Js8::frameTypeName(f.frameType), f.text.c_str());
    }

    Rng rng(seed);
    std::vector<std::int16_t> pcm(mix.size());
    for (std::size_t i = 0; i < mix.size(); ++i)
    {
        double v = mix[i] + (clean ? 0.0 : noiseRms * rng.normal());
        v = std::max(-32768.0, std::min(32767.0, std::floor(v + 0.5)));
        pcm[i] = static_cast<std::int16_t>(v);
    }

    std::FILE * f = std::fopen(out.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "js8gen: cannot write %s\n", out.c_str()); return 1; }
    if (out.size() > 4 && out.compare(out.size() - 4, 4, ".wav") == 0) writeWav(f, pcm);
    else std::fwrite(pcm.data(), 2, pcm.size(), f);
    std::fclose(f);
    return 0;
}
