// hfmodes: FoxSDR GPL helper (pipe protocol version 1). Reads mono audio on stdin, prints JSON Lines on stdout.
// Decoders: OnAir's HF utility FSK / MFSK modes (ARQ-E, ARQ-N, AUTOSPEC, SPREAD, G-TOR, EFR, Piccolo Mk VI, Coquelet-8, Coquelet-13), OnAir's CIS modes
// (CIS-36-50, CIS-14, CIS-12, CIS-36, CIS OFDM HDR, CIS-128, CIS-3000), OnAir's SITOR-B (FEC) and SITOR-A (ARQ) monitor, and Rivet's CROWD36.
//
// The glue in this file is written for this helper. The decoders are in third_party/onair and crowd36.cpp; see UPSTREAM.md.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "cis3650.h"
#include "crowd36.h"
#include "frontend.h"
#include "dect2/cishf/cishf_dsp.h"
#include "dect2/hfutil/hfu_fsk.h"
#include "dect2/marine/marine_dsp.h"
#include "dect2/marine/marine_navtex.h"
#include "dect2/sitor/sitor_arq.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

#define HFMODES_VERSION "0.1.0"
#define HFMODES_ATTRIBUTION "Based on OnAir by Kristijonas Adomaitis - https://on-air.dev"

namespace {

using hfm::cplx;

const char* kManifest =
    "{\"name\":\"hfmodes\",\"version\":\"" HFMODES_VERSION "\",\"licence\":\"GPL-3.0-or-later\","
    "\"source\":\"https://github.com/adswill/OnAir\",\"executable\":\"hfmodes.exe\",\"input\":\"audio\",\"format\":[\"s16\",\"f32\"],"
    "\"rate\":{\"min\":8000,\"max\":192000},"
    "\"attribution\":\"" HFMODES_ATTRIBUTION "\","
    "\"events\":["
    "{\"type\":\"text\",\"fields\":[\"mode\",\"text\"]},"
    "{\"type\":\"event\",\"fields\":[\"mode\",\"text\"]},"
    "{\"type\":\"status\",\"fields\":[\"mode\",\"state\",\"text\",\"centre_hz\",\"shift_hz\",\"baud\",\"detail\",\"snr_db\",\"params\",\"good\",\"bad\"]},"
    "{\"type\":\"stats\",\"fields\":[\"mode\",\"good\",\"bad\",\"samples\",\"seconds\"]}],"
    "\"modes\":[\"ARQ-E\",\"ARQ-N\",\"AUTOSPEC\",\"SPREAD\",\"G-TOR\",\"EFR\",\"PICCOLO-MK6\",\"COQUELET-8\",\"COQUELET-13\","
    "\"CIS-36-50\",\"CIS-36-50-ONAIR\",\"CIS-14\",\"CIS-12\",\"CIS-36\",\"CIS-OFDM-HDR\",\"CIS-128\",\"CIS-3000\",\"CROWD36\",\"SITOR-B\",\"SITOR-A\"]}";

// ---- JSON output -------------------------------------------------------------------------------------------------------------
std::string jstr(const std::string& s) {
    std::string o = "\"";
    char b[8];
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        if (c == '"') { o += "\\\""; i++; }
        else if (c == '\\') { o += "\\\\"; i++; }
        else if (c == '\n') { o += "\\n"; i++; }
        else if (c == '\r') { o += "\\r"; i++; }
        else if (c == '\t') { o += "\\t"; i++; }
        else if (c < 0x20 || c == 0x7f) { std::snprintf(b, sizeof b, "\\u%04x", c); o += b; i++; }
        else if (c < 0x80) { o += (char)c; i++; }
        else {
            // valid UTF-8 sequences pass through; any other byte becomes U+0080..U+00FF so the line is always valid JSON
            size_t n = c >= 0xF0 && c < 0xF8 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 ? 2 : 0;
            bool ok = n > 0 && c < 0xF8 && i + n <= s.size();
            for (size_t k = 1; ok && k < n; k++) ok = ((unsigned char)s[i + k] & 0xC0) == 0x80;
            if (ok) { o.append(s, i, n); i += n; }
            else { std::snprintf(b, sizeof b, "\\u%04x", c); o += b; i++; }
        }
    }
    return o + "\"";
}
std::string jnum(double v, int prec = 1) {
    if (!std::isfinite(v)) return "0";
    char b[48];
    std::snprintf(b, sizeof b, "%.*f", prec, v);
    return b;
}

struct Out {
    double rate = 48000;
    uint64_t samples = 0;     // input samples consumed so far
    uint64_t lines = 0;
    void line(const std::string& type, const std::string& mode, const std::string& rest) {
        std::string s = "{\"t\":" + jnum((double)samples / rate, 3) + ",\"type\":" + jstr(type);
        if (!mode.empty()) s += ",\"mode\":" + jstr(mode);
        s += rest + "}\n";
        std::fwrite(s.data(), 1, s.size(), stdout);
        std::fflush(stdout);
        lines++;
    }
    void text(const std::string& mode, const std::string& t) { line("text", mode, ",\"text\":" + jstr(t)); }
    void event(const std::string& mode, const std::string& t) { line("event", mode, ",\"text\":" + jstr(t)); }
};

// Text coalescing: a text event leaves at a newline, at 80 characters, or when half a second of stream time has passed since the first
// pending character. Deterministic (stream time only).
struct TextBuf {
    std::string mode, buf;
    double first = 0;
    void add(Out& o, const std::string& s) {
        if (s.empty()) return;
        if (buf.empty()) first = (double)o.samples / o.rate;
        buf += s;
        size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos) { const std::string l = buf.substr(0, nl); buf.erase(0, nl + 1); if (!l.empty()) o.text(mode, l); first = (double)o.samples / o.rate; }
        if (buf.size() >= 80) flush(o);
    }
    void tick(Out& o) { if (!buf.empty() && (double)o.samples / o.rate - first >= 0.5) flush(o); }
    void flush(Out& o) { if (!buf.empty()) { o.text(mode, buf); buf.clear(); } }
};

// ---- a decoder as the main loop sees it ----------------------------------------------------------------------------------------
struct Channel {
    virtual ~Channel() {}
    virtual const std::string& name() const = 0;
    virtual void feed24(const std::vector<cplx>&) {}
    virtual void feed12(const std::vector<cplx>&) {}
    virtual void feed8(const std::vector<float>&) {}
    virtual void poll(Out&) {}               // after every block: status changes, pending text
    virtual void finish(Out&) {}             // end of input
    virtual uint64_t good() const { return 0; }
    virtual uint64_t bad() const { return 0; }
    bool wants24 = false, wants12 = false, wants8 = false;
};


std::string joinParams(const std::vector<dect2::cishf::Param>& p) {
    std::string o = "{";
    for (size_t i = 0; i < p.size(); i++) { if (i) o += ","; o += jstr(p[i].key) + ":" + jstr(p[i].value); }
    return o + "}";
}

// OnAir hfutil FskRx kinds (24 kHz analytic input)
struct FskChannel : Channel {
    std::string nm;
    std::unique_ptr<dect2::hfu::FskRx> rx;
    TextBuf tb;
    std::vector<std::string> pendingEvents;
    int lastState = -1; long lastCentre = -1, lastShift = -1, lastBaud = -1; std::string lastDetail;
    FskChannel(const std::string& name, dect2::hfu::FskKind k) : nm(name), rx(new dect2::hfu::FskRx(k)) {
        wants24 = true; tb.mode = name;
        rx->setTextSink([this](const std::string& s) { pendingText += s; });
        rx->setEventSink([this](const std::string& s) { pendingEvents.push_back(s); });
    }
    std::string pendingText;
    const std::string& name() const override { return nm; }
    void feed24(const std::vector<cplx>& x) override { rx->push(x.data(), x.size()); }
    void poll(Out& o) override {
        for (const std::string& e : pendingEvents) o.event(nm, e);
        pendingEvents.clear();
        const dect2::hfu::FskStatus st = rx->status();
        if (st.state != lastState || std::lround(st.centreHz / 5) != lastCentre || std::lround(st.shiftHz / 5) != lastShift || std::lround(st.baud) != lastBaud || st.detail != lastDetail) {
            if (st.state > 0) {
                const char* sn = st.state >= 2 ? "reading" : "found";
                o.line("status", nm, std::string(",\"state\":") + jstr(sn) + ",\"centre_hz\":" + jnum(st.centreHz) + ",\"shift_hz\":" + jnum(st.shiftHz) + ",\"baud\":" + jnum(st.baud) + ",\"detail\":" + jstr(st.detail));
            }
            lastState = st.state; lastCentre = std::lround(st.centreHz / 5); lastShift = std::lround(st.shiftHz / 5); lastBaud = std::lround(st.baud); lastDetail = st.detail;
        }
        tb.add(o, pendingText); pendingText.clear();
        tb.tick(o);
    }
    void finish(Out& o) override { poll(o); tb.flush(o); }
    uint64_t good() const override { return rx->status().good; }
    uint64_t bad() const override { return rx->status().bad; }
};

// OnAir cishf decoders (12 kHz analytic input)
struct CisChannel : Channel {
    std::string nm;
    std::unique_ptr<dect2::cishf::Decoder> dec;
    TextBuf tb;
    std::string pendingText;
    std::vector<std::string> pendingEvents;
    int lastState = -1; std::string lastMode;
    CisChannel(const std::string& name, int kind) : nm(name), dec(dect2::cishf::makeDecoder(kind)) {
        wants12 = true; tb.mode = name;
        dec->setTextSink([this](const std::string& s) { pendingText += s; });
        dec->setEventSink([this](const std::string& s) { pendingEvents.push_back(s); });
    }
    const std::string& name() const override { return nm; }
    void feed12(const std::vector<cplx>& x) override { dec->push(x.data(), x.size()); }
    void poll(Out& o) override {
        for (const std::string& e : pendingEvents) o.event(nm, e);
        pendingEvents.clear();
        const dect2::cishf::Status st = dec->status();
        if (st.state > 0 && (st.state != lastState || st.mode != lastMode)) {
            o.line("status", nm, std::string(",\"state\":") + jstr(st.state >= 2 ? "reading" : "found") + ",\"text\":" + jstr(st.mode) + ",\"snr_db\":" + jnum(st.snrDb) + ",\"params\":" + joinParams(st.params));
            lastState = st.state; lastMode = st.mode;
        } else if (st.state == 0 && lastState > 0) lastState = 0;
        tb.add(o, pendingText); pendingText.clear();
        tb.tick(o);
    }
    void finish(Out& o) override { poll(o); tb.flush(o); }
    uint64_t good() const override { return dec->status().good; }
    uint64_t bad() const override { return dec->status().bad; }
};

// Rivet CROWD36 (8 kHz real input)
struct CrowdChannel : Channel {
    std::string nm = "CROWD36";
    hfm::Crowd36 rx;
    TextBuf tb;
    std::string pendingText;
    std::vector<std::string> pendingEvents;
    int lastState = -1;
    CrowdChannel(int baud) : rx(baud) {
        wants8 = true; tb.mode = nm;
        rx.setTextSink([this](const std::string& s) { pendingText += s; });
        rx.setEventSink([this](const std::string& s) { pendingEvents.push_back(s); });
    }
    const std::string& name() const override { return nm; }
    void feed8(const std::vector<float>& x) override { rx.push(x.data(), x.size()); }
    void poll(Out& o) override {
        for (const std::string& e : pendingEvents) o.event(nm, e);
        pendingEvents.clear();
        if (rx.state() != lastState) {
            lastState = rx.state();
            if (lastState > 0) o.line("status", nm, std::string(",\"state\":") + jstr(lastState >= 2 ? "reading" : "found") + ",\"text\":" + jstr(lastState >= 2 ? "symbol timing found" : "sync tones found") + ",\"detail\":" + jstr("correction " + std::to_string(rx.correctionHz()) + " Hz"));
        }
        tb.add(o, pendingText); pendingText.clear();
        tb.tick(o);
    }
    void finish(Out& o) override { poll(o); tb.flush(o); }
    uint64_t good() const override { return rx.good(); }
    uint64_t bad() const override { return rx.bad(); }
};

// Rivet CIS 36-50 (8 kHz real input)
struct Cis3650Channel : Channel {
    std::string nm = "CIS-36-50";
    hfm::Cis3650 rx;
    TextBuf tb;
    std::string pendingText;
    std::vector<std::string> pendingEvents;
    int lastState = -1, lastSync = -1;
    Cis3650Channel() {
        wants8 = true; tb.mode = nm;
        rx.setTextSink([this](const std::string& s) { pendingText += s; });
        rx.setEventSink([this](const std::string& s) { pendingEvents.push_back(s); });
    }
    const std::string& name() const override { return nm; }
    void feed8(const std::vector<float>& x) override { rx.push(x.data(), x.size()); }
    void poll(Out& o) override {
        for (const std::string& e : pendingEvents) o.event(nm, e);
        pendingEvents.clear();
        if (rx.state() != lastState || rx.syncState() != lastSync) {
            lastState = rx.state(); lastSync = rx.syncState();
            if (lastState >= 3) o.line("status", nm, std::string(",\"state\":") + jstr(lastSync == 2 ? "reading" : "found") + ",\"text\":" + jstr(rx.detail()));
        }
        tb.add(o, pendingText); pendingText.clear();
        tb.tick(o);
    }
    void finish(Out& o) override { poll(o); tb.flush(o); }
    uint64_t good() const override { return rx.good(); }
    uint64_t bad() const override { return rx.bad(); }
};

// OnAir SITOR: marine FSK front end (finds the 170 Hz pair), a SITOR-B (FEC) decoder and a SITOR-A (ARQ) monitor, each in both tone polarities.
// This is the flow of OnAir's SitorLiveReceiver (core/src/sitor/sitor_live.cpp) without its radio front end.
struct SitorChannel : Channel {
    std::string nm = "SITOR";
    dect2::marine::Spectrum spec;
    dect2::marine::FskSlicer fsk;
    dect2::marine::NavtexDecoder dec[2] = {dect2::marine::NavtexDecoder(false), dect2::marine::NavtexDecoder(true)};
    dect2::sitor::ArqMonitor arq[2] = {dect2::sitor::ArqMonitor(false), dect2::sitor::ArqMonitor(true)};
    std::string fecText[2], arqText[2];
    std::vector<std::string> arqEv[2];
    dect2::marine::Spectrum::Fsk info;
    TextBuf tbB, tbA;
    bool wantB, wantA;
    int lastB = -1, lastA = -1;
    SitorChannel(bool b, bool a) : wantB(b), wantA(a) {
        wants24 = true; tbB.mode = "SITOR-B"; tbA.mode = "SITOR-A";
        for (int i = 0; i < 2; i++) {
            dec[i].setCharCallback([this, i](int ch, bool err) { fecText[i].push_back(err ? '?' : (char)ch); });
            arq[i].setTextSink([this, i](const std::string& s) { arqText[i] += s; });
            arq[i].setEventSink([this, i](const std::string& s) { arqEv[i].push_back(s); });
        }
    }
    const std::string& name() const override { return nm; }
    void feed24(const std::vector<cplx>& x) override {
        if (x.empty()) return;
        if (spec.push(x.data(), x.size())) {
            info = spec.findFsk(170.0, 3000.0);   // OnAir searches +-600 Hz around the dial; audio input has the pair anywhere in the passband
            if (info.found) fsk.setCentreHz(info.centreHz);
        }
        fsk.process(x.data(), x.size(), [&](float v) {
            if (wantB) { dec[0].pushBit(v); dec[1].pushBit(v); }
            if (wantA) { arq[0].pushBit(v); arq[1].pushBit(v); }
        });
    }
    void poll(Out& o) override {
        if (wantB) {
            const int k = dec[1].validChars() > dec[0].validChars() ? 1 : 0;      // the polarity that read more valid words is the right one
            const bool locked = dec[k].locked();
            if (locked ? lastB != 2 : (info.found && lastB < 1)) {
                lastB = locked ? 2 : 1;
                o.line("status", "SITOR-B", std::string(",\"state\":") + jstr(locked ? "reading" : "found") + ",\"centre_hz\":" + jnum(info.centreHz) + ",\"shift_hz\":" + jnum(info.found ? info.highHz - info.lowHz : 0) + (fsk.baud() > 0 ? ",\"baud\":" + jnum(fsk.baud()) : std::string()) + ",\"snr_db\":" + jnum(info.snrDb) + ",\"detail\":" + jstr(k ? "inverted" : "normal polarity"));
            }
            if (locked) tbB.add(o, fecText[k]);
            fecText[0].clear(); fecText[1].clear();
            tbB.tick(o);
        }
        if (wantA) {
            const int q = arq[1].stats().blocks > arq[0].stats().blocks ? 1 : 0;
            const bool locked = arq[q].stats().locked;
            if (locked && lastA != 2) { lastA = 2; o.line("status", "SITOR-A", std::string(",\"state\":\"reading\",\"centre_hz\":") + jnum(info.centreHz) + (fsk.baud() > 0 ? ",\"baud\":" + jnum(fsk.baud()) : std::string()) + ",\"detail\":" + jstr(q ? "inverted" : "normal polarity")); }
            for (const std::string& e : arqEv[q]) o.event("SITOR-A", e);
            arqEv[0].clear(); arqEv[1].clear();
            if (locked) tbA.add(o, arqText[q]);
            arqText[0].clear(); arqText[1].clear();
            tbA.tick(o);
        }
    }
    void finish(Out& o) override { poll(o); if (wantB) { dec[0].flush(); dec[1].flush(); tbB.flush(o); } tbA.flush(o); }
    uint64_t good() const override { return std::max(dec[0].validChars(), dec[1].validChars()); }
    uint64_t bad() const override { return std::min(dec[0].badChars(), dec[1].badChars()); }
};

struct Entry { const char* name; int id; };
enum { K_CIS3650R, K_ARQE, K_ARQN, K_AUTOSPEC, K_SPREAD, K_GTOR, K_EFR, K_PICCOLO, K_COQ8, K_COQ13, K_CIS3650, K_CIS14, K_CIS12, K_CIS36, K_HDR, K_C128, K_C3000, K_CROWD36, K_SITORB, K_SITORA, K_COUNT };
const char* kNames[K_COUNT] = {"CIS-36-50", "ARQ-E", "ARQ-N", "AUTOSPEC", "SPREAD", "G-TOR", "EFR", "PICCOLO-MK6", "COQUELET-8", "COQUELET-13", "CIS-36-50-ONAIR", "CIS-14", "CIS-12", "CIS-36", "CIS-OFDM-HDR", "CIS-128", "CIS-3000", "CROWD36", "SITOR-B", "SITOR-A"};

std::string upper(std::string s) { for (char& c : s) c = (char)std::toupper((unsigned char)c); return s; }

void usage(FILE* f) {
    std::fprintf(f,
        "hfmodes " HFMODES_VERSION " - FoxSDR GPL helper: HF telegraphy decoders, audio in, JSON Lines out (GPL-3.0-or-later)\n"
        HFMODES_ATTRIBUTION "\n"
        "usage: hfmodes --foxsdr-helper 1 --format s16|f32 --rate <Hz> [--modes a,b,...] [--crowd36-baud N]\n"
        "       hfmodes --foxsdr-helper-info | --list-modes | --version | --help\n"
        "reads mono audio (little-endian) on stdin until end of file; --rate is 8000 to 192000 Hz.\n"
        "modes (default: all):");
    for (int i = 0; i < K_COUNT; i++) std::fprintf(f, " %s", kNames[i]);
    std::fprintf(f, "\nCROWD36 and the CIS modes decode or identify what the upstream projects do; see README.md.\n");
}

bool readExact(FILE* f, unsigned char* buf, size_t n, size_t& got) { got = std::fread(buf, 1, n, f); return got > 0; }

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::string format; double rate = 0; int proto = 0; std::string modesArg; int crowdBaud = 40;
    bool info = false, list = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> std::string { if (i + 1 >= argc) { std::fprintf(stderr, "hfmodes: %s needs a value\n", what); std::exit(2); } return argv[++i]; };
        if (a == "--foxsdr-helper") proto = std::atoi(next("--foxsdr-helper").c_str());
        else if (a == "--format") format = next("--format");
        else if (a == "--rate") rate = std::atof(next("--rate").c_str());
        else if (a == "--centre") (void)next("--centre");               // accepted and ignored: audio helpers have no centre frequency
        else if (a == "--modes") modesArg = next("--modes");
        else if (a == "--crowd36-baud") crowdBaud = std::atoi(next("--crowd36-baud").c_str());
        else if (a == "--foxsdr-helper-info") info = true;
        else if (a == "--list-modes") list = true;
        else if (a == "--version") { std::printf("hfmodes " HFMODES_VERSION "\n" HFMODES_ATTRIBUTION "\nGPL-3.0-or-later, no warranty\n"); return 0; }
        else if (a == "--help" || a == "-h") { usage(stdout); return 0; }
        else { std::fprintf(stderr, "hfmodes: unknown option %s\n", a.c_str()); usage(stderr); return 2; }
    }
    if (info) { std::printf("%s\n", kManifest); return 0; }
    if (list) { for (int i = 0; i < K_COUNT; i++) std::printf("%s\n", kNames[i]); return 0; }
    if (proto != 1) { std::fprintf(stderr, "hfmodes: --foxsdr-helper 1 is required (protocol version 1)\n"); return 2; }
    if (format != "s16" && format != "f32") { std::fprintf(stderr, "hfmodes: --format must be s16 or f32 (this helper takes mono audio), not '%s'\n", format.c_str()); return 2; }
    if (!(rate >= 8000 && rate <= 192000)) { std::fprintf(stderr, "hfmodes: --rate must be between 8000 and 192000 Hz\n"); return 2; }
    if (crowdBaud < 10 || crowdBaud > 100) { std::fprintf(stderr, "hfmodes: --crowd36-baud must be 10..100\n"); return 2; }

    bool sel[K_COUNT];
    for (int i = 0; i < K_COUNT; i++) sel[i] = modesArg.empty();
    if (!modesArg.empty()) {
        size_t p = 0;
        while (p <= modesArg.size()) {
            size_t q = modesArg.find(',', p); if (q == std::string::npos) q = modesArg.size();
            const std::string m = upper(modesArg.substr(p, q - p)); p = q + 1;
            if (m.empty()) continue;
            int found = -1;
            for (int i = 0; i < K_COUNT; i++) if (m == kNames[i]) found = i;
            if (m == "SITOR") { sel[K_SITORB] = sel[K_SITORA] = true; continue; }
            if (found < 0) { std::fprintf(stderr, "hfmodes: unknown mode '%s' (see --list-modes)\n", m.c_str()); return 2; }
            sel[found] = true;
        }
    }

    Out out; out.rate = rate;
    std::string hello = std::string("{\"helper\":\"hfmodes\",\"version\":\"" HFMODES_VERSION "\",\"protocol\":1,\"licence\":\"GPL-3.0-or-later\",\"source\":\"https://github.com/adswill/OnAir\",\"attribution\":\"" HFMODES_ATTRIBUTION "\"}\n");
    std::fwrite(hello.data(), 1, hello.size(), stdout); std::fflush(stdout);

    using namespace dect2::hfu;
    std::vector<std::unique_ptr<Channel>> ch;
    const struct { int k; FskKind f; } fskKinds[] = {{K_ARQE, FskKind::ArqE}, {K_ARQN, FskKind::ArqN}, {K_AUTOSPEC, FskKind::Autospec}, {K_SPREAD, FskKind::Spread}, {K_GTOR, FskKind::GTor}, {K_EFR, FskKind::Efr}, {K_PICCOLO, FskKind::Piccolo6}, {K_COQ8, FskKind::Coquelet8}, {K_COQ13, FskKind::Coquelet13}};
    for (auto& f : fskKinds) if (sel[f.k]) ch.emplace_back(new FskChannel(kNames[f.k], f.f));
    const struct { int k; int cis; } cisKinds[] = {{K_CIS3650, dect2::cishf::Bee}, {K_CIS14, dect2::cishf::Cis14}, {K_CIS12, dect2::cishf::Cis12}, {K_CIS36, dect2::cishf::Cis36}, {K_HDR, dect2::cishf::Hdr}, {K_C128, dect2::cishf::C128}, {K_C3000, dect2::cishf::C3000}};
    for (auto& c : cisKinds) if (sel[c.k]) ch.emplace_back(new CisChannel(kNames[c.k], c.cis));
    if (sel[K_CROWD36]) ch.emplace_back(new CrowdChannel(crowdBaud));
    if (sel[K_CIS3650R]) ch.emplace_back(new Cis3650Channel());
    if (sel[K_SITORB] || sel[K_SITORA]) ch.emplace_back(new SitorChannel(sel[K_SITORB], sel[K_SITORA]));

    bool need24 = false, need12 = false, need8 = false;
    for (auto& c : ch) { need24 |= c->wants24 || c->wants12; need12 |= c->wants12; need8 |= c->wants8; }
    hfm::Resampler rs24(rate, 24000.0), rs8(rate, 8000.0);
    hfm::Analytic analytic; hfm::Half half;

    const size_t kBlock = 1024;                       // input samples per processing step, whatever the size of the reads
    std::vector<float> blk; blk.reserve(kBlock);
    std::vector<float> r24, r8; std::vector<cplx> c24, c12;
    auto process = [&]() {
        r24.clear(); r8.clear(); c24.clear(); c12.clear();
        if (need24) { rs24.push(blk.data(), blk.size(), r24); analytic.push(r24.data(), r24.size(), c24); }
        if (need12) half.push(c24, c12);
        if (need8) rs8.push(blk.data(), blk.size(), r8);
        out.samples += blk.size();
        for (auto& c : ch) {
            if (c->wants24 && !c24.empty()) c->feed24(c24);
            if (c->wants12 && !c12.empty()) c->feed12(c12);
            if (c->wants8 && !r8.empty()) c->feed8(r8);
            c->poll(out);
        }
        blk.clear();
    };

    const size_t width = format == "s16" ? 2 : 4;
    std::vector<unsigned char> rb(65536);
    size_t carry = 0;
    for (;;) {
        size_t got = 0;
        if (!readExact(stdin, rb.data() + carry, rb.size() - carry, got)) break;
        got += carry;
        const size_t whole = got / width;
        for (size_t i = 0; i < whole; i++) {
            float v;
            if (width == 2) { const int16_t s = (int16_t)(rb[i * 2] | (rb[i * 2 + 1] << 8)); v = (float)s / 32768.0f; }
            else { uint32_t u = (uint32_t)rb[i * 4] | ((uint32_t)rb[i * 4 + 1] << 8) | ((uint32_t)rb[i * 4 + 2] << 16) | ((uint32_t)rb[i * 4 + 3] << 24); std::memcpy(&v, &u, 4); if (!std::isfinite(v)) v = 0; v = std::max(-4.0f, std::min(4.0f, v)); }
            blk.push_back(v);
            if (blk.size() == kBlock) process();
        }
        carry = got - whole * width;
        for (size_t i = 0; i < carry; i++) rb[i] = rb[whole * width + i];
    }
    if (!blk.empty()) process();
    for (auto& c : ch) c->finish(out);
    for (auto& c : ch) {
        if (c->good() || c->bad()) {
            std::string r = ",\"good\":" + std::to_string(c->good()) + ",\"bad\":" + std::to_string(c->bad());
            out.line("stats", c->name(), r);
        }
    }
    out.line("stats", "", ",\"samples\":" + std::to_string(out.samples) + ",\"seconds\":" + jnum((double)out.samples / rate, 3));
    return 0;
}
