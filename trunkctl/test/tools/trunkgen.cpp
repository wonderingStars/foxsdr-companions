// trunkgen - test signal generator for trunkctl: SmartNet and EDACS control channel frames, built from the same
// constants the decoders use (rx_smartnet.h / smartnet_parser.cc for SmartNet, edacs-fm.c for EDACS; BCH(40,28)
// from the decoder's own bch4028 module), modulated as 2-level FSK discriminator audio or as FM I/Q.
//
//   trunkgen <scenario> <rate> <format> <out> [invert] [esk] [tail=<seconds of silence>] [offset=<symbols>] [noise=<amplitude>] [seed=<n>]
//
// scenario: sn_basic sn_damaged sn_obt sn_idle ed_ea ed_std ed_damaged ed_noise
// format:   s16 f32 cs16 cf32
//
// This file is part of trunkctl and is licensed under the GNU General Public License, version 3.
#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/bch4028.h"

typedef std::vector<int> Bits;

static void put(Bits &b, uint64_t v, int n) {
    for (int i = n - 1; i >= 0; i--)
        b.push_back((int)((v >> i) & 1));
}

// --------------------------------------------------------------------------------------------------------------
// SmartNet: 84-symbol frames = 76 payload symbols + the 0xAC sync word.

static Bits sn_payload(uint16_t addr, bool grp, uint16_t cmd) {
    int d[37];
    uint32_t a = addr ^ 0xCC38u; // ~0x33C7 & 0xffff
    for (int j = 0; j < 16; j++) d[j] = (a >> (15 - j)) & 1;
    d[16] = grp ? 0 : 1; // group = ~d[16] & 1
    uint32_t c = (uint32_t)cmd ^ 0x0D5u; // ~0x32A & 0x3ff
    for (int j = 0; j < 10; j++) d[17 + j] = (c >> (9 - j)) & 1;
    uint16_t accum = 0x0393, op = 0x036E;
    for (int j = 0; j < 27; j++) {
        if (op & 1) op = (uint16_t)((op >> 1) ^ 0x0225);
        else op >>= 1;
        if (d[j]) accum ^= op;
    }
    for (int j = 0; j < 10; j++) d[27 + j] = 1 - ((accum >> (9 - j)) & 1);
    int info[38];
    for (int k = 0; k < 37; k++) info[k] = d[k];
    info[37] = 0;
    int raw[76];
    for (int k = 0; k < 38; k++) {
        raw[2 * k] = info[k];
        raw[2 * k + 1] = k == 0 ? info[0] : (info[k] ^ info[k - 1]);
    }
    Bits out(76);
    for (int k = 0; k < 19; k++)
        for (int l = 0; l < 4; l++)
            out[(size_t)(k + l * 19)] = raw[k * 4 + l];
    return out;
}

struct Sn {
    Bits bits;
    Sn() { put(bits, 0xAC, 8); } // first sync
    // flips: payload positions (0..75, in air order) to invert
    void osw(uint16_t addr, bool grp, uint16_t cmd, const std::vector<int> &flips = {}) {
        Bits p = sn_payload(addr, grp, cmd);
        for (int f : flips) p[(size_t)f] ^= 1;
        bits.insert(bits.end(), p.begin(), p.end());
        put(bits, 0xAC, 8);
    }
    void idle() { osw(0x2000, false, 0x2f8); }
    void idles(int n) { for (int i = 0; i < n; i++) idle(); }
};

// Positions in air order of the info bit k: raw index 2k sits at buffer position (2k/4) + (2k%4)*19.
static int sn_info_pos(int k) {
    int r = 2 * k;
    return r / 4 + (r % 4) * 19;
}

static Bits scenario_sn_basic() {
    Sn s;
    s.idles(24);
    // two-OSW system id + control channel (channel 0x0a8 = 855.2125 MHz on the 800 MHz standard plan)
    s.osw(0x1234, true, 0x308);
    s.osw(0x1f00, false, 0x0a8);
    s.idles(2);
    // analog group voice grant: source radio 12345, talkgroup 0x1a40 (6720), channel 0x120
    s.osw(12345, false, 0x308);
    s.osw(0x1a40, true, 0x120);
    s.idles(2);
    // the same grant repeated (must be suppressed by --update-interval) and a one-OSW voice update
    s.osw(12345, false, 0x308);
    s.osw(0x1a40, true, 0x120);
    s.osw(0x1a40, true, 0x120);
    s.idles(2);
    // digital group voice grant (0x321 form): source 777, talkgroup 0x2b50 flags 8 = encrypted, channel 0x0f0
    s.osw(777, true, 0x321);
    s.osw(0x2b58, true, 0x0f0);
    s.idles(2);
    // emergency talkgroup (status 2) on an analog grant, channel 0x2d3 (866.0 + band)
    s.osw(4242, false, 0x308);
    s.osw(0x3c02, true, 0x2d3);
    s.idles(2);
    // private call (analog): source 5001 calls 5002 on channel 0x150
    s.osw(5002, false, 0x308);
    s.osw(5001, false, 0x150);
    s.idles(2);
    // affiliation: radio 31337 joins talkgroup 0x1a40
    s.osw(31337, false, 0x308);
    s.osw(0x1a40, false, 0x310);
    s.idles(1);
    // affiliation with an idle delayed into the middle (the idle must be dropped, the affiliation decoded)
    s.osw(31338, false, 0x308);
    s.idle();
    s.osw(0x1a40, false, 0x310);
    s.idles(2);
    // status acknowledgement 3 (0x26e2) for radio 4711 (a 0x30b extended function, osw0 is the next idle)
    s.osw(4711, false, 0x308);
    s.osw(0x26e2, false, 0x30b);
    s.idles(2);
    // call denied: radio ID refused
    s.osw(4712, false, 0x308);
    s.osw(0x2c65, false, 0x30b);
    s.idles(2);
    // dynamic regroup, group busy queued, emergency busy queued
    s.osw(6001, false, 0x308);
    s.osw(0x1a50, false, 0x30a);
    s.osw(0x1a40, true, 0x300);
    s.osw(0x1a40, true, 0x303);
    s.idles(2);
    // date/time: 2026-11-12, Thursday (4), 14:35
    {
        uint16_t date = (uint16_t)(((2026 - 2000) << 9) | (11 << 5) | 12);
        uint16_t time = (uint16_t)((4 << 13) | (14 << 8) | 35);
        s.osw(date, true, 0x308);
        s.osw(time, true, 0x322);
    }
    s.idles(2);
    // three-OSW system id + control channel
    s.osw(0x1234, true, 0x308);
    s.osw((uint16_t)(0x2800 | 0x0a8), true, 0x30b);
    s.osw(0x1f00, false, 0x0a8);
    s.idles(2);
    // adjacent control channel announced with the system id (0x6000 | channel)
    s.osw(0x1234, false, 0x308);
    s.osw((uint16_t)(0x6000 | 0x0c8), false, 0x30b);
    s.idles(3);
    // patch: talkgroup 0x0010 (index) with a patch group 0x1a43 (type 3)
    s.osw(0x1a43, true, 0x308);
    s.osw(0x0010, true, 0x340);
    s.idles(2);
    // emergency PTT
    s.osw(8888, true, 0x308);
    s.osw(0x1a40, true, 0x32e);
    s.idles(2);
    // AMSS site 5 (0x364), a one-OSW system id marker (0x32b) and a plain control channel word
    s.osw(0, false, 0x364);
    s.osw(0x1234, false, 0x32b);
    s.osw(0x1f00, false, 0x0a8);
    s.idles(10);
    return s.bits;
}

static Bits scenario_sn_damaged() {
    Sn s;
    s.idles(24);
    s.osw(12345, false, 0x308);
    s.osw(0x1a40, true, 0x120); // clean grant: reported
    s.idles(2);
    // one flipped data bit in the second half: corrected by the parity rule, grant still reported
    s.osw(2001, false, 0x308);
    s.osw(0x1b50, true, 0x121, {sn_info_pos(5)});
    s.idles(2);
    // one flipped parity bit: harmless
    s.osw(2002, false, 0x308);
    s.osw(0x1b60, true, 0x122, {1});
    s.idles(2);
    // two adjacent data bits flipped in the second half (the parity rule cannot correct that): CRC fails, the grant is lost, a gap is seen, nothing is invented
    s.osw(2003, false, 0x308);
    s.osw(0x1b70, true, 0x123, {sn_info_pos(4), sn_info_pos(5)});
    s.idles(2);
    // two adjacent data bits flipped in the first half: the lone second half is a one-OSW voice update, no source radio
    s.osw(2004, false, 0x308, {sn_info_pos(10), sn_info_pos(11)});
    s.osw(0x1b80, true, 0x124);
    s.idles(2);
    // a stretch of garbage between two good frames, then recovery
    for (int i = 0; i < 300; i++) s.bits.push_back((i * 7 + (i >> 3)) & 1 ? 1 : 0);
    s.idles(6);
    s.osw(2005, false, 0x308);
    s.osw(0x1b90, true, 0x125);
    s.idles(10);
    return s.bits;
}

// OBT (400 MHz) plan: base 421.0 MHz, high 429.0 MHz, spacing 25 kHz, channel 380 = base.
static Bits scenario_sn_obt() {
    Sn s;
    s.idles(24);
    // two-OSW OBT group grant: osw2 is on a tx channel word (cmd 100 .. 379), osw1 on an rx channel (cmd >= 380)
    s.osw(9001, true, 100);      // source 9001, tx channel word
    s.osw(0x2310, true, 400);    // talkgroup 0x2310, rx channel 400 => 421.0 + 20 * 0.025 = 421.5 MHz
    s.idles(3);
    // OBT three-OSW site information: system 0x4321, site 7, control channel 390
    s.osw(0x4321, true, 120);                         // tx channel word, group
    s.osw((uint16_t)((6 << 10) | 0), true, 0x320);    // site = 6 + 1
    s.osw((uint16_t)(0x6000 | 390), false, 0x30b);    // control channel 390
    s.idles(3);
    s.osw(0x1000, false, 130);   // OBT idle: tx word followed by 0x2f8
    s.osw(0x2000, false, 0x2f8);
    s.idles(6);
    return s.bits;
}

static Bits scenario_sn_idle() {
    Sn s;
    s.idles(60);
    return s.bits;
}

// --------------------------------------------------------------------------------------------------------------
// EDACS: 48-bit sync, then two 40-bit messages, each sent three times.

static const uint64_t EDACS_SYNC = 0x555557125555ull;

static bool g_esk = false; // XOR the command byte of message A with 0xA0 (ESK)

struct Ed {
    Bits bits;
    Ed() { for (int i = 0; i < 64; i++) bits.push_back(i & 1); } // dotting lead-in
    // a and b: 40-bit message words with the parity field ignored (re-computed here). fa / fb: per copy (0..2)
    // the bit positions (0..39 of the 40-bit word, 39 = first on air) to invert.
    typedef std::vector<std::vector<int>> Flips;
    void frame(uint64_t a, uint64_t b, const Flips &fa = {}, const Flips &fb = {}) {
        put(bits, EDACS_SYNC, 48);
        if (g_esk) a ^= 0xA0ull << 32;
        uint64_t wa = trunkctl::bch4028_encode((uint32_t)((a >> 12) & 0xFFFFFFF));
        uint64_t wb = trunkctl::bch4028_encode((uint32_t)((b >> 12) & 0xFFFFFFF));
        for (int c = 0; c < 3; c++) {
            uint64_t w = wa;
            if ((size_t)c < fa.size()) for (int f : fa[(size_t)c]) w ^= 1ull << f;
            put(bits, w, 40);
        }
        for (int c = 0; c < 3; c++) {
            uint64_t w = wb;
            if ((size_t)c < fb.size()) for (int f : fb[(size_t)c]) w ^= 1ull << f;
            put(bits, w, 40);
        }
    }
};

static uint64_t ea_site(int site) {
    return (0x1Full << 35) | (0xAull << 31) | ((uint64_t)((site >> 5) & 0x1F) << 24) | ((uint64_t)(site & 0x1F) << 12);
}
static uint64_t ea_peer(int peer, int lcn) { return (0x1Full << 35) | (1ull << 31) | ((uint64_t)lcn << 24) | ((uint64_t)peer << 12); }
static uint64_t ea_cc_a() { return (0x1Full << 35) | (8ull << 31); }
static uint64_t ea_cc_b(int lcn) { return (uint64_t)lcn << 12; }
static uint64_t ea_call_a(int mt1, int lcn, int group) { return ((uint64_t)mt1 << 35) | ((uint64_t)lcn << 29) | ((uint64_t)group << 12); }
static uint64_t sender_b(int rid) { return (uint64_t)rid << 12; }
static uint64_t ea_patch_a(int source) { return (0x1Full << 35) | (0xCull << 31) | ((uint64_t)source << 12); }
static uint64_t ea_patch_b(int site, int target) { return ((uint64_t)site << 32) | ((uint64_t)target << 12); }
static uint64_t ea_kick_a() { return (0x1Full << 35) | (0xBull << 31); }

static Bits scenario_ed_ea() {
    Ed e;
    // peer listing first: it identifies extended addressing for --variant auto
    e.frame(ea_peer(22, 3), ea_cc_b(0));
    e.frame(ea_site(137), 0);
    e.frame(ea_cc_a(), ea_cc_b(1));
    e.frame(ea_call_a(3, 5, 0x1234), sender_b(654321)); // digital group call, LCN 5, group 0x1234, sender 654321
    e.frame(ea_call_a(3, 5, 0x1234), sender_b(654321)); // repeat: suppressed
    e.frame(ea_call_a(2, 7, 0x00AB), sender_b(100001)); // group data call
    e.frame(ea_call_a(1, 9, 0x0777), sender_b(42));     // TDMA call
    e.frame(ea_call_a(3, 27, 0x1234), sender_b(1));     // LCN 27 = reserved (a call state, not a channel)
    e.frame(ea_patch_a(0x2001), ea_patch_b(3, 0x2002));
    e.frame(ea_kick_a(), sender_b(555555));
    e.frame(ea_peer(23, 4), 0);
    e.frame(ea_site(137), 0);
    return e.bits;
}

static uint64_t std_site(int site, int cclcn) { return (0xFDull << 32) | ((uint64_t)cclcn << 24) | ((uint64_t)site << 12); }
static uint64_t std_call_a(int cmd, int lcn, int afs) { return ((uint64_t)cmd << 32) | ((uint64_t)lcn << 27) | ((uint64_t)afs << 12); }

static Bits scenario_ed_std() {
    Ed e;
    e.frame(std_call_a(0xEE, 2, 0x625), sender_b(98765)); // analog call, AFS 0x625 (agency 12, fleet 4, subfleet 5)
    e.frame(std_site(41, 1), 0);
    e.frame(std_call_a(0xEF, 12, 0x0A3), sender_b(1234));   // digital call
    e.frame(std_call_a(0xEE, 2, 0x625), sender_b(98765));   // repeat: suppressed
    e.frame(std_call_a(0xEE, 30, 0x0A3), sender_b(1234));   // LCN 30 = system busy
    e.frame(std_site(41, 1), 0);
    return e.bits;
}

static Bits scenario_ed_damaged() {
    Ed e;
    e.frame(ea_peer(22, 3), 0);
    // one error in every copy of each message: only the BCH correction can rescue it (default --bch-correct 1)
    e.frame(ea_call_a(3, 5, 0x1111), sender_b(1001), {{20}, {21}, {22}}, {{33}, {34}, {35}});
    // two errors in copy 0 of message A, copies 1 and 2 clean: a clean copy wins, nothing is corrected
    e.frame(ea_call_a(3, 6, 0x2222), sender_b(1002), {{10, 30}}, {});
    // two errors in every copy of message A: not correctable at --bch-correct 1, the call is dropped
    e.frame(ea_call_a(3, 7, 0x3333), sender_b(1003), {{5, 25}, {6, 26}, {7, 27}}, {});
    // message B hopeless in all copies: a call needs B so nothing is reported; the site id (A only) still is
    e.frame(ea_site(55), 0, {}, {{1, 9, 17, 25, 33}, {2, 10, 18, 26, 34}, {3, 11, 19, 27, 35}});
    e.frame(ea_call_a(3, 8, 0x4444), sender_b(1004), {}, {{1, 9, 17, 25, 33}, {2, 10, 18, 26, 34}, {3, 11, 19, 27, 35}});
    e.frame(ea_call_a(3, 9, 0x5555), sender_b(1005));
    return e.bits;
}

// --------------------------------------------------------------------------------------------------------------
// modulation

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
    double u() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0; }
    double g() { double a = 0; for (int i = 0; i < 12; i++) a += u(); return a - 6.0; }
};

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: trunkgen <scenario> <rate> <s16|f32|cs16|cf32> <out> [invert] [offset=N] [noise=A] [seed=N]\n");
        return 2;
    }
    std::string sc = argv[1];
    double rate = atof(argv[2]);
    std::string fmt = argv[3];
    const char *outp = argv[4];
    bool invert = false;
    double offset = 0, noise = 0, tail_s = 0;
    uint32_t seed = 1;
    for (int i = 5; i < argc; i++) {
        std::string a = argv[i];
        if (a == "invert") invert = true;
        else if (a == "esk") g_esk = true;
        else if (a.rfind("offset=", 0) == 0) offset = atof(a.c_str() + 7);
        else if (a.rfind("noise=", 0) == 0) noise = atof(a.c_str() + 6);
        else if (a.rfind("tail=", 0) == 0) tail_s = atof(a.c_str() + 5);
        else if (a.rfind("seed=", 0) == 0) seed = (uint32_t)atol(a.c_str() + 5);
        else { fprintf(stderr, "trunkgen: unknown argument %s\n", a.c_str()); return 2; }
    }
    Bits bits;
    double baud;
    if (sc == "sn_basic") { bits = scenario_sn_basic(); baud = 3600; }
    else if (sc == "sn_damaged") { bits = scenario_sn_damaged(); baud = 3600; }
    else if (sc == "sn_obt") { bits = scenario_sn_obt(); baud = 3600; }
    else if (sc == "sn_idle") { bits = scenario_sn_idle(); baud = 3600; }
    else if (sc == "ed_ea") { bits = scenario_ed_ea(); baud = 9600; }
    else if (sc == "ed_std") { bits = scenario_ed_std(); baud = 9600; }
    else if (sc == "ed_damaged") { bits = scenario_ed_damaged(); baud = 9600; }
    else { fprintf(stderr, "trunkgen: unknown scenario %s\n", sc.c_str()); return 2; }
    bool iq = fmt == "cs16" || fmt == "cf32";
    if (!iq && fmt != "s16" && fmt != "f32") { fprintf(stderr, "trunkgen: bad format\n"); return 2; }
    if (invert) for (size_t i = 0; i < bits.size(); i++) bits[i] ^= 1;

    const double sps = rate / baud;
    const size_t lead = (size_t)(offset * sps);
    const size_t tail = (size_t)(40 * sps) + (size_t)(tail_s * rate); // optional silence after the last frame
    const size_t total = lead + (size_t)(bits.size() * sps) + tail;
    // sample-and-hold NRZ, then a one-pole low-pass (cut-off about 0.6 x baud) like a post-detection filter
    const double alpha = 1.0 - std::exp(-2.0 * M_PI * 0.6 * baud / rate);
    Rng rng(seed);
    FILE *f = fopen(outp, "wb");
    if (!f) { perror(outp); return 1; }
    double y = 0, phase = 0;
    const double dev_hz = baud * 0.6;
    for (size_t n = 0; n < total; n++) {
        double x = 0;
        if (n >= lead) {
            size_t k = (size_t)((double)(n - lead) / sps);
            if (k < bits.size()) x = bits[k] ? 1.0 : -1.0;
        }
        y += alpha * (x - y);
        double v = y;
        if (!iq) {
            v += noise * rng.g() / 3.0;
            if (fmt == "s16") {
                double s = v * 8000.0;
                if (s > 32767) s = 32767;
                if (s < -32768) s = -32768;
                int16_t o = (int16_t)std::lround(s);
                fwrite(&o, 2, 1, f);
            } else {
                float o = (float)v;
                fwrite(&o, 4, 1, f);
            }
        } else {
            phase += 2.0 * M_PI * dev_hz * v / rate;
            double re = std::cos(phase), im = std::sin(phase);
            if (noise > 0) { re += noise * rng.g() / 6.0; im += noise * rng.g() / 6.0; }
            if (fmt == "cs16") {
                int16_t o[2] = {(int16_t)std::lround(re * 8000.0), (int16_t)std::lround(im * 8000.0)};
                fwrite(o, 2, 2, f);
            } else {
                float o[2] = {(float)re, (float)im};
                fwrite(o, 4, 2, f);
            }
        }
    }
    fclose(f);
    return 0;
}
