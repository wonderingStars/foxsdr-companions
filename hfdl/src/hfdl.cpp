/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * FoxSDR helper: C++ port of dumphfdl 1.7.0 src/hfdl.c (HFDL physical layer: channel filtering, symbol timing,
 * Costas loop, equalizer, preamble correlation, slot framing, de-interleaving, Viterbi decoding).
 *
 * What changed against upstream (also listed in UPSTREAM.md):
 *  - C++ with std::complex<float> (MSVC has no C complex type); liquid-dsp objects are the lq:: ports of lq.hpp.
 *  - No threads: the FFT block, the channel threads and block.c are replaced by the synchronous hfdl_rx object
 *    at the end of this file; channels are processed in command-line order for every FFT block.
 *  - Frame timestamps come from the input sample clock (util.c: helper_now_us) instead of gettimeofday().
 *  - No statsd, no data dumps, no noise-floor statistics thread. Variable-length arrays became std::vector.
 * The signal processing and framing logic is otherwise upstream's, statement for statement.
 */
#define _USE_MATH_DEFINES 1
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <complex>
#include <memory>
#include "lq.hpp"
#include "csdr.hpp"

extern "C" {
#include <inttypes.h>
#include <libacars/libacars.h>
#include "util.h"                   // XCALLOC, octet_string_new, REVERSE_BYTE, helper_now_us
#include "libfec/fec.h"             // viterbi27
#include "hfdl.h"
#include "metadata.h"
#include "pdu.h"                    // pdu_decoder_queue_push, hfdl_pdu_metadata_create
}

#define PREKEY_LEN 448
#define A_LEN 127
#define M1_LEN 127
#define M2_LEN 15
#define M_SHIFT_CNT 8
#define T_LEN 15
#define EQ_LEN 15
#define DATA_FRAME_LEN 30
#define DATA_FRAME_CNT_SINGLE_SLOT 72
#define DATA_FRAME_CNT_DOUBLE_SLOT 168
#define DATA_SYMBOLS_CNT_MAX (DATA_FRAME_CNT_DOUBLE_SLOT * DATA_FRAME_LEN)
#define PREAMBLE_LEN (2 * A_LEN + M1_LEN + M2_LEN + 9 * T_LEN)
#define SINGLE_SLOT_FRAME_LEN (PREKEY_LEN + PREAMBLE_LEN + DATA_FRAME_CNT_SINGLE_SLOT * (DATA_FRAME_LEN + T_LEN))
#define CORR_THRESHOLD_A1 0.36f
#define CORR_THRESHOLD_A2 0.3f
#define CORR_THRESHOLD_M1 0.3f
#define MAX_SEARCH_RETRIES 3
#define HFDL_SSB_CARRIER_OFFSET_HZ 1440

typedef enum {
	SAMPLER_EMIT_BITS = 1,
	SAMPLER_EMIT_SYMBOLS = 2,
	SAMPLER_SKIP = 3
} sampler_state;

typedef enum {
	FRAMER_A1_SEARCH = 1,
	FRAMER_A2_SEARCH = 2,
	FRAMER_M1_SEARCH = 3,
	FRAMER_M2_SKIP = 4,
	FRAMER_EQ_TRAIN = 5,
	FRAMER_DATA_1 = 6,      // first half of data frame
	FRAMER_DATA_2 = 7       // second half
} framer_state;

// values correspond to numbers of bits per symbol (arity)
typedef enum {
	M_UNKNOWN = 0,
	M_BPSK = 1,
	M_PSK4 = 2,
	M_PSK8 = 3
} mod_arity;
#define MOD_ARITY_MAX M_PSK8
#define MODULATION_CNT 4

struct hfdl_params {
	mod_arity scheme;
	int32_t data_segment_cnt;
	int32_t code_rate;
	int32_t deinterleaver_push_column_shift;
};

static struct hfdl_params const hfdl_frame_params[M_SHIFT_CNT] = {
	{ M_BPSK, DATA_FRAME_CNT_SINGLE_SLOT, 4, 17 },     // 300 bps, single slot
	{ M_BPSK, DATA_FRAME_CNT_SINGLE_SLOT, 2, 17 },     // 600 bps, single slot
	{ M_PSK4, DATA_FRAME_CNT_SINGLE_SLOT, 2, 17 },     // 1200 bps, single slot
	{ M_PSK8, DATA_FRAME_CNT_SINGLE_SLOT, 2, 17 },     // 1800 bps, single slot
	{ M_BPSK, DATA_FRAME_CNT_DOUBLE_SLOT, 4, 23 },     // 300 bps, double slot
	{ M_BPSK, DATA_FRAME_CNT_DOUBLE_SLOT, 2, 23 },     // 600 bps, double slot
	{ M_PSK4, DATA_FRAME_CNT_DOUBLE_SLOT, 2, 23 },     // 1200 bps, double slot
	{ M_PSK8, DATA_FRAME_CNT_DOUBLE_SLOT, 2, 23 }      // 1800 bps, double slot
};

#define SYMSYNC_PFB_CNT 16          // number of filter in symsync polyphase filterbank
#define HFDL_MF_SYMBOL_DELAY 3      // delay introduced by matched filter (measured in symbols)
#define HFDL_MF_TAPS_CNT 19         // SPS * 3 symbols of delay * 2 + 1
static float hfdl_matched_filter[HFDL_MF_TAPS_CNT] = {
	-0.0170974647427123f, 0.01148231492068473f, 0.03138375667422348f, 0.009454398851680437f,
	-0.04161644170893816f, -0.06451564801420356f, -0.005495792933327306f, 0.1316404671361545f,
	0.2759693160697777f, 0.3375901874933208f, 0.2759693160697777f, 0.1316404671361545f,
	-0.005495792933327306f, -0.06451564801420356f, -0.04161644170893816f, 0.009454398851680437f,
	0.03138375667422348f, 0.01148231492068473f, -0.0170974647427123f
};

static cfloat T_seq[2][T_LEN] = {
	{ 1.f, 1.f, 1.f, -1.f, 1.f, 1.f, -1.f, -1.f, 1.f, -1.f, 1.f, -1.f, -1.f, -1.f, -1.f },
	{ -1.f, -1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, -1.f, 1.f, -1.f, 1.f, 1.f, 1.f, 1.f }
};

static uint32_t T = 0x9AF;      // training sequence

/**********************************
 * bsequence / msequence (liquid-dsp src/sequence/src/bsequence.c, msequence.c; only what dumphfdl uses)
 **********************************/

static unsigned int count_ones(unsigned int x) {
	unsigned int n = 0;
	for (; x; x &= x - 1) n++;
	return n;
}

struct bsequence {
	std::vector<unsigned int> s;
	unsigned int num_bits, num_bits_msb, bit_mask_msb, s_len;

	explicit bsequence(unsigned int nbits) : num_bits(nbits) {
		unsigned int w = 8 * sizeof(unsigned int);
		s_len = nbits / w + ((nbits % w) > 0 ? 1 : 0);
		num_bits_msb = (nbits % w == 0) ? w : nbits % w;
		bit_mask_msb = 0;
		for (unsigned int i = 0; i < num_bits_msb; i++) {
			bit_mask_msb <<= 1;
			bit_mask_msb |= 1;
		}
		s.assign(s_len, 0);
	}
	void reset() { s.assign(s_len, 0); }
	void push(unsigned int bit) {
		unsigned int p = 8 * sizeof(unsigned int);
		s[0] <<= 1;
		s[0] &= bit_mask_msb;
		for (unsigned int i = 1; i < s_len; i++) {
			unsigned int overflow = (s[i] >> (p - 1)) & 1;
			s[i] <<= 1;
			s[i - 1] |= overflow;
		}
		s[s_len - 1] |= (bit & 1);
	}
	void init(const uint8_t *v) {
		unsigned int k = 0;
		uint8_t byte = 0, mask = 0x80;
		for (unsigned int i = 0; i < num_bits; i++) {
			if ((i % 8) == 0) {
				byte = v[k++];
				mask = 0x80;
			}
			push(byte & mask ? 1 : 0);
			mask >>= 1;
		}
	}
	int correlate(const bsequence &o) const {
		int rxy = 0;
		if (s_len != o.s_len) return 0;
		for (unsigned int i = 0; i < s_len; i++) {
			unsigned int chunk = ~(s[i] ^ o.s[i]);
			rxy += (int)count_ones(chunk);
		}
		rxy -= (int)(8 * sizeof(unsigned int) - num_bits_msb);
		return rxy;
	}
};

struct msequence {
	unsigned int m, g, a, n, state;
	msequence(unsigned int m_, unsigned int g_, unsigned int a_) : m(m_), g(g_), a(a_), n((1u << m_) - 1), state(a_) {}
	unsigned int advance() {
		unsigned int b = count_ones(state & g) & 1;     // liquid_bdotprod
		state <<= 1;
		state |= b;
		state &= n;
		return b;
	}
	void reset() { state = a; }
};

static bsequence *A_bs = nullptr;
static bsequence *M1_bs[M_SHIFT_CNT], *M2_bs[M_SHIFT_CNT];

/**********************************
 * Costas loop
 **********************************/

struct costas {
	float alpha, beta, phi, dphi, err;
};

static void costas_cccf_init(costas *c) {
	memset(c, 0, sizeof(*c));
	c->alpha = 0.1f;
	c->beta = 0.047f * c->alpha * c->alpha;
}

static cfloat costas_cccf_execute(costas *c, cfloat in) {
	// in * cexpf(-I * phi)
	return in * cfloat(cosf(c->phi), -sinf(c->phi));
}

static inline float branchless_limit(float x, float limit) {
	float x1 = fabsf(x + limit);
	float x2 = fabsf(x - limit);
	x1 -= x2;
	return (float)(0.5 * x1);
}

static void costas_cccf_adjust(costas *c, float err) {
	c->err = err;
	c->err = branchless_limit(c->err, 1.0f);
	c->phi += c->alpha * c->err;
	c->dphi += c->beta * c->err;
}

static void costas_cccf_step(costas *c) {
	c->phi += c->dphi;
	if(c->phi > M_PI) {
		c->phi -= (float)(2.0 * M_PI);
	} else if(c->phi < -M_PI) {
		c->phi += (float)(2.0 * M_PI);
	}
}

static void costas_cccf_reset(costas *c) {
	c->dphi = c->phi = 0.f;
}

/**********************************
 * Descrambler
 **********************************/

struct descrambler {
	msequence ms;
	uint32_t len;
	uint32_t pos;
	descrambler(uint32_t numbits, uint32_t genpoly, uint32_t init, uint32_t seq_len) : ms(numbits, genpoly, init), len(seq_len), pos(0) {}
	uint32_t advance() {
		if(pos == len) {
			pos = 0;
			ms.reset();
		}
		pos++;
		return ms.advance();
	}
};

// HFDL-specific descrambler (liquid-dsp >= 1.6 values: upstream's own non-legacy branch)
static descrambler *hfdl_descrambler_create(void) {
	uint32_t numbits = 15;
	uint32_t seq_len = 120;
	uint32_t lfsr_genpoly = 0x4001u;
	uint32_t lfsr_init = 0x4d4bu;    // 0x6959u reversed
	return new descrambler(numbits, lfsr_genpoly, lfsr_init, seq_len);
}

/**********************************
 * Deinterleaver
 **********************************/

#define DEINTERLEAVER_ROW_CNT 40
#define DEINTERLEAVER_POP_ROW_SHIFT 9

struct deinterleaver {
	std::vector<std::vector<uint8_t>> table;
	int32_t row = 0, col = 0;
	int32_t column_cnt;
	int32_t push_column_shift;

	explicit deinterleaver(int32_t M1) {
		column_cnt = hfdl_frame_params[M1].data_segment_cnt * DATA_FRAME_LEN
			* hfdl_frame_params[M1].scheme / DEINTERLEAVER_ROW_CNT;
		table.assign(DEINTERLEAVER_ROW_CNT, std::vector<uint8_t>(column_cnt, 0));
		push_column_shift = hfdl_frame_params[M1].deinterleaver_push_column_shift;
	}
	void push(uint8_t val) {
		table[row][col] = val;
		row++;
		if(row == DEINTERLEAVER_ROW_CNT) {
			row = 0;
			col++;
		}
		col -= push_column_shift;
		if(col < 0) {
			col += column_cnt;
		}
	}
	uint8_t pop() {
		uint8_t ret = table[row][col];
		row = (row + DEINTERLEAVER_POP_ROW_SHIFT) % DEINTERLEAVER_ROW_CNT;
		if(row == 0) {
			col++;
		}
		return ret;
	}
	void reset() { row = col = 0; }
	uint32_t table_size() const { return (uint32_t)(column_cnt * DEINTERLEAVER_ROW_CNT); }
};

/**********************************
 * One HFDL channel
 **********************************/

struct hfdl_channel {
	fft_channelizer_s *channelizer = nullptr;
	std::unique_ptr<lq::Msresamp> resampler;
	std::unique_ptr<lq::Agc> agc;
	costas loop;
	std::unique_ptr<lq::FirFilt> mf;
	std::unique_ptr<lq::EqLms> eq;
	std::unique_ptr<lq::Modem> m[MODULATION_CNT];
	std::unique_ptr<lq::SymSync> ss;
	std::unique_ptr<bsequence> bits;
	std::unique_ptr<bsequence> user_data;
	std::unique_ptr<lq::Cbuffer> training_symbols;
	std::unique_ptr<lq::Cbuffer> data_symbols;
	lq::Cbuffer *current_buffer = nullptr;
	std::unique_ptr<descrambler> desc;
	std::unique_ptr<deinterleaver> deint[M_SHIFT_CNT];
	void *viterbi_ctx[M_SHIFT_CNT];
	uint64_t symbol_cnt = 0, sample_cnt = 0;
	float resamp_rate = 1;
	sampler_state s_state = SAMPLER_EMIT_BITS;
	framer_state fr_state = FRAMER_A1_SEARCH;
	mod_arity data_mod_arity = M_UNKNOWN;
	mod_arity current_mod_arity = M_BPSK;
	int32_t chan_freq = 0;
	int32_t resampler_delay = 0;
	int32_t symbols_wanted = 0;
	int32_t search_retries = 0;
	int32_t eq_train_seq_cnt = 0;
	int32_t data_segment_cnt = 0;
	int32_t train_bits_total = 0;
	int32_t train_bits_bad = 0;
	int32_t T_idx = 0;
	int32_t M1 = 0;
	uint32_t bitmask = 0;
	uint32_t symsync_out_idx = 0;
	// PDU metadata
	long long pdu_timestamp_us = 0;
	float freq_err_hz = 0;
	float signal_level = 0;
	float noise_floor = 1.0f;

	// state that lives in upstream's thread function
	std::vector<cfloat> channelizer_output, resampled;
	uint32_t noise_floor_sampling_clk = 0;
	float frame_symbol_cnt = 0.0f;
	uint32_t symbols_produced = 0;
	uint32_t bits_out = 0;
	int32_t M1_match = -1;
	float corr_A1 = 0.f, corr_A2 = 0.f, corr_M1 = 0.f;

	hfdl_channel() { memset(viterbi_ctx, 0, sizeof(viterbi_ctx)); }
	~hfdl_channel() {
		for(int32_t i = 0; i < M_SHIFT_CNT; i++) {
			if(viterbi_ctx[i] != nullptr) delete_viterbi27(viterbi_ctx[i]);
		}
		delete channelizer;
	}
	void process(cfloat *fft_out);
	void framer_reset();
	void sampler_reset();
	void compute_train_bit_error_cnt();
	void decode_user_data();
	void dispatch_pdu(uint8_t *buf, size_t len);
};

#define LEVEL_TO_DB(level) (20.0f * log10f(level))

static int32_t match_sequence(bsequence **templates, size_t template_cnt, const bsequence &bits, float *result_corr) {
	float max_corr = 0.f;
	int32_t max_idx = -1;
	int32_t seq_len = (int32_t)bits.num_bits;
	for(size_t idx = 0; idx < template_cnt; idx++) {
		float corr = fabsf(2.0f * (float)templates[idx]->correlate(bits) / (float)seq_len - 1.0f);
		if(corr > max_corr) {
			max_corr = corr;
			max_idx = (int32_t)idx;
		}
	}
	*result_corr = max_corr;
	return max_idx;
}

static hfdl_channel *hfdl_channel_create(int32_t sample_rate, int32_t pre_decimation_rate,
		float transition_bw, int32_t centerfreq, int32_t frequency) {
	hfdl_channel *c = new hfdl_channel();
	c->resamp_rate = (float)(HFDL_SYMBOL_RATE * SPS) / ((float)sample_rate / (float)pre_decimation_rate);
	if(c->resamp_rate <= 0.5f || c->resamp_rate > 1.0f) {
		// Cannot happen for sample_rate >= HFDL_SYMBOL_RATE * SPS (see Msresamp in lq.hpp)
		fprintf(stderr, "Unsupported resampling rate %f\n", c->resamp_rate);
		delete c;
		return nullptr;
	}
	c->resampler.reset(new lq::Msresamp(c->resamp_rate, 60.0f));
	c->resampler_delay = (int32_t)ceilf(c->resampler->get_delay());

	c->chan_freq = frequency;
	float freq_shift = (float)(centerfreq - (frequency + HFDL_SSB_CARRIER_OFFSET_HZ)) / (float)sample_rate;

	c->channelizer = fft_channelizer_create(pre_decimation_rate, transition_bw, freq_shift);
	if(c->channelizer == nullptr) {
		delete c;
		return nullptr;
	}

	c->agc.reset(new lq::Agc());
	c->agc->set_bandwidth(0.005f);
	c->agc->set_bandwidth(0.01f);
	// Set the initial noise estimate to a very high value for faster convergence
	// (which is designed to be faster in downwards direction than upwards)
	c->noise_floor = 1.0f;

	costas_cccf_init(&c->loop);

	c->mf.reset(new lq::FirFilt(hfdl_matched_filter, HFDL_MF_TAPS_CNT));
	c->eq.reset(lq::EqLms::create_lowpass(EQ_LEN, 0.45f));
	c->eq->set_bw(0.1f);

	c->m[M_BPSK].reset(new lq::Modem(lq::Modem::BPSK));
	c->m[M_PSK4].reset(new lq::Modem(lq::Modem::PSK4));
	c->m[M_PSK8].reset(new lq::Modem(lq::Modem::PSK8));

	c->ss.reset(lq::SymSync::create_kaiser(SPS, HFDL_MF_SYMBOL_DELAY, 0.9f, SYMSYNC_PFB_CNT));
	c->ss->set_lf_bw(0.001f);
	c->ss->set_output_rate(2);

	c->bits.reset(new bsequence(M1_LEN));
	c->training_symbols.reset(new lq::Cbuffer(T_LEN));
	c->data_symbols.reset(new lq::Cbuffer(DATA_SYMBOLS_CNT_MAX));
	c->desc.reset(hfdl_descrambler_create());
	for(int32_t i = 0; i < M_SHIFT_CNT; i++) {
		c->deint[i].reset(new deinterleaver(i));
		struct hfdl_params const p = hfdl_frame_params[i];
		int32_t user_data_bits_cnt = p.data_segment_cnt * DATA_FRAME_LEN * p.scheme / p.code_rate;
		c->viterbi_ctx[i] = create_viterbi27(user_data_bits_cnt);
	}

	c->user_data.reset(new bsequence(DATA_SYMBOLS_CNT_MAX * MOD_ARITY_MAX));

	c->framer_reset();

	// scratch buffers (upstream allocates these at the top of the decoder thread)
	c->channelizer_output.assign(c->channelizer->ddc.post_input_size, cfloat(0, 0));
	size_t resampled_size = (size_t)((c->channelizer->ddc.post_input_size + c->resampler_delay + 10) * c->resamp_rate) + 16;
	c->resampled.assign(resampled_size, cfloat(0, 0));
	c->s_state = SAMPLER_EMIT_BITS;
	c->fr_state = FRAMER_A1_SEARCH;
	return c;
}

void hfdl_init_globals(void) {
	if(A_bs != nullptr) {
		return;
	}
	uint8_t A_octets[] = {
		0x5b, 0xbc, 0x74, 0x57, 0x03, 0xd9, 0x89, 0x39,
		0xf2, 0x08, 0xd5, 0x36, 0x94, 0x2c, 0x32, 0xfe
	};      // upstream writes these as 0b01011011, 0b10111100, 0b01110100, ...
	A_bs = new bsequence(A_LEN);
	A_bs->init(A_octets);

	uint32_t M1_bits[M1_LEN] = {
		0,1,1,1,0,1,1,0,1,1,1,1,0,1,0,0,0,1,0,1,1,0,0,
		1,0,1,1,1,1,1,0,0,0,1,0,0,0,0,0,0,1,1,0,0,1,1,0,1,1,
		0,0,0,1,1,1,0,0,1,1,1,0,1,0,1,1,1,0,0,0,0,1,0,0,1,1,
		0,0,0,0,0,1,0,1,0,1,0,1,1,0,1,0,0,1,0,0,1,0,1,0,0,1,
		1,1,1,0,0,1,0,0,0,1,1,0,1,0,1,0,0,0,0,1,1,1,1,1,1,1
	};

	size_t M_shifts[M_SHIFT_CNT] = { 72, 82, 113, 123, 61, 103, 93, 9 };
	for(int32_t shift = 0; shift < M_SHIFT_CNT; shift++) {
		M1_bs[shift] = new bsequence(M1_LEN);
		M2_bs[shift] = new bsequence(M2_LEN);
		for(int32_t j = 0; j < M1_LEN; j++) {
			M1_bs[shift]->push(M1_bits[(M_shifts[shift]+j) % M1_LEN]);
		}
		for(int32_t j = 0; j < M2_LEN; j++) {
			M2_bs[shift]->push(M1_bits[(M_shifts[shift]+j) % M1_LEN]);
		}
	}
}

/**********************************
 * Channel processing: the body of upstream's hfdl_decoder_thread() loop
 **********************************/

void hfdl_channel::process(cfloat *fft_out) {
	static long long const ts_correction_us = (long long)(PREKEY_LEN + 2 * A_LEN) * 1000000LL / HFDL_SYMBOL_RATE;
	static size_t const max_symbols_without_frame = 13 * SINGLE_SLOT_FRAME_LEN;
	cfloat r, s;
	cfloat symbols[3];
	uint32_t resampled_cnt = 0;

	channelizer->shift_status = fastddc_inv_cc(fft_out, channelizer_output.data(), channelizer);
	resampler->execute(channelizer_output.data(), (unsigned int)channelizer->shift_status.output_size,
			resampled.data(), &resampled_cnt);
	if(resampled_cnt < 1) {
		return;
	}
	for(size_t k = 0; k < resampled_cnt; k++, sample_cnt++) {
		r = agc->execute(resampled[k]);
		mf->push(r);
		s = mf->execute();

		// update noise floor estimate - every 255 samples, only when we aren't inside a frame
		if(fr_state == FRAMER_A1_SEARCH && (++noise_floor_sampling_clk & 0xFFu) == 0xFFu) {
			noise_floor = 0.65f * noise_floor +
				0.35f * fminf(noise_floor, agc->get_signal_level()) + 1e-6f;
		}
		ss->execute(&s, 1, symbols, &symbols_produced);
		for(size_t i = 0; i < symbols_produced; i++, symsync_out_idx++) {
			costas_cccf_step(&loop);
			r = costas_cccf_execute(&loop, symbols[i]);
			if(fabsf(loop.dphi) > 0.25f && fr_state == FRAMER_A1_SEARCH) {
				costas_cccf_reset(&loop);
				ss->reset();
			}

			eq->push(r);
			if(!(symsync_out_idx & 1)) {
				continue;
			}
			s = eq->execute();
			if(fr_state == FRAMER_EQ_TRAIN) {
				eq->step(T_seq[bitmask & 1][T_idx], s);
				T_idx++;
			}
			bits_out = m[current_mod_arity]->demodulate(s);
			costas_cccf_adjust(&loop, m[current_mod_arity]->get_demodulator_phase_error());
			symbol_cnt++;
			if(symbol_cnt >= max_symbols_without_frame && fr_state == FRAMER_A1_SEARCH) {
				symbol_cnt = 0;
				costas_cccf_reset(&loop);
				ss->reset();
			}

			if(s_state == SAMPLER_EMIT_BITS) {
				bits_out ^= bitmask;
				for(uint32_t b = 0; b < (uint32_t)current_mod_arity; b++, bits_out >>= 1) {
					bits->push(bits_out);
				}
			} else if(s_state == SAMPLER_EMIT_SYMBOLS) {
				current_buffer->push(s);
			} else {    // SKIP
						// NOOP
			}
			// Update signal level estimate - only when inside a frame
			if(fr_state > FRAMER_A1_SEARCH) {
				// Approximate averaging
				signal_level = (signal_level * frame_symbol_cnt + agc->get_signal_level()) / (frame_symbol_cnt + 1.0f);
				frame_symbol_cnt += 1.0f;
			}
			if(symbols_wanted > 1) {
				symbols_wanted--;
				continue;
			}

			switch(fr_state) {
			case FRAMER_A1_SEARCH:
				corr_A1 = 2.0f * (float)A_bs->correlate(*bits) / (float)A_LEN - 1.0f;
				if(fabsf(corr_A1) > CORR_THRESHOLD_A1) {
					bitmask = corr_A1 > 0.f ? 0 : ~0u;
					signal_level = agc->get_signal_level();
					frame_symbol_cnt = 1.0f;
					symbols_wanted = A_LEN;
					search_retries = 0;
					fr_state = (framer_state)(fr_state + 1);
				}
				break;
			case FRAMER_A2_SEARCH:
				corr_A2 = 2.0f * (float)A_bs->correlate(*bits) / (float)A_LEN - 1.0f;
				if(fabsf(corr_A2) > CORR_THRESHOLD_A2) {
					// Save the current timestamp and go back by the length
					// of the prekey and two A sequences, so that the timestamp
					// points at the start of the frame.
					pdu_timestamp_us = helper_now_us() - ts_correction_us;
					if(pdu_timestamp_us < 0) {
						pdu_timestamp_us = 0;
					}
					freq_err_hz = (float)(loop.dphi * HFDL_SYMBOL_RATE / (2.0 * M_PI));
					symbols_wanted = M1_LEN;
					search_retries = 0;
					fr_state = FRAMER_M1_SEARCH;
				} else if(++search_retries >= MAX_SEARCH_RETRIES) {
					framer_reset();
				}
				break;
			case FRAMER_M1_SEARCH:
				M1_match = match_sequence(M1_bs, M_SHIFT_CNT, *bits, &corr_M1);
				if(fabsf(corr_M1) > CORR_THRESHOLD_M1) {
					data_segment_cnt = hfdl_frame_params[M1_match].data_segment_cnt;
					data_mod_arity = hfdl_frame_params[M1_match].scheme;
					M1 = M1_match;
					symbols_wanted = M2_LEN;
					search_retries = 0;
					fr_state = FRAMER_M2_SKIP;
					s_state = SAMPLER_SKIP;
				} else {
					framer_reset();
				}
				break;
			case FRAMER_M2_SKIP:
				training_symbols->reset();
				symbols_wanted = T_LEN;
				eq_train_seq_cnt = 9;
				fr_state = FRAMER_EQ_TRAIN;
				s_state = SAMPLER_EMIT_SYMBOLS;
				break;
			case FRAMER_EQ_TRAIN:
				compute_train_bit_error_cnt();
				training_symbols->reset();
				if(eq_train_seq_cnt > 1) {               // next frame is training sequence
					eq_train_seq_cnt--;
					symbols_wanted = T_LEN;
					T_idx = 0;
				} else if(data_segment_cnt > 0) {        // next frame is data frame
					symbols_wanted = DATA_FRAME_LEN / 2;
					fr_state = FRAMER_DATA_1;
					current_mod_arity = data_mod_arity;
					current_buffer = data_symbols.get();
				} else {                                    // end of frame
					decode_user_data();
					framer_reset();
					symbol_cnt = 0;
				}
				break;
			case FRAMER_DATA_1:
				symbols_wanted = DATA_FRAME_LEN / 2;
				fr_state = FRAMER_DATA_2;
				break;
			case FRAMER_DATA_2:
				data_segment_cnt--;
				current_mod_arity = M_BPSK;
				current_buffer = training_symbols.get();
				fr_state = FRAMER_EQ_TRAIN;
				eq_train_seq_cnt = 1;
				symbols_wanted = T_LEN;
				T_idx = 0;
				break;
			}
		}
	}
}

void hfdl_channel::compute_train_bit_error_cnt() {
	uint32_t T_seq_bits = 0, bit = 0;
	cfloat s;
	for(int32_t i = 0; i < T_LEN; i++) {
		training_symbols->pop(&s);
		bit = m[M_BPSK]->demodulate(s);
		bit ^= (bitmask & 1);
		T_seq_bits = (T_seq_bits << 1) | bit;
	}
	int32_t error_cnt = (int32_t)count_ones(T ^ T_seq_bits);    // upstream: count_bit_errors(T, T_seq)
	train_bits_total += T_LEN;
	train_bits_bad += error_cnt;
}

void hfdl_channel::sampler_reset() {
	ss->reset();
	s_state = SAMPLER_EMIT_BITS;
	bitmask = 0;
}

void hfdl_channel::framer_reset() {
	fr_state = FRAMER_A1_SEARCH;
	symbols_wanted = 1;
	search_retries = 0;
	current_mod_arity = M_BPSK;
	train_bits_total = train_bits_bad = 0;
	T_idx = 0;
	current_buffer = training_symbols.get();
	agc->unlock();
	eq->reset();
	data_symbols->reset();
	training_symbols->reset();
	user_data->reset();
	for(int32_t i = 0; i < M_SHIFT_CNT; i++) {
		deint[i]->reset();
	}
	sampler_reset();
}

void hfdl_channel::decode_user_data() {
	static float const phase_flip[2] = { 1.0f, -1.0f };
	int32_t M1_idx = M1;
	uint32_t num_symbols = hfdl_frame_params[M1_idx].data_segment_cnt * DATA_FRAME_LEN;
	uint32_t num_encoded_bits = num_symbols * data_mod_arity;
	uint32_t descrambler_bit = 0;
	cfloat symbol;
	unsigned char soft_bits[8];
	lq::Modem *data_modem = m[data_mod_arity].get();
	for(uint32_t i = 0; i < num_symbols; i++) {
		data_symbols->pop(&symbol);
		descrambler_bit = desc->advance();
		// Flip symbol phase by M_PI when descrambler outputs 1
		// Flip symbol phase by M_PI when Costas loop synced in an opposite phase
		data_modem->demodulate_soft(symbol * phase_flip[descrambler_bit] * phase_flip[bitmask & 1], soft_bits);
		for(uint32_t j = 0; j < (uint32_t)data_mod_arity; j++) {
			deint[M1_idx]->push(soft_bits[j]);
		}
	}
	static const uint32_t CONV_CODE_RATE = 2;
	uint32_t viterbi_input_len = num_encoded_bits;
	// When FEC rate is 1/4, every chip is transmitted twice, so we take mean value of them
	if(hfdl_frame_params[M1_idx].code_rate == 4) {
		viterbi_input_len /= 2;
	}
	std::vector<uint8_t> viterbi_input(viterbi_input_len);
	if(hfdl_frame_params[M1_idx].code_rate == 4) {
		uint8_t a, b;
		for(uint32_t i = 0; i < viterbi_input_len; i++) {
			a = deint[M1_idx]->pop();
			b = deint[M1_idx]->pop();
			// Average without overflow (http://aggregate.org/MAGIC/#Average%20of%20Integers)
			viterbi_input[i] = (uint8_t)((a & b) + ((a ^ b) >> 1));
		}
	} else {    // code_rate == 2
		for(uint32_t i = 0; i < viterbi_input_len; i++) {
			viterbi_input[i] = deint[M1_idx]->pop();
		}
	}

	void *v = viterbi_ctx[M1_idx];
	uint32_t viterbi_output_len = viterbi_input_len / CONV_CODE_RATE;
	uint32_t viterbi_output_len_octets = viterbi_output_len / 8 + (viterbi_output_len % 8 != 0 ? 1 : 0);
	std::vector<uint8_t> viterbi_output(viterbi_output_len_octets);
	init_viterbi27(v, 0);
	update_viterbi27_blk(v, viterbi_input.data(), viterbi_output_len);
	chainback_viterbi27(v, viterbi_output.data(), viterbi_output_len, 0);
	for(uint32_t i = 0; i < viterbi_output_len_octets; i++) {
		viterbi_output[i] = REVERSE_BYTE(viterbi_output[i]);
	}
	dispatch_pdu(viterbi_output.data(), viterbi_output_len_octets);
}

void hfdl_channel::dispatch_pdu(uint8_t *buf, size_t len) {
	struct metadata *md = hfdl_pdu_metadata_create();
	struct hfdl_pdu_metadata *hm = container_of(md, struct hfdl_pdu_metadata, metadata);
	hm->version = 1;
	hm->freq = chan_freq;
	hm->freq_err_hz = freq_err_hz;
	hm->rssi = LEVEL_TO_DB(signal_level);
	hm->noise_floor = LEVEL_TO_DB(noise_floor);
	md->rx_timestamp.tv_sec = (long)(pdu_timestamp_us / 1000000LL);
	md->rx_timestamp.tv_usec = (long)(pdu_timestamp_us % 1000000LL);

	struct hfdl_params const *p = &hfdl_frame_params[M1];
	hm->bit_rate = HFDL_SYMBOL_RATE * p->scheme / p->code_rate *
		DATA_FRAME_LEN / (DATA_FRAME_LEN + T_LEN);
	hm->slot = p->data_segment_cnt == DATA_FRAME_CNT_SINGLE_SLOT ? 'S' : 'D';

	uint32_t flags = 0;
	uint8_t *copy = (uint8_t *)calloc(len, sizeof(uint8_t));
	memcpy(copy, buf, len);
	pdu_decoder_queue_push(md, octet_string_new(copy, len), flags);
}

/**********************************
 * The receiver: upstream's fft block (fft.c) + the channel blocks, run synchronously
 **********************************/

struct hfdl_rx {
	int32_t sample_rate = 0;
	fastddc_t ddc;
	std::unique_ptr<Fft> fwd;
	std::vector<cfloat> fft_input, fft_output;
	std::vector<cfloat> pending;
	size_t pending_pos = 0;
	std::vector<std::unique_ptr<hfdl_channel>> channels;
	uint64_t samples_consumed = 0;      // samples moved into FFT blocks
};

hfdl_rx *hfdl_rx_create(int32_t sample_rate, int32_t centerfreq, int32_t channel_cnt, int32_t const *frequencies) {
	if(sample_rate < HFDL_SYMBOL_RATE * SPS) {
		fprintf(stderr, "Sample rate too low (minimum %d S/s)\n", HFDL_SYMBOL_RATE * SPS);
		return nullptr;
	}
	if(channel_cnt < 1) {
		fprintf(stderr, "No channels given\n");
		return nullptr;
	}
	// upstream check_frequency_span()
	int32_t half_bandwidth = sample_rate / 2;
	for(int32_t i = 0; i < channel_cnt; i++) {
		if(abs(centerfreq - frequencies[i]) >= half_bandwidth) {
			fprintf(stderr, "Error: channel %d Hz is too far away from the centre (%d Hz) for a %d S/s stream "
					"(maximum distance %d Hz)\n", frequencies[i], centerfreq, sample_rate, half_bandwidth);
			return nullptr;
		}
	}
	hfdl_init_globals();

	std::unique_ptr<hfdl_rx> rx(new hfdl_rx());
	rx->sample_rate = sample_rate;
	int32_t fft_decimation_rate = compute_fft_decimation_rate(sample_rate, HFDL_SYMBOL_RATE * SPS);
	float fftfilt_transition_bw = compute_filter_relative_transition_bw(sample_rate, HFDL_CHANNEL_TRANSITION_BW_HZ);
	if(fastddc_init(&rx->ddc, fftfilt_transition_bw, fft_decimation_rate, 0)) {
		fprintf(stderr, "Error in fastddc_init()\n");
		return nullptr;
	}
	rx->fwd.reset(new Fft(rx->ddc.fft_size, true));
	rx->fft_input.assign(rx->ddc.fft_size, cfloat(0, 0));
	rx->fft_output.assign(rx->ddc.fft_size, cfloat(0, 0));
	for(int32_t i = 0; i < channel_cnt; i++) {
		hfdl_channel *c = hfdl_channel_create(sample_rate, fft_decimation_rate, fftfilt_transition_bw,
				centerfreq, frequencies[i]);
		if(c == nullptr) {
			fprintf(stderr, "Failed to initialize channel %d\n", frequencies[i]);
			return nullptr;
		}
		rx->channels.emplace_back(c);
	}
	return rx.release();
}

void hfdl_rx_push(hfdl_rx *rx, float const *iq, size_t samples) {
	rx->pending.reserve(rx->pending.size() + samples);
	for(size_t i = 0; i < samples; i++) {
		rx->pending.emplace_back(iq[2 * i], iq[2 * i + 1]);
	}
	fastddc_t *ddc = &rx->ddc;
	// Process only complete blocks (upstream waits for ddc->input_size samples in its circular buffer)
	while(rx->pending.size() - rx->pending_pos >= (size_t)ddc->input_size) {
		memmove(rx->fft_input.data(), rx->fft_input.data() + ddc->input_size, ddc->overlap_length * sizeof(cfloat));
		memcpy(rx->fft_input.data() + ddc->overlap_length, rx->pending.data() + rx->pending_pos,
				ddc->input_size * sizeof(cfloat));
		rx->pending_pos += ddc->input_size;
		rx->samples_consumed += ddc->input_size;
		helper_clock_set_elapsed_us((long long)((double)rx->samples_consumed * 1e6 / (double)rx->sample_rate));
		rx->fwd->execute(rx->fft_input.data(), rx->fft_output.data());
		fft_swap_sides(rx->fft_output.data(), ddc->fft_size);
		for(auto &c : rx->channels) {
			c->process(rx->fft_output.data());
		}
	}
	if(rx->pending_pos > 0) {
		rx->pending.erase(rx->pending.begin(), rx->pending.begin() + (std::ptrdiff_t)rx->pending_pos);
		rx->pending_pos = 0;
	}
}

uint64_t hfdl_rx_samples_processed(hfdl_rx const *rx) {
	return rx->samples_consumed;
}

void hfdl_rx_destroy(hfdl_rx *rx) {
	delete rx;
}
