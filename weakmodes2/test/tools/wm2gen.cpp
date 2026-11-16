// wm2gen: TEST TOOL of the weakmodes2 helper (not part of the helper). Writes slots of 12 kHz mono audio with FST4 / FST4W / Q65
// transmissions in white Gaussian noise, as raw s16le (or a .wav with --wav). The waveform generators are OnAir's addFst4 / addQ65
// (re-implementations of WSJT-X genfst4 / gen_fst4wave and genq65 / q65_*), the same upstream as the decoder.
// Signal level: SNR in 2500 Hz noise bandwidth, the WSJT-X convention (signal power = A^2 / 2 of the constant-envelope tone,
// noise power = sigma^2 * 2500 / (rate / 2)).
//
//   wm2gen --mode fst4|fst4w|q65 --period 60 [--sub A] --sig "TEXT@FREQ@SNR@DT" [--sig ...] [--slots N] [--seed S] [--sigma 0.1]
//          [--rate 12000] [--wav] --out file
//   --trunc SECONDS cuts the last slot after that many seconds (a recording that ends in the middle of a slot).
//   --lead SECONDS writes that much noise before the first slot (for the --start-utc test).
//   wm2gen --garbage BYTES [--seed S] --out file        pseudo-random bytes (for the garbage-input tests; as f32 they contain NaN / inf)
//
// Licence GPL-3.0-or-later, like the rest of the helper folder.
#include "hfdig_wsjt_int.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace dect2;

struct Sig { std::string text; double hz, snr, dt; };

// own Box-Muller on mt19937 so the noise is identical on every standard library
struct Gauss {
    std::mt19937 g;
    bool have = false;
    double spare = 0;
    explicit Gauss(unsigned s) : g(s) {}
    double next() {
        if (have) { have = false; return spare; }
        double u1, u2;
        do { u1 = (g() + 1.0) / 4294967297.0; } while (u1 <= 0);
        u2 = g() / 4294967296.0;
        const double r = std::sqrt(-2.0 * std::log(u1));
        spare = r * std::sin(2 * M_PI * u2);
        have = true;
        return r * std::cos(2 * M_PI * u2);
    }
};

static void put16(FILE* f, uint16_t v) { fputc(v & 255, f); fputc(v >> 8, f); }
static void put32(FILE* f, uint32_t v) { put16(f, v & 0xffff); put16(f, (uint16_t)(v >> 16)); }

int main(int argc, char** argv) {
    std::string mode = "fst4", out, sub = "A";
    double period = 60, rate = 12000, sigma = 0.1;
    int slots = 1;
    long garbage = 0;
    double lead = 0, trunc = 0;
    unsigned seed = 1;
    bool wav = false, f32 = false;
    std::vector<Sig> sigs;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto nx = [&]() -> const char* { if (i + 1 >= argc) { fprintf(stderr, "wm2gen: %s needs a value\n", a.c_str()); exit(2); } return argv[++i]; };
        if (a == "--mode") mode = nx();
        else if (a == "--period") period = atof(nx());
        else if (a == "--sub") sub = nx();
        else if (a == "--slots") slots = atoi(nx());
        else if (a == "--seed") seed = (unsigned)atol(nx());
        else if (a == "--sigma") sigma = atof(nx());
        else if (a == "--rate") rate = atof(nx());
        else if (a == "--wav") wav = true;
        else if (a == "--f32") f32 = true;   // raw little-endian float32 instead of s16
        else if (a == "--trunc") trunc = atof(nx());
        else if (a == "--lead") lead = atof(nx());
        else if (a == "--garbage") garbage = atol(nx());
        else if (a == "--out") out = nx();
        else if (a == "--sig") {
            const std::string s = nx();
            Sig g;
            size_t p1 = s.find('@'), p2 = s.find('@', p1 + 1), p3 = s.find('@', p2 + 1);
            if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) { fprintf(stderr, "wm2gen: --sig TEXT@FREQ@SNR@DT\n"); return 2; }
            g.text = s.substr(0, p1); g.hz = atof(s.substr(p1 + 1, p2 - p1 - 1).c_str());
            g.snr = atof(s.substr(p2 + 1, p3 - p2 - 1).c_str()); g.dt = atof(s.substr(p3 + 1).c_str());
            sigs.push_back(g);
        } else { fprintf(stderr, "wm2gen: unknown option %s\n", a.c_str()); return 2; }
    }
    if (garbage > 0) {
        std::mt19937 r(seed);
        FILE* g = fopen(out.c_str(), "wb");
        if (!g) { fprintf(stderr, "wm2gen: cannot write %s\n", out.c_str()); return 1; }
        for (long i = 0; i < garbage; i++) fputc((int)(r() & 255), g);
        fclose(g);
        return 0;
    }
    if (out.empty() || slots < 1) { fprintf(stderr, "wm2gen: --out file required\n"); return 2; }
    const bool w = mode == "fst4w", q = mode == "q65";
    const int subi = sub.empty() ? 0 : (std::toupper((unsigned char)sub[0]) - 'A');
    const double t0 = period < 20 ? 0.5 : 1.0;
    const size_t n = (size_t)std::llround(period * rate);
    Gauss gs(seed);
    FILE* f = fopen(out.c_str(), "wb");
    if (!f) { fprintf(stderr, "wm2gen: cannot write %s\n", out.c_str()); return 1; }
    if (wav) {
        const uint32_t bytes = (uint32_t)((n * (size_t)slots + (size_t)std::llround(lead * rate)) * 2);
        fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f); put32(f, 16); put16(f, 1); put16(f, 1);
        put32(f, (uint32_t)rate); put32(f, (uint32_t)rate * 2); put16(f, 2); put16(f, 16); fwrite("data", 1, 4, f); put32(f, bytes);
    }
    // precompute tones
    struct Tx { std::vector<int> tones; Sig s; };
    std::vector<Tx> txs;
    for (const Sig& s : sigs) {
        Tx t;
        t.s = s;
        t.tones = q ? wsjt::q65Tones(s.text) : wsjt::fst4Tones(s.text, w);
        if (t.tones.empty()) { fprintf(stderr, "wm2gen: message cannot be sent in this mode: '%s'\n", s.text.c_str()); return 1; }
        txs.push_back(std::move(t));
    }
    for (long i = 0, nl = std::llround(lead * rate); i < nl; i++) {
        long s16 = std::lround(sigma * gs.next() * 32768.0);
        put16(f, (uint16_t)(int16_t)std::max(-32768L, std::min(32767L, s16)));
    }
    for (int k = 0; k < slots; k++) {
        std::vector<float> x(n, 0.f);
        for (const Tx& t : txs) {
            const double amp = std::sqrt(2.0 * std::pow(10.0, t.s.snr / 10.0) * sigma * sigma * 2500.0 / (rate / 2.0));
            if (q) wsjt::addQ65(t.tones, subi, period, t.s.hz, amp, rate, t0 + t.s.dt, x);
            else wsjt::addFst4(t.tones, period, t.s.hz, amp, rate, t0 + t.s.dt, x);
        }
        const size_t nOut = (trunc > 0 && k == slots - 1) ? std::min(n, (size_t)std::llround(trunc * rate)) : n;
        for (size_t i = 0; i < nOut; i++) {
            const double v = x[i] + sigma * gs.next();
            if (f32) { const float fv = (float)v; uint32_t u; memcpy(&u, &fv, 4); put32(f, u); continue; }
            long s16 = std::lround(v * 32768.0);
            if (s16 > 32767) s16 = 32767;
            if (s16 < -32768) s16 = -32768;
            put16(f, (uint16_t)(int16_t)s16);
        }
    }
    fclose(f);
    return 0;
}
