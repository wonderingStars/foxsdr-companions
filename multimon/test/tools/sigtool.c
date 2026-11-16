/*
 *      sigtool.c -- small test helper for the FoxSDR "multimon" GPL helper's tests
 *
 *      Makes and moves test signal files; it is not shipped. New code written for the helper.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      sigtool s16tof32 <in.raw> <out.raw>     16-bit samples -> 32-bit float samples (value / 32768)
 *      sigtool garbage <seed> <bytes> <out>    pseudo-random bytes (xorshift32, fixed for a given seed)
 *      sigtool oddbyte <in> <out>              copy and append one stray byte (a cut-off sample)
 *      sigtool silence <samples> <out>         zero samples (s16)
 *      sigtool feed <in> <chunk bytes>         copy a file to stdout in small writes, flushing each
 *      sigtool lines <file>                    print the number of lines in a file
 *      sigtool eas <message> <out>             SAME (EAS) AFSK: header three times, then NNNN three times

 *      sigtool fmmod <audio.s16> <rate> <offset Hz> <out.cs16>   FM-modulate 22050 Hz audio (1 Hz deviation = 3 counts)
 *                                      onto a complex carrier at `offset`, with a little noise: input for fmdemod
 *      sigtool morse <text> <out>              700 Hz CW at 50 ms per dit (the decoder's default timing)
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

static FILE *open_out(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "sigtool: cannot write %s\n", path);
        exit(2);
    }
    return f;
}

static FILE *open_in(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "sigtool: cannot read %s\n", path);
        exit(2);
    }
    return f;
}


/* ---- signal generators written for these tests (not from multimon-ng) ------------------------------------ */

#define RATE 22050.0
#define TWO_PI 6.283185307179586

static void put_samples(FILE *out, const double *v, long n)
{
    for (long i = 0; i < n; i++) {
        double x = v[i] * 16000.0;
        short s = (short)(x < 0 ? x - 0.5 : x + 0.5);
        fwrite(&s, sizeof s, 1, out);
    }
}

static void put_silence(FILE *out, double seconds)
{
    long n = (long)(seconds * RATE);
    short z = 0;
    for (long i = 0; i < n; i++)
        fwrite(&z, sizeof z, 1, out);
}

/* One SAME burst: 16 x 0xAB, then the message; 8 bits per byte, least significant bit first,
   520.83 baud, mark (1) = 2083.3 Hz, space (0) = 1562.5 Hz, phase continuous. */
static void same_burst(FILE *out, const char *msg)
{
    size_t len = strlen(msg);
    size_t nbytes = 16 + len;
    size_t nbits = nbytes * 8;
    unsigned char *bits = (unsigned char *)malloc(nbits);
    for (size_t i = 0; i < nbytes; i++) {
        unsigned char b = i < 16 ? 0xAB : (unsigned char)msg[i - 16];
        for (int k = 0; k < 8; k++)
            bits[i * 8 + (size_t)k] = (b >> k) & 1;
    }
    const double baud = 520.8333333;
    long total = (long)((double)nbits / baud * RATE);
    double *v = (double *)malloc((size_t)total * sizeof(double));
    double phase = 0;
    for (long n = 0; n < total; n++) {
        size_t bi = (size_t)((double)n / RATE * baud);
        if (bi >= nbits)
            bi = nbits - 1;
        double f = bits[bi] ? 2083.3333333 : 1562.5;
        phase += TWO_PI * f / RATE;
        v[n] = 0.9 * sin(phase);
    }
    put_samples(out, v, total);
    free(v);
    free(bits);
}

static void make_eas(const char *msg, const char *path)
{
    FILE *out = open_out(path);
    put_silence(out, 1.0);
    for (int i = 0; i < 3; i++) {
        same_burst(out, msg);
        put_silence(out, 1.0);
    }
    put_silence(out, 2.0);
    for (int i = 0; i < 3; i++) {
        same_burst(out, "NNNN");
        put_silence(out, 1.0);
    }
    put_silence(out, 1.0);
    fclose(out);
}

static const char *morse_code(char c)
{
    static const char *letters[26] = {".-",   "-...", "-.-.", "-..",  ".",   "..-.", "--.",  "....", "..",
                                      ".---", "-.-",  ".-..", "--",   "-.",  "---",  ".--.", "--.-", ".-.",
                                      "...",  "-",    "..-",  "...-", ".--", "-..-", "-.--", "--.."};
    static const char *digits[10] = {"-----", ".----", "..---", "...--", "....-",
                                     ".....", "-....", "--...", "---..", "----."};
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z')
        return letters[c - 'A'];
    if (c >= '0' && c <= '9')
        return digits[c - '0'];
    return NULL;
}

/* keyed tone: 50 ms per dit, dah = 3 dits, 1 dit between elements, 3 between letters, 7 between words */
static void make_morse(const char *text, const char *path)
{
    FILE *out = open_out(path);
    const long dit = (long)(0.050 * RATE);
    const double f = 700.0;
    long n = 0;
    put_silence(out, 0.6);
    for (const char *p = text; *p; p++) {
        if (*p == ' ') {
            put_silence(out, (double)(4 * dit) / RATE); /* 3 after the letter + 4 = 7 */
            continue;
        }
        const char *code = morse_code(*p);
        if (!code)
            continue;
        for (const char *e = code; *e; e++) {
            long len = (*e == '.' ? 1 : 3) * dit;
            double *v = (double *)malloc((size_t)len * sizeof(double));
            for (long i = 0; i < len; i++) {
                double env = 1.0;
                long edge = (long)(0.005 * RATE);
                if (i < edge)
                    env = 0.5 - 0.5 * cos(3.141592653589793 * (double)i / (double)edge);
                else if (i >= len - edge)
                    env = 0.5 - 0.5 * cos(3.141592653589793 * (double)(len - 1 - i) / (double)edge);
                v[i] = 0.9 * env * sin(TWO_PI * f * (double)(n + i) / RATE);
            }
            put_samples(out, v, len);
            free(v);
            n += len;
            put_silence(out, (double)dit / RATE);
            n += dit;
        }
        put_silence(out, (double)(2 * dit) / RATE); /* 1 after the last element + 2 = 3 */
        n += 2 * dit;
    }
    put_silence(out, 1.0);
    fclose(out);
}

/* FM modulator used to check the fmdemod tool: audio (22050 Hz s16) -> cs16 at `rate`. */
static void make_fmmod(const char *inpath, double rate, double offset, const char *outpath)
{
    FILE *in = open_in(inpath), *out = open_out(outpath);
    short s;
    double phase = 0, pcar = 0;
    double a = 0;
    uint32_t x = 777;
    double pos = 0;
    int have = 0;
    short cur = 0;
    while (1) {
        /* nearest-sample hold of the audio at `rate` */
        while (pos >= 1.0 || !have) {
            if (fread(&s, sizeof s, 1, in) != 1) {
                fclose(in);
                fclose(out);
                return;
            }
            cur = s;
            have = 1;
            pos -= 1.0;
        }
        pos += 22050.0 / rate;
        double dev = cur / 3.0; /* Hz */
        phase += TWO_PI * dev / rate;
        pcar += TWO_PI * offset / rate;
        if (phase > TWO_PI)
            phase -= TWO_PI;
        if (pcar > TWO_PI)
            pcar -= TWO_PI;
        double th = phase + pcar;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        a = ((double)(x & 0xFFFF) / 65536.0 - 0.5) * 600.0;
        short i = (short)(8000.0 * cos(th) + a);
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        a = ((double)(x & 0xFFFF) / 65536.0 - 0.5) * 600.0;
        short q = (short)(8000.0 * sin(th) + a);
        fwrite(&i, sizeof i, 1, out);
        fwrite(&q, sizeof q, 1, out);
    }
}

int main(int argc, char **argv)
{
    if (argc >= 4 && !strcmp(argv[1], "s16tof32")) {
        FILE *in = open_in(argv[2]), *out = open_out(argv[3]);
        short s;
        while (fread(&s, sizeof s, 1, in) == 1) {
            float v = (float)s / 32768.0f;
            fwrite(&v, sizeof v, 1, out);
        }
        fclose(in);
        fclose(out);
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "garbage")) {
        uint32_t x = (uint32_t)strtoul(argv[2], NULL, 10);
        if (!x)
            x = 2463534242u;
        long n = strtol(argv[3], NULL, 10);
        FILE *out = open_out(argv[4]);
        for (long i = 0; i < n; i++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            fputc((int)(x & 0xFF), out);
        }
        fclose(out);
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "oddbyte")) {
        FILE *in = open_in(argv[2]), *out = open_out(argv[3]);
        int c;
        while ((c = fgetc(in)) != EOF)
            fputc(c, out);
        fputc(0x55, out);
        fclose(in);
        fclose(out);
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "silence")) {
        long n = strtol(argv[2], NULL, 10);
        FILE *out = open_out(argv[3]);
        for (long i = 0; i < n; i++) {
            fputc(0, out);
            fputc(0, out);
        }
        fclose(out);
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "feed")) {
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        FILE *in = open_in(argv[2]);
        long chunk = strtol(argv[3], NULL, 10);
        if (chunk < 1 || chunk > 65536)
            chunk = 4096;
        char *buf = (char *)malloc((size_t)chunk);
        size_t r;
        while ((r = fread(buf, 1, (size_t)chunk, in)) > 0) {
            if (fwrite(buf, 1, r, stdout) != r)
                break;
            fflush(stdout);
        }
        free(buf);
        fclose(in);
        return 0;
    }
    if (argc >= 6 && !strcmp(argv[1], "fmmod")) {
        make_fmmod(argv[2], atof(argv[3]), atof(argv[4]), argv[5]);
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "eas")) {
        make_eas(argv[2], argv[3]);
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "morse")) {
        make_morse(argv[2], argv[3]);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "lines")) {
        FILE *in = open_in(argv[2]);
        long n = 0;
        int c;
        while ((c = fgetc(in)) != EOF)
            if (c == '\n')
                n++;
        printf("%ld\n", n);
        fclose(in);
        return 0;
    }
    fprintf(stderr, "usage: sigtool s16tof32|garbage|oddbyte|silence|feed|lines|eas|morse ...\n");
    return 2;
}
