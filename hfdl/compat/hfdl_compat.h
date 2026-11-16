/* Force-included in every translation unit of the helper (MSVC). Written for this helper, not upstream code. */
#pragma once
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES 1
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#include <time.h>
#include <stdint.h>
/* POSIX gmtime_r is gmtime_s with the arguments swapped */
#define gmtime_r(t, result) (gmtime_s((result), (t)) == 0 ? (result) : NULL)
#define strdup _strdup
#define timegm _mkgmtime
#ifndef ssize_t
typedef long long ssize_t;
#endif
/* GCC attributes (aligned etc.) have no meaning here */
#define __attribute__(x)
