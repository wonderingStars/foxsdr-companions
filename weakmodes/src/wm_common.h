// weakmodes: common declarations shared by the mode decoders (FT2L, MSK144, JT9) and the program.
// Part of the FoxSDR "weakmodes" GPL helper. Licence: GPL-3.0-or-later; see LICENSE and UPSTREAM.md.
//
// Samples: every decoder is fed `float` samples at 12000 Hz, mono, with the SCALE OF A 16-BIT INTEGER
// (full scale = +-32768), exactly what WSJT-X's Fortran decoders receive as `dd(:)` or `iwave(:)`.
#pragma once
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wm {

typedef std::complex<double> cplx;

constexpr int kRate = 12000; // the only sample rate the decoders work at

// One decoded message.
struct Decode {
    std::string mode;    // "FT2L", "MSK144", "JT9"
    double t_start = 0;  // time of the transmission start (or of the ping), seconds on the sample clock
    double dt = 0;       // time offset against the nominal period start, seconds (as WSJT-X prints it)
    double freq = 0;     // audio frequency, Hz (carrier or lowest tone, as WSJT-X shows it)
    int snr = 0;         // dB in 2500 Hz reference bandwidth (WSJT-X convention)
    std::string text;    // message text, as WSJT-X shows it
    std::string note;    // optional extra remark (not used by every mode)
};
typedef std::function<void(const Decode &)> Emit;

// Options common to the stream decoders (set from the command line).
struct Options {
    int fmin = 200;      // lowest audio frequency searched, Hz
    int fmax = 4000;     // highest audio frequency searched, Hz
    int depth = 3;       // decoding depth: 1 fast .. 3 deepest (what each mode does with it is said in its file)
    double offset = 0.0; // seconds from the first sample to the first period boundary
};

// A stream decoder: samples go in (any block size, the result must not depend on it), decodes come out
// through Emit when a period has been received. finish() is called at end of input and decodes what is left.
class StreamDecoder {
public:
    virtual ~StreamDecoder() {}
    virtual void feed(const float *x, size_t n) = 0; // x in 16-bit-integer scale
    virtual void finish() = 0;
};

StreamDecoder *make_ft2_decoder(const Options &o, const Emit &e);    // ft2.cpp
StreamDecoder *make_msk144_decoder(const Options &o, const Emit &e); // msk144.cpp
StreamDecoder *make_jt9_decoder(const Options &o, const Emit &e);    // jt9.cpp

// In-place complex FFT of any length n >= 1 (power of two: radix 2; otherwise Bluestein).
// isign = -1: X[k] = sum x[j] exp(-2 pi i jk/n); isign = +1: exp(+...). Not normalised (like FFTW / four2a).
void fft(cplx *x, int n, int isign);
// Real-input FFT as four2a(a,n,1,-1,0) gives it: out[0..n/2] (n/2+1 values) of the forward transform.
void rfft(const double *in, int n, cplx *out);
// Inverse of rfft, as four2a(a,n,1,+1,-1): in[0..n/2] -> out[0..n-1], not normalised.
void irfft(const cplx *in, int n, double *out);

// packjt77 unpack of a 77-bit message (bits[i] is 0 or 1), as WSJT-X shows the text. ok=false if the
// message is not valid (WSJT-X's unpk77_success). Used by FT2L and MSK144. Defined in unpack77.cpp.
std::string unpack77(const uint8_t *bits, bool *ok);
void unpack77_reset();

// Deterministic pseudo-random numbers for the test generators (not used by the decoders).
class Rng {
public:
    explicit Rng(uint64_t seed) : s_(seed * 2654435769ull + 88172645463325252ull) {}
    uint64_t next() { s_ ^= s_ << 13; s_ ^= s_ >> 7; s_ ^= s_ << 17; return s_; }
    double uniform() { return (double)(next() >> 11) / 9007199254740992.0; } // [0,1)
    double gauss() { // N(0,1), Box-Muller
        double u1 = uniform(), u2 = uniform();
        if (u1 < 1e-300) u1 = 1e-300;
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }
private:
    uint64_t s_;
};

} // namespace wm
