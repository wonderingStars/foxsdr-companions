// trunkfmt_gen: test-signal generator for the trunkfmt regression tests.  GPL-3.0-or-later (see ../LICENSE).
//
// Written from the ENCODER side of the sdrtrunk constants (sync words, CRC polynomials, field layouts) and
// deliberately NOT sharing code with the decoder: the CRCs are computed here by polynomial arithmetic, not
// with the decoder lookup tables, so a green test also confirms that the tables match the polynomials.
//
//   trunkfmt_gen <outdir>
// writes: lj.f32 lj.s16 lj_48k.s16 lj_noisy.s16 tait.s16 garbage.s16 csbk.txt   (s16 = mono 16-bit LE audio)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

typedef std::vector<int> Bits;

static void putBits(Bits& b, size_t start, int width, uint64_t value)     // MSB first, contiguous
{
    if(b.size() < start + (size_t)width) { b.resize(start + (size_t)width, 0); }
    for(int i = 0; i < width; i++) { b[start + (size_t)i] = (int)((value >> (width - 1 - i)) & 1); }
}
static void putList(Bits& b, const std::vector<int>& idx, uint64_t value)  // first index = MSB (BinaryMessage.getInt(int[]))
{
    int n = (int)idx.size();
    for(int k = 0; k < n; k++)
    {
        if(b.size() <= (size_t)idx[(size_t)k]) { b.resize((size_t)idx[(size_t)k] + 1, 0); }
        b[(size_t)idx[(size_t)k]] = (int)((value >> (n - 1 - k)) & 1);
    }
}
static std::vector<int> seq(int from, int to)    // from..to ascending or descending
{
    std::vector<int> v;
    if(from <= to) { for(int i = from; i <= to; i++) { v.push_back(i); } }
    else { for(int i = from; i >= to; i--) { v.push_back(i); } }
    return v;
}
static Bits syncBits(uint64_t value, int n) { Bits b; putBits(b, 0, n, value); return b; }

// ---- LoJack ----------------------------------------------------------------------------------------
// CRC-16, polynomial 0x6F63 (x14+x13+x11+x10+x9+x8+x6+x5+x1+1 plus the x16 term implied), initial fill 0,
// over message bits 32..63 (function, address) in transmission order; result transmitted MSB first at 64..79.
static uint16_t ljCrc(const Bits& m)
{
    uint16_t reg = 0;
    for(int i = 32; i < 64; i++)
    {
        int top = (reg >> 15) & 1;
        reg = (uint16_t)(reg << 1);
        if(top ^ m[(size_t)i]) { reg ^= 0x6F63; }
    }
    return reg;
}

static Bits ljTower(uint8_t vrc, uint8_t lrc, int function, uint32_t address28)
{
    Bits m = syncBits(0x550F, 16);
    m.resize(80, 0);
    putList(m, seq(23, 16), vrc);
    putList(m, seq(31, 24), lrc);
    putList(m, seq(35, 32), (uint64_t)function);
    putList(m, seq(63, 36), address28);
    putBits(m, 64, 16, ljCrc(m));
    return m;
}

static Bits ljTransponder(uint64_t payload64)
{
    Bits m = syncBits(0x2AD5, 16);
    putBits(m, 16, 64, payload64);
    return m;
}

// ---- Tait ------------------------------------------------------------------------------------------
static void putText(Bits& m, int firstBit, const std::string& s8)
{
    for(int d = 0; d < 8; d++) { putBits(m, (size_t)(firstBit + 8 * d), 8, (unsigned char)s8[(size_t)d]); }
}

static Bits taitCommon(uint64_t sync20, int size1, const std::string& from, const std::string& to)
{
    Bits m = syncBits(sync20, 20);
    m.resize(440, 0);
    putBits(m, 20, 16, (uint64_t)size1);
    putText(m, 36, from);
    putBits(m, 100, 16, 0xBEEF);       // CHECKSUM_1 (algorithm unknown upstream: any value)
    putBits(m, 116, 16, 0xAAAA);       // REVS_2
    putBits(m, 188, 16, (uint64_t)size1);
    putText(m, 204, to);
    putBits(m, 268, 8, 0x5A);          // UNKNOWN_1
    putBits(m, 276, 16, 0xCAFE);       // CHECKSUM_2
    return m;
}

static Bits taitAni(const std::string& from, const std::string& to)
{
    return taitCommon(0x418EE, 36, from, to);
}

struct Gps { int h, mi, s, latSign, latD, latM, latS3, lonSign, lonHund, lonD, lonM, lonS3, day, f388, spdTens, spdOnes, spdTenth; };

static Bits taitGps(const std::string& from, const std::string& to, const Gps& g)
{
    Bits m = taitCommon(0xA14DC, 55, from, to);
    putBits(m, 292, 1, 1);
    putBits(m, 293, 3, (uint64_t)(g.h / 10)); putBits(m, 296, 4, (uint64_t)(g.h % 10));
    putBits(m, 300, 1, 1);
    putBits(m, 301, 3, (uint64_t)(g.mi / 10)); putBits(m, 304, 4, (uint64_t)(g.mi % 10));
    putBits(m, 308, 1, 1);
    putBits(m, 309, 3, (uint64_t)(g.s / 10)); putBits(m, 312, 4, (uint64_t)(g.s % 10));
    putBits(m, 316, 1, 1);
    putBits(m, 317, 2, (uint64_t)g.latSign);
    putBits(m, 319, 1, 1);
    putBits(m, 320, 4, (uint64_t)(g.latD / 10)); putBits(m, 324, 4, (uint64_t)(g.latD % 10));
    putBits(m, 328, 1, 1);
    putBits(m, 329, 3, (uint64_t)(g.latM / 10)); putBits(m, 332, 4, (uint64_t)(g.latM % 10));
    // seconds fields: hundreds 336..339, tens 340..342 + 344 (upstream's field list), ones 344..347.
    // The generator writes hundreds and ones only; the tens field shares bit 344 with the ones, so keep it 0.
    putBits(m, 336, 4, (uint64_t)(g.latS3 / 100));
    putBits(m, 344, 4, (uint64_t)(g.latS3 % 10));
    putBits(m, 348, 1, 1);
    putBits(m, 349, 2, (uint64_t)g.lonSign);
    putBits(m, 351, 1, (uint64_t)g.lonHund);
    putBits(m, 352, 4, (uint64_t)(g.lonD / 10)); putBits(m, 356, 4, (uint64_t)(g.lonD % 10));
    putBits(m, 360, 1, 1);
    putBits(m, 361, 3, (uint64_t)(g.lonM / 10)); putBits(m, 364, 4, (uint64_t)(g.lonM % 10));
    putBits(m, 368, 4, (uint64_t)(g.lonS3 / 100)); putBits(m, 372, 4, (uint64_t)((g.lonS3 / 10) % 10));
    putBits(m, 376, 4, (uint64_t)(g.lonS3 % 10));
    putBits(m, 380, 1, 1);
    putBits(m, 381, 3, (uint64_t)(g.day / 10)); putBits(m, 384, 4, (uint64_t)(g.day % 10));
    putBits(m, 388, 4, (uint64_t)g.f388);
    putBits(m, 392, 4, (uint64_t)g.spdTens);
    putBits(m, 396, 4, (uint64_t)g.spdOnes);
    putBits(m, 400, 4, (uint64_t)g.spdTenth);
    return m;
}

// ---- AFSK 1200 baud modulator: 1 = 1200 Hz (mark), 0 = 1800 Hz (space), continuous phase ---------------
struct Audio
{
    double rate;
    std::vector<float> x;
    double phase = 0;
    double bitPos = 0;      // fractional bit position carried between calls
    uint32_t lcg = 12345;

    explicit Audio(double r) : rate(r) {}
    void silence(double seconds) { for(long long i = 0, n = (long long)(seconds * rate); i < n; i++) { x.push_back(0.0f); } }
    void bits(const Bits& b)
    {
        const double PI2 = 6.283185307179586;
        for(int bit : b)
        {
            double f = bit ? 1200.0 : 1800.0;
            // samples for this bit: boundary at the exact fractional position
            double end = bitPos + rate / 1200.0;
            long long n0 = (long long)std::llround(bitPos), n1 = (long long)std::llround(end);
            for(long long i = n0; i < n1; i++)
            {
                phase += PI2 * f / rate;
                if(phase > PI2) { phase -= PI2; }
                x.push_back((float)(0.6 * std::sin(phase)));
            }
            bitPos = end;
        }
    }
    void frame(const Bits& b, int preamble = 32)
    {
        Bits pre;
        for(int i = 0; i < preamble; i++) { pre.push_back(i & 1); }
        bits(pre);
        bits(b);
        bits(Bits(8, 0));
        bitPos = 0;     // idle gap resets the fractional carry
        silence(0.25);
    }
    void addNoise(double amplitude)
    {
        for(auto& s : x)
        {
            lcg = lcg * 1664525u + 1013904223u;
            double u = ((lcg >> 8) & 0xFFFF) / 65535.0 - 0.5;
            s = (float)(s + 2.0 * amplitude * u);
        }
    }
    bool writeF32(const std::string& path) const     // the same samples after s16 quantisation, as 32-bit floats
    {
        FILE* f = fopen(path.c_str(), "wb");
        if(!f) { return false; }
        for(float s : x)
        {
            double v = s * 32767.0;
            if(v > 32767.0) { v = 32767.0; }
            if(v < -32768.0) { v = -32768.0; }
            float q = (float)(std::lround(v) / 32768.0);
            fwrite(&q, 4, 1, f);
        }
        fclose(f);
        return true;
    }
    bool write(const std::string& path) const
    {
        FILE* f = fopen(path.c_str(), "wb");
        if(!f) { return false; }
        for(float s : x)
        {
            double v = s * 32767.0;
            if(v > 32767.0) { v = 32767.0; }
            if(v < -32768.0) { v = -32768.0; }
            int16_t i = (int16_t)std::lround(v);
            unsigned char b[2] = {(unsigned char)(i & 0xFF), (unsigned char)((i >> 8) & 0xFF)};
            fwrite(b, 1, 2, f);
        }
        fclose(f);
        return true;
    }
};

static Bits flipped(Bits b, std::initializer_list<int> idx) { for(int i : idx) { b[(size_t)i] ^= 1; } return b; }

static void buildLJ(Audio& a)
{
    a.silence(0.3);
    a.frame(ljTower(0x5A, 0xC3, 4, 0x1234567));                          // 4-ACTIVATE
    a.frame(ljTower(0x11, 0x22, 2, 0x0ABCDEF));                          // 2-TOWER TEST
    a.frame(ljTower(0x00, 0xFF, 3, 0x7654321));                          // 3-DEACTIVATE
    // function 1 with reply digit 3 (bits 45,44,51,50,49) all ones: site id ("Y" in the middle)
    uint32_t site = 0x0012345;
    for(int i : {45, 44, 51, 50, 49}) { site |= 1u << (i - 36); }
    a.frame(ljTower(0x33, 0x44, 1, site));                               // 1Y-SITE ID
    a.frame(ljTower(0x66, 0x77, 0xF, 0x0F0F0F0));                        // F-TRACK PULSE
    Bits good = ljTower(0x5A, 0xC3, 4, 0x2468ACE);
    a.frame(flipped(good, {40}));                                        // one address bit wrong -> corrected
    a.frame(flipped(good, {70}));                                        // one CRC bit wrong -> corrected
    a.frame(flipped(good, {40, 55}));                                    // two errors -> dropped, counted
    a.frame(ljTransponder(0x0123456789ABCDEFull));                       // transponder test (no CRC upstream)
    a.silence(0.2);
}

static void buildTait(Audio& a)
{
    a.silence(0.3);
    Gps g1 = {12, 34, 56, 3, 51, 30, 456, 0, 0, 7, 45, 123, 12, 0, 4, 2, 7};   // lat +51.., lon negative
    a.frame(taitGps("12345678", "  42    ", g1), 24);
    a.frame(taitAni("ABC12345", "99887766"), 24);
    Gps g2 = {1, 2, 3, 0, 33, 15, 202, 3, 1, 15, 30, 505, 31, 5, 1, 8, 0};     // south, east, f388 = 5 (speed hundreds alias)
    a.frame(taitGps("00000001", "00000002", g2), 24);
    a.silence(0.2);
}

// ---- Fleetsync II and MPT-1327 block coding ----------------------------------------------------------
// 64-bit codeword block: 48 data bits, 15-bit CRC (polynomial 0x6815, i.e. x^15+x^14+x^13+x^11+x^4+x^2+1, zero-init
// remainder XOR 1 as in the upstream check), then an even-parity bit over the whole block.
static uint16_t fsCrc15(const Bits& m, int start)
{
    uint16_t reg = 0;
    for(int i = 0; i < 48; i++)
    {
        int top = (reg >> 14) & 1;
        reg = (uint16_t)((reg << 1) & 0x7FFF);
        if(top ^ m[(size_t)(start + i)]) { reg ^= 0x6815; }
    }
    return (uint16_t)(reg ^ 1);
}
static void sealBlock(Bits& m, int start)         // data at start..start+47 -> CRC at +48..+62, parity at +63
{
    if(m.size() < (size_t)(start + 64)) { m.resize((size_t)(start + 64), 0); }
    putBits(m, (size_t)(start + 48), 15, fsCrc15(m, start));
    int ones = 0;
    for(int i = 0; i < 63; i++) { ones += m[(size_t)(start + i)]; }
    m[(size_t)(start + 63)] = ones & 1;
}

struct FsSpec { int fleet, fromIdent, toIdent; bool ani, status, ack, gps, fleetExt; int statusValue; };

static Bits fleetsync(const FsSpec& f, int blocks, int fleetTo = 0, bool extra = false)
{
    Bits m = syncBits(0xA23EB, 21);
    m.resize(537, 0);
    if(f.status) { putBits(m, 21, 7, (uint64_t)f.statusValue); }
    else { for(int b : {22, 24, 26}) { m[(size_t)b] = 1; } }     // emergency / lone worker / paging flags are active low
    m[29] = f.ani; m[30] = f.status; m[31] = f.ack; m[35] = f.gps; m[36] = f.fleetExt;
    putBits(m, 37, 8, (uint64_t)f.fleet);
    putBits(m, 45, 12, (uint64_t)f.fromIdent);
    putBits(m, 57, 12, (uint64_t)f.toIdent);
    if(f.fleetExt) { putBits(m, 85, 8, (uint64_t)fleetTo); }
    if(extra)    // GPS report fields
    {
        putBits(m, 172, 5, 13); putBits(m, 177, 6, 45); putBits(m, 183, 6, 59);               // 13:45:59
        putBits(m, 221, 16, 5130); putBits(m, 238, 14, 4567);                                 // 51 deg 30 min, .4567 min
        putBits(m, 284, 1, 0); putBits(m, 286, 6, 26);                                        // year field
        putBits(m, 292, 4, 11); putBits(m, 296, 5, 12);                                       // month 11 day 12
        putBits(m, 301, 16, 12); putBits(m, 318, 7, 0x35);                                    // 0 deg 12 min west
        putBits(m, 353, 13, 1805);                                                            // heading 180.5
        putBits(m, 484, 8, 42); putBits(m, 492, 8, 128);                                      // speed 42 + 128/255
    }
    for(int b = 0; b < blocks; b++) { sealBlock(m, 21 + 64 * b); }
    return m;
}

struct MptSpec { uint32_t prefix, ident1, type9, ident2; };

static Bits mptBase(uint64_t sync20)
{
    Bits m = syncBits(sync20, 20);
    m.resize(350, 0);
    return m;
}
static void mptSeal(Bits& m, int blocks)
{
    for(int b = 0; b < blocks; b++) { sealBlock(m, 20 + 64 * b); }
}
static Bits mptAddress(uint64_t sync, const MptSpec& s)       // generic address codeword
{
    Bits m = mptBase(sync);
    m[20] = 1;                                                // address codeword
    putBits(m, 21, 7, s.prefix);
    putBits(m, 28, 13, s.ident1);
    putBits(m, 41, 9, s.type9);
    putBits(m, 50, 13, s.ident2);
    return m;
}

// ---- LTR / LTR-Net / Passport (300 baud sub-audible NRZ) ---------------------------------------------
// CRC tables copied from upstream (CRCLTR.sCHECKSUMS, CRCPassport.sCHECKSUMS): these two checksums are linear maps
// given only as tables, so unlike the other generators this one is NOT independent of the decoder's table.
static const int LTR_CHK[24] = {0x38, 0x1C, 0x0E, 0x46, 0x23, 0x51, 0x68, 0x75, 0x7A, 0x3D, 0x1F, 0x4F, 0x26, 0x52, 0x29, 0x15,
                                0x0B, 0x45, 0x62, 0x31, 0x19, 0x0D, 0x07, 0x43};
static const int PP_CHK[51] = {0x6E, 0xBF, 0xD6, 0xE3, 0xF8, 0x7C, 0x3E, 0x97, 0xC2, 0xE9, 0x75, 0x3B, 0x94, 0x4A, 0xAD, 0x57,
                               0xA2, 0xD9, 0x6D, 0x37, 0x92, 0xC1, 0x61, 0x31, 0x19, 0x0D, 0x07, 0x8A, 0xCD, 0x67, 0xBA, 0xD5,
                               0x6B, 0xBC, 0x5E, 0xA7, 0xDA, 0xE5, 0x73, 0xB0, 0x58, 0x2C, 0x16, 0x83, 0xC8, 0x64, 0x32, 0x91,
                               0x49, 0x25, 0x13};

static void ltrSeal(Bits& w, bool invertCrc = false)      // checksum of bits 9..32 into bits 33..39
{
    int c = 0;
    for(int i = 9; i < 33; i++) { if(w[(size_t)i]) { c ^= LTR_CHK[i - 9]; } }
    if(invertCrc) { c ^= 127; }
    putBits(w, 33, 7, (uint64_t)c);
}

// 40-bit word with the outbound sync; fields in transmission order: area(1) channel(5) home(5) group(8) free(5) crc(7)
static Bits ltrWord(int area, int channel, int home, int group, int free)
{
    Bits w = syncBits(0x158, 9);
    w.resize(40, 0);
    putBits(w, 9, 1, (uint64_t)area); putBits(w, 10, 5, (uint64_t)channel); putBits(w, 15, 5, (uint64_t)home);
    putBits(w, 20, 8, (uint64_t)group); putBits(w, 28, 5, (uint64_t)free);
    return w;
}
static Bits asIsw(Bits w) { for(auto& b : w) { b ^= 1; } return w; }      // inbound words are the bit-inverted outbound form

static Bits passportWord(int dcc, int lcn, int site, int group, int type, int free)
{
    Bits w = syncBits(0x158, 9);
    w.resize(68, 0);
    putBits(w, 9, 2, (uint64_t)dcc); putBits(w, 11, 11, (uint64_t)lcn); putBits(w, 22, 7, (uint64_t)site);
    putBits(w, 29, 16, (uint64_t)group); putBits(w, 45, 4, (uint64_t)type); putBits(w, 49, 11, (uint64_t)free);
    return w;
}
static void passportSeal(Bits& w)
{
    int c = 0;
    for(int i = 9; i <= 59; i++) { if(w[(size_t)i]) { c ^= PP_CHK[i - 9]; } }
    putBits(w, 60, 8, (uint64_t)c);
}

struct SubAudio
{
    std::vector<float> x;
    double bitPos = 0;
    void bits(const Bits& b, double rate = 8000.0, double amp = 0.25)
    {
        for(int bit : b)
        {
            double end = bitPos + rate / 300.0;
            long long n0 = (long long)std::llround(bitPos), n1 = (long long)std::llround(end);
            for(long long i = n0; i < n1; i++) { x.push_back((float)(bit ? amp : -amp)); }
            bitPos = end;
        }
    }
    void word(const Bits& w)
    {
        Bits pre;
        for(int i = 0; i < 24; i++) { pre.push_back(i & 1); }
        bits(pre);
        bits(w);
        Bits gap;
        for(int i = 0; i < 40; i++) { gap.push_back(i & 1); }
        bits(gap);                  // alternating filler between words (a run of zeros would let a word tail plus zeros mimic the sync)
        bitPos = 0;
    }
    bool write(const std::string& path, int hold = 1) const
    {
        FILE* f = fopen(path.c_str(), "wb");
        if(!f) { return false; }
        // a radio shapes the sub-audible data: smooth the square wave with a 13-sample moving average (about 600 Hz)
        std::vector<float> y(x.size(), 0.0f);
        for(size_t n = 0; n < x.size(); n++)
        {
            double acc = 0; int cnt = 0;
            for(int k = -6; k <= 6; k++) { long long idx = (long long)n + k; if(idx >= 0 && idx < (long long)x.size()) { acc += x[(size_t)idx]; cnt++; } }
            y[n] = (float)(acc / cnt);
        }
        for(float s : y)
        {
            int16_t i = (int16_t)std::lround(s * 32767.0);
            unsigned char b[2] = {(unsigned char)(i & 0xFF), (unsigned char)((i >> 8) & 0xFF)};
            for(int h = 0; h < hold; h++) { fwrite(b, 1, 2, f); }
        }
        fclose(f);
        return true;
    }
};

static Bits flippedBits(Bits b, std::initializer_list<int> idx) { for(int i : idx) { b[(size_t)i] ^= 1; } return b; }

static void buildLtr(SubAudio& a)
{
    a.bits(Bits(40, 0));
    Bits call = ltrWord(0, 5, 3, 12, 7);       ltrSeal(call); a.word(call);                  // CALL talkgroup 03-012
    Bits idle = ltrWord(1, 4, 4, 255, 4);      ltrSeal(idle); a.word(idle);                  // IDLE (channel == free, group 255)
    Bits end = ltrWord(0, 31, 3, 12, 7);       ltrSeal(end); a.word(end);                    // CALL END
    Bits unk = ltrWord(0, 25, 25, 7, 30);      ltrSeal(unk); a.word(unk);                    // valid CRC, nothing matches: UNKNOWN
    Bits bad = call; bad = flippedBits(bad, {12}); a.word(bad);                              // bad CRC: dropped
    Bits isw = ltrWord(0, 9, 8, 100, 2);       ltrSeal(isw); a.word(asIsw(isw));             // inbound word, normal CRC
    Bits iswInv = ltrWord(1, 9, 8, 101, 2);    ltrSeal(iswInv, true); a.word(asIsw(iswInv)); // inbound word, inverted CRC
}

static void buildLtrNet(SubAudio& a)
{
    a.bits(Bits(40, 0));
    auto osw = [&](Bits w) { ltrSeal(w); a.word(w); };
    auto isw = [&](Bits w, bool inv = false) { ltrSeal(w, inv); a.word(asIsw(w)); };
    osw(ltrWord(0, 5, 3, 12, 7));                                         // OSW_CALL_START
    osw(ltrWord(0, 4, 4, 255, 4));                                        // OSW_SYSTEM_IDLE
    osw(ltrWord(0, 31, 3, 12, 7));                                        // OSW_CALL_END
    {   Bits w = ltrWord(0, 18, 22, 0, 0); putBits(w, 23, 10, 345); osw(w); }                     // OSW_SITE_ID 345
    {   Bits w = ltrWord(0, 24, 5, 0, 0); putBits(w, 20, 1, 0); putBits(w, 21, 12, 1000); osw(w); } // TX frequency low, LCN 5
    {   Bits w = ltrWord(0, 24, 5, 0, 0); putBits(w, 20, 1, 1); putBits(w, 29, 4, 4); osw(w); }    // TX frequency high -> pairs
    {   Bits w = ltrWord(0, 25, 6, 0, 0); putBits(w, 20, 1, 1); putBits(w, 29, 4, 2); osw(w); }    // RX frequency high, LCN 6
    {   Bits w = ltrWord(0, 25, 6, 0, 0); putBits(w, 20, 1, 0); putBits(w, 21, 12, 77); osw(w); }  // RX frequency low -> pairs
    {   Bits w = ltrWord(0, 28, 1, 0, 0); putBits(w, 17, 1, 1); putBits(w, 18, 10, 0x2A5); osw(w); }  // channel map high
    {   Bits w = ltrWord(0, 26, 2, 0, 0); putBits(w, 15, 4, 2); putBits(w, 23, 10, 77); osw(w); }  // neighbour 77, rank 3
    {   Bits w = ltrWord(0, 0, 0, 0, 0); putBits(w, 10, 5, 17); putBits(w, 15, 2, 2); putBits(w, 17, 16, 0xA123); osw(w); }  // registration accept
    isw(ltrWord(0, 5, 3, 12, 21));                                        // ISW_CALL_START
    isw(ltrWord(0, 5, 3, 12, 31));                                        // ISW_REQUEST_ACCESS
    {   Bits w = ltrWord(0, 29, 0, 0, 0); putBits(w, 17, 16, 0xCAFE); isw(w); }                   // ISW_REGISTRATION_REQUEST_ESN_HIGH
    isw(ltrWord(0, 5, 3, 12, 23));                                        // ISW_CALL_END
    osw(ltrWord(0, 22, 9, 3, 3));                                         // OSW_UNKNOWN (channel 22 has no meaning)
    {   Bits bad = ltrWord(0, 5, 3, 12, 7); ltrSeal(bad); a.word(flippedBits(bad, {14})); }       // CRC failure: dropped
}

static void buildPassport(SubAudio& a)
{
    a.bits(Bits(40, 0));
    Bits idle = passportWord(1, 1792, 12, 0x0F05, 1, 99);          // site band (bits 41..44) = 5 -> 450 MHz; neighbor band bits 33..36 = 0
    putBits(idle, 41, 4, 5);
    passportSeal(idle);
    for(int i = 0; i < 5; i++) { a.word(idle); }                   // the fourth identical idle becomes the reference
    Bits call = passportWord(1, 100, 12, 4321, 0, 50);   passportSeal(call); a.word(call);          // CA_STRT group 4321
    Bits id = passportWord(1, 100, 12, 0, 6, 51);        putBits(id, 22, 23, 1234567); passportSeal(id); a.word(id);   // ID_RDIO
    Bits end = passportWord(1, 2047, 12, 4321, 1, 50);   passportSeal(end); a.word(end);            // CA_ENDD
    Bits page = passportWord(1, 100, 12, 777, 5, 50);    passportSeal(page); a.word(page);          // CA_PAGE
    Bits corr = passportWord(1, 100, 12, 4322, 0, 50);   passportSeal(corr); a.word(flippedBits(corr, {67}));   // check bit wrong: corrected
    Bits bad = passportWord(1, 100, 12, 4323, 0, 50);    passportSeal(bad); a.word(flippedBits(bad, {30}));      // data bit wrong: dropped
}

// ---- DMR CSBK (Connect Plus) -------------------------------------------------------------------------
// CRC-CCITT (polynomial 0x1021, initial value 0) over the 80 bits, inverted, XOR the CSBK mask 0xA5A5.
static uint16_t dmrCsbkCrc(const Bits& m)
{
    uint16_t reg = 0;
    for(int i = 0; i < 80; i++)
    {
        int top = (reg >> 15) & 1;
        reg = (uint16_t)(reg << 1);
        if(top ^ m[(size_t)i]) { reg ^= 0x1021; }
    }
    return (uint16_t)((~reg) ^ 0xA5A5);
}

static std::string hexOf(const Bits& m)
{
    std::string s;
    for(size_t i = 0; i < m.size(); i += 4)
    {
        int v = 0;
        for(size_t k = 0; k < 4; k++) { v = (v << 1) | (i + k < m.size() ? m[i + k] : 0); }
        s.push_back("0123456789ABCDEF"[v]);
    }
    return s;
}

static Bits csbk(int fid, int opcode, std::vector<std::pair<std::pair<int, int>, uint64_t>> fields)
{
    Bits m(96, 0);
    putBits(m, 0, 1, 1);                         // last block
    putBits(m, 1, 1, 0);                         // protect flag
    putBits(m, 2, 6, (uint64_t)opcode);
    putBits(m, 8, 8, (uint64_t)fid);
    for(auto& f : fields) { putBits(m, (size_t)f.first.first, f.first.second, f.second); }
    putBits(m, 80, 16, dmrCsbkCrc(m));
    return m;
}

static uint8_t crc8(const Bits& m, int len)       // polynomial x^8+x^2+x+1, initial 0
{
    uint8_t reg = 0;
    for(int i = 0; i < len; i++)
    {
        int top = (reg >> 7) & 1;
        reg = (uint8_t)(reg << 1);
        if(top ^ m[(size_t)i]) { reg ^= 0x07; }
    }
    return reg;
}

static Bits slc(int opcode, int network, int site)
{
    Bits m(44, 0);
    putBits(m, 0, 4, (uint64_t)opcode);
    putBits(m, 4, 12, (uint64_t)network);
    putBits(m, 16, 8, (uint64_t)site);
    putBits(m, 36, 8, crc8(m, 36));
    return m;
}

static void writeCsbk(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "wb");
    if(!f) { return; }
    auto line = [&](const char* comment, const Bits& m, const char* kind = "csbk") {
        if(comment) { fprintf(f, "# %s\n", comment); }
        fprintf(f, "%s %s\n", kind, hexOf(m).c_str());
    };
    const int CP = 6;
    fprintf(f, "# trunkfmt regression input: Motorola Connect Plus CSBKs, built by trunkfmt_gen\n");
    line("neighbor report: sites 3, 7, 12 (rest unused)", csbk(CP, 1, {{{16, 8}, 3}, {{24, 8}, 7}, {{32, 8}, 12}, {{56, 24}, 0xABCDEF}}));
    line("voice channel user: radio 1234567 on talkgroup 4321, LSN field 5", csbk(CP, 3, {{{16, 24}, 1234567}, {{40, 24}, 4321}, {{64, 5}, 5}, {{72, 8}, 0x42}}));
    line("data channel grant: radio 7654321, LSN field 8", csbk(CP, 6, {{{16, 24}, 7654321}, {{40, 5}, 8}, {{48, 8}, 0x11}}));
    line("OTA announcement: XCMP type 2 (network frequency file), version 0x0102, LSN field 3", csbk(CP, 10, {{{16, 8}, 2}, {{24, 16}, 0x0102}, {{40, 23}, 0x1234}, {{63, 5}, 3}}));
    line("terminate channel grant to radio 99", csbk(CP, 12, {{{16, 24}, 99}, {{40, 8}, 1}, {{48, 8}, 2}, {{56, 8}, 3}, {{64, 8}, 4}, {{72, 8}, 5}}));
    line("CSBKO 16: from 1001 to 1002", csbk(CP, 16, {{{16, 24}, 1001}, {{40, 24}, 1002}, {{64, 16}, 0xBEEF}}));
    line("registration request radio 555 -> 1", csbk(CP, 17, {{{16, 24}, 555}, {{40, 24}, 1}, {{64, 8}, 9}}));
    line("registration response 1 -> 555", csbk(CP, 18, {{{16, 24}, 1}, {{40, 24}, 555}, {{64, 8}, 9}}));
    line("talkgroup affiliation radio 555 -> talkgroup 4321", csbk(CP, 24, {{{16, 24}, 555}, {{40, 24}, 4321}, {{64, 8}, 7}}));
    line("enhanced data revert window announcement: window 17 superframe 9, reserved for radio 321", csbk(CP, 28, {{{16, 7}, 17}, {{24, 4}, 9}, {{28, 4}, 2}, {{32, 24}, 321}}));
    line("enhanced data revert window announcement, not reserved", csbk(CP, 28, {{{16, 7}, 5}, {{24, 4}, 1}}));
    line("enhanced data revert window grant to radio 321: superframe 9 window 21", csbk(CP, 29, {{{16, 24}, 321}, {{48, 4}, 9}, {{70, 5}, 21}}));
    Bits good = csbk(CP, 3, {{{16, 24}, 1234567}, {{40, 24}, 4321}, {{64, 5}, 5}});
    line("one bit wrong in the payload: upstream correction needs residual == table entry (see UPSTREAM.md)", flipped(good, {20}));
    line("two bits wrong: CRC error, still decoded", flipped(good, {20, 30}));
    line("Connect Plus voice channel user without the CRC field (10 bytes)", Bits(good.begin(), good.begin() + 80));
    line("standard DMR Aloha (FID 0, opcode 25): reported, not decoded here", csbk(0, 25, {{{16, 8}, 1}}));
    line("Connect Plus FID, opcode 63 (not in the upstream table)", csbk(CP, 63, {}));
    line("Short LC: Connect Plus control channel, network 100 site 5", slc(10, 100, 5), "slc");
    line("Short LC: Connect Plus traffic channel, network 100 site 5", slc(9, 100, 5), "slc");
    Bits badSlc = slc(10, 100, 5);
    badSlc[20] ^= 1;
    line("Short LC with a bad CRC-8", badSlc, "slc");
    fprintf(f, "# a line that cannot be parsed\n");
    fprintf(f, "csbk 12ZZ\n");
    fprintf(f, "csbk 0123\n");
    fclose(f);
}

static void buildFleetsync(Audio& a)
{
    a.silence(0.3);
    a.frame(fleetsync({26, 1234, 0, true, false, false, false, false, 0}, 1), 32);                   // ANI
    a.frame(fleetsync({26, 1234, 4321, false, true, false, false, false, 0x7B}, 1), 32);              // status 123
    a.frame(fleetsync({30, 77, 88, false, true, false, false, true, 0x7B}, 2, 41), 32);               // status, fleet extension
    a.frame(fleetsync({9, 2000, 3000, false, false, false, true, false, 0}, 8, 0, true), 32);         // GPS location report
    FsSpec ack = {26, 1234, 0, false, false, true, false, false, 0};
    a.frame(fleetsync(ack, 1), 32);                                                                   // acknowledge
    Bits good = fleetsync({26, 555, 0, true, false, false, false, false, 0}, 1);
    a.frame(flipped(good, {50}), 32);                                                                 // data bit wrong: corrected
    a.frame(flipped(good, {50, 60}), 32);                                                             // two bits wrong: dropped
    a.silence(0.2);
}

static void buildMpt(Audio& a)
{
    const uint64_t CTRL = 0xAC4D7, TRAF = 0xA3B28;
    a.silence(0.3);
    {   // GTC: bit 41 = 0, channel 43..52, ident2 53..65
        Bits m = mptBase(CTRL);
        m[20] = 1; putBits(m, 21, 7, 12); putBits(m, 28, 13, 1001); putBits(m, 43, 10, 345); putBits(m, 53, 13, 2002);
        mptSeal(m, 1); a.frame(m, 32);
    }
    {   // ALH: type 256, wait 3, M 5, N 9, system 0x1234 in block 2
        Bits m = mptAddress(CTRL, {0, 0, 256, 0});
        putBits(m, 54, 3, 3); putBits(m, 57, 2, 0); putBits(m, 59, 5, 5); putBits(m, 64, 4, 9);
        putBits(m, 85, 15, 0x1234);
        mptSeal(m, 2); a.frame(m, 32);
    }
    {   // BCAST: type 284, sysdef 0 (announce control channel), system 4660
        Bits m = mptBase(CTRL);
        m[20] = 1; putBits(m, 21, 5, 0); putBits(m, 26, 15, 4660); putBits(m, 41, 9, 284);
        mptSeal(m, 1); a.frame(m, 32);
    }
    {   // CLEAR: type 282, traffic channel 321 (21..30), control channel 1 (31..40)
        Bits m = mptAddress(TRAF, {0, 0, 282, 0});
        putBits(m, 21, 10, 321); putBits(m, 31, 10, 1);
        mptSeal(m, 1); a.frame(m, 32);
    }
    {   // ACK from 012-5678 to 012-1234 (user ident)
        Bits m = mptAddress(CTRL, {12, 1234, 264, 5678});
        mptSeal(m, 1); a.frame(m, 32);
    }
    {   // AHYQ status message "Request Speech Call" (status 0): two blocks
        Bits m = mptAddress(CTRL, {12, 1234, 278, 5678});
        putBits(m, 63, 5, 0);
        mptSeal(m, 2); a.frame(m, 32);
    }
    {   // HEAD plus 2 data codewords, MPT-1343 ASCII short data message "FOXSDR TESTS"
        Bits m = mptAddress(CTRL, {12, 1234, 308, 5678});
        m[84] = 0;
        putBits(m, 85, 1, 1); putBits(m, 86, 3, 3); putBits(m, 89, 1, 0);        // STF=1 (MPT1343), GFI=3 (ASCII)
        const char* text = "FOXSDR TESTS";
        int c = 0;
        auto ch = [&](const std::vector<int>& idx) { putList(m, idx, (uint64_t)(unsigned char)text[c++]); };
        ch(seq(91, 97)); ch(seq(98, 104)); ch(seq(105, 111)); ch(seq(112, 118)); ch(seq(119, 125));
        ch({126, 127, 128, 129, 130, 131, 150});
        ch(seq(151, 157)); ch(seq(158, 164)); ch(seq(165, 171)); ch(seq(172, 178)); ch(seq(179, 185)); ch(seq(186, 192));
        mptSeal(m, 3); a.frame(m, 32);
    }
    {   // one bit wrong in block 1 -> corrected
        Bits m = mptAddress(CTRL, {12, 1234, 264, 5678});
        mptSeal(m, 1); a.frame(flipped(m, {30}), 32);
    }
    {   // two bits wrong -> dropped
        Bits m = mptAddress(CTRL, {12, 1234, 264, 5678});
        mptSeal(m, 1); a.frame(flipped(m, {30, 35}), 32);
    }
    a.silence(0.2);
}

int main(int argc, char** argv)
{
    if(argc < 2)
    {
        fprintf(stderr, "usage: trunkfmt_gen <outdir>\n");
        return 2;
    }
    std::string dir = argv[1];
    {
        Audio a(8000.0);
        buildLJ(a);
        a.write(dir + "/lj.s16");
        a.writeF32(dir + "/lj.f32");
    }
    {
        Audio a(48000.0);
        buildLJ(a);
        a.write(dir + "/lj_48k.s16");
    }
    {
        Audio a(8000.0);
        buildLJ(a);
        a.addNoise(0.12);
        a.write(dir + "/lj_noisy.s16");
    }
    {
        Audio a(8000.0);
        buildTait(a);
        a.write(dir + "/tait.s16");
    }
    {
        // garbage: 20 s of pseudo-random 16-bit samples
        Audio a(8000.0);
        uint32_t s = 987654321u;
        for(int i = 0; i < 160000; i++)
        {
            s = s * 1664525u + 1013904223u;
            a.x.push_back(((int)((s >> 8) & 0xFFFF) - 32768) / 32768.0f);
        }
        a.write(dir + "/garbage.s16");
    }
    {
        Audio a(8000.0);
        buildFleetsync(a);
        a.write(dir + "/fleetsync.s16");
    }
    {
        Audio a(8000.0);
        buildMpt(a);
        a.write(dir + "/mpt1327.s16");
    }
    { SubAudio a; buildLtr(a); a.write(dir + "/ltr.s16"); a.write(dir + "/ltr_48k.s16", 6); }
    { SubAudio a; buildLtrNet(a); a.write(dir + "/ltrnet.s16"); }
    { SubAudio a; buildPassport(a); a.write(dir + "/passport.s16"); }
    writeCsbk(dir + "/csbk.txt");
    return 0;
}
