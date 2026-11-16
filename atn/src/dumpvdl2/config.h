/*
 *  Replaces dumpvdl2's CMake-generated config.h (which was generated from config.h.in, GPL-3.0-or-later,
 *  Copyright (c) 2017-2026 Tomasz Lemiech). Every optional feature of dumpvdl2 is off in this helper.
 */
#ifndef _CONFIG_H
#define _CONFIG_H
/* little-endian host: IS_BIG_ENDIAN stays undefined */
#define HAVE_PTHREAD_BARRIERS 1   /* the compat pthread.h stands in; dumpvdl2's pthread_barrier.h is not used */
#endif
