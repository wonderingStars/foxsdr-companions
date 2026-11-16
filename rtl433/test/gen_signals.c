/*
 * gen_signals.c -- test signal generator for the FoxSDR rtl433 helper (not shipped, test input only).
 *
 * Written for this helper (GPL-2.0-or-later like the rest). It makes I/Q files of synthetic short-range
 * device transmissions from the protocol descriptions in the comments of three rtl_433 decoders
 * (src/devices/silvercrest.c, prologue.c, tpms_toyota.c); it contains no rtl_433 code. Deterministic:
 * the same arguments always give the same bytes.
 *
 *   gen_signals --out <file> [--format cu8|cs16|cf32] [--rate 250000] [--noise 0.01] [--gap-ms 80] \
 *               <event> [: <event> ...]
 *
 * events (units in brackets):
 *   silvercrest <button 0..15>                       OOK PWM remote control, 33 bits x 3
 *   prologue <id 0..255> <channel 1..4> <temp x0.1 C> <humidity % | -1> [battery_ok 0/1] [button 0/1]
 *                                                    OOK PPM temperature sensor, 36 bits x 7
 *   toyota <id hex 8 digits> <pressure_raw 0..255> <temp_raw 0..255> [status 0/1] [bad_crc 0/1]
 *                                                    FSK TPMS, 9 bytes differential Manchester, CRC-8
 *
 * rolling codes / key material: none of these protocols has any, and this tool cannot make any.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double g_rate = 250000.0;
static double g_noise = 0.01;
static double g_amp = 0.7;

static float *g_i, *g_q;
static size_t g_n, g_cap;
static double g_time_us; /* cumulative time, so that rounding never drifts */
static double g_phase;
static uint32_t g_rng = 0x12345678u;

static double rnd(void) /* uniform -1..1, xorshift32 */
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return (double)(g_rng >> 8) / 8388608.0 - 1.0;
}

static void push(double i, double q)
{
    if (g_n == g_cap) {
        g_cap = g_cap ? g_cap * 2 : 1 << 20;
        g_i = realloc(g_i, g_cap * sizeof(float));
        g_q = realloc(g_q, g_cap * sizeof(float));
        if (!g_i || !g_q) {
            fprintf(stderr, "gen_signals: out of memory\n");
            exit(1);
        }
    }
    g_i[g_n]   = (float)(i + g_noise * rnd());
    g_q[g_n++] = (float)(q + g_noise * rnd());
}

/* advance the signal by `us` microseconds. ook: amplitude on/off; fsk: tone at hz offset (hz = 1e30: carrier off) */
static void span(double us, double amp, double tone_hz)
{
    double end = g_time_us + us;
    size_t target = (size_t)floor(end * g_rate / 1e6 + 0.5);
    while (g_n < target) {
        if (tone_hz < 1e29)
            g_phase += 2.0 * M_PI * tone_hz / g_rate;
        push(amp * cos(g_phase), amp * sin(g_phase));
    }
    g_time_us = end;
}

static void ook(double us, int on) { span(us, on ? g_amp : 0.0, 0.0); }

static void silence_ms(double ms)
{
    span(ms * 1000.0, 0.0, 0.0);
}

/* ---- Silvercrest remote (OOK PWM: short pulse 264 us = 1, long 744 us = 0; 33 bits; 3 repeats) ---- */
static void ev_silvercrest(int button)
{
    static const uint8_t lut[16] = {2, 3, 0, 1, 4, 5, 7, 6, 0xC, 0xD, 0xF, 0xE, 8, 9, 0xB, 0xA};
    uint8_t b[5] = {0x7c, 0x26, (uint8_t)(0xA0 | (button & 15)), (uint8_t)(0x50 | lut[button & 15]), 0x80};
    for (int rep = 0; rep < 3; ++rep) {
        for (int k = 0; k < 33; ++k) {
            int bit = (b[k / 8] >> (7 - k % 8)) & 1;
            ook(bit ? 264.0 : 744.0, 1);
            ook(480.0, 0);
        }
        ook(8000.0, 0); /* between repeats: above gap_limit (5000), below reset_limit (12000) */
    }
    ook(20000.0, 0);
}

/* ---- Prologue temperature sensor (OOK PPM: 500 us pulse, gap 2000 = 0 / 4000 = 1, sync gap 9000; 36 bits x 7) ---- */
static void ev_prologue(int id, int channel, int temp10, int humi, int battery_ok, int button)
{
    uint8_t b[5] = {0};
    int type = 9;
    b[0] = (uint8_t)((type << 4) | ((id >> 4) & 0x0f));
    b[1] = (uint8_t)(((id & 0x0f) << 4) | (battery_ok ? 0x08 : 0) | (button ? 0x04 : 0) | ((channel - 1) & 3));
    int t = temp10 & 0xfff;
    int h = humi < 0 ? 0xcc : humi;
    b[2] = (uint8_t)(t >> 4);
    b[3] = (uint8_t)(((t & 0x0f) << 4) | ((h >> 4) & 0x0f));
    b[4] = (uint8_t)((h & 0x0f) << 4);
    ook(500.0, 1); /* sync pulse */
    ook(9000.0, 0);
    for (int rep = 0; rep < 7; ++rep) {
        for (int k = 0; k < 36; ++k) {
            int bit = (b[k / 8] >> (7 - k % 8)) & 1;
            ook(500.0, 1);
            ook(bit ? 4000.0 : 2000.0, 0);
        }
        ook(500.0, 1); /* closing pulse, then the sync gap before the next repeat */
        ook(9000.0, 0);
    }
    ook(20000.0, 0);
}

/* ---- Toyota TPMS (FSK, 52 us per raw bit, differential Manchester, CRC-8 poly 0x07 init 0x80) ---- */
static uint8_t crc8(const uint8_t *m, int n, uint8_t poly, uint8_t init)
{
    uint8_t rem = init;
    for (int i = 0; i < n; ++i) {
        rem ^= m[i];
        for (int j = 0; j < 8; ++j)
            rem = (rem & 0x80) ? (uint8_t)((rem << 1) ^ poly) : (uint8_t)(rem << 1);
    }
    return rem;
}

static void ev_toyota(uint32_t id, int pressure_raw, int temp_raw, int status, int bad_crc)
{
    uint8_t b[9];
    b[0] = (uint8_t)(id >> 24);
    b[1] = (uint8_t)(id >> 16);
    b[2] = (uint8_t)(id >> 8);
    b[3] = (uint8_t)id;
    b[4] = (uint8_t)((status ? 0x80 : 0) | ((pressure_raw >> 1) & 0x7f));
    b[5] = (uint8_t)(((pressure_raw & 1) << 7) | ((temp_raw >> 1) & 0x7f));
    b[6] = (uint8_t)((temp_raw & 1) << 7);
    b[7] = (uint8_t)(pressure_raw ^ 0xff);
    b[8] = crc8(b, 8, 0x07, 0x80);
    if (bad_crc)
        b[8] ^= 0x5a; /* a damaged frame: the decoder must reject it */

    /* raw bit stream: lead-in, 14 bit sync 01010101001111, 72 data bits differential Manchester, 3 bit trailer */
    static uint8_t raw[400];
    int n = 0;
    for (int k = 0; k < 6; ++k)
        raw[n++] = (uint8_t)(k & 1 ? 1 : 0); /* short lead-in 0101..., the decoder wants the sync near the start of the row */
    static const char sync[] = "01010101001111";
    for (const char *s = sync; *s; ++s)
        raw[n++] = (uint8_t)(*s - '0');
    int prev = raw[n - 1];
    for (int k = 0; k < 72; ++k) {
        int bit = (b[k / 8] >> (7 - k % 8)) & 1;
        int first = !prev;               /* a transition at the start of every bit */
        int second = bit ? first : !first; /* 1: no mid-bit transition, 0: a mid-bit transition */
        raw[n++] = (uint8_t)first;
        raw[n++] = (uint8_t)second;
        prev = second;
    }
    static const char trailer[] = "00101010111";
    for (const char *s = trailer; *s; ++s)
        raw[n++] = (uint8_t)(*s - '0');
    const double dev = 40000.0;
    for (int k = 0; k < n; ++k)
        span(52.0, g_amp, raw[k] ? dev : -dev);
    silence_ms(1);
}

/* ---- output ---- */
static int wr_file(const char *path, const char *fmt)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "gen_signals: cannot write %s\n", path);
        return 1;
    }
    for (size_t k = 0; k < g_n; ++k) {
        double i = g_i[k], q = g_q[k];
        if (!strcmp(fmt, "cu8")) {
            double vi = floor(127.5 + 127.0 * i + 0.5), vq = floor(127.5 + 127.0 * q + 0.5);
            uint8_t o[2] = {(uint8_t)(vi < 0 ? 0 : vi > 255 ? 255 : vi), (uint8_t)(vq < 0 ? 0 : vq > 255 ? 255 : vq)};
            fwrite(o, 1, 2, f);
        }
        else if (!strcmp(fmt, "cs16")) {
            double vi = floor(16000.0 * i + 0.5), vq = floor(16000.0 * q + 0.5);
            int16_t o[2] = {(int16_t)(vi < -32768 ? -32768 : vi > 32767 ? 32767 : vi),
                    (int16_t)(vq < -32768 ? -32768 : vq > 32767 ? 32767 : vq)};
            uint8_t le[4] = {(uint8_t)(o[0] & 0xff), (uint8_t)((o[0] >> 8) & 0xff), (uint8_t)(o[1] & 0xff), (uint8_t)((o[1] >> 8) & 0xff)};
            fwrite(le, 1, 4, f);
        }
        else if (!strcmp(fmt, "cs8")) {
            double vi = floor(127.0 * i + 0.5), vq = floor(127.0 * q + 0.5);
            int8_t o[2] = {(int8_t)(vi < -128 ? -128 : vi > 127 ? 127 : vi), (int8_t)(vq < -128 ? -128 : vq > 127 ? 127 : vq)};
            fwrite(o, 1, 2, f);
        }
        else { /* cf32, little-endian host assumed */
            float o[2] = {(float)i, (float)q};
            fwrite(o, sizeof(float), 2, f);
        }
    }
    fclose(f);
    return 0;
}

static int usage(void)
{
    fprintf(stderr, "usage: gen_signals --out <file> [--format cu8|cs16|cf32] [--rate Hz] [--noise x] [--gap-ms ms] event [: event ...]\n");
    return 2;
}

int main(int argc, char **argv)
{
    const char *out = NULL, *fmt = "cu8";
    double gap_ms = 80.0;
    int i = 1;
    for (; i < argc && !strncmp(argv[i], "--", 2); i += 2) {
        if (i + 1 >= argc)
            return usage();
        if (!strcmp(argv[i], "--out")) out = argv[i + 1];
        else if (!strcmp(argv[i], "--format")) fmt = argv[i + 1];
        else if (!strcmp(argv[i], "--rate")) g_rate = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--noise")) g_noise = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--gap-ms")) gap_ms = atof(argv[i + 1]);
        else return usage();
    }
    if (!out || (strcmp(fmt, "cu8") && strcmp(fmt, "cs8") && strcmp(fmt, "cs16") && strcmp(fmt, "cf32")) || g_rate < 50000.0)
        return usage();
    silence_ms(gap_ms);
    while (i < argc) {
        const char *k = argv[i++];
        int a0 = i;
        while (i < argc && strcmp(argv[i], ":"))
            i++;
        int na = i - a0;
        char **a = argv + a0;
        if (i < argc)
            i++; /* skip ':' */
        if (!strcmp(k, "silvercrest") && na == 1)
            ev_silvercrest(atoi(a[0]));
        else if (!strcmp(k, "prologue") && na >= 4)
            ev_prologue(atoi(a[0]), atoi(a[1]), atoi(a[2]), atoi(a[3]), na > 4 ? atoi(a[4]) : 1, na > 5 ? atoi(a[5]) : 0);
        else if (!strcmp(k, "toyota") && na >= 3)
            ev_toyota((uint32_t)strtoul(a[0], NULL, 16), atoi(a[1]), atoi(a[2]), na > 3 ? atoi(a[3]) : 0, na > 4 ? atoi(a[4]) : 0);
        else
            return usage();
        silence_ms(gap_ms);
    }
    return wr_file(out, fmt);
}
