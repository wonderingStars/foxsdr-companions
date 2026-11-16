/*
 * dw_compat.h - forced-include shim so Dire Wolf's C sources build with MSVC (FoxSDR "packet" helper).
 * Licence: GPL-2.0-or-later, as the rest of the helper.  Nothing here changes behaviour.
 */
#ifndef DW_COMPAT_H
#define DW_COMPAT_H 1
#ifdef _MSC_VER
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define __attribute__(x)		/* GCC attributes: aligned, hot, format, ... (no effect on results) */
#define __restrict__ __restrict
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define localtime_r(t, r) (localtime_s((r), (t)) == 0 ? (r) : NULL)	/* only used by timestamp helpers */

static __inline int dw_popcount32(uint32_t x)
{
	x = x - ((x >> 1) & 0x55555555u);
	x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
	x = (x + (x >> 4)) & 0x0f0f0f0fu;
	return (int)((x * 0x01010101u) >> 24);
}
static __inline int dw_popcount64(uint64_t x)
{
	return dw_popcount32((uint32_t)x) + dw_popcount32((uint32_t)(x >> 32));
}
#define __builtin_popcount(x) dw_popcount32((uint32_t)(x))
#define __builtin_popcountll(x) dw_popcount64((uint64_t)(x))
#endif
#endif
