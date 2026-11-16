// weakmodes: FT2L channel symbols and waveform (shared by the decoder's signal subtraction and the test generator).
// Port of genft2l.f90 and gen_ft2lwave.f90 from wsjtx-ft2libre (see ft2_wave.cpp).
#pragma once
#include <complex>
#include <cstdint>
#include <vector>

namespace wm {

constexpr int FT2_NSPS = 288;   // samples per symbol at 12000 S/s
constexpr int FT2_NN = 103;     // sync + data symbols
constexpr int FT2_NMAX = 45000; // samples in one 3.75 s period

// The scrambling vector rvec(77) XORed onto the message before encoding (and removed after decoding)
extern const uint8_t kFt2Rvec[77];

// get_ft2l_tones_from_77bits: msgbits (77 bits, 0/1, NOT yet scrambled) -> 103 tones 0..3
void ft2l_tones_from_77bits(const uint8_t *msgbits, int *i4tone);

// gen_ft2lwave (complex form): (nsym+2)*nsps samples of unit-amplitude carrier at f0 Hz, GFSK, with raised-cosine
// ramps in the first and last symbol slots. cwave must hold (nsym+2)*nsps values.
void gen_ft2lwave(const int *itone, int nsym, int nsps, double fsample, double f0, std::complex<double> *cwave);

} // namespace wm
