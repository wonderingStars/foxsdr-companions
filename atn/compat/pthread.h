/* Stand-in for pthread.h: type names only; dumpvdl2.h mentions them in structures this helper never uses. */
#ifndef FOXSDR_COMPAT_PTHREAD_H
#define FOXSDR_COMPAT_PTHREAD_H
typedef unsigned long pthread_t;
typedef int pthread_barrier_t;
#endif
