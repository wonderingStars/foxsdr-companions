/* Stand-in for sys/time.h: struct timeval, same layout as Winsock's. No Winsock is used or linked. */
#ifndef FOXSDR_COMPAT_SYS_TIME_H
#define FOXSDR_COMPAT_SYS_TIME_H
#ifdef _WIN32
#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED
struct timeval {
	long tv_sec;
	long tv_usec;
};
#endif
#else
#include_next <sys/time.h>
#endif
#endif
