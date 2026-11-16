/* Windows/MSVC shim for <sys/time.h>, written for this helper (not upstream code). */
#pragma once
#include <time.h>
#include <winsock.h>   /* struct timeval, as libacars itself does on Windows (no sockets are used) */
#ifndef timersub
#define timersub(a, b, result) \
	do { \
		(result)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
		(result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
		if ((result)->tv_usec < 0) { \
			--(result)->tv_sec; \
			(result)->tv_usec += 1000000; \
		} \
	} while (0)
#endif
