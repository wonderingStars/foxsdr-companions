// weakmodes: FFT of any length (radix 2, Bluestein for other lengths). Written for this helper (not WSJT-X code);
// GPL-3.0-or-later like the rest of the helper. WSJT-X uses FFTW through four2a.f90; sign and scaling follow it.
#include "wm_common.h"
#include <map>
#include <memory>

namespace wm {

namespace {

const double kPi = 3.14159265358979323846;

bool is_pow2(int n) { return n > 0 && (n & (n - 1)) == 0; }

struct Pow2Plan {
    int n = 0;
    std::vector<int> rev;
    std::vector<cplx> w; // exp(-2 pi i k/n), k < n/2
};

const Pow2Plan &pow2_plan(int n) {
    static std::map<int, std::unique_ptr<Pow2Plan>> cache;
    auto it = cache.find(n);
    if (it != cache.end()) return *it->second;
    std::unique_ptr<Pow2Plan> p(new Pow2Plan);
    p->n = n;
    p->rev.assign(n, 0);
    int lg = 0;
    while ((1 << lg) < n) lg++;
    for (int i = 0; i < n; i++) {
        int r = 0;
        for (int b = 0; b < lg; b++)
            if (i & (1 << b)) r |= 1 << (lg - 1 - b);
        p->rev[i] = r;
    }
    p->w.resize(n / 2 > 0 ? n / 2 : 1);
    for (int k = 0; k < n / 2; k++) p->w[k] = cplx(std::cos(2 * kPi * k / n), -std::sin(2 * kPi * k / n));
    const Pow2Plan &ref = *p;
    cache[n] = std::move(p);
    return ref;
}

void fft_pow2(cplx *x, int n, int isign) {
    if (n <= 1) return;
    const Pow2Plan &p = pow2_plan(n);
    for (int i = 0; i < n; i++)
        if (i < p.rev[i]) std::swap(x[i], x[p.rev[i]]);
    for (int len = 2; len <= n; len <<= 1) {
        int half = len >> 1, step = n / len;
        for (int i = 0; i < n; i += len) {
            for (int j = 0; j < half; j++) {
                cplx w = p.w[j * step];
                if (isign > 0) w = std::conj(w);
                cplx u = x[i + j], v = x[i + j + half] * w;
                x[i + j] = u + v;
                x[i + j + half] = u - v;
            }
        }
    }
}


// ---- mixed radix (all prime factors <= 13): recursive decimation in time ------------------------------------------
struct MixPlan {
    int n = 0;
    std::vector<int> factors;
    std::vector<cplx> w; // exp(-2 pi i k/n), k < n
};

bool factorise(int n, std::vector<int> &f) {
    f.clear();
    int m = n;
    for (int p : {4, 2, 3, 5, 7, 11, 13}) {
        while (m % p == 0) { f.push_back(p); m /= p; }
    }
    return m == 1;
}

const MixPlan *mix_plan(int n) {
    static std::map<int, std::unique_ptr<MixPlan>> cache;
    auto it = cache.find(n);
    if (it != cache.end()) return it->second.get();
    std::unique_ptr<MixPlan> p(new MixPlan);
    p->n = n;
    if (!factorise(n, p->factors)) { cache[n] = nullptr; return nullptr; }
    p->w.resize(n);
    for (int k = 0; k < n; k++) p->w[k] = cplx(std::cos(2 * kPi * k / n), -std::sin(2 * kPi * k / n));
    const MixPlan *r = p.get();
    cache[n] = std::move(p);
    return r;
}

void mix_rec(const MixPlan &pl, cplx *out, const cplx *in, int n, int istride, int fi, int isign, cplx *tmp) {
    int p = pl.factors[fi], m = n / p;
    if (m == 1) {
        for (int q = 0; q < p; q++) out[q] = in[(size_t)q * istride];
    } else {
        for (int q = 0; q < p; q++) mix_rec(pl, out + q * m, in + (size_t)q * istride, m, istride * p, fi + 1, isign, tmp);
    }
    int tw = pl.n / n; // twiddle step for this level
    int rootstep = pl.n / p;
    for (int k = 0; k < m; k++) {
        cplx t[16];
        for (int q = 0; q < p; q++) {
            cplx v = out[q * m + k];
            if (q && k) {
                cplx w = pl.w[((size_t)q * k * tw) % pl.n];
                v *= (isign > 0) ? std::conj(w) : w;
            }
            t[q] = v;
        }
        if (p == 2) {
            out[k] = t[0] + t[1];
            out[k + m] = t[0] - t[1];
        } else if (p == 4) {
            cplx a = t[0] + t[2], b = t[0] - t[2], c = t[1] + t[3], d = t[1] - t[3];
            cplx jd = (isign < 0) ? cplx(d.imag(), -d.real()) : cplx(-d.imag(), d.real()); // -i*d or +i*d
            out[k] = a + c;
            out[k + m] = b + jd;
            out[k + 2 * m] = a - c;
            out[k + 3 * m] = b - jd;
        } else {
            for (int r = 0; r < p; r++) {
                cplx sum = t[0];
                for (int q = 1; q < p; q++) {
                    cplx w = pl.w[((size_t)q * r * rootstep) % pl.n];
                    sum += t[q] * ((isign > 0) ? std::conj(w) : w);
                }
                tmp[r] = sum;
            }
            for (int r = 0; r < p; r++) out[k + r * m] = tmp[r];
        }
    }
}

struct BluePlan {
    int n = 0, m = 0;
    std::vector<cplx> chirp; // exp(-i pi k^2/n)
    std::vector<cplx> bfft;  // FFT of the conjugate chirp, length m
};

const BluePlan &blue_plan(int n) {
    static std::map<int, std::unique_ptr<BluePlan>> cache;
    auto it = cache.find(n);
    if (it != cache.end()) return *it->second;
    std::unique_ptr<BluePlan> p(new BluePlan);
    p->n = n;
    int m = 1;
    while (m < 2 * n - 1) m <<= 1;
    p->m = m;
    p->chirp.resize(n);
    for (int k = 0; k < n; k++) {
        long long kk = ((long long)k * k) % (2LL * n); // keep the angle small and exact
        double a = kPi * (double)kk / n;
        p->chirp[k] = cplx(std::cos(a), -std::sin(a));
    }
    p->bfft.assign(m, cplx(0, 0));
    p->bfft[0] = std::conj(p->chirp[0]);
    for (int k = 1; k < n; k++) p->bfft[k] = p->bfft[m - k] = std::conj(p->chirp[k]);
    fft_pow2(p->bfft.data(), m, -1);
    const BluePlan &ref = *p;
    cache[n] = std::move(p);
    return ref;
}

} // namespace

void fft(cplx *x, int n, int isign) {
    if (n <= 1) return;
    if (is_pow2(n)) { fft_pow2(x, n, isign); return; }
    if (const MixPlan *mp = mix_plan(n)) {
        std::vector<cplx> out(n), tmp(16);
        mix_rec(*mp, out.data(), x, n, 1, 0, isign, tmp.data());
        for (int i = 0; i < n; i++) x[i] = out[i];
        return;
    }
    const BluePlan &p = blue_plan(n);
    std::vector<cplx> a(p.m, cplx(0, 0));
    // the inverse is done by conjugating the input and the output of the forward transform
    if (isign > 0)
        for (int k = 0; k < n; k++) x[k] = std::conj(x[k]);
    for (int k = 0; k < n; k++) a[k] = x[k] * p.chirp[k];
    fft_pow2(a.data(), p.m, -1);
    for (int k = 0; k < p.m; k++) a[k] *= p.bfft[k];
    fft_pow2(a.data(), p.m, +1);
    double sc = 1.0 / p.m;
    for (int k = 0; k < n; k++) x[k] = a[k] * sc * p.chirp[k];
    if (isign > 0)
        for (int k = 0; k < n; k++) x[k] = std::conj(x[k]);
}

void rfft(const double *in, int n, cplx *out) {
    std::vector<cplx> t(n);
    for (int i = 0; i < n; i++) t[i] = cplx(in[i], 0.0);
    fft(t.data(), n, -1);
    for (int k = 0; k <= n / 2; k++) out[k] = t[k];
}

void irfft(const cplx *in, int n, double *out) {
    std::vector<cplx> t(n);
    for (int k = 0; k <= n / 2; k++) t[k] = in[k];
    for (int k = n / 2 + 1; k < n; k++) t[k] = std::conj(in[n - k]);
    t[0] = cplx(t[0].real(), 0.0);
    if (n % 2 == 0) t[n / 2] = cplx(t[n / 2].real(), 0.0);
    fft(t.data(), n, +1);
    for (int i = 0; i < n; i++) out[i] = t[i].real();
}

} // namespace wm
