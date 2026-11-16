// FoxSDR GPL helper "weakmodes2": GCC builtins that the upstream files use, for MSVC (force-included with /FI so the upstream sources stay unchanged).
#pragma once
#ifdef _MSC_VER
#include <bit>
#include <cstdint>
static inline int __builtin_ctzll(unsigned long long v) { return std::countr_zero(v); }
static inline int __builtin_popcountll(unsigned long long v) { return std::popcount(v); }
static inline int __builtin_popcount(unsigned int v) { return std::popcount(v); }
#endif
