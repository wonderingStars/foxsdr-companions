// hfmodes test signal generator: writes raw mono s16 little-endian audio for the CTest cases. Uses the generators that ship with the OnAir
// decoders (genFsk, genBee, genCis14 ..., genSitorArq), a CROWD36 and a SITOR-B generator written here, then takes the real part of the
// complex baseband, resamples it to the requested rate and adds white noise (deterministic: mt19937 + Box-Muller, no std distributions).
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/crowd36.h"
#include "../src/frontend.h"
#include "dect2/cishf/cishf_dsp.h"
#include "dect2/hfutil/hfu_fsk.h"
#include "dect2/marine/marine_navtex.h"
#include "dect2/sitor/sitor_arq.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

using dect2::cf32;
namespace {
constexpr double kPi = 3.14159265358979323846;

struct Sig { std::vector<float> a; double fs; double snrDb = 200; };   // real audio at its native rate

// shiftHz: move the complex baseband up first, so a wide signal that the OnAir generator centres on 1500 Hz does not reach below 0 Hz
// (a real audio signal cannot hold negative frequencies; they would fold back over the positive ones)
Sig realPart(const std::vector<cf32>& x, double fs, double snr = 200, double shiftHz = 0) {
    Sig s; s.fs = fs; s.snrDb = snr;
    s.a.reserve(x.size());
    double ph = 0; const double dp = 2 * kPi * shiftHz / fs;
    for (const cf32& v : x) {
        if (shiftHz != 0) { s.a.push_back((float)(v.real() * std::cos(ph) - v.imag() * std::sin(ph))); ph += dp; if (ph > 2 * kPi) ph -= 2 * kPi; }
        else s.a.push_back(v.real());
    }
    return s;
}

// Band-pass the real audio like a 3 kHz SSB channel would (the OnAir generators for the wide CIS signals use short truncated pulses whose
// sidelobes reach the Nyquist frequency; a transmitter and a receiver filter do not).
Sig bandLimit(Sig s, double lo, double hi) {
    const int taps = 801, c = taps / 2;
    std::vector<double> h(taps);
    for (int i = 0; i < taps; i++) {
        const double t = i - c;
        const double a = t == 0 ? 2 * (hi - lo) / s.fs : (std::sin(2 * kPi * hi * t / s.fs) - std::sin(2 * kPi * lo * t / s.fs)) / (kPi * t);
        h[i] = a * (0.42 - 0.5 * std::cos(2 * kPi * i / (taps - 1)) + 0.08 * std::cos(4 * kPi * i / (taps - 1)));
    }
    std::vector<float> o(s.a.size(), 0.f);
    for (size_t n = 0; n < s.a.size(); n++) {
        double acc = 0;
        for (int k = 0; k < taps; k++) { const long j = (long)n + c - k; if (j >= 0 && j < (long)s.a.size()) acc += h[k] * s.a[(size_t)j]; }
        o[n] = (float)acc;
    }
    s.a = o;
    return s;
}

const char* kMsg = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 12345 HF UTILITY TEST MESSAGE";

std::vector<int> randomChars(std::mt19937& g, int n) {
    std::vector<int> valid;
    for (int c = 0; c < 128; c++) if (dect2::cishf::beeValidChar(c) && c != dect2::cishf::kBeeEom) valid.push_back(c);
    std::vector<int> v;
    for (int i = 0; i < n; i++) v.push_back(valid[g() % valid.size()]);
    return v;
}

Sig fsk(dect2::hfu::FskKind k, double baud, double shift, int cycle, int depth, double snr, const char* text = kMsg, int idle = 150) {
    dect2::hfu::FskGenCfg c; c.kind = k; c.baud = baud; c.shift = shift; c.cycle = cycle; c.depth = depth; c.idleChars = idle;
    return realPart(dect2::hfu::genFsk(c, text), 24000, snr);
}

Sig mfsk(dect2::hfu::FskKind k, double ms, double snr) {
    dect2::hfu::FskGenCfg c; c.kind = k; c.toneMs = ms; c.centre = 0; c.idleChars = 40;
    return realPart(dect2::hfu::genFsk(c, kMsg), 24000, snr);
}

// CROWD36 test signal (8 kHz): alternating sync tones, then the text as tones with the letters / figures shifts, then silence.
Sig crowd36(const std::string& text, int baud, double snr) {
    const double fs = 8000;
    const int sps = (int)(fs / baud);
    std::vector<int> tones;
    for (int i = 0; i < 30; i++) { tones.push_back(24); tones.push_back(14 + 4); }     // sync tones: the high tone (number 24) and a lower one
    bool fig = false;
    for (char ch : text) {
        int t = hfm::Crowd36::toneForChar(ch, fig);
        if (t < 0) { t = hfm::Crowd36::toneForChar(ch, !fig); if (t < 0) continue; tones.push_back(fig ? 26 : 14); fig = !fig; }   // <ls> is tone 26, <fs> is tone 14
        tones.push_back(t);
    }
    tones.push_back(18);   // cr
    Sig s; s.fs = fs; s.snrDb = snr;
    double ph = 0;
    for (int t : tones) {
        const double f = hfm::Crowd36::toneTable()[t];
        for (int i = 0; i < sps; i++) {
            // the transmitter's amplitude dips for about 3 ms around each tone change (what the decoder's symbol timing looks for)
            const double edge = std::min(i, sps - 1 - i) / (fs * 0.0015);
            const double env = edge >= 1 ? 1.0 : 0.25 + 0.75 * edge;
            ph += 2 * kPi * f / fs;
            s.a.push_back((float)(0.5 * env * std::sin(ph)));
        }
    }
    s.a.resize(s.a.size() + (size_t)(4 * fs), 0.f);    // the decoder reads 1.28 s behind its input: give it time to finish
    // leading silence so the hunt starts on an empty buffer
    s.a.insert(s.a.begin(), (size_t)(0.5 * fs), 0.f);
    return s;
}

// SITOR-B (FEC) at 100 Bd, 170 Hz shift, centre 1500 Hz, 24 kHz: text -> CCIR 476 -> DX / RX slots -> FSK
Sig sitorB(const std::string& text, double snr) {
    std::vector<uint8_t> codes = dect2::marine::navtexEncode(text);
    std::vector<int> tok;
    for (int i = 0; i < 40; i++) tok.push_back(-1);
    for (uint8_t c : codes) tok.push_back(c);
    for (int i = 0; i < 40; i++) tok.push_back(-1);
    std::vector<uint8_t> slots = dect2::marine::sitorBSlots(tok);
    std::vector<uint8_t> bits = dect2::marine::codesToBits(slots);
    const double fs = 24000, baud = 100, shift = 170, centre = 1500;
    Sig s; s.fs = fs; s.snrDb = snr;
    s.a.assign((size_t)(0.5 * fs), 0.f);
    double ph = 0, acc = 0;
    for (uint8_t b : bits) {
        const double f = centre + (b ? shift / 2 : -shift / 2);
        acc += fs / baud;
        const int n = (int)std::floor(acc); acc -= n;
        for (int i = 0; i < n; i++) { ph += 2 * kPi * f / fs; s.a.push_back((float)(0.4 * std::sin(ph))); }
    }
    s.a.resize(s.a.size() + (size_t)(0.5 * fs), 0.f);
    return s;
}

std::string gTruth;   // what the signal carries, written next to the output for the test ("<out>.truth")

// CIS 36-50 in the form Rivet reads (8 kHz audio): 50 Bd, 200 Hz shift (1400 / 1600 Hz), the 1 bit on the LOWER tone, an alternating sync, the 44-bit start word
// (21 ones), the 70-bit session key twice, 3-of-7 characters, the end of message 1110111 x3, then alternating sync and silence.
Sig cis3650(double snr) {
    std::mt19937 g(11);
    std::vector<int> key = randomChars(g, 10), msg = randomChars(g, 60);
    std::vector<int> bits;
    for (int i = 0; i < 150; i++) bits.push_back(i & 1);
    for (int i = 43; i >= 0; i--) bits.push_back((int)((0xA5C3B1E4D21ULL >> i) & 1));
    for (int rep = 0; rep < 2; rep++) for (int c : key) for (int b = 6; b >= 0; b--) bits.push_back((c >> b) & 1);
    for (int c : msg) for (int b = 6; b >= 0; b--) bits.push_back((c >> b) & 1);
    for (int r = 0; r < 3; r++) for (int b = 6; b >= 0; b--) bits.push_back((0x77 >> b) & 1);
    for (int i = 0; i < 100; i++) bits.push_back(i & 1);
    for (int c : msg) { char h[8]; std::snprintf(h, sizeof h, "0x%02x ", c); gTruth += h; }
    Sig s; s.fs = 8000; s.snrDb = snr;
    s.a.assign((size_t)(0.5 * 8000), 0.f);
    double ph = 0;
    for (int b : bits) for (int i = 0; i < 160; i++) { ph += 2 * kPi * (b ? 1400.0 : 1600.0) / 8000.0; s.a.push_back((float)(0.5 * std::sin(ph))); }
    s.a.resize(s.a.size() + (size_t)(3 * 8000), 0.f);
    return s;
}

Sig bee(double snr) {
    std::mt19937 g(7);
    dect2::cishf::GenOpts o; o.trafficBaud = 50;
    dect2::cishf::BeeMsg a; a.key = randomChars(g, 10); a.chars = randomChars(g, 60);
    dect2::cishf::BeeMsg b; b.withStart = false; b.chars = randomChars(g, 30);
    for (int c : a.chars) { char h[8]; std::snprintf(h, sizeof h, "%02X ", c); gTruth += h; }
    return realPart(dect2::cishf::genBee(o, {a, b}), 12000, snr);
}
Sig cis14(double snr) {
    std::mt19937 g(5);
    dect2::cishf::GenOpts o;
    std::vector<uint16_t> fr;
    for (int i = 0; i < 400; i++) fr.push_back(dect2::cishf::cis14Frame(2, (int)(g() % 32), (int)(g() % 32), (int)(g() % 4)));
    return realPart(dect2::cishf::genCis14(o, fr), 12000, snr);
}

std::map<std::string, std::function<Sig()>> cases() {
    using K = dect2::hfu::FskKind;
    std::map<std::string, std::function<Sig()>> m;
    m["arqe"] = [] { return fsk(K::ArqE, 96, 400, 8, 11, 18, kMsg, 200); };
    m["arqn"] = [] { return fsk(K::ArqN, 96, 850, 0, 11, 18, kMsg, 200); };
    m["autospec"] = [] { return fsk(K::Autospec, 68.5, 270, 0, 1, 20); };
    m["spread"] = [] { return fsk(K::Spread, 102.7, 410, 0, 11, 20); };
    m["gtor"] = [] { dect2::hfu::FskGenCfg c; c.kind = K::GTor; c.gtorBaud = 200; return realPart(dect2::hfu::genFsk(c, "G-TOR test frame at the given speed, with a CRC check."), 24000, 22); };
    m["efr"] = [] { dect2::hfu::FskGenCfg c; c.kind = K::Efr; return realPart(dect2::hfu::genFsk(c, "ABC"), 24000, 20); };
    m["piccolo"] = [] { return mfsk(K::Piccolo6, 50, 22); };
    m["coquelet8"] = [] { return mfsk(K::Coquelet8, 75, 22); };
    m["coquelet13"] = [] { return mfsk(K::Coquelet13, 75, 22); };
    m["bee"] = [] { return bee(25); };
    m["cis3650"] = [] { return cis3650(25); };
    m["cis14"] = [] { return cis14(25); };
    m["cis12"] = [] { dect2::cishf::GenOpts o; o.symbols = 600; return realPart(dect2::cishf::genCis12(o, nullptr), 12000, 25); };
    m["cis36"] = [] { dect2::cishf::GenOpts o; o.trafficBaud = 20; o.symbols = 800; return realPart(dect2::cishf::genCis36(o), 12000, 25); };
    m["hdr"] = [] { dect2::cishf::GenOpts o; o.variant = 0; o.symbols = 400; return bandLimit(realPart(dect2::cishf::genOfdm(dect2::cishf::Hdr, o), 12000, 25, 0), 60, 3800); };
    m["c128"] = [] { dect2::cishf::GenOpts o; o.symbols = 300; return bandLimit(realPart(dect2::cishf::genOfdm(dect2::cishf::C128, o), 12000, 25, 0), 60, 3800); };
    m["c3000"] = [] { dect2::cishf::GenOpts o; o.symbols = 24000; return bandLimit(realPart(dect2::cishf::genCis3000(o), 12000, 25, 0), 60, 3800); };
    m["crowd36"] = [] { return crowd36("THE QUICK BROWN FOX 1234 JUMPS OVER THE LAZY DOG", 40, 30); };
    m["sitorb"] = [] { return sitorB("CQ CQ CQ DE TEST 1234 NAVTEX FEC BROADCAST", 25); };
    m["sitora"] = [] {
        dect2::sitor::ArqGenCfg c; c.centreHz = 1500; c.rate = 24000;
        return realPart(dect2::sitor::genSitorArq(c, "CQ DE TEST HELLO WORLD 1234"), 24000, 25);
    };
    m["silence"] = [] { Sig s; s.fs = 8000; s.a.assign(8000 * 3, 0.f); return s; };
    m["noise"] = [] { Sig s; s.fs = 8000; s.a.assign(8000 * 20, 0.f); s.snrDb = -100; return s; };   // noise only: handled below
    return m;
}

double gauss(std::mt19937& g) {
    const double u1 = ((double)g() + 1.0) / 4294967297.0, u2 = ((double)g() + 1.0) / 4294967297.0;
    return std::sqrt(-2 * std::log(u1)) * std::cos(2 * kPi * u2);
}

}  // namespace

int main(int argc, char** argv) {
    std::string name, out; double rate = 0, snrOverride = -1000;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--case" && i + 1 < argc) name = argv[++i];
        else if (a == "--rate" && i + 1 < argc) rate = std::atof(argv[++i]);
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--snr" && i + 1 < argc) snrOverride = std::atof(argv[++i]);
        else if (a == "--list") { for (auto& c : cases()) std::printf("%s\n", c.first.c_str()); return 0; }
        else { std::fprintf(stderr, "usage: hfmodes_gen --case <name> --rate <Hz> --out <file> | --list\n"); return 2; }
    }
    if (name == "garbage") {      // random bytes: not audio of any kind (also read as f32 by the failure-path test)
        std::mt19937 g(777);
        FILE* f = std::fopen(out.c_str(), "wb");
        if (!f) return 1;
        for (int i = 0; i < 300001; i++) { const unsigned char b = (unsigned char)(g() >> 13); std::fwrite(&b, 1, 1, f); }
        std::fclose(f);
        return 0;
    }
    auto cs = cases();
    if (!cs.count(name) || rate <= 0 || out.empty()) { std::fprintf(stderr, "hfmodes_gen: need --case (see --list), --rate and --out\n"); return 2; }
    Sig s = cs[name]();
    if (snrOverride > -999) s.snrDb = snrOverride;
    hfm::Resampler rs(s.fs, rate);
    std::vector<float> a;
    // pad with silence so the resampler's filter tail is flushed
    s.a.resize(s.a.size() + 4096, 0.f);
    rs.push(s.a.data(), s.a.size(), a);
    // noise
    std::mt19937 g(12345);
    if (name == "noise") {
        for (auto& v : a) v = (float)(0.2 * gauss(g));
    } else if (s.snrDb < 100) {
        double p = 0; size_t n = 0;
        for (float v : a) if (std::fabs(v) > 1e-4) { p += (double)v * v; n++; }
        p = n ? p / (double)n : 0;
        const double sigma = std::sqrt(p / std::pow(10.0, s.snrDb / 10) * (rate / 2) / 3000.0);   // SNR in a 3 kHz band
        for (auto& v : a) v += (float)(sigma * gauss(g));
    }
    float peak = 0; for (float v : a) peak = std::max(peak, std::fabs(v));
    const float scale = peak > 0 ? 0.7f / peak : 1.f;
    if (!gTruth.empty()) { FILE* t = std::fopen((out + ".truth").c_str(), "wb"); if (t) { std::fwrite(gTruth.data(), 1, gTruth.size(), t); std::fclose(t); } }
    FILE* f = std::fopen(out.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "hfmodes_gen: cannot write %s\n", out.c_str()); return 1; }
    for (float v : a) { int x = (int)std::lrintf(v * scale * 32767.f); if (x > 32767) x = 32767; if (x < -32768) x = -32768; const int16_t s16 = (int16_t)x; std::fwrite(&s16, 2, 1, f); }
    std::fclose(f);
    return 0;
}
