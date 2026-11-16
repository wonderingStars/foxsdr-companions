/*
 *      fmdemod.c -- narrow-band FM demodulator for the multimon helper's real-signal checks
 *
 *      Part of the FoxSDR "multimon" GPL helper test tools (not shipped, not part of multimon-ng). New code.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      fmdemod <in.cs16|in.cu8> <sample rate Hz> <offset Hz> [--format cs16|cu8] [--seconds S] > audio.s16
 *
 *      Reads interleaved I/Q (cs16: little-endian int16; cu8: unsigned 8-bit, zero at 127.5; default from the file
 *      extension, else cs16), shifts the channel at `offset` Hz from the capture centre to 0, filters and decimates
 *      (two stages, to about 50 kHz: the rate need not be a multiple of anything, 1.024 MS/s from an RTL-SDR works),
 *      FM-demodulates (phase difference), low-passes the audio at 8 kHz and resamples to 22050 Hz with linear
 *      interpolation. Writes mono s16 to standard output: the helper's input. `--seconds S` stops after S seconds
 *      of capture. Audio scale: 1 Hz of deviation = 3 counts, so 4.5 kHz deviation is about 13500.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#define PI 3.14159265358979323846

/* windowed-sinc low pass (Hamming), cutoff fc in cycles/sample, n taps (odd), unity DC gain */
static float *design(int n, double fc)
{
    float *h = (float *)malloc((size_t)n * sizeof(float));
    double sum = 0;
    for (int i = 0; i < n; i++) {
        double x = i - (n - 1) / 2.0;
        double s = x == 0 ? 2 * fc : sin(2 * PI * fc * x) / (PI * x);
        double w = 0.54 - 0.46 * cos(2 * PI * i / (n - 1));
        h[i] = (float)(s * w);
        sum += h[i];
    }
    for (int i = 0; i < n; i++)
        h[i] = (float)(h[i] / sum);
    return h;
}

/* streaming decimating complex FIR */
struct dec {
    int taps, factor, phase, pos;
    float *h, *ri, *rq; /* circular history, doubled for contiguous reads */
};

static void dec_init(struct dec *d, int taps, int factor, double fc)
{
    d->taps = taps;
    d->factor = factor;
    d->phase = 0;
    d->pos = 0;
    d->h = design(taps, fc);
    d->ri = (float *)calloc((size_t)taps * 2, sizeof(float));
    d->rq = (float *)calloc((size_t)taps * 2, sizeof(float));
}

/* push one sample; returns 1 and fills (oi, oq) when an output is due */
static int dec_push(struct dec *d, float i, float q, float *oi, float *oq)
{
    d->pos = d->pos ? d->pos - 1 : d->taps - 1;
    d->ri[d->pos] = d->ri[d->pos + d->taps] = i;
    d->rq[d->pos] = d->rq[d->pos + d->taps] = q;
    if (++d->phase < d->factor)
        return 0;
    d->phase = 0;
    float ai = 0, aq = 0;
    const float *pi_ = d->ri + d->pos, *pq = d->rq + d->pos;
    for (int k = 0; k < d->taps; k++) {
        ai += d->h[k] * pi_[k];
        aq += d->h[k] * pq[k];
    }
    *oi = ai;
    *oq = aq;
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: fmdemod <in.cs16|in.cu8> <rate Hz> <offset Hz> [--format cs16|cu8] [--seconds S] > audio.s16\n");
        return 2;
    }
    int is_u8 = 0;
    double seconds = 0;
    {
        size_t n = strlen(argv[1]);
        if (n >= 4 && strcmp(argv[1] + n - 4, ".cu8") == 0)
            is_u8 = 1;
        for (int a = 4; a < argc; a++) {
            if (strcmp(argv[a], "--format") == 0 && a + 1 < argc) {
                a++;
                if (strcmp(argv[a], "cu8") == 0)
                    is_u8 = 1;
                else if (strcmp(argv[a], "cs16") == 0)
                    is_u8 = 0;
                else {
                    fprintf(stderr, "fmdemod: unknown --format %s\n", argv[a]);
                    return 2;
                }
            } else if (strcmp(argv[a], "--seconds") == 0 && a + 1 < argc) {
                seconds = atof(argv[++a]);
            } else {
                fprintf(stderr, "fmdemod: unknown option %s\n", argv[a]);
                return 2;
            }
        }
    }
    FILE *in = fopen(argv[1], "rb");
    if (!in) {
        fprintf(stderr, "fmdemod: cannot read %s\n", argv[1]);
        return 2;
    }
    double rate = atof(argv[2]);
    double offset = atof(argv[3]);
    if (rate < 200000) {
        fprintf(stderr, "fmdemod: rate must be at least 200000 Hz\n");
        return 2;
    }
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    const int f2 = 4;
    int fa = (int)(rate / 200000.0 + 0.5);               /* stage 1 brings the rate to about 200 kHz */
    if (fa < 1)
        fa = 1;
    const double mid = rate / fa / f2;                   /* discriminator rate: 50 kHz for 1 MS/s, 51.2 kHz for 1.024 */
    struct dec s1, s2;
    dec_init(&s1, 31, fa, 0.5 / fa * 0.8);              /* to about 200 kHz, passband 80 % of that */
    dec_init(&s2, 101, f2, 7500.0 / (rate / fa));        /* to the discriminator rate, cutoff 7.5 kHz */
    float *alp = design(63, 8000.0 / mid);
    float hist[63] = {0};
    int hp = 0;

    double nco = 0, dnco = -2 * PI * offset / rate;
    float pri = 0, prq = 0;
    int have_prev = 0;
    double pos = 0; /* position in discriminator-rate samples of the next output */
    const double step = mid / 22050.0;
    const double max_in = seconds > 0 ? seconds * rate : 1e30;
    double read_total = 0;
    float lp_prev = 0, lp_cur = 0;
    double lp_index = 0; /* index of lp_cur */
    int started = 0;

    static short blk[65536 * 2];
    static unsigned char ublk[65536 * 2];
    size_t got;
    while (read_total < max_in) {
        if (is_u8) {
            got = fread(ublk, 2, 65536, in);
            for (size_t k = 0; k < got * 2; k++)
                blk[k] = (short)(((int)ublk[k] * 2 - 255) * 128); /* (v - 127.5) / 127.5 of full scale */
        } else {
            got = fread(blk, sizeof(short) * 2, 65536, in);
        }
        if (got == 0)
            break;
        read_total += (double)got;
        for (size_t n = 0; n < got; n++) {
            float c = (float)cos(nco), s = (float)sin(nco);
            nco += dnco;
            if (nco < -PI)
                nco += 2 * PI;
            float i0 = blk[2 * n] * (1.0f / 32768.0f), q0 = blk[2 * n + 1] * (1.0f / 32768.0f);
            float mi = i0 * c - q0 * s, mq = i0 * s + q0 * c;
            float ai, aq, bi, bq;
            if (!dec_push(&s1, mi, mq, &ai, &aq))
                continue;
            if (!dec_push(&s2, ai, aq, &bi, &bq))
                continue;
            /* FM discriminator at 50 kHz */
            float d = 0;
            if (have_prev) {
                float re = bi * pri + bq * prq, im = bq * pri - bi * prq;
                d = (float)(atan2(im, re) * (mid / (2 * PI)) * 3.0);
            }
            pri = bi;
            prq = bq;
            have_prev = 1;
            /* audio low pass */
            hist[hp] = d;
            float y = 0;
            for (int k = 0; k < 63; k++)
                y += alp[k] * hist[(hp - k + 63) % 63];
            hp = (hp + 1) % 63;
            /* linear-interpolating resampler to 22050 Hz */
            lp_prev = lp_cur;
            lp_cur = y;
            lp_index += 1;
            if (!started) {
                started = 1;
                pos = lp_index;
            }
            while (pos <= lp_index && pos >= lp_index - 1) {
                double frac = pos - (lp_index - 1);
                double v = lp_prev + (lp_cur - lp_prev) * frac;
                if (v > 32767)
                    v = 32767;
                if (v < -32768)
                    v = -32768;
                short o = (short)lrint(v);
                fwrite(&o, sizeof o, 1, stdout);
                pos += step;
            }
        }
    }
    fclose(in);
    return 0;
}
