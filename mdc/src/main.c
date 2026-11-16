/*
 * mdc helper: MDC-1200 and Fleetsync I decoders behind the FoxSDR helper pipe protocol (version 1).
 *
 * Licence: GPL-2.0-only, the licence of the decoder libraries it is built around (see ../LICENSE, ../UPSTREAM.md).
 * The decoders are Matthew Kaufman's mdc_decode.c and fsync_decode.c (upstream/). This file is the command-line
 * front end: it reads audio on standard input and prints JSON Lines on standard output.
 *
 *   mdc.exe --foxsdr-helper 1 --format s16 --rate 16000 [--hide-ids] [--no-mdc] [--no-fleetsync] [--report-bad]
 *   mdc.exe --foxsdr-helper-info
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

#include "mdc_decode.h"
#include "fsync_decode.h"
#include "helper_manifest.h"

#define HELPER_NAME "mdc"
#define HELPER_VERSION "1.0.0"
#define HELPER_LICENCE "GPL-2.0-only"
#define HELPER_SOURCE "https://github.com/atmatthewat/mdc-encode-decode"
#define HELPER_UPSTREAM "mdc-encode-decode 45fc9095fbde (MDC-1200); fsync-mdc1200-decode 287356a99d4 (Fleetsync I)"

#define RATE_MIN 8000
#define RATE_MAX 96000
#define BAD_WINDOW_S 0.15 /* a CRC failure within this time of a good packet is the same transmission */

/* ---- id hiding: the same identifier always gets the same token, in order of first appearance ------------------ */
struct idmap {
    long *v;
    size_t n, cap;
    char prefix;
};

static void idmap_token(struct idmap *m, long id, char *out, size_t outsz)
{
    size_t i;
    for (i = 0; i < m->n; i++)
        if (m->v[i] == id)
            goto found;
    if (m->n >= 1000000) { /* bounded: beyond a million distinct ids every new one shares one token */
        snprintf(out, outsz, "%coverflow", m->prefix);
        return;
    }
    if (m->n == m->cap) {
        size_t nc = m->cap ? m->cap * 2 : 64;
        long *nv = (long *)realloc(m->v, nc * sizeof(long));
        if (!nv) {
            snprintf(out, outsz, "%coverflow", m->prefix);
            return;
        }
        m->v = nv;
        m->cap = nc;
    }
    m->v[m->n++] = id;
    i = m->n - 1;
found:
    snprintf(out, outsz, "%c%u", m->prefix, (unsigned)(i + 1));
}

struct app {
    unsigned rate;
    unsigned long long samples;
    int hide_ids;
    int report_bad;
    int want_mdc, want_fs;
    mdc_decoder_t *mdc;
    fsync_decoder_t *fs;
    struct idmap units, fleets;
    /* statistics */
    unsigned long n_mdc, n_mdc_double, n_mdc_ecc, n_mdc_badcrc, n_fs1, n_fs2_ignored;
    /* pending CRC-failure report (see BAD_WINDOW_S) */
    int bad_pending;
    double bad_time;
    int bad_frame;
    double last_good_time;
    int have_good;
    unsigned quiet_run;
};

static double now_s(const struct app *a) { return (double)a->samples / (double)a->rate; }

static void emit(const char *line)
{
    fputs(line, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

/* append s to buf as the inside of a JSON string; printable ASCII stays, everything else becomes \u00XX */
static size_t json_esc(char *buf, size_t pos, size_t cap, const unsigned char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned c = s[i];
        if (pos + 8 >= cap)
            break;
        if (c == '"' || c == '\\') {
            buf[pos++] = '\\';
            buf[pos++] = (char)c;
        } else if (c >= 0x20 && c < 0x7f) {
            buf[pos++] = (char)c;
        } else {
            pos += (size_t)snprintf(buf + pos, cap - pos, "\\u%04x", c);
        }
    }
    buf[pos] = 0;
    return pos;
}

/* ---- MDC-1200 ---------------------------------------------------------------------------------------------- */
static void flush_bad(struct app *a, int force)
{
    if (!a->bad_pending)
        return;
    if (!force && now_s(a) - a->bad_time < BAD_WINDOW_S)
        return;
    a->bad_pending = 0;
    a->n_mdc_badcrc++;
    if (a->report_bad) {
        char line[256];
        snprintf(line, sizeof line, "{\"t\":%.4f,\"type\":\"mdc1200_crc_fail\",\"frame\":%d,\"crc_ok\":false}", a->bad_time,
                 a->bad_frame);
        emit(line);
    }
}

static void mdc_badcrc_cb(int frame, void *ctx)
{
    struct app *a = (struct app *)ctx;
    double t = now_s(a);
    /* the five parallel bit-phase decoders each try the same block: only one of them is usually right */
    if (a->have_good && t - a->last_good_time < BAD_WINDOW_S)
        return;
    if (!a->bad_pending) {
        a->bad_pending = 1;
        a->bad_time = t;
        a->bad_frame = frame;
    }
}

static void unit_text(struct app *a, unsigned id, char *out, size_t sz, int quote)
{
    char tok[32];
    if (a->hide_ids) {
        idmap_token(&a->units, (long)id, tok, sizeof tok);
        snprintf(out, sz, "\"%s\"", tok);
    } else if (quote) {
        snprintf(out, sz, "\"%04X\"", id);
    } else {
        snprintf(out, sz, "%u", id);
    }
}

static void mdc_cb(int frames, unsigned char op, unsigned char arg, unsigned short unit, unsigned char e0, unsigned char e1,
                   unsigned char e2, unsigned char e3, void *ctx)
{
    struct app *a = (struct app *)ctx;
    char line[1024], u1[48], u2[48];
    int n;

    /* a good packet makes any CRC failure pending from the same transmission moot */
    if (a->bad_pending && fabs(a->bad_time - now_s(a)) < BAD_WINDOW_S)
        a->bad_pending = 0;
    a->have_good = 1;
    a->last_good_time = now_s(a);

    a->n_mdc++;
    if (frames == 2)
        a->n_mdc_double++;
    if (a->mdc->ecc_fixed > 0)
        a->n_mdc_ecc++;

    unit_text(a, unit, u1, sizeof u1, 1);
    n = snprintf(line, sizeof line, "{\"t\":%.4f,\"type\":\"mdc1200\",\"frames\":%d,\"op\":%u,\"op_hex\":\"%02X\"", now_s(a), frames,
                 op, op);
    n += snprintf(line + n, sizeof line - (size_t)n, ",\"arg\":%u,\"arg_hex\":\"%02X\"", arg, arg);
    n += snprintf(line + n, sizeof line - (size_t)n, ",\"unit\":%s", u1);
    if (frames == 2) {
        /* second block of a double packet: its own operation, argument and unit ID (e0, e1, e2:e3) */
        unsigned op2 = e0, arg2 = e1, unit2 = ((unsigned)e2 << 8) | e3;
        unit_text(a, unit2, u2, sizeof u2, 1);
        n += snprintf(line + n, sizeof line - (size_t)n, ",\"op2\":%u,\"op2_hex\":\"%02X\"", op2, op2);
        n += snprintf(line + n, sizeof line - (size_t)n, ",\"arg2\":%u,\"arg2_hex\":\"%02X\"", arg2, arg2);
        n += snprintf(line + n, sizeof line - (size_t)n, ",\"unit2\":%s", u2);
    }
    snprintf(line + n, sizeof line - (size_t)n, ",\"ecc_bits\":%d,\"crc_ok\":true}", (int)a->mdc->ecc_fixed);
    emit(line);
}

/* ---- Fleetsync I ------------------------------------------------------------------------------------------- */
static void fs_cb(int cmd, int subcmd, int from_fleet, int from_unit, int to_fleet, int to_unit, int allflag, unsigned char *payload,
                  int payload_len, unsigned char *raw, int raw_len, void *ctx, int is_fsync2, int is_2400)
{
    struct app *a = (struct app *)ctx;
    char line[4096];
    char ff[40], fu[40], tf[40], tu[40];
    size_t pos;
    int i;

    if (is_fsync2) { /* Fleetsync II is built elsewhere (trunkfmt); only counted here */
        a->n_fs2_ignored++;
        return;
    }
    a->n_fs1++;
    if (a->hide_ids) {
        char tok[32];
#define HIDE(dst, map, val)                                       \
    do {                                                          \
        if ((val) < 0) {                                          \
            snprintf(dst, sizeof dst, "-1");                      \
        } else {                                                  \
            idmap_token(&a->map, (long)(val), tok, sizeof tok);   \
            snprintf(dst, sizeof dst, "\"%s\"", tok);             \
        }                                                         \
    } while (0)
        HIDE(ff, fleets, from_fleet);
        HIDE(fu, units, from_unit);
        HIDE(tf, fleets, to_fleet);
        HIDE(tu, units, to_unit);
    } else {
        snprintf(ff, sizeof ff, "%d", from_fleet);
        snprintf(fu, sizeof fu, "%d", from_unit);
        snprintf(tf, sizeof tf, "%d", to_fleet);
        snprintf(tu, sizeof tu, "%d", to_unit);
    }
    pos = (size_t)snprintf(line, sizeof line,
                           "{\"t\":%.4f,\"type\":\"fleetsync1\",\"cmd\":%d,\"subcmd\":%d,\"from_fleet\":%s,\"from_unit\":%s,"
                           "\"to_fleet\":%s,\"to_unit\":%s,\"all_call\":%s,\"baud\":%d,\"raw\":\"",
                           now_s(a), cmd, subcmd, ff, fu, tf, tu, allflag ? "true" : "false", is_2400 ? 2400 : 1200);
    for (i = 0; i < raw_len && pos + 4 < sizeof line; i++)
        pos += (size_t)snprintf(line + pos, sizeof line - pos, "%02X", raw[i]);
    pos += (size_t)snprintf(line + pos, sizeof line - pos, "\"");
    if (payload_len > 0) {
        pos += (size_t)snprintf(line + pos, sizeof line - pos, ",\"payload\":\"");
        pos = json_esc(line, pos, sizeof line - 8, payload, (size_t)payload_len);
        pos += (size_t)snprintf(line + pos, sizeof line - pos, "\"");
    }
    snprintf(line + pos, sizeof line - pos, ",\"crc_ok\":true}");
    emit(line);
}

/* Fleetsync I only hands a message over once the next 64-bit word fails its CRC, and the upstream decoder only advances
 * on audio whose zero crossings come at 1200 / 1800 Hz spacing (any other count is skipped). After a message the audio may
 * go silent (a closed squelch gives digital zeros), so the decoder would sit on a finished message for ever. When that
 * happens, feed it 160 bits of fixed pseudo-random 1200/1800 Hz tones: the next word fails its CRC and the message is
 * delivered. The burst is not counted as input time. */
static unsigned long long noise_state = 0x2545F4914F6CDD1Dull;

static int noise_bit(void)
{
    noise_state ^= noise_state << 13;
    noise_state ^= noise_state >> 7;
    noise_state ^= noise_state << 17;
    return (int)((noise_state >> 33) & 1);
}

static void inject_noise(struct app *a)
{
    enum { NBITS = 160 };
    double base[NBITS + 1];
    int bits[NBITS], b;
    size_t k, n;
    const double two_pi = 6.283185307179586;
    if (a->fs->actives <= 0)
        return;
    base[0] = 0.0;
    for (b = 0; b < NBITS; b++) {
        bits[b] = noise_bit();
        base[b + 1] = base[b] + two_pi * (bits[b] ? 1200.0 : 1800.0) / 1200.0;
    }
    n = (size_t)((double)NBITS / 1200.0 * (double)a->rate);
    for (k = 0; k < n; k++) {
        double t = (double)k / (double)a->rate;
        int bit = (int)(t * 1200.0);
        double f, ph;
        fsync_sample_t v;
        if (bit >= NBITS)
            bit = NBITS - 1;
        f = bits[bit] ? 1200.0 : 1800.0;
        ph = base[bit] + two_pi * f * (t - (double)bit / 1200.0);
        v = (fsync_sample_t)(0.17 * sin(ph) * ((double)a->rate / 8000.0)); /* same scale as feed_fleetsync, about half full scale */
        fsync_decoder_process_samples(a->fs, &v, 1);
    }
}

#define QUIET_LEVEL 32 /* |sample| below this (of 32768) counts as silence */

static void feed_fleetsync(struct app *a, short s)
{
    /* upstream scaled U8 input to (s-128)/256; the same fraction of full scale for S16 is s/65536. The decoder's edge
     * detector looks at the change between neighbouring samples against a fixed threshold, which shrinks as the
     * sample rate rises, so the input is scaled by rate/8000 to give every rate the sensitivity upstream has at 8 kHz. */
    fsync_sample_t v = (fsync_sample_t)((double)s / 65536.0 * ((double)a->rate / 8000.0));
    fsync_decoder_process_samples(a->fs, &v, 1);
    if (s > -QUIET_LEVEL && s < QUIET_LEVEL) {
        if (++a->quiet_run == a->rate / 50) /* 20 ms of silence, once per run */
            inject_noise(a);
    } else {
        a->quiet_run = 0;
    }
}

/* ---- main -------------------------------------------------------------------------------------------------- */
static void usage(FILE *f)
{
    fputs("mdc helper " HELPER_VERSION " (GPL-2.0-only): MDC-1200 and Fleetsync I decoders, FoxSDR helper protocol 1\n"
          "  mdc --foxsdr-helper 1 --format s16|f32 --rate <8000..96000> [options] < audio\n"
          "  mdc --foxsdr-helper-info\n"
          "  input:  mono audio, raw samples on standard input until end of file (s16 little-endian or f32)\n"
          "  output: a hello line, then one JSON object per decoded packet; diagnostics on standard error\n"
          "  options:\n"
          "    --hide-ids      replace unit and fleet ids by tokens (same id, same token within one run)\n"
          "    --no-mdc        do not run the MDC-1200 decoder\n"
          "    --no-fleetsync  do not run the Fleetsync I decoder\n"
          "    --report-bad    also print mdc1200_crc_fail events (blocks that failed the CRC)\n"
          "    --centre <Hz>   accepted and ignored (audio input has no centre frequency)\n",
          f);
}

static int parse_uint(const char *s, unsigned *out)
{
    char *end;
    unsigned long v;
    if (!s || !*s)
        return 0;
    v = strtoul(s, &end, 10);
    if (*end || v > 0xFFFFFFFFul)
        return 0;
    *out = (unsigned)v;
    return 1;
}

static short f32_to_s16(float f)
{
    if (!(f == f)) /* NaN */
        return 0;
    if (f > 1.0f)
        f = 1.0f;
    if (f < -1.0f)
        f = -1.0f;
    return (short)lrintf(f * 32767.0f);
}

int main(int argc, char **argv)
{
    struct app A;
    int i, proto = 0, info = 0, fmt_f32 = 0, have_fmt = 0;
    unsigned rate = 0;
    unsigned char buf[8192];
    size_t have = 0;
    short s16buf[sizeof(buf) / 2 + 2];

#ifdef _WIN32
    /* never raise a Windows dialog: no critical-error box, no fault box, no file-open box */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    memset(&A, 0, sizeof A);
    A.want_mdc = A.want_fs = 1;
    A.units.prefix = 'u';
    A.fleets.prefix = 'f';

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--foxsdr-helper-info")) {
            info = 1;
        } else if (!strcmp(a, "--foxsdr-helper")) {
            unsigned v;
            if (i + 1 >= argc || !parse_uint(argv[++i], &v) || v != 1) {
                fprintf(stderr, "mdc: --foxsdr-helper needs protocol version 1\n");
                return 2;
            }
            proto = 1;
        } else if (!strcmp(a, "--format")) {
            const char *f = i + 1 < argc ? argv[++i] : "";
            if (!strcmp(f, "s16"))
                fmt_f32 = 0;
            else if (!strcmp(f, "f32"))
                fmt_f32 = 1;
            else {
                fprintf(stderr, "mdc: unsupported --format '%s': this helper takes mono audio, s16 or f32\n", f);
                return 2;
            }
            have_fmt = 1;
        } else if (!strcmp(a, "--rate")) {
            if (i + 1 >= argc || !parse_uint(argv[++i], &rate) || rate < RATE_MIN || rate > RATE_MAX) {
                fprintf(stderr, "mdc: --rate must be a whole number from %d to %d Hz\n", RATE_MIN, RATE_MAX);
                return 2;
            }
        } else if (!strcmp(a, "--centre") || !strcmp(a, "--center")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "mdc: %s needs a value\n", a);
                return 2;
            }
            i++;
        } else if (!strcmp(a, "--hide-ids")) {
            A.hide_ids = 1;
        } else if (!strcmp(a, "--no-mdc")) {
            A.want_mdc = 0;
        } else if (!strcmp(a, "--no-fleetsync")) {
            A.want_fs = 0;
        } else if (!strcmp(a, "--report-bad")) {
            A.report_bad = 1;
        } else if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "mdc: unknown argument '%s'\n", a);
            usage(stderr);
            return 2;
        }
    }
    if (info) {
        fputs(HELPER_MANIFEST_JSON, stdout);
        fflush(stdout);
        return 0;
    }
    if (!proto || !have_fmt || !rate) {
        fprintf(stderr, "mdc: --foxsdr-helper 1, --format and --rate are all required\n");
        usage(stderr);
        return 2;
    }
    if (!A.want_mdc && !A.want_fs) {
        fprintf(stderr, "mdc: no decoder selected\n");
        return 2;
    }
    A.rate = rate;

    if (A.want_mdc) {
        A.mdc = mdc_decoder_new((int)rate);
        if (!A.mdc || mdc_decoder_set_callback(A.mdc, mdc_cb, &A) || mdc_decoder_set_badcrc_callback(A.mdc, mdc_badcrc_cb)) {
            fprintf(stderr, "mdc: cannot create the MDC-1200 decoder\n");
            return 2;
        }
        A.mdc->callback_context = &A;
    }
    if (A.want_fs) {
        A.fs = fsync_decoder_new((int)rate);
        if (!A.fs || fsync_decoder_set_callback(A.fs, fs_cb, &A)) {
            fprintf(stderr, "mdc: cannot create the Fleetsync decoder\n");
            return 2;
        }
    }

    {
        char hello[768];
        snprintf(hello, sizeof hello,
                 "{\"helper\":\"" HELPER_NAME "\",\"version\":\"" HELPER_VERSION "\",\"protocol\":1,\"licence\":\"" HELPER_LICENCE
                 "\",\"source\":\"" HELPER_SOURCE "\",\"upstream\":\"" HELPER_UPSTREAM "\",\"format\":\"%s\",\"rate\":%u,"
                 "\"decoders\":[%s%s%s]%s}",
                 fmt_f32 ? "f32" : "s16", rate, A.want_mdc ? "\"mdc1200\"" : "", (A.want_mdc && A.want_fs) ? "," : "",
                 A.want_fs ? "\"fleetsync1\"" : "", A.hide_ids ? ",\"hide_ids\":true" : "");
        emit(hello);
    }

    for (;;) {
        size_t unit = fmt_f32 ? 4 : 2;
        size_t got = fread(buf + have, 1, sizeof buf - have, stdin);
        size_t total = have + got, nsamp = total / unit, k;
        if (got == 0 && have == 0)
            break;
        if (fmt_f32) {
            for (k = 0; k < nsamp; k++) {
                float f;
                memcpy(&f, buf + k * 4, 4);
                s16buf[k] = f32_to_s16(f);
            }
        } else {
            for (k = 0; k < nsamp; k++)
                s16buf[k] = (short)(buf[k * 2] | (buf[k * 2 + 1] << 8));
        }
        /* one sample at a time so every event carries the time of its own sample */
        for (k = 0; k < nsamp; k++) {
            A.samples++;
            if (A.mdc)
                mdc_decoder_process_samples(A.mdc, &s16buf[k], 1);
            if (A.fs)
                feed_fleetsync(&A, s16buf[k]);
            if (A.bad_pending)
                flush_bad(&A, 0);
        }
        have = total - nsamp * unit;
        if (have)
            memmove(buf, buf + nsamp * unit, have);
        if (got == 0)
            break; /* end of input with a cut-off sample left over */
    }
    if (have)
        fprintf(stderr, "mdc: %u stray byte(s) at the end of the input were ignored (cut-off sample)\n", (unsigned)have);
    if (A.fs) {
        inject_noise(&A);
        fsync_decoder_end_samples(A.fs);
    }
    flush_bad(&A, 1);

    {
        char st[512];
        snprintf(st, sizeof st,
                 "{\"t\":%.4f,\"type\":\"stats\",\"samples\":%llu,\"mdc1200\":%lu,\"mdc1200_double\":%lu,\"mdc1200_ecc_corrected\":%lu,"
                 "\"mdc1200_crc_fail\":%lu,\"fleetsync1\":%lu,\"fleetsync2_ignored\":%lu}",
                 now_s(&A), A.samples, A.n_mdc, A.n_mdc_double, A.n_mdc_ecc, A.n_mdc_badcrc, A.n_fs1, A.n_fs2_ignored);
        emit(st);
    }
    return 0;
}
