// weakmodes: FT2L decoder ("FT2" as implemented by wsjtx-ft2libre).
//
// NOTE: FT2L is a REVERSE-ENGINEERED format. Its author wrote it from captures of the closed-source Decodium program
// and says the published ft2.it description did not match. It may not match other FT2 software, and this helper has
// not been tested against real FT2 traffic.
//
// C++ port of the decoder in wsjtx-ft2libre 356be92 (a WSJT-X derivative, GPL-3.0):
//   lib/ft2l_decode.f90 (subroutine decode), lib/ft2l/getcandidates2l.f90, lib/ft2l/ft2l_baseline.f90,
//   lib/ft2l/ft2l_downsample.f90, lib/ft2l/sync2ld.f90, lib/ft2l/get_ft2l_bitmetrics.f90,
//   lib/ft2l/subtractft2l.f90, lib/twkfreq1.f90, lib/nuttal_window.f90, lib/pctile.f90 (shell.f90),
//   lib/polyfit.f90 and normalizebmet (lib/ft8/ft8b.f90).
// WSJT-X, Copyright (C) 2001-2025 by Joe Taylor, K1JT, and the WSJT Development Group; wsjtx-ft2libre changes by
// its authors. GPL-3.0-or-later, see LICENSE and UPSTREAM.md.
//
// Changes against the upstream routines: no my-call / his-call (so the AP passes that need a station call are
// absent; only the CQ a-priori pass remains), no a-priori contest modes, no early (partial period) decoding, no
// timer calls. polyfit solves the normal equations by Gaussian elimination instead of Cramer's rule with determ().
// The period buffer is fed from the stream; each 3.75 s period is decoded when it is complete.
#include "wm_common.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ft2_wave.h"
#include "ldpc174_91.h"

namespace wm {

namespace {

// ft2l_params.f90
const int KK = 91, ND = 87, NS = 16, NN = 103, NSPS = 288, NMAX = FT2_NMAX, NFFT1 = 1152, NH1 = NFFT1 / 2, NSTEP = NSPS;
const int NHSYM = (NMAX - NFFT1) / NSTEP; // 152
const int NDOWN = 9, NSS = NSPS / NDOWN, NDMAX = NMAX / NDOWN;
const int MAXCAND = 200;
const double kPi = 3.14159265358979323846;

const int icos4[4][4] = {{0, 1, 3, 2}, {1, 0, 2, 3}, {2, 3, 1, 0}, {3, 2, 0, 1}};

// ---- helpers: nuttal_window, pctile, polyfit, normalizebmet -----------------------------------------------------------

void nuttal_window(double *win, int n) {
    const double a0 = 0.3635819, a1 = -0.4891775, a2 = 0.1365995, a3 = -0.0106411;
    for (int i = 1; i <= n; i++)
        win[i - 1] = a0 + a1 * std::cos(2 * kPi * (i - 1) / n) + a2 * std::cos(4 * kPi * (i - 1) / n) + a3 * std::cos(6 * kPi * (i - 1) / n);
}

// pctile: value at the npct-th percentile of x (sorted copy), j=nint(npts*0.01*npct) clamped to 1..npts
float pctile(const float *x, int npts, int npct) {
    if (npts < 0 || npct < 0 || npct > 100) return 1.0f;
    std::vector<float> tmp(x, x + npts);
    std::sort(tmp.begin(), tmp.end());
    int j = (int)std::lrint(npts * 0.01 * npct);
    if (j < 1) j = 1;
    if (j > npts) j = npts;
    return tmp[j - 1];
}

// polyfit(mode=0): least-squares polynomial with nterms coefficients, a[0] constant term
void polyfit(const double *x, const double *y, int npts, int nterms, double *a) {
    double sumx[10] = {0}, sumy[10] = {0};
    int nmax = 2 * nterms - 1;
    for (int i = 0; i < npts; i++) {
        double xterm = 1.0;
        for (int n = 0; n < nmax; n++) { sumx[n] += xterm; xterm *= x[i]; }
        double yterm = y[i];
        for (int n = 0; n < nterms; n++) { sumy[n] += yterm; yterm *= x[i]; }
    }
    double m[5][6];
    for (int j = 0; j < nterms; j++) {
        for (int k = 0; k < nterms; k++) m[j][k] = sumx[j + k];
        m[j][nterms] = sumy[j];
    }
    for (int c = 0; c < nterms; c++) { // Gaussian elimination, partial pivoting
        int piv = c;
        for (int r = c + 1; r < nterms; r++)
            if (std::fabs(m[r][c]) > std::fabs(m[piv][c])) piv = r;
        if (m[piv][c] == 0.0) { for (int i = 0; i < nterms; i++) a[i] = 0.0; return; }
        if (piv != c) for (int k = 0; k <= nterms; k++) std::swap(m[piv][k], m[c][k]);
        for (int r = 0; r < nterms; r++) {
            if (r == c) continue;
            double f = m[r][c] / m[c][c];
            for (int k = c; k <= nterms; k++) m[r][k] -= f * m[c][k];
        }
    }
    for (int i = 0; i < nterms; i++) a[i] = m[i][nterms] / m[i][i];
}

void normalizebmet(float *bmet, int n) {
    float av = 0, av2 = 0;
    for (int i = 0; i < n; i++) { av += bmet[i]; av2 += bmet[i] * bmet[i]; }
    av /= n;
    av2 /= n;
    float var = av2 - av * av;
    float sig = var > 0.0f ? std::sqrt(var) : std::sqrt(av2);
    for (int i = 0; i < n; i++) bmet[i] /= sig;
}

// ---- ft2l_baseline: s is 1-based (s[1..NH1]), converted to dB in place for ia..ib; sbase likewise ---------------------

void ft2l_baseline(float *s, int nfa, int nfb, float *sbase) {
    const int nseg = 10, npct = 10;
    float df = 12000.0f / NFFT1;
    int ia = std::max((int)std::lrint(200.0f / df), nfa);
    int ib = std::min(NH1, nfb);
    for (int i = ia; i <= ib; i++) s[i] = 10.0f * std::log10(s[i]);
    const int nterms = 5;
    int nlen = (ib - ia + 1) / nseg;
    int i0 = (ib - ia + 1) / 2;
    double x[1001], y[1001];
    int k = 0;
    for (int n = 1; n <= nseg; n++) {
        int ja = ia + (n - 1) * nlen, jb = ja + nlen - 1;
        float base = pctile(&s[ja], nlen, npct);
        for (int i = ja; i <= jb; i++) {
            if (s[i] <= base) {
                if (k < 1000) k++;
                x[k] = i - i0;
                y[k] = s[i];
            }
        }
    }
    int kz = k;
    double a[5] = {0, 0, 0, 0, 0};
    polyfit(&x[1], &y[1], kz, nterms, a);
    for (int i = ia; i <= ib; i++) {
        double t = i - i0;
        double v = a[0] + t * (a[1] + t * (a[2] + t * (a[3] + t * a[4]))) + 0.65;
        sbase[i] = (float)std::pow(10.0, v / 10.0);
    }
}

// ---- getcandidates2l ------------------------------------------------------------------------------------------------

struct Cand { float f, s; };

void getcandidates2l(const float *dd, float fa, float fb, float syncmin, float nfqso, std::vector<Cand> &candidate) {
    static std::vector<double> window;
    if (window.empty()) { window.assign(NFFT1, 0.0); nuttal_window(window.data(), NFFT1); }
    std::vector<float> savg(NH1 + 2, 0.f), savsm(NH1 + 2, 0.f), sbase(NH1 + 2, 0.f);
    float df = 12000.0f / NFFT1;
    const double fac = 1.0 / 300.0;
    std::vector<double> x(NFFT1);
    std::vector<cplx> cx(NFFT1 / 2 + 1);
    for (int j = 1; j <= NHSYM; j++) {
        int ia = (j - 1) * NSTEP + 1;
        int ib = ia + NFFT1 - 1;
        if (ib > NMAX) break;
        for (int i = 0; i < NFFT1; i++) x[i] = fac * dd[ia - 1 + i] * window[i];
        rfft(x.data(), NFFT1, cx.data());
        for (int i = 1; i <= NH1; i++) savg[i] += (float)std::norm(cx[i]);
    }
    for (int i = 1; i <= NH1; i++) savg[i] /= NHSYM;
    for (int i = 8; i <= NH1 - 7; i++) {
        float sum = 0;
        for (int k = i - 7; k <= i + 7; k++) sum += savg[k];
        savsm[i] = sum / 15.0f;
    }
    int nfa = (int)(fa / df);
    if (nfa < (int)std::lrint(200.0f / df)) nfa = (int)std::lrint(200.0f / df);
    int nfb = (int)(fb / df);
    if (nfb > (int)std::lrint(4910.0f / df)) nfb = (int)std::lrint(4910.0f / df);
    candidate.clear();
    if (nfb <= nfa + 1) return;
    ft2l_baseline(savg.data(), nfa, nfb, sbase.data());
    for (int i = nfa; i <= nfb; i++)
        if (sbase[i] <= 0) return;
    for (int i = nfa; i <= nfb; i++) savsm[i] /= sbase[i];
    float f_offset = -1.5f * 12000.0f / NSPS;
    std::vector<Cand> t;
    for (int i = nfa + 1; i <= nfb - 1; i++) {
        if (savsm[i] >= savsm[i - 1] && savsm[i] >= savsm[i + 1] && savsm[i] >= syncmin) {
            float den = savsm[i - 1] - 2 * savsm[i] + savsm[i + 1];
            float del = 0;
            if (den != 0.0f) del = 0.5f * (savsm[i - 1] - savsm[i + 1]) / den;
            float fpeak = (i + del) * df + f_offset;
            if (fpeak < 200.0f || fpeak > 4910.0f) continue;
            float speak = savsm[i] - 0.25f * (savsm[i - 1] - savsm[i + 1]) * del;
            t.push_back({fpeak, speak});
            if ((int)t.size() == MAXCAND) break;
        }
    }
    // candidates within 20 Hz of nfqso first
    for (const Cand &c : t) if (std::fabs(c.f - nfqso) <= 20.0f) candidate.push_back(c);
    for (const Cand &c : t) if (!(std::fabs(c.f - nfqso) <= 20.0f)) candidate.push_back(c);
}

// ---- ft2l_downsample ------------------------------------------------------------------------------------------------

struct Downsampler {
    std::vector<cplx> cx;      // 0..NMAX/2
    std::vector<double> window; // 0..NFFT2-1
    const int NFFT2 = NMAX / NDOWN;
    Downsampler() : cx(NMAX / 2 + 1) {
        float df = 12000.0f / NMAX, baud = 12000.0f / NSPS;
        volatile float bw_transition = 0.5f * baud, bw_flat = 4 * baud;
        int iwt = (int)(bw_transition / df), iwf = (int)(bw_flat / df);
        window.assign(NFFT2, 0.0);
        for (int i = 0; i < iwt; i++) window[i] = 0.5 * (1 + std::cos(kPi * (iwt - 1 - i) / iwt));
        for (int i = iwt; i < iwt + iwf; i++) window[i] = 1.0;
        for (int i = 0; i < iwt; i++) window[iwt + iwf + i] = 0.5 * (1 + std::cos(kPi * i / iwt));
        int iws = (int)(baud / df);
        std::vector<double> w2(NFFT2);
        for (int i = 0; i < NFFT2; i++) w2[i] = window[(i + iws) % NFFT2];
        window = w2;
    }
    void newdata(const float *dd) {
        std::vector<double> x(NMAX);
        for (int i = 0; i < NMAX; i++) x[i] = dd[i];
        rfft(x.data(), NMAX, cx.data());
    }
    void run(float f0, cplx *c) { // c has NDMAX entries
        float df = 12000.0f / NMAX;
        int i0 = (int)std::lrint(f0 / df);
        std::vector<cplx> c1(NFFT2, cplx(0, 0));
        if (i0 >= 0 && i0 <= NMAX / 2) c1[0] = cx[i0];
        for (int i = 1; i <= NFFT2 / 2; i++) {
            if (i0 + i >= 0 && i0 + i <= NMAX / 2) c1[i] = cx[i0 + i];
            if (i0 - i >= 0 && i0 - i <= NMAX / 2) c1[NFFT2 - i] = cx[i0 - i];
        }
        for (int i = 0; i < NFFT2; i++) c1[i] *= window[i] / NFFT2;
        fft(c1.data(), NFFT2, +1);
        for (int i = 0; i < NDMAX; i++) c[i] = c1[i];
    }
};

// ---- sync2ld ----------------------------------------------------------------------------------------------------------

struct SyncTables {
    cplx csync[4][2 * NSS];       // csynca..csyncd
    cplx ctwk2[85][2 * NSS];      // ctwk2(:, -42:42)
    cplx cs[85][4][2 * NSS];      // conj(ctwk2 * csync_a), what sync2ld correlates with
    SyncTables() {
        const double twopi = 8.0 * std::atan(1.0);
        for (int a = 0; a < 4; a++) {
            int k = 0;
            double phi = 0;
            for (int i = 0; i < 4; i++) {
                double dphi = 2 * twopi * icos4[a][i] / (double)NSS;
                for (int j = 0; j < NSS / 2; j++) {
                    csync[a][k++] = cplx(std::cos(phi), std::sin(phi));
                    phi = std::fmod(phi + dphi, twopi);
                }
            }
        }
        const double fsample = (12000.0 / NDOWN) / 2.0;
        for (int idf = -42; idf <= 42; idf++) {
            cplx w(1, 0);
            double dphi = idf * (6.283185307 / fsample);
            for (int i = 0; i < 2 * NSS; i++) {
                w *= cplx(std::cos(dphi), std::sin(dphi));
                ctwk2[idf + 42][i] = w;
            }
            for (int a = 0; a < 4; a++)
                for (int i = 0; i < 2 * NSS; i++) cs[idf + 42][a][i] = std::conj(ctwk2[idf + 42][i] * csync[a][i]);
        }
    }
};

const SyncTables &sync_tables() {
    static SyncTables t;
    return t;
}

// sync2ld(cd0,i0,ctwk,itwk,sync): sync power of a downsampled FT2L signal starting at sample i0.
// cs[a] is conj(ctwk*csync_a) for the frequency tweak in use (precomputed once per tweak: SyncTables::cs).
inline void dot_stride2(const cplx *cd, int start, const cplx *c, int n, double &re, double &im) {
    double sr = 0, si = 0;
    const double *p = reinterpret_cast<const double *>(cd + start);
    const double *q = reinterpret_cast<const double *>(c);
    for (int m = 0; m < n; m++) { // cd0(start+2m) * conj(csync2(m))   (c already holds the conjugate)
        double ar = p[4 * m], ai = p[4 * m + 1], br = q[2 * m], bi = q[2 * m + 1];
        sr += ar * br - ai * bi;
        si += ar * bi + ai * br;
    }
    re += sr;
    im += si;
}

double sync2ld(const cplx *cd0, int i0, const cplx (*cs)[2 * NSS]) {
    const int NP = NMAX / NDOWN;
    const double fac = 1.0 / (2.0 * NSS);
    int i1 = i0, i2 = i0 + 33 * NSS, i3 = i0 + 66 * NSS, i4 = i0 + 99 * NSS;
    double r1 = 0, q1 = 0, r2 = 0, q2 = 0, r3 = 0, q3 = 0, r4 = 0, q4 = 0;
    if (i1 >= 0 && i1 + 4 * NSS - 1 <= NP - 1) {
        dot_stride2(cd0, i1, cs[0], 2 * NSS, r1, q1);
    } else if (i1 < 0) {
        int npts = (i1 + 4 * NSS - 1) / 2;
        if (npts > 16) dot_stride2(cd0, 0, cs[0] + (2 * NSS - npts - 1), npts + 1, r1, q1);
    }
    if (i2 >= 0 && i2 + 4 * NSS - 1 <= NP - 1) dot_stride2(cd0, i2, cs[1], 2 * NSS, r2, q2);
    if (i3 >= 0 && i3 + 4 * NSS - 1 <= NP - 1) dot_stride2(cd0, i3, cs[2], 2 * NSS, r3, q3);
    if (i4 >= 0 && i4 + 4 * NSS - 1 <= NP - 1) {
        dot_stride2(cd0, i4, cs[3], 2 * NSS, r4, q4);
    } else if (i4 + 4 * NSS - 1 > NP - 1) {
        int npts = (NP - 1 - i4 + 1) / 2;
        if (npts > 16) dot_stride2(cd0, i4, cs[3], npts, r4, q4);
    }
    return (std::sqrt(r1 * r1 + q1 * q1) + std::sqrt(r2 * r2 + q2 * q2) + std::sqrt(r3 * r3 + q3 * q3) + std::sqrt(r4 * r4 + q4 * q4)) * fac;
}

// ---- get_ft2l_bitmetrics ----------------------------------------------------------------------------------------------

// cd: NN*NSS complex samples. bitmetrics[5][2*NN] (index [j][i-1] for bitmetrics(i,j+1)). Returns badsync.
bool get_ft2l_bitmetrics(const cplx *cd, float bitmetrics[5][2 * NN]) {
    static const int graymap[4] = {0, 1, 3, 2};
    cplx cs[NN][4];
    double s4[NN][4];
    for (int k = 0; k < NN; k++) {
        cplx csymb[NSS];
        for (int i = 0; i < NSS; i++) csymb[i] = cd[k * NSS + i];
        fft(csymb, NSS, -1);
        for (int t = 0; t < 4; t++) { cs[k][t] = csymb[t]; s4[k][t] = std::abs(csymb[t]); }
    }
    int nbits = 0;
    for (int k = 1; k <= 4; k++) {
        for (int a = 0; a < 4; a++) {
            int sym = (k - 1) + 33 * a; // 0-based symbol index of the a-th Costas array
            int ip = 0;
            for (int t = 1; t < 4; t++)
                if (s4[sym][t] > s4[sym][ip]) ip = t;
            int is_det = ip, is_true = icos4[a][k - 1];
            if (is_det == is_true) nbits += 2;
            else if (std::abs(is_det - is_true) == 2) nbits += 0;
            else nbits += 1;
        }
    }
    if (nbits < 8) return true;
    for (int j = 0; j < 5; j++) for (int i = 0; i < 2 * NN; i++) bitmetrics[j][i] = 0.f;
    for (int nseq = 1; nseq <= 3; nseq++) {
        int nsym = nseq == 1 ? 1 : (nseq == 2 ? 2 : 4);
        int nt = 1 << (2 * nsym);
        for (int ks = 1; ks <= NN - nsym + 1; ks += nsym) {
            float s2[256];
            for (int i = 0; i < nt; i++) {
                int i1 = i / 64, i2 = (i & 63) / 16, i3 = (i & 15) / 4, i4 = i & 3;
                if (nsym == 1) s2[i] = (float)std::abs(cs[ks - 1][graymap[i & 3]]);
                else if (nsym == 2) s2[i] = (float)std::abs(cs[ks - 1][graymap[i3]] + cs[ks][graymap[i4]]);
                else s2[i] = (float)std::abs(cs[ks - 1][graymap[i1]] + cs[ks][graymap[i2]] + cs[ks + 1][graymap[i3]] + cs[ks + 2][graymap[i4]]);
            }
            int ipt = 1 + (ks - 1) * 2;
            int ibmax = nsym == 1 ? 1 : (nsym == 2 ? 3 : 7);
            for (int ib = 0; ib <= ibmax; ib++) {
                int bit = ibmax - ib;
                float m1 = -1e30f, m0 = -1e30f;
                for (int i = 0; i < nt; i++) {
                    if ((i >> bit) & 1) m1 = std::max(m1, s2[i]);
                    else m0 = std::max(m0, s2[i]);
                }
                float bm = m1 - m0;
                if (ipt + ib > 2 * NN) continue;
                bitmetrics[nseq - 1][ipt + ib - 1] = bm;
            }
        }
    }
    for (int i = 204; i < 206; i++) bitmetrics[1][i] = bitmetrics[0][i];
    for (int i = 200; i < 204; i++) bitmetrics[2][i] = bitmetrics[1][i];
    for (int i = 204; i < 206; i++) bitmetrics[2][i] = bitmetrics[0][i];
    normalizebmet(bitmetrics[0], 2 * NN);
    normalizebmet(bitmetrics[1], 2 * NN);
    normalizebmet(bitmetrics[2], 2 * NN);
    for (int i = 0; i < 2 * NN; i++) {
        float bmax = -1.0f;
        for (int j = 0; j < 3; j++) {
            if (std::fabs(bitmetrics[j][i]) > bmax) {
                bmax = std::fabs(bitmetrics[j][i]);
                bitmetrics[3][i] = bitmetrics[j][i];
            }
        }
        bitmetrics[4][i] = (bitmetrics[0][i] + bitmetrics[1][i] + bitmetrics[2][i]) / 3.0f;
    }
    return false;
}

// ---- subtractft2l -----------------------------------------------------------------------------------------------------

void subtractft2l(float *dd, const int *itone, float f0, float dt) {
    const int NFILT = 700, NFRAME = (103 + 2) * NSPS, nfft = NMAX;
    static std::vector<cplx> cw;
    if (cw.empty()) {
        cw.assign(nfft, cplx(0, 0));
        double sum = 0;
        std::vector<double> window(NFILT + 1);
        for (int j = -NFILT / 2; j <= NFILT / 2; j++) {
            double c = std::cos(kPi * j / NFILT);
            window[j + NFILT / 2] = c * c;
            sum += window[j + NFILT / 2];
        }
        std::vector<cplx> tmp(nfft, cplx(0, 0));
        for (int i = 0; i <= NFILT; i++) tmp[i] = window[i] / sum;
        int shift = NFILT / 2 + 1; // cshift(cw,NFILT/2+1): result(i)=cw(i+shift)
        for (int i = 0; i < nfft; i++) cw[i] = tmp[(i + shift) % nfft];
        fft(cw.data(), nfft, -1);
        for (int i = 0; i < nfft; i++) cw[i] *= 1.0 / nfft;
    }
    int nstart = (int)(dt * 12000 + 1 - NSPS);
    std::vector<cplx> cref(NFRAME);
    gen_ft2lwave(itone, 103, NSPS, 12000.0, f0, cref.data());
    std::vector<cplx> cfilt(nfft, cplx(0, 0));
    for (int i = 1; i <= NFRAME; i++) {
        int id = nstart - 1 + i;
        if (id >= 1 && id <= NMAX) cfilt[i - 1] = (double)dd[id - 1] * std::conj(cref[i - 1]);
    }
    fft(cfilt.data(), nfft, -1);
    for (int i = 0; i < nfft; i++) cfilt[i] *= cw[i];
    fft(cfilt.data(), nfft, +1);
    for (int i = 1; i <= NFRAME; i++) {
        int j = nstart + i - 1;
        if (j >= 1 && j <= NMAX) dd[j - 1] -= (float)(2 * (cfilt[i - 1] * cref[i - 1]).real());
    }
}

// ---- the decode() routine of ft2l_decode.f90, one period --------------------------------------------------------------

const int mcq[29] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0};

struct Found { float sync; int snr; float dt; float freq; std::string text; float qual; };

void decode_period(const float *iwave, int ndepth, int nfa, int nfb, std::vector<Found> &out) {
    std::vector<float> dd(NMAX);
    for (int i = 0; i < NMAX; i++) dd[i] = iwave[i] / 1000.0f;
    std::vector<std::string> decodes;
    float syncmin = 1.05f;
    bool dosubtract = true, doosd = true;
    int nsp = 3;
    if (ndepth == 2) doosd = false;
    if (ndepth == 1) { nsp = 1; dosubtract = false; doosd = false; }
    const float nfqso = 0.f;
    int nd1 = 0;
    int ndecodes = 0;
    std::vector<cplx> cd2(NDMAX), cb(NDMAX), cd(NN * NSS);
    Downsampler ds;
    const SyncTables &T = sync_tables();
    int nharderror = -1;
    for (int isp = 1; isp <= nsp; isp++) {
        if (isp == 2) {
            if (ndecodes == 0) break;
            nd1 = ndecodes;
        } else if (isp == 3) {
            int nd2 = ndecodes - nd1;
            if (nd2 == 0) break;
        }
        std::vector<Cand> cands;
        getcandidates2l(dd.data(), (float)nfa, (float)nfb, syncmin, nfqso, cands);
        bool dobigfft = true;
        for (const Cand &cand : cands) {
            float f0 = cand.f;
            float snr = cand.s - 1.0f;
            if (dobigfft) ds.newdata(dd.data());
            ds.run(f0, cd2.data());
            dobigfft = false;
            double sum2 = 0;
            for (int i = 0; i < NDMAX; i++) sum2 += std::norm(cd2[i]);
            sum2 /= ((double)NMAX / (double)NDOWN);
            if (sum2 > 0.0) { double sc = 1.0 / std::sqrt(sum2); for (int i = 0; i < NDMAX; i++) cd2[i] *= sc; }
            double smax = -99., smax1 = 0;
            int ibest = -1, idfbest = 0;
            for (int iseg = 1; iseg <= 3; iseg++) {
                for (int isync = 1; isync <= 2; isync++) {
                    int idfmin, idfmax, idfstp, ibmin, ibmax, ibstp;
                    if (isync == 1) {
                        idfmin = -24; idfmax = 24; idfstp = 6;
                        ibmin = -688; ibmax = 2024;
                        if (iseg == 1) { ibmin = 216; ibmax = 1120; }
                        else if (iseg == 2) { smax1 = smax; ibmin = 1120; ibmax = 2024; }
                        else if (iseg == 3) { ibmin = -688; ibmax = 216; }
                        ibstp = 1;
                    } else {
                        idfmin = idfbest - 8; idfmax = idfbest + 8; idfstp = 1;
                        ibmin = ibest - 10; ibmax = ibest + 10; ibstp = 1;
                    }
                    ibest = -1;
                    idfbest = 0;
                    smax = -99.;
                    for (int idf = idfmin; idf <= idfmax; idf += idfstp) {
                        for (int istart = ibmin; istart <= ibmax; istart += ibstp) {
                            double sync = sync2ld(cd2.data(), istart, T.cs[idf + 42]);
                            if (sync > smax) { smax = sync; ibest = istart; idfbest = idf; }
                        }
                    }
                }
                if (iseg == 1) smax1 = smax;
                if (smax < 1.0) continue;
                if (iseg > 1 && smax < smax1) continue;
                float f1 = f0 + (float)idfbest;
                if (f1 <= 10.0f || f1 >= 4990.0f) continue;
                ds.run(f1, cb.data());
                double s2 = 0;
                // sum(abs(cb)**2) is over the whole array cb(0:NDMAX-1), divided by NSS*NN
                for (int i = 0; i < NDMAX; i++) s2 += std::norm(cb[i]);
                s2 /= ((double)NSS * NN);
                if (s2 > 0.0) { double sc = 1.0 / std::sqrt(s2); for (int i = 0; i < NDMAX; i++) cb[i] *= sc; }
                for (auto &v : cd) v = cplx(0, 0);
                if (ibest >= 0) {
                    int it = std::min(NDMAX - 1, ibest + NN * NSS - 1);
                    int np = it - ibest + 1;
                    for (int i = 0; i < np; i++) cd[i] = cb[ibest + i];
                } else {
                    int n = NN * NSS + 2 * ibest; // cb(0:NN*NSS+2*ibest-1) -> cd(-ibest:ibest+NN*NSS-1)
                    for (int i = 0; i < n; i++) cd[-ibest + i] = cb[i];
                }
                float bitmetrics[5][2 * NN];
                if (get_ft2l_bitmetrics(cd.data(), bitmetrics)) continue; // badsync
                int hbits[2 * NN];
                for (int i = 0; i < 2 * NN; i++) hbits[i] = bitmetrics[0][i] >= 0 ? 1 : 0;
                static const int s1[8] = {0, 0, 0, 1, 1, 0, 1, 1}, s2a[8] = {0, 1, 0, 0, 1, 1, 1, 0},
                                 s3[8] = {1, 1, 1, 0, 0, 1, 0, 0}, s4[8] = {1, 0, 1, 1, 0, 0, 0, 1};
                int nsync_qual = 0;
                for (int i = 0; i < 8; i++) {
                    if (hbits[i] == s1[i]) nsync_qual++;
                    if (hbits[66 + i] == s2a[i]) nsync_qual++;
                    if (hbits[132 + i] == s3[i]) nsync_qual++;
                    if (hbits[198 + i] == s4[i]) nsync_qual++;
                }
                if (nsync_qual < 15) continue;
                const float scalefac = 2.83f;
                float llrs[5][174];
                for (int j = 0; j < 5; j++) {
                    for (int i = 0; i < 58; i++) {
                        llrs[j][i] = scalefac * bitmetrics[j][8 + i];
                        llrs[j][58 + i] = scalefac * bitmetrics[j][74 + i];
                        llrs[j][116 + i] = scalefac * bitmetrics[j][140 + i];
                    }
                }
                float apmag = 0;
                for (int i = 0; i < 174; i++) apmag = std::max(apmag, std::fabs(llrs[0][i]));
                apmag *= 1.1f;
                int npasses = 5 + 2; // 5 normal passes + nappasses(0)=2 (only the CQ pass can run: no station call)
                if (ndepth == 1) npasses = 5;
                float llr[174];
                uint8_t apmask[174], message91[91], cw[174];
                for (int ipass = 1; ipass <= npasses; ipass++) {
                    int iaptype = 0;
                    if (ipass <= 5) {
                        memcpy(llr, llrs[ipass - 1], sizeof llr);
                        memset(apmask, 0, sizeof apmask);
                    } else {
                        float llrd[174];
                        memcpy(llrd, llrs[2], sizeof llrd); // llrd=llrc
                        iaptype = ipass == 6 ? 1 : 2;
                        if (iaptype >= 2) continue; // needs my call: none set (apbits(1) > 1)
                        memset(apmask, 0, sizeof apmask);
                        for (int i = 0; i < 29; i++) { apmask[i] = 1; llrd[i] = apmag * mcq[i]; }
                        memcpy(llr, llrd, sizeof llr);
                    }
                    int ndeep = 3, maxosd = 3;
                    if (!doosd) maxosd = -1;
                    int ntype = 0;
                    float dmin = 0;
                    decode174_91(llr, 91, maxosd, ndeep, apmask, message91, cw, &ntype, &nharderror, &dmin);
                    uint8_t message77[77];
                    int sum77 = 0;
                    for (int i = 0; i < 77; i++) { message77[i] = message91[i]; sum77 += message77[i]; }
                    if (sum77 == 0) continue;
                    if (nharderror >= 0) {
                        for (int i = 0; i < 77; i++) message77[i] = (message77[i] + kFt2Rvec[i]) & 1; // remove rvec scrambling
                        bool ok = false;
                        std::string message = unpack77(message77, &ok);
                        if (!ok) break;
                        if (dosubtract) {
                            int i4tone[NN];
                            ft2l_tones_from_77bits(message77, i4tone);
                            float dt = (float)ibest / 1333.33f;
                            subtractft2l(dd.data(), i4tone, f1, dt);
                        }
                        bool idupe = false;
                        for (const std::string &d : decodes) if (d == message) idupe = true;
                        if (idupe) break;
                        ndecodes++;
                        decodes.push_back(message);
                        double xsnr = snr > 0.0f ? 10 * std::log10(snr) - 14.8 : -21.0;
                        int nsnr = (int)std::lrint(std::max(-21.0, xsnr));
                        float xdt = ibest / 1333.33f - 0.5f;
                        float qual = 1.0f - (nharderror + dmin) / 60.0f;
                        out.push_back({(float)smax, nsnr, xdt, f1, message, qual});
                        break;
                    }
                }
                if (nharderror >= 0) break;
            }
        }
    }
}

// ---- the stream decoder -----------------------------------------------------------------------------------------------

class Ft2Decoder : public StreamDecoder {
public:
    Ft2Decoder(const Options &o, const Emit &e) : opt_(o), emit_(e) {
        long long off = std::llround(o.offset * kRate);
        long long per = NMAX;
        off %= per;
        if (off < 0) off += per;
        skip_ = off; // samples to drop before the first period boundary
        start0_ = off;
    }
    void feed(const float *x, size_t n) override {
        for (size_t i = 0; i < n; i++) {
            if (skip_ > 0) { skip_--; total_++; continue; }
            buf_.push_back(x[i]);
            total_++;
            if ((int)buf_.size() == NMAX) { run_period(); buf_.clear(); period_++; }
        }
    }
    void finish() override {
        // a partial last period holding the whole transmission (it ends 3.02 s into the period) is decoded, zero padded
        if ((int)buf_.size() >= (int)(3.1 * kRate)) {
            buf_.resize(NMAX, 0.f);
            run_period();
            period_++;
        }
        buf_.clear();
    }
private:
    void run_period() {
        std::vector<Found> found;
        decode_period(buf_.data(), opt_.depth, opt_.fmin, opt_.fmax, found);
        double period_start = (double)(start0_ + period_ * NMAX) / kRate;
        for (const Found &f : found) {
            Decode d;
            d.mode = "FT2L";
            d.dt = f.dt;
            d.freq = f.freq;
            d.snr = f.snr;
            d.text = f.text;
            if (f.qual < 0.17f) d.note = "low confidence"; // WSJT-X marks such decodes with "?"
            d.t_start = period_start + f.dt + 0.5;
            emit_(d);
        }
    }
    Options opt_;
    Emit emit_;
    std::vector<float> buf_;
    long long skip_ = 0, total_ = 0, period_ = 0, start0_ = 0;
};

} // namespace

StreamDecoder *make_ft2_decoder(const Options &o, const Emit &e) { return new Ft2Decoder(o, e); }

} // namespace wm
