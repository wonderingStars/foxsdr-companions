// SPDX-License-Identifier: MIT
/*
 * C++ port of the parts of liquid-dsp that dumphfdl uses, for the FoxSDR hfdl helper.
 *
 * liquid-dsp: https://github.com/jgaeddert/liquid-dsp, commit 10041f70cebbe3b97887e75bb41e48b73dda1b23
 * Copyright (c) 2007 - 2026 Joseph Gaeddert, MIT licence (the licence text is in third_party/liquid-dsp-LICENSE).
 *
 * Why a port and not the library: liquid-dsp is C99 code built on `float complex`, which MSVC's C compiler does
 * not have; the C++ build uses std::complex<float>. Only the objects dumphfdl calls are ported, in the crcf / cccf
 * flavours it needs, following the liquid-dsp source function by function (the original function names are in
 * the comments). Not bit-identical to a liquid-dsp build (no SIMD dot products), but the same algorithms.
 *
 *   cbuffercf                  src/buffer/src/cbuffer.proto.c
 *   windowcf, wdelayf          src/buffer/src/window.proto.c, wdelay.proto.c
 *   firdes_kaiser & helpers    src/filter/src/firdes.c, src/math/src/math.bessel.c, math.gamma.c, windows.c
 *   firpfb_crcf                src/filter/src/firpfb.proto.c
 *   resamp_crcf, msresamp_crcf src/filter/src/resamp.proto.c, msresamp.proto.c (0 half-band stages only,
 *                              which is all that dumphfdl's rates can produce: see Msresamp below)
 *   iirfiltsos_rrrf            src/filter/src/iirfiltsos.proto.c
 *   symsync_crcf               src/filter/src/symsync.proto.c
 *   firfilt_crcf               src/filter/src/firfilt.proto.c
 *   eqlms_cccf                 src/equalization/src/eqlms.proto.c
 *   agc_crcf                   src/agc/src/agc.proto.c
 *   modem (BPSK, PSK4, PSK8)   src/modem/src/modem_common.proto.c, modem_bpsk.proto.c, modem_psk.proto.c
 */
#pragma once
#include <complex>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace lq {

typedef std::complex<float> cf;

unsigned int msb_index(unsigned int x);                    // liquid_msb_index
float kaiser_beta_As(float as);                            // kaiser_beta_As
float sincf(float x);                                      // sincf
float lngammaf(float z);                                   // liquid_lngammaf
float besseli0f(float z);                                  // liquid_besseli0f
float kaiser_win(unsigned int i, unsigned int wlen, float beta);   // liquid_kaiser
void firdes_kaiser(unsigned int n, float fc, float as, float mu, float *h);   // liquid_firdes_kaiser

// cbuffercf: FIFO of fixed capacity
class Cbuffer {
public:
	explicit Cbuffer(unsigned int max_size);
	void reset();
	unsigned int size() const { return num_elements_; }
	unsigned int space_available() const { return max_size_ - num_elements_; }
	void push(cf v);
	void pop(cf *v);
private:
	std::vector<cf> v_;
	unsigned int max_size_, num_elements_ = 0, read_index_ = 0, write_index_ = 0;
};

// windowcf / windowf: sliding window, read() returns the `len` newest samples, oldest first
template <typename T>
class Window {
public:
	explicit Window(unsigned int n) : len_(n) {
		m_ = msb_index(n);
		n_ = 1u << m_;
		mask_ = n_ - 1;
		v_.assign(n_ + len_ - 1, T());
	}
	void reset() { read_index_ = 0; v_.assign(v_.size(), T()); }
	void push(T x) {
		read_index_ = (read_index_ + 1) & mask_;
		if (read_index_ == 0) {
			for (unsigned int i = 0; i + 1 < len_; i++) v_[i] = v_[n_ + i];     // memmove(v, v + n, (len-1))
		}
		v_[read_index_ + len_ - 1] = x;
	}
	const T *read() const { return v_.data() + read_index_; }
private:
	unsigned int len_, m_, n_, mask_, read_index_ = 0;
	std::vector<T> v_;
};

// wdelayf: delay line; read() returns the sample pushed `delay` pushes ago
class WdelayF {
public:
	explicit WdelayF(unsigned int delay) : delay_(delay), v_(delay + 1, 0.0f) {}
	void reset() { read_index_ = 0; v_.assign(v_.size(), 0.0f); }
	void push(float x) { v_[read_index_] = x; read_index_ = (read_index_ + 1) % (delay_ + 1); }
	float read() const { return v_[read_index_]; }
private:
	unsigned int delay_, read_index_ = 0;
	std::vector<float> v_;
};

// firpfb_crcf: polyphase filter bank, real coefficients, complex data
class FirPfb {
public:
	FirPfb(unsigned int num_filters, const float *h, unsigned int h_len);
	void reset() { w_.reset(); }
	void push(cf x) { w_.push(x); }
	cf execute(unsigned int i) const;
private:
	unsigned int num_filters_, h_sub_len_;
	std::vector<std::vector<float>> dp_;
	Window<cf> w_;
};

// resamp_crcf: arbitrary-rate resampler
class Resamp {
public:
	Resamp(float rate, unsigned int m, float fc, float as, unsigned int npfb);
	void execute(cf x, cf *y, unsigned int *num_written);
	void execute_block(const cf *x, unsigned int nx, cf *y, unsigned int *ny);
	unsigned int get_delay() const { return m_; }
private:
	void update_timing_state();
	unsigned int m_;
	float rate_, del_ = 0, tau_ = 0, bf_ = 0;
	int b_ = 0;
	float mu_ = 0;
	cf y0_, y1_;
	unsigned int npfb_;
	FirPfb f_;
	enum { BOUNDARY, INTERP } state_ = INTERP;
};

// msresamp_crcf for rates r in (0.5, 1.0]: zero half-band stages, so exactly one arbitrary resampler.
// (dumphfdl first decimates in the frequency domain so that the rate fed here is always in 5400..10800 S/s, and
// 5400 / that is in (0.5, 1]; the half-band stages of liquid's msresamp are therefore never used.)
class Msresamp {
public:
	Msresamp(float r, float as);
	void execute(const cf *x, unsigned int nx, cf *y, unsigned int *ny) { r_.execute_block(x, nx, y, ny); }
	float get_delay() const { return (float)r_.get_delay(); }
private:
	Resamp r_;
};

// iirfiltsos_rrrf: second-order IIR section (direct form II), used as the symbol timing loop filter
class IirFiltSos {
public:
	IirFiltSos(const float *b, const float *a) { set_coefficients(b, a); reset(); }
	void set_coefficients(const float *b, const float *a);
	void reset() { for (int i = 0; i < 3; i++) v_[i] = 0; }
	float execute(float x);
private:
	float b_[3], a_[3], v_[3];
};

// symsync_crcf: polyphase symbol timing synchronizer
class SymSync {
public:
	SymSync(unsigned int k, unsigned int M, const float *h, unsigned int h_len);
	static SymSync *create_kaiser(unsigned int k, unsigned int m, float beta, unsigned int M);
	void reset();
	void set_output_rate(unsigned int k_out);
	void set_lf_bw(float bt);
	void execute(const cf *x, unsigned int nx, cf *y, unsigned int *ny);
private:
	void step(cf x, cf *y, unsigned int *ny);
	void advance_internal_loop(cf mf, cf dmf);
	unsigned int h_len_, k_, k_out_ = 1, decim_counter_ = 0;
	int is_locked_ = 0;
	float rate_ = 1, del_ = 1, tau_ = 0, tau_decim_ = 0, bf_ = 0;
	int b_ = 0;
	float q_ = 0, q_hat_ = 0;
	float B_[3], A_[3];
	IirFiltSos pll_;
	float rate_adjustment_ = 0;
	unsigned int npfb_;
	FirPfb mf_, dmf_;
};

// firfilt_crcf: FIR filter, real taps
class FirFilt {
public:
	FirFilt(const float *h, unsigned int n);
	void push(cf x) { w_.push(x); }
	cf execute() const;
private:
	std::vector<float> h_;     // stored reversed, as dotprod_crcf_create_rev does
	Window<cf> w_;
};

// eqlms_cccf: least-mean-squares equalizer, complex taps
class EqLms {
public:
	EqLms(const cf *h, unsigned int h_len);
	static EqLms *create_lowpass(unsigned int h_len, float fc);
	void reset();
	void set_bw(float mu) { mu_ = mu; }
	void push(cf x);
	cf execute() const;
	void step(cf d, cf d_hat);
private:
	unsigned int h_len_;
	float mu_ = 0.5f;
	std::vector<cf> h0_, w0_, w1_;
	unsigned int count_ = 0;
	int buf_full_ = 0;
	Window<cf> buffer_;
	WdelayF x2_;
	float x2_sum_ = 0;
};

// agc_crcf
class Agc {
public:
	Agc();
	void reset();
	void set_bandwidth(float bt) { bandwidth_ = bt; alpha_ = bt; }
	cf execute(cf x);
	void unlock() { is_locked_ = 0; }
	float get_signal_level() const { return 1.0f / g_; }
	float get_rssi() const;
	float get_gain() const { return g_; }
private:
	float g_ = 1, scale_ = 1, bandwidth_ = 0, alpha_ = 0, y2_prime_ = 1;
	int is_locked_ = 0;
};

// modem: BPSK, PSK4 and PSK8 (liquid modem_create(LIQUID_MODEM_BPSK / PSK4 / PSK8))
class Modem {
public:
	enum Scheme { BPSK, PSK4, PSK8 };
	explicit Modem(Scheme s);
	// returns the hard-decision symbol; remembers the received sample and the re-modulated symbol
	unsigned int demodulate(cf x);
	// returns the symbol; writes bps soft bits (0..255, 127 = erasure)
	unsigned int demodulate_soft(cf x, unsigned char *soft_bits);
	float get_demodulator_phase_error() const { return std::imag(r_ * std::conj(x_hat_)); }
	unsigned int bits_per_symbol() const { return m_; }
private:
	cf modulate_psk(unsigned int sym) const;
	unsigned int demodulate_psk(cf x);
	Scheme scheme_;
	unsigned int m_, M_;
	cf r_ = 1.0f, x_hat_ = 1.0f;
	float alpha_ = 0, d_phi_ = 0, ref_[8] = {0};
	std::vector<cf> symbol_map_;
	std::vector<unsigned char> neighbors_;
	unsigned int demod_soft_p_ = 0;
};

}  // namespace lq
