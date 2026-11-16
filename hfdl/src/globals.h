/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "config.h"         // DATADUMPS
#include "systable.h"
#include "ac_cache.h"
#include "ac_data.h"

enum ac_data_details {
	AC_DETAILS_NORMAL = 0,
	AC_DETAILS_VERBOSE = 1
};

// global config
struct dumphfdl_config {
#ifdef DEBUG
	uint32_t debug_filter;
#endif
	char *station_id;
	int32_t output_queue_hwm;
	int32_t nf_stats_interval;
	int32_t ac_cache_ttl;
	enum ac_data_details ac_data_details;
	bool utc;
	bool milliseconds;
	bool output_raw_frames;
	bool output_mpdus;
	bool output_corrupted_pdus;
	bool freq_as_squawk;
	bool ac_data_available;
#ifdef DATADUMPS
	bool datadumps;
#endif
};

#define STATION_ID_LEN_MAX 255

extern struct dumphfdl_config Config;
extern int32_t do_exit;
extern int32_t exitcode;

// version.c
extern char const * const DUMPHFDL_VERSION;

extern systable *Systable;
// FoxSDR helper: single-threaded, so the locks are no-ops (kept so that the call sites stay as upstream)
#define Systable_lock() do { } while(0)
#define Systable_unlock() do { } while(0)

extern ac_cache *AC_cache;
#define AC_cache_lock() do { } while(0)
#define AC_cache_unlock() do { } while(0)

extern ac_data *AC_data;
