// weakmodes: LDPC(174,91) code used by FT2L (port of WSJT-X lib/ft8/*174_91*.f90). See ldpc174_91.cpp.
#pragma once
#include <cstdint>

namespace wm {

// 14-bit CRC check/compute exactly as WSJT-X get_crc14: mc[0..len-1] are bits (0/1)
void get_crc14(const uint8_t *mc, int len, int *ncrc);
// message77 -> 174-bit codeword (77 bits + CRC14 + 83 parity)
void encode174_91(const uint8_t *message77, uint8_t *codeword);
void encode174_91_nocrc(const uint8_t *message91, uint8_t *codeword);
// Hybrid BP / OSD decoder. llr[174]: positive = bit 1 more likely. Keff = 91. maxosd < 0: BP only; norder = OSD depth.
// apmask[174]: 1 for bits fixed by a-priori information. On success (nharderror >= 0) message91 holds the 91 bits.
int decode174_91(const float *llr, int Keff, int maxosd, int norder, const uint8_t *apmask, uint8_t *message91, uint8_t *cw,
                 int *ntype, int *nharderror, float *dmin);

} // namespace wm
