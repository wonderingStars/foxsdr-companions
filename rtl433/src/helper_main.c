/** @file
    FoxSDR "rtl433" GPL helper: command line, input loop and JSON Lines output.

    This file replaces upstream rtl_433's src/rtl_433.c (the program's main). It keeps the part of
    rtl_433.c's main() that reads samples from a file or standard input and feeds them to
    process_sdr_frame() / push_sdr_flow() (the "in files" test mode), and drops everything else:
    the SDR device code, the web server, MQTT / InfluxDB / syslog / UDP / rtl_tcp outputs, frequency
    hopping, the signal-file writers and the configuration-file reader.

    Based on rtl_433 src/rtl_433.c:
    Copyright (C) 2012 by Kyle Keen <keenerd@gmail.com>
    Copyright (C) 2014 by Hans-Frieder Vogt <hfvogt@gmx.net>
    Copyright (C) 2014 by Dave Pannu
    Copyright (C) 2014 by Christian Zuckschwerdt <zany@triq.net> and others (see AUTHORS)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#endif
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#include <bcrypt.h>
#else
#include <unistd.h>
#endif

#include "r_api.h"
#include "r_flow.h"
#include "r_util.h"
#include "rtl_433.h"
#include "r_private.h"
#include "r_device.h"
#include "rtl_433_devices.h"
#include "pulse_detect.h"
#include "fileformat.h"
#include "logger.h"
#include "data.h"
#include "list.h"
#include "output_foxsdr.h"
#include "helper_manifest.h"

#define HELPER_NAME "rtl433"
#define HELPER_VERSION "25.12.0"
#define HELPER_LICENCE "GPL-2.0-or-later"
#define HELPER_SOURCE "https://github.com/merbanan/rtl_433"
#define HELPER_UPSTREAM "rtl_433 25.12+0c85f7ad3566"

#define MIN_RATE 50000u
#define MAX_RATE 4000000u

typedef enum { FMT_CS16, FMT_CU8, FMT_CS8, FMT_CF32 } sample_format_t;

static r_cfg_t g_cfg;

static char const usage_str[] =
        "Usage: %s --foxsdr-helper 1 --format cs16|cu8|cs8|cf32 --rate <Hz> [--centre <Hz>] [options] < samples\n"
        "\n"
        "  Decodes short-range radio devices (weather stations, tyre-pressure sensors, remote controls,\n"
        "  door and alarm sensors, meters ...) from I/Q samples on standard input. rtl_433 decoders, no\n"
        "  radio drivers, no network code. Output: a hello line, then one JSON object per decoded event.\n"
        "\n"
        "  --foxsdr-helper 1     protocol version (required)\n"
        "  --format <fmt>        cs16 (interleaved I,Q int16 LE), cu8 (RTL-SDR native), cs8, cf32 (float); required\n"
        "  --rate <Hz>           sample rate, %u..%u (250000 is rtl_433's native rate)\n"
        "  --centre <Hz>         centre frequency of the recording, default 433920000 (reported, not tuned)\n"
        "  -R <n>                enable only protocol <n> (repeat); -R -<n> disables protocol <n>\n"
        "  -X <spec>             add a user-defined 'flex' decoder (see rtl_433's flex decoder help)\n"
        "  -C native|si|customary  unit conversion of the reported values\n"
        "  --no-meta             leave out modulation / frequency / RSSI / SNR / noise fields\n"
        "  --hide-ids            replace every identifier field by a short hash (hNNNNNNNN), unique per session,\n"
        "                        and drop raw payload fields (data, payload, raw ...)\n"
        "  --hide-ids-salt <s>   salt for --hide-ids hashes (default: random per run; fix it for repeatable output)\n"
        "  --list-protocols      print the built-in protocols and exit\n"
        "  -v                    more messages on standard error (repeat)\n"
        "  --foxsdr-helper-info  print the manifest JSON and exit\n"
        "  --version, --help\n";

/* ---------------------------------------------------------------------- */

static int arg_error(char const *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "rtl433: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    return 2;
}

static int need_arg(int argc, char **argv, int i)
{
    if (i + 1 >= argc) {
        arg_error("option %s needs a value", argv[i]);
        return 0;
    }
    return 1;
}

static int parse_uint(char const *s, unsigned long long *v)
{
    char *end = NULL;
    if (!s || !*s || *s == '-')
        return 0;
    errno = 0;
    unsigned long long x = strtoull(s, &end, 10);
    if (errno || !end || *end)
        return 0;
    *v = x;
    return 1;
}

static int session_salt(char *out, size_t size)
{
    unsigned char r[8] = {0};
#ifdef _WIN32
    if (BCryptGenRandom(NULL, r, sizeof(r), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        return 0;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(r, 1, sizeof(r), f) != sizeof(r)) {
        if (f)
            fclose(f);
        return 0;
    }
    fclose(f);
#endif
    snprintf(out, size, "%02x%02x%02x%02x%02x%02x%02x%02x", r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
    return 1;
}

static void json_hello(FILE *out, sample_format_t fmt, unsigned rate, unsigned centre, unsigned built,
        unsigned enabled, int hide_ids)
{
    static char const *const names[] = {"cs16", "cu8", "cs8", "cf32"};
    fprintf(out,
            "{\"helper\":\"%s\",\"version\":\"%s\",\"protocol\":1,\"licence\":\"%s\",\"source\":\"%s\","
            "\"upstream\":\"%s\",\"format\":\"%s\",\"rate\":%u,\"centre\":%u,"
            "\"protocols_built\":%u,\"protocols_enabled\":%u,\"hide_ids\":%s}\n",
            HELPER_NAME, HELPER_VERSION, HELPER_LICENCE, HELPER_SOURCE, HELPER_UPSTREAM, names[fmt], rate, centre,
            built, enabled, hide_ids ? "true" : "false");
    fflush(out);
}

static int is_removed(r_device const *d)
{
    return d->disabled >= 3 && d->decode_fn == NULL;
}

static unsigned count_built(r_cfg_t const *cfg)
{
    unsigned n = 0;
    for (unsigned i = 0; i < cfg->num_r_devices; ++i)
        if (!is_removed(&cfg->devices[i]))
            n++;
    return n;
}

static void list_protocols(r_cfg_t const *cfg)
{
    for (unsigned i = 0; i < cfg->num_r_devices; ++i) {
        r_device const *d = &cfg->devices[i];
        if (is_removed(d))
            printf("%u\t-\t%s\n", d->protocol_num, d->name);
        else
            printf("%u\t%s\t%s\n", d->protocol_num, d->disabled == 0 ? "default" : "off", d->name);
    }
}

/* ---------------------------------------------------------------------- */

typedef struct {
    int nums[512];
    int count;
} int_list_t;

int main(int argc, char **argv)
{
#ifdef _WIN32
    /* No fault box, no "insert disk" box, ever: a helper has no user to answer them. */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _putenv("TZ=UTC0"); /* nothing may depend on the development machine's time zone */
    _tzset();
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY); /* the manifest and the protocol list go out byte for byte */
    _setmode(_fileno(stderr), _O_BINARY);
#endif

    int protocol = 0, want_info = 0, want_version = 0, want_help = 0, want_list = 0;
    int have_format = 0, meta = 1, hide_ids = 0, verbosity = 0;
    sample_format_t fmt = FMT_CS16;
    unsigned long long rate = 0, centre = DEFAULT_FREQUENCY;
    char const *salt_arg = NULL;
    conversion_mode_t conversion = CONVERT_NATIVE;
    int_list_t enable = {{0}, 0}, disable = {{0}, 0};
    char *flex_specs[16];
    int n_flex = 0;
    int errflg = 0;

    for (int i = 1; i < argc && !errflg; ++i) {
        char const *a = argv[i];
        unsigned long long v;
        if (!strcmp(a, "--foxsdr-helper")) {
            if (!need_arg(argc, argv, i)) { errflg = 2; break; }
            if (!parse_uint(argv[++i], &v) || v != 1) {
                errflg = arg_error("unsupported protocol version \"%s\" (this helper speaks version 1)", argv[i]);
                break;
            }
            protocol = 1;
        }
        else if (!strcmp(a, "--foxsdr-helper-info")) {
            want_info = 1;
        }
        else if (!strcmp(a, "--version")) {
            want_version = 1;
        }
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            want_help = 1;
        }
        else if (!strcmp(a, "--list-protocols")) {
            want_list = 1;
        }
        else if (!strcmp(a, "--format")) {
            if (!need_arg(argc, argv, i)) { errflg = 2; break; }
            char const *f = argv[++i];
            if (!strcmp(f, "cs16")) fmt = FMT_CS16;
            else if (!strcmp(f, "cu8")) fmt = FMT_CU8;
            else if (!strcmp(f, "cs8")) fmt = FMT_CS8;
            else if (!strcmp(f, "cf32")) fmt = FMT_CF32;
            else {
                errflg = arg_error("unsupported --format \"%s\" (use cs16, cu8, cs8 or cf32; this helper takes I/Q, not audio)", f);
                break;
            }
            have_format = 1;
        }
        else if (!strcmp(a, "--rate")) {
            if (!need_arg(argc, argv, i)) { errflg = 2; break; }
            if (!parse_uint(argv[++i], &rate) || rate < MIN_RATE || rate > MAX_RATE) {
                errflg = arg_error("--rate \"%s\" must be a whole number of Hz in %u..%u", argv[i], MIN_RATE, MAX_RATE);
                break;
            }
        }
        else if (!strcmp(a, "--centre") || !strcmp(a, "--center")) {
            if (!need_arg(argc, argv, i)) { errflg = 2; break; }
            if (!parse_uint(argv[++i], &centre) || centre < 1000000ull || centre > 6000000000ull) {
                errflg = arg_error("--centre \"%s\" must be a whole number of Hz in 1000000..6000000000", argv[i]);
                break;
            }
        }
        else if (!strcmp(a, "-R")) {
            if (!need_arg(argc, argv, i)) { errflg = 2; break; }
            char const *s = argv[++i];
            int neg = (*s == '-');
            if (neg) s++;
            if (!parse_uint(s, &v) || v < 1 || v > 9999) {
                errflg = arg_error("-R \"%s\": not a protocol number (see --list-protocols)", argv[i]);
                break;
            }
            int_list_t *l = neg ? &disable : &enable;
            if (l->count < 512)
                l->nums[l->count++] = (int)v;
        }
        else if (!strcmp(a, "-X")) {
            if (!need_arg(argc, argv, i)) { errflg = 2; break; }
            if (n_flex >= 16) {
                errflg = arg_error("at most 16 -X decoders");
                break;
            }
            flex_specs[n_flex++] = argv[++i];
        }
        else if (!strcmp(a, "-C")) {
            if (!need_arg(argc, argv, i)) { errflg = 2; break; }
            char const *c = argv[++i];
            if (!strcmp(c, "native")) conversion = CONVERT_NATIVE;
            else if (!strcmp(c, "si")) conversion = CONVERT_SI;
            else if (!strcmp(c, "customary")) conversion = CONVERT_CUSTOMARY;
            else { errflg = arg_error("-C \"%s\": use native, si or customary", c); break; }
        }
        else if (!strcmp(a, "--no-meta")) {
            meta = 0;
        }
        else if (!strcmp(a, "--hide-ids")) {
            hide_ids = 1;
        }
        else if (!strcmp(a, "--hide-ids-salt")) {
            if (!need_arg(argc, argv, i)) { errflg = 2; break; }
            salt_arg = argv[++i];
        }
        else if (!strcmp(a, "-v")) {
            verbosity++;
        }
        else {
            errflg = arg_error("unknown option \"%s\" (try --help)", a);
        }
    }

    if (!errflg && want_version) {
        printf("%s %s (%s; protocol 1)\n", HELPER_NAME, HELPER_VERSION, HELPER_UPSTREAM);
        return 0;
    }
    if (!errflg && want_info) {
        fputs(HELPER_MANIFEST_JSON, stdout);
        return 0;
    }

    r_cfg_t *cfg = &g_cfg;
    r_init_cfg(cfg);

    if (!errflg && want_list) {
        list_protocols(cfg);
        return 0;
    }
    if (errflg) /* the message was printed where the problem was found */
        return errflg;
    if (want_help || !protocol) {
        if (!want_help)
            fprintf(stderr, "rtl433: --foxsdr-helper 1 is required (try --help)\n");
        fprintf(want_help ? stdout : stderr, usage_str, argc > 0 ? argv[0] : "rtl433-helper", MIN_RATE, MAX_RATE);
        return want_help ? 0 : 2;
    }
    if (!have_format) {
        fprintf(stderr, "rtl433: --format is required (cs16, cu8, cs8 or cf32)\n");
        return 2;
    }
    if (!rate)
        rate = DEFAULT_SAMPLE_RATE;

    if (hide_ids && !salt_arg) {
        static char salt_buf[32];
        if (!session_salt(salt_buf, sizeof(salt_buf))) {
            fprintf(stderr, "rtl433: cannot get random bytes for --hide-ids; give --hide-ids-salt\n");
            return 3;
        }
        salt_arg = salt_buf;
    }
    if (hide_ids && !*salt_arg) {
        fprintf(stderr, "rtl433: --hide-ids-salt must not be empty\n");
        return 2;
    }

    /* JSON goes out on a private copy of standard output; fd 1 itself is pointed at standard error so that a
       stray printf inside a decoder can never corrupt the JSON stream. */
    FILE *jout;
    fflush(stdout);
#ifdef _WIN32
    int jfd = _dup(1);
    if (jfd < 0 || _dup2(2, 1) < 0) {
        fprintf(stderr, "rtl433: cannot set up standard output\n");
        return 3;
    }
    _setmode(jfd, _O_BINARY);
    jout = _fdopen(jfd, "wb");
#else
    int jfd = dup(1);
    if (jfd < 0 || dup2(2, 1) < 0) {
        fprintf(stderr, "rtl433: cannot set up standard output\n");
        return 3;
    }
    jout = fdopen(jfd, "wb");
#endif
    if (!jout) {
        fprintf(stderr, "rtl433: cannot open standard output\n");
        return 3;
    }
    setvbuf(jout, NULL, _IOFBF, 1 << 16);
    setvbuf(stderr, NULL, _IONBF, 0);

    /* ---- configure the decoder pipeline (what rtl_433's main() does for an input file) ---- */
    struct dm_state *demod = cfg->demod;
    cfg->verbosity        = verbosity > 0 ? LOG_WARNING + verbosity : LOG_WARNING;
    cfg->samp_rate        = (uint32_t)rate;
    cfg->center_frequency = (uint32_t)centre;
    cfg->frequency[0]     = (uint32_t)centre;
    cfg->frequencies      = 1;
    cfg->report_time      = REPORT_TIME_SAMPLES; /* the "time" field is the sample position, becomes "t" */
    cfg->report_protocol  = 1;
    cfg->report_meta      = meta;
    cfg->conversion_mode  = conversion;

    pulse_detect_set_levels(demod->pulse_detect, demod->use_mag_est, demod->level_limit, demod->min_level,
            demod->min_snr, demod->detect_verbosity);

    data_output_t *out = foxsdr_output_create(jout, hide_ids, salt_arg ? salt_arg : "");
    list_push(&cfg->output_handler, out);
    r_redirect_logging(cfg);

    /* protocols: like rtl_433 -R: any positive -R selects only those, else all defaults minus the negatives */
    for (int k = 0; k < enable.count; ++k) {
        int n = enable.nums[k];
        if (n < 1 || (unsigned)n > cfg->num_r_devices) {
            fprintf(stderr, "rtl433: -R %d: unknown protocol (1..%u)\n", n, cfg->num_r_devices);
            return 2;
        }
        if (is_removed(&cfg->devices[n - 1])) {
            fprintf(stderr, "rtl433: -R %d: \"%s\"\n", n, cfg->devices[n - 1].name);
            return 2;
        }
    }
    for (int k = 0; k < disable.count; ++k) {
        int n = disable.nums[k];
        if (n < 1 || (unsigned)n > cfg->num_r_devices) {
            fprintf(stderr, "rtl433: -R -%d: unknown protocol (1..%u)\n", n, cfg->num_r_devices);
            return 2;
        }
    }
    if (enable.count) {
        for (int k = 0; k < enable.count; ++k) {
            int n = enable.nums[k];
            register_protocol(cfg, &cfg->devices[n - 1], NULL);
        }
    }
    else {
        for (unsigned i = 0; i < cfg->num_r_devices; ++i) {
            r_device *d = &cfg->devices[i];
            if (d->disabled > 0)
                continue;
            int off = 0;
            for (int k = 0; k < disable.count; ++k)
                if (disable.nums[k] == (int)d->protocol_num)
                    off = 1;
            if (!off)
                register_protocol(cfg, d, NULL);
        }
    }
    for (int k = 0; k < n_flex; ++k) {
        register_protocol(cfg, &flex_decoder, flex_specs[k]);
    }
    if (!demod->r_devs.len) {
        fprintf(stderr, "rtl433: no protocol selected\n");
        return 2;
    }
    if (verbosity > 0)
        fprintf(stderr, "rtl433: %zu protocols enabled of %u built\n", demod->r_devs.len, count_built(cfg));

    /* FM demodulation is needed when any enabled decoder is an FSK decoder */
    for (void **iter = demod->r_devs.elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;
        if (r_dev->modulation >= FSK_DEMOD_MIN_VAL && r_dev->modulation < LORA_DEMOD_MIN_VAL) {
            demod->enable_FM_demod = 1;
            break;
        }
    }

    json_hello(jout, fmt, (unsigned)rate, (unsigned)centre, count_built(cfg), (unsigned)demod->r_devs.len, hide_ids);

    /* ---- the input loop ---- */
    int const sample_bytes_in = (fmt == FMT_CS16) ? 4 : (fmt == FMT_CF32) ? 8 : 2;
    demod->load_info.format   = (fmt == FMT_CS16) ? CS16_IQ : (fmt == FMT_CF32) ? CF32_IQ : (fmt == FMT_CS8) ? CS8_IQ : CU8_IQ;
    demod->sample_size        = (fmt == FMT_CS16 || fmt == FMT_CF32) ? 4 : 2; /* after conversion: CU8 2, CS16 4 */
    demod->center_frequency   = cfg->center_frequency;
    demod->samp_rate          = cfg->samp_rate;
    demod->raw_handler        = &cfg->raw_handler;
    demod->fsk_pulse_detect_mode = cfg->fsk_pulse_detect_mode;
    demod->report_noise       = 0;
    demod->verbosity          = cfg->verbosity;
    demod->raw_mode           = 0;
    demod->grab_mode          = 0;

    unsigned char *iq_in  = malloc(DEFAULT_BUF_LENGTH * 2u);
    unsigned char *iq_out = malloc(DEFAULT_BUF_LENGTH);
    if (!iq_in || !iq_out) {
        fprintf(stderr, "rtl433: out of memory\n");
        return 3;
    }
    unsigned long long total_samples = 0, total_bytes_in = 0;
    int partial = 0;

    for (;;) {
        size_t n_read_in; /* bytes of iq_in read */
        size_t n_conv;    /* bytes of converted samples in iq_out */
        size_t want = (size_t)DEFAULT_BUF_LENGTH * (fmt == FMT_CF32 ? 2u : 1u);
        n_read_in = fread(iq_in, 1, want, stdin);
        if (n_read_in == 0)
            break;
        total_bytes_in += n_read_in;
        size_t whole = n_read_in - (n_read_in % (size_t)sample_bytes_in);
        if (whole != n_read_in)
            partial = (int)(n_read_in - whole);
        if (whole == 0)
            break;
        if (fmt == FMT_CF32) {
            float const *f = (float const *)iq_in; /* little-endian host; unaligned-safe use via memcpy */
            size_t n_float = whole / sizeof(float);
            int16_t *o = (int16_t *)iq_out;
            for (size_t n = 0; n < n_float; ++n) {
                float x;
                memcpy(&x, (char const *)f + n * sizeof(float), sizeof(float));
                int s_tmp = (int)(x * INT16_MAX); /* as rtl_433: clamp to [-1,1], scale to Q0.15 */
                if (!(x == x)) s_tmp = 0;
                if (s_tmp < -INT16_MAX) s_tmp = -INT16_MAX;
                else if (s_tmp > INT16_MAX) s_tmp = INT16_MAX;
                o[n] = (int16_t)s_tmp;
            }
            n_conv = n_float * sizeof(int16_t);
        }
        else if (fmt == FMT_CS8) {
            for (size_t n = 0; n < whole; ++n)
                iq_out[n] = (unsigned char)(((int8_t)iq_in[n]) + 128);
            n_conv = whole;
        }
        else {
            memcpy(iq_out, iq_in, whole);
            n_conv = whole;
        }
        total_samples += n_conv / (size_t)demod->sample_size;
        demod->sample_file_pos = (float)((double)total_samples / (double)rate);

        /* process_sdr_frame() of rtl_433.c, reduced: no byte limit, hopping, timers or stats printing */
        push_sdr_flow(cfg, iq_out, (uint32_t)n_conv);
        if (whole != n_read_in)
            break;
    }
    if (partial)
        fprintf(stderr, "rtl433: %d trailing byte(s) do not make a whole sample; ignored\n", partial);

    /* flush to make sure the last package is detected */
    flush_sdr_flow(cfg);

    /* final statistics line (counts only: deterministic) */
    {
        unsigned ook = demod->total_frames_ook, fsk = demod->total_frames_fsk, ev = demod->total_frames_events;
        fprintf(jout,
                "{\"t\":%.6f,\"type\":\"stats\",\"samples\":%llu,\"bytes\":%llu,\"frames\":%u,\"frames_ook\":%u,"
                "\"frames_fsk\":%u,\"frames_events\":%u,\"events\":%u,\"protocols_enabled\":%zu}\n",
                (double)total_samples / (double)rate, total_samples, total_bytes_in, demod->total_frames_count, ook, fsk,
                ev, foxsdr_output_events(out), demod->r_devs.len);
        fflush(jout);
    }

    reset_sdr_flow(cfg);
    free(iq_in);
    free(iq_out);
    r_free_cfg(cfg);
    fflush(jout);
    return 0;
}
