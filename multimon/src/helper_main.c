/*
 *      helper_main.c -- FoxSDR "multimon" GPL helper: command line, input loop and JSON Lines output
 *
 *      Derived from unixinput.c of multimon-ng 1.6.2 (input loop, demodulator table, option names, process_buffer).
 *      The audio device code, the sox/fork file reader, and the console banner are removed; input is standard
 *      input only; everything the demodulators print is turned into JSON Lines (see helper_events.c).
 *      Every change is listed in UPSTREAM.md.
 *
 *      Copyright (C) 1996
 *          Thomas Sailer (sailer@ife.ee.ethz.ch, hb9jnx@hb9w.che.eu)
 *
 *      Copyright (C) 2012-2026
 *          Elias Oenal    (multimon-ng@eliasoenal.com)
 *
 *      Copyright (C) 2024
 *          Jason Lingohr (jason@lucid.net.au)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "multimon.h"
#include "helper_events.h"
#include "helper_manifest.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

#define HELPER_NAME "multimon"
#define HELPER_VERSION "1.6.2"
#define HELPER_LICENCE "GPL-2.0-or-later"
#define HELPER_SOURCE "https://github.com/EliasOenal/multimon-ng"
#define HELPER_UPSTREAM "multimon-ng 1.6.2 b5f7d71c5e9b"
#define HELPER_RATE 22050u
#define READ_CHUNK 8192u /* samples per read: same as multimon-ng's file reader, fixed for determinism */
#define MAX_LINE (1u << 20)

/* ---------------------------------------------------------------------- */
/* The demodulators this helper carries. Left out on purpose: DUMPCSV (a debug dump), SCOPE and SDL_SCOPE
   (X11 / SDL windows). */

static const struct demod_param *dem[] = {
    &demod_poc5,    &demod_poc12,    &demod_poc24,     &demod_flex,      &demod_flex_next, &demod_gsc,
    &demod_eas,     &demod_ufsk1200, &demod_clipfsk,   &demod_fmsfsk,    &demod_afsk1200,  &demod_afsk2400,
    &demod_afsk2400_2, &demod_afsk2400_3, &demod_hapn4800, &demod_fsk9600, &demod_dtmf,     &demod_dualtone,
    &demod_zvei1,   &demod_zvei2,    &demod_zvei3,     &demod_dzvei,     &demod_pzvei,     &demod_eea,
    &demod_eia,     &demod_ccir,     &demod_morse,     &demod_x10,
};

#define NUMDEMOD (sizeof(dem) / sizeof(dem[0]))

static struct demod_state dem_st[NUMDEMOD];
static unsigned int dem_mask[(NUMDEMOD + 31) / 32];

#define MASK_SET(n) dem_mask[(n) >> 5] |= 1u << ((n)&0x1f)
#define MASK_RESET(n) dem_mask[(n) >> 5] &= ~(1u << ((n)&0x1f))
#define MASK_ISSET(n) (dem_mask[(n) >> 5] & 1u << ((n)&0x1f))

/* ---------------------------------------------------------------------- */
/* globals that multimon-ng's demodulators expect from unixinput.c */

int json_mode = 0; /* always 0: the helper builds its own JSON from the printed text */
static int verbose_level = 0;
static int integer_only = 1;

extern int pocsag_mode;
extern int pocsag_error_correction;
extern int pocsag_show_partial_decodes;
extern int pocsag_heuristic_pruning;
extern int pocsag_prune_empty;
extern int pocsag_polarity;
extern bool pocsag_init_charset(char *charset);
extern int aprs_mode;
extern int cw_dit_length;
extern int cw_gap_length;
extern int cw_threshold;
extern bool cw_disable_auto_threshold;
extern bool cw_disable_auto_timing;
extern int flex_disable_timestamp;
extern bool fms_justhex;

void addJsonTimestamp(cJSON *json_output) { (void)json_output; }
void printJson(cJSON *json_output) { (void)json_output; }
void print_json(int argc, char **argv)
{
    (void)argc;
    (void)argv;
}

/* ---------------------------------------------------------------------- */
/* time: samples read so far, and the virtual clock used in place of time(NULL) by the FLEX demodulators */

static unsigned long long g_samples = 0;
static unsigned int g_chunk = READ_CHUNK; /* samples per read; --chunk lowers it for finer event times */
static long long g_start_time = 0;

time_t hm_time(time_t *t)
{
    time_t v = (time_t)(g_start_time + (long long)(g_samples / HELPER_RATE));
    if (t)
        *t = v;
    return v;
}

/* ---------------------------------------------------------------------- */
/* output */

static FILE *jout = NULL;
static unsigned long long g_events = 0;

static void out_line(const char *s, size_t n)
{
    if (fwrite(s, 1, n, jout) != n || fputc('\n', jout) == EOF || fflush(jout) != 0) {
        fprintf(stderr, "multimon: cannot write to standard output; stopping\n");
        exit(3);
    }
}

/* Per-demodulator line assembly. Demodulators print with verbprintf(0, ...) in pieces; a piece ending in
   '\n' completes a line. Complete lines are grouped into events (hev_starts_event decides where a new event
   begins) and one group is written when the demodulator's call returns. */
struct line_state {
    char *part;       /* text of the unfinished line */
    size_t part_len;
    size_t part_cap;
    int part_overflow;
    char *group;      /* completed lines of the event being built, joined by '\n' */
    size_t group_len;
    size_t group_cap;
    int have_group;
};

static struct line_state ls[NUMDEMOD];
static int cur = 0; /* index of the demodulator that is running */

static void buf_append(char **p, size_t *len, size_t *cap, const char *s, size_t n)
{
    if (*len + n + 1 > *cap) {
        size_t ncap = *cap ? *cap : 256;
        while (ncap < *len + n + 1)
            ncap *= 2;
        char *np = (char *)realloc(*p, ncap);
        if (!np) {
            fprintf(stderr, "multimon: out of memory\n");
            exit(3);
        }
        *p = np;
        *cap = ncap;
    }
    memcpy(*p + *len, s, n);
    *len += n;
    (*p)[*len] = '\0';
}

static void emit_group(int idx)
{
    struct line_state *l = &ls[idx];
    if (!l->have_group)
        return;
    if (l->group_len == 0) { /* an empty line is not a message */
        l->have_group = 0;
        return;
    }
    struct hev_sb sb;
    hev_sb_init(&sb);
    hev_build(&sb, (double)g_samples / HELPER_RATE, dem[idx]->name, l->group ? l->group : "");
    if (sb.oom || !sb.p) {
        fprintf(stderr, "multimon: out of memory\n");
        exit(3);
    }
    out_line(sb.p, sb.n);
    hev_sb_free(&sb);
    g_events++;
    l->group_len = 0;
    if (l->group)
        l->group[0] = '\0';
    l->have_group = 0;
}

static void complete_line(int idx)
{
    struct line_state *l = &ls[idx];
    const char *line = l->part ? l->part : "";
    if (l->have_group && !hev_starts_event(dem[idx]->name, line)) {
        buf_append(&l->group, &l->group_len, &l->group_cap, "\n", 1);
        buf_append(&l->group, &l->group_len, &l->group_cap, line, l->part_len);
    } else {
        emit_group(idx);
        l->group_len = 0;
        buf_append(&l->group, &l->group_len, &l->group_cap, line, l->part_len);
        l->have_group = 1;
    }
    l->part_len = 0;
    if (l->part)
        l->part[0] = '\0';
    l->part_overflow = 0;
}

/* MORSE_CW never ends a line while it is decoding: it prints characters and spaces. One event per word. */
static void morse_words(int idx)
{
    struct line_state *l = &ls[idx];
    while (l->part_len) {
        char *sp = (char *)memchr(l->part, ' ', l->part_len);
        if (!sp)
            return;
        size_t wl = (size_t)(sp - l->part);
        if (wl) {
            l->have_group = 0;
            l->group_len = 0;
            buf_append(&l->group, &l->group_len, &l->group_cap, l->part, wl);
            l->have_group = 1;
            emit_group(idx);
        }
        memmove(l->part, sp + 1, l->part_len - wl - 1);
        l->part_len -= wl + 1;
        l->part[l->part_len] = '\0';
    }
}

static void flush_partial(int idx)
{
    struct line_state *l = &ls[idx];
    if (l->part_len)
        complete_line(idx);
    emit_group(idx);
}

void _verbprintf(int verb_level, const char *fmt, ...)
{
    va_list args, args2;
    va_start(args, fmt);
    va_copy(args2, args);
    int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (n <= 0) {
        va_end(args2);
        return;
    }
    char *tmp = (char *)malloc((size_t)n + 1);
    if (!tmp) {
        va_end(args2);
        return;
    }
    vsnprintf(tmp, (size_t)n + 1, fmt, args2);
    va_end(args2);

    if (verb_level > 0) {
        /* diagnostics, not decoded output: stderr, only when -v asks for that level */
        if (verb_level <= verbose_level)
            fwrite(tmp, 1, (size_t)n, stderr);
        free(tmp);
        return;
    }

    struct line_state *l = &ls[cur];
    const char *s = tmp;
    const char *end = tmp + n;
    int is_morse = !strcmp(dem[cur]->name, "MORSE_CW");
    while (s < end) {
        const char *nl = (const char *)memchr(s, '\n', (size_t)(end - s));
        size_t chunk = nl ? (size_t)(nl - s) : (size_t)(end - s);
        if (l->part_len + chunk > MAX_LINE) {
            if (!l->part_overflow)
                fprintf(stderr, "multimon: %s: line longer than %u bytes cut\n", dem[cur]->name, MAX_LINE);
            l->part_overflow = 1;
            chunk = l->part_len < MAX_LINE ? MAX_LINE - l->part_len : 0;
        }
        if (chunk)
            buf_append(&l->part, &l->part_len, &l->part_cap, s, chunk);
        if (nl) {
            complete_line(cur);
            s = nl + 1;
        } else {
            s = end;
        }
    }
    if (is_morse)
        morse_words(cur);
    free(tmp);
}

/* ---------------------------------------------------------------------- */

static void process_buffer(float *float_buf, short *short_buf, unsigned int len)
{
    for (int i = 0; (unsigned int)i < NUMDEMOD; i++)
        if (MASK_ISSET(i) && dem[i]->demod) {
            buffer_t buffer = {short_buf, float_buf};
            cur = i;
            dem[i]->demod(dem_st + i, buffer, (int)len);
            emit_group(i);
        }
}

static void quit_all(void)
{
    for (int i = 0; (unsigned int)i < NUMDEMOD; i++) {
        if (MASK_ISSET(i)) {
            cur = i;
            if (dem[i]->deinit)
                dem[i]->deinit(dem_st + i);
            flush_partial(i);
        }
    }
}

/* ---------------------------------------------------------------------- */

enum sample_format { FMT_S16, FMT_F32 };

/* Read exactly `want` bytes unless end of file. Returns bytes read. */
static size_t read_full(void *buf, size_t want)
{
    size_t got = 0;
    while (got < want) {
        size_t r = fread((char *)buf + got, 1, want - got, stdin);
        if (r == 0)
            break;
        got += r;
    }
    return got;
}

static void input_stdin(unsigned int overlap, enum sample_format fmt)
{
    short buffer[READ_CHUNK];
    float fbuf[READ_CHUNK * 2 + 64];
    float fin[READ_CHUNK];
    unsigned int fbuf_cnt = 0;

    for (;;) {
        unsigned int nsamp;
        if (fmt == FMT_S16) {
            size_t got = read_full(buffer, g_chunk * sizeof(short));
            nsamp = (unsigned int)(got / sizeof(short));
            if (got % sizeof(short))
                fprintf(stderr, "multimon: warning: input ends in the middle of a sample; last byte ignored\n");
            if (nsamp == 0)
                break;
        } else {
            size_t got = read_full(fin, g_chunk * sizeof(float));
            nsamp = (unsigned int)(got / sizeof(float));
            if (got % sizeof(float))
                fprintf(stderr, "multimon: warning: input ends in the middle of a sample; last bytes ignored\n");
            if (nsamp == 0)
                break;
            for (unsigned int k = 0; k < nsamp; k++) {
                float v = fin[k];
                if (!(v == v)) /* NaN */
                    v = 0.0f;
                if (v > 1.0f)
                    v = 1.0f;
                if (v < -1.0f)
                    v = -1.0f;
                buffer[k] = (short)lrintf(v * 32767.0f);
            }
        }

        g_samples += nsamp;
        if (integer_only) {
            fbuf_cnt = nsamp;
        } else {
            for (unsigned int k = 0; k < nsamp; k++)
                fbuf[fbuf_cnt++] = buffer[k] * (1.0f / 32768.0f);
        }
        if (fbuf_cnt > overlap) {
            process_buffer(fbuf, buffer, fbuf_cnt - overlap);
            memmove(fbuf, fbuf + fbuf_cnt - overlap, overlap * sizeof(fbuf[0]));
            fbuf_cnt = overlap;
        }
    }
}

/* ---------------------------------------------------------------------- */

static const char usage_str[] =
    "multimon-helper: multimon-ng decoders as a FoxSDR GPL helper (GPL-2.0-or-later)\n"
    "\n"
    "Usage: %s --foxsdr-helper 1 --format s16|f32 --rate 22050 [options] < samples\n"
    "  Reads mono audio samples on standard input until end of file and writes JSON Lines on standard\n"
    "  output: first a hello line, then one object per decoded message. Diagnostics go to standard error.\n"
    "  --foxsdr-helper 1   protocol version (required)\n"
    "  --format s16|f32    s16 = little-endian 16-bit, f32 = little-endian float in -1..1 (default s16)\n"
    "  --rate 22050        sample rate in Hz; only 22050 is accepted (resample before piping in)\n"
    "  --centre <Hz>       accepted and ignored (audio helper)\n"
    "  --foxsdr-helper-info  print the manifest JSON and exit\n"
    "  --version           print the version and exit\n"
    "  -a <demod>          run only this demodulator (repeat for several); --demod is the same\n"
    "  -s <demod>          run all demodulators except this one (repeat)\n"
    "  -c                  clear the demodulator list (then add with -a)\n"
    "  -v <level>          diagnostics to standard error (multimon-ng verbosity levels)\n"
    "  -A                  APRS mode (AFSK1200 only, \"APRS: ...\" lines)\n"
    "  -j                  FMS: hex and CRC only, no parsing\n"
    "  -e                  POCSAG: hide empty messages\n"
    "  -u                  POCSAG: heuristically prune unlikely decodes\n"
    "  -p                  POCSAG: show partially received messages\n"
    "  -P <auto|normal|inverted>  POCSAG: polarity\n"
    "  -f <numeric|alpha|skyper|auto>  POCSAG: force the message format (skyper = ROT-1 text)\n"
    "  -b <0|1|2>          POCSAG: BCH correction level (default 2)\n"
    "  -C <US|FR|DE|DK|SE|SI>  POCSAG: character set\n"
    "  -o <n> -d <ms> -g <ms>  CW: threshold, dit length, gap length\n"
    "  -x -y               CW: disable auto threshold / auto timing\n"
    "  --chunk <samples>   samples per read, 64..8192 (default 8192). Smaller gives finer event times t; the\n"
    "                      decoded text should not change (the tests check this)\n"
    "  --start-time <unix seconds>  start of the virtual clock the FLEX decoders stamp lines with (default 0;\n"
    "                      also switches FLEX time stamps on). The clock then follows the sample count.\n"
    "Demodulators:";

static int find_demod(const char *name)
{
    for (unsigned int i = 0; i < NUMDEMOD; i++)
        if (!strcasecmp(name, dem[i]->name))
            return (int)i;
    return -1;
}

static int need_arg(int argc, char **argv, int i)
{
    if (i + 1 >= argc) {
        fprintf(stderr, "multimon: option %s needs a value\n", argv[i]);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    /* No fault box, no "insert disk" box, ever: a helper has no user to answer them. */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _putenv("TZ=UTC0"); /* localtime() in the FLEX decoders must not depend on the development machine's zone */
    _tzset();
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY); /* the manifest printed by --foxsdr-helper-info and any --help text must not get CRLF from text mode */
    _setmode(_fileno(stderr), _O_BINARY);
#endif

    int protocol = 0;
    int want_info = 0, want_version = 0, want_help = 0;
    enum sample_format fmt = FMT_S16;
    long rate = (long)HELPER_RATE;
    int mask_first = 1;
    int errflg = 0;
    int start_time_given = 0;
    int pocsag_mode_set = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--foxsdr-helper")) {
            if (!need_arg(argc, argv, i)) { errflg++; break; }
            protocol = atoi(argv[++i]);
            if (protocol != 1) {
                fprintf(stderr, "multimon: unsupported protocol version %s (this helper speaks 1)\n", argv[i]);
                return 2;
            }
        } else if (!strcmp(a, "--foxsdr-helper-info")) {
            want_info = 1;
        } else if (!strcmp(a, "--version")) {
            want_version = 1;
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            want_help = 1;
        } else if (!strcmp(a, "--format")) {
            if (!need_arg(argc, argv, i)) { errflg++; break; }
            const char *f = argv[++i];
            if (!strcmp(f, "s16"))
                fmt = FMT_S16;
            else if (!strcmp(f, "f32"))
                fmt = FMT_F32;
            else {
                fprintf(stderr,
                        "multimon: unsupported --format \"%s\": this is an audio helper, use s16 or f32 "
                        "(cs16 / cf32 are I/Q formats)\n",
                        f);
                return 2;
            }
        } else if (!strcmp(a, "--rate")) {
            if (!need_arg(argc, argv, i)) { errflg++; break; }
            char *e;
            rate = strtol(argv[++i], &e, 10);
            if (*e || rate != (long)HELPER_RATE) {
                fprintf(stderr,
                        "multimon: unsupported --rate \"%s\": the multimon-ng decoders take 22050 Hz only, "
                        "resample before piping the samples in\n",
                        argv[i]);
                return 2;
            }
        } else if (!strcmp(a, "--centre") || !strcmp(a, "--center")) {
            if (!need_arg(argc, argv, i)) { errflg++; break; }
            i++; /* ignored */
        } else if (!strcmp(a, "--chunk")) {
            if (!need_arg(argc, argv, i)) { errflg++; break; }
            long c = strtol(argv[++i], NULL, 10);
            if (c < 64 || c > (long)READ_CHUNK) {
                fprintf(stderr, "multimon: --chunk must be 64..%u samples\n", READ_CHUNK);
                errflg++;
            } else {
                g_chunk = (unsigned int)c;
            }
        } else if (!strcmp(a, "--start-time")) {
            if (!need_arg(argc, argv, i)) { errflg++; break; }
            g_start_time = strtoll(argv[++i], NULL, 10);
            start_time_given = 1;
            hev_clock_set = 1;
        } else if (!strcmp(a, "-a") || !strcmp(a, "--demod")) {
            if (!need_arg(argc, argv, i)) { errflg++; break; }
            const char *n = argv[++i];
            if (mask_first)
                memset(dem_mask, 0, sizeof(dem_mask));
            mask_first = 0;
            int d = find_demod(n);
            if (d < 0) {
                fprintf(stderr, "multimon: invalid mode \"%s\"\n", n);
                errflg++;
            } else {
                MASK_SET(d);
            }
        } else if (!strcmp(a, "-s")) {
            if (!need_arg(argc, argv, i)) { errflg++; break; }
            const char *n = argv[++i];
            if (mask_first)
                memset(dem_mask, 0xff, sizeof(dem_mask));
            mask_first = 0;
            int d = find_demod(n);
            if (d < 0) {
                fprintf(stderr, "multimon: invalid mode \"%s\"\n", n);
                errflg++;
            } else {
                MASK_RESET(d);
            }
        } else if (!strcmp(a, "-c")) {
            if (mask_first)
                memset(dem_mask, 0xff, sizeof(dem_mask));
            mask_first = 0;
            for (unsigned int k = 0; k < NUMDEMOD; k++)
                MASK_RESET(k);
        } else if (!strcmp(a, "-v")) {
            if (!need_arg(argc, argv, i)) { errflg++; break; }
            verbose_level = (int)strtoul(argv[++i], 0, 0);
        } else if (!strcmp(a, "-A")) {
            aprs_mode = 1;
            memset(dem_mask, 0, sizeof(dem_mask));
            mask_first = 0;
            MASK_SET(find_demod("AFSK1200"));
        } else if (!strcmp(a, "-j")) {
            fms_justhex = true;
        } else if (!strcmp(a, "-e")) {
            pocsag_prune_empty = 1;
        } else if (!strcmp(a, "-u")) {
            pocsag_heuristic_pruning = 1;
        } else if (!strcmp(a, "-p")) {
            pocsag_show_partial_decodes = 1;
        } else if (!strcmp(a, "-P") || !strcmp(a, "--pocsag-polarity")) {
            if (!need_arg(argc, argv, i)) { errflg++; break; }
            const char *v = argv[++i];
            if (!strcmp(v, "auto") || !strcmp(v, "0"))
                pocsag_polarity = 0;
            else if (!strcmp(v, "normal") || !strcmp(v, "1"))
                pocsag_polarity = 1;
            else if (!strcmp(v, "inverted") || !strcmp(v, "2"))
                pocsag_polarity = 2;
            else {
                fprintf(stderr, "multimon: invalid POCSAG polarity: %s (use auto/normal/inverted)\n", v);
                errflg++;
            }
        } else if (!strcmp(a, "-f")) {
            if (!need_arg(argc, argv, i)) { errflg++; break; }
            const char *v = argv[++i];
            if (pocsag_mode_set) {
                fprintf(stderr, "multimon: a POCSAG mode has already been selected!\n");
            } else if (!strcmp(v, "numeric")) {
                pocsag_mode = POCSAG_MODE_NUMERIC;
                pocsag_mode_set = 1;
            } else if (!strcmp(v, "alpha")) {
                pocsag_mode = POCSAG_MODE_ALPHA;
                pocsag_mode_set = 1;
            } else if (!strcmp(v, "skyper")) {
                pocsag_mode = POCSAG_MODE_SKYPER;
                pocsag_mode_set = 1;
            } else if (!strcmp(v, "auto")) {
                pocsag_mode = POCSAG_MODE_AUTO;
                pocsag_mode_set = 1;
            } else {
                fprintf(stderr, "multimon: invalid POCSAG mode \"%s\" (numeric, alpha, skyper or auto)\n", v);
                errflg++;
            }
        } else if (!strcmp(a, "-b")) {
            if (!need_arg(argc, argv, i)) { errflg++; break; }
            pocsag_error_correction = (int)strtoul(argv[++i], 0, 0);
            if (pocsag_error_correction > 2 || pocsag_error_correction < 0) {
                fprintf(stderr, "multimon: invalid error correction value!\n");
                pocsag_error_correction = 2;
            }
        } else if (!strcmp(a, "-C") || !strcmp(a, "--charset")) {
            if (!need_arg(argc, argv, i)) { errflg++; break; }
            if (!pocsag_init_charset(argv[++i]))
                errflg++;
        } else if (!strcmp(a, "-o")) {
            if (!need_arg(argc, argv, i)) { errflg++; break; }
            int v = atoi(argv[++i]);
            if (v)
                cw_threshold = abs(v);
        } else if (!strcmp(a, "-d")) {
            if (!need_arg(argc, argv, i)) { errflg++; break; }
            int v = atoi(argv[++i]);
            if (v)
                cw_dit_length = abs(v);
        } else if (!strcmp(a, "-g")) {
            if (!need_arg(argc, argv, i)) { errflg++; break; }
            int v = atoi(argv[++i]);
            if (v)
                cw_gap_length = abs(v);
        } else if (!strcmp(a, "-x")) {
            cw_disable_auto_threshold = true;
        } else if (!strcmp(a, "-y")) {
            cw_disable_auto_timing = true;
        } else {
            fprintf(stderr, "multimon: unknown option \"%s\"\n", a);
            errflg++;
        }
    }

    if (want_info) {
        fputs(HELPER_MANIFEST_JSON, stdout);
        fflush(stdout);
        return 0;
    }
    if (want_version) {
        printf("%s %s (%s), %s\n", HELPER_NAME, HELPER_VERSION, HELPER_UPSTREAM, HELPER_LICENCE);
        return 0;
    }
    if (want_help || errflg || protocol != 1) {
        if (!want_help && !errflg)
            fprintf(stderr, "multimon: --foxsdr-helper 1 is required\n\n");
        fprintf(want_help ? stdout : stderr, usage_str, argc > 0 ? argv[0] : "multimon-helper");
        for (unsigned int k = 0; k < NUMDEMOD; k++)
            fprintf(want_help ? stdout : stderr, " %s", dem[k]->name);
        fprintf(want_help ? stdout : stderr, "\n");
        return want_help && !errflg ? 0 : 2;
    }

    if (mask_first)
        memset(dem_mask, 0xff, sizeof(dem_mask));
    /* the dem_mask has bits beyond NUMDEMOD set by 0xff: harmless, only indices below NUMDEMOD are tested */

    if (!start_time_given)
        flex_disable_timestamp = 1;

    /* JSON goes out on a private copy of standard output; fd 1 itself is pointed at standard error so that a
       stray printf inside a demodulator can never corrupt the JSON stream. */
#ifdef _WIN32
    fflush(stdout);
    int jfd = _dup(1);
    if (jfd < 0 || _dup2(2, 1) < 0) {
        fprintf(stderr, "multimon: cannot set up standard output\n");
        return 3;
    }
    _setmode(jfd, _O_BINARY);
    jout = _fdopen(jfd, "wb");
#else
    fflush(stdout);
    int jfd = dup(1);
    if (jfd < 0 || dup2(2, 1) < 0) {
        fprintf(stderr, "multimon: cannot set up standard output\n");
        return 3;
    }
    jout = fdopen(jfd, "wb");
#endif
    if (!jout) {
        fprintf(stderr, "multimon: cannot open standard output\n");
        return 3;
    }
    setvbuf(jout, NULL, _IOFBF, 1 << 16);

    unsigned int overlap = 0;
    int enabled = 0;
    for (unsigned int i = 0; i < NUMDEMOD; i++) {
        if (MASK_ISSET(i)) {
            enabled++;
            if (dem[i]->float_samples)
                integer_only = 0;
            memset(dem_st + i, 0, sizeof(dem_st[i]));
            dem_st[i].dem_par = dem[i];
            cur = (int)i;
            if (dem[i]->init)
                dem[i]->init(dem_st + i);
            if (dem[i]->samplerate != HELPER_RATE) {
                fprintf(stderr, "multimon: internal error: demodulator \"%s\" needs %u Hz\n", dem[i]->name,
                        dem[i]->samplerate);
                return 3;
            }
            if (dem[i]->overlap > overlap)
                overlap = dem[i]->overlap;
        }
    }
    if (!enabled) {
        fprintf(stderr, "multimon: no demodulator selected\n");
        return 2;
    }

    /* hello */
    {
        struct hev_sb sb;
        hev_sb_init(&sb);
        hev_sb_cat(&sb, "{\"helper\":\"" HELPER_NAME "\",\"version\":\"" HELPER_VERSION "\",\"protocol\":1,"
                        "\"licence\":\"" HELPER_LICENCE "\",\"source\":\"" HELPER_SOURCE "\","
                        "\"upstream\":\"" HELPER_UPSTREAM "\",\"format\":\"");
        hev_sb_cat(&sb, fmt == FMT_S16 ? "s16" : "f32");
        hev_sb_fmt(&sb, "\",\"rate\":%u,\"demods\":[", HELPER_RATE);
        int first = 1;
        for (unsigned int i = 0; i < NUMDEMOD; i++) {
            if (!MASK_ISSET(i))
                continue;
            if (!first)
                hev_sb_cat(&sb, ",");
            first = 0;
            hev_sb_json_str(&sb, dem[i]->name, strlen(dem[i]->name));
        }
        hev_sb_cat(&sb, "]}");
        out_line(sb.p, sb.n);
        hev_sb_free(&sb);
    }

    input_stdin(overlap, fmt);
    quit_all();
    fflush(jout);
    return 0;
}
