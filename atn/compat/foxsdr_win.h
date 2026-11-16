/* Force-included into every file of this helper on MSVC (/FI): ssize_t, which dumpvdl2's cotp.c uses through the
 * asn1 headers without including anything that defines it on Windows. */
#ifndef FOXSDR_COMPAT_WIN_H
#define FOXSDR_COMPAT_WIN_H
#ifdef _MSC_VER
#include <stddef.h>
#ifndef FOXSDR_SSIZE_T_DEFINED
#define FOXSDR_SSIZE_T_DEFINED
typedef ptrdiff_t ssize_t;
#endif
#endif
#endif
