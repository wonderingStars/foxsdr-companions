/*
 * stdc_gen.cpp -- test signal generator for the stdc helper: an Inmarsat-C TDM channel ENCODER.
 *
 * Written for the FoxSDR stdc helper tests (GNU GPL version 3, like the rest of this folder). It is not part of
 * the helper. It builds Inmarsat-C frames the way the transmit side does and applies the inverse of every receive
 * stage of the upstream library, using the constants upstream holds (unique-word patterns, the 23/39 row
 * permutation, the 64x160 interleaver, the K=7 rate-1/2 code whose polynomials are recovered from upstream's
 * partabIdx table, the 4-byte-group scrambler seeded 0x80), so a frame that goes through this encoder and then
 * the helper's decoder must come out unchanged.
 *
 *   stdc-gen <scenario> <mode> <outfile> [options]
 *     scenario  all     3 frames, every packet type the parser knows, a BD/BE multiframe chain, message texts
 *                       that must never be printed
 *               simple  one frame: bulletin board + signalling channel
 *               fuzz    pathological but CRC-valid packets, then random frames
 *               stdc-gen feed <infile> <chunk>      copy infile to stdout in writes of <chunk> bytes
 *               stdc-gen wavdata <in.wav> <outfile> write the data chunk of a WAV file as raw samples
 *               stdc-gen garbage <seed> <outfile> <bytes>   pseudo-random bytes
 *     mode      sym     one byte (0/1) per symbol, 1200 Hz
 *               f32/cf32 as s16/cs16 but 32-bit float samples
 *               s16     BPSK audio, 48000 Hz, signed 16 bit mono, carrier --carrier Hz (default 2500)
 *               cs16    BPSK I/Q, 48000 Hz, interleaved signed 16 bit, carrier --carrier Hz (default 0)
 *     options   --invert         invert every symbol (the demodulator phase ambiguity)
 *               --errors N       flip N symbols spread over the whole stream (deterministic)
 *               --damage PCT     flip PCT percent of the data symbols of every frame (unique words intact)
 *               --kill-uw        flip the unique word of every third row (44 wrong bits: the frame is not accepted)
 *               --lead N         N random symbols before the first frame (default 3000)
 *               --noise SIGMA    audio/IQ: add Gaussian noise of this sigma (amplitude 1.0 = symbol peak)
 *               --seed N         random seed (default 1)
 *               --truncate N     keep only the first N symbols
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef std::vector<uint8_t> Bytes;

static uint64_t g_rng = 1;
static uint32_t rnd() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (uint32_t)(g_rng >> 11);
}
static double gauss() {
    double u1 = (rnd() + 1.0) / 4294967297.0, u2 = (rnd() + 1.0) / 4294967297.0;
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * M_PI * u2);
}

/* ---- constants held by upstream (inmarsatc_decoder.h / .cpp) ------------------------------------------------ */
static const uint8_t UW_NORMAL[64] = {
    0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 0,
    0, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 1, 1, 1, 1, 0, 0, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0};
static const uint8_t PARTAB_IDX[32] = {0, 1, 3, 2, 3, 2, 0, 1, 0, 1, 3, 2, 3, 2, 0, 1,
                                       2, 3, 1, 0, 1, 0, 2, 3, 2, 3, 1, 0, 1, 0, 2, 3};

static int parity7(unsigned v) {
    int p = 0;
    while(v) { p ^= v & 1; v >>= 1; }
    return p;
}

/* recover the two 7-bit generator polynomials from upstream's trellis table and check them against all 128 cases */
static unsigned GA = 0, GB = 0;
static void derive_code() {
    int found = 0;
    for(unsigned a = 0; a < 128; a++) {
        for(unsigned b = 0; b < 128; b++) {
            bool ok = true;
            for(int s = 0; s < 64 && ok; s++) {
                for(int u = 0; u < 2 && ok; u++) {
                    int top = s >> 5, i = s & 31;
                    unsigned R = ((unsigned)s << 1) | (unsigned)u;
                    int sym = PARTAB_IDX[i] ^ (3 * (top ^ u));
                    int A = parity7(R & a), B = parity7(R & b);
                    ok = (2 * A + B) == sym;
                }
            }
            if(ok) { GA = a; GB = b; found++; }
        }
    }
    if(found != 1) {
        std::fprintf(stderr, "stdc-gen: code polynomials not unique/found (%d)\n", found);
        std::exit(3);
    }
}

static uint8_t rev8(uint8_t v) {
    uint8_t r = 0;
    for(int i = 0; i < 8; i++) { r = (uint8_t)((r << 1) | ((v >> i) & 1)); }
    return r;
}

/* ---- packets ---------------------------------------------------------------------------------------------------------- */
static void put_crc(Bytes& p) {
    short C0 = 0, C1 = 0;
    /* the two CRC bytes themselves enter the sums as zeros (they still advance C1) */
    for(size_t i = 0; i < p.size(); i++) { C0 += (i + 2 < p.size()) ? p[i] : 0; C1 += C0; }
    p[p.size() - 2] = (uint8_t)(C0 - C1);
    p[p.size() - 1] = (uint8_t)(C1 - 2 * C0);
}
/* short descriptor packet: total length is fixed by the descriptor; body goes at offset 1 */
static Bytes shortp(uint8_t d, const Bytes& body) {
    size_t len = (d & 0x0F) + 1;
    Bytes p(len, 0);
    p[0] = d;
    if(body.size() + 3 > len) { std::fprintf(stderr, "short packet 0x%02X body too long\n", d); std::exit(3); }
    for(size_t i = 0; i < body.size(); i++) p[1 + i] = body[i];
    put_crc(p);
    return p;
}
/* medium descriptor packet: [d][len-2][body...][crc]; len = 2 + body + 2 unless forced */
static Bytes medp(uint8_t d, const Bytes& body) {
    Bytes p(4 + body.size(), 0);
    p[0] = d;
    p[1] = (uint8_t)(p.size() - 2);
    for(size_t i = 0; i < body.size(); i++) p[2 + i] = body[i];
    put_crc(p);
    return p;
}
static void app(Bytes& b, std::initializer_list<int> v) { for(int x : v) b.push_back((uint8_t)x); }
static void apps(Bytes& b, const char* s) { while(*s) b.push_back((uint8_t)*s++); }
static void fill(Bytes& b, size_t n, uint8_t v) { for(size_t i = 0; i < n; i++) b.push_back(v); }

static Bytes pk7d(int frameNo, int sat, int les, int chType) {
    Bytes b;
    app(b, {109, frameNo >> 8, frameNo & 255, 1 << 2, 0x00, chType << 5 | (3 << 2), sat << 6 | les, 0xE8, 0xF4, 0xB7, 0x1E});
    return shortp(0x7D, b);
}
static Bytes pk6c() {
    Bytes b;
    app(b, {0xF4, 0x14, 0x00 /* uplink raw 5120+... */, 0x1B, 0x6C, 0xB1, 0x1B, 0xC6, 0x6C, 0x1B});
    b[1] = 0x1F; b[2] = 0x40; /* 8000 -> 1626.5 + (8000-6000)*0.0025 = 1631.5 MHz */
    return shortp(0x6C, b);
}
static Bytes pk08() { Bytes b; app(b, {1 << 6 | 21, 130, 0x1B, 0x58, 0, 0}); return shortp(0x08, b); }
static Bytes pk27(int mes) { Bytes b; app(b, {mes >> 16 & 255, mes >> 8 & 255, mes & 255, 1 << 6 | 4, 77}); return shortp(0x27, b); }
static Bytes pk2a(int mes) { Bytes b; app(b, {mes >> 16 & 255, mes >> 8 & 255, mes & 255, 1 << 6 | 12, 61, 0xDE, 0xAD, 0x01}); return shortp(0x2A, b); }
static Bytes pk81(int mes) {
    Bytes b;
    app(b, {mes >> 16 & 255, mes >> 8 & 255, mes & 255, 1 << 6 | 21, 0x1F, 0x40, 0x07, 140, 0xAA, 0xBB, 0xCC, 0xDD, 6, 0x11, 0x22});
    return medp(0x81, b);
}
static Bytes pk83(int mes) {
    Bytes b;
    app(b, {mes >> 16 & 255, mes >> 8 & 255, mes & 255, 2 << 6 | 3, 0x05, 88, 6, 4, 0x1F, 0x40, 0x17, 0x70, 9, 0xAB});
    return medp(0x83, b);
}
static void station(Bytes& b, int sat, int les, int start, int iss, int dl) {
    app(b, {sat << 6 | les, start, iss >> 8, iss & 255, dl >> 8, dl & 255});
}
static Bytes pk92(int mes) {
    Bytes b;
    app(b, {mes >> 16 & 255, mes >> 8 & 255, mes & 255, 0x1F, 0x40, 0x03, 2});
    station(b, 1, 2, 3, 0xE000 | 0x1F00 | 0x0C, 8000 + 120);
    station(b, 1, 12, 4, 0xF810, 8000 + 240);
    Bytes p = medp(0x92, b);
    return p; /* byte 1 = length - 2 = 21 > 7, so upstream reads the station list */
}
static Bytes pkab(int count) {
    Bytes b;
    app(b, {7, count});
    for(int i = 0; i < count; i++) station(b, i & 3, 1 + i, i, 0xE000 | (i << 4), 8000 + 40 * i);
    return medp(0xAB, b);
}
static Bytes pka3(int mes, const char* text) {
    Bytes b;
    app(b, {mes >> 16 & 255, mes >> 8 & 255, mes & 255, 1 << 6 | 21, 1, 2, 3, 4, 5, 6, 0});
    apps(b, text);
    while(b.size() + 4 < 38) b.push_back('.');
    return medp(0xA3, b);
}
static Bytes pka8(int mes, const char* text) {
    Bytes b;
    app(b, {mes >> 16 & 255, mes >> 8 & 255, mes & 255, 1 << 6 | 12, 5, 0x28, 0x34, (int)std::strlen(text), 0x51});
    apps(b, text);
    return medp(0xA8, b);
}
static Bytes pkaa(const char* text) {
    Bytes b;
    app(b, {1 << 6 | 4, 33, 5});
    apps(b, text);
    return medp(0xAA, b);
}
static Bytes pkegc(uint8_t d, int msgType, int addrLen, int prio, int msgId, int pktNo, const char* text) {
    Bytes b;
    app(b, {msgType, 0x80 | prio << 5 | 3, msgId >> 8, msgId & 255, pktNo, 0});
    fill(b, addrLen, 0);
    for(int i = 1; i < addrLen; i++) b[6 + i] = (uint8_t)(0x10 * i + 5);
    apps(b, text);
    return medp(d, b);
}
static Bytes pkgeneric(uint8_t d, int n, uint8_t v) { Bytes b; fill(b, n, v); return medp(d, b); }

/* ---- frames and the transmit chain ---------------------------------------------------------------------------------- */
static Bytes make_frame(const std::vector<Bytes>& packets) {
    Bytes f;
    for(size_t i = 0; i < packets.size(); i++) f.insert(f.end(), packets[i].begin(), packets[i].end());
    if(f.size() > 639) { std::fprintf(stderr, "frame too long: %zu\n", f.size()); std::exit(3); }
    f.resize(640, 0);
    return f;
}

static void scrambler_array(uint8_t arr[160]) {
    uint8_t reg = 0x80;
    for(int i = 0; i < 160; i++) {
        uint8_t x7 = reg & 1, x5 = (reg >> 2) & 1, x4 = (reg >> 3) & 1, x3 = (reg >> 4) & 1;
        arr[i] = x7;
        reg >>= 1;
        reg |= (uint8_t)((x7 ^ x5 ^ x4 ^ x3) << 7);
    }
}

/* 640 frame bytes -> 10368 symbols in transmit order (unique words included) */
static std::vector<uint8_t> encode_frame(Bytes f) {
    uint8_t arr[160];
    scrambler_array(arr);
    /* the last byte is the encoder flush byte: choose it so that the trellis ends in state 0 */
    f[639] = arr[159] ? 0xFF : 0x00;
    Bytes v(640);
    for(int g = 0; g < 160; g++) {
        for(int k = 0; k < 4; k++) {
            uint8_t x = f[g * 4 + k];
            if(arr[g]) x = (uint8_t)~x;
            v[g * 4 + k] = rev8(x);
        }
    }
    std::vector<uint8_t> coded;
    unsigned S = 0;
    for(int k = 0; k < 5120; k++) {
        unsigned u = (v[k / 8] >> (7 - k % 8)) & 1;
        unsigned R = (S << 1) | u;
        coded.push_back((uint8_t)parity7(R & GA));
        coded.push_back((uint8_t)parity7(R & GB));
        S = R & 63;
    }
    if(S != 0 && (v[639] & 0x3F) != 0) { /* cannot happen: flush byte construction above */
        std::fprintf(stderr, "stdc-gen: trellis does not end in state 0\n");
        std::exit(3);
    }
    /* interleave: matrix M[r][c] = coded[c*64 + r]; transmitted row j carries matrix row (j*39)%64 */
    std::vector<uint8_t> sym;
    sym.reserve(10368);
    for(int j = 0; j < 64; j++) {
        int r = (j * 39) % 64;
        sym.push_back(UW_NORMAL[j]);
        sym.push_back(UW_NORMAL[j]);
        for(int c = 0; c < 160; c++) sym.push_back(coded[c * 64 + r]);
    }
    return sym;
}

/* ---- scenarios -------------------------------------------------------------------------------------------------------- */
static std::vector<Bytes> scenario_all() {
    std::vector<Bytes> frames;
    frames.push_back(make_frame({pk7d(6000, 1, 44, 1), pk6c(), pk08(), pk27(4660), pk2a(305419), pk81(8473448), pk83(1193046),
                                 pk92(7874820), pkab(3), pka3(12657851, "PRIVATE-POLL-TEXT-XYZ"), pka8(440847, "PRIVATE-CONFIRM-TEXT"),
                                 pkaa("PRIVATE-MESSAGE-BODY"),
                                 pkegc(0xB1, 0x31, 4, 1, 4242, 1, "SECRET-EGC-TEXT-ONE"), pkegc(0xB2, 0x04, 7, 3, 4243, 2, "SECRET-EGC-TEXT-TWO"),
                                 pkgeneric(0x91, 5, 0x11), pkgeneric(0x9A, 4, 0x22), pkgeneric(0xA0, 3, 0x33), pkgeneric(0xAC, 6, 0x44),
                                 pkgeneric(0xAD, 4, 0x55), shortp(0x19, Bytes{1, 2, 3, 4, 5, 6, 7})}));
    /* multiframe chain: an AB (5 stations, 36 bytes with its CRC) is cut into a BD in frame 2 and a BE in frame 3 */
    Bytes inner = pkab(5);
    Bytes bdBody(inner.begin(), inner.begin() + 16);
    Bytes beBody(inner.begin() + 16, inner.begin() + 34); /* the inner CRC is never sent */
    Bytes bd = medp(0xBD, bdBody);
    Bytes be = medp(0xBE, beBody);
    frames.push_back(make_frame({pk7d(6001, 1, 44, 1), bd, pk6c()}));
    frames.push_back(make_frame({pk7d(6002, 1, 44, 1), be, pk08()}));
    return frames;
}
static std::vector<Bytes> scenario_simple() { return {make_frame({pk7d(6000, 1, 44, 1), pk6c()})}; }
static std::vector<Bytes> scenario_fuzz() {
    std::vector<Bytes> frames;
    /* BD whose inner packet has length 0 / is shorter than its first part; BE with no BD; a BD inside a BE chain */
    Bytes bdZero = medp(0xBD, Bytes{0xC0, 0x00, 1, 2, 3, 4, 5, 6});
    Bytes bdShort = medp(0xBD, Bytes{0x02, 9, 9, 9, 9, 9, 9, 9, 9, 9});
    Bytes beLone = medp(0xBE, Bytes{1, 2, 3, 4, 5, 6, 7, 8});
    Bytes bdNested = medp(0xBD, Bytes{0xBE, 6, 0xBD, 4, 1, 2});
    Bytes beNested = medp(0xBE, Bytes{0, 0, 0, 0});
    Bytes abHuge = medp(0xAB, Bytes{7, 255, 1, 2, 3});               /* 255 stations announced in a 7 byte body */
    Bytes loginHuge = medp(0x92, Bytes{0, 0, 1, 0, 0, 0, 255, 1, 2}); /* the same inside a login acknowledgement */
    frames.push_back(make_frame({bdZero, beLone, bdShort, beLone, bdNested, beNested, beNested, abHuge, loginHuge}));
    /* a medium packet that claims to run past the end of the frame, at the very end */
    Bytes tail(630, 0);
    for(size_t i = 0; i < tail.size(); i++) tail[i] = (uint8_t)(0x6C);
    Bytes f2 = tail;
    f2.resize(640, 0);
    f2[636] = 0x81; f2[637] = 0xFF;
    frames.push_back(f2);
    Bytes f3(640, 0);
    f3[0] = 0xC3; f3[1] = 0xFF;      /* descriptor type 11: no known length */
    f3[10] = 0x10;                   /* short packet of length 1 */
    f3[20] = 0x80; f3[21] = 0x00;    /* medium packet of length 2 */
    frames.push_back(f3);
    for(int i = 0; i < 6; i++) {     /* random frames */
        Bytes r(640);
        for(size_t k = 0; k < r.size(); k++) r[k] = (uint8_t)rnd();
        frames.push_back(r);
    }
    return frames;
}

static void write_all(const char* path, const void* data, size_t n) {
    FILE* f = std::fopen(path, "wb");
    if(!f) { std::fprintf(stderr, "stdc-gen: cannot write %s\n", path); std::exit(2); }
    if(n && std::fwrite(data, 1, n, f) != n) { std::fprintf(stderr, "stdc-gen: short write\n"); std::exit(2); }
    std::fclose(f);
}

int main(int argc, char** argv) {
    if(argc < 4) {
        std::fprintf(stderr, "usage: stdc-gen all|simple|fuzz sym|s16|cs16 <outfile> [options]\n");
        return 2;
    }
    std::string scenario = argv[1], mode = argv[2];
    const char* outPath = argv[3];
    if(scenario == "feed") { /* feed <infile> <chunk>: argv[2]=infile, argv[3]=chunk */
        FILE* in = std::fopen(argv[2], "rb");
        if(!in) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        size_t chunk = (size_t)std::atol(argv[3]);
        if(chunk == 0) chunk = 1;
        Bytes buf(chunk);
        size_t n;
        while((n = std::fread(buf.data(), 1, chunk, in)) > 0) { std::fwrite(buf.data(), 1, n, stdout); std::fflush(stdout); }
        std::fclose(in);
        return 0;
    }
    if(scenario == "wavdata") {
        FILE* in = std::fopen(argv[2], "rb");
        if(!in) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
        Bytes all;
        Bytes buf(65536);
        size_t n;
        while((n = std::fread(buf.data(), 1, buf.size(), in)) > 0) all.insert(all.end(), buf.begin(), buf.begin() + n);
        std::fclose(in);
        size_t pos = 12;
        while(pos + 8 <= all.size()) {
            uint32_t sz = all[pos + 4] | all[pos + 5] << 8 | all[pos + 6] << 16 | (uint32_t)all[pos + 7] << 24;
            if(std::memcmp(&all[pos], "data", 4) == 0) {
                size_t avail = all.size() - pos - 8;
                write_all(argv[3], &all[pos + 8], sz < avail ? sz : avail);
                return 0;
            }
            pos += 8 + sz + (sz & 1);
        }
        std::fprintf(stderr, "no data chunk in %s\n", argv[2]);
        return 2;
    }
    if(scenario == "garbage") { /* mode = seed, outfile, then the byte count: pseudo-random bytes, for the garbage-input test */
        g_rng = (uint64_t)std::atoll(argv[2]) * 2654435761ull + 88172645463325252ull;
        long n = argc > 4 ? std::atol(argv[4]) : 100000;
        Bytes r((size_t)n);
        for(size_t i = 0; i < r.size(); i++) r[i] = (uint8_t)rnd();
        write_all(outPath, r.data(), r.size());
        return 0;
    }
    bool invert = false, killUw = false;
    long errors = 0, lead = 3000, truncateAt = -1;
    double damage = 0.0, noise = 0.0, carrier = (mode == "s16" || mode == "f32") ? 2500.0 : 0.0;
    for(int i = 4; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() { if(i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(2); } return argv[++i]; };
        if(a == "--invert") invert = true;
        else if(a == "--kill-uw") killUw = true;
        else if(a == "--errors") errors = std::atol(val());
        else if(a == "--damage") damage = std::atof(val());
        else if(a == "--lead") lead = std::atol(val());
        else if(a == "--noise") noise = std::atof(val());
        else if(a == "--seed") g_rng = (uint64_t)std::atoll(val()) * 2654435761ull + 88172645463325252ull;
        else if(a == "--truncate") truncateAt = std::atol(val());
        else if(a == "--carrier") carrier = std::atof(val());
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    if(g_rng == 1) g_rng = 88172645463325252ull;
    derive_code();

    std::vector<Bytes> frames;
    if(scenario == "all") frames = scenario_all();
    else if(scenario == "simple") frames = scenario_simple();
    else if(scenario == "fuzz") frames = scenario_fuzz();
    else { std::fprintf(stderr, "unknown scenario %s\n", scenario.c_str()); return 2; }

    std::vector<uint8_t> syms;
    for(long i = 0; i < lead; i++) syms.push_back((uint8_t)(rnd() & 1));
    for(size_t fi = 0; fi < frames.size(); fi++) {
        std::vector<uint8_t> s = encode_frame(frames[fi]);
        for(int j = 0; j < 64; j++) {
            for(int k = 0; k < 162; k++) {
                bool isUw = k < 2;
                size_t idx = (size_t)j * 162 + k;
                /* a third of the rows lose their unique word: 44 wrong bits, more than the tolerance, and not the mirror
                   image of the word (which would be read as a reversed-polarity frame) */
                if(isUw && killUw && j % 3 == 0) s[idx] ^= 1;
                if(!isUw && damage > 0.0 && (rnd() % 10000) < (uint32_t)(damage * 100.0)) s[idx] ^= 1;
            }
        }
        syms.insert(syms.end(), s.begin(), s.end());
    }
    for(long i = 0; i < errors; i++) syms[rnd() % syms.size()] ^= 1;
    /* trailing symbols so the last chunk of 5000 is complete and then some */
    long tail = 5000 + (5000 - (long)(syms.size() % 5000)) % 5000;
    for(long i = 0; i < tail; i++) syms.push_back((uint8_t)(rnd() & 1));
    if(truncateAt >= 0 && (size_t)truncateAt < syms.size()) syms.resize((size_t)truncateAt);
    if(invert) for(size_t i = 0; i < syms.size(); i++) syms[i] ^= 1;

    if(mode == "sym") {
        write_all(outPath, syms.data(), syms.size());
        return 0;
    }
    if(mode != "s16" && mode != "cs16" && mode != "f32" && mode != "cf32") { std::fprintf(stderr, "unknown mode %s\n", mode.c_str()); return 2; }
    const bool iq = (mode == "cs16" || mode == "cf32"), f32 = (mode == "f32" || mode == "cf32");
    /* BPSK: 40 samples per symbol, root-raised-cosine pulse (alpha 1), +-1 for 1/0 */
    const int SPS = 40, SPAN = 4;
    std::vector<double> pulse;
    {
        const double alpha = 1.0;
        for(int n = -SPAN * SPS; n <= SPAN * SPS; n++) {
            double t = (double)n / SPS, v;
            if(n == 0) v = 1.0 - alpha + 4.0 * alpha / M_PI;
            else if(std::fabs(std::fabs(4.0 * alpha * t) - 1.0) < 1e-9)
                v = alpha / std::sqrt(2.0) * ((1 + 2 / M_PI) * std::sin(M_PI / (4 * alpha)) + (1 - 2 / M_PI) * std::cos(M_PI / (4 * alpha)));
            else
                v = (std::sin(M_PI * t * (1 - alpha)) + 4 * alpha * t * std::cos(M_PI * t * (1 + alpha))) / (M_PI * t * (1 - std::pow(4 * alpha * t, 2)));
            pulse.push_back(v);
        }
    }
    size_t total = syms.size() * SPS + pulse.size();
    std::vector<double> base(total, 0.0);
    for(size_t k = 0; k < syms.size(); k++) {
        double a = syms[k] ? 1.0 : -1.0;
        for(size_t p = 0; p < pulse.size(); p++) base[k * SPS + p] += a * pulse[p];
    }
    double peak = 0;
    for(size_t i = 0; i < total; i++) peak = std::fmax(peak, std::fabs(base[i]));
    std::vector<double> x; /* interleaved I,Q for the I/Q modes; full scale is +-1.0 */
    for(size_t i = 0; i < total; i++) {
        double b = base[i] / peak;
        double ph = 2.0 * M_PI * carrier * (double)i / 48000.0;
        x.push_back(0.5 * b * std::cos(ph) + noise * gauss() * 0.5);
        if(iq) {
            x.push_back(0.5 * b * std::sin(ph) + noise * gauss() * 0.5);
        }
    }
    if(f32) {
        std::vector<float> out(x.begin(), x.end());
        write_all(outPath, out.data(), out.size() * sizeof(float));
    } else {
        std::vector<int16_t> out;
        for(size_t i = 0; i < x.size(); i++) out.push_back((int16_t)std::lrint(x[i] * 20000.0));
        write_all(outPath, out.data(), out.size() * 2);
    }
    return 0;
}
