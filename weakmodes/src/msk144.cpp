// weakmodes: MSK144 receive decoder, a C++ port of the WSJT-X real-time MSK144 decoder.
//
// Ported from WSJT-X (https://sourceforge.net/p/wsjt/wsjtx, commit b4f9a431bcf6449df8f37b56de79d48b665b044c,
// 2025-02-04), directory lib/. Upstream file and routine names are given beside each part below.
//
//   Copyright (C) 2001-2025 Joseph H. Taylor, Jr., K1JT, and the WSJT Development Group
//   (MSK144: Steven Franke, K9AN, and Joe Taylor, K1JT).
//
//   This program is free software: you can redistribute it and/or modify it under the terms of the GNU General
//   Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
//   option) any later version. It is distributed WITHOUT ANY WARRANTY; see the GNU General Public License.
//
// What is ported: decode_msk144.f90 (block loop: blocks of 7168 samples every 3584), mskrtd.f90 (analytic signal,
// noise estimate, SNR, duplicate check, frame-averaging patterns), analytic.f90, msk144spd.f90 (short-ping decoder),
// msk144sync.f90 + msk144_freq_search.f90, tweak1.f90, msk144decodeframe.f90, bpdecode128_90.f90, platanh.f90,
// chkcrc13a.f90 (+ crc13.cpp) and indexx.f90 (only the rank lookup).
// Left out on purpose: the MSK40 short-message decoders (msk40spd, msk40sync, msk40decodeframe), the static
// equaliser training (msk144signalquality, pcoeffs), the Fortran "sw" (shorthand) list handling, and the OSD
// decoder osd128_90.f90, which msk144decodeframe.f90 does not call.
// Calculations are in double precision where the Fortran source uses single precision.

#include "msk144_tables.h"
#include "wm_common.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace wm {

namespace {

const int NSPM = 864;      // samples per 72 ms frame
const int NZ = 7168;       // analysis block (mskrtd NZ)
const int NSTEP = NZ / 2;  // decode_msk144 STEP_SIZE
const int NFFT1 = 8192;    // FFT size for the analytic signal
const double kPi = 3.14159265358979323846;
const double kTwoPi = 2.0 * kPi;

// ---- fixed waveforms (msk144decodeframe.f90 / msk144sync.f90 `first` block) ------------------------------------
struct Consts {
    double pp[12];  // half-sine pulse
    int s8[8];      // sync word as +-1
    cplx cb[42];    // complex sync-word waveform
    float rcw[12];  // raised-cosine edge (msk144spd)
    Consts() {
        static const int s8bits[8] = {0, 1, 1, 1, 0, 0, 1, 0};
        for (int i = 0; i < 12; i++) pp[i] = std::sin(i * kPi / 12.0);
        for (int i = 0; i < 8; i++) s8[i] = 2 * s8bits[i] - 1;
        double cbq[42], cbi[42];
        for (int i = 0; i < 6; i++) cbq[i] = pp[6 + i] * s8[0];
        for (int i = 0; i < 12; i++) cbq[6 + i] = pp[i] * s8[2];
        for (int i = 0; i < 12; i++) cbq[18 + i] = pp[i] * s8[4];
        for (int i = 0; i < 12; i++) cbq[30 + i] = pp[i] * s8[6];
        for (int i = 0; i < 12; i++) cbi[i] = pp[i] * s8[1];
        for (int i = 0; i < 12; i++) cbi[12 + i] = pp[i] * s8[3];
        for (int i = 0; i < 12; i++) cbi[24 + i] = pp[i] * s8[5];
        for (int i = 0; i < 6; i++) cbi[36 + i] = pp[i] * s8[7];
        for (int i = 0; i < 42; i++) cb[i] = cplx(cbi[i], cbq[i]);
        for (int i = 0; i < 12; i++) rcw[i] = (float)((1 - std::cos(i * kPi / 12.0)) / 2);
    }
};
const Consts &K() { static Consts k; return k; }

// ---- CRC-13 (crc13.cpp: boost::augmented_crc<13, 0x15D7>, initial remainder 0, MSB first) -------------------------
int crc13(const unsigned char *data, int length) {
    unsigned rem = 0;
    for (int b = 0; b < length; b++)
        for (int i = 7; i >= 0; i--) {
            unsigned top = (rem >> 12) & 1u;
            rem = ((rem << 1) | ((data[b] >> i) & 1u)) & 0x1FFFu;
            if (top) rem ^= 0x15D7u;
        }
    return (int)rem;
}

// chkcrc13a.f90: decoded[0..89] = 77 message bits + 13 CRC bits. Returns true when the CRC is good.
bool chkcrc13a(const unsigned char *decoded) {
    unsigned char by[12] = {0};
    for (int i = 0; i < 77; i++)
        if (decoded[i]) by[i / 8] |= (unsigned char)(0x80 >> (i % 8));
    int rx = 0;
    for (int i = 77; i < 90; i++) rx = (rx << 1) | (decoded[i] ? 1 : 0);
    return crc13(by, 12) == rx;
}

// ---- platanh.f90 ---------------------------------------------------------------------------------------------------
double platanh(double x) {
    int isign = 1;
    double z = x;
    if (x < 0) { isign = -1; z = std::fabs(x); }
    if (z <= 0.664) return x / 0.83;
    if (z <= 0.9217) return isign * (z - 0.4064) / 0.322;
    if (z <= 0.9951) return isign * (z - 0.8378) / 0.0524;
    if (z <= 0.9998) return isign * (z - 0.9914) / 0.0012;
    return isign * 7.0;
}

// ---- bpdecode128_90.f90: log-domain belief propagation for the (128,90) code -------------------------------------
// llr[128] in; on success message77[77] is filled, the number of hard errors is returned, else -1.
int bpdecode128_90(const double *llr, int maxiterations, unsigned char *message77) {
    using namespace msk144tab;
    const int N = kN, M = kM, ncw = kNcw;
    static double toc[M][11], tanhtoc[M][11], tov[N][4], zn[N];
    unsigned char cw[N], decoded[kK];
    for (int j = 0; j < M; j++)
        for (int i = 0; i < 11; i++) { toc[j][i] = 0; tanhtoc[j][i] = 0; }
    for (int j = 0; j < N; j++)
        for (int i = 0; i < 4; i++) tov[j][i] = 0;
    for (int j = 0; j < M; j++)
        for (int i = 0; i < nrw[j]; i++) toc[j][i] = llr[Nm[j][i] - 1];

    int ncnt = 0, nclast = 0;
    for (int iter = 0; iter <= maxiterations; iter++) {
        for (int i = 0; i < N; i++) {
            double s = 0;
            for (int k = 0; k < ncw; k++) s += tov[i][k];
            zn[i] = llr[i] + s; // apmask is all zero for MSK144
        }
        for (int i = 0; i < N; i++) cw[i] = zn[i] > 0.0 ? 1 : 0;
        int ncheck = 0;
        for (int i = 0; i < M; i++) {
            int synd = 0;
            for (int k = 0; k < nrw[i]; k++) synd += cw[Nm[i][k] - 1];
            if (synd & 1) ncheck++;
        }
        if (ncheck == 0) { // a codeword: the first 90 bits are message + CRC
            for (int i = 0; i < kK; i++) decoded[i] = cw[i];
            if (chkcrc13a(decoded)) {
                for (int i = 0; i < 77; i++) message77[i] = decoded[i];
                int nh = 0;
                for (int i = 0; i < N; i++)
                    if ((2 * cw[i] - 1) * llr[i] < 0.0) nh++;
                return nh;
            }
        }
        if (iter > 0) { // early stopping criterion
            int nd = ncheck - nclast;
            if (nd < 0) ncnt = 0; else ncnt++;
            if (ncnt >= 3 && iter >= 5 && ncheck > 10) return -1;
        }
        nclast = ncheck;
        // bits -> checks
        for (int j = 0; j < M; j++)
            for (int i = 0; i < nrw[j]; i++) {
                int ibj = Nm[j][i] - 1;
                toc[j][i] = zn[ibj];
                for (int kk = 0; kk < ncw; kk++)
                    if (Mn[ibj][kk] == j + 1) toc[j][i] -= tov[ibj][kk];
            }
        // checks -> bits
        for (int i = 0; i < M; i++)
            for (int k = 0; k < 11; k++) tanhtoc[i][k] = std::tanh(-toc[i][k] / 2);
        for (int j = 0; j < N; j++)
            for (int i = 0; i < ncw; i++) {
                int ichk = Mn[j][i] - 1;
                double Tmn = 1.0;
                for (int k = 0; k < nrw[ichk]; k++)
                    if (Nm[ichk][k] != j + 1) Tmn *= tanhtoc[ichk][k];
                tov[j][i] = 2 * platanh(-Tmn);
            }
    }
    return -1;
}

// ---- tweak1.f90: shift the frequency of an analytic signal by f0 Hz -------------------------------------------------
void tweak1(const cplx *ca, int jz, double f0, cplx *cb) {
    cplx w(1.0, 0.0);
    double dphi = kTwoPi * f0 / 12000.0;
    cplx wstep(std::cos(dphi), std::sin(dphi));
    for (int i = 0; i < jz; i++) {
        w *= wstep;
        cb[i] = w * ca[i];
    }
}

// ---- analytic.f90: real data -> analytic signal (static equaliser off: amplitude 1, phase 0) ----------------------
// d[0..npts-1] -> c[0..nfft-1]
void analytic(const double *d, int npts, int nfft, cplx *c) {
    const int nh = nfft / 2;
    const double df = 12000.0 / nfft;
    const double t = 1.0 / 2000.0, beta = 0.1;
    std::vector<double> h(nh + 1);
    for (int i = 0; i <= nh; i++) {
        double f = i * df - 1500.0;
        double af = std::fabs(f);
        double hv = 1.0;
        if (af > (1 - beta) / (2 * t) && af <= (1 + beta) / (2 * t))
            hv *= 0.5 * (1 + std::cos((kPi * t / beta) * (af - (1 - beta) / (2 * t))));
        else if (af > (1 + beta) / (2 * t))
            hv = 0.0;
        h[i] = hv;
    }
    double fac = 2.0 / nfft;
    for (int i = 0; i < npts; i++) c[i] = cplx(fac * d[i], 0.0);
    for (int i = npts; i < nfft; i++) c[i] = cplx(0, 0);
    fft(c, nfft, -1);
    for (int i = 0; i <= nh; i++) c[i] *= h[i];
    c[0] *= 0.5;
    for (int i = nh + 1; i < nfft; i++) c[i] = cplx(0, 0);
    fft(c, nfft, +1);
}

// ---- msk144_freq_search.f90 ---------------------------------------------------------------------------------------
// Finds the frequency offset that maximises the sync correlation. cdat has nframes*NSPM samples.
void freq_search(const cplx *cdat, double fc, int if1, int if2, double delf, int nframes, const int *navmask, double *xmax,
                 double *bestf, cplx *cs, double *xccs) {
    const Consts &k = K();
    int navg = 0;
    for (int i = 0; i < nframes; i++) navg += navmask[i];
    const int n = nframes * NSPM;
    const double fac = 1.0 / (48.0 * std::sqrt((double)navg));
    std::vector<cplx> cdat2(n);
    std::vector<cplx> ct2(2 * NSPM);
    std::vector<double> xcc(NSPM);
    cplx c[NSPM];
    for (int ifr = if1; ifr <= if2; ifr++) {
        double ferr = ifr * delf;
        tweak1(cdat, n, -(fc + ferr), cdat2.data());
        for (int i = 0; i < NSPM; i++) c[i] = cplx(0, 0);
        for (int i = 0; i < nframes; i++)
            if (navmask[i] == 1)
                for (int j = 0; j < NSPM; j++) c[j] += cdat2[i * NSPM + j];
        for (int i = 0; i < NSPM; i++) ct2[i] = ct2[NSPM + i] = c[i];
        double xm = 0;
        for (int ish = 0; ish < NSPM; ish++) {
            cplx cc(0, 0);
            for (int i = 0; i < 42; i++) cc += std::conj(ct2[ish + i] + ct2[336 + ish + i]) * k.cb[i]; // dot_product conjugates arg 1
            xcc[ish] = std::abs(cc);
            if (xcc[ish] > xm) xm = xcc[ish];
        }
        double xb = xm * fac;
        if (xb > *xmax) {
            *xmax = xb;
            *bestf = ferr;
            for (int i = 0; i < NSPM; i++) { cs[i] = c[i]; xccs[i] = xcc[i]; }
        }
    }
}

// ---- msk144sync.f90 ---------------------------------------------------------------------------------------------
// npklocs[0..npeaks-1] are 1-based sample shifts as in Fortran. Returns nsuccess; c[NSPM] is the averaged frame.
int msk144sync(const cplx *cdat, int nframes, int ntol, double delf, const int *navmask, int npeaks, double fc,
               double *fest, int *npklocs, double *xmax, cplx *c) {
    int nf = (int)std::lround(ntol / delf);
    double xm = 0, bf = 0;
    cplx cs[NSPM];
    double xcc[NSPM];
    for (int i = 0; i < NSPM; i++) { cs[i] = cplx(0, 0); xcc[i] = 0; }
    freq_search(cdat, fc, -nf, nf, delf, nframes, navmask, &xm, &bf, cs, xcc);
    *xmax = xm;
    *fest = fc + bf;
    for (int i = 0; i < NSPM; i++) c[i] = cs[i];
    for (int ipk = 0; ipk < npeaks; ipk++) { // the npeaks largest peaks (maxloc: first maximum)
        int ic2 = 0;
        double best = xcc[0];
        for (int i = 1; i < NSPM; i++)
            if (xcc[i] > best) { best = xcc[i]; ic2 = i; }
        ic2 += 1; // Fortran maxloc: 1-based position
        npklocs[ipk] = ic2;
        int lo = std::max(0, ic2 - 7), hi = std::min(NSPM - 1, ic2 + 7); // sic: upstream uses ic2 as a 0-based index
        for (int i = lo; i <= hi; i++) xcc[i] = 0.0;
    }
    return xm >= 1.3 ? 1 : 0;
}

// ---- msk144decodeframe.f90 -----------------------------------------------------------------------------------------
// c[864] is the (cyclically shifted) coherently averaged frame; it is rotated in place. Returns nsuccess.
// bits77_out receives the 77 message bits on success; msg the text.
std::string rtrim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

int decodeframe(cplx *c, double *softbits, std::string *msg, unsigned char *bits77_out) {
    const Consts &k = K();
    // carrier phase from the two sync words
    cplx cca(0, 0), ccb(0, 0);
    for (int i = 0; i < 42; i++) {
        cca += c[i] * std::conj(k.cb[i]);
        ccb += c[336 + i] * std::conj(k.cb[i]);
    }
    cplx sum = cca + ccb;
    double phase0 = std::atan2(sum.imag(), sum.real());
    cplx cfac(std::cos(phase0), std::sin(phase0));
    for (int i = 0; i < NSPM; i++) c[i] *= std::conj(cfac);

    // matched filter -> 144 soft bits (Fortran index i -> softbits[i-1])
    {
        double s1 = 0;
        for (int j = 0; j < 6; j++) s1 += c[j].imag() * k.pp[6 + j];
        for (int j = 0; j < 6; j++) s1 += c[NSPM - 6 + j].imag() * k.pp[j];
        softbits[0] = s1;
        double s2 = 0;
        for (int j = 0; j < 12; j++) s2 += c[j].real() * k.pp[j];
        softbits[1] = s2;
    }
    for (int i = 2; i <= 72; i++) {
        double si = 0, sr = 0;
        int b1 = 1 + (i - 1) * 12 - 6;   // 1-based first index of the imaginary window
        int b2 = 7 + (i - 1) * 12 - 6;   // and of the real window
        for (int j = 0; j < 12; j++) {
            si += c[b1 - 1 + j].imag() * k.pp[j];
            sr += c[b2 - 1 + j].real() * k.pp[j];
        }
        softbits[2 * i - 2] = si;
        softbits[2 * i - 1] = sr;
    }
    // sync-word hard errors
    int hard[144];
    for (int i = 0; i < 144; i++) hard[i] = softbits[i] >= 0.0 ? 1 : 0;
    int s1 = 0, s2 = 0;
    for (int i = 0; i < 8; i++) {
        s1 += (2 * hard[i] - 1) * k.s8[i];
        s2 += (2 * hard[56 + i] - 1) * k.s8[i];
    }
    int nbadsync = (8 - s1) / 2 + (8 - s2) / 2;
    if (nbadsync > 4) return 0;

    // normalise and make LLRs
    double sav = 0, s2av = 0;
    for (int i = 0; i < 144; i++) { sav += softbits[i]; s2av += softbits[i] * softbits[i]; }
    sav /= 144;
    s2av /= 144;
    double ssig = std::sqrt(s2av - sav * sav);
    for (int i = 0; i < 144; i++) softbits[i] /= ssig;
    const double sigma = 0.60;
    double llr[128];
    for (int i = 0; i < 48; i++) llr[i] = softbits[8 + i];
    for (int i = 0; i < 80; i++) llr[48 + i] = softbits[64 + i];
    for (int i = 0; i < 128; i++) llr[i] = 2.0 * llr[i] / (sigma * sigma);

    unsigned char dec77[77];
    int nharderror = bpdecode128_90(llr, 10, dec77);
    if (nharderror >= 0 && nharderror < 18) {
        int n3 = dec77[71] * 4 + dec77[72] * 2 + dec77[73]; // c77(72:77) -> '(2b3)' n3,i3
        int i3 = dec77[74] * 4 + dec77[75] * 2 + dec77[76];
        if ((i3 == 0 && (n3 == 1 || n3 == 3 || n3 == 4 || n3 > 5)) || i3 == 3 || i3 > 5) return 0;
        bool ok = false;
        std::string t = unpack77(dec77, &ok);
        if (!ok) return 0;
        *msg = rtrim(t);
        memcpy(bits77_out, dec77, 77);
        return 1;
    }
    return 0;
}

// ---- msk144spd.f90: short-ping decoder -------------------------------------------------------------------------------
struct SpdResult {
    int nsuccess = 0;
    std::string msg;
    double fret = 0, tret = 0;
    int navg = 0;
    cplx ct[NSPM];
    double softbits[144];
};

void spd(const cplx *cbig, int n, int ntol, double fc, SpdResult *r) {
    const Consts &k = K();
    const int MAXSTEPS = 100, NFFT = NSPM, MAXCAND = 5, NPATTERNS = 6;
    static const int navpatterns[NPATTERNS][3] = {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {1, 1, 0}, {0, 1, 1}, {1, 1, 1}};
    const double fs = 12000.0, df = fs / NFFT;
    const int nstep = (n - NSPM) / 216; // 18 ms steps
    std::vector<double> detmet(MAXSTEPS + 1, 0.0), detmet2(MAXSTEPS + 1, 0.0), detfer(MAXSTEPS + 1, -999.99);
    const int nfhi = (int)(2 * (fc + 500)), nflo = (int)(2 * (fc - 500));
    auto clampi = [&](int v) { return std::max(2, std::min(NFFT - 1, v)); };
    const int ihlo = clampi((int)std::lround((nfhi - 2 * ntol) / df) + 1), ihhi = clampi((int)std::lround((nfhi + 2 * ntol) / df) + 1);
    const int illo = clampi((int)std::lround((nflo - 2 * ntol) / df) + 1), ilhi = clampi((int)std::lround((nflo + 2 * ntol) / df) + 1);
    const int i2000 = (int)std::lround(nflo / df) + 1, i4000 = (int)std::lround(nfhi / df) + 1;
    std::vector<cplx> ctmp(NFFT);
    std::vector<double> tonespec(NFFT + 1);
    for (int istp = 1; istp <= nstep; istp++) {
        int ns = 1 + 216 * (istp - 1);
        int ne = ns + NSPM - 1;
        if (ne > n) break;
        for (int i = 0; i < NFFT; i++) ctmp[i] = cbig[ns - 1 + i];
        // coarse carrier sync: tones at 2000 and 4000 Hz in the squared-signal spectrum
        for (int i = 0; i < NFFT; i++) ctmp[i] = ctmp[i] * ctmp[i];
        for (int i = 0; i < 12; i++) ctmp[i] *= (double)k.rcw[i];
        for (int i = 0; i < 12; i++) ctmp[NSPM - 12 + i] *= (double)k.rcw[11 - i];
        fft(ctmp.data(), NFFT, -1);
        for (int i = 1; i <= NFFT; i++) tonespec[i] = std::norm(ctmp[i - 1]);
        auto C = [&](int i) { return ctmp[i - 1]; };
        // high tone window
        int ihpk = ihlo;
        for (int i = ihlo; i <= ihhi; i++)
            if (tonespec[i] > tonespec[ihpk]) ihpk = i;
        double deltah = -((C(ihpk - 1) - C(ihpk + 1)) / (2.0 * C(ihpk) - C(ihpk - 1) - C(ihpk + 1))).real();
        double ah = tonespec[ihpk], sh = 0;
        for (int i = ihlo; i <= ihhi; i++) sh += tonespec[i];
        double ahavp = (sh - ah) / (ihhi - ihlo + 1);
        double trath = ah / (ahavp + 0.01);
        // low tone window
        int ilpk = illo;
        for (int i = illo; i <= ilhi; i++)
            if (tonespec[i] > tonespec[ilpk]) ilpk = i;
        double deltal = -((C(ilpk - 1) - C(ilpk + 1)) / (2.0 * C(ilpk) - C(ilpk - 1) - C(ilpk + 1))).real();
        double al = tonespec[ilpk], sl = 0;
        for (int i = illo; i <= ilhi; i++) sl += tonespec[i];
        double alavp = (sl - al) / (ilhi - illo + 1);
        double tratl = al / (alavp + 0.01);
        double ferrh = (ihpk + deltah - i4000) * df / 2.0;
        double ferrl = (ilpk + deltal - i2000) * df / 2.0;
        double ferr = ah >= al ? ferrh : ferrl;
        detmet[istp] = std::max(ah, al);
        detmet2[istp] = std::max(trath, tratl);
        detfer[istp] = ferr;
    }
    // xmed = detmet(indices(nstep/4)): the (nstep/4)-th smallest value (indexx.f90 sorts ascending)
    {
        std::vector<double> v(detmet.begin() + 1, detmet.begin() + 1 + nstep);
        std::sort(v.begin(), v.end());
        int idx = nstep / 4;
        double xmed = idx >= 1 ? v[idx - 1] : v[0];
        for (double &x : detmet) x /= xmed;
    }
    int ndet = 0, nstart[MAXCAND];
    double ferrs[MAXCAND];
    for (int ip = 1; ip <= MAXCAND; ip++) {
        int il = 1;
        for (int i = 2; i <= nstep; i++)
            if (detmet[i] > detmet[il]) il = i;
        if (detmet[il] < 3.0) break;
        if (std::fabs(detfer[il]) <= ntol) {
            nstart[ndet] = 1 + (il - 1) * 216 + 1;
            ferrs[ndet] = detfer[il];
            ndet++;
        }
        detmet[il] = 0.0;
    }
    if (ndet < 3) {
        int nloop = MAXCAND - ndet;
        for (int ip = 1; ip <= nloop; ip++) {
            int il = 1;
            for (int i = 2; i <= nstep; i++)
                if (detmet2[i] > detmet2[il]) il = i;
            if (detmet2[il] < 12.0) break;
            if (std::fabs(detfer[il]) <= ntol) {
                nstart[ndet] = 1 + (il - 1) * 216 + 1;
                ferrs[ndet] = detfer[il];
                ndet++;
            }
            detmet2[il] = 0.0;
        }
    }
    r->nsuccess = 0;
    r->msg.clear();
    const int npeaks = 2, ntol0 = 8;
    const double deltaf = 2.0;
    std::vector<cplx> cdat(3 * NSPM);
    for (int icand = 0; icand < ndet; icand++) {
        int ib = std::max(1, nstart[icand] - NSPM);
        int ie = ib - 1 + 3 * NSPM;
        if (ie > n) { ie = n; ib = ie - 3 * NSPM + 1; }
        for (int i = 0; i < 3 * NSPM; i++) cdat[i] = cbig[ib - 1 + i];
        double fo = fc + ferrs[icand];
        for (int iav = 0; iav < NPATTERNS; iav++) {
            int navmask[3] = {navpatterns[iav][0], navpatterns[iav][1], navpatterns[iav][2]};
            int npkloc[10];
            double fest, xmax;
            cplx c[NSPM];
            int nsync = msk144sync(cdat.data(), 3, ntol0, deltaf, navmask, npeaks, fo, &fest, npkloc, &xmax, c);
            if (!nsync) continue;
            for (int ipk = 0; ipk < npeaks; ipk++)
                for (int is = 1; is <= 3; is++) {
                    int ic0 = npkloc[ipk];
                    if (is == 2) ic0 = std::max(1, ic0 - 1);
                    if (is == 3) ic0 = std::min(NSPM, ic0 + 1);
                    cplx ct[NSPM];
                    for (int i = 0; i < NSPM; i++) ct[i] = c[(i + ic0 - 1) % NSPM]; // cshift(c, ic0-1)
                    unsigned char b77[77];
                    if (decodeframe(ct, r->softbits, &r->msg, b77)) {
                        r->tret = (nstart[icand] + NSPM / 2) / fs;
                        r->fret = fest;
                        r->navg = navmask[0] + navmask[1] + navmask[2];
                        r->nsuccess = 1;
                        for (int i = 0; i < NSPM; i++) r->ct[i] = ct[i];
                        return;
                    }
                }
        }
    }
}

// ---- mskrtd.f90 per-block decoder + decode_msk144.f90 block loop ----------------------------------------------------
class Msk144Stream : public StreamDecoder {
public:
    Msk144Stream(const Options &o, const Emit &e) : emit_(e), depth_(o.depth) {
        if (o.fmin == 200 && o.fmax == 4000) { // the defaults of the program: use WSJT-X's own (Rx freq 1500 Hz, Ftol 100 Hz)
            fc_ = 1500;
            ntol_ = 100;
        } else {
            fc_ = (o.fmin + o.fmax) / 2;
            ntol_ = (o.fmax - o.fmin) / 2;
        }
        period_start_ = (long long)std::llround(o.offset * kRate);
        if (period_start_ < 0) period_start_ = 0;
        block_start_ = period_start_;
        reset_dupe();
    }

    void feed(const float *x, size_t n) override {
        buf_.insert(buf_.end(), x, x + n);
        total_ += (long long)n;
        run();
    }
    void finish() override { run(); }

private:
    static const long long kPeriod = 15LL * kRate; // T/R period 15 s (the WSJT-X default for MSK144)

    Emit emit_;
    int depth_;
    int fc_ = 1500, ntol_ = 100;
    std::vector<float> buf_;        // samples from absolute index buf0_ on
    long long buf0_ = 0, total_ = 0;
    long long period_start_ = 0, block_start_ = 0;
    // mskrtd static state
    double pnoise_ = -1.0;
    std::string msglast_;
    int nsnrlast_ = -99;

    void reset_dupe() { msglast_.clear(); nsnrlast_ = -99; }

    void run() {
        for (;;) {
            if (block_start_ - period_start_ + NZ > kPeriod) { // next block would cross the period end: new period
                period_start_ += kPeriod;
                block_start_ = period_start_;
                reset_dupe();
                continue;
            }
            if (total_ < block_start_ + NZ) break;
            if (block_start_ >= buf0_) process_block(&buf_[(size_t)(block_start_ - buf0_)]);
            block_start_ += NSTEP;
        }
        // drop what can no longer be needed
        long long keep = std::min(block_start_, total_);
        if (keep > buf0_) {
            buf_.erase(buf_.begin(), buf_.begin() + (size_t)(keep - buf0_));
            buf0_ = keep;
        }
    }

    // hspec.f90 only calls mskrtd when both half blocks are not silent; mskrtd returns when rms < 1.
    void process_block(const float *id2) {
        double tt1 = 0, tt2 = 0;
        for (int i = 0; i < NSTEP; i++) { tt1 += std::fabs(id2[i]); tt2 += std::fabs(id2[NSTEP + i]); }
        if (tt1 == 0.0 || tt2 == 0.0) return;
        double tsec = (double)(block_start_ - period_start_) / kRate;

        std::vector<double> d(NZ);
        double ss = 0;
        for (int i = 0; i < NZ; i++) { d[i] = (double)(int)std::lrint(id2[i]); ss += d[i] * d[i]; }
        double rms = std::sqrt(ss / NZ);
        if (rms < 1.0) return;
        double fac = 1.0 / rms;
        for (int i = 0; i < NZ; i++) d[i] *= fac;
        std::vector<cplx> cdat(NFFT1);
        analytic(d.data(), NZ, NFFT1, cdat.data());

        double pow8[8], pmax = -99, psum = 0;
        for (int i = 0; i < 8; i++) {
            double s = 0;
            for (int j = 0; j < NSPM; j++) s += std::norm(cdat[i * NSPM + j]);
            pow8[i] = s * rms * rms;
            pmax = std::max(pmax, pow8[i]);
            psum += pow8[i];
        }
        double pavg = psum / 8.0;

        SpdResult sp;
        const int np = 8 * NSPM;
        spd(cdat.data(), np, ntol_, (double)fc_, &sp);
        std::string msg;
        double fest = 0, tdec = 0;
        bool ok = false;
        if (sp.nsuccess) {
            ok = true;
            msg = sp.msg;
            fest = sp.fret;
            tdec = tsec + sp.tret;
        } else {
            int npat = 4;
            if (depth_ == 1) npat = 0;
            if (depth_ == 2) npat = 2;
            static const int iavpatterns[4][8] = {{1, 1, 1, 1, 0, 0, 0, 0}, {0, 0, 1, 1, 1, 1, 0, 0}, {1, 1, 1, 1, 1, 0, 0, 0}, {1, 1, 1, 1, 1, 1, 1, 0}};
            static const double xmc[4] = {2.0, 4.5, 2.5, 3.5};
            const double tframe = (double)NSPM / 12000.0;
            for (int iavg = 0; iavg < npat && !ok; iavg++) {
                const int *iavmask = iavpatterns[iavg];
                int navg = 0;
                for (int i = 0; i < 8; i++) navg += iavmask[i];
                double deltaf = 10.0 / navg;
                int npkloc[10];
                double fe, xmax;
                cplx c[NSPM];
                int nsync = msk144sync(cdat.data(), 8, ntol_, deltaf, iavmask, 2, (double)fc_, &fe, npkloc, &xmax, c);
                if (!nsync) continue;
                for (int ipk = 0; ipk < 2 && !ok; ipk++)
                    for (int is = 1; is <= 3 && !ok; is++) {
                        int ic0 = npkloc[ipk];
                        if (is == 2) ic0 = std::max(1, ic0 - 1);
                        if (is == 3) ic0 = std::min(NSPM, ic0 + 1);
                        cplx ct[NSPM];
                        for (int i = 0; i < NSPM; i++) ct[i] = c[(i + ic0 - 1) % NSPM];
                        double sb[144];
                        unsigned char b77[77];
                        std::string m;
                        if (decodeframe(ct, sb, &m, b77)) {
                            ok = true;
                            msg = m;
                            fest = fe;
                            tdec = tsec + xmc[iavg] * tframe;
                        }
                    }
            }
        }
        if (!ok) { // no decode: update the noise level used for the displayed SNR
            if (pnoise_ < 0) pnoise_ = pavg;
            else if (pavg > pnoise_) pnoise_ = 0.9 * pnoise_ + 0.1 * pavg;
            else if (pavg < pnoise_) pnoise_ = pavg;
            return;
        }
        double snr0 = 0.0;
        if (pnoise_ > 0.0) {
            double q = pmax / pnoise_ - 1.0;
            snr0 = q > 0 ? 10.0 * std::log10(q) : -99.0;
        }
        int nsnr = (int)std::lround(snr0);
        if (nsnr < -8) nsnr = -8;
        if (nsnr > 24) nsnr = 24;
        // duplicate check (ndecodesuccess == 1 only; the shorthand/SWL branch is not ported)
        if (msg != msglast_ || nsnr > nsnrlast_) {
            msglast_ = msg;
            nsnrlast_ = nsnr;
            Decode dd;
            dd.mode = "MSK144";
            dd.dt = tdec;
            dd.t_start = (double)period_start_ / kRate + tdec;
            dd.freq = fest;
            dd.snr = nsnr;
            dd.text = msg;
            if (sp.nsuccess) dd.note = "short-ping decoder"; else dd.note = "frame-averaging decoder";
            emit_(dd);
        }
    }
};

} // namespace

StreamDecoder *make_msk144_decoder(const Options &o, const Emit &e) { return new Msk144Stream(o, e); }

} // namespace wm
