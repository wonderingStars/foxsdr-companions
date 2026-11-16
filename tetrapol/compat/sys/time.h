/* FoxSDR helper compatibility shim: upstream tp_timer.h needs struct timeval from <sys/time.h>. */
#pragma once
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
