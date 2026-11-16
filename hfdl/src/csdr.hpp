// SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-or-later
/*
 * FoxSDR helper: declarations for the DSP routines that dumphfdl takes from libcsdr (fastddc.cpp, libcsdr.cpp:
 * Andras Retzler, BSD 3-clause; libcsdr_gpl.cpp: GPL-3.0-or-later). This header replaces upstream's fastddc.h,
 * libcsdr.h, libcsdr_gpl.h and fft.h and is written for this helper. The routines are C++ with std::complex<float>
 * instead of C99 float complex (MSVC has no C complex type), and the FFT is fft.cpp instead of FFTW.
 */
#pragma once
#include <complex>
#include <vector>
#include <cstdint>

typedef std::complex<float> cfloat;

// fft.cpp: radix-2 complex FFT, out of place, unnormalised, same sign convention as FFTW
class Fft {
public:
	Fft(int32_t size, bool forward);
	int32_t size() const { return size_; }
	void execute(const cfloat *in, cfloat *out) const;
private:
	int32_t size_;
	bool forward_;
	std::vector<int32_t> rev_;
	std::vector<cfloat> tw_;
};

// libcsdr.c
typedef enum window_s { WINDOW_BOXCAR, WINDOW_BLACKMAN, WINDOW_HAMMING } window_t;
#define WINDOW_DEFAULT WINDOW_HAMMING
int32_t next_pow2(int32_t x);
int32_t firdes_filter_len(float transition_bw);
void firdes_bandpass_c(cfloat *output, int32_t length, float lowcut, float highcut, window_t window);
float compute_filter_relative_transition_bw(int32_t sample_rate, int32_t transition_bw_Hz);
int32_t compute_fft_decimation_rate(int32_t sample_rate, int32_t target_rate);

// libcsdr_gpl.c
typedef struct shift_addition_data_s {
	float sindelta;
	float cosdelta;
	float rate;
} shift_addition_data_t;

typedef struct decimating_shift_addition_status_s {
	int32_t decimation_remain;
	float starting_phase;
	int32_t output_size;
} decimating_shift_addition_status_t;

shift_addition_data_t shift_addition_init(float rate);
shift_addition_data_t decimating_shift_addition_init(float rate, int decimation);
decimating_shift_addition_status_t decimating_shift_addition_cc(cfloat *input, cfloat *output, int32_t input_size,
		shift_addition_data_t d, int32_t decimation, decimating_shift_addition_status_t s);

// fastddc.c
struct fastddc_t {
	int32_t pre_decimation;
	int32_t post_decimation;
	int32_t taps_length;
	int32_t taps_min_length;
	int32_t overlap_length;     // it is taps_length - 1
	int32_t fft_size;
	int32_t fft_inv_size;
	int32_t input_size;
	int32_t post_input_size;
	float pre_shift;
	int32_t startbin;           // for pre_shift
	int32_t v;                  // step for pre_shift
	int32_t offsetbin;
	float post_shift;
	int32_t output_scrape;
	int32_t scrap;
	shift_addition_data_t dsadata;
};

int32_t fastddc_init(fastddc_t *ddc, float transition_bw, int32_t decimation, float shift_rate);
void fft_swap_sides(cfloat *io, int32_t fft_size);

// A per-channel frequency-domain channelizer (upstream: fft_channelizer)
struct fft_channelizer_s {
	fastddc_t ddc;
	Fft *inv_plan = nullptr;
	std::vector<cfloat> inv_input, inv_output;
	std::vector<cfloat> filtertaps_fft;
	decimating_shift_addition_status_t shift_status = {0, 0.0f, 0};
	~fft_channelizer_s() { delete inv_plan; }
};
fft_channelizer_s *fft_channelizer_create(int32_t decimation, float transition_bw, float freq_shift);
decimating_shift_addition_status_t fastddc_inv_cc(cfloat *input, cfloat *output, fft_channelizer_s *c);
