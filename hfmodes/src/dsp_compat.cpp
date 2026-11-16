// hfmodes helper: replacement for OnAir's core/src/dsp/dsp_compat.cpp (the upstream file carries SIMD dispatch and Accelerate paths this helper
// does not need). Only fftSplit is provided: a plain radix-2 FFT on split arrays with the same contract as upstream's (natural order in and out,
// forward = exp(-j...), inverse = exp(+j...), neither normalised). Everything else declared in dsp_compat.h is unused by the files taken.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "dect2/dsp/dsp_compat.h"
#include <cmath>
#include <vector>

namespace dect2 {

void fftSplit(float* re, float* im, int log2n, bool inverse) {
    if (log2n < 1) return;
    const int n = 1 << log2n;
    if (inverse) { float* t = re; re = im; im = t; }   // ifft(x) = swap(fft(swap(x)))
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float a = re[i]; re[i] = re[j]; re[j] = a; a = im[i]; im[i] = im[j]; im[j] = a; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const int h = len / 2;
        // twiddles per stage, double precision then rounded once, so the result is identical on every run
        std::vector<float> wr((size_t)h), wi((size_t)h);
        for (int k = 0; k < h; k++) {
            const double a = -2.0 * 3.14159265358979323846 * k / len;
            wr[(size_t)k] = (float)std::cos(a);
            wi[(size_t)k] = (float)std::sin(a);
        }
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < h; k++) {
                const int p = i + k, q = i + k + h;
                const float tr = re[q] * wr[(size_t)k] - im[q] * wi[(size_t)k];
                const float ti = re[q] * wi[(size_t)k] + im[q] * wr[(size_t)k];
                re[q] = re[p] - tr; im[q] = im[p] - ti;
                re[p] += tr; im[p] += ti;
            }
    }
}

}  // namespace dect2
