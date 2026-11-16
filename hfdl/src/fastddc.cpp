/*
This software is part of libcsdr, a set of simple DSP routines for
Software Defined Radio.

Copyright (c) 2014, Andras Retzler <randras@sdr.hu>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ANDRAS RETZLER BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


// FoxSDR helper: C++ port (std::complex<float>, own FFT, no thread/block plumbing) of dumphfdl 1.7.0 src/fastddc.c.
// Function names are upstream's. Unchanged algorithm.
#define _USE_MATH_DEFINES 1
#include <cmath>
#include <cstring>
#include <cstdint>
#include "csdr.hpp"

//DDC implementation based on:
//http://www.3db-labs.com/01598092_MultibandFilterbank.pdf

static int32_t is_integer(float a) { return floorf(a) == a; }

int32_t fastddc_init(fastddc_t *ddc, float transition_bw, int32_t decimation, float shift_rate)
{
	ddc->pre_decimation = 1; //this will be done in the frequency domain
	ddc->post_decimation = decimation; //this will be done in the time domain
	while( is_integer((float)ddc->post_decimation/2) && ddc->post_decimation/2 != 1)
	{
		ddc->post_decimation/=2;
		ddc->pre_decimation*=2;
	}
	ddc->taps_min_length = firdes_filter_len(transition_bw); //his is the minimal number of taps to achieve the given transition_bw; we are likely to have more taps than this number.
	ddc->taps_length = next_pow2((int32_t)ceilf(ddc->taps_min_length/(float)ddc->pre_decimation) * ddc->pre_decimation) + 1; //the number of taps must be a multiple of the decimation factor
	ddc->fft_size = next_pow2(ddc->taps_length * 4); //it is a good rule of thumb for performance (based on the article), but we should do benchmarks
	while (ddc->fft_size<ddc->pre_decimation) ddc->fft_size*=2; //fft_size should be a multiple of pre_decimation.
	ddc->overlap_length = ddc->taps_length - 1;
	ddc->input_size = ddc->fft_size - ddc->overlap_length;
	ddc->fft_inv_size = ddc->fft_size / ddc->pre_decimation;

	//Shift operation in the frequency domain: we can shift by a multiple of v.
	ddc->v = ddc->fft_size/ddc->overlap_length; //overlap factor | +-1 ? (or maybe ceil() this?)
	int32_t middlebin=ddc->fft_size / 2;
	ddc->startbin = (int32_t)(middlebin + middlebin * (-shift_rate) * 2);
	ddc->startbin = (int32_t)(ddc->v * round( ddc->startbin / (float)ddc->v ));
	ddc->offsetbin = ddc->startbin - middlebin;
	ddc->post_shift = (ddc->pre_decimation)*(shift_rate+((float)ddc->offsetbin/ddc->fft_size));
	ddc->pre_shift = ddc->offsetbin/(float)ddc->fft_size;
	ddc->dsadata = decimating_shift_addition_init(ddc->post_shift, ddc->post_decimation);

	//Overlap is scrapped, not added
	ddc->scrap=ddc->overlap_length/ddc->pre_decimation; //TODO this is problematic sometimes! overlap_length = 401 :: scrap = 200
	ddc->post_input_size=ddc->fft_inv_size-ddc->scrap;
	return ddc->fft_size<=2; //returns true on error
}

void fft_swap_sides(cfloat *io, int32_t fft_size)
{
	int32_t middle = fft_size / 2;
	cfloat temp;
	for(int32_t i = 0; i < middle; i++)
	{
		temp = io[i];
		io[i] = io[i+middle];
		io[i+middle] = temp;
	}
}

static void multiply_add(cfloat const *input, cfloat const *kernel, cfloat *output, int32_t len) {
	for(int32_t i = 0; i < len; i++) {
		output[i] += kernel[i] * input[i];
	}
}

static void multiply_and_shift(cfloat const *input, cfloat const *kernel, int32_t input_len,
		cfloat *output, int32_t output_len, int32_t offset) {
	int32_t input_idx = 0;
	int32_t head_output_idx = (input_len - offset + output_len / 2) % output_len;
	int32_t head_output_len = output_len - head_output_idx;
	int32_t tail_output_len = output_len - head_output_len;
	memset(output, 0, output_len * sizeof(cfloat));

	// Handle block head
	int32_t output_idx = head_output_idx;
	multiply_add(input + input_idx, kernel + input_idx, output + output_idx, head_output_len);
	input_idx += head_output_len;

	// Handle whole blocks
	int32_t whole_blocks_cnt = input_len / output_len - 1;
	output_idx = 0;
	for(int32_t i = 0; i < whole_blocks_cnt; i++) {
		multiply_add(input + input_idx, kernel + input_idx, output + output_idx, output_len);
		input_idx += output_len;
	}

	// Handle block tail
	multiply_add(input + input_idx, kernel + input_idx, output + output_idx, tail_output_len);
}

// upstream: fastddc_inv_cc(input, output, ddc, plan_inverse, taps_fft, shift_stat); the channelizer object now
// carries the pieces that were passed separately
decimating_shift_addition_status_t fastddc_inv_cc(cfloat *input, cfloat *output, fft_channelizer_s *c)
{
	//implements DDC by using the overlap & scrap method
	//input should have ddc->fft_size number of elements
	fastddc_t *ddc = &c->ddc;
	cfloat *inv_input = c->inv_input.data();
	cfloat *inv_output = c->inv_output.data();
	multiply_and_shift(input, c->filtertaps_fft.data(), ddc->fft_size, inv_input, c->inv_plan->size(), ddc->offsetbin);
	fft_swap_sides(inv_input, c->inv_plan->size());
	c->inv_plan->execute(inv_input, inv_output);

	// Normalize iFFT result
	cfloat const norm = (float)(ddc->pre_decimation * c->inv_plan->size());
	for(int32_t i = 0; i < c->inv_plan->size(); i++) {
		inv_output[i] /= norm;
	}
	//Overlap is scrapped, not added
	//Shift correction
	return decimating_shift_addition_cc(inv_output+ddc->scrap, output, ddc->post_input_size, ddc->dsadata,
			ddc->post_decimation, c->shift_status);
}

fft_channelizer_s *fft_channelizer_create(int32_t decimation, float transition_bw, float freq_shift) {
	window_t window = WINDOW_HAMMING;
	fft_channelizer_s *c = new fft_channelizer_s();
	if(fastddc_init(&c->ddc, transition_bw, decimation, freq_shift)) {
		delete c;
		return nullptr;
	}
	//prepare making the filter and doing FFT on it
	std::vector<cfloat> taps(c->ddc.fft_size);
	c->filtertaps_fft.assign(c->ddc.fft_size, cfloat(0, 0));
	Fft filter_taps_plan(c->ddc.fft_size, true);

	//make the filter
	float filter_half_bw = 0.5f / decimation;
	firdes_bandpass_c(taps.data(), c->ddc.taps_length, (-freq_shift) - filter_half_bw, (-freq_shift) + filter_half_bw, window);
	filter_taps_plan.execute(taps.data(), c->filtertaps_fft.data());
	fft_swap_sides(c->filtertaps_fft.data(), c->ddc.fft_size);

	//make FFT plan
	c->inv_input.assign(c->ddc.fft_size, cfloat(0, 0));
	c->inv_output.assign(c->ddc.fft_size, cfloat(0, 0));
	c->inv_plan = new Fft(c->ddc.fft_inv_size, false);
	return c;
}
