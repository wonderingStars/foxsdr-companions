// SPDX-License-Identifier: GPL-3.0-or-later
// FoxSDR helper: radix-2 FFT written for this helper. It replaces the FFTW (fftw3f) calls of upstream's
// fft_fftw.c; dumphfdl only ever uses power-of-two sizes. Forward: X[k] = sum x[n] exp(-2 pi i k n / N), backward
// uses the opposite sign; neither is scaled (as in FFTW).
#define _USE_MATH_DEFINES 1
#include <cmath>
#include "csdr.hpp"

Fft::Fft(int32_t size, bool forward) : size_(size), forward_(forward), rev_(size), tw_(size / 2 > 0 ? size / 2 : 1) {
	int bits = 0;
	while ((1 << bits) < size) {
		bits++;
	}
	for (int32_t i = 0; i < size; i++) {
		int32_t r = 0;
		for (int b = 0; b < bits; b++) {
			if (i & (1 << b)) {
				r |= 1 << (bits - 1 - b);
			}
		}
		rev_[i] = r;
	}
	for (int32_t k = 0; k < size / 2; k++) {
		double a = 2.0 * M_PI * (double)k / (double)size;
		tw_[k] = cfloat((float)cos(a), (float)(forward ? -sin(a) : sin(a)));
	}
}

void Fft::execute(const cfloat *in, cfloat *out) const {
	for (int32_t i = 0; i < size_; i++) {
		out[rev_[i]] = in[i];
	}
	for (int32_t len = 2; len <= size_; len <<= 1) {
		int32_t half = len >> 1, step = size_ / len;
		for (int32_t base = 0; base < size_; base += len) {
			for (int32_t j = 0; j < half; j++) {
				cfloat t = tw_[j * step] * out[base + j + half];
				cfloat u = out[base + j];
				out[base + j] = u + t;
				out[base + j + half] = u - t;
			}
		}
	}
}
