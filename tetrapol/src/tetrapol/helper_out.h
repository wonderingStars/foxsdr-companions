/*
 * helper_out.h -- JSON Lines output of the FoxSDR "tetrapol" GPL helper
 *
 * Part of the FoxSDR tetrapol helper, derived from tetrapol-kit (https://github.com/airphel/tetrapol-kit-2023,
 * GPL-2.0). This header is new code written for the helper; the upstream library calls it where upstream used
 * printf() / LOGF(). See UPSTREAM.md.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation; version 2 of the License.
 */
#pragma once

#include <stdint.h>
#include <stdio.h>

#include <tetrapol/addr.h>
#include <tetrapol/frame.h>
#include <tetrapol/tetrapol_int.h>
#include <tetrapol/tsdu.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Symbol (bit) rate of a TETRAPOL channel: 8000 bit/s, one frame of 160 bits every 20 ms. */
#define TP_BIT_RATE 8000

typedef struct {
    FILE *out;              /**< where JSON lines go (stdout of the helper) */
    int hide_ids;           /**< --hide-ids: terminal / group identities become short session hashes */
    uint64_t salt;          /**< session salt for the hashes */
    int report_bad_frames;  /**< --report-bad-frames: also print a line for each frame that failed its CRC */
    int no_frames;          /**< --no-frames: do not print one line per good frame */
    int traffic_mode;       /**< input is a traffic channel (--channel tch) */
} tp_out_cfg_t;

/** Set up the output. Must be called before any decoding. */
void tp_out_init(const tp_out_cfg_t *cfg);

/** Time of the decoder position in seconds (bit count / 8000). */
double tp_out_time(const tpol_t *tpol);

/** Raw output of one finished line (without the newline). Exits the process with status 3 when stdout is gone. */
void tp_out_line(const char *line, size_t len);

/** Counters, printed by tp_emit_stats at the end of the input. */
typedef struct {
    uint64_t bits;
    uint64_t frames_ok, frames_bad, frames_fixed;
    uint64_t messages, messages_data, messages_unknown;
    uint64_t traffic_frames;
    uint64_t sync_found, sync_lost;
    uint64_t lines_dropped;     /* lines too long for the output buffer */
} tp_out_stats_t;

const tp_out_stats_t *tp_out_stats(void);
/** Count bits taken from the input (for the final stats line). */
void tp_out_count_bits(uint64_t n);

/* --- events (every one is one JSON line {"t":...,"type":...}) --- */
void tp_emit_sync(const tpol_t *tpol, int found);
void tp_emit_scr(const tpol_t *tpol, int scr);
void tp_emit_frame(const tpol_t *tpol, const frame_t *fr);
void tp_emit_traffic(const tpol_t *tpol, const frame_t *fr);
void tp_emit_not_decoded(const tpol_t *tpol, const char *what);
void tp_emit_pch(const tpol_t *tpol, const uint8_t *act_bitmap, int nbitmap, const addr_t *addrs, int naddrs);
void tp_emit_rch(const tpol_t *tpol, const addr_t *addrs, int naddrs);
/** @param tsdu decoded message or NULL when it could not be decoded (or was deliberately not decoded) */
void tp_emit_tsdu(const tpol_t *tpol, const tpol_tsdu_t *tt, const tsdu_t *tsdu);
void tp_emit_stats(const tpol_t *tpol);
void tp_emit_status(const tpol_t *tpol, const char *text);

/** Codops whose content is user data (short data, datagrams ...): never decoded, only reported by length. */
int tp_codop_is_data(uint8_t codop);

#ifdef __cplusplus
}
#endif
