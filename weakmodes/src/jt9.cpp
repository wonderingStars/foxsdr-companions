// weakmodes / JT9: receive decoder for the one-minute sub-mode (JT9A, 9-FSK, 85 symbols of 6912 samples at 12000 Hz,
// tone spacing 1.736 Hz, 16 sync symbols on tone 0 and 69 data symbols of 3 bits each, K=32 r=1/2 convolutional
// code, 72-bit message, no CRC). Decoded with Phil Karn's Fano sequential decoder.
//
// C++ port of the WSJT-X decoder path used for JT9 sub-mode A (nmode=9, nsubmode=0). Upstream file and routine
// names are given in comments beside each ported part:
//   lib/symspec.f90   (half-symbol spectra ss(184,NSMAX), here ss for 181 half symbols)
//   lib/jt9_decode.f90 (decode: candidate loop), lib/sync9.f90, lib/pctile.f90 (+ shell.f90)
//   lib/softsym.f90, lib/downsam9.f90, lib/peakdt9.f90, lib/getlags.f90, lib/afc9.f90 (+ shft), lib/fchisq.f90,
//   lib/twkfreq.f90, lib/symspec2.f90, lib/chkss2.f90, lib/interleave9.f90 (jt9_pack.cpp)
//   lib/jt9fano.f90, lib/fano232.f90 (Fano decoder, translated by K1JT from the C routine by Phil Karn, KA9Q)
//   lib/packjt.f90 unpackmsg (jt9_pack.cpp)
// Changes against upstream are listed in UPSTREAM.md / notes-jt9.md.
//
// Copyright (C) 2001-2025 Joseph H. Taylor, Jr., K1JT and the WSJT Development Group.
// Fano decoder: Copyright 1994, Phil Karn, KA9Q (the C original); minor modifications K1JT.
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU General
// Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option)
// any later version. It is distributed WITHOUT ANY WARRANTY. See LICENSE.

#include "jt9_pack.h"
#include "jt9_tables.h"
#include "wm_common.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace wm {
namespace {

using namespace jt9;

const int kNsmax = 6827;             // constants.f90 NSMAX
const int kNsps = 6912;              // samples per symbol
const int kStep = kNsps / 2;         // half symbol, samples
const int kPeriod = 60 * kRate;      // samples in a T/R period
const int kNzhsym = 181;             // half-symbol spectra used (jt9.f90 reads data until nhsym >= 181)
const int kNpts = 182 * kStep;       // samples handed to the big FFT (what has arrived when ss(181) is complete)
const int kNfft1 = 653184;           // downsam9.f90 NFFT1
const int kNfft2 = 1512;             // downsam9.f90 NFFT2
const int kNz3 = 1360;               // softsym.f90 NZ3
const double kTwopi = 6.283185307;   // twkfreq.f90 uses this truncated value
const double kDf3 = 12000.0 / 16384; // symspec.f90 df3 = 0.732 Hz

int nint(double x) { return (int)std::lround(x); }

// lib/pctile.f90 (the Fortran sorts with shell.f90; the result is the same as a full sort): element j = nint(n*0.01*npct)
double pctile(const double *x, int npts, int npct) {
    if (npts < 0 || npct < 0 || npct > 100) return 1.0;
    std::vector<double> t(x, x + npts);
    std::sort(t.begin(), t.end());
    int j = nint(npts * 0.01 * npct);
    if (j < 1) j = 1;
    if (j > npts) j = npts;
    return t[j - 1];
}

// ---------------------------------------------------------------------------------------------------------------
// lib/fano232.f90 + jt9fano.f90 (metric table and the call)
struct FanoTable {
    int mettab[256][2]; // mettab(-128:127,0:1): [value+128]
    int ndelta;
    FanoTable() {
        const double bias = 0.5, scale = 50;
        ndelta = nint(3.4 * scale);
        const int ib = 160, slope = 2;
        for (int i = 0; i < 256; i++) {
            mettab[i][0] = nint(scale * (kXx0[i] - bias));
            if (i > ib) mettab[i][0] = mettab[ib][0] - slope * (i - ib);
            if (i >= 1) mettab[256 - i][1] = mettab[i][0];
        }
        mettab[0][1] = mettab[1][1];
    }
};

// Sequential decoder for K=32, r=1/2 convolutional code using the Fano algorithm. Returns true if it finished within
// maxcycles; dat gets 13 bytes. (fano232.f90; "nbits" here is the number of message bits plus the 31-bit tail)
bool fano232(const int8_t *symbol, int nbits, const FanoTable &T, long maxcycles, uint8_t *dat, long *ncycles) {
    const int MAXBITS = 103;
    if (nbits > MAXBITS) return false;
    uint32_t nstate[MAXBITS + 1];
    int gamma[MAXBITS + 1];
    int metrics[4][MAXBITS];
    int tm[2][MAXBITS + 1];
    int ii[MAXBITS + 1];
    const int ntail = nbits - 31;
    const int ndelta = T.ndelta;
    const uint32_t p1 = (uint32_t)kNpoly1, p2 = (uint32_t)kNpoly2;

    for (int np = 0; np < nbits; np++) {
        int i4a = symbol[2 * np] + 128, i4b = symbol[2 * np + 1] + 128;
        metrics[0][np] = T.mettab[i4a][0] + T.mettab[i4b][0];
        metrics[1][np] = T.mettab[i4a][0] + T.mettab[i4b][1];
        metrics[2][np] = T.mettab[i4a][1] + T.mettab[i4b][0];
        metrics[3][np] = T.mettab[i4a][1] + T.mettab[i4b][1];
    }
    auto lsym_of = [&](uint32_t st) {
        uint32_t n = st & p1;
        n ^= n >> 16;
        int l = parity8((int)((n ^ (n >> 8)) & 255));
        n = st & p2;
        n ^= n >> 16;
        return l + l + parity8((int)((n ^ (n >> 8)) & 255));
    };

    int np = 0;
    nstate[0] = 0;
    {
        int lsym = lsym_of(nstate[0]);
        int m0 = metrics[lsym][0], m1 = metrics[3 ^ lsym][0];
        if (m0 > m1) { tm[0][0] = m0; tm[1][0] = m1; }
        else { tm[0][0] = m1; tm[1][0] = m0; nstate[0] += 1; }
    }
    ii[0] = 0;
    gamma[0] = 0;
    int nt = 0;
    long i;
    long N = (long)nbits * maxcycles;
    bool done = false;
    for (i = 1; i <= N; i++) {
        int ngamma = gamma[np] + tm[ii[np]][np];
        if (ngamma >= nt) {
            if (gamma[np] < nt + ndelta) nt = nt + ndelta * ((ngamma - nt) / ndelta);
            gamma[np + 1] = ngamma;
            nstate[np + 1] = nstate[np] << 1;
            np++;
            if (np == nbits) { done = true; break; }
            int lsym = lsym_of(nstate[np]);
            if (np >= ntail) {
                tm[0][np] = metrics[lsym][np];
            } else {
                int m0 = metrics[lsym][np], m1 = metrics[3 ^ lsym][np];
                if (m0 > m1) { tm[0][np] = m0; tm[1][np] = m1; }
                else { tm[0][np] = m1; tm[1][np] = m0; nstate[np] += 1; }
            }
            ii[np] = 0;
        } else {
            for (;;) {
                bool noback = false;
                if (np == 0) noback = true;
                if (np > 0 && gamma[np - 1] < nt) noback = true;
                if (noback) {
                    nt -= ndelta;
                    if (ii[np] != 0) { ii[np] = 0; nstate[np] ^= 1; }
                    break;
                }
                np--;
                if (np < ntail && ii[np] != 1) {
                    ii[np]++;
                    nstate[np] ^= 1;
                    break;
                }
            }
        }
    }
    if (!done) i = N; // the Fortran sets i = nbits*maxcycles after the loop
    int nbytes = (nbits + 7) / 8;
    int q = 7;
    for (int j = 0; j < nbytes - 1; j++) {
        dat[j] = (uint8_t)(nstate[q] & 255);
        q += 8;
    }
    dat[nbytes - 1] = 0;
    *ncycles = i + 1;
    return true;
}

// lib/jt9fano.f90: soft symbols (207, unscrambled) -> message text ("" if none)
std::string jt9fano(const int8_t *soft, int limit) {
    static const FanoTable T;
    const int nbits = 72;
    uint8_t dec[13];
    long ncycles = 0;
    fano232(soft, nbits + 31, T, limit, dec, &ncycles);
    std::string msg;
    if (ncycles < (long)(nbits + 31) * limit) {
        int bits[72];
        for (int i = 0; i < 9; i++)
            for (int j = 0; j < 8; j++) bits[8 * i + j] = (dec[i] >> (7 - j)) & 1;
        int words[12];
        for (int i = 0; i < 12; i++) {
            int n = 0;
            for (int j = 0; j < 6; j++) n = (n << 1) | bits[6 * i + j];
            words[i] = n;
        }
        msg = unpackmsg(words);
        if ((msg + "  ").find("000AAA ") != std::string::npos) msg.clear(); // index(msg,'000AAA ') on the padded message
    }
    return msg;
}

// ---------------------------------------------------------------------------------------------------------------
// lib/twkfreq.f90: adjust frequency of complex data. a(1): Hz; a(2): Hz/(0.5*TxT); a(3): quadratic term.
void twkfreq(const cplx *c3, cplx *c4, int npts, double fsample, const double *a) {
    cplx w(1.0, 0.0);
    double x0 = 0.5 * (npts + 1);
    double s = 2.0 / npts;
    for (int i = 1; i <= npts; i++) {
        double x = s * (i - x0);
        double p2 = 1.5 * x * x - 0.5;
        double dphi = (a[0] + x * a[1] + p2 * a[2]) * (kTwopi / fsample);
        cplx wstep(std::cos(dphi), std::sin(dphi));
        w = w * wstep;
        c4[i - 1] = w * c3[i - 1];
    }
}

// lib/fchisq.f90: minus the sync power (no caching between calls, see notes)
double fchisq(const cplx *c3, int npts, double fsample, const double *a) {
    std::vector<cplx> c4(npts);
    twkfreq(c3, c4.data(), npts, fsample, a);
    const int nspsd = 16;
    double sum1 = 0, sum0 = 0;
    int k = -1;
    for (int i = 0; i < 85; i++) {
        cplx z(0, 0);
        for (int j = 0; j < nspsd; j++) { k++; z += c4[k]; }
        double pp = z.real() * z.real() + z.imag() * z.imag();
        if (kIsync[i] == 1) sum1 += pp; else sum0 += pp;
    }
    (void)sum0;
    return -(sum1 / 10000.0);
}

// lib/afc9.f90 shft: circular shift by nint(a3a) samples; zero fill for n > 0 (as upstream: nothing for n < 0)
void shft(const cplx *c3a, double a3a, double *a3, cplx *c3) {
    *a3 = a3a;
    int n = nint(*a3);
    int N = kNz3;
    for (int i = 0; i < N; i++) {
        int j = ((i + n) % N + N) % N;
        c3[i] = c3a[j];
    }
    if (n > 0)
        for (int i = N - n; i < N; i++) if (i >= 0) c3[i] = 0.0;
}

// lib/afc9.f90: find delta f, f-dot and an extra time shift by coordinate descent on the sync power.
// Returns false if the search went numerically wrong.
bool afc9(cplx *c3a, int npts, double fsample, double *a, double *syncpk) {
    a[0] = a[1] = a[2] = 0.0;
    double deltaa[3] = {1.736, 1.736, 1.0};
    const int nterms = 3;
    double chisqr = 0.0, chisqr0 = 1.0e6;
    std::vector<cplx> c3(c3a, c3a + npts);
    double a3 = a[2];
    const int kCap = 2000; // guard against endless loops on flat data (the Fortran has none)
    for (int iter = 1; iter <= 4; iter++) {
        for (int j = 0; j < nterms; j++) {
            if (a[2] != a3) shft(c3a, a[2], &a3, c3.data());
            double chisq1 = fchisq(c3.data(), npts, fsample, a);
            double fn = 0.0;
            double delta = deltaa[j];
            double chisq2;
            int guard = 0;
            for (;;) { // label 10
                a[j] += delta;
                if (a[2] != a3) shft(c3a, a[2], &a3, c3.data());
                chisq2 = fchisq(c3.data(), npts, fsample, a);
                if (chisq2 == chisq1 && ++guard < kCap) continue;
                break;
            }
            if (chisq2 > chisq1) {
                delta = -delta;
                a[j] += delta;
                double tmp = chisq1;
                chisq1 = chisq2;
                chisq2 = tmp;
            }
            double chisq3;
            for (int g = 0;; g++) { // label 20
                fn += 1.0;
                a[j] += delta;
                if (a[2] != a3) shft(c3a, a[2], &a3, c3.data());
                chisq3 = fchisq(c3.data(), npts, fsample, a);
                if (chisq3 < chisq2 && g < kCap) { chisq1 = chisq2; chisq2 = chisq3; continue; }
                break;
            }
            delta = delta * (1.0 / (1.0 + (chisq1 - chisq2) / (chisq3 - chisq2)) + 0.5);
            a[j] -= delta;
            if (j < 2) deltaa[j] = deltaa[j] * fn / 3.0;
            if (!std::isfinite(a[j])) return false;
        }
        if (a[2] != a3) shft(c3a, a[2], &a3, c3.data());
        chisqr = fchisq(c3.data(), npts, fsample, a);
        if (chisqr / chisqr0 > 0.99) break;
        chisqr0 = chisqr;
    }
    *syncpk = -chisqr;
    for (int i = 0; i < npts; i++) c3a[i] = c3[i];
    return std::isfinite(*syncpk);
}

// ---------------------------------------------------------------------------------------------------------------
struct Soft {
    double syncpk = 0, snrdb = 0, xdt = 0, freq = 0, drift = 0, a3 = 0, schk = 0;
    int8_t sym[207];
};

struct PassParams { int limit; double ccflim, red2lim, sync_min, schk_min; };
// Pass 2 (depth 3): deeper Fano search (100000), lower candidate thresholds. Values chosen by the sweep in notes-jt9.md.
static const PassParams kPass2 = {100000, 2.2, 1.2, 0.8, 1.2};

class Jt9Decoder : public StreamDecoder {
public:
    Jt9Decoder(const Options &o, const Emit &e) : o_(o), emit_(e) {
        skip_ = (long long)std::llround(o.offset * kRate);
        if (skip_ < 0) skip_ = 0;
        buf_.reserve(kPeriod);
    }
    void feed(const float *x, size_t n) override {
        for (size_t i = 0; i < n; i++) {
            long long g = total_ + (long long)i;
            if (g < skip_) continue;
            buf_.push_back(x[i]);
            if ((int)buf_.size() == kPeriod) {
                decode_period();
                buf_.clear();
                period_++;
            }
        }
        total_ += (long long)n;
    }
    void finish() override {
        if (buf_.size() >= (size_t)(45 * kRate)) { // a cut-off last period still decodes if it holds most of the signal
            buf_.resize(kPeriod, 0.0f);
            decode_period();
        }
        buf_.clear();
    }

private:
    Options o_;
    Emit emit_;
    long long skip_ = 0, total_ = 0, period_ = 0;
    std::vector<float> buf_;

    // lib/symspec.f90 (bLowSidelobes false, as jt9.f90 runs it): ss(ihsym, i) for ihsym = 1..181
    void symspec(std::vector<float> &ss) {
        const int nfft3 = 16384;
        ss.assign((size_t)kNzhsym * (kNsmax + 1), 0.0f);
        std::vector<cplx> t(nfft3);
        const double fac = std::pow(1.0 / nfft3, 2);
        for (int ihsym = 1; ihsym <= kNzhsym; ihsym++) {
            int ja = ihsym * kStep;
            for (int i = 0; i < nfft3; i++) {
                int j = ja + i - (nfft3 - 1); // 1-based sample index
                double v = 0.0;
                if (j >= 1 && j <= kPeriod) v = 0.1 * buf_[j - 1];
                t[i] = cplx(v, 0.0);
            }
            fft(t.data(), nfft3, -1);
            for (int i = 1; i <= kNsmax; i++) {
                const cplx &c = t[i - 1];
                ss[(size_t)(ihsym - 1) * (kNsmax + 1) + i] = (float)(fac * (c.real() * c.real() + c.imag() * c.imag()));
            }
        }
    }

    // lib/sync9.f90. ss1 index [1..kNzhsym]; arrays are 1-based here.
    void sync9(const std::vector<float> &ss, int nzhsym, int lag1, int lag2, int ia, int ib, std::vector<double> &ccfred,
               std::vector<double> &red2) {
        ccfred.assign(kNsmax + 2, 0.0);
        red2.assign(kNsmax + 2, 0.0);
        std::vector<double> ss1(nzhsym);
        for (int i = ia; i <= ib; i++) {
            for (int j = 0; j < nzhsym; j++) ss1[j] = ss[(size_t)j * (kNsmax + 1) + i];
            double xmed = pctile(ss1.data(), nzhsym, 40);
            for (int j = 0; j < nzhsym; j++) {
                ss1[j] = ss1[j] / xmed - 1.0;
                if (ss1[j] > 3.0) ss1[j] = 3.0;
            }
            double sbase = pctile(ss1.data(), nzhsym, 45);
            double sq0 = 0;
            for (int j = 0; j < nzhsym; j++) { ss1[j] -= sbase; sq0 += ss1[j] * ss1[j]; }
            double smax = 0.0;
            for (int lag = lag1; lag <= lag2; lag++) {
                double sum1 = 0.0;
                for (int j = 0; j < 16; j++) {
                    int k = kIi2[j] + lag;
                    if (k >= 1 && k <= nzhsym) sum1 += ss1[k - 1];
                }
                if (sum1 > smax) smax = sum1;
            }
            ccfred[i] = smax;
        }
        {
            double xmed = pctile(&ccfred[ia], ib - ia + 1, 50);
            if (xmed <= 0.0) xmed = 1.0;
            for (int i = 0; i < kNsmax + 2; i++) ccfred[i] = 2.0 * ccfred[i] / xmed;
        }
        std::vector<double> savg(kNsmax + 2, 0.0), savg2(kNsmax + 2, 0.0), sq(kNsmax + 2, 0.0);
        for (int j = 0; j < nzhsym; j++)
            for (int i = ia; i <= ib; i++) savg[i] += ss[(size_t)j * (kNsmax + 1) + i];
        for (int i = ia; i <= ib; i++) savg[i] /= nzhsym;
        double smo[31]; // smo(-5:25), index j+5
        for (int j = 0; j <= 20; j++) smo[j + 5] = 1.0 / 21.0;
        for (int j = -5; j <= -1; j++) smo[j + 5] = -(1.0 / 21.0) * (21.0 / 10.0);
        for (int j = 21; j <= 25; j++) smo[j + 5] = smo[0];
        for (int i = ia; i <= ib; i++) {
            double sm = 0.0;
            for (int j = -5; j <= 25; j++)
                if (i + j >= 1 && i + j < kNsmax) sm += smo[j + 5] * savg[i + j];
            savg2[i] = sm;
            sq[i] = sm * sm;
        }
        double sq0 = pctile(&sq[ia], ib - ia + 1, 20);
        double rms = std::sqrt(sq0);
        for (int i = ia; i <= ib; i++) savg2[i] = savg2[i] / (5.0 * rms);
        for (int i = ia + 11; i <= ib - 10; i++) {
            double ref = std::max(savg2[i - 10], savg2[i + 10]);
            red2[i] = savg2[i] - ref;
            if (red2[i] < -99.0) red2[i] = -99.0;
            if (red2[i] > 99.0) red2[i] = 99.0;
        }
    }

    // big FFT of the whole period once (downsam9.f90 newdat part): c1 and the 1-Hz power sums s(1..5000)
    std::vector<cplx> c1_;
    std::vector<double> s_;
    void bigfft() {
        std::vector<double> x(kNfft1, 0.0);
        int npts = std::min(kNpts, kNfft1);
        for (int i = 0; i < npts; i++) x[i] = buf_[i];
        c1_.assign(kNfft1 / 2 + 1, cplx(0, 0));
        rfft(x.data(), kNfft1, c1_.data());
        double df1 = 12000.0 / kNfft1;
        int nadd = (int)(1.0 / df1);
        s_.assign(5001, 0.0);
        for (int i = 1; i <= 5000; i++) {
            int j = (int)((i - 1) / df1);
            for (int n = 1; n <= nadd; n++) {
                j++;
                s_[i] += c1_[j].real() * c1_[j].real() + c1_[j].imag() * c1_[j].imag();
            }
        }
    }

    // lib/downsam9.f90: mix down from fpk and decimate to 16 samples per symbol (c2: 1512 samples)
    void downsam9(double fpk, cplx *c2) {
        double df1 = 12000.0 / kNfft1;
        int nh2 = kNfft2 / 2;
        int nf = nint(fpk);
        int i0 = (int)(fpk / df1);
        const int nw = 100;
        int ia = std::max(1, nf - nw), ib = std::min(5000, nf + nw);
        double avenoise = pctile(&s_[ia], ib - ia + 1, 40);
        double fac = std::sqrt(1.0 / avenoise);
        for (int i = 0; i < kNfft2; i++) {
            int j = i0 + i;
            if (i > nh2) j -= kNfft2;
            c2[i] = (j >= 0 && j <= kNfft1 / 2) ? fac * c1_[j] : cplx(0, 0);
        }
        fft(c2, kNfft2, +1); // four2a(c2,NFFT2,1,1,1): back to the time domain
    }

    // lib/peakdt9.f90 with lib/getlags.f90 (nsps8 = 864): find DT
    void peakdt9(const cplx *c2, int nspsd, cplx *c3, double *xdt) {
        std::vector<double> p(3301, 0.0);
        const int i0 = 5 * nspsd;
        for (int i = 0; i < kNfft2; i++) {
            cplx z(0, 0);
            for (int k = std::max(i - (nspsd - 1), 0); k <= i; k++) z += c2[k];
            z *= 1.0e-3;
            p[i0 + i] = z.real() * z.real() + z.imag() * z.imag();
        }
        const int lag1 = 39, lag2 = 291, lag0 = 123;
        const double tsymbol = 864 / 1500.0;
        const double dtlag = tsymbol / nspsd;
        double smax = 0.0;
        int lagpk = 0;
        for (int lag = lag1; lag <= lag2; lag++) {
            double sum0 = 0, sum1 = 0;
            int j = -nspsd;
            for (int i = 1; i <= 85; i++) {
                j += nspsd;
                if (kIsync[i - 1] == 1) sum1 += p[j + lag]; else sum0 += p[j + lag];
            }
            double ss = (sum1 / 16.0) / (sum0 / 69.0) - 1.0;
            if (ss > smax) { smax = ss; lagpk = lag; }
        }
        *xdt = (lagpk - lag0) * dtlag;
        for (int i = 0; i < kNz3; i++) {
            int j = i + lagpk - i0 - nspsd + 1;
            c3[i] = (j >= 0 && j < kNfft2) ? c2[j] : cplx(0, 0);
        }
    }

    // lib/symspec2.f90 + chkss2.f90: soft symbols from the final downsampled data
    void symspec2(cplx *c5, double fsample, double freq, double drift, double *snrdb, double *schk, int8_t *soft) {
        (void)freq; (void)drift;
        const int nsps8 = 864, nspsd = 16;
        double aa[3] = {-1500.0 / nsps8, 0.0, 0.0};
        double ss2[9][85];
        double ss3[8][69];
        for (int i = 0; i <= 8; i++) {
            if (i >= 1) twkfreq(c5, c5, kNz3, fsample, aa);
            int m = 0, k = -1;
            for (int j = 1; j <= 85; j++) {
                cplx z(0, 0);
                for (int n = 1; n <= nspsd; n++) { k++; z += c5[k]; }
                ss2[i][j - 1] = z.real() * z.real() + z.imag() * z.imag();
                if (i >= 1 && kIsync[j - 1] == 0) { m++; ss3[i - 1][m - 1] = ss2[i][j - 1]; }
            }
        }
        { // chkss2.f90
            double sum = 0;
            for (int i = 0; i < 9; i++) for (int j = 0; j < 85; j++) sum += ss2[i][j];
            double ave = sum / (9 * 85);
            double s1 = 0;
            for (int i = 0; i < 16; i++) {
                int j = kIi[i];
                if (j <= 85) s1 += ss2[0][j - 1] / ave - 1.0;
            }
            *schk = s1 / 16.0;
        }
        double ss = 0, sig = 0;
        for (int j = 0; j < 69; j++) {
            double smax = 0;
            for (int i = 0; i < 8; i++) { smax = std::max(smax, ss3[i][j]); ss += ss3[i][j]; }
            sig += smax;
            ss -= smax;
        }
        double ave = ss / (69 * 7);
        for (int i = 0; i < 8; i++) for (int j = 0; j < 69; j++) ss3[i][j] /= ave;
        sig /= 69.0;
        double t = std::max(1.0, sig - 1.0);
        *snrdb = 10.0 * std::log10(t) - 61.3; // db(t) - 61.3 (t >= 1)
        const double scale = 10.0;
        int k = 0;
        for (int j = 0; j < 69; j++) {
            for (int m = 2; m >= 0; m--) {
                double r1, r0;
                if (m == 2) {
                    r1 = std::max(std::max(ss3[4][j], ss3[5][j]), std::max(ss3[6][j], ss3[7][j]));
                    r0 = std::max(std::max(ss3[0][j], ss3[1][j]), std::max(ss3[2][j], ss3[3][j]));
                } else if (m == 1) {
                    r1 = std::max(std::max(ss3[2][j], ss3[3][j]), std::max(ss3[4][j], ss3[5][j]));
                    r0 = std::max(std::max(ss3[0][j], ss3[1][j]), std::max(ss3[6][j], ss3[7][j]));
                } else {
                    r1 = std::max(std::max(ss3[1][j], ss3[2][j]), std::max(ss3[4][j], ss3[7][j]));
                    r0 = std::max(std::max(ss3[0][j], ss3[3][j]), std::max(ss3[5][j], ss3[6][j]));
                }
                int i4 = nint(scale * (r1 - r0));
                if (i4 < -127) i4 = -127;
                if (i4 > 127) i4 = 127;
                soft[k++] = (int8_t)i4;
            }
        }
    }

    // lib/softsym.f90. Returns false if the numbers went wrong.
    bool softsym(double fpk, Soft *r) {
        const int nspsd = 16, ndown = 864 / nspsd;
        std::vector<cplx> c2(kNfft2), c3(kNz3), c5(kNz3);
        downsam9(fpk, c2.data());
        peakdt9(c2.data(), nspsd, c3.data(), &r->xdt);
        double fsample = 1500.0 / ndown;
        double a[3];
        if (!afc9(c3.data(), kNz3, fsample, a, &r->syncpk)) return false;
        r->freq = fpk - a[0];
        r->drift = -2.0 * a[1];
        r->a3 = a[2];
        a[2] = 0.0;
        twkfreq(c3.data(), c5.data(), kNz3, fsample, a);
        int8_t scr[207];
        symspec2(c5.data(), fsample, r->freq, r->drift, &r->snrdb, &r->schk, scr);
        int8_t un[208];
        interleave9(scr, -1, un);
        un[206] = 0;
        memcpy(r->sym, un, 207);
        return std::isfinite(r->snrdb) && std::isfinite(r->freq) && std::isfinite(r->xdt);
    }

    // lib/jt9_decode.f90 (decode), one pass nqd = 0 over the whole band
    void decode_period() {
        std::vector<float> ss;
        symspec(ss);
        const double df3 = 1500.0 / 2048.0;
        const int nf0 = 0;
        int ia = std::max(1, nint((o_.fmin - nf0) / df3));
        int ib = std::min(kNsmax, nint((o_.fmax - nf0) / df3));
        if (ib - ia < 40) return;
        const double tstep = 0.5 * kNsps / 12000.0;
        int lag1 = -(int)(2.5 / tstep + 0.9999);
        int lag2 = (int)(5.0 / tstep + 0.9999);
        std::vector<double> ccfred, red2;
        sync9(ss, kNzhsym, lag1, lag2, ia, ib, ccfred, red2);

        const int nsps8 = 864;
        const double df8 = 1500.0 / nsps8;
        // Passes. Pass 1 is WSJT-X's nqd=0 pass for the chosen depth (jt9_decode.f90: limit, ccflim, red2lim; the
        // sync >= 1.0 and schk >= 1.5 tests). Pass 2 (depth 3 only) is an addition of this helper, see notes-jt9.md.
        typedef PassParams Pass;
        std::vector<Pass> passes;
        if (o_.depth <= 1) passes.push_back({5000, 3.0, 1.6, 1.0, 1.5});
        else if (o_.depth == 2) passes.push_back({10000, 2.7, 1.6, 1.0, 1.5});
        else {
            passes.push_back({30000, 2.5, 1.6, 1.0, 1.5});
            passes.push_back(kPass2);
        }
        std::vector<char> done(kNsmax + 2, 0);
        std::vector<Decode> found;
        bool have_fft = false;
        double period_start = (double)(skip_ + period_ * (long long)kPeriod) / kRate;
        for (const Pass &P : passes) {
            std::vector<char> ccfok(kNsmax + 2, 0);
            for (int i = ia; i <= ib; i++) ccfok[i] = (ccfred[i] > P.ccflim && red2[i] > P.red2lim) ? 1 : 0;
            double fgood = 0.0;
            for (int i = ia; i <= ib; i++) {
                if (done[i] || !ccfok[i]) continue;
                double f = (i - 1) * df3;
                if (!(ccfred[i] >= P.ccflim && std::fabs(f - fgood) > 10.0 * df8)) continue;
                if (!have_fft) { bigfft(); have_fft = true; }
                Soft r;
                double fpk = nf0 + df3 * (i - 1);
                if (!softsym(fpk, &r)) continue;
                double sync = (r.syncpk + 1) / 4.0;
                if (sync < P.sync_min || r.schk < P.schk_min) continue;
                std::string msg = jt9fano(r.sym, P.limit);
                if (msg.empty()) continue;
                // The same message found again within one signal width (15 Hz) is the same signal: this can only
                // happen in pass 2, which starts from bins that pass 1 did not mark as done.
                bool dup = false;
                for (const Decode &o : found)
                    if (o.text == msg && std::fabs(o.freq - r.freq) < 20.0) dup = true;
                if (dup) {
                    int iaa = std::max(1, i - 12), ibb = std::min(kNsmax, i + 22);
                    for (int q = iaa; q <= ibb; q++) { ccfok[q] = 0; done[q] = 1; }
                    continue;
                }
                Decode d;
                d.mode = "JT9";
                // CHANGE (see notes-jt9.md): WSJT-X prints the coarse peakdt9 value; afc9 finds a further time shift
                // a(3), in samples of the 27.78 Hz series (0.036 s), that upstream computes and discards. It is added.
                double dtref = r.xdt + nint(r.a3) * (864 / 1500.0 / 16);
                d.dt = dtref;
                d.freq = r.freq;
                d.snr = nint(r.snrdb);
                d.text = msg;
                d.t_start = period_start + 1.0 + dtref; // JT9 dt is counted from 1.0 s into the minute
                emit_(d);
                found.push_back(d);
                int iaa = std::max(1, i - 1), ibb = std::min(kNsmax, i + 22);
                fgood = f;
                for (int q = iaa; q <= ibb; q++) { ccfok[q] = 0; done[q] = 1; }
            }
        }
    }
};

} // namespace

StreamDecoder *make_jt9_decoder(const Options &o, const Emit &e) { return new Jt9Decoder(o, e); }

} // namespace wm
