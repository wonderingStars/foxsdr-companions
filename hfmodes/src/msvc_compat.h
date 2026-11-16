// hfmodes helper: force-included under MSVC so the OnAir sources (written for GCC / Clang) compile unchanged where they use GCC builtins.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
static inline int hfm_popcount(unsigned v) { return (int)__popcnt(v); }
#define __builtin_popcount(x) hfm_popcount((unsigned)(x))
#endif
