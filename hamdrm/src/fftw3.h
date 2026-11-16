// fftw3.h - a small stand-in for the parts of FFTW 3 that QSSTV's HamDRM code uses (hamdrm helper).
//
// This file is NOT from QSSTV. It was written for this helper because FFTW is not installed on the build machine
// and FFTW is GPL-2.0-or-later itself: only the API names and the documented FFTW data layouts are followed
// (plans, fftw_complex = double[2], fftwf_complex = float[2], the half-complex layout of the r2r transforms).
// Licence: GNU GPL version 3 or later, same as the helper. No FFTW code is used.
//
// Supported: fftw_plan_dft_1d / fftwf_plan_dft_1d (FFTW_FORWARD, FFTW_BACKWARD, unnormalised, in-place or not),
// fftw_plan_r2r_1d with FFTW_R2HC and FFTW_HC2R, fftw_execute, fftw_destroy_plan, fftw_malloc, fftw_free.
// Any length n >= 1 works: mixed radix (any prime factor, O(n * p) for a prime p) so the 112 / 160 / 256 / 288 point
// OFDM transforms of the DRM modes are fast; a length with a large prime factor is slow but correct.
#pragma once

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#define FFTW_FORWARD (-1)
#define FFTW_BACKWARD (+1)
#define FFTW_ESTIMATE (1U << 6)
#define FFTW_MEASURE (0U)
#define FFTW_PATIENT (1U << 5)
#define FFTW_DESTROY_INPUT (1U << 0)
#define FFTW_PRESERVE_INPUT (1U << 4)

typedef double fftw_complex[2];
typedef float fftwf_complex[2];
typedef enum { FFTW_R2HC = 0, FFTW_HC2R = 1 } fftw_r2r_kind;

namespace fftwshim {

struct Cx
{
    double re, im;
};

// Recursive decimation in time; works for every n, using the smallest prime factor as the radix.
inline void fft_rec(const Cx *in, Cx *out, size_t n, size_t stride, int sign, std::vector<Cx> &tw, size_t twn)
{
    if (n == 1)
    {
        out[0] = in[0];
        return;
    }
    size_t p = 2;
    while (p * p <= n && n % p)
        p++;
    if (n % p)
        p = n; // prime
    const size_t m = n / p;
    if (p == n)
    {
        // direct DFT of a prime length
        for (size_t k = 0; k < n; k++)
        {
            double sr = 0, si = 0;
            for (size_t j = 0; j < n; j++)
            {
                const Cx &w = tw[((j * k) % n) * (twn / n)];
                const Cx x = in[j * stride];
                const double wi = sign < 0 ? -w.im : w.im;
                sr += x.re * w.re - x.im * wi;
                si += x.re * wi + x.im * w.re;
            }
            out[k].re = sr;
            out[k].im = si;
        }
        return;
    }
    for (size_t r = 0; r < p; r++)
        fft_rec(in + r * stride, out + r * m, m, stride * p, sign, tw, twn);
    std::vector<Cx> t(p);
    for (size_t k = 0; k < m; k++)
    {
        for (size_t r = 0; r < p; r++)
        {
            const Cx &w = tw[((r * k) % n) * (twn / n)];
            const double wi = sign < 0 ? -w.im : w.im;
            const Cx x = out[r * m + k];
            t[r].re = x.re * w.re - x.im * wi;
            t[r].im = x.re * wi + x.im * w.re;
        }
        for (size_t q = 0; q < p; q++)
        {
            double sr = 0, si = 0;
            for (size_t r = 0; r < p; r++)
            {
                const Cx &w = tw[((r * q * m) % n) * (twn / n)];
                const double wi = sign < 0 ? -w.im : w.im;
                sr += t[r].re * w.re - t[r].im * wi;
                si += t[r].re * wi + t[r].im * w.re;
            }
            out[q * m + k].re = sr;
            out[q * m + k].im = si;
        }
    }
}

struct Plan
{
    size_t n = 0;
    int kind = 0; // 0 dft float, 1 dft double, 2 r2hc double, 3 hc2r double
    int sign = -1;
    void *in = nullptr;
    void *out = nullptr;
    std::vector<Cx> tw; // exp(+i 2 pi k / n)
};

inline Plan *make(size_t n, int kind, int sign, void *in, void *out)
{
    Plan *p = new Plan;
    p->n = n;
    p->kind = kind;
    p->sign = sign;
    p->in = in;
    p->out = out;
    p->tw.resize(n ? n : 1);
    const double pi2 = 6.283185307179586476925286766559;
    for (size_t k = 0; k < n; k++)
    {
        p->tw[k].re = std::cos(pi2 * (double)k / (double)n);
        p->tw[k].im = std::sin(pi2 * (double)k / (double)n);
    }
    return p;
}

inline void run_complex(Plan *p, const std::vector<Cx> &x, std::vector<Cx> &y, int sign)
{
    y.resize(p->n);
    fft_rec(x.data(), y.data(), p->n, 1, sign, p->tw, p->n);
}

inline void execute(Plan *p)
{
    const size_t n = p->n;
    std::vector<Cx> x(n), y;
    if (p->kind == 0)
    {
        const float *in = (const float *)p->in;
        for (size_t i = 0; i < n; i++)
        {
            x[i].re = in[2 * i];
            x[i].im = in[2 * i + 1];
        }
        run_complex(p, x, y, p->sign);
        float *out = (float *)p->out;
        for (size_t i = 0; i < n; i++)
        {
            out[2 * i] = (float)y[i].re;
            out[2 * i + 1] = (float)y[i].im;
        }
    }
    else if (p->kind == 1)
    {
        const double *in = (const double *)p->in;
        for (size_t i = 0; i < n; i++)
        {
            x[i].re = in[2 * i];
            x[i].im = in[2 * i + 1];
        }
        run_complex(p, x, y, p->sign);
        double *out = (double *)p->out;
        for (size_t i = 0; i < n; i++)
        {
            out[2 * i] = y[i].re;
            out[2 * i + 1] = y[i].im;
        }
    }
    else if (p->kind == 2) // R2HC: r0 r1 ... r(n/2) i((n+1)/2-1) ... i1
    {
        const double *in = (const double *)p->in;
        for (size_t i = 0; i < n; i++)
        {
            x[i].re = in[i];
            x[i].im = 0;
        }
        run_complex(p, x, y, -1);
        std::vector<double> o(n);
        for (size_t k = 0; k <= n / 2; k++)
            o[k] = y[k].re;
        for (size_t k = 1; k < (n + 1) / 2; k++)
            o[n - k] = y[k].im;
        memcpy(p->out, o.data(), n * sizeof(double));
    }
    else // HC2R, unnormalised
    {
        const double *in = (const double *)p->in;
        for (size_t k = 0; k <= n / 2; k++)
        {
            x[k].re = in[k];
            x[k].im = (k == 0 || (n % 2 == 0 && k == n / 2)) ? 0.0 : in[n - k];
        }
        for (size_t k = 1; k < (n + 1) / 2; k++)
        {
            x[n - k].re = x[k].re;
            x[n - k].im = -x[k].im;
        }
        run_complex(p, x, y, +1);
        std::vector<double> o(n);
        for (size_t i = 0; i < n; i++)
            o[i] = y[i].re;
        memcpy(p->out, o.data(), n * sizeof(double));
    }
}

} // namespace fftwshim

typedef fftwshim::Plan *fftw_plan;
typedef fftwshim::Plan *fftwf_plan;

inline void *fftw_malloc(size_t n) { return malloc(n); }
inline void fftw_free(void *p) { free(p); }
inline fftw_plan fftw_plan_dft_1d(int n, fftw_complex *in, fftw_complex *out, int sign, unsigned) { return fftwshim::make((size_t)n, 1, sign, in, out); }
inline fftwf_plan fftwf_plan_dft_1d(int n, fftwf_complex *in, fftwf_complex *out, int sign, unsigned) { return fftwshim::make((size_t)n, 0, sign, in, out); }
inline fftw_plan fftw_plan_r2r_1d(int n, double *in, double *out, fftw_r2r_kind kind, unsigned) { return fftwshim::make((size_t)n, kind == FFTW_R2HC ? 2 : 3, 0, in, out); }
inline void fftw_execute(const fftw_plan p) { fftwshim::execute(p); }
inline void fftwf_execute(const fftwf_plan p) { fftwshim::execute(p); }
inline void fftw_destroy_plan(fftw_plan p) { delete p; }
inline void fftwf_destroy_plan(fftwf_plan p) { delete p; }
