/*
 * helper_main.c -- FoxSDR "tetrapol" GPL helper: command line, input loop and hello line
 *
 * Part of the FoxSDR tetrapol helper, derived from tetrapol-kit (https://github.com/airphel/tetrapol-kit-2023,
 * GPL-2.0). This file replaces upstream's apps/tetrapol_dump.c (which reads demodulated bits from a file or pipe with
 * poll()/fcntl()); it adds the FoxSDR helper pipe protocol version 1 (docs/PIPE-PROTOCOL.md) and the GMSK demodulator.
 * Every change against upstream is listed in UPSTREAM.md.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* the upstream headers define short macro names (ERR, INFO, DBG ...): keep them behind the system headers */
#include "gmsk.h"
#include "helper_manifest.h"

#include <tetrapol/helper_out.h>
#include <tetrapol/log.h>
#include <tetrapol/phys_ch.h>
#include <tetrapol/tetrapol.h>
#include <tetrapol/tetrapol_int.h>

#define HELPER_NAME "tetrapol"
#define HELPER_VERSION "0.1.0"
#define HELPER_LICENCE "GPL-2.0-only"
#define HELPER_SOURCE "https://github.com/airphel/tetrapol-kit-2023"
#define HELPER_UPSTREAM "tetrapol-kit-2023 09def9d"

#define IQ_CHUNK 4096u /* complex samples per read: fixed, so the output does not depend on how the pipe is cut */
#define BITS_CHUNK 4096u
#define RATE_MIN 16000.0
#define RATE_MAX 500000.0

enum input_format { FMT_CF32, FMT_CS16, FMT_BITS };

static const char usage_str[] =
    "tetrapol-helper: TETRAPOL control-channel signalling decoder as a FoxSDR GPL helper (GPL-2.0)\n"
    "\n"
    "  tetrapol-helper --foxsdr-helper 1 --format <cf32|cs16|bits> --rate <Hz> [options] < samples > json-lines\n"
    "  tetrapol-helper --foxsdr-helper-info\n"
    "\n"
    "  --format cf32|cs16   baseband I/Q of ONE TETRAPOL channel (carrier near 0 Hz, within about 2 kHz),\n"
    "                       interleaved I,Q; float32 or little-endian int16; --rate 16000 ... 500000 (48000 usual)\n"
    "  --format bits        hard-decided GMSK bits as gnuradio gmsk_demod writes them, one byte (0 or 1) per bit;\n"
    "                       the rate is 8000 (the symbol rate)\n"
    "  --channel cch|tch    control channel (default; signalling is decoded) or traffic channel (frames are only\n"
    "                       counted: \"traffic frame, not decoded\")\n"
    "  --band uhf|vhf       radio band (default uhf)\n"
    "  --direction down|up  downlink (default) or uplink\n"
    "  --scr N              scrambling constant 0..127 (default: detected from the signal)\n"
    "  --hide-ids           terminal and group identities become short hashes (stable inside one run)\n"
    "  --hide-ids-seed N    fixed salt for the hashes (default: random per run); makes runs reproducible\n"
    "  --no-frames          do not print a line for every good frame\n"
    "  --report-bad-frames  also print a line for every frame that failed its checksum\n"
    "  -v                   more diagnostics on stderr (-v, -vv, -vvv); with --hide-ids only -v is honoured\n";

static int fail(const char *msg)
{
    fprintf(stderr, "tetrapol-helper: %s\n", msg);
    return 2;
}

/* Read exactly `want` bytes unless end of file. Returns the bytes read. */
static size_t read_full(void *buf, size_t want)
{
    size_t got = 0;
    while (got < want) {
        size_t r = fread((char *)buf + got, 1, want - got, stdin);
        if (r == 0) {
            break;
        }
        got += r;
    }
    return got;
}

static int parse_double(const char *s, double *out)
{
    char *end = NULL;
    errno = 0;
    double v = strtod(s, &end);
    if (end == s || *end || errno || !(v == v)) {
        return 0;
    }
    *out = v;
    return 1;
}

static uint64_t entropy_salt(void)
{
    uint64_t h = 14695981039346656037ULL;
    uint64_t parts[4];
    parts[0] = (uint64_t)time(NULL);
    parts[1] = (uint64_t)clock();
#ifdef _WIN32
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    parts[2] = (uint64_t)q.QuadPart;
    parts[3] = (uint64_t)GetCurrentProcessId() ^ ((uint64_t)GetTickCount64() << 20);
#else
    parts[2] = (uint64_t)(uintptr_t)&parts;
    parts[3] = 0;
#endif
    const unsigned char *p = (const unsigned char *)parts;
    for (size_t i = 0; i < sizeof(parts); ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* feed demodulated bits to the physical channel decoder (upstream tetrapol_dump_loop does the same in 4096 bit steps) */
static void feed_bits(phys_ch_t *pc, uint8_t *bits, size_t n)
{
    int stalls = 0;
    tp_out_count_bits(n);
    while (n > 0) {
        int r = tetrapol_phys_ch_recv(pc, bits, (int)n);
        if (r < 0) {
            return;
        }
        if (r > 0) {
            bits += r;
            n -= (size_t)r;
            stalls = 0;
        } else if (++stalls > 4) {
            return; /* cannot happen with upstream's buffer handling; never loop forever */
        }
        tetrapol_phys_ch_process(pc);
    }
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    /* first thing: no fault box, no "file not found" box, ever */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif

    int proto = 0, info = 0;
    enum input_format fmt = FMT_CF32;
    int have_format = 0;
    double rate = 0.0;
    int have_rate = 0;
    tetrapol_cfg_t cfg = {
        .band = TETRAPOL_BAND_UHF,
        .dir = DIR_DOWNLINK,
        .radio_ch_type = TETRAPOL_RADIO_CCH,
    };
    int scr = PHYS_CH_SCR_DETECT;
    int verbose = 0;
    tp_out_cfg_t ocfg;
    memset(&ocfg, 0, sizeof(ocfg));
    int seed_given = 0;
    uint64_t seed = 0;
    double centre_hz = 0.0;
    int have_centre = 0;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const char *val = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "--foxsdr-helper-info")) {
            info = 1;
        } else if (!strcmp(a, "--foxsdr-helper")) {
            if (!val || strcmp(val, "1")) {
                return fail("only pipe protocol version 1 is supported (--foxsdr-helper 1)");
            }
            proto = 1;
            ++i;
        } else if (!strcmp(a, "--format")) {
            if (!val) {
                return fail("--format needs a value");
            }
            if (!strcmp(val, "cf32")) {
                fmt = FMT_CF32;
            } else if (!strcmp(val, "cs16")) {
                fmt = FMT_CS16;
            } else if (!strcmp(val, "bits")) {
                fmt = FMT_BITS;
            } else {
                fprintf(stderr, "tetrapol-helper: unsupported --format '%s' (this helper takes cf32, cs16 or bits)\n", val);
                return 2;
            }
            have_format = 1;
            ++i;
        } else if (!strcmp(a, "--rate")) {
            if (!val || !parse_double(val, &rate)) {
                return fail("--rate needs a number (Hz)");
            }
            have_rate = 1;
            ++i;
        } else if (!strcmp(a, "--centre") || !strcmp(a, "--center")) {
            if (!val || !parse_double(val, &centre_hz)) {
                return fail("--centre needs a number (Hz)");
            }
            have_centre = 1; /* accepted and ignored: the input is already one channel at baseband */
            ++i;
        } else if (!strcmp(a, "--channel")) {
            if (val && !strcmp(val, "cch")) {
                cfg.radio_ch_type = TETRAPOL_RADIO_CCH;
            } else if (val && !strcmp(val, "tch")) {
                cfg.radio_ch_type = TETRAPOL_RADIO_TCH;
            } else {
                return fail("--channel is cch or tch");
            }
            ++i;
        } else if (!strcmp(a, "--band")) {
            if (val && !strcmp(val, "uhf")) {
                cfg.band = TETRAPOL_BAND_UHF;
            } else if (val && !strcmp(val, "vhf")) {
                cfg.band = TETRAPOL_BAND_VHF;
            } else {
                return fail("--band is uhf or vhf");
            }
            ++i;
        } else if (!strcmp(a, "--direction")) {
            if (val && (!strcmp(val, "down") || !strcmp(val, "downlink"))) {
                cfg.dir = DIR_DOWNLINK;
            } else if (val && (!strcmp(val, "up") || !strcmp(val, "uplink"))) {
                cfg.dir = DIR_UPLINK;
            } else {
                return fail("--direction is down or up");
            }
            ++i;
        } else if (!strcmp(a, "--scr")) {
            double v;
            if (!val || !parse_double(val, &v) || v < 0 || v > 127 || v != floor(v)) {
                return fail("--scr is an integer 0..127");
            }
            scr = (int)v;
            ++i;
        } else if (!strcmp(a, "--hide-ids")) {
            ocfg.hide_ids = 1;
        } else if (!strcmp(a, "--hide-ids-seed")) {
            double v;
            if (!val || !parse_double(val, &v) || v < 0 || v != floor(v) || v > 9007199254740992.0) {
                return fail("--hide-ids-seed is a non-negative integer");
            }
            seed = (uint64_t)v;
            seed_given = 1;
            ++i;
        } else if (!strcmp(a, "--no-frames")) {
            ocfg.no_frames = 1;
        } else if (!strcmp(a, "--report-bad-frames")) {
            ocfg.report_bad_frames = 1;
        } else if (!strcmp(a, "-v")) {
            verbose = 1;
        } else if (!strcmp(a, "-vv")) {
            verbose = 2;
        } else if (!strcmp(a, "-vvv")) {
            verbose = 3;
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            fputs(usage_str, stdout);
            return 0;
        } else {
            fprintf(stderr, "tetrapol-helper: unknown option '%s'\n\n%s", a, usage_str);
            return 2;
        }
    }
    (void)centre_hz;
    (void)have_centre;

    if (info) {
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        fputs(HELPER_MANIFEST_JSON, stdout);
        fflush(stdout);
        return 0;
    }
    if (!proto) {
        fprintf(stderr, "tetrapol-helper: not started as a helper (use --foxsdr-helper 1 ...)\n\n%s", usage_str);
        return 2;
    }
    if (!have_format) {
        return fail("--format is required (cf32, cs16 or bits)");
    }
    if (fmt == FMT_BITS) {
        if (have_rate && rate != 8000.0) {
            return fail("--format bits is one bit per symbol: --rate must be 8000");
        }
        rate = 8000.0;
    } else {
        if (!have_rate) {
            return fail("--rate is required for I/Q input");
        }
        if (!(rate >= RATE_MIN && rate <= RATE_MAX)) {
            fprintf(stderr, "tetrapol-helper: --rate %.0f out of range (%.0f ... %.0f Hz for I/Q input)\n", rate, RATE_MIN, RATE_MAX);
            return 2;
        }
    }

    /* The JSON goes to a private copy of stdout; the real fd 1 is pointed at stderr so that nothing printed by
       library code can ever corrupt the protocol. Input is binary. */
    FILE *jout;
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    int jfd = _dup(1);
    if (jfd < 0 || _dup2(2, 1) < 0) {
        return fail("cannot set up standard output");
    }
    _setmode(jfd, _O_BINARY);
    jout = _fdopen(jfd, "wb");
#else
    int jfd = dup(1);
    if (jfd < 0 || dup2(2, 1) < 0) {
        return fail("cannot set up standard output");
    }
    jout = fdopen(jfd, "wb");
#endif
    if (!jout) {
        return fail("cannot set up standard output");
    }
    ocfg.out = jout;
    ocfg.salt = seed_given ? seed : entropy_salt();
    ocfg.traffic_mode = (cfg.radio_ch_type == TETRAPOL_RADIO_TCH);
    tp_out_init(&ocfg);

    /* quiet by default; identities can appear in the library's own diagnostics, so with --hide-ids stay at errors */
    if (ocfg.hide_ids && verbose > 1) {
        verbose = 1;
    }
    log_set_lvl(verbose == 0 ? -1 : verbose == 1 ? ERR : verbose == 2 ? INFO : DBG);

    tetrapol_t *tetrapol = tetrapol_create(&cfg);
    phys_ch_t *pc = tetrapol ? tetrapol_phys_ch_create(tetrapol) : NULL;
    if (!pc) {
        fprintf(stderr, "tetrapol-helper: cannot initialise the decoder\n");
        return 3;
    }
    if (scr != PHYS_CH_SCR_DETECT) {
        tetrapol_phys_ch_set_scr(pc, scr);
    }
    tpol_t *tpol = tetrapol_get_tpol(tetrapol);

    /* hello line */
    {
        char hello[768];
        snprintf(hello, sizeof(hello),
                 "{\"helper\":\"%s\",\"version\":\"%s\",\"protocol\":1,\"licence\":\"%s\",\"source\":\"%s\","
                 "\"upstream\":\"%s\",\"format\":\"%s\",\"rate\":%.0f,\"channel\":\"%s\",\"band\":\"%s\","
                 "\"direction\":\"%s\",\"hide_ids\":%s}",
                 HELPER_NAME, HELPER_VERSION, HELPER_LICENCE, HELPER_SOURCE, HELPER_UPSTREAM,
                 fmt == FMT_CF32 ? "cf32" : fmt == FMT_CS16 ? "cs16" : "bits", rate,
                 cfg.radio_ch_type == TETRAPOL_RADIO_CCH ? "cch" : "tch", cfg.band == TETRAPOL_BAND_UHF ? "uhf" : "vhf",
                 cfg.dir == DIR_DOWNLINK ? "down" : "up", ocfg.hide_ids ? "true" : "false");
        tp_out_line(hello, strlen(hello));
    }

    int rc = 0;
    if (fmt == FMT_BITS) {
        static uint8_t buf[BITS_CHUNK];
        for (;;) {
            size_t got = read_full(buf, sizeof(buf));
            if (got == 0) {
                break;
            }
            for (size_t k = 0; k < got; ++k) {
                buf[k] &= 1;
            }
            feed_bits(pc, buf, got);
            if (got < sizeof(buf)) {
                break;
            }
        }
    } else {
        gmsk_t *gm = gmsk_create(rate);
        if (!gm) {
            fprintf(stderr, "tetrapol-helper: cannot initialise the demodulator\n");
            return 3;
        }
        static float iq[IQ_CHUNK * 2];
        static int16_t raw16[IQ_CHUNK * 2];
        const size_t bits_cap = (size_t)(IQ_CHUNK * 8000.0 / rate) + 8;
        uint8_t *bits = (uint8_t *)malloc(bits_cap);
        if (!bits) {
            return 3;
        }
        const size_t sample_bytes = (fmt == FMT_CF32) ? 8 : 4;
        for (;;) {
            size_t got, ns;
            if (fmt == FMT_CF32) {
                got = read_full(iq, IQ_CHUNK * sample_bytes);
                ns = got / sample_bytes;
            } else {
                got = read_full(raw16, IQ_CHUNK * sample_bytes);
                ns = got / sample_bytes;
                for (size_t k = 0; k < ns * 2; ++k) {
                    iq[k] = (float)raw16[k] * (1.0f / 32768.0f);
                }
            }
            if (got % sample_bytes) {
                fprintf(stderr, "tetrapol-helper: warning: input ends in the middle of a sample; last bytes ignored\n");
            }
            if (ns == 0) {
                break;
            }
            size_t nb = gmsk_process(gm, iq, ns, bits, bits_cap);
            feed_bits(pc, bits, nb);
            if (got < IQ_CHUNK * sample_bytes) {
                break;
            }
        }
        if (gmsk_weak_samples(gm) > 64) { /* a few at the start are the empty filter, not a weak signal */
            fprintf(stderr, "tetrapol-helper: %llu samples were too weak to demodulate\n",
                    (unsigned long long)gmsk_weak_samples(gm));
        }
        free(bits);
        gmsk_destroy(gm);
    }

    tp_emit_stats(tpol);
    const tp_out_stats_t *st = tp_out_stats();
    fprintf(stderr, "tetrapol-helper: %llu bits, %llu frames ok, %llu bad, %llu messages, %llu traffic frames not decoded\n",
            (unsigned long long)st->bits, (unsigned long long)st->frames_ok, (unsigned long long)st->frames_bad,
            (unsigned long long)st->messages, (unsigned long long)st->traffic_frames);

    tetrapol_phys_ch_destroy(pc);
    tetrapol_destroy(tetrapol);
    fflush(jout);
    return rc;
}
