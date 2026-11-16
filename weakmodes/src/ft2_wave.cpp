// weakmodes: FT2L channel symbols and waveform.
//
// C++ port of lib/ft2l/genft2l.f90 (the part after pack77) and lib/ft2l/gen_ft2lwave.f90 (with gfsk_pulse.f90) from
// wsjtx-ft2libre 356be92, a WSJT-X derivative (GPL-3.0). FT2L is a REVERSE-ENGINEERED format that its author wrote
// from captures of the closed Decodium program; it may not match other FT2 software.
// WSJT-X, Copyright (C) 2001-2025 by Joe Taylor, K1JT, and the WSJT Development Group; wsjtx-ft2libre changes by
// its authors (SP5TLS). GPL-3.0-or-later, see LICENSE and UPSTREAM.md.
#include "ft2_wave.h"

#include <cmath>

#include "ldpc174_91.h"

namespace wm {

const uint8_t kFt2Rvec[77] = {0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 0,
                              1, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 1,
                              0, 1, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 1};

void ft2l_tones_from_77bits(const uint8_t *msgbits, int *i4tone) {
    static const int icos4a[4] = {0, 1, 3, 2}, icos4b[4] = {1, 0, 2, 3}, icos4c[4] = {2, 3, 1, 0}, icos4d[4] = {3, 2, 0, 1};
    uint8_t scrambled[77];
    for (int i = 0; i < 77; i++) scrambled[i] = (msgbits[i] + kFt2Rvec[i]) & 1; // msgbits=mod(msgbits+rvec,2)
    uint8_t cw[174];
    encode174_91(scrambled, cw);
    int itmp[87];
    for (int i = 0; i < 87; i++) { // Gray mapping: 00->0, 01->1, 11->2, 10->3
        int is = cw[2 * i + 1] + 2 * cw[2 * i];
        itmp[i] = is <= 1 ? is : (is == 2 ? 3 : 2);
    }
    for (int i = 0; i < 4; i++) i4tone[i] = icos4a[i];
    for (int i = 0; i < 29; i++) i4tone[4 + i] = itmp[i];
    for (int i = 0; i < 4; i++) i4tone[33 + i] = icos4b[i];
    for (int i = 0; i < 29; i++) i4tone[37 + i] = itmp[29 + i];
    for (int i = 0; i < 4; i++) i4tone[66 + i] = icos4c[i];
    for (int i = 0; i < 29; i++) i4tone[70 + i] = itmp[58 + i];
    for (int i = 0; i < 4; i++) i4tone[99 + i] = icos4d[i];
}

namespace {
// gfsk_pulse(b,t)
double gfsk_pulse(double b, double t) {
    const double pi = 4.0 * std::atan(1.0);
    double c = pi * std::sqrt(2.0 / std::log(2.0));
    return 0.5 * (std::erf(c * b * (t + 0.5)) - std::erf(c * b * (t - 0.5)));
}
} // namespace

void gen_ft2lwave(const int *itone, int nsym, int nsps, double fsample, double f0, std::complex<double> *cwave) {
    const double twopi = 8.0 * std::atan(1.0);
    const double dt = 1.0 / fsample, hmod = 1.0;
    std::vector<double> pulse(3 * nsps);
    for (int i = 1; i <= 3 * nsps; i++) {
        double tt = (i - 1.5 * nsps) / (double)nsps;
        pulse[i - 1] = gfsk_pulse(1.0, tt);
    }
    int total = (nsym + 2) * nsps;
    std::vector<double> dphi(total, 0.0);
    double dphi_peak = twopi * hmod / (double)nsps;
    for (int j = 1; j <= nsym; j++) {
        int ib = (j - 1) * nsps;
        for (int k = 0; k < 3 * nsps && ib + k < total; k++) dphi[ib + k] += dphi_peak * pulse[k] * itone[j - 1];
    }
    double phi = 0.0;
    for (int j = 0; j < total; j++) dphi[j] += twopi * f0 * dt;
    for (int j = 0; j < total; j++) {
        cwave[j] = std::complex<double>(std::cos(phi), std::sin(phi));
        phi = std::fmod(phi + dphi[j], twopi);
    }
    for (int i = 0; i < nsps; i++) // ramp-up in the first symbol slot, ramp-down in the last
        cwave[i] *= (1.0 - std::cos(twopi * i / (2.0 * nsps))) / 2.0;
    int k1 = (nsym + 1) * nsps;
    for (int i = 0; i < nsps; i++) cwave[k1 + i] *= (1.0 + std::cos(twopi * i / (2.0 * nsps))) / 2.0;
}

} // namespace wm
