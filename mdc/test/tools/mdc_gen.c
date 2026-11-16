/*
 * mdc_gen: test signal generator for the FoxSDR "mdc" helper. NOT part of the shipped helper.
 * Licence: GPL-2.0-only (it links Matthew Kaufman's mdc_encode.c, upstream/).
 *
 * Writes mono 16-bit audio made of silences, MDC-1200 packets (made by upstream's encoder) and Fleetsync I messages
 * (made by the generator below, which is written from the decoder in upstream/fsync_decode.c: see README.md, "What has been
 * tried on real signals", for what that does and does not prove), optionally with white noise added.
 *
 *   mdc_gen --rate <Hz> [--noise <rms as a fraction of full scale>] [--seed <n>] [--gain <0..1>] (--raw f.raw | --wav f.wav)
 *           ITEM...
 *
 *   sil <seconds>
 *   mdc <op> <arg> <unit>                          single packet, numbers in hex
 *   mdc2 <op> <arg> <unit> <e0> <e1> <e2> <e3>     double packet
 *   mdcflip <op> <arg> <unit> <n>                  single packet with n bits of the coded block flipped (far apart)
 *   mdcbad <op> <arg> <unit>                       single packet whose coded block is wrecked: the CRC must fail
 *   fs1 <from_fleet> <from_unit> <to_unit> <cmd>   Fleetsync I, one block, fleet/unit in decimal, cmd in hex (even number)
 *   fs1t <from_fleet> <from_unit> <to_fleet> <to_unit> <text>   Fleetsync I data message (cmd 0x42) with a text payload
 *   fs1bad <from_fleet> <from_unit> <to_unit> <cmd>             like fs1 but with a wrong CRC
 *
 * further options: --f32 (raw file as 32-bit float samples), --truncate <samples> (cut the audio short), --odd-byte (append one stray byte to the raw file)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#include "mdc_encode.h"

#define MAXS (48000 * 120)

static short *out;
static size_t nout;
static unsigned rate = 16000;

static void put(short s)
{
    if (nout < MAXS)
        out[nout++] = s;
}

static void silence(double sec)
{
    size_t n = (size_t)(sec * rate + 0.5), i;
    for (i = 0; i < n; i++)
        put(0);
}

/* ---- MDC-1200 through upstream's encoder ---- */
static void put_encoded(mdc_encoder_t *e)
{
    mdc_sample_t buf[1024];
    int n;
    while ((n = mdc_encoder_get_samples(e, buf, 1024)) > 0) {
        int i;
        for (i = 0; i < n; i++)
            put(buf[i]);
    }
}

static void mdc_packet(unsigned op, unsigned arg, unsigned unit, int dbl, unsigned e0, unsigned e1, unsigned e2, unsigned e3,
                       int flip_n, int wreck)
{
    mdc_encoder_t *e = mdc_encoder_new((int)rate);
    if (!e) {
        fprintf(stderr, "mdc_gen: encoder\n");
        exit(1);
    }
    if (dbl)
        mdc_encoder_set_double_packet(e, (unsigned char)op, (unsigned char)arg, (unsigned short)unit, (unsigned char)e0,
                                      (unsigned char)e1, (unsigned char)e2, (unsigned char)e3);
    else
        mdc_encoder_set_packet(e, (unsigned char)op, (unsigned char)arg, (unsigned short)unit);
    /* the 14 coded block bytes of the first block start after the 12-byte leader; bits there are interleaved on the air */
    if (flip_n > 0) {
        int k;
        for (k = 0; k < flip_n; k++) {
            int bit = 5 + k * 47; /* far apart in the block: the code corrects each one */
            e->data[12 + (bit / 8) % 14] ^= (unsigned char)(1 << (bit % 8));
        }
    }
    if (wreck) {
        int k;
        for (k = 12; k < 12 + 14; k += 2)
            e->data[k] ^= 0xFF; /* far beyond what the error correction can repair */
    }
    put_encoded(e);
    free(e);
}

/* ---- Fleetsync I (from fsync_decode.c: sync 0xAAAA23EB, then 64-bit blocks of 6 data bytes + 15-bit CRC + parity) ---- */
static int fs_crc(unsigned word1, unsigned word2)
{ /* the same routine as fsync_common.c _fsync_crc, used here to FIND the field value that makes the check pass */
    int paritybit = 0, crcsr = 0, bit, cur, invert;
    for (bit = 0; bit < 48; bit++) {
        cur = bit < 32 ? (int)((word1 >> (31 - bit)) & 1) : (int)((word2 >> (31 - (bit - 32))) & 1);
        if (cur)
            paritybit ^= 1;
        invert = cur ^ (0x01 & (crcsr >> 15));
        if (invert)
            crcsr ^= 0x6815;
        crcsr <<= 1;
    }
    for (bit = 48; bit < 63; bit++) {
        cur = (int)((word2 >> (31 - (bit - 32))) & 1);
        if (cur)
            paritybit ^= 1;
    }
    crcsr ^= 0x0002;
    crcsr += paritybit;
    return crcsr & 0xffff;
}

static unsigned char fsbits[8192];
static size_t nfsbits;

static void fs_bits(unsigned v, int n)
{
    int i;
    for (i = n - 1; i >= 0; i--)
        if (nfsbits < sizeof fsbits)
            fsbits[nfsbits++] = (unsigned char)((v >> i) & 1);
}

static void fs_block(const unsigned char *six, int wrong_crc)
{
    unsigned w1 = ((unsigned)six[0] << 24) | ((unsigned)six[1] << 16) | ((unsigned)six[2] << 8) | six[3];
    unsigned w2 = ((unsigned)six[4] << 24) | ((unsigned)six[5] << 16);
    /* crc field: bits 48..62 CRC, bit 63 parity. Try the 15-bit field values until the decoder's own check passes. */
    unsigned f;
    for (f = 0; f < 0x10000; f++) {
        if (fs_crc(w1, w2 | f) == (int)f)
            break;
    }
    if (f == 0x10000) {
        fprintf(stderr, "mdc_gen: no valid crc field\n");
        exit(1);
    }
    if (wrong_crc)
        f ^= 0x0A50;
    w2 |= f;
    fs_bits(w1, 32);
    fs_bits(w2, 32);
}

/* Bits are sent as 1200 Hz (1) and 1800 Hz (0) at 1200 baud, phase continuous (MSK): a 1 is exactly one cycle and a 0 exactly one
 * and a half cycles, so the phase at every bit boundary is 0 or pi. The phase is computed from exact time, not accumulated per
 * sample, so rounding never moves a bit boundary off a zero crossing. */
static void fs_modulate(double gain)
{
    static double base[8192 + 1];
    const double two_pi = 6.283185307179586;
    size_t b, nsamp, k;
    base[0] = 0.0;
    for (b = 0; b < nfsbits; b++)
        base[b + 1] = base[b] + two_pi * (fsbits[b] ? 1200.0 : 1800.0) / 1200.0;
    nsamp = (size_t)((double)nfsbits / 1200.0 * (double)rate);
    for (k = 0; k < nsamp; k++) {
        double t = (double)k / (double)rate;
        size_t bit = (size_t)(t * 1200.0);
        double f, ph;
        if (bit >= nfsbits)
            bit = nfsbits - 1;
        f = fsbits[bit] ? 1200.0 : 1800.0;
        ph = base[bit] + two_pi * f * (t - (double)bit / 1200.0);
        put((short)lrint(gain * 32767.0 * sin(ph)));
    }
    nfsbits = 0;
}

static void fs_message(const unsigned char *msg, size_t len, int wrong_crc, double gain)
{
    size_t off;
    int i;
    nfsbits = 0;
    for (i = 0; i < 48; i++) /* dotting */
        fs_bits(i & 1 ? 0u : 1u, 1);
    fs_bits(0xAAAA, 16);
    fs_bits(0x23EB, 16);
    for (off = 0; off + 6 <= len; off += 6)
        fs_block(msg + off, wrong_crc && off == 0);
    fs_modulate(gain);
}

static void fs_header(unsigned char *m, int from_fleet, int from_unit, int to_fleet, int to_unit, int cmd, int fleetflag)
{
    unsigned fu = (unsigned)(from_unit - 999), tu = (unsigned)(to_unit - 999);
    m[0] = 0;
    m[1] = (unsigned char)(cmd | (fleetflag ? 1 : 0));
    m[2] = (unsigned char)(from_fleet - 99);
    m[3] = (unsigned char)(fu >> 4);
    m[4] = (unsigned char)(((fu & 0xf) << 4) | ((tu >> 8) & 0xf));
    m[5] = (unsigned char)(tu & 0xff);
    m[6] = (unsigned char)(to_fleet - 99);
}

/* ---- noise ---- */
static unsigned long long rng_state = 1;
static double urand(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (double)(rng_state >> 11) / 9007199254740992.0;
}
static double gauss(void)
{
    double s = 0;
    int i;
    for (i = 0; i < 12; i++)
        s += urand();
    return s - 6.0;
}

static unsigned hex(const char *s) { return (unsigned)strtoul(s, 0, 16); }

/* mdc_gen feed <file> <chunk bytes>: copy a file to standard output in small flushed writes (to test the pipe path) */
static int feed(const char *path, size_t chunk)
{
    FILE *f = fopen(path, "rb");
    unsigned char buf[65536];
    size_t n;
    if (!f || chunk == 0 || chunk > sizeof buf)
        return 1;
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    while ((n = fread(buf, 1, chunk, f)) > 0) {
        fwrite(buf, 1, n, stdout);
        fflush(stdout);
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    int i = 1;
    if (argc == 4 && !strcmp(argv[1], "feed"))
        return feed(argv[2], (size_t)strtoull(argv[3], 0, 10));
    double noise = 0.0, gain = 0.68;
    const char *rawf = 0, *wavf = 0;
    size_t trunc_at = (size_t)-1;
    int odd_byte = 0, as_f32 = 0;
    out = (short *)calloc(MAXS, sizeof(short));
    if (!out)
        return 1;
    /* options first */
    while (i < argc && !strncmp(argv[i], "--", 2)) {
        if (!strcmp(argv[i], "--rate") && i + 1 < argc)
            rate = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--noise") && i + 1 < argc)
            noise = atof(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            rng_state = (unsigned long long)strtoull(argv[++i], 0, 10) * 2654435761ull + 88172645463325252ull;
        else if (!strcmp(argv[i], "--gain") && i + 1 < argc)
            gain = atof(argv[++i]);
        else if (!strcmp(argv[i], "--truncate") && i + 1 < argc)
            trunc_at = (size_t)strtoull(argv[++i], 0, 10);
        else if (!strcmp(argv[i], "--f32"))
            as_f32 = 1;
        else if (!strcmp(argv[i], "--odd-byte"))
            odd_byte = 1;
        else if (!strcmp(argv[i], "--raw") && i + 1 < argc)
            rawf = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc)
            wavf = argv[++i];
        else {
            fprintf(stderr, "mdc_gen: bad option %s\n", argv[i]);
            return 1;
        }
        i++;
    }
    if (!rawf && !wavf) {
        fprintf(stderr, "mdc_gen: need --raw or --wav\n");
        return 1;
    }
    while (i < argc) {
        const char *w = argv[i++];
        if (!strcmp(w, "sil") && i < argc)
            silence(atof(argv[i++]));
        else if (!strcmp(w, "mdc") && i + 2 < argc) {
            mdc_packet(hex(argv[i]), hex(argv[i + 1]), hex(argv[i + 2]), 0, 0, 0, 0, 0, 0, 0);
            i += 3;
        } else if (!strcmp(w, "mdc2") && i + 6 < argc) {
            mdc_packet(hex(argv[i]), hex(argv[i + 1]), hex(argv[i + 2]), 1, hex(argv[i + 3]), hex(argv[i + 4]), hex(argv[i + 5]),
                       hex(argv[i + 6]), 0, 0);
            i += 7;
        } else if (!strcmp(w, "mdcflip") && i + 3 < argc) {
            mdc_packet(hex(argv[i]), hex(argv[i + 1]), hex(argv[i + 2]), 0, 0, 0, 0, 0, atoi(argv[i + 3]), 0);
            i += 4;
        } else if (!strcmp(w, "mdcbad") && i + 2 < argc) {
            mdc_packet(hex(argv[i]), hex(argv[i + 1]), hex(argv[i + 2]), 0, 0, 0, 0, 0, 0, 1);
            i += 3;
        } else if ((!strcmp(w, "fs1") || !strcmp(w, "fs1bad")) && i + 3 < argc) {
            unsigned char m[6];
            fs_header(m, atoi(argv[i]), atoi(argv[i + 1]), atoi(argv[i]), atoi(argv[i + 2]), (int)hex(argv[i + 3]), 0);
            fs_message(m, 6, !strcmp(w, "fs1bad"), gain);
            i += 4;
        } else if (!strcmp(w, "fs1t") && i + 4 < argc) {
            unsigned char m[96];
            const char *t = argv[i + 4];
            size_t tl = strlen(t), k, blocks;
            memset(m, 0, sizeof m);
            fs_header(m, atoi(argv[i]), atoi(argv[i + 1]), atoi(argv[i + 2]), atoi(argv[i + 3]), 0x42, 1);
            m[9] = (unsigned char)(tl >> 8);
            m[10] = (unsigned char)(tl & 0xff);
            for (k = 0; k < tl && k < 60; k++)
                m[11 + 6 * (k / 6) + 6 - (k % 6)] = (unsigned char)t[k];
            blocks = (11 + 6 * ((tl - 1) / 6 + 1) + 6 + 5) / 6;
            fs_message(m, blocks * 6, 0, gain);
            i += 5;
        } else {
            fprintf(stderr, "mdc_gen: bad item '%s'\n", w);
            return 1;
        }
    }
    if (noise > 0.0) {
        size_t k;
        for (k = 0; k < nout; k++) {
            double v = out[k] + noise * 32768.0 * gauss() / 1.0;
            if (v > 32767)
                v = 32767;
            if (v < -32768)
                v = -32768;
            out[k] = (short)lrint(v);
        }
    }
    if (trunc_at < nout)
        nout = trunc_at;
    if (rawf) {
        FILE *f = fopen(rawf, "wb");
        if (f && as_f32) { /* 32-bit float samples, full scale = 1.0 */
            size_t q;
            for (q = 0; q < nout; q++) {
                float v = (float)out[q] / 32768.0f;
                fwrite(&v, 4, 1, f);
            }
        }
        if (!f || (!as_f32 && fwrite(out, 2, nout, f) != nout) || (odd_byte && fputc(0x55, f) == EOF)) {
            fprintf(stderr, "mdc_gen: cannot write %s\n", rawf);
            return 1;
        }
        fclose(f);
    }
    if (wavf) {
        FILE *f = fopen(wavf, "wb");
        unsigned dlen = (unsigned)(nout * 2), v;
        unsigned short h;
        if (!f)
            return 1;
        fwrite("RIFF", 1, 4, f);
        v = 36 + dlen;
        fwrite(&v, 4, 1, f);
        fwrite("WAVEfmt ", 1, 8, f);
        v = 16;
        fwrite(&v, 4, 1, f);
        h = 1;
        fwrite(&h, 2, 1, f);
        h = 1;
        fwrite(&h, 2, 1, f);
        v = rate;
        fwrite(&v, 4, 1, f);
        v = rate * 2;
        fwrite(&v, 4, 1, f);
        h = 2;
        fwrite(&h, 2, 1, f);
        h = 16;
        fwrite(&h, 2, 1, f);
        fwrite("data", 1, 4, f);
        fwrite(&dlen, 4, 1, f);
        fwrite(out, 2, nout, f);
        fclose(f);
    }
    free(out);
    return 0;
}
