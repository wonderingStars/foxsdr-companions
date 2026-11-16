// msk144_gen: MSK144 test-signal generator (TEST TOOL only; the helper never transmits).
//
// C++ port of the WSJT-X programs msk144sim.f90 and makepings.f90 and of the subroutines genmsk_128_90.f90 and
// encode_128_90.f90 (WSJT-X b4f9a43, lib/). WSJT-X, Copyright (C) 2001-2025 by Joe Taylor, K1JT, and the WSJT
// Development Group (MSK144: Steven Franke, K9AN). GPL-3.0-or-later, see LICENSE.
//
// Writes a 12000 Hz mono 16-bit WAV. Conventions of msk144sim: the peak amplitude of the signal is
// sig = sqrt(2) * 10^(snr/20); the noise is Gaussian with sigma = sqrt(6000/2500) per sample, so snr is in a
// 2500 Hz reference bandwidth; the samples are scaled by 30 before they are rounded to 16 bits.
//
//   msk144_gen --msg "CQ K1ABC FN42" --snr 0 --out x.wav [--freq 1500] [--seed 1] [--length 15]
//              [--shape ping|cw] [--width 0.12] [--ping-times 1,6,11] [--delay 0] [--noise-only]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "msk144_tables.h"
#include "pack77.h"
#include "wm_common.h"
#include "wm_wav.h"

// crc13.cpp: boost::augmented_crc<13, 0x15D7>
static int crc13(const unsigned char *data, int length) {
    unsigned rem = 0;
    for (int b = 0; b < length; b++)
        for (int i = 7; i >= 0; i--) {
            unsigned top = (rem >> 12) & 1u;
            rem = ((rem << 1) | ((data[b] >> i) & 1u)) & 0x1FFFu;
            if (top) rem ^= 0x15D7u;
        }
    return (int)rem;
}

// encode_128_90.f90: 77 message bits -> 128-bit codeword (90 message+CRC bits, then 38 parity bits)
static void encode_128_90(const uint8_t *msg77, uint8_t *cw) {
    using namespace wm::msk144tab;
    uint8_t message[90];
    unsigned char by[12] = {0};
    for (int i = 0; i < 77; i++) {
        message[i] = msg77[i];
        if (msg77[i]) by[i / 8] |= (unsigned char)(0x80 >> (i % 8));
    }
    int c = crc13(by, 12);
    for (int i = 0; i < 13; i++) message[77 + i] = (uint8_t)((c >> (12 - i)) & 1);
    for (int i = 0; i < 90; i++) cw[i] = message[i];
    for (int i = 0; i < 38; i++) {
        int nsum = 0;
        for (int j = 0; j < 23; j++) {
            int v = gen_hex[i][j] <= '9' ? gen_hex[i][j] - '0' : gen_hex[i][j] - 'a' + 10;
            int ibmax = j == 22 ? 2 : 4;
            for (int jj = 1; jj <= ibmax; jj++) {
                int icol = j * 4 + jj; // 1-based
                if ((v >> (4 - jj)) & 1) nsum += message[icol - 1];
            }
        }
        cw[90 + i] = (uint8_t)(nsum & 1);
    }
}

// genmsk_128_90.f90: 144 tones (0/1) for a 77-bit message
static void genmsk(const uint8_t *msg77, int *i4tone) {
    static const int s8[8] = {0, 1, 1, 1, 0, 0, 1, 0};
    uint8_t cw[128];
    encode_128_90(msg77, cw);
    int bitseq[144];
    for (int i = 0; i < 8; i++) bitseq[i] = s8[i];
    for (int i = 0; i < 48; i++) bitseq[8 + i] = cw[i];
    for (int i = 0; i < 8; i++) bitseq[56 + i] = s8[i];
    for (int i = 0; i < 80; i++) bitseq[64 + i] = cw[48 + i];
    for (int i = 0; i < 144; i++) bitseq[i] = 2 * bitseq[i] - 1;
    for (int i = 1; i <= 72; i++) {
        i4tone[2 * i - 2] = (bitseq[2 * i - 1] * bitseq[2 * i - 2] + 1) / 2;
        i4tone[2 * i - 1] = -(bitseq[2 * i - 1] * bitseq[(2 * i) % 144] - 1) / 2;
    }
    for (int i = 0; i < 144; i++) i4tone[i] = -i4tone[i] + 1; // flip polarity
}

int main(int argc, char **argv) {
    std::string msg = "CQ K1ABC FN42", out, shape = "ping", pingtimes;
    double snr = 0, freq = 1500, width = 0.12, length = 15;
    unsigned long long seed = 1;
    long delay = 0;
    bool noise_only = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> const char * {
            if (i + 1 >= argc) { fprintf(stderr, "msk144_gen: %s needs a value\n", a.c_str()); exit(2); }
            return argv[++i];
        };
        if (a == "--msg") msg = val();
        else if (a == "--snr") snr = atof(val());
        else if (a == "--freq") freq = atof(val());
        else if (a == "--seed") seed = strtoull(val(), nullptr, 10);
        else if (a == "--length") length = atof(val());
        else if (a == "--shape") shape = val();
        else if (a == "--width") width = atof(val());
        else if (a == "--ping-times") pingtimes = val();
        else if (a == "--delay") delay = atol(val());
        else if (a == "--noise-only") noise_only = true;
        else if (a == "--out") out = val();
        else { fprintf(stderr, "msk144_gen: unknown option %s\n", a.c_str()); return 2; }
    }
    if (out.empty()) { fprintf(stderr, "msk144_gen: --out is required\n"); return 2; }
    const int npts = (int)std::lround(length * 12000);
    const double twopi = 6.283185307179586;

    std::vector<double> waveform(npts, 0.0);
    if (!noise_only) {
        uint8_t bits[77];
        if (!wmtest::pack77(msg, bits)) { fprintf(stderr, "msk144_gen: cannot pack '%s'\n", msg.c_str()); return 1; }
        int itone[144];
        genmsk(bits, itone);
        const double dphi0 = twopi * (freq - 500.0) / 12000.0, dphi1 = twopi * (freq + 500.0) / 12000.0;
        double phi = 0.0;
        long k = 0;
        for (long rep = 0; k < npts + 144 * 6; rep++)
            for (int i = 0; i < 144; i++) {
                double dphi = itone[i] == 0 ? dphi0 : dphi1;
                for (int j = 0; j < 6; j++) {
                    long kk = k - delay;
                    if (kk >= 0 && kk < npts) waveform[kk] = std::cos(phi);
                    k++;
                    phi = std::fmod(phi + dphi, twopi);
                }
            }
    }
    // envelope: makepings.f90 (pings at t = 1, 2, ... or the times given) or constant carrier
    const double sig = std::sqrt(2.0) * std::pow(10.0, 0.05 * snr);
    std::vector<double> env(npts, 0.0);
    if (shape == "cw") {
        for (int i = 0; i < npts; i++) env[i] = sig;
    } else {
        std::vector<double> t0;
        if (pingtimes.empty()) {
            for (int i = 1; i < (int)length; i++) t0.push_back(i);
        } else {
            size_t p = 0;
            while (p < pingtimes.size()) {
                size_t q = pingtimes.find(',', p);
                if (q == std::string::npos) q = pingtimes.size();
                t0.push_back(atof(pingtimes.substr(p, q - p).c_str()));
                p = q + 1;
            }
        }
        for (double tp : t0)
            for (int i = 0; i < npts; i++) {
                double t = (i / 12000.0 - tp) / width;
                if (t < 0.0 || t > 10.0) continue;
                env[i] += 2.718 * t * std::exp(-t) * sig;
            }
    }
    wm::Rng rng(seed);
    const double fac = std::sqrt(6000.0 / 2500.0);
    std::vector<int16_t> iw(npts);
    for (int i = 0; i < npts; i++) iw[i] = wmtest::clip16(30.0 * (env[i] * waveform[i] + fac * rng.gauss()));
    if (!wmtest::write_wav16(out.c_str(), iw)) { fprintf(stderr, "msk144_gen: cannot write %s\n", out.c_str()); return 1; }
    printf("msk144_gen: %s freq=%.1f snr=%.1f shape=%s seed=%llu\n", noise_only ? "(noise only)" : msg.c_str(), freq, snr, shape.c_str(), seed);
    return 0;
}
