// hamdrm_txgen - stand-alone HamDRM (digital SSTV) test-signal generator.
//
// NOT part of QSSTV. This file is the command-line driver written for the HamDRM helper; everything that actually
// makes the signal (MOT encoder, data packets, FAC, MLC, interleaver, OFDM) is QSSTV's transmit chain
// (drmtx/ in this folder, ported with minimal edits, see CHANGES.md). The drivers' flow mirrors
// QSSTV's drmTransmitter::init()/start() (drmtx/drmtransmitter.cpp).
//
// Licence: GNU GPL version 3 or later (same as the helper).
//
// Output: mono 16-bit little-endian raw samples. Default rate 12000 Hz: QSSTV's own 48 kHz transmit stream passed
// through the 180-tap FIR down-sampler (decimation by 4) that QSSTV's receiver uses (dsp/downsamplefilter.cpp).
// --rate 48000 writes QSSTV's transmit stream (real channel of the stereo frames) as it leaves the chain.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "drm.h"
#include "drmtransmitter.h"
#include "drmparams.h"
#include "csoundout.h"
#include "tables/TableCarrier.h"
#include "tables/TableDRMGlobal.h"

// from dsp/downsample_coeffs.cpp (QSSTV dsp/downsamplefilter.cpp)
extern const double downSampleFilterParam[180];

namespace {

struct Options
{
    std::string in, name, out, callsign = "TEST";
    char mode = 'B';
    int bw = 0, qam = 1, prot = 0, interleaver = 1;
    int leadMs = 0, tailMs = 0, repeat = 1, rate = 12000;
    bool snr = false;
    double snrDb = 0.0;
    unsigned long long seed = 1;
    bool report = false;
    std::string dumpMot;
};

void usage()
{
    std::fputs(
        "hamdrm_txgen --in <payload file> --name <name sent in the MOT header, e.g. test.txt> --out <signal file>\n"
        "  [--mode A|B|E] (B) [--bw 0|1] (0) [--qam 0|1|2] (1: 0=4-QAM 1=16-QAM 2=64-QAM) [--prot 0|1] (0)\n"
        "  [--interleaver 0|1] (1: 0=long 2 s, 1=short 400 ms) [--callsign CALL] (TEST)\n"
        "  [--lead-silence-ms N] [--tail-silence-ms N] [--snr-db X --seed N] [--repeat N]\n"
        "  [--rate 12000|48000] (12000) [--report] [--dump-mot <text file>]\n"
        "Writes mono 16-bit little-endian raw samples. --report prints frames/duration/level and the energy per 100 Hz band.\n",
        stderr);
}

bool parseInt(const char *s, long long &v)
{
    char *end = nullptr;
    v = std::strtoll(s, &end, 10);
    return end && *end == 0 && end != s;
}

bool parseArgs(int argc, char **argv, Options &o)
{
    for (int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        auto next = [&](const char *&val) -> bool {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "missing value for %s\n", a.c_str());
                return false;
            }
            val = argv[++i];
            return true;
        };
        const char *v = nullptr;
        long long n = 0;
        if (a == "--report")
            o.report = true;
        else if (a == "--dump-mot")
        {
            if (!next(v)) return false;
            o.dumpMot = v;
        }
        else if (a == "--in")
        {
            if (!next(v)) return false;
            o.in = v;
        }
        else if (a == "--name")
        {
            if (!next(v)) return false;
            o.name = v;
        }
        else if (a == "--out")
        {
            if (!next(v)) return false;
            o.out = v;
        }
        else if (a == "--callsign")
        {
            if (!next(v)) return false;
            o.callsign = v;
        }
        else if (a == "--mode")
        {
            if (!next(v)) return false;
            if (std::strlen(v) != 1 || !std::strchr("ABEabe", v[0]))
            {
                std::fputs("--mode must be A, B or E\n", stderr);
                return false;
            }
            o.mode = (char)std::toupper(v[0]);
        }
        else if (a == "--snr-db")
        {
            if (!next(v)) return false;
            char *end = nullptr;
            o.snrDb = std::strtod(v, &end);
            if (!end || *end || end == v)
            {
                std::fputs("bad --snr-db\n", stderr);
                return false;
            }
            o.snr = true;
        }
        else if (a == "--seed")
        {
            if (!next(v)) return false;
            if (!parseInt(v, n) || n < 0) { std::fputs("bad --seed\n", stderr); return false; }
            o.seed = (unsigned long long)n;
        }
        else if (a == "--bw" || a == "--qam" || a == "--prot" || a == "--interleaver" || a == "--lead-silence-ms" ||
                 a == "--tail-silence-ms" || a == "--repeat" || a == "--rate")
        {
            if (!next(v)) return false;
            if (!parseInt(v, n))
            {
                std::fprintf(stderr, "bad value for %s\n", a.c_str());
                return false;
            }
            if (a == "--bw" && (n == 0 || n == 1)) o.bw = (int)n;
            else if (a == "--qam" && n >= 0 && n <= 2) o.qam = (int)n;
            else if (a == "--prot" && (n == 0 || n == 1)) o.prot = (int)n;
            else if (a == "--interleaver" && (n == 0 || n == 1)) o.interleaver = (int)n;
            else if (a == "--lead-silence-ms" && n >= 0 && n <= 3600000) o.leadMs = (int)n;
            else if (a == "--tail-silence-ms" && n >= 0 && n <= 3600000) o.tailMs = (int)n;
            else if (a == "--repeat" && n >= 1 && n <= 1000) o.repeat = (int)n;
            else if (a == "--rate" && (n == 12000 || n == 48000)) o.rate = (int)n;
            else
            {
                std::fprintf(stderr, "value out of range for %s\n", a.c_str());
                return false;
            }
        }
        else if (a == "--help" || a == "-h")
        {
            usage();
            std::exit(0);
        }
        else
        {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return false;
        }
    }
    if (o.in.empty() || o.name.empty() || o.out.empty())
    {
        std::fputs("--in, --name and --out are required\n", stderr);
        return false;
    }
    return true;
}

// QSSTV's 48 kHz -> 12 kHz receive down-sampler (dsp/downsamplefilter.cpp, downSample4() with the "scaled" filter
// parameters): out[m] = sum_j c[j] * x[4m + 3 - j], c normalised to unity DC gain, zero history at the start.
std::vector<double> decimateBy4(const std::vector<short> &x)
{
    double gain = 0;
    for (int j = 0; j < 180; j++) gain += downSampleFilterParam[j];
    std::vector<double> c(180);
    for (int j = 0; j < 180; j++) c[j] = downSampleFilterParam[j] / gain;
    std::vector<double> y(x.size() / 4);
    for (size_t m = 0; m < y.size(); m++)
    {
        double acc = 0;
        const long long base = (long long)(4 * m + 3);
        for (int j = 0; j < 180; j++)
        {
            const long long idx = base - j;
            if (idx < 0) break;
            acc += c[j] * (double)x[(size_t)idx];
        }
        y[m] = acc;
    }
    return y;
}

short toShort(double v)
{
    const double r = std::floor(v + 0.5);
    if (r > 32767.0) return 32767;
    if (r < -32768.0) return -32768;
    return (short)r;
}

// deterministic gaussian: mt19937_64 raw output + Box-Muller (no std::normal_distribution: its output is not
// specified by the standard and differs between library implementations)
struct Gauss
{
    std::mt19937_64 g;
    bool have = false;
    double spare = 0;
    explicit Gauss(unsigned long long seed) : g(seed) {}
    double uni()
    {
        return (double)((g() >> 11) + 1) / 9007199254740994.0; // (0,1)
    }
    double next()
    {
        if (have)
        {
            have = false;
            return spare;
        }
        const double u1 = uni(), u2 = uni();
        const double r = std::sqrt(-2.0 * std::log(u1));
        spare = r * std::sin(2.0 * M_PI * u2);
        have = true;
        return r * std::cos(2.0 * M_PI * u2);
    }
};

// ---- report: Welch spectrum, energy per 100 Hz band -------------------------------------------------------------
void fftRadix2(std::vector<double> &re, std::vector<double> &im)
{
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; i++)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j)
        {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / (double)len;
        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < len / 2; k++)
            {
                const double wr = std::cos(ang * (double)k), wi = std::sin(ang * (double)k);
                const size_t a = i + k, b = i + k + len / 2;
                const double tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
    }
}

void report(const std::vector<short> &s, int rate, size_t from, size_t to, char mode, int bw)
{
    double sum2 = 0;
    int peak = 0;
    for (size_t i = from; i < to; i++)
    {
        sum2 += (double)s[i] * s[i];
        peak = std::max(peak, std::abs((int)s[i]));
    }
    const double n = (double)(to - from);
    std::printf("signal: rms=%.1f peak=%d (16-bit full scale 32767)\n", std::sqrt(sum2 / std::max(n, 1.0)), peak);

    const int N = 4096;
    std::vector<double> psd(N / 2 + 1, 0.0), win(N);
    for (int i = 0; i < N; i++) win[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / N);
    int segs = 0;
    for (size_t p = from; p + N <= to; p += N / 2)
    {
        std::vector<double> re(N), im(N, 0.0);
        for (int i = 0; i < N; i++) re[i] = (double)s[p + i] * win[i];
        fftRadix2(re, im);
        for (int k = 0; k <= N / 2; k++) psd[k] += re[k] * re[k] + im[k] * im[k];
        segs++;
    }
    if (!segs)
    {
        std::puts("report: signal shorter than one analysis block");
        return;
    }
    const double binHz = (double)rate / N;
    const int nb = (int)(rate / 2 / 100);
    std::vector<double> band(nb, 0.0);
    double total = 0;
    for (int k = 0; k <= N / 2; k++)
    {
        const int b = std::min(nb - 1, (int)(k * binHz / 100.0));
        band[b] += psd[k];
        total += psd[k];
    }
    // expected occupancy: carriers Kmin..Kmax of the chosen mode around the DC carrier at 350 Hz (SetCarOffset)
    const int mi = mode == 'A' ? 0 : (mode == 'B' ? 1 : 2);
    const double fft = mode == 'A' ? RMA_FFT_SIZE_N : (mode == 'B' ? RMB_FFT_SIZE_N : RME_FFT_SIZE_N);
    const double df = (double)SOUNDCRD_SAMPLE_RATE / fft;
    const double lo = 350.0 + iTableCarrierKmin[bw][mi] * df, hi = 350.0 + iTableCarrierKmax[bw][mi] * df;
    double inside = 0;
    for (int k = 0; k <= N / 2; k++)
    {
        const double f = k * binHz;
        if (f >= lo - df && f <= hi + df) inside += psd[k];
    }
    double peakBand = 0;
    for (int b = 0; b < nb; b++) peakBand = std::max(peakBand, band[b]);
    int firstB = -1, lastB = -1;
    for (int b = 0; b < nb; b++)
        if (band[b] > peakBand * 1e-4) // within 40 dB of the strongest 100 Hz band
        {
            if (firstB < 0) firstB = b;
            lastB = b;
        }
    std::printf("expected carriers: mode %c bw %d: k=%d..%d, spacing %.3f Hz, DC carrier 350 Hz -> %.1f .. %.1f Hz\n",
                mode, bw, iTableCarrierKmin[bw][mi], iTableCarrierKmax[bw][mi], df, lo, hi);
    std::printf("occupied (bands within 40 dB of the strongest 100 Hz band): %d .. %d Hz\n", firstB * 100, (lastB + 1) * 100);
    std::printf("energy inside [%.1f, %.1f] Hz (+-1 carrier spacing): %.4f %% of total, outside: %.4f %%\n", lo - df, hi + df,
                100.0 * inside / total, 100.0 * (total - inside) / total);
    std::puts("energy per 100 Hz band (dB re strongest band):");
    for (int b = 0; b < nb; b++)
    {
        const double db = band[b] > 0 ? 10.0 * std::log10(band[b] / peakBand) : -999.0;
        if (db > -60.0) std::printf("  %4d-%4d Hz  %7.1f dB\n", b * 100, b * 100 + 100, db);
    }
}

} // namespace

int main(int argc, char **argv)
{
    Options o;
    if (!parseArgs(argc, argv, o))
    {
        usage();
        return 2;
    }

    // payload
    std::vector<unsigned char> payload;
    {
        std::ifstream f(o.in, std::ios::binary);
        if (!f)
        {
            std::fprintf(stderr, "cannot read %s\n", o.in.c_str());
            return 1;
        }
        payload.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    if (payload.empty())
    {
        std::fputs("payload file is empty\n", stderr);
        return 1;
    }
    // QSSTV passes the base name and the suffix separately; the MOT ContentName is "<base>.<suffix>"
    const size_t dot = o.name.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == o.name.size())
    {
        std::fputs("--name must look like base.ext (QSSTV sends <base>.<suffix>)\n", stderr);
        return 2;
    }
    const std::string base = o.name.substr(0, dot), ext = o.name.substr(dot + 1);
    if (o.name.size() > 80) std::fputs("note: QSSTV cuts the MOT file name to 80 bytes\n", stderr);

    drmTxParams p;
    p.robMode = o.mode == 'A' ? 0 : (o.mode == 'B' ? 1 : 2);
    p.qam = o.qam;
    p.bandwith = o.bw;
    p.interleaver = o.interleaver;
    p.protection = o.prot;
    p.callsign = o.callsign;
    p.reedSolomon = 0; // the .rs1..rs4 Reed-Solomon wrapper (utils/reedsolomoncoder) is not part of this tool

    txSamples48k.clear();
    txFramesWritten = 0;
    int framesPerRun = 0;
    int segmentListSize = 0;
    unsigned transport = 0;
    std::vector<std::vector<unsigned char> > groups;
    for (int r = 0; r < o.repeat; r++)
    {
        txGroupDump = (r == 0 && !o.dumpMot.empty()) ? &groups : nullptr; // dump the first transmission only
        drmTransmitter tx; // QSSTV creates one transmission per press of "send": a fresh encoder each time
        tx.init(&payload, base, ext, p);
        segmentListSize = numTxFrames;
        transport = txTransportID;
        const int before = txFramesWritten;
        tx.start(true); // runs CDRMTransmitter::Run() until the MOT encoder sets stopDRM
        framesPerRun = txFramesWritten - before;
    }
    txGroupDump = nullptr;
    const size_t samples48 = txSamples48k.size();
    if (!o.dumpMot.empty())
    {
        std::ofstream f(o.dumpMot, std::ios::trunc);
        if (!f)
        {
            std::fprintf(stderr, "cannot write %s\n", o.dumpMot.c_str());
            return 1;
        }
        f << "# one line per MSC data group = one 400 ms frame: index, hex bytes (group header, session header, segment, CRC-16)\n";
        for (size_t i = 0; i < groups.size(); i++)
        {
            f << i << ' ';
            for (unsigned char b : groups[i])
            {
                char h[4];
                std::snprintf(h, sizeof h, "%02x", b);
                f << h;
            }
            f << '\n';
        }
    }

    // output stream
    std::vector<short> sig;
    if (o.rate == 48000)
        sig = txSamples48k;
    else
    {
        const std::vector<double> d = decimateBy4(txSamples48k);
        sig.resize(d.size());
        for (size_t i = 0; i < d.size(); i++) sig[i] = toShort(d[i]);
    }
    const size_t lead = (size_t)((unsigned long long)o.leadMs * o.rate / 1000ULL);
    const size_t tail = (size_t)((unsigned long long)o.tailMs * o.rate / 1000ULL);
    std::vector<short> out(lead, 0);
    out.insert(out.end(), sig.begin(), sig.end());
    out.insert(out.end(), tail, 0);

    double noiseSigma = 0;
    if (o.snr)
    {
        double sum2 = 0;
        for (short v : sig) sum2 += (double)v * v;
        const double sigPow = sum2 / (double)sig.size(); // mean square over the transmission (silence excluded)
        noiseSigma = std::sqrt(sigPow / std::pow(10.0, o.snrDb / 10.0));
        Gauss g(o.seed);
        for (size_t i = 0; i < out.size(); i++) out[i] = toShort((double)out[i] + noiseSigma * g.next());
    }

    {
        std::ofstream f(o.out, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            std::fprintf(stderr, "cannot write %s\n", o.out.c_str());
            return 1;
        }
        std::vector<char> bytes(out.size() * 2);
        for (size_t i = 0; i < out.size(); i++) // explicit little endian
        {
            const uint16_t u = (uint16_t)out[i];
            bytes[2 * i] = (char)(u & 0xFF);
            bytes[2 * i + 1] = (char)(u >> 8);
        }
        f.write(bytes.data(), (std::streamsize)bytes.size());
        if (!f)
        {
            std::fputs("write error\n", stderr);
            return 1;
        }
    }

    std::printf("payload %zu bytes, MOT name \"%s\", transport id %u (0x%04X), %zu data groups in one transmission list\n", payload.size(),
                (base + "." + ext).c_str(), transport, transport, (size_t)segmentListSize);
    std::printf("mode %c bw %d qam %d prot %d interleaver %s callsign %s, repeat %d\n", o.mode, o.bw, o.qam, o.prot,
                o.interleaver ? "short" : "long", o.callsign.c_str(), o.repeat);
    std::printf("frames: %d total (%d per transmission), %zu samples at 48000 Hz (%.3f s, %zu per frame)\n", txFramesWritten, framesPerRun, samples48,
                (double)samples48 / 48000.0, txFramesWritten ? samples48 / (size_t)txFramesWritten : (size_t)0);
    {
        int pk = 0;
        for (short v : txSamples48k) pk = std::max(pk, std::abs((int)v));
        std::printf("48 kHz stream peak %d%s\n", pk, pk >= 32767 ? "  WARNING: overflow in QSSTV's own (short) cast" : "");
    }
    std::printf("output: %zu samples at %d Hz = %.3f s (lead %zu, tail %zu)\n", out.size(), o.rate, (double)out.size() / o.rate, lead, tail);
    if (o.snr) std::printf("noise: white gaussian, sigma %.2f, SNR %.1f dB over the 0..%d Hz output band, seed %llu\n", noiseSigma, o.snrDb, o.rate / 2, o.seed);
    if (o.report) report(out, o.rate, lead, lead + sig.size(), o.mode, o.bw);
    return 0;
}
