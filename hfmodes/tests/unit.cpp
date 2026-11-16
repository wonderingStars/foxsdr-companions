// hfmodes unit tests: the front end, the Coquelet tone tables against the Crypto Museum figure, CROWD36 tables, and the CIS-3000
// identifier fed with complex baseband directly (the OnAir way).
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/crowd36.h"
#include "../src/frontend.h"
#include "dect2/cishf/cishf_dsp.h"
#include "dect2/hfutil/hfu_fsk.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)
static const double kPi = 3.14159265358979323846;

// ---- front end ----
static void testFrontEnd() {
    // a 1000 Hz tone at 48 kHz -> 24 kHz analytic: the energy is at +1000 Hz, not at -1000 Hz
    std::vector<float> x(48000);
    for (size_t i = 0; i < x.size(); i++) x[i] = (float)(0.5 * std::sin(2 * kPi * 1000.0 * (double)i / 48000.0));
    hfm::Resampler rs(48000, 24000);
    std::vector<float> r;
    rs.push(x.data(), x.size(), r);
    CHECK(r.size() > 23900 && r.size() <= 24001, "resampled length %zu", r.size());
    hfm::Analytic an;
    std::vector<hfm::cplx> c;
    an.push(r.data(), r.size(), c);
    std::complex<double> pos(0, 0), neg(0, 0);
    double amp = 0;
    for (size_t i = 8000; i < 16000 && i < c.size(); i++) {
        const double ph = 2 * kPi * 1000.0 * (double)i / 24000.0;
        pos += std::complex<double>(c[i]) * std::complex<double>(std::cos(ph), -std::sin(ph));
        neg += std::complex<double>(c[i]) * std::complex<double>(std::cos(ph), std::sin(ph));
        amp += std::abs(std::complex<double>(c[i]));
    }
    CHECK(std::abs(pos) > 20 * std::abs(neg), "analytic: positive %.1f negative %.1f", std::abs(pos), std::abs(neg));
    CHECK(std::fabs(amp / 8000 - 0.5) < 0.02, "analytic amplitude %.3f (0.5 expected)", amp / 8000);
    // 11025 Hz input to 8000 Hz keeps a 1 kHz tone's frequency: count the zero crossings
    std::vector<float> y(11025), z;
    for (size_t i = 0; i < y.size(); i++) y[i] = (float)std::sin(2 * kPi * 1000.0 * (double)i / 11025.0);
    hfm::Resampler r8(11025, 8000);
    r8.push(y.data(), y.size(), z);
    int zc = 0;
    for (size_t i = 100; i + 1 < z.size() - 100; i++) if ((z[i] < 0) != (z[i + 1] < 0)) zc++;
    const double f = zc / 2.0 / ((double)(z.size() - 200) / 8000.0);
    CHECK(std::fabs(f - 1000) < 15, "resampled tone %.1f Hz", f);
}

// ---- Coquelet 13 tone table: the Crypto Museum figure (cryptomuseum.com/ref/coquelet/svg/coquelet13_bits.svg), written out independently ----
// group I tones 1..8 (812 .. 1022 Hz) carry ITA-2 bits 3 4 5; group II tones 9..12 (1082 .. 1172 Hz) carry bits 1 2
static void testCoquelet13Table() {
    const char* g1[8] = {"111", "011", "001", "100", "101", "110", "010", "000"};   // bits 3 4 5 of tones 1 .. 8
    const char* g2[4] = {"11", "10", "01", "00"};                                    // bits 1 2 of tones 9 .. 12
    // ITA-2, bit 1 first (the standard table): LTRS = 11111, T = 00001, E = 10000, S = 10100
    const char* chars[5] = {"11111", "00001", "10000", "10100", "00001"};   // LTRS T E S T
    std::vector<double> exp;
    for (const char* bits : chars) {
        int t2 = -1, t1 = -1;
        for (int i = 0; i < 4; i++) if (std::string(bits, 2) == g2[i]) t2 = i;
        for (int i = 0; i < 8; i++) if (std::string(bits + 2, 3) == g1[i]) t1 = i;
        exp.push_back(812 + 30.0 * t1); exp.push_back(1082 + 30.0 * t2);
    }
    // measure the generator: which of the 13 comb tones is strongest in each 75 ms slot
    using namespace dect2::hfu;
    FskGenCfg c; c.kind = FskKind::Coquelet13; c.toneMs = 75; c.centre = 0; c.idleChars = 0; c.leadSec = 0.3; c.tailSec = 0.1;
    const std::vector<dect2::cf32> sig = genFsk(c, "TEST");
    std::vector<double> got;
    const int slot = (int)(0.075 * 24000);
    for (int s = 0; s < 10; s++) {
        const size_t a = (size_t)(0.3 * 24000) + (size_t)s * (size_t)slot;
        double best = -1, bf = 0;
        for (int k = 0; k < 13; k++) {
            const double f = 812 + 30.0 * k;
            std::complex<double> acc(0, 0);
            for (int i = 0; i < slot && a + (size_t)i < sig.size(); i++) { const double ph = -2 * kPi * f * (double)i / 24000.0; acc += std::complex<double>(sig[a + (size_t)i]) * std::complex<double>(std::cos(ph), std::sin(ph)); }
            if (std::abs(acc) > best) { best = std::abs(acc); bf = f; }
        }
        got.push_back(bf);
    }
    // expected from the figure: LTRS, T, E, S, T  ->  812 1082 | 872 1172 | 1022 1112 | 902 1112 | 872 1172
    for (int i = 0; i < 10; i++) CHECK(std::fabs(got[(size_t)i] - exp[(size_t)i]) < 1, "Coquelet 13 slot %d: %.0f Hz, the figure says %.0f", i, got[(size_t)i], exp[i]);
    // the same sequence is what python-codext prints for Coquelet 13 'TEST': 872 1172 1022 1112 902 1112 872 1172 (listed in UPSTREAM.md)
}

// ---- Coquelet 8: OnAir's tone table against the sequence python-codext (GPL-3.0, which cites Wavecom's table) prints for 'TEST' ----
static void testCoquelet8Table() {
    using namespace dect2::hfu;
    FskGenCfg c; c.kind = FskKind::Coquelet8; c.toneMs = 75; c.centre = 0; c.idleChars = 0; c.leadSec = 0.3; c.tailSec = 0.1;
    const std::vector<dect2::cf32> sig = genFsk(c, "TEST");
    const double tones[8] = {773, 800, 826, 853, 880, 907, 933, 960};
    const double exp[10] = {773, 880, 826, 960, 960, 907, 853, 907, 826, 960};   // LTRS, then TEST: 826 960 960 907 853 907 826 960 (python-codext)
    const int slot = (int)(0.075 * 24000);
    for (int s = 0; s < 10; s++) {
        const size_t a = (size_t)(0.3 * 24000) + (size_t)s * (size_t)slot;
        double best = -1, bf = 0;
        for (double f : tones) {
            std::complex<double> acc(0, 0);
            for (int i = 0; i < slot && a + (size_t)i < sig.size(); i++) { const double ph = -2 * kPi * f * (double)i / 24000.0; acc += std::complex<double>(sig[a + (size_t)i]) * std::complex<double>(std::cos(ph), std::sin(ph)); }
            if (std::abs(acc) > best) { best = std::abs(acc); bf = f; }
        }
        CHECK(std::fabs(bf - exp[s]) < 1, "Coquelet 8 slot %d: %.0f Hz, python-codext says %.0f", s, bf, exp[s]);
    }
}

// ---- CROWD36 tables ----
static void testCrowd36Tables() {
    const int* t = hfm::Crowd36::toneTable();
    CHECK(t[0] == 1410 && t[33] == 2730 && t[24] == 2370, "CROWD36 tone table ends %d %d %d", t[0], t[33], t[24]);
    for (int i = 1; i < 34; i++) CHECK(t[i] - t[i - 1] == 40, "tone spacing at %d", i);
    CHECK(hfm::Crowd36::toneForChar('Q', false) == 0 && hfm::Crowd36::toneForChar('A', false) == 31 && hfm::Crowd36::toneForChar('1', true) == 0, "letter / figure tables");
}

// ---- CIS-3000 identification with complex baseband fed directly (as OnAir's own test does) ----
static void testCis3000Direct() {
    using namespace dect2::cishf;
    GenOpts o; o.symbols = 24000;
    auto x = genCis3000(o);
    auto d = makeDecoder(C3000);
    for (size_t i = 0; i < x.size(); i += 480) d->push(x.data() + i, std::min<size_t>(480, x.size() - i));
    const Status s = d->status();
    CHECK(s.mode.rfind("CIS-3000: 8-PSK, 3000 Bd", 0) == 0, "direct feed: %s", s.mode.c_str());
}

int main() {
    testFrontEnd();
    testCoquelet13Table();
    testCoquelet8Table();
    testCrowd36Tables();
    testCis3000Direct();
    std::printf("%s (%d failures)\n", fails ? "FAILED" : "all unit tests passed", fails);
    return fails ? 1 : 0;
}
