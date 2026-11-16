// SPDX-License-Identifier: MIT
// C++ port of the liquid-dsp objects used by dumphfdl: see lq.hpp for sources, licence and scope.
#define _USE_MATH_DEFINES 1
#include <cmath>
#include <cfloat>
#include <cstring>
#include "lq.hpp"

namespace lq {

/********************************************************************
 * math: liquid_msb_index, kaiser window, Bessel I0, gamma, sinc
 ********************************************************************/

// utility/src/msb_index.c: liquid_msb_index
unsigned int msb_index(unsigned int x) {
	unsigned int bits = 0;
	while (x != 0) {
		bits++;
		x >>= 1;
	}
	return bits;
}

// math/src/math.gamma.c: liquid_lngammaf
float lngammaf(float z) {
	float g;
	if (z < 0) {
		return 0.0f;
	} else if (z < 10.0f) {
		return lngammaf(z + 1.0f) - logf(z);
	} else {
		g = (float)(0.5 * (logf(2 * (float)M_PI) - log((double)z)));
		g += z * (logf(z + (1 / (12.0f * z - 0.1f / z))) - 1);
	}
	return g;
}

static float gammaf_(float z) {          // liquid_gammaf, z >= 0 only (all that is needed here)
	return expf(lngammaf(z));
}

// math/src/math.bessel.c: liquid_lnbesselif / liquid_besselif / liquid_besseli0f
static float lnbesselif(float nu, float z) {
	if (z == 0) {
		return nu == 0.0f ? 0.0f : -FLT_MAX;
	}
	if (nu == 0.5f) {
		return 0.5f * logf(2.0f / ((float)M_PI * z)) + logf(sinhf(z));
	}
	if (z < 1e-3f * sqrtf(nu + 1.0f)) {
		return -lngammaf(nu + 1.0f) + nu * logf(0.5f * z);
	}
	float t0 = nu * logf(0.5f * z);
	float t1 = 0.0f, t2 = 0.0f, t3 = 0.0f, y = 0.0f;
	for (unsigned int k = 0; k < 64; k++) {
		t1 = 2.0f * k * logf(0.5f * z);
		t2 = lngammaf((float)k + 1.0f);
		t3 = lngammaf(nu + (float)k + 1.0f);
		y += expf(t1 - t2 - t3);
	}
	return t0 + logf(y);
}

static float besselif(float nu, float z) {
	if (z == 0) {
		return nu == 0.0f ? 1.0f : 0.0f;
	}
	if (nu == 0.5f) {
		return sqrtf(2.0f / ((float)M_PI * z)) * sinhf(z);
	}
	if (z < 1e-3f * sqrtf(nu + 1.0f)) {
		return powf(0.5f * z, nu) / gammaf_(nu + 1.0f);
	}
	return expf(lnbesselif(nu, z));
}

float besseli0f(float z) {
	return besselif(0, z);
}

// math/src/windows.c: liquid_kaiser
float kaiser_win(unsigned int i, unsigned int wlen, float beta) {
	float t = (float)i - (float)(wlen - 1) / 2;
	float r = 2.0f * t / (float)(wlen - 1);
	float a = besseli0f(beta * sqrtf(1 - r * r));
	float b = besseli0f(beta);
	return a / b;
}

// math/src/math.c: sincf
float sincf(float x) {
	if (fabsf(x) < 0.01f) {
		return cosf((float)M_PI * x / 2.0f) * cosf((float)M_PI * x / 4.0f) * cosf((float)M_PI * x / 8.0f);
	}
	return sinf((float)M_PI * x) / ((float)M_PI * x);
}

// filter/src/firdes.c: kaiser_beta_As
float kaiser_beta_As(float as) {
	as = fabsf(as);
	float beta;
	if (as > 50.0f) {
		beta = 0.1102f * (as - 8.7f);
	} else if (as > 21.0f) {
		beta = (float)(0.5842 * powf(as - 21, 0.4f) + 0.07886f * (as - 21));
	} else {
		beta = 0.0f;
	}
	if (as > 110.0f) {
		beta *= 1.02f;
	}
	return beta;
}

// filter/src/firdes.c: liquid_firdes_kaiser
void firdes_kaiser(unsigned int n, float fc, float as, float mu, float *h) {
	float beta = kaiser_beta_As(as);
	for (unsigned int i = 0; i < n; i++) {
		float t = (float)i - (float)(n - 1) / 2 + mu;
		float h1 = sincf(2.0f * fc * t);
		float h2 = kaiser_win(i, n, beta);
		h[i] = h1 * h2;
	}
}

/********************************************************************
 * cbuffercf (buffer/src/cbuffer.proto.c)
 ********************************************************************/

Cbuffer::Cbuffer(unsigned int max_size) : v_(max_size), max_size_(max_size) {}

void Cbuffer::reset() {
	read_index_ = 0;
	write_index_ = 0;
	num_elements_ = 0;
}

void Cbuffer::push(cf v) {
	v_[write_index_] = v;
	write_index_ = (write_index_ + 1) % max_size_;
	num_elements_++;
}

void Cbuffer::pop(cf *v) {
	if (v != nullptr) {
		*v = v_[read_index_];
	}
	read_index_ = (read_index_ + 1) % max_size_;
	num_elements_--;
}

/********************************************************************
 * firpfb_crcf (filter/src/firpfb.proto.c)
 ********************************************************************/

FirPfb::FirPfb(unsigned int num_filters, const float *h, unsigned int h_len)
	: num_filters_(num_filters), h_sub_len_(h_len / num_filters), w_(h_len / num_filters) {
	dp_.resize(num_filters_);
	for (unsigned int i = 0; i < num_filters_; i++) {
		std::vector<float> h_sub(h_sub_len_);
		for (unsigned int n = 0; n < h_sub_len_; n++) {
			h_sub[h_sub_len_ - n - 1] = h[i + n * num_filters_];
		}
		dp_[i] = h_sub;
	}
	w_.reset();
}

// firpfb_crcf_execute (+ dotprod_crcf_execute)
cf FirPfb::execute(unsigned int i) const {
	const cf *r = w_.read();
	const float *h = dp_[i].data();
	float re = 0.0f, im = 0.0f;
	for (unsigned int k = 0; k < h_sub_len_; k++) {
		re += h[k] * r[k].real();
		im += h[k] * r[k].imag();
	}
	return cf(re, im);      // scale is 1
}

/********************************************************************
 * resamp_crcf (filter/src/resamp.proto.c), msresamp_crcf with 0 half-band stages
 ********************************************************************/

static std::vector<float> resamp_prototype(unsigned int m, float fc, float as, unsigned int npfb) {
	unsigned int n = 2 * m * npfb + 1;
	std::vector<float> hf(n), h(n);
	firdes_kaiser(n, fc / (float)npfb, as, 0.0f, hf.data());
	float gain = 0.0f;
	for (unsigned int i = 0; i < n; i++) {
		gain += hf[i];
	}
	gain = (float)npfb / gain;
	for (unsigned int i = 0; i < n; i++) {
		h[i] = hf[i] * gain;
	}
	return h;
}

// resamp_crcf_create
Resamp::Resamp(float rate, unsigned int m, float fc, float as, unsigned int npfb)
	: m_(m), rate_(rate), npfb_(npfb),
	  f_(npfb, resamp_prototype(m, fc, as, npfb).data(), 2 * m * npfb) {
	del_ = 1.0f / rate_;
	// resamp_crcf_reset
	f_.reset();
	state_ = INTERP;
	tau_ = 0.0f;
	bf_ = 0.0f;
	b_ = 0;
	mu_ = 0.0f;
	y0_ = 0;
	y1_ = 0;
}

// resamp_crcf_update_timing_state
void Resamp::update_timing_state() {
	tau_ += del_;
	bf_ = tau_ * (float)npfb_;
	b_ = (int)floorf(bf_);
	mu_ = bf_ - (float)b_;
}

// resamp_crcf_execute
void Resamp::execute(cf x, cf *y, unsigned int *num_written) {
	f_.push(x);
	unsigned int n = 0;
	while ((unsigned int)b_ < npfb_) {
		switch (state_) {
		case BOUNDARY:
			y1_ = f_.execute(0);
			y[n++] = (1.0f - mu_) * y0_ + mu_ * y1_;
			update_timing_state();
			state_ = INTERP;
			break;
		case INTERP:
			y0_ = f_.execute((unsigned int)b_);
			if ((unsigned int)b_ == npfb_ - 1) {
				state_ = BOUNDARY;
				b_ = (int)npfb_;
			} else {
				y1_ = f_.execute((unsigned int)b_ + 1);
				y[n++] = (1.0f - mu_) * y0_ + mu_ * y1_;
				update_timing_state();
			}
			break;
		}
	}
	tau_ -= 1.0f;
	bf_ -= (float)npfb_;
	b_ -= (int)npfb_;
	*num_written = n;
}

// resamp_crcf_execute_block
void Resamp::execute_block(const cf *x, unsigned int nx, cf *y, unsigned int *ny) {
	unsigned int n = 0, w = 0;
	for (unsigned int i = 0; i < nx; i++) {
		execute(x[i], &y[n], &w);
		n += w;
	}
	*ny = n;
}

static inline float fminf_(float a, float b) { return a < b ? a : b; }

// msresamp_crcf_create for 0.5 < r <= 1.0: rate_arbitrary = r, no half-band stage
// (resamp_crcf_create(rate, 7, min(0.515*rate, 0.49), as, 256))
Msresamp::Msresamp(float r, float as) : r_(r, 7, fminf_(0.515f * r, 0.49f), as, 256) {}

/********************************************************************
 * iirfiltsos_rrrf (filter/src/iirfiltsos.proto.c)
 ********************************************************************/

void IirFiltSos::set_coefficients(const float *b, const float *a) {
	float a0 = a[0];
	b_[0] = b[0] / a0;
	b_[1] = b[1] / a0;
	b_[2] = b[2] / a0;
	a_[0] = a[0] / a0;
	a_[1] = a[1] / a0;
	a_[2] = a[2] / a0;
}

// iirfiltsos_rrrf_execute_df2
float IirFiltSos::execute(float x) {
	v_[2] = v_[1];
	v_[1] = v_[0];
	v_[0] = x - a_[1] * v_[1] - a_[2] * v_[2];
	return b_[0] * v_[0] + b_[1] * v_[1] + b_[2] * v_[2];
}

/********************************************************************
 * symsync_crcf (filter/src/symsync.proto.c)
 ********************************************************************/

// symsync_crcf_create
SymSync::SymSync(unsigned int k, unsigned int M, const float *h, unsigned int h_len)
	: k_(k),
	  B_{0.0f, 0.0f, 0.0f}, A_{1.0f, 0.0f, 0.0f},
	  pll_(B_, A_),
	  npfb_(M),
	  mf_(M, h, h_len),
	  dmf_(M, [&]() {
		  // derivative matched filter coefficients, normalised
		  static thread_local std::vector<float> dh;
		  dh.assign(h_len, 0.0f);
		  float hdh_max = 0.0f;
		  for (unsigned int i = 0; i < h_len; i++) {
			  if (i == 0) {
				  dh[i] = h[i + 1] - h[h_len - 1];
			  } else if (i == h_len - 1) {
				  dh[i] = h[0] - h[i - 1];
			  } else {
				  dh[i] = h[i + 1] - h[i - 1];
			  }
			  if (fabsf(h[i] * dh[i]) > hdh_max || i == 0) {
				  hdh_max = fabsf(h[i] * dh[i]);
			  }
		  }
		  for (unsigned int i = 0; i < h_len; i++) {
			  dh[i] *= 0.06f / hdh_max;
		  }
		  return dh.data();
	  }(), h_len) {
	set_output_rate(1);
	h_len_ = (h_len - 1) / npfb_;
	reset();
	set_lf_bw(0.01f);
	is_locked_ = 0;
}

// symsync_crcf_create_kaiser
SymSync *SymSync::create_kaiser(unsigned int k, unsigned int m, float beta, unsigned int M) {
	(void)beta;     // liquid-dsp validates beta in [0,1] but does not otherwise use it in this constructor
	unsigned int H_len = 2 * M * k * m + 1;
	std::vector<float> Hf(H_len), H(H_len);
	float fc = 0.75f;
	float as = 40.0f;
	firdes_kaiser(H_len, fc / (float)(k * M), as, 0.0f, Hf.data());
	for (unsigned int i = 0; i < H_len; i++) {
		H[i] = Hf[i] * 2.0f * fc;
	}
	return new SymSync(k, M, H.data(), H_len);
}

// symsync_crcf_reset (only the matched filter is cleared, as in liquid-dsp)
void SymSync::reset() {
	mf_.reset();
	rate_ = (float)k_ / (float)k_out_;
	del_ = rate_;
	b_ = 0;
	bf_ = 0.0f;
	tau_ = 0.0f;
	tau_decim_ = 0.0f;
	q_ = 0.0f;
	q_hat_ = 0.0f;
	decim_counter_ = 0;
	pll_.reset();
}

void SymSync::set_output_rate(unsigned int k_out) {
	k_out_ = k_out;
	rate_ = (float)k_ / (float)k_out_;
	del_ = rate_;
}

void SymSync::set_lf_bw(float bt) {
	float alpha = 1.000f - bt;
	float beta = 0.220f * bt;
	float a = 0.500f;
	float b = 0.495f;
	B_[0] = beta;
	B_[1] = 0.00f;
	B_[2] = 0.00f;
	A_[0] = 1.00f - a * alpha;
	A_[1] = -b * alpha;
	A_[2] = 0.00f;
	pll_.set_coefficients(B_, A_);
	rate_adjustment_ = (float)(0.5 * bt);
}

void SymSync::execute(const cf *x, unsigned int nx, cf *y, unsigned int *ny) {
	unsigned int n = 0, k = 0;
	for (unsigned int i = 0; i < nx; i++) {
		step(x[i], &y[n], &k);
		n += k;
	}
	*ny = n;
}

// symsync_crcf_step
void SymSync::step(cf x, cf *y, unsigned int *ny) {
	mf_.push(x);
	dmf_.push(x);
	cf mf, dmf;
	unsigned int n = 0;
	while (b_ < (int)npfb_) {
		mf = mf_.execute((unsigned int)b_);
		y[n] = mf / (float)k_;
		if (decim_counter_ == k_out_) {
			decim_counter_ = 0;
			if (is_locked_) {
				continue;
			}
			dmf = dmf_.execute((unsigned int)b_);
			advance_internal_loop(mf, dmf);
			tau_decim_ = tau_;
		}
		decim_counter_++;
		tau_ += del_;
		bf_ = tau_ * (float)npfb_;
		b_ = (int)roundf(bf_);
		n++;
	}
	tau_ -= 1.0f;
	bf_ -= (float)npfb_;
	b_ -= (int)npfb_;
	*ny = n;
}

// symsync_crcf_advance_internal_loop
void SymSync::advance_internal_loop(cf mf, cf dmf) {
	q_ = std::real(std::conj(mf) * dmf);
	if (q_ > 1.0f) {
		q_ = 1.0f;
	} else if (q_ < -1.0f) {
		q_ = -1.0f;
	}
	q_hat_ = pll_.execute(q_);
	rate_ += rate_adjustment_ * q_hat_;
	del_ = rate_ + q_hat_;
}

/********************************************************************
 * firfilt_crcf (filter/src/firfilt.proto.c)
 ********************************************************************/

FirFilt::FirFilt(const float *h, unsigned int n) : h_(n), w_(n) {
	for (unsigned int i = 0; i < n; i++) {
		h_[i] = h[n - 1 - i];       // dotprod_crcf_create_rev
	}
	w_.reset();
}

cf FirFilt::execute() const {
	const cf *r = w_.read();
	float re = 0.0f, im = 0.0f;
	for (size_t i = 0; i < h_.size(); i++) {
		re += h_[i] * r[i].real();
		im += h_[i] * r[i].imag();
	}
	return cf(re, im);      // scale is 1
}

/********************************************************************
 * eqlms_cccf (equalization/src/eqlms.proto.c)
 ********************************************************************/

EqLms::EqLms(const cf *h, unsigned int h_len)
	: h_len_(h_len), h0_(h_len), w0_(h_len), w1_(h_len), buffer_(h_len), x2_(h_len) {
	if (h == nullptr) {
		for (unsigned int i = 0; i < h_len_; i++) {
			h0_[i] = (i == h_len_ / 2) ? 1.0f : 0.0f;
		}
	} else {
		for (unsigned int i = 0; i < h_len_; i++) {
			h0_[i] = std::conj(h[h_len_ - i - 1]);
		}
	}
	reset();
}

// eqlms_cccf_create_lowpass
EqLms *EqLms::create_lowpass(unsigned int h_len, float fc) {
	std::vector<float> h(h_len);
	std::vector<cf> hc(h_len);
	firdes_kaiser(h_len, fc, 40.0f, 0.0f, h.data());
	for (unsigned int i = 0; i < h_len; i++) {
		hc[i] = h[i] * 2 * fc;
	}
	return new EqLms(hc.data(), h_len);
}

void EqLms::reset() {
	w0_ = h0_;
	buffer_.reset();
	x2_.reset();
	count_ = 0;
	buf_full_ = 0;
	x2_sum_ = 0;
}

// eqlms_cccf_push + eqlms_cccf_update_sumsq
void EqLms::push(cf x) {
	buffer_.push(x);
	float x2_n = std::real(x * std::conj(x));
	x2_.push(x2_n);
	float x2_0 = x2_.read();
	x2_sum_ = x2_sum_ + x2_n - x2_0;
	count_++;
}

// eqlms_cccf_execute
cf EqLms::execute() const {
	cf y = 0;
	const cf *r = buffer_.read();
	for (unsigned int i = 0; i < h_len_; i++) {
		cf sum = std::conj(w0_[i]) * r[i];
		y += sum;
	}
	return y;
}

// eqlms_cccf_step
void EqLms::step(cf d, cf d_hat) {
	if (!buf_full_) {
		if (count_ < h_len_) {
			return;
		}
		buf_full_ = 1;
	}
	cf alpha = d - d_hat;
	const cf *r = buffer_.read();
	if (x2_sum_ > 0.0f) {
		for (unsigned int i = 0; i < h_len_; i++) {
			w1_[i] = w0_[i] + (mu_ * std::conj(alpha)) * r[i] / x2_sum_;
		}
	} else {
		w1_ = w0_;
	}
	w0_ = w1_;
}

/********************************************************************
 * agc_crcf (agc/src/agc.proto.c)
 ********************************************************************/

Agc::Agc() {
	set_bandwidth(1e-2f);
	reset();
	scale_ = 1;
}

void Agc::reset() {
	g_ = 1.0f;
	y2_prime_ = 1.0f;
	is_locked_ = 0;
}

cf Agc::execute(cf x) {
	cf y = x * g_;
	float y2 = std::real(y * std::conj(y));
	y2_prime_ = (float)((1.0 - alpha_) * y2_prime_ + alpha_ * y2);
	if (is_locked_) {
		return y;
	}
	if (y2_prime_ > 1e-6f) {
		g_ *= expf(-0.5f * alpha_ * logf(y2_prime_));
	}
	g_ = (g_ > 1e6f) ? 1e6f : g_;
	y *= scale_;
	return y;
}

float Agc::get_rssi() const {
	return (float)(-20 * log10((double)g_));
}

/********************************************************************
 * modem (modem/src/modem_common.proto.c, modem_bpsk.proto.c, modem_psk.proto.c)
 ********************************************************************/

static unsigned int gray_encode(unsigned int s) { return s ^ (s >> 1); }
static unsigned int gray_decode(unsigned int s) {
	unsigned int mask = s, out = s;
	for (unsigned int i = 0; i < 8; i += 4) {
		out ^= (mask >> 1);
		out ^= (mask >> 2);
		out ^= (mask >> 3);
		out ^= (mask >> 4);
		mask >>= 4;
	}
	return out;
}

Modem::Modem(Scheme s) : scheme_(s) {
	if (s == BPSK) {
		m_ = 1;
		M_ = 2;
		return;
	}
	m_ = (s == PSK4) ? 2 : 3;
	M_ = 1u << m_;
	// modem_create_psk
	alpha_ = (float)M_PI / (float)M_;
	for (unsigned int k = 0; k < m_; k++) {
		ref_[k] = (float)(1 << k) * alpha_;
	}
	d_phi_ = (float)M_PI * (1.0f - 1.0f / (float)M_);
	symbol_map_.resize(M_);
	for (unsigned int i = 0; i < M_; i++) {
		symbol_map_[i] = modulate_psk(i);
	}
	if (m_ >= 3) {
		// modem_demodsoft_gentab(q, 2)
		const unsigned int p = 2;
		demod_soft_p_ = p;
		neighbors_.assign(M_ * p, 0);
		std::vector<cf> c(M_);
		for (unsigned int i = 0; i < M_; i++) {
			c[i] = symbol_map_[i];
		}
		for (unsigned int i = 0; i < M_; i++) {
			for (unsigned int k = 0; k < p; k++) {
				neighbors_[i * p + k] = (unsigned char)M_;
			}
		}
		for (unsigned int i = 0; i < M_; i++) {
			for (unsigned int k = 0; k < p; k++) {
				float dmin = 1e9f;
				for (unsigned int j = 0; j < M_; j++) {
					int symbol_valid = 1;
					if (i == j) {
						symbol_valid = 0;
					}
					for (unsigned int l = 0; l < p; l++) {
						if (neighbors_[i * p + l] == j) {
							symbol_valid = 0;
						}
					}
					float d = std::abs(c[i] - c[j]);
					if (d < dmin && symbol_valid) {
						dmin = d;
						neighbors_[i * p + k] = (unsigned char)j;
					}
				}
			}
		}
	}
	r_ = 1.0f;
	x_hat_ = r_;
}

// modem_modulate_psk
cf Modem::modulate_psk(unsigned int sym_in) const {
	sym_in = gray_decode(sym_in);
	float theta = (float)sym_in * 2 * alpha_;
	return cf(cosf(theta), sinf(theta));
}

// modem_demodulate_psk
unsigned int Modem::demodulate_psk(cf x) {
	float theta = std::arg(x);
	theta -= d_phi_;
	if (theta < -(float)M_PI) {
		theta += 2 * (float)M_PI;
	}
	// modem_demodulate_linear_array_ref
	unsigned int s = 0;
	float v = theta;
	for (unsigned int i = 0; i < m_; i++) {
		s <<= 1;
		if (v > 0) {
			s |= 1;
			v -= ref_[m_ - i - 1];
		} else {
			s |= 0;
			v += ref_[m_ - i - 1];
		}
	}
	unsigned int sym_out = gray_encode(s);
	x_hat_ = symbol_map_[sym_out];
	r_ = x;
	return sym_out;
}

unsigned int Modem::demodulate(cf x) {
	if (scheme_ == BPSK) {
		unsigned int sym = (std::real(x) > 0) ? 0 : 1;
		x_hat_ = sym ? -1.0f : 1.0f;
		r_ = x;
		return sym;
	}
	return demodulate_psk(x);
}

unsigned int Modem::demodulate_soft(cf x, unsigned char *soft_bits) {
	if (scheme_ == BPSK) {
		// modem_demodulate_soft_bpsk
		float gamma = 4.0f;
		float LLR = -2.0f * std::real(x) * gamma;
		int soft_bit = (int)(LLR * 16 + 127);
		if (soft_bit > 255) soft_bit = 255;
		if (soft_bit < 0) soft_bit = 0;
		soft_bits[0] = (unsigned char)soft_bit;
		unsigned int sym = (std::real(x) > 0) ? 0 : 1;
		x_hat_ = sym ? -1.0f : 1.0f;
		r_ = x;
		return sym;
	}
	if (neighbors_.empty()) {
		// no soft table (PSK4): hard decisions as 0 / 255 soft bits (liquid_unpack_soft_bits)
		unsigned int sym = demodulate_psk(x);
		for (unsigned int i = 0; i < m_; i++) {
			soft_bits[i] = ((sym >> (m_ - i - 1)) & 1) ? 255 : 0;
		}
		return sym;
	}
	// modem_demodulate_soft_table
	unsigned int s = demodulate_psk(x);
	const unsigned int bps = m_;
	const unsigned int p = demod_soft_p_;
	float gamma = 1.2f * (float)M_;
	float dmin_0[8], dmin_1[8];
	for (unsigned int k = 0; k < bps; k++) {
		dmin_0[k] = 8.0f;
		dmin_1[k] = 8.0f;
	}
	cf e0 = x - x_hat_;
	float d = std::real(e0 * std::conj(e0));
	for (unsigned int k = 0; k < bps; k++) {
		unsigned int bit = (s >> (bps - k - 1)) & 0x01;
		if (bit) dmin_1[k] = d; else dmin_0[k] = d;
	}
	for (unsigned int i = 0; i < p; i++) {
		cf x_hat = symbol_map_[neighbors_[s * p + i]];
		cf e = x - x_hat;
		d = std::real(e) * std::real(e) + std::imag(e) * std::imag(e);
		for (unsigned int k = 0; k < bps; k++) {
			unsigned int bit = (neighbors_[s * p + i] >> (bps - k - 1)) & 0x01;
			if (bit) {
				if (d < dmin_1[k]) dmin_1[k] = d;
			} else {
				if (d < dmin_0[k]) dmin_0[k] = d;
			}
		}
	}
	for (unsigned int k = 0; k < bps; k++) {
		int soft_bit = (int)(((dmin_0[k] - dmin_1[k]) * gamma) * 16 + 127);
		if (soft_bit > 255) soft_bit = 255;
		if (soft_bit < 0) soft_bit = 0;
		soft_bits[k] = (unsigned char)soft_bit;
	}
	return s;
}

}  // namespace lq
