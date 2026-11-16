/*
 *      helper_events.h -- turn the text multimon-ng prints into JSON Lines events
 *
 *      Part of the FoxSDR "multimon" GPL helper (see UPSTREAM.md).
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */
#ifndef HELPER_EVENTS_H
#define HELPER_EVENTS_H

#include <stdio.h>

extern int hev_clock_set; /* 1 when --start-time gave the FLEX clock a real start */

/* Growable byte string used to build one JSON line. */
struct hev_sb {
    char *p;
    size_t n, cap;
    int oom;
};

void hev_sb_init(struct hev_sb *sb);
void hev_sb_free(struct hev_sb *sb);
void hev_sb_cat(struct hev_sb *sb, const char *s);
void hev_sb_catn(struct hev_sb *sb, const char *s, size_t n);
void hev_sb_fmt(struct hev_sb *sb, const char *fmt, ...);
/* Append s as a JSON string literal, with quotes. Bytes that are not valid UTF-8 are written as the
   Unicode code point of the same value (Latin-1 reading), so the output is always valid UTF-8 JSON. */
void hev_sb_json_str(struct hev_sb *sb, const char *s, size_t n);

/* Does this printed line begin a new event for this demodulator? A line that does not (for example the
   payload lines under an AX.25 header) is attached to the event before it. */
int hev_starts_event(const char *demod, const char *line);

/* Append one event (a JSON object, no trailing newline) for the lines multimon-ng printed. `text` is the
   printed line exactly (several lines joined by '\n' when they were grouped). `t` is seconds. */
void hev_build(struct hev_sb *out, double t, const char *demod, const char *text);

#endif
