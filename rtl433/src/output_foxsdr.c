/** @file
    FoxSDR GPL helper: the one output handler of this helper.

    New file written for the FoxSDR helper (not part of upstream rtl_433);
    the JSON number formats follow upstream's src/output_file.c (JSON printer).
    GPL-2.0-or-later, like the rest of the helper.
*/

#include "output_foxsdr.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

/* ---- growable text buffer ------------------------------------------------ */

typedef struct {
    char *p;
    size_t len;
    size_t cap;
} dbuf_t;

static void dbuf_reserve(dbuf_t *b, size_t more)
{
    if (b->len + more + 1 <= b->cap)
        return;
    size_t cap = b->cap ? b->cap : 512;
    while (cap < b->len + more + 1)
        cap *= 2;
    char *p = realloc(b->p, cap);
    if (!p) {
        fprintf(stderr, "rtl433 helper: out of memory\n");
        exit(1);
    }
    b->p   = p;
    b->cap = cap;
}

static void dbuf_cat(dbuf_t *b, char const *s)
{
    size_t n = strlen(s);
    dbuf_reserve(b, n);
    memcpy(b->p + b->len, s, n + 1);
    b->len += n;
}

static void dbuf_putc(dbuf_t *b, char c)
{
    dbuf_reserve(b, 1);
    b->p[b->len++] = c;
    b->p[b->len]   = '\0';
}

static void dbuf_printf(dbuf_t *b, char const *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

#include <stdarg.h>
static void dbuf_printf(dbuf_t *b, char const *fmt, ...)
{
    char tmp[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n < sizeof(tmp)) {
        dbuf_cat(b, tmp);
        return;
    }
    dbuf_reserve(b, (size_t)n);
    va_start(ap, fmt);
    vsnprintf(b->p + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

/* ---- identifier rules ---------------------------------------------------- */

static int streq_ci(char const *a, char const *b)
{
    for (; *a && *b; ++a, ++b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    }
    return *a == *b;
}

static int endswith(char const *s, char const *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && !strcmp(s + ls - lx, suffix);
}

int foxsdr_is_id_key(char const *key)
{
    static char const *const exact[] = {
        "id", "ids", "uid", "sn", "esn", "serial", "serial_number", "serialnumber",
        "address", "addr", "housecode", "house_code", "house_id", "node", "mac",
        "eui", "imei", "id_wire", "device_id", "sensor_id", "remote_id", "station_id",
        NULL};
    // identifier-like names that are NOT device identifiers (message numbers, commands, buttons)
    static char const *const not_ids[] = {
        "msg_id", "cmd_id", "sequence_id", "button_id", "packetTypeid", "protocolid",
        NULL};
    if (!key || !*key)
        return 0;
    for (int i = 0; not_ids[i]; ++i)
        if (streq_ci(key, not_ids[i]))
            return 0;
    for (int i = 0; exact[i]; ++i)
        if (streq_ci(key, exact[i]))
            return 1;
    // id0, id1, id2 ...
    if ((key[0] == 'i' || key[0] == 'I') && (key[1] == 'd' || key[1] == 'D') && isdigit((unsigned char)key[2]))
        return 1;
    if (endswith(key, "_id") || endswith(key, "_ID"))
        return 1;
    // CamelCase: networkID, EndpointID, MeterId ...
    size_t n = strlen(key);
    if (n > 2 && (endswith(key, "ID") || endswith(key, "Id")) && islower((unsigned char)key[n - 3]))
        return 1;
    if (strstr(key, "serial") || strstr(key, "Serial"))
        return 1;
    return 0;
}

int foxsdr_is_payload_key(char const *key)
{
    static char const *const payload[] = {
        "data", "payload", "raw", "raw_data", "raw_msg", "packet", "frame", "msg_data", "hex", "bits", "codes", "rows",
        NULL};
    if (!key)
        return 0;
    for (int i = 0; payload[i]; ++i)
        if (streq_ci(key, payload[i]))
            return 1;
    return 0;
}

void foxsdr_hash_id(char const *salt, char const *value, char out[16])
{
    // FNV-1a 64 over "salt \0 value", folded to 32 bits.
    uint64_t h = 1469598103934665603ULL;
    for (char const *p = salt; p && *p; ++p) {
        h ^= (uint8_t)*p;
        h *= 1099511628211ULL;
    }
    h ^= 0;
    h *= 1099511628211ULL;
    for (char const *p = value; p && *p; ++p) {
        h ^= (uint8_t)*p;
        h *= 1099511628211ULL;
    }
    // final avalanche so that short ids spread over all bits
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    snprintf(out, 16, "h%08x", (unsigned)(h & 0xffffffffu));
}

/* ---- the output ---------------------------------------------------------- */

typedef struct {
    struct data_output output;
    FILE *fp;
    int hide_ids;
    char salt[64];
    unsigned events;
    dbuf_t line; // the JSON line being built
    dbuf_t text; // the summary being built
    int depth;   // 0 outside any print_data
} foxsdr_output_t;

static void json_string(dbuf_t *b, char const *s)
{
    dbuf_putc(b, '"');
    unsigned char const *u = (unsigned char const *)s;
    while (*u) {
        unsigned char c = *u;
        if (c == '"' || c == '\\') {
            dbuf_putc(b, '\\');
            dbuf_putc(b, (char)c);
            u++;
        }
        else if (c == '\n') { dbuf_cat(b, "\\n"); u++; }
        else if (c == '\r') { dbuf_cat(b, "\\r"); u++; }
        else if (c == '\t') { dbuf_cat(b, "\\t"); u++; }
        else if (c < 0x20 || c == 0x7f) {
            dbuf_printf(b, "\\u%04x", c);
            u++;
        }
        else if (c < 0x80) {
            dbuf_putc(b, (char)c);
            u++;
        }
        else {
            // validate one UTF-8 sequence, else U+FFFD (keeps the line valid JSON)
            int n = (c >= 0xc2 && c <= 0xdf) ? 2 : (c >= 0xe0 && c <= 0xef) ? 3 : (c >= 0xf0 && c <= 0xf4) ? 4 : 0;
            int ok = n > 0;
            for (int i = 1; ok && i < n; ++i)
                if ((u[i] & 0xc0) != 0x80)
                    ok = 0;
            if (ok && n == 3) {
                unsigned cp = ((c & 0x0f) << 12) | ((u[1] & 0x3f) << 6) | (u[2] & 0x3f);
                if (cp < 0x800 || (cp >= 0xd800 && cp <= 0xdfff))
                    ok = 0;
            }
            if (ok && n == 4) {
                unsigned cp = ((c & 0x07) << 18) | ((u[1] & 0x3f) << 12) | ((u[2] & 0x3f) << 6) | (u[3] & 0x3f);
                if (cp < 0x10000 || cp > 0x10ffff)
                    ok = 0;
            }
            if (ok) {
                for (int i = 0; i < n; ++i)
                    dbuf_putc(b, (char)u[i]);
                u += n;
            }
            else {
                dbuf_cat(b, "\\ufffd");
                u++;
            }
        }
    }
    dbuf_putc(b, '"');
}

static void R_API_CALLCONV fx_print_string(data_output_t *output, char const *str, char const *format)
{
    (void)format;
    foxsdr_output_t *o = (foxsdr_output_t *)output;
    json_string(&o->line, str ? str : "");
}

static void R_API_CALLCONV fx_print_double(data_output_t *output, double v, char const *format)
{
    (void)format;
    foxsdr_output_t *o = (foxsdr_output_t *)output;
    if (isfinite(v))
        dbuf_printf(&o->line, "%.3f", v); // as upstream's JSON printer
    else
        dbuf_cat(&o->line, "null");
}

static void R_API_CALLCONV fx_print_int(data_output_t *output, int v, char const *format)
{
    (void)format;
    foxsdr_output_t *o = (foxsdr_output_t *)output;
    dbuf_printf(&o->line, "%d", v);
}

static void R_API_CALLCONV fx_print_array(data_output_t *output, data_array_t *array, char const *format)
{
    foxsdr_output_t *o = (foxsdr_output_t *)output;
    dbuf_putc(&o->line, '[');
    for (int c = 0; c < array->num_values; ++c) {
        if (c)
            dbuf_putc(&o->line, ',');
        print_array_value(output, array, format, c);
    }
    dbuf_putc(&o->line, ']');
}

/// Text of a scalar value, for hashing and for the summary. Returns 0 for non-scalars.
static int scalar_text(data_t const *d, char *buf, size_t size)
{
    switch (d->type) {
    case DATA_INT:
        snprintf(buf, size, "%d", d->value.v_int);
        return 1;
    case DATA_DOUBLE:
        if (isfinite(d->value.v_dbl))
            snprintf(buf, size, "%.6g", d->value.v_dbl);
        else
            snprintf(buf, size, "null");
        return 1;
    case DATA_STRING:
        snprintf(buf, size, "%s", d->value.v_ptr ? (char const *)d->value.v_ptr : "");
        return 1;
    default:
        return 0;
    }
}

static char const *renamed_key(char const *key, char *buf, size_t size)
{
    // the pipe protocol owns these top-level names
    if (!strcmp(key, "t") || !strcmp(key, "type") || !strcmp(key, "text") || !strcmp(key, "time")) {
        snprintf(buf, size, "rtl_%s", key);
        return buf;
    }
    return key;
}

static void R_API_CALLCONV fx_print_data(data_output_t *output, data_t *data, char const *format)
{
    (void)format;
    foxsdr_output_t *o = (foxsdr_output_t *)output;
    int top            = o->depth == 0;
    o->depth++;
    if (top) {
        // header of the line is written by fx_output_print()
    }
    else {
        dbuf_putc(&o->line, '{');
    }
    int first = !top;
    for (data_t *d = data; d; d = d->next) {
        if (!d->key)
            continue;
        if (top && !strcmp(d->key, "time"))
            continue; // becomes "t"
        if (top && o->hide_ids && foxsdr_is_payload_key(d->key))
            continue;
        char keybuf[96];
        char const *key = top ? renamed_key(d->key, keybuf, sizeof(keybuf)) : d->key;
        if (!first || top)
            dbuf_putc(&o->line, ',');
        first = 0;
        json_string(&o->line, key);
        dbuf_putc(&o->line, ':');
        char val[256];
        if (o->hide_ids && foxsdr_is_id_key(d->key) && scalar_text(d, val, sizeof(val))) {
            char h[16];
            foxsdr_hash_id(o->salt, val, h);
            json_string(&o->line, h);
        }
        else if (o->hide_ids && foxsdr_is_id_key(d->key)) {
            json_string(&o->line, "hidden"); // an identifier that is not a plain number or string
        }
        else {
            print_value(output, d->type, d->value, d->format);
        }
    }
    if (!top)
        dbuf_putc(&o->line, '}');
    o->depth--;
}

/// the radio measurements added by the meta report (see data_acquired_handler in r_api.c), not part of the summary
static int is_meta_key(char const *key)
{
    static char const *const meta[] = {"protocol", "mod", "freq", "freq1", "freq2", "rssi", "snr", "noise", NULL};
    for (int i = 0; meta[i]; ++i)
        if (!strcmp(key, meta[i]))
            return 1;
    return 0;
}

static void build_text(foxsdr_output_t *o, data_t const *data)
{
    o->text.len = 0;
    if (o->text.p)
        o->text.p[0] = '\0';
    // model first
    for (data_t const *d = data; d; d = d->next) {
        if (d->key && !strcmp(d->key, "model") && d->type == DATA_STRING && d->value.v_ptr) {
            dbuf_cat(&o->text, (char const *)d->value.v_ptr);
            break;
        }
    }
    for (data_t const *d = data; d; d = d->next) {
        if (!d->key || !strcmp(d->key, "model") || !strcmp(d->key, "time") || is_meta_key(d->key))
            continue;
        if (o->hide_ids && foxsdr_is_payload_key(d->key))
            continue;
        char val[256];
        if (!scalar_text(d, val, sizeof(val)))
            continue;
        if (o->hide_ids && foxsdr_is_id_key(d->key)) {
            char h[16];
            foxsdr_hash_id(o->salt, val, h);
            snprintf(val, sizeof(val), "%s", h);
        }
        if (strlen(val) > 48)
            strcpy(val + 45, "...");
        if (o->text.len)
            dbuf_putc(&o->text, ' ');
        dbuf_cat(&o->text, d->key);
        dbuf_putc(&o->text, '=');
        dbuf_cat(&o->text, val);
        if (o->text.len > 400)
            break;
    }
}

static void R_API_CALLCONV fx_output_print(data_output_t *output, data_t *data)
{
    foxsdr_output_t *o = (foxsdr_output_t *)output;
    if (!o->fp)
        return;
    // t: the decoder's "time" field is "@<seconds>s" in sample-count mode
    double t = 0.0;
    for (data_t const *d = data; d; d = d->next) {
        if (d->key && !strcmp(d->key, "time") && d->type == DATA_STRING && d->value.v_ptr
                && ((char const *)d->value.v_ptr)[0] == '@') {
            t = strtod((char const *)d->value.v_ptr + 1, NULL);
            break;
        }
    }
    o->line.len = 0;
    dbuf_printf(&o->line, "{\"t\":%.6f,\"type\":\"rtl433\"", t);
    o->depth = 0;
    fx_print_data(output, data, NULL);
    build_text(o, data);
    dbuf_cat(&o->line, ",\"text\":");
    json_string(&o->line, o->text.p ? o->text.p : "");
    dbuf_putc(&o->line, '}');
    dbuf_putc(&o->line, '\n');
    fwrite(o->line.p, 1, o->line.len, o->fp);
    fflush(o->fp);
    o->events++;
}

static void R_API_CALLCONV fx_output_free(data_output_t *output)
{
    foxsdr_output_t *o = (foxsdr_output_t *)output;
    if (!o)
        return;
    free(o->line.p);
    free(o->text.p);
    free(o);
}

data_output_t *foxsdr_output_create(FILE *fp, int hide_ids, char const *salt)
{
    foxsdr_output_t *o = calloc(1, sizeof(*o));
    if (!o) {
        fprintf(stderr, "rtl433 helper: out of memory\n");
        exit(1);
    }
    o->output.print_data   = fx_print_data;
    o->output.print_array  = fx_print_array;
    o->output.print_string = fx_print_string;
    o->output.print_double = fx_print_double;
    o->output.print_int    = fx_print_int;
    o->output.output_print = fx_output_print;
    o->output.output_free  = fx_output_free;
    o->output.log_level    = 0; // events only, never decoder log messages
    o->fp                  = fp;
    o->hide_ids            = hide_ids;
    snprintf(o->salt, sizeof(o->salt), "%s", salt ? salt : "");
    return &o->output;
}

unsigned foxsdr_output_events(data_output_t const *output)
{
    return output ? ((foxsdr_output_t const *)output)->events : 0;
}
