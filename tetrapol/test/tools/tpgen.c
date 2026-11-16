/*
 * tpgen.c -- test signal generator for the FoxSDR "tetrapol" GPL helper (not shipped with the helper)
 *
 * Makes TETRAPOL control-channel (and traffic-channel) bit streams and I/Q files for the CTest cases. The channel
 * coding below is an encoder written for this test from the same constants the upstream decoder uses
 * (tetrapol-kit lib/frame.c: scrambling sequence, interleaving tables, differential precoding indices, CRC-5 / CRC-3;
 * lib/bit_utils.c: the HDLC FCS; PAS 0001-2 section 6 and PAS 0001-3-3 section 7 are the references named there).
 * The upstream decoder is NOT used to make the signal. The upstream encoder (frame_encoder_encode in frame.c) is used
 * only by the "xcheck" command, which proves that both encoders produce the same bits.
 *
 *   tpgen cch  <out.bits> [scr] [band]          two superframes (400 frames) of a control channel
 *   tpgen tch  <out.bits> [scr]                 traffic channel: random "voice" frames and data frames
 *   tpgen damage <in.bits> <out.bits> <spec>    flip bits; spec = frame:bit,frame:bit,... or frame:* (whole frame)
 *   tpgen iq <in.bits> <out> <cf32|cs16> <rate> [snr_db] [cfo_hz] [timing_offset_symbols] [clock_ppm]
 *   tpgen xcheck                                compare with upstream frame_encoder_encode, exit 0 if identical
 *   tpgen garbage <seed> <nbytes> <out>         pseudo-random bytes
 *   tpgen fuzzmsg <seed> <count> <out.bits>     control channel whose messages are random bytes in valid frames
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation; version 2 of the License.
 */
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

/* The upstream tables (static in frame.c) and the upstream encoder, for the cross check. */
int log_global_lvl = -1;
#include "../../upstream/frame.c"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------------------------------------------------ */
/* bit stream                                                                                                    */

typedef struct {
    uint8_t *b;
    size_t n, cap;
    int carry; /* last "in" bit of the previous frame, for the differential encoding */
    int band;
    int scr;
    int frames;
} gen_t;

static void put_bit(gen_t *g, int v)
{
    if (g->n == g->cap) {
        g->cap = g->cap ? g->cap * 2 : 65536;
        g->b = (uint8_t *)realloc(g->b, g->cap);
        if (!g->b) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
    }
    g->b[g->n++] = (uint8_t)v;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* channel coding (PAS 0001-2 section 6)                                                                         */

/* CRC-5 of a data frame (x^5 + x^2 + 1), the 5 check bits follow the 69 information bits, MSB first. */
static void crc5_bits(const uint8_t *in, int n, uint8_t out[5])
{
    unsigned crc = 0;
    for (int i = 0; i < n; ++i) {
        unsigned inv = (in[i] ^ (crc >> 4)) & 1;
        crc = (crc << 1) & 0x1f;
        if (inv) {
            crc ^= 0x05;
        }
    }
    for (int i = 0; i < 5; ++i) {
        out[i] = (crc >> (4 - i)) & 1;
    }
}

/* CRC-3 of a voice frame (x^3 + x + 1), result inverted. */
static void crc3_bits(const uint8_t *in, int n, uint8_t out[3])
{
    unsigned crc = 0;
    for (int i = 0; i < n; ++i) {
        unsigned inv = (in[i] ^ (crc >> 2)) & 1;
        crc = (crc << 1) & 0x7;
        if (inv) {
            crc ^= 0x03;
        }
    }
    crc ^= 7;
    for (int i = 0; i < 3; ++i) {
        out[i] = (crc >> (2 - i)) & 1;
    }
}

/* Rate 1/2 convolutional code, constraint length 3, tail-biting over n information bits:
   c[2i] = u[i] ^ u[i-1] ^ u[i-2], c[2i+1] = u[i] ^ u[i-2]  (indices modulo n). */
static void conv_encode(const uint8_t *u, int n, uint8_t *c)
{
    for (int i = 0; i < n; ++i) {
        int u1 = u[(i + n - 1) % n], u2 = u[(i + n - 2) % n];
        c[2 * i] = u[i] ^ u1 ^ u2;
        c[2 * i + 1] = u[i] ^ u2;
    }
}

static const uint8_t *itab(int band, int voice, int j)
{
    if (band == TETRAPOL_BAND_VHF) {
        return interleave_data_VHF; /* VHF voice and data share one table */
    }
    return (voice && j >= 52) ? interleave_voice_UHF : interleave_data_UHF;
}

/* Put 152 coded bits on the air: interleave, (UHF) differential precode, scramble, header, differential encode. */
static void put_coded(gen_t *g, const uint8_t coded[152], int voice)
{
    uint8_t x[152], y[152];
    for (int j = 0; j < 152; ++j) {
        x[itab(g->band, voice, j)[j]] = coded[j];
    }
    if (g->band == TETRAPOL_BAND_UHF) {
        /* decoder: y[j] = x[j] ^ x[j - P[j]]; so x'[j] = y[j] ^ x'[j - P[j]] */
        memcpy(y, x, sizeof(y));
        for (int j = 1; j < 152; ++j) {
            y[j] = (uint8_t)(x[j] ^ y[j - diff_precod_UHF[j]]);
        }
        memcpy(x, y, sizeof(x));
    }
    if (g->scr) {
        for (int k = 0; k < 152; ++k) {
            x[k] ^= scramb_table[(k + g->scr) % 127];
        }
    }
    /* frame = 8 header bits (0x46, least significant bit first) + 152 bits; then S[n] = in[n] ^ in[n-1] */
    uint8_t in[160];
    for (int k = 0; k < 8; ++k) {
        in[k] = (0x46 >> k) & 1;
    }
    memcpy(in + 8, x, 152);
    for (int k = 0; k < 160; ++k) {
        put_bit(g, in[k] ^ g->carry);
        g->carry = in[k];
    }
    ++g->frames;
}

/* One data block: fn = 0..3 (the two FN bits), 64 payload bits, asb = 0. */
static void put_data_block(gen_t *g, int fn, const uint8_t payload[64])
{
    uint8_t info[76];
    memset(info, 0, sizeof(info));
    info[0] = 1; /* d */
    info[1] = fn & 1;
    info[2] = (fn >> 1) & 1;
    memcpy(info + 3, payload, 64);
    /* info[67], info[68] = asb, zero */
    crc5_bits(info, 69, info + 69);
    /* info[74], info[75] zero */
    uint8_t coded[152];
    conv_encode(info, 26, coded);
    conv_encode(info + 26, 50, coded + 52);
    put_coded(g, coded, 0);
}

/* A voice frame: d=0, 20 + 100 arbitrary bits (no codec: random bits stand in for the speech data), CRC-3. */
static void put_voice_block(gen_t *g, const uint8_t v1[20], const uint8_t v2[100])
{
    uint8_t info[26];
    memset(info, 0, sizeof(info));
    memcpy(info + 1, v1, 20);
    crc3_bits(info, 23, info + 23);
    uint8_t coded[152];
    conv_encode(info, 26, coded);
    memcpy(coded + 52, v2, 100);
    put_coded(g, coded, 1);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* HDLC frame (PAS 0001-3-3 section 7.4.1): address, command, data, FCS; blocks of 8 bytes, bits least significant first */

static unsigned fcs_register(const uint8_t *data, int nbits)
{
    /* the check of upstream bit_utils.c check_fcs(), written out again: the register must end as 0xffff */
    uint32_t crc = 0;
    for (int i = 0; i < 16; ++i) {
        crc = (crc << 1) | ((data[i / 8] >> (i % 8)) & 1);
    }
    crc ^= 0xffff;
    for (int i = 16; i < nbits; ++i) {
        crc = (crc << 1) | ((data[i / 8] >> (i % 8)) & 1);
        if (crc & 0x10000) {
            crc ^= 0x11021;
        }
    }
    return crc & 0xffff;
}

/* fill in the two FCS bytes at the end of `frame` (n bytes) so that the register ends as 0xffff:
   the register is affine in the 16 FCS bits; solve the 16x16 system over GF(2). */
static void set_fcs(uint8_t *frame, int n)
{
    frame[n - 2] = frame[n - 1] = 0;
    unsigned base = fcs_register(frame, n * 8);
    unsigned col[16];
    for (int t = 0; t < 16; ++t) {
        frame[n - 2] = frame[n - 1] = 0;
        if (t < 8) {
            frame[n - 2] = (uint8_t)(1 << t);
        } else {
            frame[n - 1] = (uint8_t)(1 << (t - 8));
        }
        col[t] = fcs_register(frame, n * 8) ^ base;
    }
    unsigned target = base ^ 0xffff, f = 0;
    /* Gaussian elimination: find F with XOR of col[t] over set bits of F equal to target */
    unsigned rows[16], rhs[16];
    for (int r = 0; r < 16; ++r) {
        rows[r] = 0;
        for (int t = 0; t < 16; ++t) {
            rows[r] |= ((col[t] >> r) & 1u) << t;
        }
        rhs[r] = (target >> r) & 1u;
    }
    int piv[16];
    for (int c = 0, r = 0; c < 16; ++c) {
        int p = -1;
        for (int k = r; k < 16; ++k) {
            if ((rows[k] >> c) & 1) {
                p = k;
                break;
            }
        }
        piv[c] = -1;
        if (p < 0) {
            continue;
        }
        unsigned tr = rows[p];
        rows[p] = rows[r];
        rows[r] = tr;
        unsigned th = rhs[p];
        rhs[p] = rhs[r];
        rhs[r] = th;
        for (int k = 0; k < 16; ++k) {
            if (k != r && ((rows[k] >> c) & 1)) {
                rows[k] ^= rows[r];
                rhs[k] ^= rhs[r];
            }
        }
        piv[c] = r++;
    }
    for (int c = 0; c < 16; ++c) {
        if (piv[c] >= 0 && rhs[piv[c]]) {
            f |= 1u << c;
        }
    }
    frame[n - 2] = (uint8_t)(f & 0xff);
    frame[n - 1] = (uint8_t)(f >> 8);
    if (fcs_register(frame, n * 8) != 0xffff) {
        fprintf(stderr, "internal error: FCS solve failed\n");
        exit(1);
    }
}

/* returns the HDLC frame length in bytes (multiple of 8) */
static int hdlc_build(uint8_t *out, unsigned z, unsigned y, unsigned x, uint8_t cmd, const uint8_t *payload, int plen)
{
    int total = 3 + plen + 2;
    total = (total + 7) / 8 * 8;
    memset(out, 0, (size_t)total);
    unsigned a = (z & 1) << 15 | (y & 7) << 12 | (x & 0xfff);
    out[0] = (uint8_t)(a >> 8);
    out[1] = (uint8_t)a;
    out[2] = cmd;
    memcpy(out + 3, payload, (size_t)plen);
    set_fcs(out, total);
    return total;
}

/* data blocks of an HDLC frame (bit i of the stream = bit i%8 of byte i/8, least significant bit first) */
static void put_hdlc(gen_t *g, const uint8_t *frame, int nbytes)
{
    int nb = nbytes / 8;
    uint8_t pay[9][64];
    for (int b = 0; b < nb; ++b) {
        for (int i = 0; i < 64; ++i) {
            int k = b * 64 + i;
            pay[b][i] = (frame[k / 8] >> (k % 8)) & 1;
        }
    }
    if (nb == 1) {
        put_data_block(g, 0, pay[0]);
        return;
    }
    if (nb == 2) { /* dual frame: FN 01, 11, no parity block */
        put_data_block(g, 1, pay[0]);
        put_data_block(g, 3, pay[1]);
        return;
    }
    /* multi frame: data blocks + XOR parity block; FN 01, 10, 11 ..., 10 (last data), 01 (parity) */
    for (int b = 0; b < nb; ++b) {
        int fn = (b == 0) ? 1 : (b == nb - 1) ? 2 : (b == 1) ? 2 : 3;
        put_data_block(g, fn, pay[b]);
    }
    uint8_t par[64];
    for (int i = 0; i < 64; ++i) {
        par[i] = 0;
        for (int b = 0; b < nb; ++b) {
            par[i] ^= pay[b][i];
        }
    }
    put_data_block(g, 1, par);
}

/* the stuffing frame sent when a channel has nothing to say (PAS 0001-3-3 7.4.1.9): table of upstream hdlc_frame.c */
static const uint8_t stuffing[40][5] = {
    {0x48, 0x57, 0x63, 0xe6, 0x90}, {0x90, 0xae, 0xc7, 0xcd, 0x20}, {0x21, 0x5d, 0x8f, 0x9a, 0x41},
    {0x42, 0xbb, 0x1f, 0x34, 0x82}, {0x85, 0x76, 0x3e, 0x69, 0x04}, {0x0a, 0xec, 0x7c, 0xd2, 0x09},
    {0x15, 0xd8, 0xf9, 0xa4, 0x12}, {0x2b, 0xb1, 0xf3, 0x48, 0x24},
};

static void put_stuffing(gen_t *g, int idx)
{
    uint8_t f[8];
    unsigned a = 7u << 12; /* z=0, y=7, x=0: "no station" */
    f[0] = (uint8_t)(a >> 8);
    f[1] = (uint8_t)a;
    f[2] = 0x03;
    memcpy(f + 3, stuffing[idx % 8], 5);
    uint8_t pay[64];
    for (int i = 0; i < 64; ++i) {
        pay[i] = (f[i / 8] >> (i % 8)) & 1;
    }
    put_data_block(g, 0, pay);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* messages (PAS 0001-3-2 4.4, bit layouts as read by the upstream decoder tsdu.c)                               */

static int msg_system_info(uint8_t *m, int bch, unsigned cpt)
{
    memset(m, 0, 17);
    m[0] = 0x90;
    m[1] = (uint8_t)(0 | 1 << 1 | bch << 4 | 0 << 5); /* exp 0, roam 1 (home only), bch, mode 0 normal */
    m[2] = (uint8_t)(1 | 0 << 1 | 0 << 2 | 1 << 6);   /* dc 1, sim 0, mux_type 0, atta 1, ecch 0 */
    m[3] = 33;                                        /* country code */
    m[4] = (uint8_t)(2 | 5 << 4);                     /* version 2, network 5 */
    m[5] = (uint8_t)(17 | 0 << 6);                    /* loc_id 17, mode 0 */
    m[6] = 42;                                        /* bn_id */
    m[7] = 12;                                        /* cell id format 0: bs_id 12 ... */
    m[8] = (uint8_t)(3 << 4 | 0x1);                   /* ... rsw_id 3; cell_bn r1 = 1 */
    m[9] = (uint8_t)(0x2 << 4 | 0x3);                 /* r2 = 2, r3 = 3 */
    m[10] = 77;                                       /* u_ch_scrambling */
    m[11] = (uint8_t)(3 << 5 | 9);                    /* tx_max 3, radio_link_timeout 9 */
    m[12] = (uint8_t)(6 << 4 | 4);                    /* pwr_tx_adjust 6, rx_lev_access 4 */
    m[13] = 0x5a;                                     /* system time */
    m[14] = (uint8_t)(0x2 << 4 | 0x1);                /* min_reg_class 2, min_service_class 1 */
    m[15] = (uint8_t)(cpt >> 8 & 0xf);
    m[16] = (uint8_t)cpt;
    return 17;
}

static int msg_neighbouring_cell(uint8_t *m)
{
    int n = 0;
    m[n++] = 0x94;
    m[n++] = 2;    /* two adjacent cells */
    m[n++] = 0;    /* ccr_param */
    m[n++] = 0x3a; /* bn_nb 3, channel 0xa */
    m[n++] = 0xbc; /* channel 0xabc */
    m[n++] = (uint8_t)(5 | 1 << 6 | 1 << 7); /* rxlev 5, same loc, same bn */
    m[n++] = 0x40; /* bn_nb 4, channel 0x012 */
    m[n++] = 0x12;
    m[n++] = (uint8_t)(9 | 1 << 4); /* rxlev 9, experimental */
    m[n++] = 0x02; /* IEI cell id list */
    m[n++] = 4;
    m[n++] = 7; /* format 0, bs 7 */
    m[n++] = 0x20; /* rsw 2 */
    m[n++] = 8;
    m[n++] = 0x30; /* rsw 3 */
    return n;
}

static int msg_registration_ack(uint8_t *m)
{
    memset(m, 0, 16);
    m[0] = 0x22;
    m[1] = 1;    /* complete_reg */
    m[2] = 20;   /* rt_min_activity */
    m[3] = 0x05; /* rt_status: tra, chg */
    /* host address: RFSI 250-3-12-0123456? nibbles: li=0, cna=1 */
    m[4] = (uint8_t)(1 << 4 | 0x2);
    m[5] = 0x50;
    m[6] = 0x31;
    m[7] = 0x20;
    m[8] = 0x12;
    m[9] = 30;   /* rt_min_registration */
    m[10] = 5;   /* tlr_value */
    m[11] = 0x0a; /* rt_data_info: polling 2, iab */
    m[12] = 0x12; /* group_id 0x123 */
    m[13] = 0x30;
    m[14] = 0x82; /* IEI coverage id */
    m[15] = 9;
    return 16;
}

static int msg_registration_nak(uint8_t *m)
{
    memset(m, 0, 10);
    m[0] = 0x21;
    m[1] = 4; /* cause */
    m[2] = (uint8_t)(1 << 4 | 0x2);
    m[3] = 0x50;
    m[4] = 0x31;
    m[5] = 0x20;
    m[6] = 0x12;
    m[7] = 42;
    m[8] = 12;
    m[9] = 0x30;
    return 10;
}

static int msg_group_activation(uint8_t *m, int with_tti)
{
    int n = 0;
    m[n++] = 0x55;
    m[n++] = (uint8_t)(1 << 6 | 1 << 4 | 0x4); /* hook 1, type 1, group id 0x4c3 */
    m[n++] = 0xc3;
    m[n++] = 5;    /* coverage id */
    m[n++] = 0x01; /* channel 0x1f4 */
    m[n++] = 0xf4;
    m[n++] = 21;   /* u_ch_scrambling */
    m[n++] = 22;   /* d_ch_scrambling */
    m[n++] = 0x03; /* key type 0, index 3 */
    if (with_tti) {
        m[n++] = 0x05; /* IEI TTI */
        m[n++] = 0x00; /* filler as read by the decoder: data[10] */
        m[n++] = 0x00;
        m[n - 2] = 0x71; /* z 0, y 7, x 0x1ab */
        m[n - 1] = 0xab;
    }
    return n;
}

static int msg_group_paging(uint8_t *m)
{
    m[0] = 0x5b;
    m[1] = (uint8_t)(2 << 4 | 0x0);
    m[2] = 0x7f; /* group 0x07f */
    m[3] = 3;
    m[4] = 0x00;
    return 5;
}

static int msg_group_idle(uint8_t *m)
{
    m[0] = 0x58;
    m[1] = 2;
    return 2;
}

static int msg_call_setup(uint8_t *m)
{
    int n = 0;
    m[n++] = 0x32;
    m[n++] = (uint8_t)(1 << 4 | 0x3); /* RFSI */
    m[n++] = 0x21;
    m[n++] = 0x45;
    m[n++] = 0x67;
    m[n++] = 0x89;
    m[n++] = 0x84; /* IEI add_setup_param */
    m[n++] = (uint8_t)(2 | 1 << 5);
    return n;
}

static int msg_connect_dch(uint8_t *m)
{
    m[0] = 0x60;
    m[1] = 1;
    m[2] = 0x03;
    m[3] = 0x20;
    m[4] = 31;
    m[5] = 32;
    return 6;
}

static int msg_group_list(uint8_t *m)
{
    int n = 0;
    m[n++] = 0x92;
    m[n++] = (uint8_t)(1 << 5 | 1); /* revision 1, dc umbrella */
    m[n++] = (uint8_t)(1 << 6 | 4); /* index list mode 1, index 4 */
    m[n++] = (uint8_t)(1 << 6 | 1); /* talk groups: 1 */
    m[n++] = 7;                     /* coverage id */
    m[n++] = 0;
    m[n++] = 0x01;
    m[n++] = 0x23;
    m[n++] = (uint8_t)(2 << 6 | 1); /* emergency: 1 */
    m[n++] = 5;                     /* bs 5 */
    m[n++] = 0x40;                  /* rsw 4 */
    m[n++] = 0x00;                  /* end */
    return n;
}

static int msg_datagram(uint8_t *m)
{
    /* a short-data style message: its bytes must never appear in the output */
    int n = 0;
    m[n++] = 0x4a;
    m[n++] = 0x20;
    m[n++] = 1;
    m[n++] = 0;
    m[n++] = 0;
    for (int i = 0; i < 6; ++i) {
        m[n++] = (uint8_t)(0xd0 + i);
    }
    return n;
}

static int msg_authentication(uint8_t *m)
{
    memset(m, 0, 16);
    m[0] = 0x13;
    m[1] = 0x12; /* key type 1, index 2 */
    for (int i = 0; i < 8; ++i) {
        m[2 + i] = (uint8_t)(0xa0 + i); /* challenge: must never appear in the output */
    }
    return 16;
}

static int msg_unknown(uint8_t *m)
{
    m[0] = 0x7e;
    m[1] = 0x11;
    m[2] = 0x22;
    return 3;
}

/* UI TPDU around a TSDU: [ext 0, seg 0, prio, id_tsap] [len] [tsdu], HDLC command UI */
static int ui_frame(uint8_t *out, unsigned z, unsigned y, unsigned x, int prio, int tsap, const uint8_t *tsdu, int tlen)
{
    uint8_t p[80];
    p[0] = (uint8_t)((prio & 3) << 4 | (tsap & 15));
    p[1] = (uint8_t)tlen;
    memcpy(p + 2, tsdu, (size_t)tlen);
    return hdlc_build(out, z, y, x, 0x03, p, tlen + 2);
}

/* information frame with an FCR TPDU (a connection with an embedded TSDU) */
static int info_frame(uint8_t *out, unsigned z, unsigned y, unsigned x, int ns, int ref, int tsap, const uint8_t *tsdu, int tlen)
{
    uint8_t p[80];
    p[0] = 0x30; /* ext 0, seg 0, d 1, code 0x10 (FCR) */
    p[1] = (uint8_t)((ref & 15) << 4 | (tsap & 15));
    p[2] = (uint8_t)tlen;
    memcpy(p + 3, tsdu, (size_t)tlen);
    return hdlc_build(out, z, y, x, (uint8_t)((ns & 7) << 1), p, tlen + 3);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* channels                                                                                                      */

static void put_pch(gen_t *g, const unsigned *tti, int n)
{
    uint8_t f[16];
    memset(f, 0, sizeof(f));
    f[0] = 0x05; /* activation bitmap (8 bytes) */
    f[3] = 0x80;
    for (int i = 0; i < 4; ++i) {
        unsigned a = (i < n) ? (7u << 12 | tti[i]) : (7u << 12);
        f[8 + 2 * i] = (uint8_t)(a >> 8);
        f[9 + 2 * i] = (uint8_t)a;
    }
    uint8_t pay[2][64];
    for (int b = 0; b < 2; ++b) {
        for (int i = 0; i < 64; ++i) {
            int k = b * 64 + i;
            pay[b][i] = (f[k / 8] >> (k % 8)) & 1;
        }
    }
    put_data_block(g, 1, pay[0]);
    put_data_block(g, 3, pay[1]);
}

/* kind: 0 = ack of a TTI, 1 = noise, 2 = collision */
static void put_rch(gen_t *g, const int *kind, const unsigned *tti, int n)
{
    uint8_t f[8];
    memset(f, 0, sizeof(f));
    for (int i = 0; i < 3; ++i) {
        unsigned a;
        if (i >= n) {
            a = 7u << 12;
        } else if (kind[i] == 0) {
            a = 7u << 12 | tti[i];
        } else {
            a = 1u << 15 | (kind[i] == 1 ? 4u : 5u) << 12;
        }
        f[2 * i] = (uint8_t)(a >> 8);
        f[2 * i + 1] = (uint8_t)a;
    }
    set_fcs(f, 8);
    uint8_t pay[64];
    for (int i = 0; i < 64; ++i) {
        pay[i] = (f[i / 8] >> (i % 8)) & 1;
    }
    put_data_block(g, 0, pay);
}

/* A control channel of two superframes (frame numbers 0..399): system info, paging, acks, signalling messages. */
static void make_cch(gen_t *g, int variant)
{
    uint8_t m[80], hf[128];
    int sdch_slot[400];
    memset(sdch_slot, 0, sizeof(sdch_slot));
    enum { MSGS = 12 };
    /* the messages, placed on fixed SDCH frames; each uses its own number of frames */
    struct {
        int frame;
        int kind;
    } plan[MSGS] = {
        {10, 0}, {20, 1}, {30, 2}, {45, 3}, {54, 4}, {60, 5}, {70, 6}, {80, 7},
        {110, 8}, {120, 9}, {130, 10}, {140, 11},
    };
    int next_idle = 0;
    int pi = 0;
    /* a few idle frames in front, so that synchronisation (and, for I/Q, the timing loop) is settled before frame 0 */
    for (int k = 0; k < 8; ++k) {
        put_stuffing(g, next_idle++);
    }
    for (int f = 0; f < 400; ) {
        int fn = f % 200, fm = f % 100;
        if (fm <= 3) {
            int cpt = (f / 200) * 2 + (f % 200 >= 100);
            msg_system_info(m, (f % 200) >= 100, (unsigned)(variant ? 0x321 : 0x100) + cpt);
            uint8_t p[19];
            p[0] = 0x00;
            p[1] = 17;
            memcpy(p + 2, m, 17);
            int n = hdlc_build(hf, 0, 7, 0xfff, 0x03, p, 19);
            put_hdlc(g, hf, n);
            f += n / 8 + 1; /* 3 data blocks + parity */
            continue;
        }
        (void)fn;
        if (fm == 98 || fm == 99) {
            unsigned tti[2] = {0x123 + (unsigned)(f / 100), 0x456};
            put_pch(g, tti, 2);
            f += 2;
            continue;
        }
        if (fm % 25 == 14) {
            int kind[3] = {0, 1, 2};
            unsigned tti[3] = {0x321, 0, 0};
            put_rch(g, kind, tti, (f / 25) % 2 ? 3 : 1);
            ++f;
            continue;
        }
        if (pi < MSGS && f >= plan[pi].frame) {
            int n = 0, nbytes = 0;
            int kind = plan[pi].kind;
            ++pi;
            switch (kind) {
                case 0: n = msg_registration_ack(m); nbytes = ui_frame(hf, 0, 7, 0x321, 0, 1, m, n); break;
                case 1: n = msg_group_activation(m, 1); nbytes = ui_frame(hf, 0, 7, 0x321, 1, 2, m, n); break;
                case 2: n = msg_group_paging(m); nbytes = ui_frame(hf, 0, 7, 0x322, 0, 2, m, n); break;
                case 3: n = msg_call_setup(m); nbytes = info_frame(hf, 0, 7, 0x321, 0, 3, 1, m, n); break;
                case 4: n = msg_neighbouring_cell(m); nbytes = ui_frame(hf, 0, 7, 0xfff, 0, 1, m, n); break;
                case 5: n = msg_datagram(m); nbytes = ui_frame(hf, 0, 7, 0x321, 0, 4, m, n); break;
                case 6: n = msg_authentication(m); nbytes = ui_frame(hf, 0, 7, 0x321, 0, 1, m, n); break;
                case 7: n = msg_unknown(m); nbytes = ui_frame(hf, 0, 7, 0x333, 0, 1, m, n); break;
                case 8: n = msg_registration_nak(m); nbytes = ui_frame(hf, 0, 7, 0x444, 0, 1, m, n); break;
                case 9: n = msg_connect_dch(m); nbytes = ui_frame(hf, 0, 7, 0x321, 0, 1, m, n); break;
                case 10: n = msg_group_list(m); nbytes = ui_frame(hf, 0, 7, 0xfff, 0, 1, m, n); break;
                default: n = msg_group_idle(m); nbytes = ui_frame(hf, 0, 7, 0x555, 0, 1, m, n); break;
            }
            int blocks = nbytes / 8;
            int need = blocks <= 2 ? blocks : blocks + 1;
            /* the frames of one message must not run into a PCH / RCH / BCH slot */
            int ok = 1;
            for (int k = 0; k < need; ++k) {
                int q = (f + k) % 100;
                if (q <= 3 || q >= 98 || q % 25 == 14) {
                    ok = 0;
                }
            }
            if (ok) {
                put_hdlc(g, hf, nbytes);
                f += need;
            } else {
                --pi; /* try again later */
                put_stuffing(g, next_idle++);
                ++f;
            }
            continue;
        }
        put_stuffing(g, next_idle++);
        ++f;
    }
}

/* ------------------------------------------------------------------------------------------------------------ */

static uint32_t rng_state = 12345;
static uint32_t rnd(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

static void make_tch(gen_t *g)
{
    uint8_t v1[20], v2[100], pay[64];
    for (int f = 0; f < 120; ++f) {
        if (f % 10 == 9) { /* a data frame inside the call */
            for (int i = 0; i < 64; ++i) {
                pay[i] = (uint8_t)(rnd() & 1);
            }
            put_data_block(g, 0, pay);
        } else {
            for (int i = 0; i < 20; ++i) {
                v1[i] = (uint8_t)(rnd() & 1);
            }
            for (int i = 0; i < 100; ++i) {
                v2[i] = (uint8_t)(rnd() & 1);
            }
            put_voice_block(g, v1, v2);
        }
    }
}

/* control channel whose messages are random bytes (valid framing, valid FCS): robustness */
static void make_fuzzmsg(gen_t *g, int count)
{
    uint8_t m[64], hf[128];
    int idle = 0;
    for (int i = 0; i < count; ++i) {
        int n = 1 + (int)(rnd() % 40);
        for (int k = 0; k < n; ++k) {
            m[k] = (uint8_t)rnd();
        }
        if (rnd() % 3 == 0) {
            static const uint8_t cod[] = {0x90, 0x94, 0x92, 0x93, 0x22, 0x21, 0x55, 0x5b, 0x32, 0x34, 0x35, 0x60, 0x61,
                                          0x13, 0x16, 0x4a, 0x46, 0x8b, 0x25, 0x23, 0x3e, 0x96, 0x57, 0x59, 0x83, 0x2f};
            m[0] = cod[rnd() % sizeof(cod)];
        }
        int nbytes;
        switch (rnd() % 4) {
            case 0: nbytes = ui_frame(hf, 0, 7, 0x321 + rnd() % 5, rnd() & 3, rnd() & 15, m, n); break;
            case 1: nbytes = info_frame(hf, 0, 7, 0x321 + rnd() % 5, (int)(rnd() & 7), (int)(rnd() & 15), (int)(rnd() & 15), m, n); break;
            case 2: { /* raw random HDLC payload */
                uint8_t p[40];
                for (int k = 0; k < n && k < 40; ++k) {
                    p[k] = (uint8_t)rnd();
                }
                nbytes = hdlc_build(hf, rnd() & 1, rnd() & 7, rnd() & 0xfff, (uint8_t)rnd(), p, n < 40 ? n : 40);
                break;
            }
            default: nbytes = ui_frame(hf, 0, 7, 0xfff, 0, 1, m, n); break;
        }
        put_hdlc(g, hf, nbytes);
        put_stuffing(g, idle++);
    }
}

/* ------------------------------------------------------------------------------------------------------------ */
/* damage: flip bits of chosen frames                                                                            */

static int cmd_damage(const char *in, const char *out, const char *spec)
{
    FILE *f = fopen(in, "rb");
    if (!f) {
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = (uint8_t *)malloc((size_t)n);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) {
        return 1;
    }
    fclose(f);
    char *s = _strdup(spec), *tok = strtok(s, ",");
    while (tok) {
        int fr, bit;
        if (sscanf(tok, "%d:%d", &fr, &bit) == 2 && (long)fr * 160 + 8 + bit < n) {
            b[(long)fr * 160 + 8 + bit] ^= 1;
        } else if (sscanf(tok, "%d:*", &fr) == 1) {
            for (int k = 0; k < 152; k += 1) {
                if ((long)fr * 160 + 8 + k < n && (rnd() & 1)) {
                    b[(long)fr * 160 + 8 + k] ^= 1;
                }
            }
        } else if (sscanf(tok, "%d:s%d", &fr, &bit) == 2) { /* flip a bit of the sync pattern */
            b[(long)fr * 160 + 1 + bit] ^= 1;
        }
        tok = strtok(NULL, ",");
    }
    f = fopen(out, "wb");
    if (!f || fwrite(b, 1, (size_t)n, f) != (size_t)n) {
        return 1;
    }
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* GMSK modulator: bits as the decoder reads them -> I/Q                                                         */

static double gauss_pulse(double t, double T, double sigma)
{
    double a = 1.0 / (sigma * 1.4142135623730951);
    return 0.5 * (erf((t + T / 2) * a) - erf((t - T / 2) * a));
}

static double gauss_noise(void)
{
    double u1 = (rnd() + 1.0) / 16777217.0, u2 = (rnd() + 0.5) / 16777217.0;
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

static int cmd_iq(const char *in, const char *out, const char *fmt, double rate, double snr_db, double cfo, double timing, double ppm)
{
    FILE *f = fopen(in, "rb");
    if (!f) {
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long nb = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = (uint8_t *)malloc((size_t)nb);
    if (!d || fread(d, 1, (size_t)nb, f) != (size_t)nb) {
        return 1;
    }
    fclose(f);
    /* transmitter: s[n] = s[n-1] ^ d[n] (differential encoder), NRZ +-1, Gaussian BT 0.25, h = 0.5 */
    int *s = (int *)malloc(sizeof(int) * ((size_t)nb + 16));
    int prev = 0;
    for (long i = 0; i < nb; ++i) {
        prev ^= d[i] & 1;
        s[i] = prev ? 1 : -1;
    }
    const double T = (1.0 / 8000.0) * (1.0 + ppm * 1e-6);
    const double sigma = sqrt(log(2.0)) / (2.0 * M_PI * 0.25 / T);
    const double dt = 1.0 / rate;
    const double t0 = 3.0 * T + timing * T; /* start with a few symbols of lead-in */
    long nsamp = (long)((nb + 8) * T * rate);
    double phase = 0.0;
    FILE *o = fopen(out, "wb");
    if (!o) {
        return 1;
    }
    double noise_sigma = 0.0;
    if (snr_db < 200.0) {
        noise_sigma = sqrt(0.5 / pow(10.0, snr_db / 10.0)); /* unit-power signal, noise per I and per Q */
    }
    int is16 = !strcmp(fmt, "cs16");
    for (long n = 0; n < nsamp; ++n) {
        double t = (double)n * dt - t0; /* symbol k is centred at k*T */
        long k0 = (long)floor(t / T);
        double fsum = 0.0;
        for (long k = k0 - 3; k <= k0 + 4; ++k) {
            if (k < 0 || k >= nb) {
                continue;
            }
            fsum += s[k] * gauss_pulse(t - (double)k * T, T, sigma);
        }
        phase += (M_PI * 0.5 / T) * fsum * dt;
        double re = cos(phase + 2.0 * M_PI * cfo * (double)n * dt), im = sin(phase + 2.0 * M_PI * cfo * (double)n * dt);
        if (noise_sigma > 0) {
            re += noise_sigma * gauss_noise();
            im += noise_sigma * gauss_noise();
        }
        if (is16) {
            int16_t v[2];
            double a = re * 12000.0, b = im * 12000.0;
            v[0] = (int16_t)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
            v[1] = (int16_t)(b > 32767 ? 32767 : b < -32768 ? -32768 : b);
            fwrite(v, 2, 2, o);
        } else {
            float v[2] = {(float)re, (float)im};
            fwrite(v, 4, 2, o);
        }
    }
    fclose(o);
    return 0;
}

/* ------------------------------------------------------------------------------------------------------------ */

static int cmd_xcheck(void)
{
    /* data frames and voice frames, both bands, a few scrambling constants: this generator against upstream's */
    int bad = 0, total = 0;
    for (int band = 1; band <= 2; ++band) {
        for (int scr = 0; scr < 128; scr += 19) {
            for (int voice = 0; voice < 2; ++voice) {
                gen_t g;
                memset(&g, 0, sizeof(g));
                g.band = band;
                g.scr = scr;
                frame_encoder_t *fe = frame_encoder_create(band, scr, DIR_DOWNLINK);
                for (int k = 0; k < 8; ++k) {
                    uint8_t pay[64], v1[20], v2[100];
                    frame_t fr;
                    memset(&fr, 0, sizeof(fr));
                    uint8_t ref[20];
                    memset(ref, 0, sizeof(ref));
                    size_t before = g.n;
                    if (!voice) {
                        int fn = k & 3;
                        for (int i = 0; i < 64; ++i) {
                            pay[i] = (uint8_t)(rnd() & 1);
                        }
                        put_data_block(&g, fn, pay);
                        fr.fr_type = FRAME_TYPE_DATA;
                        fr.data.data[0] = fn & 1;
                        fr.data.data[1] = (fn >> 1) & 1;
                        memcpy(fr.data.data + 2, pay, 64);
                    } else {
                        for (int i = 0; i < 20; ++i) {
                            v1[i] = (uint8_t)(rnd() & 1);
                        }
                        for (int i = 0; i < 100; ++i) {
                            v2[i] = (uint8_t)(rnd() & 1);
                        }
                        put_voice_block(&g, v1, v2);
                        fr.fr_type = FRAME_TYPE_VOICE;
                        memcpy(fr.voice.voice1, v1, 20);
                        memcpy(fr.voice.voice2, v2, 100);
                    }
                    frame_encoder_encode(fe, ref, &fr);
                    /* the upstream bytes: bit k of the frame is bit k%8 (LSB first) of byte k/8 */
                    int cbad = 0;
                    for (int i = 0; i < 160; ++i) {
                        int ub = (ref[i / 8] >> (i % 8)) & 1;
                        ++total;
                        if (ub != g.b[before + (size_t)i]) {
                            ++bad;
                            ++cbad;
                            if (getenv("XCHECK_VERBOSE")) {
                                printf("    bit %d mine %d upstream %d\n", i, g.b[before + (size_t)i], ub);
                            }
                        }
                    }
                    if (cbad) {
                        printf("  band %d scr %d %s frame %d: %d bits differ\n", band, scr, voice ? "voice" : "data", k, cbad);
                    }
                }
                frame_encoder_destroy(fe);
                free(g.b);
            }
        }
    }
    printf("xcheck: %d bits compared, %d different\n", total, bad);
    return bad ? 1 : 0;
}

static int write_bits(const gen_t *g, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(g->b, 1, g->n, f) != g->n) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: tpgen cch|tch|damage|iq|xcheck|garbage|fuzzmsg ...\n");
        return 2;
    }
    const char *cmd = argv[1];
    gen_t g;
    memset(&g, 0, sizeof(g));
    g.band = TETRAPOL_BAND_UHF;
    if (!strcmp(cmd, "cch") && argc >= 3) {
        g.scr = argc > 3 ? atoi(argv[3]) : 67;
        if (argc > 4 && !strcmp(argv[4], "vhf")) {
            g.band = TETRAPOL_BAND_VHF;
        }
        make_cch(&g, 0);
        return write_bits(&g, argv[2]);
    }
    if (!strcmp(cmd, "tch") && argc >= 3) {
        g.scr = argc > 3 ? atoi(argv[3]) : 33;
        make_tch(&g);
        return write_bits(&g, argv[2]);
    }
    if (!strcmp(cmd, "fuzzmsg") && argc >= 5) {
        rng_state = (uint32_t)atoi(argv[2]);
        g.scr = 5;
        make_fuzzmsg(&g, atoi(argv[3]));
        return write_bits(&g, argv[4]);
    }
    if (!strcmp(cmd, "damage") && argc >= 5) {
        return cmd_damage(argv[2], argv[3], argv[4]);
    }
    if (!strcmp(cmd, "iq") && argc >= 6) {
        return cmd_iq(argv[2], argv[3], argv[4], atof(argv[5]), argc > 6 ? atof(argv[6]) : 1000.0,
                      argc > 7 ? atof(argv[7]) : 0.0, argc > 8 ? atof(argv[8]) : 0.0, argc > 9 ? atof(argv[9]) : 0.0);
    }
    if (!strcmp(cmd, "xcheck")) {
        return cmd_xcheck();
    }
    if (!strcmp(cmd, "feed") && argc >= 4) { /* file -> stdout in small writes, like a slow pipe */
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        FILE *f = fopen(argv[2], "rb");
        if (!f) {
            return 1;
        }
        size_t chunk = (size_t)atol(argv[3]);
        uint8_t *buf = (uint8_t *)malloc(chunk);
        size_t r;
        while (buf && (r = fread(buf, 1, chunk, f)) > 0) {
            fwrite(buf, 1, r, stdout);
            fflush(stdout);
        }
        fclose(f);
        return 0;
    }
    if (!strcmp(cmd, "garbage") && argc >= 5) {
        rng_state = (uint32_t)atoi(argv[2]);
        long n = atol(argv[3]);
        FILE *f = fopen(argv[4], "wb");
        if (!f) {
            return 1;
        }
        for (long i = 0; i < n; ++i) {
            fputc((int)(rnd() & 0xff), f);
        }
        fclose(f);
        return 0;
    }
    fprintf(stderr, "tpgen: bad command line\n");
    return 2;
}
