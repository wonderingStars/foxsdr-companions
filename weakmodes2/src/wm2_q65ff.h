// weakmodes2: the fast-fading (spread signal) Q65 decode of WSJT-X, for the candidates the plain decoder of hfdig_q65.cpp could not decode.
// New code written for this helper; it calls WSJT-X's own C codec (upstream/wsjtx_q65: q65.c, qracodes.c, ...) the way
// WSJT-X lib/qra/q65/q65.f90 (q65_dec2 / q65_s1_to_s3 / q65_dec_q012) does with no a-priori information.
#pragma once
#include <vector>

namespace dect2 {
namespace wsjt {

// y: the audio at `rate` (a symbol is N samples, so the bin spacing is the baud rate), the first symbol starts at sample `start`, tone 0
// is at bf Hz, sub = 0 (A) .. 4 (E), isync = the 22 one-origin positions of the sync symbols among the 85. On success fills msg13 (the 13
// six-bit symbols of the message) and esnodb (WSJT-X's estimate of Es/No in dB for the spread model that decoded) and returns true.
bool q65FastFading(const std::vector<float>& y, double rate, int N, long start, double bf, int sub, const int* isync, int* msg13,
                   float* esnodb);

} // namespace wsjt
} // namespace dect2
