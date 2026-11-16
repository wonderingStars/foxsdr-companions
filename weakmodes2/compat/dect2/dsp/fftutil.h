// FoxSDR GPL helper "weakmodes2": replacement for OnAir's core/include/dect2/dsp/fftutil.h (which sits on OnAir's own FFT kernels).
// Same interface (class dect2::Fft with forward / inverse on interleaved complex floats, the inverse unnormalised, size a power of two),
// self-contained iterative radix-2 transform with a twiddle table. Written for this helper; licence GPL-3.0 like the rest of the folder.
#pragma once
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace dect2 {

typedef std::complex<float> cf32;

class Fft {
public:
    explicit Fft(int n) : n_(n), cosT_((size_t)(n > 1 ? n / 2 : 1)), sinT_((size_t)(n > 1 ? n / 2 : 1)), rev_((size_t)n) {
        log2n_ = 0;
        while ((1 << log2n_) < n) log2n_++;
        for (int i = 0; i < n / 2; i++) {
            const double a = -2.0 * 3.14159265358979323846 * (double)i / (double)n;
            cosT_[(size_t)i] = (float)std::cos(a);
            sinT_[(size_t)i] = (float)std::sin(a);
        }
        for (int i = 0; i < n; i++) {
            int r = 0;
            for (int b = 0; b < log2n_; b++) if (i & (1 << b)) r |= 1 << (log2n_ - 1 - b);
            rev_[(size_t)i] = r;
        }
    }
    int size() const { return n_; }
    // In-place; the inverse is unnormalised (sum over carriers), like the textbook IDFT without the 1/N
    void forward(cf32* x) { run(x, false); }
    void inverse(cf32* x) { run(x, true); }

private:
    void run(cf32* x, bool inv) {
        for (int i = 0; i < n_; i++) { const int j = rev_[(size_t)i]; if (j > i) std::swap(x[i], x[j]); }
        for (int len = 2; len <= n_; len <<= 1) {
            const int half = len >> 1, stride = n_ / len;
            for (int i = 0; i < n_; i += len)
                for (int k = 0; k < half; k++) {
                    const float wr = cosT_[(size_t)(k * stride)], wi = inv ? -sinT_[(size_t)(k * stride)] : sinT_[(size_t)(k * stride)];
                    const cf32 a = x[i + k], b = x[i + k + half];
                    const cf32 t(b.real() * wr - b.imag() * wi, b.real() * wi + b.imag() * wr);
                    x[i + k] = a + t;
                    x[i + k + half] = a - t;
                }
        }
    }
    int n_, log2n_;
    std::vector<float> cosT_, sinT_;
    std::vector<int> rev_;
};

} // namespace dect2
