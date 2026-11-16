/*
 *      helper_events.c -- turn the text multimon-ng prints into JSON Lines events
 *
 *      Part of the FoxSDR "multimon" GPL helper (see UPSTREAM.md). New code written for the helper;
 *      it only reads the text lines that the unmodified multimon-ng demodulators print.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */

#include "helper_events.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* Set by the program when it was given --start-time: only then are the clock values the FLEX decoders print
   real times rather than "1970-01-01 00:00:xx". */
int hev_clock_set = 0;

/* ---------------------------------------------------------------------- */
/* growable string */

void hev_sb_init(struct hev_sb *sb)
{
    sb->p = NULL;
    sb->n = 0;
    sb->cap = 0;
    sb->oom = 0;
}

void hev_sb_free(struct hev_sb *sb)
{
    free(sb->p);
    hev_sb_init(sb);
}

static int sb_reserve(struct hev_sb *sb, size_t extra)
{
    if (sb->oom)
        return 0;
    if (sb->n + extra + 1 <= sb->cap)
        return 1;
    size_t ncap = sb->cap ? sb->cap : 256;
    while (ncap < sb->n + extra + 1)
        ncap *= 2;
    char *np = (char *)realloc(sb->p, ncap);
    if (!np) {
        sb->oom = 1;
        return 0;
    }
    sb->p = np;
    sb->cap = ncap;
    return 1;
}

void hev_sb_catn(struct hev_sb *sb, const char *s, size_t n)
{
    if (!sb_reserve(sb, n))
        return;
    memcpy(sb->p + sb->n, s, n);
    sb->n += n;
    sb->p[sb->n] = '\0';
}

void hev_sb_cat(struct hev_sb *sb, const char *s)
{
    hev_sb_catn(sb, s, strlen(s));
}

void hev_sb_fmt(struct hev_sb *sb, const char *fmt, ...)
{
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n < sizeof tmp) {
        hev_sb_catn(sb, tmp, (size_t)n);
        return;
    }
    char *big = (char *)malloc((size_t)n + 1);
    if (!big) {
        sb->oom = 1;
        return;
    }
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    hev_sb_catn(sb, big, (size_t)n);
    free(big);
}

/* Length of a valid UTF-8 sequence starting at s (at most n bytes available), or 0 if invalid. */
static size_t utf8_len(const unsigned char *s, size_t n)
{
    if (s[0] < 0x80)
        return 1;
    if (s[0] >= 0xC2 && s[0] <= 0xDF && n >= 2 && (s[1] & 0xC0) == 0x80)
        return 2;
    if (s[0] >= 0xE0 && s[0] <= 0xEF && n >= 3 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        unsigned cp = ((unsigned)(s[0] & 0x0F) << 12) | ((unsigned)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        if (cp >= 0x800 && !(cp >= 0xD800 && cp <= 0xDFFF))
            return 3;
        return 0;
    }
    if (s[0] >= 0xF0 && s[0] <= 0xF4 && n >= 4 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
        (s[3] & 0xC0) == 0x80) {
        unsigned cp = ((unsigned)(s[0] & 0x07) << 18) | ((unsigned)(s[1] & 0x3F) << 12) |
                      ((unsigned)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        if (cp >= 0x10000 && cp <= 0x10FFFF)
            return 4;
    }
    return 0;
}

void hev_sb_json_str(struct hev_sb *sb, const char *str, size_t n)
{
    const unsigned char *s = (const unsigned char *)str;
    size_t i = 0;
    hev_sb_catn(sb, "\"", 1);
    while (i < n) {
        unsigned char c = s[i];
        if (c == '"') {
            hev_sb_catn(sb, "\\\"", 2);
            i++;
        } else if (c == '\\') {
            hev_sb_catn(sb, "\\\\", 2);
            i++;
        } else if (c == '\n') {
            hev_sb_catn(sb, "\\n", 2);
            i++;
        } else if (c == '\r') {
            hev_sb_catn(sb, "\\r", 2);
            i++;
        } else if (c == '\t') {
            hev_sb_catn(sb, "\\t", 2);
            i++;
        } else if (c < 0x20 || c == 0x7F) {
            hev_sb_fmt(sb, "\\u%04x", (unsigned)c);
            i++;
        } else if (c < 0x80) {
            hev_sb_catn(sb, (const char *)&s[i], 1);
            i++;
        } else {
            size_t l = utf8_len(s + i, n - i);
            if (l) {
                hev_sb_catn(sb, (const char *)s + i, l);
                i += l;
            } else {
                hev_sb_fmt(sb, "\\u%04x", (unsigned)c); /* Latin-1 reading of a stray byte */
                i++;
            }
        }
    }
    hev_sb_catn(sb, "\"", 1);
}

/* ---------------------------------------------------------------------- */
/* field helpers: every field is written as ,"name":value */

static void f_key(struct hev_sb *sb, const char *name)
{
    hev_sb_catn(sb, ",\"", 2);
    hev_sb_cat(sb, name);
    hev_sb_catn(sb, "\":", 2);
}

static void f_str(struct hev_sb *sb, const char *name, const char *s, size_t n)
{
    f_key(sb, name);
    hev_sb_json_str(sb, s, n);
}

static void f_cstr(struct hev_sb *sb, const char *name, const char *s)
{
    f_str(sb, name, s, strlen(s));
}

static void f_int(struct hev_sb *sb, const char *name, long long v)
{
    f_key(sb, name);
    hev_sb_fmt(sb, "%lld", v);
}

static void f_null(struct hev_sb *sb, const char *name)
{
    f_key(sb, name);
    hev_sb_cat(sb, "null");
}

static int starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* If line is "<demod>: rest" return rest, else NULL. */
static const char *after_name(const char *demod, const char *line)
{
    size_t l = strlen(demod);
    if (strncmp(line, demod, l) == 0 && line[l] == ':' && line[l + 1] == ' ')
        return line + l + 2;
    return NULL;
}

/* "message": the printed text without the leading "<demod>: " (the whole line if it has none). */
static void f_message_generic(struct hev_sb *sb, const char *demod, const char *text)
{
    const char *m = after_name(demod, text);
    if (!m)
        m = text;
    f_cstr(sb, "message", m);
}

/* ---------------------------------------------------------------------- */
/* demodulator families */

static int is_pocsag(const char *d) { return starts_with(d, "POCSAG"); }
static int is_selcall(const char *d)
{
    return !strcmp(d, "ZVEI1") || !strcmp(d, "ZVEI2") || !strcmp(d, "ZVEI3") || !strcmp(d, "DZVEI") ||
           !strcmp(d, "PZVEI") || !strcmp(d, "EEA") || !strcmp(d, "EIA") || !strcmp(d, "CCIR");
}
static int is_hdlc(const char *d)
{
    return !strcmp(d, "AFSK1200") || !strcmp(d, "AFSK2400") || !strcmp(d, "AFSK2400_2") ||
           !strcmp(d, "AFSK2400_3") || !strcmp(d, "HAPN4800") || !strcmp(d, "FSK9600");
}

int hev_starts_event(const char *demod, const char *line)
{
    if (is_pocsag(demod) || is_selcall(demod) || !strcmp(demod, "GSC") || !strcmp(demod, "EAS") ||
        !strcmp(demod, "DTMF") || !strcmp(demod, "DUALTONE"))
        return after_name(demod, line) != NULL;
    if (!strcmp(demod, "FLEX"))
        return starts_with(line, "FLEX|") || starts_with(line, "FLEX: ");
    if (!strcmp(demod, "FLEX_NEXT"))
        return starts_with(line, "FLEX_NEXT|") || starts_with(line, "FLEX_NEXT: ");
    if (is_hdlc(demod)) {
        if (starts_with(line, "APRS: "))
            return 1;
        return after_name(demod, line) != NULL;
    }
    if (!strcmp(demod, "X10"))
        return starts_with(line, "bstring = ");
    return 1;
}

/* ---------------------------------------------------------------------- */
/* POCSAG: "POCSAG1200: Address:  273040  Function: 3  Alpha:   text"
           "POCSAG1200: Address:       -  Function: -  Numeric: 123"   (continuation without address)
           "POCSAG1200: Address:  273040  Function: 3 "                (address only, no message) */

static void fields_pocsag(struct hev_sb *sb, const char *demod, const char *text)
{
    const char *p = after_name(demod, text);
    if (!p || strncmp(p, "Address:", 8) != 0) {
        f_message_generic(sb, demod, text);
        return;
    }
    p += 8;
    while (*p == ' ')
        p++;
    if (*p == '-') {
        f_null(sb, "address");
        p++;
    } else {
        char *e;
        long long a = strtoll(p, &e, 10);
        if (e == p) {
            f_message_generic(sb, demod, text);
            return;
        }
        f_int(sb, "address", a);
        p = e;
    }
    while (*p == ' ')
        p++;
    if (strncmp(p, "Function:", 9) != 0) {
        return;
    }
    p += 9;
    while (*p == ' ')
        p++;
    if (*p == '-') {
        f_null(sb, "function");
        p++;
    } else {
        char *e;
        long long fn = strtoll(p, &e, 10);
        f_int(sb, "function", fn);
        p = e;
    }
    while (*p == ' ')
        p++;
    /* the three printed message kinds; the label is followed by fixed padding */
    if (starts_with(p, "Numeric:")) {
        f_cstr(sb, "kind", "numeric");
        p += 8;
        if (*p == ' ')
            p++;
        f_cstr(sb, "message", p);
    } else if (starts_with(p, "Alpha:")) {
        f_cstr(sb, "kind", "alpha");
        p += 6;
        for (int i = 0; i < 3 && *p == ' '; i++)
            p++;
        f_cstr(sb, "message", p);
    } else if (starts_with(p, "Skyper:")) {
        f_cstr(sb, "kind", "skyper");
        p += 7;
        for (int i = 0; i < 2 && *p == ' '; i++)
            p++;
        f_cstr(sb, "message", p);
    } else {
        f_cstr(sb, "kind", "none");
    }
}

/* ---------------------------------------------------------------------- */
/* GSC: GSC: Address: 2345675  Function: 1  Numeric: "555-1234"
        GSC: Address: 3333335  Function: 1  Alpha:   "CALL 555-1234"
        GSC: Address: 5050059  Function: 1  Tone
        GSC: Address: 1234561  Function: 1  Voice: message start | message end */

static void fields_gsc(struct hev_sb *sb, const char *text)
{
    const char *p = after_name("GSC", text);
    if (!p || strncmp(p, "Address: ", 9) != 0) {
        f_message_generic(sb, "GSC", text);
        return;
    }
    p += 9;
    const char *e = strstr(p, "  Function: ");
    if (!e) {
        f_message_generic(sb, "GSC", text);
        return;
    }
    f_str(sb, "address", p, (size_t)(e - p));
    p = e + 12;
    char *q;
    long long fn = strtoll(p, &q, 10);
    f_int(sb, "function", fn);
    p = q;
    while (*p == ' ')
        p++;
    if (starts_with(p, "Numeric:") || starts_with(p, "Alpha:")) {
        int numeric = starts_with(p, "Numeric:");
        f_cstr(sb, "kind", numeric ? "numeric" : "alpha");
        const char *lq = strchr(p, '"');
        if (lq) {
            const char *rq = strrchr(lq, '"');
            if (rq && rq > lq)
                f_str(sb, "message", lq + 1, (size_t)(rq - lq - 1));
            else
                f_cstr(sb, "message", lq + 1);
        }
    } else if (starts_with(p, "Tone")) {
        f_cstr(sb, "kind", "tone");
    } else if (starts_with(p, "Voice:")) {
        f_cstr(sb, "kind", "voice");
        p += 6;
        while (*p == ' ')
            p++;
        f_cstr(sb, "state", starts_with(p, "message end") ? "end" : "start");
    } else {
        f_cstr(sb, "kind", "unknown");
        f_cstr(sb, "message", p);
    }
}

/* ---------------------------------------------------------------------- */
/* FLEX (pipe separated):
     FLEX|1600/2/A/B|00.013|000123456|ALN|message             (no time stamp)
     FLEX|1600/2/A|00.013|[000123456]|NUM 12345               (numeric / tone only / unknown: older layout)
     FLEX|2026-11-12 10:00:00|1600/2/A/B|00.013|000123456|ALN|message   (with a time stamp)
   Group messages list several capcodes in the capcode field, separated by spaces. */

static size_t split_pipes(char *s, char **part, size_t maxparts)
{
    size_t n = 0;
    part[n++] = s;
    while (*s && n < maxparts) {
        if (*s == '|') {
            *s = '\0';
            part[n++] = s + 1;
        }
        s++;
    }
    return n;
}

static void f_capcodes(struct hev_sb *sb, char *field)
{
    /* "000123456" or "[000123456]" or "000123456 000123457 ..." -> capcode (first) and capcodes[] (all) */
    long long first = -1;
    struct hev_sb arr;
    hev_sb_init(&arr);
    hev_sb_cat(&arr, "[");
    int count = 0;
    char *p = field;
    while (*p) {
        while (*p == ' ' || *p == '[' || *p == ']')
            p++;
        if (!*p)
            break;
        char *e;
        long long v = strtoll(p, &e, 10);
        if (e == p)
            break;
        if (count)
            hev_sb_cat(&arr, ",");
        hev_sb_fmt(&arr, "%lld", v);
        if (first < 0)
            first = v;
        count++;
        p = e;
    }
    hev_sb_cat(&arr, "]");
    if (count) {
        f_int(sb, "capcode", first);
        f_key(sb, "capcodes");
        hev_sb_cat(sb, arr.p ? arr.p : "[]");
    }
    hev_sb_free(&arr);
}

static void f_flex_rate(struct hev_sb *sb, char *field)
{
    /* "1600/2/A/B" or "1600/2/A": baud / levels / [fragment flag /] phase */
    char *p = field;
    char *e;
    long b = strtol(p, &e, 10);
    if (e == p || *e != '/')
        return;
    f_int(sb, "baud", b);
    p = e + 1;
    long lv = strtol(p, &e, 10);
    if (e == p)
        return;
    f_int(sb, "levels", lv);
    p = e;
    if (*p == '/') {
        p++;
        char a = p[0], b2 = (p[0] && p[1] == '/') ? p[2] : 0;
        if (b2) {
            f_str(sb, "fragment", &a, 1);
            f_str(sb, "phase", &b2, 1);
        } else if (a) {
            f_str(sb, "phase", &a, 1);
        }
    }
}

static void fields_flex(struct hev_sb *sb, const char *text)
{
    size_t len = strlen(text);
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return;
    memcpy(copy, text, len + 1);
    char *part[8];
    size_t n = split_pipes(copy, part, 8);
    size_t i = 1;
    if (n >= 2 && n >= 3 && strchr(part[1], ':'))
        i = 2; /* time stamp present: parts[1] holds it */
    if (n < i + 3) {
        f_message_generic(sb, "FLEX", text);
        free(copy);
        return;
    }
    if (i == 2)
        f_cstr(sb, "time", part[1]);
    f_flex_rate(sb, part[i]);
    /* cycle.frame */
    {
        char *dot = strchr(part[i + 1], '.');
        if (dot) {
            f_int(sb, "cycle", strtol(part[i + 1], NULL, 10));
            f_int(sb, "frame", strtol(dot + 1, NULL, 10));
        }
    }
    f_capcodes(sb, part[i + 2]);
    if (n > i + 3) {
        const char *k = part[i + 3];
        /* kind is the first three letters; for ALN the message is the rest of the line after the next '|' */
        char kind[4] = {0};
        for (int j = 0; j < 3 && k[j]; j++)
            kind[j] = k[j];
        f_cstr(sb, "kind", kind);
        const char *msg;
        if (!strcmp(kind, "ALN") && n > i + 4) {
            /* message may itself contain '|': take everything after the kind field from the original text */
            size_t off = (size_t)(part[i + 4] - copy);
            msg = text + off;
        } else {
            msg = k + (k[0] ? 3 : 0);
            if (*msg == ' ')
                msg++;
        }
        f_cstr(sb, "message", msg);
    }
    free(copy);
}

/* FLEX_NEXT:  FLEX_NEXT|<timestamp>|<baud>/<levels>|<cycle>.<frame>.<phase>|<capcodes>|<addr type>|<TYPE>|<flags>|<message>
   (the layout is described in the header of demod_flex_next.c). */
static void fields_flex_next(struct hev_sb *sb, const char *text)
{
    size_t len = strlen(text);
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return;
    memcpy(copy, text, len + 1);
    char *part[10];
    size_t n = split_pipes(copy, part, 9);
    if (n < 7 || strcmp(part[0], "FLEX_NEXT") != 0) {
        f_message_generic(sb, "FLEX_NEXT", text);
        free(copy);
        return;
    }
    if (hev_clock_set)
        f_cstr(sb, "time", part[1]);
    {
        char *e;
        long b = strtol(part[2], &e, 10);
        if (e != part[2] && *e == '/') {
            f_int(sb, "baud", b);
            f_int(sb, "levels", strtol(e + 1, NULL, 10));
        }
    }
    {
        char *e1;
        long cy = strtol(part[3], &e1, 10);
        if (e1 != part[3] && *e1 == '.') {
            char *e2;
            long fr = strtol(e1 + 1, &e2, 10);
            f_int(sb, "cycle", cy);
            f_int(sb, "frame", fr);
            if (*e2 == '.' && e2[1])
                f_str(sb, "phase", e2 + 1, 1);
        }
    }
    f_capcodes(sb, part[4]);
    f_cstr(sb, "address_type", part[5]);
    f_cstr(sb, "kind", part[6]);
    if (n >= 8)
        f_cstr(sb, "flags", part[7]);
    if (n >= 9) {
        size_t off = (size_t)(part[8] - copy);
        f_cstr(sb, "message", text + off);
    }
    free(copy);
}

/* ---------------------------------------------------------------------- */
/* EAS:  EAS: ZCZC-ORG-EEE-PSSCCC-PSSCCC+TTTT-JJJHHMM-LLLLLLLL-     or     EAS: NNNN */

static void fields_eas(struct hev_sb *sb, const char *text)
{
    const char *m = after_name("EAS", text);
    if (!m) {
        f_message_generic(sb, "EAS", text);
        return;
    }
    if (starts_with(m, "NNNN")) {
        f_cstr(sb, "kind", "eom");
        f_cstr(sb, "message", m);
        return;
    }
    f_cstr(sb, "kind", "header");
    f_cstr(sb, "message", m);
    if (!starts_with(m, "ZCZC-"))
        return;
    size_t len = strlen(m);
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return;
    memcpy(copy, m, len + 1);
    /* tokens separated by '-' */
    char *tok[64];
    size_t n = 0;
    char *p = copy;
    tok[n++] = p;
    while (*p && n < 64) {
        if (*p == '-') {
            *p = '\0';
            tok[n++] = p + 1;
        }
        p++;
    }
    if (n >= 4) {
        f_cstr(sb, "originator", tok[1]);
        f_cstr(sb, "event", tok[2]);
        struct hev_sb locs;
        hev_sb_init(&locs);
        hev_sb_cat(&locs, "[");
        size_t i = 3;
        int count = 0;
        const char *duration = NULL;
        for (; i < n; i++) {
            char *plus = strchr(tok[i], '+');
            if (count)
                hev_sb_cat(&locs, ",");
            if (plus) {
                hev_sb_json_str(&locs, tok[i], (size_t)(plus - tok[i]));
                duration = plus + 1;
                count++;
                i++;
                break;
            }
            hev_sb_json_str(&locs, tok[i], strlen(tok[i]));
            count++;
        }
        hev_sb_cat(&locs, "]");
        f_key(sb, "locations");
        hev_sb_cat(sb, locs.p ? locs.p : "[]");
        hev_sb_free(&locs);
        if (duration)
            f_cstr(sb, "duration", duration);
        if (duration && i < n)
            f_cstr(sb, "issued", tok[i++]);
        if (duration && i < n && tok[i][0])
            f_cstr(sb, "sender", tok[i]);
    }
    free(copy);
}

/* ---------------------------------------------------------------------- */
/* AX.25 / APRS (hdlc.c):
     "AFSK1200: fm N0CALL-0 to APRS-0 via WIDE1-1,WIDE2-1* UI^ pid=F0"   then payload lines
     "APRS: N0CALL>APRS,WIDE1-1*:payload"                                (-A mode) */

static void fields_hdlc(struct hev_sb *sb, const char *demod, const char *text)
{
    const char *nl = strchr(text, '\n');
    size_t first_len = nl ? (size_t)(nl - text) : strlen(text);
    char *line = (char *)malloc(first_len + 1);
    if (!line)
        return;
    memcpy(line, text, first_len);
    line[first_len] = '\0';

    if (starts_with(line, "APRS: ")) {
        f_cstr(sb, "kind", "aprs");
        char *p = line + 6;
        char *gt = strchr(p, '>');
        char *colon = strchr(p, ':');
        if (gt && colon && gt < colon) {
            f_str(sb, "source", p, (size_t)(gt - p));
            char *comma = memchr(gt + 1, ',', (size_t)(colon - gt - 1));
            if (comma) {
                f_str(sb, "dest", gt + 1, (size_t)(comma - gt - 1));
                f_str(sb, "path", comma + 1, (size_t)(colon - comma - 1));
            } else {
                f_str(sb, "dest", gt + 1, (size_t)(colon - gt - 1));
            }
            f_cstr(sb, "info", colon + 1);
        } else {
            f_cstr(sb, "message", p);
        }
    } else {
        const char *p = after_name(demod, line);
        if (p && starts_with(p, "fm ")) {
            f_cstr(sb, "kind", "ax25");
            p += 3;
            const char *to = strstr(p, " to ");
            if (to) {
                f_str(sb, "from", p, (size_t)(to - p));
                p = to + 4;
                const char *sp = strchr(p, ' ');
                size_t tl = sp ? (size_t)(sp - p) : strlen(p);
                f_str(sb, "to", p, tl);
                p += tl;
                while (*p == ' ')
                    p++;
                if (starts_with(p, "via ")) {
                    p += 4;
                    const char *sp2 = strchr(p, ' ');
                    size_t vl = sp2 ? (size_t)(sp2 - p) : strlen(p);
                    f_str(sb, "via", p, vl);
                    p += vl;
                    while (*p == ' ')
                        p++;
                }
                const char *pid = strstr(p, " pid=");
                if (pid) {
                    f_str(sb, "frame", p, (size_t)(pid - p));
                    f_cstr(sb, "pid", pid + 5);
                } else {
                    f_cstr(sb, "frame", p);
                }
            } else {
                f_cstr(sb, "message", p);
            }
        } else {
            f_message_generic(sb, demod, line);
        }
    }
    free(line);
    if (nl && nl[1]) {
        /* payload lines under the header, one array element each */
        f_key(sb, "info_lines");
        hev_sb_cat(sb, "[");
        const char *s = nl + 1;
        int first = 1;
        while (*s) {
            const char *e = strchr(s, '\n');
            size_t l = e ? (size_t)(e - s) : strlen(s);
            if (!first)
                hev_sb_cat(sb, ",");
            hev_sb_json_str(sb, s, l);
            first = 0;
            if (!e)
                break;
            s = e + 1;
        }
        hev_sb_cat(sb, "]");
    }
}

/* ---------------------------------------------------------------------- */
/* X10: bstring / bytes / hex / housecode lines printed together */

static void fields_x10(struct hev_sb *sb, const char *text)
{
    const char *p = strstr(text, "bstring = ");
    if (p) {
        p += 10;
        const char *e = strchr(p, '\n');
        f_str(sb, "bits", p, e ? (size_t)(e - p) : strlen(p));
    }
    p = strstr(text, "housecode = ");
    if (p) {
        p += 12;
        if (p[0] && p[1] == ' ') {
            f_str(sb, "house", p, 1);
            f_int(sb, "unit", strtol(p + 2, NULL, 10));
        }
    }
}

/* ---------------------------------------------------------------------- */

void hev_build(struct hev_sb *out, double t, const char *demod, const char *text)
{
    hev_sb_fmt(out, "{\"t\":%.3f,\"type\":", t);
    hev_sb_json_str(out, demod, strlen(demod));

    if (is_pocsag(demod))
        fields_pocsag(out, demod, text);
    else if (!strcmp(demod, "GSC"))
        fields_gsc(out, text);
    else if (!strcmp(demod, "FLEX"))
        fields_flex(out, text);
    else if (!strcmp(demod, "FLEX_NEXT"))
        fields_flex_next(out, text);
    else if (!strcmp(demod, "EAS"))
        fields_eas(out, text);
    else if (!strcmp(demod, "DTMF")) {
        const char *m = after_name("DTMF", text);
        if (m && m[0])
            f_str(out, "digit", m, 1);
        else
            f_message_generic(out, demod, text);
    } else if (is_selcall(demod)) {
        const char *m = after_name(demod, text);
        f_cstr(out, "digits", m ? m : text);
    } else if (is_hdlc(demod))
        fields_hdlc(out, demod, text);
    else if (!strcmp(demod, "X10"))
        fields_x10(out, text);
    else
        f_message_generic(out, demod, text);

    f_cstr(out, "text", text);
    hev_sb_cat(out, "}");
}
