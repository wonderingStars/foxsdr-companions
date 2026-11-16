/*
 * iqshift: TEST TOOL for the rtl433 helper (not part of the helper, not shipped in the manifest).
 *
 * Takes a wideband I/Q recording on standard input, moves a channel to zero frequency, low-pass filters and keeps
 * every M-th sample, and writes the result as cs16 to standard output: the rate the rtl_433 decoders are tuned for
 * (250000 Hz) can be made out of a 2 MS/s or 2.5 MS/s B200 recording.
 *
 *   iqshift --format cs16|cu8 --rate <Hz> --shift <Hz> --decim <M> [--floor-dbfs <dB> | --gain-db <dB>] [--cal-seconds <s>]
 *           [--skip-seconds <s>] [--seconds <s>] < wide.cs16 > narrow.cs16
 *
 *   --shift   channel offset from the recording's centre in Hz (a channel at centre + 100000 Hz: --shift 100000).
 *   --decim   integer decimation factor; the output rate is rate / M (printed on stderr).
 *   --gain-db fixed digital gain (default 0).
 *   --floor-dbfs  instead of a fixed gain: pick the gain that puts the MEDIAN envelope of the first --cal-seconds
 *             (default 20) of the output channel at this level in dBFS. Median of |x| is a noise-floor estimate when
 *             the channel is active only a few percent of the time. Needed because rtl_433's detector has a minimum
 *             level (about -12 dB re 16384, i.e. 0.125 of full scale) that a B200 recording at low gain never reaches.
 *   --skip-seconds / --seconds  process only that part of the input.
 *
 * Low-pass: Blackman-windowed sinc, cut-off 0.4 x output rate, 16 x M + 1 taps. Plain C, no dependencies; it reads
 * a file from stdin and writes to stdout, nothing else. Prints one summary line on stderr (gain used, median
 * envelope, clipped samples). Licence GPL-2.0-or-later (part of the helper repository).
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

static int cmp_float(void const *a, void const *b)
{
    float x = *(float const *)a, y = *(float const *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
    char const *fmt = NULL;
    double rate = 0, shift = 0, gain_db = 0, floor_db = 0, cal_s = 20, skip_s = 0, total_s = 0;
    int decim = 0, use_floor = 0;
    for (int i = 1; i < argc; ++i) {
        char const *a = argv[i];
        char const *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "--format") && v) fmt = argv[++i];
        else if (!strcmp(a, "--rate") && v) rate = atof(argv[++i]);
        else if (!strcmp(a, "--shift") && v) shift = atof(argv[++i]);
        else if (!strcmp(a, "--decim") && v) decim = atoi(argv[++i]);
        else if (!strcmp(a, "--gain-db") && v) gain_db = atof(argv[++i]);
        else if (!strcmp(a, "--floor-dbfs") && v) { floor_db = atof(argv[++i]); use_floor = 1; }
        else if (!strcmp(a, "--cal-seconds") && v) cal_s = atof(argv[++i]);
        else if (!strcmp(a, "--skip-seconds") && v) skip_s = atof(argv[++i]);
        else if (!strcmp(a, "--seconds") && v) total_s = atof(argv[++i]);
        else { fprintf(stderr, "iqshift: bad argument \"%s\" (see the comment at the top of tools/iqshift.c)\n", a); return 2; }
    }
    int is_u8 = fmt && !strcmp(fmt, "cu8");
    if (!fmt || (!is_u8 && strcmp(fmt, "cs16")) || rate < 1000 || decim < 1 || decim > 200) {
        fprintf(stderr, "iqshift: need --format cs16|cu8, --rate <Hz>, --decim <1..200>\n");
        return 2;
    }
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    int const ntaps = 16 * decim + 1;
    double *h = (double *)malloc(sizeof(double) * ntaps);
    float *hf = (float *)malloc(sizeof(float) * ntaps);
    double const fc = 0.4 / decim; /* cut-off as a fraction of the input rate */
    double sum = 0;
    for (int n = 0; n < ntaps; ++n) {
        double m = n - (ntaps - 1) / 2.0;
        double s = (m == 0) ? 2 * fc : sin(2 * PI * fc * m) / (PI * m);
        double w = 0.42 - 0.5 * cos(2 * PI * n / (ntaps - 1)) + 0.08 * cos(4 * PI * n / (ntaps - 1));
        h[n] = s * w;
        sum += h[n];
    }
    for (int n = 0; n < ntaps; ++n) hf[n] = (float)(h[n] / sum);

    size_t const bsz = 1u << 16; /* input samples per block */
    size_t const hist = (size_t)ntaps;
    float *xi = (float *)malloc(sizeof(float) * (hist + bsz));
    float *xq = (float *)malloc(sizeof(float) * (hist + bsz));
    unsigned char *raw = (unsigned char *)malloc(bsz * 4);
    int16_t *outbuf = (int16_t *)malloc(sizeof(int16_t) * 2 * (bsz / decim + 2));
    if (!h || !hf || !xi || !xq || !raw || !outbuf) { fprintf(stderr, "iqshift: out of memory\n"); return 1; }
    memset(xi, 0, sizeof(float) * hist);
    memset(xq, 0, sizeof(float) * hist);

    size_t const bps = is_u8 ? 2 : 4;
    unsigned long long skip = (unsigned long long)(skip_s * rate);
    unsigned long long limit = total_s > 0 ? (unsigned long long)(total_s * rate) : ~0ull;
    if (skip) { /* read and drop */
        unsigned long long left = skip;
        while (left) {
            size_t n = left > bsz ? bsz : (size_t)left;
            size_t got = fread(raw, bps, n, stdin);
            if (!got) break;
            left -= got;
        }
    }

    /* rotator: x * exp(-j 2 pi shift n / rate), advanced by recurrence, renormalised per block */
    double ph = 0, dph = -2 * PI * shift / rate;
    float const scale = is_u8 ? (1.0f / 127.5f) : (1.0f / 32768.0f); /* to +-1.0 full scale */

    float *cal = NULL; /* envelope of the output during calibration */
    size_t ncal = 0, cal_max = (size_t)(cal_s * rate / decim) + 1, cal_pending = 0;
    double gain = pow(10.0, gain_db / 20.0);
    int calibrating = use_floor;
    int16_t *held = NULL; /* complex output held back while calibrating: float pairs kept before the gain is known */
    float *heldf = NULL;
    size_t nheld = 0;
    if (calibrating) {
        cal = (float *)malloc(sizeof(float) * cal_max);
        heldf = (float *)malloc(sizeof(float) * 2 * cal_max);
        if (!cal || !heldf) { fprintf(stderr, "iqshift: out of memory\n"); return 1; }
    }
    (void)held;
    (void)cal_pending;

    unsigned long long in_total = 0, out_total = 0, clipped = 0, phase_in = 0; /* phase_in: input index of next output */
    double med = 0;

    for (;;) {
        size_t want = bsz;
        if (limit != ~0ull && in_total + want > limit) want = (size_t)(limit - in_total);
        if (!want) break;
        size_t got = fread(raw, bps, want, stdin);
        if (!got) break;
        /* mix */
        for (size_t n = 0; n < got; ++n) {
            float i0, q0;
            if (is_u8) { i0 = ((int)raw[2 * n] - 127.5f) * scale; q0 = ((int)raw[2 * n + 1] - 127.5f) * scale; }
            else {
                int16_t a, b;
                memcpy(&a, raw + 4 * n, 2);
                memcpy(&b, raw + 4 * n + 2, 2);
                i0 = a * scale; q0 = b * scale;
            }
            float c = (float)cos(ph), s = (float)sin(ph);
            ph += dph;
            xi[hist + n] = i0 * c - q0 * s;
            xq[hist + n] = i0 * s + q0 * c;
        }
        ph = fmod(ph, 2 * PI);
        /* decimating FIR: output k is at input index (phase_in), using the ntaps samples ending there */
        size_t nout = 0;
        size_t base = (size_t)(in_total); /* global index of xi[hist] */
        while (phase_in < base + got) {
            size_t pos = hist + (size_t)(phase_in - base); /* index in xi of the newest sample for this output */
            float ai = 0, aq = 0;
            float const *pi_ = xi + pos - (ntaps - 1);
            float const *pq_ = xq + pos - (ntaps - 1);
            for (int t = 0; t < ntaps; ++t) { /* symmetric filter, so tap order does not matter */
                ai += hf[t] * pi_[t];
                aq += hf[t] * pq_[t];
            }
            if (calibrating) {
                cal[ncal] = sqrtf(ai * ai + aq * aq);
                heldf[2 * ncal] = ai;
                heldf[2 * ncal + 1] = aq;
                ++ncal;
                if (ncal >= cal_max) goto calibrated;
            }
            else {
                double yi = ai * gain * 32768.0, yq = aq * gain * 32768.0;
                if (yi > 32767) { yi = 32767; ++clipped; } else if (yi < -32768) { yi = -32768; ++clipped; }
                if (yq > 32767) { yq = 32767; ++clipped; } else if (yq < -32768) { yq = -32768; ++clipped; }
                outbuf[2 * nout] = (int16_t)lrint(yi);
                outbuf[2 * nout + 1] = (int16_t)lrint(yq);
                ++nout;
            }
            phase_in += decim;
        }
        if (0) {
        calibrated:
            qsort(cal, ncal, sizeof(float), cmp_float);
            med = cal[ncal / 2];
            if (med < 1e-9) med = 1e-9;
            gain = pow(10.0, floor_db / 20.0) / med;
            for (size_t k = 0; k < ncal; ++k) {
                double yi = heldf[2 * k] * gain * 32768.0, yq = heldf[2 * k + 1] * gain * 32768.0;
                if (yi > 32767) { yi = 32767; ++clipped; } else if (yi < -32768) { yi = -32768; ++clipped; }
                if (yq > 32767) { yq = 32767; ++clipped; } else if (yq < -32768) { yq = -32768; ++clipped; }
                int16_t pair[2] = {(int16_t)lrint(yi), (int16_t)lrint(yq)};
                fwrite(pair, 2, 2, stdout);
                ++out_total;
            }
            nheld = ncal;
            free(heldf); heldf = NULL; free(cal); cal = NULL;
            calibrating = 0;
            nout = 0; /* the rest of this block (after the calibration point) continues below */
            phase_in += decim;
            /* continue filtering the remainder of the current block with the final gain */
            while (phase_in < base + got) {
                size_t pos = hist + (size_t)(phase_in - base);
                float ai = 0, aq = 0;
                float const *pi_ = xi + pos - (ntaps - 1);
                float const *pq_ = xq + pos - (ntaps - 1);
                for (int t = 0; t < ntaps; ++t) { ai += hf[t] * pi_[t]; aq += hf[t] * pq_[t]; }
                double yi = ai * gain * 32768.0, yq = aq * gain * 32768.0;
                if (yi > 32767) { yi = 32767; ++clipped; } else if (yi < -32768) { yi = -32768; ++clipped; }
                if (yq > 32767) { yq = 32767; ++clipped; } else if (yq < -32768) { yq = -32768; ++clipped; }
                outbuf[2 * nout] = (int16_t)lrint(yi);
                outbuf[2 * nout + 1] = (int16_t)lrint(yq);
                ++nout;
                phase_in += decim;
            }
        }
        if (nout) { fwrite(outbuf, 4, nout, stdout); out_total += nout; }
        /* keep the last ntaps-1 mixed samples as history */
        memmove(xi, xi + got, sizeof(float) * hist);
        memmove(xq, xq + got, sizeof(float) * hist);
        in_total += got;
        if (got < want) break;
    }
    if (calibrating && ncal) { /* input ended before the calibration window was full: use what there is */
        qsort(cal, ncal, sizeof(float), cmp_float);
        med = cal[ncal / 2];
        if (med < 1e-9) med = 1e-9;
        gain = pow(10.0, floor_db / 20.0) / med;
        for (size_t k = 0; k < ncal; ++k) {
            double yi = heldf[2 * k] * gain * 32768.0, yq = heldf[2 * k + 1] * gain * 32768.0;
            if (yi > 32767) { yi = 32767; ++clipped; } else if (yi < -32768) { yi = -32768; ++clipped; }
            if (yq > 32767) { yq = 32767; ++clipped; } else if (yq < -32768) { yq = -32768; ++clipped; }
            int16_t pair[2] = {(int16_t)lrint(yi), (int16_t)lrint(yq)};
            fwrite(pair, 2, 2, stdout);
            ++out_total;
        }
    }
    (void)nheld;
    fflush(stdout);
    fprintf(stderr,
            "iqshift: in %llu samples at %.0f Hz, out %llu samples at %.0f Hz, shift %.0f Hz, decim %d, gain %.2f dB%s, "
            "median envelope before gain %.1f dBFS, clipped output values %llu\n",
            in_total, rate, out_total, rate / decim, shift, decim, 20.0 * log10(gain),
            use_floor ? " (from --floor-dbfs)" : "", use_floor ? 20.0 * log10(med) : 0.0, clipped);
    return 0;
}
