// SPDX-License-Identifier: GPL-3.0-only
//
// Part of the FoxSDR "js8" GPL helper (see ../UPSTREAM.md). Written for this helper; not taken from FFTW.

#include "fftw_shim.h"

#include <cmath>
#include <cstddef>
#include <map>
#include <memory>
#include <vector>

namespace
{
    using cd = std::complex<double>;

    // Twiddle tables W_n^k = exp(-2 pi i k / n), shared between plans of the same length.
    std::shared_ptr<std::vector<cd>> twiddles(int n)
    {
        static std::map<int, std::shared_ptr<std::vector<cd>>> cache;
        auto it = cache.find(n);
        if (it != cache.end()) return it->second;

        auto t = std::make_shared<std::vector<cd>>(static_cast<std::size_t>(n));
        const double step = -2.0 * 3.14159265358979323846264338327950288 / n;
        for (int k = 0; k < n; ++k)
        {
            (*t)[static_cast<std::size_t>(k)] = std::polar(1.0, step * k);
        }
        cache[n] = t;
        return t;
    }

    std::vector<int> factorise(int n)
    {
        std::vector<int> f;
        for (int p : {4, 2, 3, 5})
        {
            while (n % p == 0)
            {
                f.push_back(p);
                n /= p;
            }
        }
        for (int p = 7; n > 1; p += 2)
        {
            while (n % p == 0)
            {
                f.push_back(p);
                n /= p;
            }
            if (p * p > n && n > 1)
            {
                f.push_back(n);
                break;
            }
        }
        return f;
    }
}

struct fftwf_plan_s
{
    int                              n       = 0;
    int                              sign    = -1;
    bool                             r2c     = false;
    float *                          in      = nullptr; // real input (r2c)
    fftwf_complex *                  cin     = nullptr; // complex input
    fftwf_complex *                  out     = nullptr;
    std::vector<int>                 factors;
    std::shared_ptr<std::vector<cd>> tw;
    std::vector<cd>                  a, b, tmp;

    // Recursive decimation-in-time mixed-radix transform of `len` points read from `src` with stride `stride`,
    // written contiguously to `dst`. `fi` indexes the factor list.
    void rec(cd * dst, cd const * src, std::size_t len, std::size_t stride, std::size_t fi)
    {
        if (len == 1)
        {
            dst[0] = src[0];
            return;
        }

        const std::size_t p = static_cast<std::size_t>(factors[fi]);
        const std::size_t m = len / p;

        for (std::size_t q = 0; q < p; ++q)
        {
            rec(dst + q * m, src + q * stride, m, stride * p, fi + 1);
        }

        const std::size_t N     = static_cast<std::size_t>(n);
        const std::size_t tstep = N / len; // W_len^j = W_N^(j * tstep)
        auto const &      W     = *tw;
        const bool        inv   = sign > 0;
        auto              w     = [&](std::size_t j) { return inv ? std::conj(W[j % N]) : W[j % N]; };

        if (p == 2)
        {
            for (std::size_t k = 0; k < m; ++k)
            {
                const cd t0 = dst[k];
                const cd t1 = dst[k + m] * w(k * tstep);
                dst[k]      = t0 + t1;
                dst[k + m]  = t0 - t1;
            }
            return;
        }

        cd t[64];
        std::vector<cd> big;
        cd * tv = t;
        if (p > 64)
        {
            big.resize(p);
            tv = big.data();
        }

        for (std::size_t k = 0; k < m; ++k)
        {
            for (std::size_t q = 0; q < p; ++q)
            {
                tv[q] = dst[k + q * m] * w(q * k * tstep);
            }
            for (std::size_t r = 0; r < p; ++r)
            {
                cd acc = tv[0];
                for (std::size_t q = 1; q < p; ++q)
                {
                    // W_p^(q*r) = W_N^((q*r mod p) * N / p)
                    acc += tv[q] * w(((q * r) % p) * (N / p));
                }
                dst[k + r * m] = acc;
            }
        }
    }

    void run(std::vector<cd> & input)
    {
        b.resize(static_cast<std::size_t>(n));
        rec(b.data(), input.data(), static_cast<std::size_t>(n), 1, 0);
    }
};

fftwf_plan fftwf_plan_dft_1d(int n, fftwf_complex * in, fftwf_complex * out, int sign, unsigned)
{
    if (n <= 0) return nullptr;
    auto * p    = new fftwf_plan_s;
    p->n        = n;
    p->sign     = sign;
    p->cin      = in;
    p->out      = out;
    p->factors  = factorise(n);
    p->tw       = twiddles(n);
    return p;
}

fftwf_plan fftwf_plan_dft_r2c_1d(int n, float * in, fftwf_complex * out, unsigned)
{
    if (n <= 0) return nullptr;
    auto * p    = new fftwf_plan_s;
    p->n        = n;
    p->sign     = -1;
    p->r2c      = true;
    p->in       = in;
    p->out      = out;
    p->factors  = factorise(n);
    p->tw       = twiddles(n);
    return p;
}

void fftwf_execute(fftwf_plan p)
{
    const std::size_t n = static_cast<std::size_t>(p->n);
    p->a.resize(n);

    if (p->r2c)
    {
        for (std::size_t i = 0; i < n; ++i) p->a[i] = cd(p->in[i], 0.0);
        p->run(p->a);
        for (std::size_t i = 0; i < n / 2 + 1; ++i)
        {
            p->out[i][0] = static_cast<float>(p->b[i].real());
            p->out[i][1] = static_cast<float>(p->b[i].imag());
        }
    }
    else
    {
        for (std::size_t i = 0; i < n; ++i) p->a[i] = cd(p->cin[i][0], p->cin[i][1]);
        p->run(p->a);
        for (std::size_t i = 0; i < n; ++i)
        {
            p->out[i][0] = static_cast<float>(p->b[i].real());
            p->out[i][1] = static_cast<float>(p->b[i].imag());
        }
    }
}

void fftwf_destroy_plan(fftwf_plan p)
{
    delete p;
}
