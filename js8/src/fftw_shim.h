// SPDX-License-Identifier: GPL-3.0-only
//
// Part of the FoxSDR "js8" GPL helper (see ../UPSTREAM.md).
//
// A small stand-in for the handful of FFTW single-precision calls that the JS8Call decoder (JS8.cpp) makes, so the
// decoder source can stay as close to upstream as possible without needing the FFTW library on this PC. It covers
// exactly the calls JS8.cpp uses: fftwf_plan_dft_1d, fftwf_plan_dft_r2c_1d, fftwf_execute, fftwf_destroy_plan. Any
// transform length is supported (mixed radix, with a generic butterfly for factors above 5). The result is not
// bit-identical to FFTW's (different summation order), which is within the noise of the decoder.
//
// Conventions follow FFTW: unnormalised transforms, FFTW_FORWARD uses exp(-i...), FFTW_BACKWARD uses exp(+i...),
// and the real-to-complex transform writes n/2+1 complex outputs (it may run in place, with the n real inputs stored
// contiguously at the start of the buffer).
#pragma once

#include <complex>

extern "C++" {

using fftwf_complex = float[2];

struct fftwf_plan_s;
using fftwf_plan = fftwf_plan_s *;

constexpr int FFTW_FORWARD  = -1;
constexpr int FFTW_BACKWARD = +1;
constexpr unsigned FFTW_ESTIMATE_PATIENT = 0; // flags are ignored

fftwf_plan fftwf_plan_dft_1d(int n, fftwf_complex *in, fftwf_complex *out, int sign, unsigned flags);
fftwf_plan fftwf_plan_dft_r2c_1d(int n, float *in, fftwf_complex *out, unsigned flags);
void       fftwf_execute(fftwf_plan plan);
void       fftwf_destroy_plan(fftwf_plan plan);

}
