/*
 * gmsk.c -- GMSK demodulator of the FoxSDR "tetrapol" GPL helper (I/Q in, hard bits out)
 *
 * New code written for the helper (see gmsk.h). Chain, in the same order as upstream's GNU Radio flowgraph
 * (tetrapol-kit demod/demod.py: low-pass filter, then digital.gmsk_demod = FM demodulator, clock recovery, binary
 * slicer, differential decoder):
 *
 *   low-pass FIR (about 5.2 kHz) -> FM discriminator -> slow mean removal (carrier offset)
 *   -> Gardner timing recovery with cubic interpolation -> slicer -> differential decode (bit = s[n] xor s[n-1])
 *
 * The TETRAPOL modulation is GMSK, 8000 bit/s, BT 0.25 (PAS 0001-2, physical layer); the transmitter differentially
 * encodes the bits before the Gaussian filter, so the decoder's XOR of neighbouring decisions gives the frame bits
 * back and does not care about the polarity of the signal.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation; version 2 of the License.
 */
#include "gmsk.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BIT_RATE 8000.0
#define MAX_TAPS 511
#define FBUF 256 /* ring of discriminator outputs, power of two (must exceed 2 * samples per symbol + 8) */

struct gmsk_priv {
    double rate;
    double sps; /* nominal samples per symbol */

    /* low-pass filter on I and Q */
    int ntaps;
    float taps[MAX_TAPS];
    float *di, *dq; /* delay lines, 2 * ntaps long so the window is always contiguous */
    int dpos;

    /* discriminator */
    float zi, zq; /* previous filtered sample */
    int have_prev;
    double mean; /* removed carrier offset (Hz) */
    uint64_t count;
    uint64_t weak;

    /* smoothing and timing */
    float f[FBUF];
    uint64_t n; /* absolute index of the newest f sample + 1 */
    double next_t;
    double period;
    double period_nominal;
    double xprev;
    double amp;
    int sprev;
    int primed;
};

gmsk_t *gmsk_create(double rate)
{
    if (!(rate >= 12000.0) || !(rate <= 5.0e5)) {
        return NULL;
    }
    gmsk_t *g = (gmsk_t *)calloc(1, sizeof(*g));
    if (!g) {
        return NULL;
    }
    g->rate = rate;
    g->sps = rate / BIT_RATE;

    /* windowed-sinc low-pass, cut-off 5200 Hz, transition about 2500 Hz (Hamming) */
    int nt = (int)ceil(3.3 * rate / 2500.0);
    nt |= 1;
    if (nt > MAX_TAPS) {
        nt = MAX_TAPS;
    }
    if (nt < 9) {
        nt = 9;
    }
    g->ntaps = nt;
    double fc = 5200.0 / rate;
    double sum = 0.0;
    for (int i = 0; i < nt; ++i) {
        double x = i - (nt - 1) / 2.0;
        double s = (x == 0.0) ? 2.0 * fc : sin(2.0 * M_PI * fc * x) / (M_PI * x);
        double w = 0.54 - 0.46 * cos(2.0 * M_PI * i / (nt - 1));
        g->taps[i] = (float)(s * w);
        sum += s * w;
    }
    for (int i = 0; i < nt; ++i) {
        g->taps[i] = (float)(g->taps[i] / sum);
    }
    g->di = (float *)calloc((size_t)nt * 2, sizeof(float));
    g->dq = (float *)calloc((size_t)nt * 2, sizeof(float));
    if (!g->di || !g->dq) {
        gmsk_destroy(g);
        return NULL;
    }
    g->period_nominal = g->sps;
    g->period = g->sps;
    g->next_t = g->sps * 2.0;
    g->amp = 1500.0;
    return g;
}

void gmsk_destroy(gmsk_t *g)
{
    if (!g) {
        return;
    }
    free(g->di);
    free(g->dq);
    free(g);
}

uint64_t gmsk_weak_samples(const gmsk_t *g) { return g->weak; }

/* 4-point cubic (Lagrange) interpolation of the discriminator output at absolute position t */
static double interp(const gmsk_t *g, double t)
{
    int64_t i = (int64_t)floor(t);
    double mu = t - (double)i;
    double y0 = g->f[(i - 1) & (FBUF - 1)];
    double y1 = g->f[i & (FBUF - 1)];
    double y2 = g->f[(i + 1) & (FBUF - 1)];
    double y3 = g->f[(i + 2) & (FBUF - 1)];
    double a = -y0 / 6.0 + y1 / 2.0 - y2 / 2.0 + y3 / 6.0;
    double b = y0 / 2.0 - y1 + y2 / 2.0;
    double c = -y0 / 3.0 - y1 / 2.0 + y2 - y3 / 6.0;
    return ((a * mu + b) * mu + c) * mu + y1;
}

size_t gmsk_process(gmsk_t *g, const float *iq, size_t n, uint8_t *bits, size_t cap)
{
    size_t nb = 0;
    const int nt = g->ntaps;
    const double hz_per_rad = g->rate / (2.0 * M_PI);
    const double kp = 0.04;
    const double ki = kp * kp / 4.0;

    for (size_t k = 0; k < n; ++k) {
        float si = iq[2 * k];
        float sq = iq[2 * k + 1];
        if (!(si == si) || !(sq == sq)) { /* NaN */
            si = 0.0f;
            sq = 0.0f;
        }

        /* low-pass: newest sample at dpos, window [dpos, dpos + nt) in the doubled delay line */
        g->dpos = (g->dpos + nt - 1) % nt;
        g->di[g->dpos] = g->di[g->dpos + nt] = si;
        g->dq[g->dpos] = g->dq[g->dpos + nt] = sq;
        const float *wi = g->di + g->dpos;
        const float *wq = g->dq + g->dpos;
        float fi = 0.0f, fq = 0.0f;
        for (int j = 0; j < nt; ++j) {
            fi += g->taps[j] * wi[j];
            fq += g->taps[j] * wq[j];
        }

        /* discriminator: phase step between neighbouring filtered samples */
        double y = 0.0;
        if (g->have_prev) {
            double re = (double)fi * g->zi + (double)fq * g->zq;
            double im = (double)fq * g->zi - (double)fi * g->zq;
            if (re * re + im * im > 1e-14) {
                y = atan2(im, re) * hz_per_rad;
            } else {
                ++g->weak;
            }
        }
        g->zi = fi;
        g->zq = fq;
        g->have_prev = 1;

        /* remove the carrier offset: running mean, fast at first, then a time constant of 0.5 s */
        double a = 1.0 / (double)(g->count + 1);
        double amin = 1.0 / (0.5 * g->rate);
        if (a < amin) {
            a = amin;
        }
        ++g->count;
        g->mean += a * (y - g->mean);
        float f = (float)(y - g->mean);

        g->f[g->n & (FBUF - 1)] = f;
        ++g->n;

        /* symbol clock: handle every symbol instant whose interpolation window (floor(t)+2) has arrived */
        while (g->next_t + 2.0 <= (double)g->n - 1.0) {
            double xk = interp(g, g->next_t);
            double xm = interp(g, g->next_t - g->period / 2.0);
            double den = g->amp * g->amp + 1.0;
            double e = xm * (xk - g->xprev) / den;
            if (e > 1.0) {
                e = 1.0;
            } else if (e < -1.0) {
                e = -1.0;
            }
            if (!g->primed) { /* the first decision has no previous symbol to compare with */
                e = 0.0;
                g->primed = 1;
            }
            g->amp += 0.01 * (fabs(xk) - g->amp);
            if (g->amp < 200.0) {
                g->amp = 200.0;
            }
            g->period -= ki * e * g->sps;
            if (g->period > g->period_nominal * 1.005) {
                g->period = g->period_nominal * 1.005;
            } else if (g->period < g->period_nominal * 0.995) {
                g->period = g->period_nominal * 0.995;
            }
            g->next_t += g->period - kp * e * g->sps;
            g->xprev = xk;

            int s = xk > 0.0;
            if (nb < cap) {
                bits[nb++] = (uint8_t)(s ^ g->sprev);
            }
            g->sprev = s;
        }
    }
    return nb;
}
