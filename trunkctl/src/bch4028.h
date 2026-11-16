// trunkctl - EDACS BCH(40,28) code, written for this helper (not taken from any upstream file).
//
// Parameters are the ones edacs-fm documents ("40-28-6-2"): a 40-bit word is 28 message bits followed by
// 12 parity bits, the code is the binary BCH code over GF(2^6) (primitive polynomial x^6+x+1), designed
// to correct t = 2 errors, shortened from length 63. The 40-bit word is read MSB first: bit 39 is the first
// bit received, and bit i is the coefficient of x^i of the code polynomial.
//
// This file is part of trunkctl and is licensed under the GNU General Public License, version 3.
#ifndef TRUNKCTL_BCH4028_H
#define TRUNKCTL_BCH4028_H

#include <cstdint>

namespace trunkctl {

// Systematic encode: returns (data28 << 12) | parity12.
uint64_t bch4028_encode(uint32_t data28);

// Decode a received 40-bit word. Returns the number of corrected bit errors (0, 1 or 2) and stores the
// corrected 40-bit word in *corrected, or returns -1 when the word has more errors than the code can
// correct (or the error pattern points outside the 40 transmitted positions). max_errors (0..2) limits
// how many corrections are accepted: with 12 parity bits a random word is within distance 1 of a codeword
// about 1% of the time and within distance 2 about 20% of the time, so the default is 1.
int bch4028_decode(uint64_t word40, int max_errors, uint64_t *corrected);

} // namespace trunkctl

#endif
