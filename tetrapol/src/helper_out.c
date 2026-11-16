/*
 * helper_out.c -- JSON Lines output of the FoxSDR "tetrapol" GPL helper
 *
 * Part of the FoxSDR tetrapol helper, derived from tetrapol-kit (https://github.com/airphel/tetrapol-kit-2023,
 * GPL-2.0). New code written for the helper: it replaces upstream's frame_json.c, tsdu_json.c and tsdu_print.c
 * (which printed raw bytes and text). Message names, field names and the meaning of the fields follow the TETRAPOL
 * air interface specification PAS 0001-3-2 (messages, section 4.4, information elements, section 5.3) and
 * PAS 0001-3-3 (link and transport layers), as cited in the upstream headers.
 *
 * What is NOT output, on purpose: the bytes of any data message (short data, datagrams, ability management,
 * information delivery ...), any voice or circuit-data frame content, authentication challenges, call keys.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation; version 2 of the License.
 */
#include <tetrapol/helper_out.h>
#include <tetrapol/hdlc_frame.h>
#include <tetrapol/misc.h>

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------------------------ */
/* output state                                                                                                  */

static tp_out_cfg_t g_cfg;
static tp_out_stats_t g_stats;

#define LINE_MAX_LEN 16384

void tp_out_init(const tp_out_cfg_t *cfg)
{
    g_cfg = *cfg;
    memset(&g_stats, 0, sizeof(g_stats));
}

const tp_out_stats_t *tp_out_stats(void)
{
    return &g_stats;
}

double tp_out_time(const tpol_t *tpol)
{
    return (double)tpol->rx_offs / (double)TP_BIT_RATE;
}

void tp_out_line(const char *line, size_t len)
{
    if (fwrite(line, 1, len, g_cfg.out) != len || fputc('\n', g_cfg.out) == EOF || fflush(g_cfg.out) != 0) {
        fprintf(stderr, "tetrapol: cannot write to standard output; stopping\n");
        exit(3);
    }
}

/* ------------------------------------------------------------------------------------------------------------ */
/* a tiny JSON writer: one line, bounded, never overflows (an over-long line is dropped and counted)             */

typedef struct {
    char *p;
    size_t n;
    size_t cap;
    int over;
} jb_t;

static void jb_raw(jb_t *j, const char *s)
{
    size_t l = strlen(s);
    if (j->n + l + 1 > j->cap) {
        j->over = 1;
        return;
    }
    memcpy(j->p + j->n, s, l + 1);
    j->n += l;
}

static void jb_printf(jb_t *j, const char *fmt, ...)
{
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (r < 0 || r >= (int)sizeof(tmp)) {
        j->over = 1;
        return;
    }
    jb_raw(j, tmp);
}

/* comma unless the line just opened an object or array */
static void jb_comma(jb_t *j)
{
    if (j->n && j->p[j->n - 1] != '{' && j->p[j->n - 1] != '[') {
        jb_raw(j, ",");
    }
}

static void jb_key(jb_t *j, const char *k)
{
    jb_comma(j);
    jb_printf(j, "\"%s\":", k);
}

static void jb_obj_open(jb_t *j, const char *k)
{
    if (k) {
        jb_key(j, k);
    } else {
        jb_comma(j);
    }
    jb_raw(j, "{");
}

static void jb_obj_close(jb_t *j) { jb_raw(j, "}"); }

static void jb_arr_open(jb_t *j, const char *k)
{
    jb_key(j, k);
    jb_raw(j, "[");
}

static void jb_arr_close(jb_t *j) { jb_raw(j, "]"); }

static void jb_int(jb_t *j, const char *k, long long v)
{
    jb_key(j, k);
    jb_printf(j, "%lld", v);
}

static void jb_null(jb_t *j, const char *k)
{
    jb_key(j, k);
    jb_raw(j, "null");
}

static void jb_bool(jb_t *j, const char *k, int v)
{
    jb_key(j, k);
    jb_raw(j, v ? "true" : "false");
}

/* only ASCII text made by this file is written; characters that need escaping are replaced */
static void jb_str(jb_t *j, const char *k, const char *s)
{
    char tmp[200];
    size_t o = 0;
    for (; *s && o + 1 < sizeof(tmp); ++s) {
        unsigned char c = (unsigned char)*s;
        tmp[o++] = (c < 0x20 || c == '"' || c == '\\' || c >= 0x7f) ? '?' : (char)c;
    }
    tmp[o] = 0;
    jb_key(j, k);
    jb_printf(j, "\"%s\"", tmp);
}

static void jb_start(jb_t *j, char *buf, size_t cap, const tpol_t *tpol, const char *type)
{
    j->p = buf;
    j->n = 0;
    j->cap = cap;
    j->over = 0;
    buf[0] = 0;
    jb_raw(j, "{");
    jb_key(j, "t");
    jb_printf(j, "%.4f", tp_out_time(tpol));
    jb_str(j, "type", type);
}

static void jb_finish(jb_t *j)
{
    jb_obj_close(j);
    if (j->over) {
        ++g_stats.lines_dropped;
        return;
    }
    tp_out_line(j->p, j->n);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* --hide-ids: terminal and group identities as short hashes, stable inside one session                           */

static uint64_t fnv1a(uint64_t h, const void *data, size_t n)
{
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* kind: 'a' terminal / link address, 'g' group, 'r' RFSI or PABX address, 's' serial number */
static void hide_str(char *out, size_t cap, char kind, const void *bytes, size_t n)
{
    uint64_t h = 14695981039346656037ULL;
    h = fnv1a(h, &g_cfg.salt, sizeof(g_cfg.salt));
    h = fnv1a(h, &kind, 1);
    h = fnv1a(h, bytes, n);
    h ^= h >> 32;
    snprintf(out, cap, "h:%08x", (unsigned)(h & 0xffffffffu));
}

/* addresses that are broadcast constants, not identities */
static int addr_is_constant(const addr_t *a)
{
    return (a->y == 7 && a->x == 0xfff) || (a->y == 7 && a->x == 0) || (a->y == 0 && a->x == 0xfff) ||
           (a->y == 1 && a->x == 0);
}

static void jb_addr(jb_t *j, const char *k, const addr_t *a)
{
    char buf[32];
    if (g_cfg.hide_ids && !addr_is_constant(a)) {
        uint8_t b[4] = {a->z, a->y, (uint8_t)(a->x >> 8), (uint8_t)a->x};
        hide_str(buf, sizeof(buf), 'a', b, sizeof(b));
    } else {
        snprintf(buf, sizeof(buf), "%d.%d.0x%03x", a->z, a->y, a->x);
    }
    jb_str(j, k, buf);
}

/* group identity (12 bit) */
static void jb_gid(jb_t *j, const char *k, unsigned gid)
{
    if (g_cfg.hide_ids) {
        char buf[32];
        uint8_t b[2] = {(uint8_t)(gid >> 8), (uint8_t)gid};
        hide_str(buf, sizeof(buf), 'g', b, sizeof(b));
        jb_str(j, k, buf);
    } else {
        jb_int(j, k, gid);
    }
}

/* RFSI / PABX address of a terminal (PAS 0001-3-2 5.3.4) */
static void jb_address(jb_t *j, const char *k, const address_t *a)
{
    char buf[64];
    const char *kind = "none";
    buf[0] = 0;
    switch (a->cna) {
        case ADDRESS_CNA_NOT_SIGNIFICANT:
            kind = "none";
            break;
        case ADDRESS_CNA_RFSI:
            kind = "rfsi";
            if (g_cfg.hide_ids) {
                hide_str(buf, sizeof(buf), 'r', a->rfsi.addr, sizeof(a->rfsi.addr));
            } else {
                snprintf(buf, sizeof(buf), "%x%x%x-%x-%x%x-%x%x%x", a->rfsi.r[0], a->rfsi.r[1], a->rfsi.r[2],
                         a->rfsi.f, a->rfsi.s[0], a->rfsi.s[1], a->rfsi.i[0], a->rfsi.i[1], a->rfsi.i[2]);
            }
            break;
        case ADDRESS_CNA_PABX: {
            kind = "pabx";
            int n = a->len > 15 ? 15 : a->len;
            if (g_cfg.hide_ids) {
                hide_str(buf, sizeof(buf), 'r', a->pabx, (size_t)n);
            } else {
                for (int i = 0; i < n; ++i) {
                    buf[i] = (char)('0' + (a->pabx[i] % 10));
                }
                buf[n] = 0;
            }
            break;
        }
        default:
            kind = "unsupported";
            break;
    }
    jb_obj_open(j, k);
    jb_str(j, "kind", kind);
    if (buf[0]) {
        jb_str(j, "value", buf);
    }
    jb_obj_close(j);
}

static void jb_hex(jb_t *j, const char *k, const uint8_t *d, int n)
{
    char tmp[64];
    if (n > 31) {
        n = 31;
    }
    sprint_hex2(tmp, d, n);
    jb_str(j, k, tmp);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* simple events                                                                                                 */

void tp_emit_sync(const tpol_t *tpol, int found)
{
    char buf[256];
    jb_t j;
    jb_start(&j, buf, sizeof(buf), tpol, "sync");
    jb_str(&j, "state", found ? "found" : "lost");
    jb_finish(&j);
    if (found) {
        ++g_stats.sync_found;
    } else {
        ++g_stats.sync_lost;
    }
}

void tp_emit_scr(const tpol_t *tpol, int scr)
{
    char buf[256];
    jb_t j;
    jb_start(&j, buf, sizeof(buf), tpol, "scr");
    jb_int(&j, "scr", scr);
    jb_finish(&j);
}

void tp_emit_status(const tpol_t *tpol, const char *text)
{
    char buf[512];
    jb_t j;
    jb_start(&j, buf, sizeof(buf), tpol, "status");
    jb_str(&j, "text", text);
    jb_finish(&j);
}

void tp_emit_frame(const tpol_t *tpol, const frame_t *fr)
{
    if (fr->broken) {
        ++g_stats.frames_bad;
    } else {
        ++g_stats.frames_ok;
        if (fr->bits_fixed) {
            ++g_stats.frames_fixed;
        }
    }
    if ((fr->broken && !g_cfg.report_bad_frames) || (!fr->broken && g_cfg.no_frames)) {
        return;
    }
    char buf[512];
    jb_t j;
    jb_start(&j, buf, sizeof(buf), tpol, "frame");
    if (tpol->frame_no != FRAME_NO_UNKNOWN) {
        jb_int(&j, "frame_no", tpol->frame_no);
    } else {
        jb_null(&j, "frame_no");
    }
    if (!fr->broken) {
        jb_str(&j, "state", "ok");
    } else if (fr->broken == -1) {
        jb_str(&j, "state", "bad_crc");
    } else if (fr->broken == -2) {
        jb_str(&j, "state", "invalid");
    } else {
        jb_str(&j, "state", "uncorrectable");
    }
    jb_int(&j, "syndromes", fr->syndromes);
    jb_int(&j, "bits_fixed", fr->bits_fixed);
    jb_finish(&j);
}

void tp_emit_traffic(const tpol_t *tpol, const frame_t *fr)
{
    ++g_stats.traffic_frames;
    char buf[512];
    jb_t j;
    jb_start(&j, buf, sizeof(buf), tpol, "traffic");
    jb_str(&j, "kind", fr->fr_type == FRAME_TYPE_VOICE ? "voice frame" : "data frame");
    jb_str(&j, "text", "traffic frame, not decoded");
    jb_int(&j, "syndromes", fr->syndromes);
    jb_finish(&j);
}

void tp_emit_not_decoded(const tpol_t *tpol, const char *what)
{
    ++g_stats.traffic_frames;
    char buf[512];
    jb_t j;
    jb_start(&j, buf, sizeof(buf), tpol, "traffic");
    jb_str(&j, "kind", what);
    jb_str(&j, "text", "traffic frame, not decoded");
    jb_finish(&j);
}

void tp_emit_pch(const tpol_t *tpol, const uint8_t *act_bitmap, int nbitmap, const addr_t *addrs, int naddrs)
{
    char buf[1024];
    jb_t j;
    jb_start(&j, buf, sizeof(buf), tpol, "paging");
    if (tpol->frame_no != FRAME_NO_UNKNOWN) {
        jb_int(&j, "frame_no", tpol->frame_no);
    } else {
        jb_null(&j, "frame_no");
    }
    jb_hex(&j, "activation_bitmap", act_bitmap, nbitmap);
    jb_arr_open(&j, "addrs");
    for (int i = 0; i < naddrs && i < 4; ++i) {
        jb_comma(&j);
        char b[32];
        if (g_cfg.hide_ids && !addr_is_constant(&addrs[i])) {
            uint8_t raw[4] = {addrs[i].z, addrs[i].y, (uint8_t)(addrs[i].x >> 8), (uint8_t)addrs[i].x};
            hide_str(b, sizeof(b), 'a', raw, sizeof(raw));
        } else {
            snprintf(b, sizeof(b), "%d.%d.0x%03x", addrs[i].z, addrs[i].y, addrs[i].x);
        }
        jb_printf(&j, "\"%s\"", b);
    }
    jb_arr_close(&j);
    jb_finish(&j);
}

/* RCH: acknowledgements of random access (PAS 0001-3-3), z=1 entries are negative acks */
void tp_emit_rch(const tpol_t *tpol, const addr_t *addrs, int naddrs)
{
    char buf[1024];
    jb_t j;
    jb_start(&j, buf, sizeof(buf), tpol, "ack");
    if (tpol->frame_no != FRAME_NO_UNKNOWN) {
        jb_int(&j, "frame_no", tpol->frame_no);
    } else {
        jb_null(&j, "frame_no");
    }
    jb_arr_open(&j, "acks");
    int nnack = 0;
    for (int i = 0; i < naddrs && i < 3; ++i) {
        if (addrs[i].z) {
            ++nnack;
            continue;
        }
        jb_comma(&j);
        char b[32];
        if (g_cfg.hide_ids && !addr_is_constant(&addrs[i])) {
            uint8_t raw[4] = {addrs[i].z, addrs[i].y, (uint8_t)(addrs[i].x >> 8), (uint8_t)addrs[i].x};
            hide_str(b, sizeof(b), 'a', raw, sizeof(raw));
        } else {
            snprintf(b, sizeof(b), "%d.%d.0x%03x", addrs[i].z, addrs[i].y, addrs[i].x);
        }
        jb_printf(&j, "\"%s\"", b);
    }
    jb_arr_close(&j);
    jb_arr_open(&j, "nacks");
    for (int i = 0; i < naddrs && i < 3; ++i) {
        if (!addrs[i].z) {
            continue;
        }
        jb_comma(&j);
        jb_printf(&j, "\"%s\"", addrs[i].y == 4 ? "noise" : addrs[i].y == 5 ? "collision" : "unknown");
    }
    jb_arr_close(&j);
    (void)nnack;
    jb_finish(&j);
}

void tp_out_count_bits(uint64_t n)
{
    g_stats.bits += n;
}

void tp_emit_stats(const tpol_t *tpol)
{
    char buf[1024];
    jb_t j;
    tpol_t end = *tpol;
    end.rx_offs = g_stats.bits; /* the last line is stamped with the end of the input */
    jb_start(&j, buf, sizeof(buf), &end, "stats");
    jb_int(&j, "bits", (long long)g_stats.bits);
    jb_int(&j, "frames_ok", (long long)g_stats.frames_ok);
    jb_int(&j, "frames_bad", (long long)g_stats.frames_bad);
    jb_int(&j, "frames_corrected", (long long)g_stats.frames_fixed);
    jb_int(&j, "messages", (long long)g_stats.messages);
    jb_int(&j, "messages_data_not_decoded", (long long)g_stats.messages_data);
    jb_int(&j, "messages_unknown", (long long)g_stats.messages_unknown);
    jb_int(&j, "traffic_frames_not_decoded", (long long)g_stats.traffic_frames);
    jb_int(&j, "sync_found", (long long)g_stats.sync_found);
    jb_int(&j, "sync_lost", (long long)g_stats.sync_lost);
    jb_int(&j, "lines_dropped", (long long)g_stats.lines_dropped);
    jb_finish(&j);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* TSDU messages                                                                                                 */

enum { C_SYSTEM, C_REG, C_CALL, C_GROUP, C_DATA, C_AUTH, C_OTHER };
static const char *const class_names[] = {"system", "registration", "call", "group", "data", "authentication",
                                          "other"};

typedef struct {
    uint8_t codop;
    const char *name;
    uint8_t cls;
    uint8_t content; /* 1: the message carries user data; it is never decoded */
} codop_info_t;

/* names as in PAS 0001-3-2 4.4 (see tsdu.h) */
static const codop_info_t codops[] = {
    {D_REJECT, "D_REJECT", C_OTHER, 0},
    {D_REFUSAL, "D_REFUSAL", C_OTHER, 0},
    {U_END, "U_END", C_CALL, 0},
    {D_BACK_CCH, "D_BACK_CCH", C_SYSTEM, 0},
    {D_RELEASE, "D_RELEASE", C_CALL, 0},
    {U_ABORT, "U_ABORT", C_CALL, 0},
    {U_TERMINATE, "U_TERMINATE", C_CALL, 0},
    {D_HOOK_ON_INVITATION, "D_HOOK_ON_INVITATION", C_CALL, 0},
    {D_RETURN, "D_RETURN", C_OTHER, 0},
    {U_EVENT_REPORT, "U_EVENT_REPORT", C_OTHER, 0},
    {D_CALL_WAITING, "D_CALL_WAITING", C_CALL, 0},
    {D_AUTHENTICATION, "D_AUTHENTICATION", C_AUTH, 0},
    {U_AUTHENTICATION, "U_AUTHENTICATION", C_AUTH, 0},
    {D_AUTHORISATION, "D_AUTHORISATION", C_AUTH, 0},
    {U_ERROR_REPORT, "U_ERROR_REPORT", C_OTHER, 0},
    {D_CHANNEL_INIT, "D_CHANNEL_INIT", C_SYSTEM, 0},
    {U_REGISTRATION_REQ, "U_REGISTRATION_REQ", C_REG, 0},
    {D_REGISTRATION_NAK, "D_REGISTRATION_NAK", C_REG, 0},
    {D_REGISTRATION_ACK, "D_REGISTRATION_ACK", C_REG, 0},
    {D_FORCED_REGISTRATION, "D_FORCED_REGISTRATION", C_REG, 0},
    {U_LOCATION_ACTIVITY, "U_LOCATION_ACTIVITY", C_REG, 0},
    {D_LOCATION_ACTIVITY_ACK, "D_LOCATION_ACTIVITY_ACK", C_REG, 0},
    {U_CALL_SETUP, "U_CALL_SETUP", C_CALL, 0},
    {D_CALL_ALERT, "D_CALL_ALERT", C_CALL, 0},
    {D_CALL_SETUP, "D_CALL_SETUP", C_CALL, 0},
    {U_CALL_ANSWER, "U_CALL_ANSWER", C_CALL, 0},
    {D_CALL_CONNECT, "D_CALL_CONNECT", C_CALL, 0},
    {D_CALL_SWITCH, "D_CALL_SWITCH", C_CALL, 0},
    {U_CALL_INTRUSION_PC, "U_CALL_INTRUSION_PC", C_CALL, 0},
    {U_CALL_INTRUSION_OCH, "U_CALL_INTRUSION_OCH", C_CALL, 0},
    {D_TRANSFER_NAK, "D_TRANSFER_NAK", C_CALL, 0},
    {U_TRANSFER_REQ, "U_TRANSFER_REQ", C_CALL, 0},
    {U_CALL_INTRUSION_ECH, "U_CALL_INTRUSION_ECH", C_CALL, 0},
    {U_CALL_RELEASE, "U_CALL_RELEASE", C_CALL, 0},
    {U_CALL_CONNECT, "U_CALL_CONNECT", C_CALL, 0},
    {D_CALL_START, "D_CALL_START", C_CALL, 0},
    {D_CALL_ACTIVATION, "D_CALL_ACTIVATION", C_CALL, 0},
    {D_CALL_COMPOSITION, "D_CALL_COMPOSITION", C_CALL, 0},
    {D_CALL_END, "D_CALL_END", C_CALL, 0},
    {D_CALL_OVERLOAD_ID, "D_CALL_OVERLOAD_ID", C_CALL, 0},
    {D_FUNCTIONAL_SHORT_DATA, "D_FUNCTIONAL_SHORT_DATA", C_DATA, 1},
    {U_DATA_DOWN_ACCEPT, "U_DATA_DOWN_ACCEPT", C_DATA, 0},
    {U_DATA_MSG_UP, "U_DATA_MSG_UP", C_DATA, 1},
    {D_DATA_MSG_DOWN, "D_DATA_MSG_DOWN", C_DATA, 1},
    {D_EXPLICIT_SHORT_DATA, "D_EXPLICIT_SHORT_DATA", C_DATA, 1},
    {D_DATA_END, "D_DATA_END", C_DATA, 0},
    {D_DATAGRAM_NOTIFY, "D_DATAGRAM_NOTIFY", C_DATA, 0},
    {D_DATAGRAM, "D_DATAGRAM", C_DATA, 1},
    {D_BROADCAST, "D_BROADCAST", C_DATA, 1},
    {D_DATA_SERV, "D_DATA_SERV", C_DATA, 0},
    {D_DATA_DOWN_STATUS, "D_DATA_DOWN_STATUS", C_DATA, 0},
    {D_CONNECT_DCH, "D_CONNECT_DCH", C_CALL, 0},
    {D_CONNECT_CCH, "D_CONNECT_CCH", C_SYSTEM, 0},
    {D_DATA_AUTHENTICATION, "D_DATA_AUTHENTICATION", C_AUTH, 0},
    {D_DATA_REQUEST, "D_DATA_REQUEST", C_DATA, 0},
    {D_DCH_OPEN, "D_DCH_OPEN", C_SYSTEM, 0},
    {U_DATA_REQUEST, "U_DATA_REQUEST", C_DATA, 0},
    {D_EXTENDED_STATUS, "D_EXTENDED_STATUS", C_DATA, 1},
    {D_CCH_OPEN, "D_CCH_OPEN", C_SYSTEM, 0},
    {D_BROADCAST_WAITING, "D_BROADCAST_WAITING", C_GROUP, 0},
    {U_OCH_RELEASE, "U_OCH_RELEASE", C_GROUP, 0},
    {U_OCH_SETUP, "U_OCH_SETUP", C_GROUP, 0},
    {U_ECH_CLOSE, "U_ECH_CLOSE", C_GROUP, 0},
    {D_EMERGENCY_NOTIFICATION, "D_EMERGENCY_NOTIFICATION", C_GROUP, 0},
    {U_ECH_SETUP, "U_ECH_SETUP", C_GROUP, 0},
    {D_GROUP_ACTIVATION, "D_GROUP_ACTIVATION", C_GROUP, 0},
    {D_ECH_ACTIVATION, "D_ECH_ACTIVATION", C_GROUP, 0},
    {D_GROUP_END, "D_GROUP_END", C_GROUP, 0},
    {D_GROUP_IDLE, "D_GROUP_IDLE", C_GROUP, 0},
    {D_GROUP_REJECT, "D_GROUP_REJECT", C_GROUP, 0},
    {D_ECH_REJECT, "D_ECH_REJECT", C_GROUP, 0},
    {D_GROUP_PAGING, "D_GROUP_PAGING", C_GROUP, 0},
    {D_BROADCAST_NOTIFICATION, "D_BROADCAST_NOTIFICATION", C_GROUP, 0},
    {D_CRISIS_NOTIFICATION, "D_CRISIS_NOTIFICATION", C_GROUP, 0},
    {D_EMERGENCY_ACK, "D_EMERGENCY_ACK", C_GROUP, 0},
    {D_EMERGENCY_NAK, "D_EMERGENCY_NAK", C_GROUP, 0},
    {U_EMERGENCY_REQ, "U_EMERGENCY_REQ", C_GROUP, 0},
    {D_GROUP_OVERLOAD_ID, "D_GROUP_OVERLOAD_ID", C_GROUP, 0},
    {D_ECH_OVERLOAD_ID, "D_ECH_OVERLOAD_ID", C_GROUP, 0},
    {D_PRIORITY_GRP_WAITING, "D_PRIORITY_GRP_WAITING", C_GROUP, 0},
    {D_PRIORITY_GRP_ACTIVATION, "D_PRIORITY_GRP_ACTIVATION", C_GROUP, 0},
    {D_OC_ACTIVATION, "D_OC_ACTIVATION", C_GROUP, 0},
    {D_OC_REJECT, "D_OC_REJECT", C_GROUP, 0},
    {D_OC_PAGING, "D_OC_PAGING", C_GROUP, 0},
    {D_ACCESS_DISABLED, "D_ACCESS_DISABLED", C_SYSTEM, 0},
    {D_TRAFFIC_ENABLED, "D_TRAFFIC_ENABLED", C_SYSTEM, 0},
    {D_TRAFFIC_DISABLED, "D_TRAFFIC_DISABLED", C_SYSTEM, 0},
    {U_DEVIATION_CLEAR, "U_DEVIATION_CLEAR", C_OTHER, 0},
    {U_DEVIATION_SET, "U_DEVIATION_SET", C_OTHER, 0},
    {D_DEVIATION_ON, "D_DEVIATION_ON", C_OTHER, 0},
    {D_SERVICE_DISABLED, "D_SERVICE_DISABLED", C_SYSTEM, 0},
    {D_ABILITY_MNGT, "D_ABILITY_MNGT", C_DATA, 1},
    {D_SYSTEM_INFO, "D_SYSTEM_INFO", C_SYSTEM, 0},
    {D_GROUP_LIST, "D_GROUP_LIST", C_SYSTEM, 0},
    {D_GROUP_COMPOSITION, "D_GROUP_COMPOSITION", C_GROUP, 0},
    {D_NEIGHBOURING_CELL, "D_NEIGHBOURING_CELL", C_SYSTEM, 0},
    {D_ECCH_DESCRIPTION, "D_ECCH_DESCRIPTION", C_SYSTEM, 0},
    {D_ADDITIONAL_PARTICIPANTS, "D_ADDITIONAL_PARTICIPANTS", C_CALL, 0},
    {D_INFORMATION_DELIVERY, "D_INFORMATION_DELIVERY", C_DATA, 1},
};

static const codop_info_t *codop_lookup(uint8_t codop)
{
    for (size_t i = 0; i < sizeof(codops) / sizeof(codops[0]); ++i) {
        if (codops[i].codop == codop) {
            return &codops[i];
        }
    }
    return NULL;
}

int tp_codop_is_data(uint8_t codop)
{
    const codop_info_t *ci = codop_lookup(codop);
    return ci && ci->content;
}

static void jb_key_ref(jb_t *j, const char *k, key_reference_t kr)
{
    /* which key type / index the call is announced with (key_index 0 = clear call); the key itself is never here */
    jb_obj_open(j, k);
    jb_int(j, "key_type", kr.key_type);
    jb_int(j, "key_index", kr.key_index);
    jb_bool(j, "clear", kr.key_index == KEY_INDEX_CLEAR_CALL);
    jb_obj_close(j);
}

static void jb_activation(jb_t *j, const activation_mode_t *am)
{
    jb_obj_open(j, "activation_mode");
    jb_int(j, "hook", am->hook);
    jb_int(j, "type", am->type);
    jb_obj_close(j);
}

static void jb_cell_id(jb_t *j, const char *k, const cell_id_t *c)
{
    jb_obj_open(j, k);
    jb_int(j, "bs_id", (int8_t)c->bs_id == -1 ? -1 : c->bs_id);
    jb_int(j, "rsw_id", (int8_t)c->rsw_id == -1 ? -1 : c->rsw_id);
    jb_obj_close(j);
}

static void jb_rt_status(jb_t *j, const char *k, rt_status_t s)
{
    jb_obj_open(j, k);
    jb_int(j, "tra", s.tra);
    jb_int(j, "ren", s.ren);
    jb_int(j, "chg", s.chg);
    jb_int(j, "pro", s.pro);
    jb_int(j, "fix", s.fix);
    jb_obj_close(j);
}

static void fields_system_info(jb_t *j, const tsdu_d_system_info_t *t)
{
    jb_obj_open(j, "cell_state");
    jb_int(j, "mode", t->cell_state.mode);
    jb_int(j, "bch", t->cell_state.bch);
    if (t->cell_state.mode == CELL_STATE_MODE_NORMAL) {
        jb_bool(j, "experimental", t->cell_state.exp);
        jb_bool(j, "home_only", t->cell_state.roam);
    }
    jb_obj_close(j);
    jb_cell_id(j, "cell_id", &t->cell_id);
    jb_int(j, "bn_id", t->bn_id);
    jb_int(j, "u_ch_scrambling", t->u_ch_scrambling);
    jb_obj_open(j, "radio_param");
    jb_int(j, "tx_max", t->cell_radio_param.tx_max);
    jb_int(j, "radio_link_timeout", t->cell_radio_param.radio_link_timeout);
    jb_int(j, "pwr_tx_adjust_dbm", CELL_RADIO_PARAM_PWR_TX_ADJUST_TO_DBM[t->cell_radio_param.pwr_tx_adjust & 15]);
    jb_int(j, "rx_lev_access_dbm", CELL_RADIO_PARAM_RX_LEV_ACCESS_TO_DBM[t->cell_radio_param.rx_lev_access & 15]);
    jb_obj_close(j);
    if (t->cell_state.mode != CELL_STATE_MODE_NORMAL) {
        /* cell in disconnected mode: only band and channel are announced */
        jb_int(j, "band", t->band);
        jb_int(j, "channel_id", t->channel_id);
        return;
    }
    jb_obj_open(j, "cell_config");
    jb_bool(j, "double_coverage", t->cell_config.dc);
    jb_bool(j, "simulcast", t->cell_config.sim);
    jb_int(j, "mux_type", t->cell_config.mux_type);
    jb_bool(j, "atta", t->cell_config.atta);
    jb_bool(j, "ecch", t->cell_config.eccch);
    jb_obj_close(j);
    jb_int(j, "country_code", t->country_code);
    jb_obj_open(j, "system_id");
    jb_int(j, "network", t->system_id.network);
    jb_int(j, "version", t->system_id.version);
    jb_obj_close(j);
    jb_obj_open(j, "loc_area");
    jb_int(j, "id", t->loc_area_id.loc_id);
    jb_int(j, "mode", t->loc_area_id.mode);
    jb_obj_close(j);
    jb_obj_open(j, "cell_bn");
    jb_int(j, "r1", t->cell_bn.r1);
    jb_int(j, "r2", t->cell_bn.r2);
    jb_int(j, "r3", t->cell_bn.r3);
    jb_obj_close(j);
    jb_int(j, "system_time", t->system_time);
    jb_obj_open(j, "cell_access");
    jb_int(j, "min_service_class", t->cell_access.min_service_class);
    jb_int(j, "min_reg_class", t->cell_access.min_reg_class);
    jb_obj_close(j);
    jb_int(j, "superframe_cpt", t->superframe_cpt);
}

static void fields_neighbouring_cell(jb_t *j, const tsdu_d_neighbouring_cell_t *t)
{
    int n = t->ccr_config.number > 15 ? 15 : t->ccr_config.number;
    jb_int(j, "ccr_config", t->ccr_config.number);
    jb_arr_open(j, "adjacent_cells");
    for (int i = 0; i < n; ++i) {
        jb_obj_open(j, NULL);
        jb_int(j, "bn_nb", t->adj_cells[i].bn_nb);
        jb_int(j, "channel_id", t->adj_cells[i].channel_id);
        jb_bool(j, "same_bn", t->adj_cells[i].adjacent_param.bn);
        jb_bool(j, "same_loc", t->adj_cells[i].adjacent_param.loc);
        jb_bool(j, "experimental", t->adj_cells[i].adjacent_param.exp);
        jb_int(j, "rx_lev_access_dbm", CELL_RADIO_PARAM_RX_LEV_ACCESS_TO_DBM[t->adj_cells[i].adjacent_param.rxlev_access & 15]);
        jb_obj_close(j);
    }
    jb_arr_close(j);
    if (t->cell_ids) {
        jb_arr_open(j, "cell_ids");
        for (int i = 0; i < t->cell_ids->len && i < 64; ++i) {
            jb_obj_open(j, NULL);
            jb_int(j, "bs_id", t->cell_ids->cell_ids[i].bs_id);
            jb_int(j, "rsw_id", t->cell_ids->cell_ids[i].rsw_id);
            jb_obj_close(j);
        }
        jb_arr_close(j);
    }
    if (t->cell_bns) {
        jb_arr_open(j, "adjacent_bn");
        for (int i = 0; i < t->cell_bns->len && i < 64; ++i) {
            jb_obj_open(j, NULL);
            jb_int(j, "r1", t->cell_bns->cell_bn[i].r1);
            jb_int(j, "r2", t->cell_bns->cell_bn[i].r2);
            jb_int(j, "r3", t->cell_bns->cell_bn[i].r3);
            jb_obj_close(j);
        }
        jb_arr_close(j);
    }
}

static void fields_group_list(jb_t *j, const tsdu_d_group_list_t *t)
{
    jb_obj_open(j, "reference_list");
    jb_int(j, "revision", t->reference_list.revision);
    jb_bool(j, "umbrella", t->reference_list.dc);
    jb_bool(j, "cso_advise", t->reference_list.cso);
    jb_bool(j, "csg_advise", t->reference_list.csg);
    jb_obj_close(j);
    if (t->reference_list.revision == 0) {
        return;
    }
    jb_obj_open(j, "index_list");
    jb_int(j, "mode", t->index_list.mode);
    jb_int(j, "index", t->index_list.index);
    jb_obj_close(j);
    jb_int(j, "n_emergency", t->nemergency);
    jb_int(j, "n_talk_groups", t->ngroup);
    jb_int(j, "n_open", t->nopen);
    jb_arr_open(j, "emergency_cells");
    for (int i = 0; i < t->nemergency && i < 64; ++i) {
        jb_obj_open(j, NULL);
        jb_int(j, "bs_id", t->emergency[i].cell_id.bs_id);
        jb_int(j, "rsw_id", t->emergency[i].cell_id.rsw_id);
        jb_obj_close(j);
    }
    jb_arr_close(j);
    jb_arr_open(j, "talk_groups");
    for (int i = 0; i < t->ngroup && i < 64; ++i) {
        jb_obj_open(j, NULL);
        jb_int(j, "coverage_id", t->group[i].coverage_id);
        jb_int(j, "neighbouring_cell", t->group[i].neighbouring_cell);
        jb_obj_close(j);
    }
    jb_arr_close(j);
    jb_arr_open(j, "open_channels");
    for (int i = 0; i < t->nopen && i < 64; ++i) {
        jb_obj_open(j, NULL);
        jb_int(j, "coverage_id", t->open[i].coverage_id);
        jb_int(j, "call_priority", t->open[i].call_priority);
        jb_gid(j, "group_id", t->open[i].group_id);
        jb_bool(j, "add_allowed", t->open[i].och_parameters.add);
        jb_bool(j, "multi_bn", t->open[i].och_parameters.mbn);
        jb_int(j, "neighbouring_cell", t->open[i].neighbouring_cell);
        jb_obj_close(j);
    }
    jb_arr_close(j);
}

static void jb_cause(jb_t *j, uint8_t cause) { jb_int(j, "cause", cause); }

/* the decoded fields of one message; returns nothing, adds to the open object */
static void fields_of(jb_t *j, uint8_t codop, const tsdu_t *ts)
{
    switch (codop) {
        case D_SYSTEM_INFO:
            fields_system_info(j, (const tsdu_d_system_info_t *)ts);
            break;
        case D_NEIGHBOURING_CELL:
            fields_neighbouring_cell(j, (const tsdu_d_neighbouring_cell_t *)ts);
            break;
        case D_GROUP_LIST:
            fields_group_list(j, (const tsdu_d_group_list_t *)ts);
            break;
        case D_REGISTRATION_ACK: {
            const tsdu_d_registration_ack_t *t = (const tsdu_d_registration_ack_t *)ts;
            jb_int(j, "complete_reg", t->complete_reg);
            jb_int(j, "rt_min_activity", t->rt_min_activity);
            jb_rt_status(j, "rt_status", t->rt_status);
            jb_address(j, "host_adr", &t->host_adr);
            jb_int(j, "rt_min_registration", t->rt_min_registration);
            jb_int(j, "tlr_value", t->tlr_value);
            jb_obj_open(j, "rt_data_info");
            jb_int(j, "polling", t->rt_data_info.polling);
            jb_bool(j, "cnt", t->rt_data_info.cnt);
            jb_bool(j, "iab", t->rt_data_info.iab);
            jb_obj_close(j);
            jb_gid(j, "group_id", t->group_id);
            if (t->has_coverage_id) {
                jb_int(j, "coverage_id", t->coverage_id);
            }
            break;
        }
        case D_REGISTRATION_NAK: {
            const tsdu_d_registration_nak_t *t = (const tsdu_d_registration_nak_t *)ts;
            jb_cause(j, t->cause);
            jb_address(j, "host_adr", &t->host_adr);
            jb_int(j, "bn_id", t->bn_id);
            jb_cell_id(j, "cell_id", &t->cell_id);
            break;
        }
        case D_FORCED_REGISTRATION:
            jb_address(j, "calling_adr", &((const tsdu_d_forced_registration_t *)ts)->calling_adr);
            break;
        case D_LOCATION_ACTIVITY_ACK:
            jb_rt_status(j, "rt_status", ((const tsdu_d_location_activity_ack_t *)ts)->rt_status);
            break;
        case U_REGISTRATION_REQ: {
            const tsdu_u_registration_req_t *t = (const tsdu_u_registration_req_t *)ts;
            jb_address(j, "host_adr", &t->host_adr);
            jb_int(j, "system_id", t->system_id);
            if (g_cfg.hide_ids) {
                char b[32];
                hide_str(b, sizeof(b), 's', t->serial_nb, sizeof(t->serial_nb));
                jb_str(j, "serial_nb", b);
            } else {
                jb_hex(j, "serial_nb", t->serial_nb, sizeof(t->serial_nb));
            }
            jb_int(j, "counter_bn", t->counter_bn);
            jb_int(j, "counter_rsw", t->counter_rsw);
            jb_int(j, "reg_seq", t->reg_seq);
            jb_int(j, "complete_reg", t->complete_reg);
            jb_rt_status(j, "rt_status", t->rt_status);
            break;
        }
        case D_GROUP_ACTIVATION: {
            const tsdu_d_group_activation_t *t = (const tsdu_d_group_activation_t *)ts;
            jb_activation(j, &t->activation_mode);
            jb_gid(j, "group_id", t->group_id);
            jb_int(j, "coverage_id", t->coverage_id);
            jb_int(j, "channel_id", t->channel_id);
            jb_int(j, "u_ch_scrambling", t->u_ch_scrambling);
            jb_int(j, "d_ch_scrambling", t->d_ch_scrambling);
            jb_key_ref(j, "key_reference", t->key_reference);
            if (t->has_addr_tti) {
                jb_addr(j, "addr_tti", &t->addr_tti);
            }
            break;
        }
        case D_GROUP_PAGING: {
            const tsdu_d_group_paging_t *t = (const tsdu_d_group_paging_t *)ts;
            jb_activation(j, &t->activation_mode);
            jb_gid(j, "group_id", t->group_id);
            jb_int(j, "coverage_id", t->coverage_id);
            jb_key_ref(j, "key_reference", t->key_reference);
            break;
        }
        case D_GROUP_REJECT: {
            const tsdu_d_group_reject_t *t = (const tsdu_d_group_reject_t *)ts;
            jb_activation(j, &t->activation_mode);
            jb_gid(j, "group_id", t->group_id);
            jb_int(j, "coverage_id", t->coverage_id);
            jb_cause(j, t->cause);
            break;
        }
        case D_GROUP_IDLE:
            jb_cause(j, ((const tsdu_d_group_idle_t *)ts)->cause);
            break;
        case D_GROUP_COMPOSITION: {
            const tsdu_d_group_composition_t *t = (const tsdu_d_group_composition_t *)ts;
            jb_gid(j, "group_id", t->group_id);
            jb_int(j, "og_nb", t->og_nb);
            jb_arr_open(j, "group_ids");
            for (int i = 0; i < t->og_nb && i < 16; ++i) {
                jb_comma(j);
                if (g_cfg.hide_ids) {
                    char b[32];
                    uint8_t raw[2] = {(uint8_t)(t->group_ids[i] >> 8), (uint8_t)t->group_ids[i]};
                    hide_str(b, sizeof(b), 'g', raw, 2);
                    jb_printf(j, "\"%s\"", b);
                } else {
                    jb_printf(j, "%d", t->group_ids[i]);
                }
            }
            jb_arr_close(j);
            break;
        }
        case D_ECH_OVERLOAD_ID: {
            const tsdu_d_ech_overload_id_t *t = (const tsdu_d_ech_overload_id_t *)ts;
            jb_activation(j, &t->activation_mode);
            jb_gid(j, "group_id", t->group_id);
            jb_cell_id(j, "cell_id", &t->cell_id);
            jb_int(j, "organisation", t->organisation);
            break;
        }
        case D_CRISIS_NOTIFICATION: {
            const tsdu_d_crisis_notification_t *t = (const tsdu_d_crisis_notification_t *)ts;
            jb_address(j, "calling_adr", &t->calling_adr);
            jb_int(j, "organisation", t->organisation);
            jb_int(j, "coverage_id", t->coverage_id);
            jb_int(j, "og_nb", t->og_nb);
            jb_arr_open(j, "group_ids");
            for (int i = 0; i < t->og_nb && i < 5; ++i) {
                jb_comma(j);
                if (g_cfg.hide_ids) {
                    char b[32];
                    uint8_t raw[2] = {(uint8_t)(t->group_ids[i] >> 8), (uint8_t)t->group_ids[i]};
                    hide_str(b, sizeof(b), 'g', raw, 2);
                    jb_printf(j, "\"%s\"", b);
                } else {
                    jb_printf(j, "%d", t->group_ids[i]);
                }
            }
            jb_arr_close(j);
            break;
        }
        case D_CALL_SETUP: {
            const tsdu_d_call_setup_t *t = (const tsdu_d_call_setup_t *)ts;
            jb_address(j, "calling_adr", &t->calling_adr);
            if (t->has_add_setup_param) {
                jb_obj_open(j, "add_setup_param");
                jb_int(j, "origin", t->add_setup_param.origin);
                jb_bool(j, "id", t->add_setup_param.id);
                jb_bool(j, "mod", t->add_setup_param.mod);
                jb_bool(j, "sil", t->add_setup_param.sil);
                jb_obj_close(j);
            }
            break;
        }
        case D_CALL_CONNECT: {
            /* the call key and the valid_rt challenge bytes are left out */
            const tsdu_d_call_connect_t *t = (const tsdu_d_call_connect_t *)ts;
            jb_obj_open(j, "call_type");
            jb_int(j, "origin", t->call_type.origin);
            jb_int(j, "destination", t->call_type.destination);
            jb_bool(j, "trfs", t->call_type.trfs);
            jb_obj_close(j);
            jb_int(j, "channel_id", t->channel_id);
            jb_int(j, "u_ch_scrambling", t->u_ch_scrambling);
            jb_int(j, "d_ch_scrambling", t->d_ch_scrambling);
            jb_key_ref(j, "key_reference", t->key_reference);
            break;
        }
        case D_CALL_START: {
            const tsdu_d_call_start_t *t = (const tsdu_d_call_start_t *)ts;
            if (t->has_key_reference) {
                jb_key_ref(j, "key_reference", t->key_reference);
            }
            break;
        }
        case D_CONNECT_DCH: {
            const tsdu_d_connect_dch_t *t = (const tsdu_d_connect_dch_t *)ts;
            jb_int(j, "dch_low_layer", t->dch_low_layer);
            jb_int(j, "channel_id", t->channel_id);
            jb_int(j, "u_ch_scrambling", t->u_ch_scrambling);
            jb_int(j, "d_ch_scrambling", t->d_ch_scrambling);
            break;
        }
        case D_ADDITIONAL_PARTICIPANTS: {
            const tsdu_d_additional_participants_t *t = (const tsdu_d_additional_participants_t *)ts;
            jb_int(j, "coverage_id", t->coverage_id);
            if (t->calling_adr) {
                jb_arr_open(j, "calling_adr");
                for (int i = 0; i < t->calling_adr->nadrs && i < 8; ++i) {
                    jb_address(j, NULL, &t->calling_adr->called_adr[i]);
                }
                jb_arr_close(j);
            }
            break;
        }
        case D_REJECT:
            jb_cause(j, ((const tsdu_d_reject_t *)ts)->cause);
            break;
        case D_REFUSAL:
            jb_cause(j, ((const tsdu_d_refusal_t *)ts)->cause);
            break;
        case D_RELEASE:
            jb_cause(j, ((const tsdu_d_release_t *)ts)->cause);
            break;
        case D_RETURN:
            jb_cause(j, ((const tsdu_d_return_t *)ts)->cause);
            break;
        case D_HOOK_ON_INVITATION:
            jb_cause(j, ((const tsdu_d_hook_on_invitation_t *)ts)->cause);
            break;
        case D_DATA_END:
            jb_cause(j, ((const tsdu_d_data_end_t *)ts)->cause);
            break;
        case U_TERMINATE:
            jb_cause(j, ((const tsdu_u_terminate_t *)ts)->cause);
            break;
        case D_DATAGRAM_NOTIFY: {
            const tsdu_d_datagram_notify_t *t = (const tsdu_d_datagram_notify_t *)ts;
            jb_int(j, "call_priority", t->call_priority);
            jb_int(j, "message_reference", t->message_reference);
            jb_key_ref(j, "key_reference", t->key_reference);
            if (t->destination_port >= 0) {
                jb_int(j, "destination_port", t->destination_port);
            }
            break;
        }
        case D_DATA_REQUEST: {
            const tsdu_d_data_request_t *t = (const tsdu_d_data_request_t *)ts;
            jb_key_ref(j, "key_reference_ciph", t->key_reference_ciph);
            jb_int(j, "trans_mode", t->trans_mode);
            jb_int(j, "trans_param1", t->trans_param1);
            jb_int(j, "trans_param2", t->trans_param2);
            if (t->has_trans_param3) {
                jb_int(j, "trans_param3", t->trans_param3);
            }
            break;
        }
        case U_DATA_REQUEST: {
            const tsdu_u_data_request_t *t = (const tsdu_u_data_request_t *)ts;
            jb_int(j, "trans_mode", t->trans_mode);
            jb_int(j, "trans_param1", t->trans_param1);
            jb_int(j, "trans_param2", t->trans_param2);
            break;
        }
        case D_AUTHENTICATION:
            /* challenge bytes (valid_rt) are left out */
            jb_key_ref(j, "key_reference", ((const tsdu_d_authentication_t *)ts)->key_reference);
            break;
        case D_DATA_AUTHENTICATION: {
            const tsdu_d_data_authentication_t *t = (const tsdu_d_data_authentication_t *)ts;
            jb_key_ref(j, "key_reference_auth", t->key_reference_auth);
            jb_key_ref(j, "key_reference_ciph", t->key_reference_ciph);
            break;
        }
        case D_AUTHORISATION: {
            const tsdu_d_authorisation_t *t = (const tsdu_d_authorisation_t *)ts;
            if (t->has_key_reference) {
                jb_key_ref(j, "key_reference", t->key_reference);
            }
            break;
        }
        default:
            /* no fields: D_CALL_ALERT, D_CCH_OPEN, D_CONNECT_CCH, D_DCH_OPEN, U_AUTHENTICATION, U_CALL_CONNECT, ... */
            break;
    }
}

void tp_emit_tsdu(const tpol_t *tpol, const tpol_tsdu_t *tt, const tsdu_t *tsdu)
{
    if (tt->data_len <= 0 || !tt->data) {
        return;
    }
    const uint8_t codop = tt->data[0];
    const codop_info_t *ci = codop_lookup(codop);

    char buf[LINE_MAX_LEN];
    jb_t j;
    jb_start(&j, buf, sizeof(buf), tpol, "message");

    const char *log_ch;
    switch (tt->log_ch) {
        case LOG_CH_BCH:  log_ch = "BCH";  break;
        case LOG_CH_DACH: log_ch = "DACH"; break;
        case LOG_CH_PCH:  log_ch = "PCH";  break;
        case LOG_CH_RACH: log_ch = "RACH"; break;
        case LOG_CH_RCH:  log_ch = "RCH";  break;
        case LOG_CH_SDCH: log_ch = "SDCH"; break;
        case LOG_CH_SCH:  log_ch = "SCH";  break;
        case LOG_CH_VCH:  log_ch = "VCH";  break;
        default:          log_ch = "unknown"; break;
    }
    jb_str(&j, "log_ch", log_ch);
    if (tpol->frame_no != FRAME_NO_UNKNOWN) {
        jb_int(&j, "frame_no", tpol->frame_no);
    } else {
        jb_null(&j, "frame_no");
    }
    jb_str(&j, "tpdu", tt->tpdu_type == TPDU_TYPE_TPDU_UI ? "UI" : "TPDU");
    jb_addr(&j, "addr", &tt->addr);
    jb_int(&j, "prio", tt->prio);
    if (tt->tsap_id != TSAP_ID_UNKNOWN) {
        jb_int(&j, "tsap_id", tt->tsap_id);
    } else {
        jb_null(&j, "tsap_id");
    }
    if (tt->tpdu_type == TPDU_TYPE_TPDU) {
        if (tt->tsap_ref_swmi != TSAP_REF_UNKNOWN) {
            jb_int(&j, "tsap_ref_swmi", tt->tsap_ref_swmi);
        }
        if (tt->tsap_ref_rt != TSAP_REF_UNKNOWN) {
            jb_int(&j, "tsap_ref_rt", tt->tsap_ref_rt);
        }
    }
    char name[16];
    if (ci) {
        jb_str(&j, "msg", ci->name);
        jb_str(&j, "class", class_names[ci->cls]);
    } else {
        snprintf(name, sizeof(name), "UNKNOWN_0x%02x", codop);
        jb_str(&j, "msg", name);
        jb_str(&j, "class", "unknown");
    }
    jb_int(&j, "codop", codop);
    jb_int(&j, "len", tt->data_len);

    if (ci && ci->content) {
        ++g_stats.messages_data;
        jb_str(&j, "text", "data message, content not decoded");
    } else if (!ci) {
        ++g_stats.messages_unknown;
        jb_str(&j, "text", "message type not decoded");
    } else if (!tsdu) {
        ++g_stats.messages_unknown;
        jb_str(&j, "text", "message too short or damaged, not decoded");
    } else {
        ++g_stats.messages;
        fields_of(&j, codop, tsdu);
    }
    jb_finish(&j);
}
