// weakmodes / JT9: message packing and unpacking, convolutional encoder, interleaver.
// C++ port of parts of WSJT-X lib/packjt.f90, lib/gen9.f90, lib/encode232.f90, lib/entail.f90, lib/interleave9.f90,
// lib/graycode.f90, lib/jt9sync.f90, lib/conv232.f90.
//
// Copyright (C) 2001-2025 Joseph H. Taylor, Jr., K1JT and the WSJT Development Group.
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU General
// Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option)
// any later version. It is distributed WITHOUT ANY WARRANTY. See LICENSE.
#pragma once
#include <cstdint>
#include <string>

namespace wm {
namespace jt9 {

// jt9sync.f90: sync vector (1 = sync symbol), positions of the sync symbols and of the sync half-symbols
extern const int kIsync[85];
extern const int kIi[16];  // ii(16)  (1-based symbol numbers)
extern const int kIi2[16]; // ii2(16) (1-based half-symbol numbers)

// conv232.f90: Layland-Lushbaugh polynomials of the K=32, r=1/2 code and the parity table
extern const int32_t kNpoly1;
extern const int32_t kNpoly2;
int parity8(int n); // partab(n), n = 0..255

// packjt.f90 unpackmsg: 12 six-bit words -> message text (trailing blanks removed).
std::string unpackmsg(const int *dat12);
// packjt.f90 packmsg for standard messages (itype 1) and free text (itype 6). Returns the itype, or -1 for a
// message this port does not pack (calls with a "/" prefix or suffix, JT65v2 style). dat12 gets 12 six-bit words.
int packmsg(const std::string &msg0, int *dat12);

// interleave9.f90: ndir > 0 scrambles, ndir < 0 unscrambles (206 elements used; ia/ib hold 207)
void interleave9(const int8_t *ia, int ndir, int8_t *ib);
// encode232.f90 (nsym = 206) and entail.f90 combined: 12 six-bit words -> 206 encoded bits
void encode_words(const int *dat12, int8_t *bits206);
// gen9.f90: message text -> 85 tone numbers (0 = sync tone, 1..8 data tones) and the message as it will be received.
// Returns itype (1 or 6) or -1 if the message cannot be packed by this port.
int gen9(const std::string &msg, int *tone85, std::string *msgsent);

} // namespace jt9
} // namespace wm
