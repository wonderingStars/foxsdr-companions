/*
 * gen.cpp - test signal generator and checker for the FoxSDR "sondes" GPL helper.
 *
 * Licence: GNU GPL v3 (see ../LICENSE). Uses the Reed-Solomon encoder and the frame layouts of rs1729/RS
 * (upstream/bch_ecc_mod.c and the constants in the decoders); see ../UPSTREAM.md.
 *
 *   sondes-gen <type> --out <file.raw> --truth <file.jsonl> [--rate 48000] [--frames N] [--noise A] [--f32]
 *   sondes-gen check <truth.jsonl> <helper-output.jsonl>
 *
 * It builds the frames a sonde would send (from the upstream decoders' field offsets and scaling, so the generator
 * and the decoder agree on the format but are different code), puts them through the sonde's channel code
 * (LMS6: RS(255,223) + the rate 1/2 convolutional code; Mk IIa: 8N1 bytes), and writes the FM-demodulated audio a
 * discriminator would give: NRZ bits, low-pass filtered, plus optional deterministic noise.
 * "check" compares the helper's sonde events with the truth file (serial, frame, lat, lon, alt, speeds).
 */
#define _USE_MATH_DEFINES
#include <cmath>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>

extern "C++" {
typedef unsigned char ui8_t;
}
#include "bch_ecc_mod.h"

static const double B60B60 = (1 << 30) / 90.0;

/* ---------------------------------------------------------------- deterministic noise */
static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static double rnd() {            /* uniform 0..1 */
    g_rng = g_rng * 6364136223846793005ull + 1442695040888963407ull;
    return (double)(g_rng >> 11) / 9007199254740992.0;
}
static double gauss() {
    double s = 0;
    for (int i = 0; i < 12; i++) s += rnd();
    return s - 6.0;
}

/* ---------------------------------------------------------------- bits -> audio */
static std::vector<float> modulate(const std::vector<uint8_t> &bits, double baud, int rate, double amp, double noise,
                                   bool invert, double lead_s, double tail_s) {
    double sps = rate / baud;
    size_t nlead = (size_t)(lead_s * rate), ntail = (size_t)(tail_s * rate);
    size_t nbits = (size_t)(bits.size() * sps);
    std::vector<float> raw(nbits);
    for (size_t n = 0; n < nbits; n++) {
        size_t k = (size_t)(n / sps);
        if (k >= bits.size()) k = bits.size() - 1;
        float v = bits[k] ? 1.0f : -1.0f;
        raw[n] = invert ? -v : v;
    }
    /* two box filters of about 0.9 bit: smooth transitions like a discriminator after the IF filter */
    int w = (int)(sps * 0.9 + 0.5);
    if (w < 1) w = 1;
    for (int pass = 0; pass < 2; pass++) {
        std::vector<float> o(raw.size());
        double acc = 0;
        for (size_t n = 0; n < raw.size(); n++) {
            acc += raw[n];
            if (n >= (size_t)w) acc -= raw[n - w];
            o[n] = (float)(acc / w);
        }
        raw.swap(o);
    }
    std::vector<float> out;
    out.reserve(nlead + raw.size() + ntail);
    for (size_t n = 0; n < nlead; n++) out.push_back((float)(noise * gauss()));
    for (size_t n = 0; n < raw.size(); n++) out.push_back((float)(amp * raw[n] + noise * gauss()));
    for (size_t n = 0; n < ntail; n++) out.push_back((float)(noise * gauss()));
    return out;
}

static bool write_audio(const char *path, const std::vector<float> &a, bool f32) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    if (f32) {
        fwrite(a.data(), 4, a.size(), f);
    } else {
        std::vector<int16_t> s(a.size());
        for (size_t i = 0; i < a.size(); i++) {
            double v = a[i] * 32767.0;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            s[i] = (int16_t)lrint(v);
        }
        fwrite(s.data(), 2, s.size(), f);
    }
    fclose(f);
    return true;
}

static unsigned crc16_0(const ui8_t *b, int len) {
    unsigned rem = 0;
    for (int i = 0; i < len; i++) {
        rem ^= (unsigned)b[i] << 8;
        for (int j = 0; j < 8; j++) rem = (rem & 0x8000) ? ((rem << 1) ^ 0x1021) & 0xFFFF : (rem << 1) & 0xFFFF;
    }
    return rem;
}

static void put32(ui8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void put24(ui8_t *p, int32_t v) { p[0] = (v >> 16) & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = v & 0xFF; }

struct Truth {
    std::string sonde, subtype, serial;
    int frame;
    double lat = 0, lon = 0, alt = 0, vel_h = NAN, heading = NAN, vel_v = NAN;
    std::string time;
    std::string extra;          /* more truth fields, as JSON text starting with a comma, e.g. ,"temp":-12.3 */
};
static std::vector<Truth> g_truth;

static void truth_line(FILE *f, const Truth &t) {
    fprintf(f, "{\"sonde\":\"%s\",\"serial\":\"%s\",\"frame\":%d,\"lat\":%.7f,\"lon\":%.7f,\"alt\":%.3f",
            t.sonde.c_str(), t.serial.c_str(), t.frame, t.lat, t.lon, t.alt);
    if (!std::isnan(t.vel_h)) fprintf(f, ",\"vel_h\":%.3f", t.vel_h);
    if (!std::isnan(t.vel_v)) fprintf(f, ",\"vel_v\":%.3f", t.vel_v);
    if (!std::isnan(t.heading)) fprintf(f, ",\"heading\":%.3f", t.heading);
    fprintf(f, ",\"time\":\"%s\"%s}\n", t.time.c_str(), t.extra.c_str());
}

/* ---------------------------------------------------------------- LMS6-403 */
/* frame layout: lms6Xmod.c pos_* (OFS=4), typ 6: lat/lon /B60B60, alt mm, velocities 24-bit mm/s east/north/up. */
static void lms6_bits(std::vector<uint8_t> &out, int nframes, int first_frame) {
    const int N = 255, K = 223, R = 32;
    RS_t RS;
    rs_init_RS255ccsds(&RS);
    std::vector<uint8_t> bytes;          /* the byte stream: [00 58 f3 3f b8][RS codeword] x nframes + padding */
    const ui8_t sync[5] = {0x00, 0x58, 0xf3, 0x3f, 0xb8};
    double lat0 = 51.5074, lon0 = -0.1278, alt0 = 120.0;
    unsigned sn = 0x7A1234;
    for (int fi = 0; fi < nframes; fi++) {
        ui8_t fr[K];
        memset(fr, 0, sizeof fr);
        for (int i = 0; i < K; i++) fr[i] = (ui8_t)(rnd() * 256);                   /* unused fields */
        fr[0] = 0x24; fr[1] = 0x54; fr[2] = 0x00; fr[3] = 0x00;
        int frnr = first_frame + fi;
        double tow_s = 345600.0 + 12 * 3600 + 34 * 60 + 5 + fi;                     /* Fri (day 4) 12:34:05 + fi */
        double lat = lat0 + 0.0009 * fi, lon = lon0 + 0.0013 * fi, alt = alt0 + 5.25 * fi;
        double vE = 3.2 + 0.1 * fi, vN = -1.5 + 0.2 * fi, vU = 5.0 + 0.01 * fi;
        fr[4] = 0x00; fr[5] = (sn >> 16) & 0xFF; fr[6] = (sn >> 8) & 0xFF; fr[7] = sn & 0xFF;
        fr[8] = frnr >> 8; fr[9] = frnr & 0xFF;
        put32(fr + 10, (uint32_t)llround(tow_s * 1000));
        put32(fr + 18, (uint32_t)(int32_t)llround(lat * B60B60));
        put32(fr + 22, (uint32_t)(int32_t)llround(lon * B60B60));
        put32(fr + 26, (uint32_t)(int32_t)llround(alt * 1000));
        put24(fr + 30, (int32_t)llround(vE * 1000));
        put24(fr + 33, (int32_t)llround(vN * 1000));
        put24(fr + 36, (int32_t)llround(vU * 1000));
        unsigned crc = crc16_0(fr, 221);
        fr[221] = crc >> 8; fr[222] = crc & 0xFF;
        ui8_t cw[N];
        memset(cw, 0, sizeof cw);
        for (int j = 0; j < K; j++) cw[N - 1 - j] = fr[j];
        rs_encode(&RS, cw);
        for (int i = 0; i < 5; i++) bytes.push_back(sync[i]);
        for (int j = 0; j < N; j++) bytes.push_back(cw[N - 1 - j]);
        for (int i = 0; i < 40; i++) bytes.push_back(0x00);                         /* 300 - 260 */
        /* the decoder rounds position/time through its own scaling; the truth is what it can represent */
        Truth t;
        t.sonde = "LMS";
        t.subtype = "LMS6-403";
        char b[64];
        snprintf(b, sizeof b, "LMS6-%u", sn & 0xFFFFFF);
        t.serial = b;
        t.frame = frnr;
        t.lat = lat; t.lon = lon; t.alt = alt;
        t.vel_h = sqrt(vE * vE + vN * vN);
        t.vel_v = vU;
        int sec = (int)tow_s % 86400;
        snprintf(b, sizeof b, "%02d:%02d:%06.3fZ", sec / 3600, (sec % 3600) / 60, (double)(sec % 60));
        t.time = b;
        g_truth.push_back(t);
    }
    for (int i = 0; i < 40; i++) bytes.push_back(0x00);
    /* convolutional code, rate 1/2, K=7, polynomials 0x4f / 0x6d as polyA/polyB in lms6Xmod.c; the second bit of
       each pair is inverted on the air ("(c0, inv(c1))"). Bytes go out least significant bit first. */
    const char *polyA = "1001111", *polyB = "1101101";
    std::vector<uint8_t> in;
    for (size_t i = 0; i < bytes.size(); i++) for (int b = 0; b < 8; b++) in.push_back((bytes[i] >> b) & 1);
    for (size_t k = 0; k < in.size(); k++) {
        int c0 = 0, c1 = 0;
        for (int j = 0; j < 7; j++) {
            long idx = (long)k - 6 + j;
            int bit = idx >= 0 ? in[idx] : 0;
            c0 ^= bit & (polyA[j] & 1);
            c1 ^= bit & (polyB[j] & 1);
        }
        out.push_back((uint8_t)c0);
        out.push_back((uint8_t)(c1 ^ 1));
    }
}

/* ---------------------------------------------------------------- Mk IIa (LMS6-1680) */
/* Real subframes recorded from a sonde, published by rs1729/RS (mk2a/Mk2a_9600.bytes, a hex text file): 0x4D config
   subframe (66 data bytes + CRC) and 0x54 position subframe (172 + CRC). Sent as 8N1 bytes after CA CA CA CA. */
static bool crc_len(const std::vector<uint8_t> &b, int &n) {
    for (n = 30; n + 1 < (int)b.size(); n++)
        if (crc16_0(b.data(), n) == (unsigned)((b[n] << 8) | b[n + 1])) return true;
    return false;
}

static bool mk2a_bits(std::vector<uint8_t> &out, const char *datafile, int nlines) {
    FILE *f = fopen(datafile, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", datafile); return false; }
    std::vector<std::vector<uint8_t> > frames;
    char line[4096];
    while (fgets(line, sizeof line, f) && (int)frames.size() < nlines) {
        std::vector<uint8_t> b;
        for (char *p = strtok(line, " \r\n"); p; p = strtok(nullptr, " \r\n")) b.push_back((uint8_t)strtol(p, nullptr, 16));
        int n;
        if (!crc_len(b, n)) continue;
        b.resize(n + 2);
        frames.push_back(b);
    }
    fclose(f);
    if (frames.empty()) return false;
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < frames.size(); i++) {
        for (int k = 0; k < 10; k++) bytes.push_back(0xCA);
        for (size_t k = 0; k < frames[i].size(); k++) bytes.push_back(frames[i][k]);
        const std::vector<uint8_t> &b = frames[i];
        if (b[2] == 0x54) {
            Truth t;
            t.sonde = "LMS"; t.subtype = "MK2A";
            uint32_t id = 0;
            for (size_t j = 0; j <= i; j++) {      /* the most recent 0x4D with a full id in front of this frame */
                if (frames[j][2] == 0x4D && frames[j].size() > 54 && frames[j][4] == frames[j][50] && frames[j][5] == frames[j][51])
                    id = (frames[j][52] << 24) | (frames[j][53] << 16) | (frames[j][50] << 8) | frames[j][51];
            }
            char s[64];
            snprintf(s, sizeof s, "LMS6-%u", id);
            t.serial = s;
            t.frame = (b[6] << 8) | b[7];
            uint32_t tow = (b[10] << 24) | (b[11] << 16) | (b[12] << 8) | b[13];
            int32_t la = (int32_t)((b[18] << 24) | (b[19] << 16) | (b[20] << 8) | b[21]);
            int32_t lo = (int32_t)((b[22] << 24) | (b[23] << 16) | (b[24] << 8) | b[25]);
            int32_t al = (int32_t)((b[26] << 24) | (b[27] << 16) | (b[28] << 8) | b[29]);
            auto v24 = [&](int p) { int v = (b[p] << 16) | (b[p + 1] << 8) | b[p + 2]; if (v > 0x7FFFFF) v -= 0x1000000; return v / 1e3; };
            t.lat = la / B60B60; t.lon = lo / B60B60; t.alt = al / 1000.0;
            double ve = v24(30), vn = v24(33);
            t.vel_h = sqrt(ve * ve + vn * vn);
            t.vel_v = v24(36);
            int sec = (int)(tow / 1000) % 86400;
            snprintf(s, sizeof s, "%02d:%02d:%06.3fZ", sec / 3600, (sec % 3600) / 60, (sec % 60) + (tow % 1000) / 1000.0);
            t.time = s;
            if (id & 0xFFFF0000) {
                if (g_truth.empty() || g_truth.back().frame != t.frame) g_truth.push_back(t);   /* repeats are suppressed */
            }
        }
    }
    for (int k = 0; k < 10; k++) bytes.push_back(0xCA);
    for (size_t i = 0; i < bytes.size(); i++) {
        out.push_back(0);                                   /* start bit */
        for (int b = 0; b < 8; b++) out.push_back((bytes[i] >> b) & 1);
        out.push_back(1);                                   /* stop bit */
    }
    return true;
}


/* ---------------------------------------------------------------- MTS01 */
/* mts01mod.c: 1200 baud, preamble AA.., header B4 2B, then 1 + 128 + 2 bytes: a type byte, 128 ASCII characters of
   comma separated fields (id, ?, counter, YYMMDDhhmmss, battery mV, lat, lon, alt, heading, speed, ?, T1, T2, RH...)
   and a CRC16 (poly 0x8005, init FFFF, bit-reversed result, sent low byte first). Bytes go out MSB first. */
static unsigned crc16_re(const ui8_t *bytes, int len) {
    unsigned rem = 0xFFFF, re = 0;
    for (int i = 0; i < len; i++) {
        rem ^= (unsigned)bytes[i] << 8;
        for (int j = 0; j < 8; j++) rem = (rem & 0x8000) ? ((rem << 1) ^ 0x8005) & 0xFFFF : (rem << 1) & 0xFFFF;
    }
    for (int j = 0; j < 16; j++) if (rem & (1 << (15 - j))) re |= 1u << j;
    return re;
}

static void bytes_msb(std::vector<uint8_t> &out, const std::vector<uint8_t> &bytes) {
    for (size_t i = 0; i < bytes.size(); i++) for (int b = 7; b >= 0; b--) out.push_back((bytes[i] >> b) & 1);
}

static void mts01_bits(std::vector<uint8_t> &out, int nframes, int first_frame) {
    std::vector<uint8_t> bytes;
    for (int fi = 0; fi < nframes; fi++) {
        int cnt = first_frame + fi;
        double lat = 47.2500 + 0.0010 * fi, lon = 8.5500 + 0.0015 * fi, alt = 510.0 + 4.5 * fi;
        double R = 18.0 + 0.5 * fi;                       /* thermistor value the decoder converts to degrees */
        char s[200];
        snprintf(s, sizeof s, "MT%06d,0,%d,26111213%02d%02d,3150,%.5f,%.5f,%.1f,215.5,6.5,0,%.1f,%.1f,2000,0,0,0",
                 123456, cnt, 14 + fi / 60, fi % 60, lat, lon, alt, R, R);
        std::vector<uint8_t> frm(131, 0);
        frm[0] = 0x80;
        memcpy(&frm[1], s, strlen(s));
        unsigned crc = crc16_re(&frm[1], 128);
        frm[129] = crc & 0xFF; frm[130] = crc >> 8;
        for (int k = 0; k < 8; k++) bytes.push_back(0xAA);
        bytes.push_back(0xB4); bytes.push_back(0x2B);
        bytes.insert(bytes.end(), frm.begin(), frm.end());
        for (int k = 0; k < 6; k++) bytes.push_back(0x00);
        Truth t;
        t.sonde = "MTS01"; t.subtype = "";
        t.serial = "MTS01-MT123456";
        t.frame = cnt; t.lat = lat; t.lon = lon; t.alt = alt; t.vel_h = 6.5; t.heading = 215.5;
        snprintf(s, sizeof s, "2026-11-12T13:%02d:%02d.000Z", 0, 0);
        char tb[64];
        snprintf(tb, sizeof tb, "2026-11-12T%02d:%02d:%06.3fZ", 13, 14 + fi / 60, (double)(fi % 60));
        t.time = tb;
        double T = 1.0 / (1.0 / 273.15 + 1.0 / 3000.0 * log(R / 15.0)) - 273.15;
        char ex[96];
        snprintf(ex, sizeof ex, ",\"temp\":%.1f,\"batt\":3.15", T);
        t.extra = ex;
        g_truth.push_back(t);
    }
    bytes_msb(out, bytes);
}

/* ---------------------------------------------------------------- CF-06-AH */
/* cf06ht03mod.c: 2400 baud, bytes least significant bit first. Frame of 109 bytes: AA x5, 2D D4 63 7F FF, AA AA
   (= CF-06-AH), message 1 (42 bytes, 40 data + CRC16 big endian) + 6 parity bytes, message 2 (41 bytes) + 6 parity.
   The Reed-Solomon code is RS(255,249) over GF(2^8)/0x11D, shortened; message 1 at offset 12. */
static void cf06_parity(RS_t *RS, const ui8_t *msg, int len, ui8_t *par) {
    ui8_t cw[255];
    memset(cw, 0, sizeof cw);
    for (int i = 0; i < len; i++) cw[6 + i] = msg[len - 1 - i];
    rs_encode(RS, cw);
    for (int j = 0; j < 6; j++) par[j] = cw[5 - j];
}

static void cf06_bits(std::vector<uint8_t> &out, int nframes, int first_frame) {
    RS_t RS;
    memset(&RS, 0, sizeof RS);
    RS.N = 255; RS.t = 3; RS.R = 6; RS.K = 249; RS.b = 1; RS.p = 1; RS.ip = 1;
    RS.GF.f = 0x11D; RS.GF.ord = 256; RS.GF.alpha = 0x02;
    rs_init_RS(&RS);
    std::vector<uint8_t> bytes;
    for (int fi = 0; fi < nframes; fi++) {
        ui8_t fr[109];
        memset(fr, 0, sizeof fr);
        for (int i = 0; i < 5; i++) fr[i] = 0xAA;
        fr[5] = 0x2D; fr[6] = 0xD4; fr[7] = 0x63; fr[8] = 0x7F; fr[9] = 0xFF; fr[10] = 0xAA; fr[11] = 0xAA;
        ui8_t *m1 = fr + 12;
        unsigned sn = 12345678u + fi * 0;                       /* decimal digits in pairs, one pair per byte */
        m1[0] = (sn / 1000000) % 100; m1[1] = (sn / 10000) % 100; m1[2] = (sn / 100) % 100; m1[3] = sn % 100;
        double tow_s = 2 * 86400 + 9 * 3600 + 41 * 60 + 7 + fi;            /* Wed 09:41:07 + fi */
        uint32_t tow_ms = (uint32_t)llround(tow_s * 1000) + 250;
        for (int j = 0; j < 4; j++) m1[4 + j] = (tow_ms >> (8 * j)) & 0xFF;
        double lat = 35.6800 + 0.0008 * fi, lon = 139.7500 + 0.0011 * fi, alt = 1500.0 + 6.0 * fi;
        int32_t ilon = (int32_t)llround(lon * 1e7), ilat = (int32_t)llround(lat * 1e7), ialt = (int32_t)llround(alt * 1e3);
        for (int j = 0; j < 4; j++) { m1[8 + j] = ((uint32_t)ilon >> (8 * j)) & 0xFF; m1[12 + j] = ((uint32_t)ilat >> (8 * j)) & 0xFF;
                                      m1[16 + j] = ((uint32_t)ialt >> (8 * j)) & 0xFF; }
        double vE = 4.5, vN = -3.0, vU = 5.5;
        int16_t ve = (int16_t)llround(vE * 100), vn = (int16_t)llround(vN * 100), vu = (int16_t)llround(-vU * 100);
        m1[20] = ve & 0xFF; m1[21] = (ve >> 8) & 0xFF; m1[22] = vn & 0xFF; m1[23] = (vn >> 8) & 0xFF; m1[24] = vu & 0xFF; m1[25] = (vu >> 8) & 0xFF;
        for (int i = 26; i < 38; i++) m1[i] = (ui8_t)(rnd() * 256);
        m1[38] = (first_frame + fi) & 0xFF;                      /* the 8-bit counter at OFS+45 = 50 */
        m1[39] = 0x11;
        unsigned c1 = crc16_0(m1, 40);
        m1[40] = c1 >> 8; m1[41] = c1 & 0xFF;
        cf06_parity(&RS, m1, 42, fr + 54);
        ui8_t *m2 = fr + 60;
        for (int i = 0; i < 10; i++) m2[i] = 0xAA;
        for (int i = 10; i < 39; i++) m2[i] = (ui8_t)(rnd() * 256);
        unsigned c2 = crc16_0(m2, 39);
        m2[39] = c2 >> 8; m2[40] = c2 & 0xFF;
        cf06_parity(&RS, m2, 41, fr + 101);
        for (int k = 0; k < 4; k++) bytes.push_back(0xAA);
        for (int k = 0; k < 109; k++) bytes.push_back(fr[k]);
        for (int k = 0; k < 6; k++) bytes.push_back(0x00);
        Truth t;
        t.sonde = "CF6";
        char b[64];
        snprintf(b, sizeof b, "CF6-%08u", sn);
        t.serial = b;
        t.frame = (int)((tow_ms + 500) / 1000);
        t.lat = lat; t.lon = lon; t.alt = alt;
        t.vel_h = sqrt(vE * vE + vN * vN);
        t.vel_v = vU;
        t.heading = atan2(vE, vN) * 180.0 / M_PI; if (t.heading < 0) t.heading += 360.0;
        int sec = (int)(tow_ms / 1000) % 86400;
        snprintf(b, sizeof b, "%02d:%02d:%06.3fZ", sec / 3600, (sec % 3600) / 60, (sec % 60) + (tow_ms % 1000) / 1000.0);
        t.time = b;
        g_truth.push_back(t);
    }
    for (size_t i = 0; i < bytes.size(); i++) for (int b = 0; b < 8; b++) out.push_back((bytes[i] >> b) & 1);   /* LSB first */
}

/* ---------------------------------------------------------------- MRZ-N1 / MP3-H1 */
/* mp3h1mod.c: 2400 baud Manchester (data 1 -> 10, 0 -> 01), bytes MSB first. Frame: AA BF 35, 45 bytes of data
   (counter nibble, UTC h:m:s, ECEF x y z in cm, ECEF velocity in cm/s, satellites, T and RH in 1/100, counter, one
   32-bit configuration word per frame cycling through 16 slots: serial numbers in slots 12 and 13, date in slot 15),
   CRC16 (poly 0xA001 reflected, init FFFF, low byte first), then AA AA. */
static unsigned crc16rev_a001(const ui8_t *b, int len) {
    unsigned rem = 0xFFFF;
    for (int i = 0; i < len; i++) {
        rem ^= b[i];
        for (int j = 0; j < 8; j++) rem = (rem & 1) ? ((rem >> 1) ^ 0xA001) & 0xFFFF : (rem >> 1) & 0xFFFF;
    }
    return rem;
}
static void put16le(ui8_t *p, int v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
static void put32le(ui8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF; }

static void mrz_bits(std::vector<uint8_t> &out, int nframes) {
    const double a = 6378137.0, f = 1 / 298.257223563, e2 = f * (2 - f);
    std::vector<uint8_t> data;                             /* data bits, MSB first per byte */
    auto byte = [&](int v) { for (int b = 7; b >= 0; b--) data.push_back((v >> b) & 1); };
    for (int k = 0; k < 4; k++) byte(0xAA);
    const unsigned snC = 4242, snD = 7777;
    for (int fi = 0; fi < nframes; fi++) {
        double lat = 55.7500 + 0.0007 * fi, lon = 37.6200 + 0.0010 * fi, alt = 150.0 + 5.0 * fi;
        double vN = 1.5, vE = -2.5, vU = 5.0;
        double phi = lat * M_PI / 180, lam = lon * M_PI / 180;
        double Nn = a / sqrt(1 - e2 * sin(phi) * sin(phi));
        double X = (Nn + alt) * cos(phi) * cos(lam), Y = (Nn + alt) * cos(phi) * sin(lam), Z = (Nn * (1 - e2) + alt) * sin(phi);
        double V[3] = {-vN * sin(phi) * cos(lam) - vE * sin(lam) + vU * cos(phi) * cos(lam),
                       -vN * sin(phi) * sin(lam) + vE * cos(lam) + vU * cos(phi) * sin(lam),
                        vN * cos(phi) + vU * sin(phi)};
        ui8_t fr[52];
        memset(fr, 0, sizeof fr);
        fr[0] = 0xAA; fr[1] = 0xBF; fr[2] = 0x35;
        int slot = fi % 16;
        fr[3] = 0x80 | slot;
        int hh = 10, mm = 20, ss = fi;
        fr[4] = hh; fr[5] = mm; fr[6] = ss;
        put32le(fr + 8, (uint32_t)(int32_t)llround(X * 100));
        put32le(fr + 12, (uint32_t)(int32_t)llround(Y * 100));
        put32le(fr + 16, (uint32_t)(int32_t)llround(Z * 100));
        for (int k = 0; k < 3; k++) put16le(fr + 20 + 2 * k, (int)llround(V[k] * 100));
        fr[26] = 9;
        put16le(fr + 29, -1234);
        put16le(fr + 31, 4500);
        fr[43] = slot + 1;
        uint32_t cfg = 0;
        if (slot == 12) cfg = snC;
        else if (slot == 13) cfg = snD;
        else if (slot == 15) cfg = 12 * 10000 + 11 * 100 + 26;       /* DDMMYY = 12.11.26 */
        else { float fl = 1.0f + slot; memcpy(&cfg, &fl, 4); }
        put32le(fr + 44, cfg);
        unsigned crc = crc16rev_a001(fr + 3, 45);
        fr[48] = crc & 0xFF; fr[49] = crc >> 8;
        fr[50] = 0xAA; fr[51] = 0xAA;
        for (int rep = 0; rep < 3; rep++) {                   /* every frame is sent several times in a row */
            for (int k = 0; k < 52; k++) byte(fr[k]);
            if (rep == 2) { byte(0x00); byte(0x00); byte(0xAA); byte(0xAA); }
        }
        if (slot == 15 || fi >= 15) {
            Truth t;
            t.sonde = "MRZ";
            char b[96];
            snprintf(b, sizeof b, "MRZ-%u-%u", snC, snD);
            t.serial = b;
            int yy = 2026, mo = 11, dd = 12;
            int y2 = yy, m2 = mo;
            if (m2 < 3) { y2 -= 1; m2 += 12; }
            int gpsDays = (int)(365.25 * y2) + (int)(30.6001 * (m2 + 1.0)) + dd - 723263;
            int week = gpsDays / 7, tow = (gpsDays % 7) * 86400 + hh * 3600 + mm * 60 + ss;
            t.frame = week * 604800 + tow;
            t.lat = lat; t.lon = lon; t.alt = alt;
            t.vel_h = sqrt(vN * vN + vE * vE);
            t.heading = atan2(vE, vN) * 180.0 / M_PI; if (t.heading < 0) t.heading += 360.0;
            t.vel_v = vU;
            snprintf(b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02dZ", yy, mo, dd, hh, mm, ss);
            t.time = b;
            t.extra = ",\"temp\":-12.3,\"humidity\":45.0";
            g_truth.push_back(t);
        }
    }
    byte(0x00); byte(0x00); byte(0x00);
    for (size_t i = 0; i < data.size(); i++) { out.push_back(data[i] ? 1 : 0); out.push_back(data[i] ? 0 : 1); }
}

/* ---------------------------------------------------------------- check */
static bool getnum(const std::string &l, const char *key, double &v) {
    std::string k = std::string("\"") + key + "\":";
    size_t p = l.find(k);
    if (p == std::string::npos) return false;
    v = atof(l.c_str() + p + k.size());
    return true;
}
static bool getstr(const std::string &l, const char *key, std::string &v) {
    std::string k = std::string("\"") + key + "\":\"";
    size_t p = l.find(k);
    if (p == std::string::npos) return false;
    size_t e = l.find('"', p + k.size());
    if (e == std::string::npos) return false;
    v = l.substr(p + k.size(), e - p - k.size());
    return true;
}
static std::vector<std::string> readlines(const char *path) {
    std::vector<std::string> v;
    FILE *f = fopen(path, "r");
    if (!f) return v;
    char buf[8192];
    while (fgets(buf, sizeof buf, f)) { std::string s = buf; while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back(); if (!s.empty()) v.push_back(s); }
    fclose(f);
    return v;
}

/* every truth line must be matched, in order, by a "sonde" event of the output (extra events are not allowed) */
static int check(const char *truthf, const char *outf) {
    std::vector<std::string> tr = readlines(truthf), out = readlines(outf), ev;
    for (size_t i = 0; i < out.size(); i++) if (out[i].find("\"type\":\"sonde\"") != std::string::npos) ev.push_back(out[i]);
    if (ev.size() != tr.size()) { fprintf(stderr, "check: %zu sonde events, %zu expected\n", ev.size(), tr.size()); return 1; }
    int bad = 0;
    for (size_t i = 0; i < tr.size(); i++) {
        std::string ts, es;
        double tv, ev_;
        const char *skeys[] = {"sonde", "serial", "time"};
        for (const char *k : skeys) {
            if (!getstr(tr[i], k, ts) || !getstr(ev[i], k, es) || ts != es) { fprintf(stderr, "check: event %zu %s: '%s' vs '%s'\n", i, k, ts.c_str(), es.c_str()); bad++; }
        }
        const struct { const char *k; double tol; } nk[] = {{"frame", 0}, {"lat", 2e-5}, {"lon", 2e-5}, {"alt", 0.02}, {"vel_h", 0.06}, {"vel_v", 0.06}, {"heading", 0.3}, {"temp", 0.06}, {"batt", 0.006}, {"humidity", 0.06}};
        for (const auto &n : nk) {
            if (!getnum(tr[i], n.k, tv)) continue;
            if (!getnum(ev[i], n.k, ev_) || fabs(tv - ev_) > n.tol) { fprintf(stderr, "check: event %zu %s: %g vs %g\n", i, n.k, tv, ev_); bad++; }
        }
    }
    if (!bad) printf("check ok: %zu events match the truth\n", tr.size());
    return bad ? 1 : 0;
}


/* ---------------------------------------------------------------- small file tools for the tests */
static int tool_garbage(const char *seed, const char *nbytes, const char *path) {
    g_rng ^= (uint64_t)atoll(seed) * 0x9E3779B97F4A7C15ull;
    long n = atol(nbytes);
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    for (long i = 0; i < n; i++) fputc((int)(rnd() * 256), f);
    fclose(f);
    return 0;
}
static int tool_cat(int argc, char **argv) {          /* cat <out> <in>... */
    FILE *o = fopen(argv[0], "wb");
    if (!o) return 1;
    for (int i = 1; i < argc; i++) {
        FILE *in = fopen(argv[i], "rb");
        if (!in) { fclose(o); return 1; }
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, o);
        fclose(in);
    }
    fclose(o);
    return 0;
}
static int tool_lines(const char *path) {
    std::vector<std::string> v = readlines(path);
    printf("%zu\n", v.size());
    return 0;
}
static int tool_events(const char *path) {           /* count of "sonde" events */
    std::vector<std::string> v = readlines(path);
    size_t n = 0;
    for (size_t i = 0; i < v.size(); i++) if (v[i].find("\"type\":\"sonde\"") != std::string::npos) n++;
    printf("%zu\n", n);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 5 && strcmp(argv[1], "garbage") == 0) return tool_garbage(argv[2], argv[3], argv[4]);
    if (argc >= 4 && strcmp(argv[1], "cat") == 0) return tool_cat(argc - 2, argv + 2);
    if (argc >= 3 && strcmp(argv[1], "lines") == 0) return tool_lines(argv[2]);
    if (argc >= 3 && strcmp(argv[1], "events") == 0) return tool_events(argv[2]);
    if (argc >= 4 && strcmp(argv[1], "check") == 0) return check(argv[2], argv[3]);
    if (argc < 2) { fprintf(stderr, "usage: sondes-gen lms6|mk2a --out f --truth t [...] | check truth out\n"); return 2; }
    std::string type = argv[1], out, truth, data;
    int rate = 48000, frames = 6, first = 1000;
    double noise = 0.0, amp = 0.6, baud = 0, lead = 0.3;
    bool f32 = false, invert = false;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> const char * { if (i + 1 >= argc) { fprintf(stderr, "%s needs a value\n", a.c_str()); exit(2); } return argv[++i]; };
        if (a == "--out") out = val();
        else if (a == "--truth") truth = val();
        else if (a == "--rate") rate = atoi(val());
        else if (a == "--frames") frames = atoi(val());
        else if (a == "--first") first = atoi(val());
        else if (a == "--noise") noise = atof(val());
        else if (a == "--amp") amp = atof(val());
        else if (a == "--baud") baud = atof(val());
        else if (a == "--data") data = val();
        else if (a == "--lead") lead = atof(val());
        else if (a == "--f32") f32 = true;
        else if (a == "--invert") invert = true;
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    if (out.empty()) { fprintf(stderr, "--out is required\n"); return 2; }
    std::vector<uint8_t> bits;
    if (type == "lms6") {
        lms6_bits(bits, frames, first);
        if (baud <= 0) baud = 4800;
    } else if (type == "mts01") {
        mts01_bits(bits, frames, first);
        if (baud <= 0) baud = 1200;
    } else if (type == "cf06") {
        cf06_bits(bits, frames, first);
        if (baud <= 0) baud = 2400;
    } else if (type == "mrz") {
        mrz_bits(bits, frames);
        if (baud <= 0) baud = 2400;
    } else if (type == "mk2a") {
        if (data.empty()) { fprintf(stderr, "mk2a needs --data Mk2a_9600.bytes\n"); return 2; }
        if (!mk2a_bits(bits, data.c_str(), frames)) return 1;
        if (baud <= 0) baud = 9600;
    } else { fprintf(stderr, "unknown type %s\n", type.c_str()); return 2; }
    std::vector<float> a = modulate(bits, baud, rate, amp, noise, invert, lead, 0.3);
    if (!write_audio(out.c_str(), a, f32)) { fprintf(stderr, "cannot write %s\n", out.c_str()); return 1; }
    if (!truth.empty()) {
        FILE *f = fopen(truth.c_str(), "w");
        if (!f) return 1;
        for (size_t i = 0; i < g_truth.size(); i++) truth_line(f, g_truth[i]);
        fclose(f);
    }
    fprintf(stderr, "%s: %zu bits, %zu samples at %d Hz, %zu truth events\n", type.c_str(), bits.size(), a.size(), rate, g_truth.size());
    return 0;
}
