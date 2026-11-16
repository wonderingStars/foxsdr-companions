/*
 *      helper_compat.h -- force-included into demod_flex.c and demod_flex_next.c only
 *
 *      Part of the FoxSDR "multimon" GPL helper (see UPSTREAM.md). Lets those two upstream files compile and
 *      behave as a helper needs without editing them:
 *
 *      1. time(): FLEX and FLEX_NEXT stamp what they print with the wall clock. A helper must give the same
 *         output for the same input, so time() is replaced by a clock the helper derives from the number of
 *         samples it has read: start time (--start-time, default 0) plus samples / 22050.
 *      2. __builtin_popcount(): a GCC / clang built-in that MSVC does not have (demod_flex_next.c uses it
 *         unconditionally).
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */
#ifndef HELPER_COMPAT_H
#define HELPER_COMPAT_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif
time_t hm_time(time_t *t);
#ifdef __cplusplus
}
#endif

#define time(t) hm_time(t)

#if defined(_MSC_VER) && !defined(__clang__)
static __inline int hm_popcount(unsigned int x)
{
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0f0f0f0fu;
    return (int)((x * 0x01010101u) >> 24);
}
#define __builtin_popcount(x) hm_popcount(x)
#endif

#endif
