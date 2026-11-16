// weakmodes2: FoxSDR GPL helper. FST4, FST4W and Q65 receiver behind the FoxSDR helper pipe protocol (version 1).
//
// Audio on stdin (--format s16 or f32, mono, --rate >= 8000, 12000 recommended), one JSON line per decode on stdout.
// The decoders are OnAir's C++ re-implementations of the WSJT-X decoders (see UPSTREAM.md); this file is new code written for
// this helper: command line, slot clock, input loop, decode worker and the JSON output.
//
// Slot clock: the first sample is the start of a slot of every decoder unless --slot-offset (seconds already elapsed in the slot
// at sample 0) or --start-utc (UTC epoch seconds of sample 0; slots are then the UTC slots of each period) says otherwise. There is no
// wall clock anywhere: the output depends only on the input bytes and the options.
//
// Copyright (C) 2026 FoxSDR helper authors. Licence: GPL-3.0-or-later (see LICENSE). Based on OnAir by Kristijonas Adomaitis - https://on-air.dev
#include "hfdig_wsjt_int.h"
#include "wm2_manifest.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace dect2;

#define HELPER_NAME "weakmodes2"
#define HELPER_VERSION "0.1.0"
#define HELPER_LICENCE "GPL-3.0-or-later"
#define HELPER_SOURCE "https://github.com/adswill/OnAir"
#define HELPER_UPSTREAM "OnAir e118396b25d5 (hfdig FST4/FST4W/Q65 after WSJT-X, sourceforge.net/p/wsjt/wsjtx b4f9a431bcf6)"
#define ATTRIBUTION "Based on OnAir by Kristijonas Adomaitis - https://on-air.dev"

namespace {

FILE* g_json = nullptr;   // the protocol stream (a duplicate of fd 1; fd 1 itself points at stderr so nothing else can corrupt it)

enum Kind { kFst4 = 0, kFst4w = 1, kQ65 = 2 };

struct Dec {
    Kind kind;
    double period;     // s
    int sub;           // Q65: 0 A .. 4 E
    std::string name;  // "FST4" ...
    std::string label; // "60", "60A", "120"
    double offset;     // seconds already elapsed in the current slot at sample 0
    std::vector<float> buf;
    long long slotIndex = 0;   // number of the slot being filled (0 = the slot containing sample 0)
    long long periodSamples = 0;
    double txSeconds = 0;      // length of one transmission
};

struct Job {
    int dec;
    long long slot;
    std::vector<float> x;
    double endT;               // protocol time of the end of the data in x, s
    bool partial;
};

struct Options {
    std::string format = "s16";
    double rate = 12000;
    std::vector<Dec> decs;
    double fLo = 200, fHi = 3000, dtMin = -2.5, dtMax = 2.5;
    bool haveUtc = false;
    double startUtc = 0, slotOffset = 0;
    size_t chunk = 4096;   // samples per read (fixed, so the output never depends on the pipe)
    bool info = false, version = false, help = false;
    bool protocolSeen = false;
};

void jsonStr(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
            else out += (char)c;
        }
    }
    out += '"';
}

void emit(const std::string& line) {
    std::fwrite(line.data(), 1, line.size(), g_json);
    std::fputc('\n', g_json);
    std::fflush(g_json);
}

std::string fmt(const char* f, double v) { char b[64]; std::snprintf(b, sizeof b, f, v); return b; }

void emitStatus(double t, const std::string& text) {
    std::string l = "{\"t\":" + fmt("%.3f", t) + ",\"type\":\"status\",\"text\":";
    jsonStr(l, text);
    l += "}";
    emit(l);
}

double nspsSeconds(Kind k, double period) {
    // transmission length: FST4 160 symbols, Q65 85 symbols
    static const double per[7] = {15, 30, 60, 120, 300, 900, 1800};
    static const int fst[7] = {720, 1680, 3888, 8200, 21504, 66560, 134400};
    static const int q65[5] = {1800, 3600, 7200, 16000, 41472};
    for (int i = 0; i < 7; i++) {
        if (std::fabs(per[i] - period) < 0.5) {
            if (k == kQ65) return i < 5 ? 85.0 * q65[i] / 12000.0 : 0;
            return 160.0 * fst[i] / 12000.0;
        }
    }
    return 0;
}

bool validPeriod(Kind k, double p) {
    static const double fst[7] = {15, 30, 60, 120, 300, 900, 1800};
    const int n = k == kQ65 ? 5 : 7;
    for (int i = 0; i < n; i++) if (std::fabs(fst[i] - p) < 0.5) return true;
    return false;
}

// "fst4:60,fst4w:120,q65:60A"
bool parseDecoders(const std::string& spec, std::vector<Dec>& out, std::string& err) {
    size_t pos = 0;
    while (pos <= spec.size()) {
        size_t e = spec.find(',', pos);
        if (e == std::string::npos) e = spec.size();
        std::string item = spec.substr(pos, e - pos);
        pos = e + 1;
        if (item.empty()) { if (e >= spec.size()) break; continue; }
        const size_t c = item.find(':');
        if (c == std::string::npos) { err = "decoder '" + item + "': expected mode:period (fst4:60, fst4w:120, q65:60A)"; return false; }
        std::string m = item.substr(0, c), p = item.substr(c + 1);
        for (char& ch : m) ch = (char)std::tolower((unsigned char)ch);
        Dec d;
        d.sub = 0;
        if (m == "fst4") d.kind = kFst4, d.name = "FST4";
        else if (m == "fst4w") d.kind = kFst4w, d.name = "FST4W";
        else if (m == "q65") d.kind = kQ65, d.name = "Q65";
        else { err = "unknown mode '" + m + "' (fst4, fst4w, q65)"; return false; }
        if (d.kind == kQ65) {
            if (p.empty() || !std::isalpha((unsigned char)p.back())) { err = "q65 needs a sub-mode letter A..E, e.g. q65:60A"; return false; }
            const char sc = (char)std::toupper((unsigned char)p.back());
            if (sc < 'A' || sc > 'E') { err = "q65 sub-mode must be A..E"; return false; }
            d.sub = sc - 'A';
            p.pop_back();
        }
        char* endp = nullptr;
        d.period = std::strtod(p.c_str(), &endp);
        if (p.empty() || *endp || !validPeriod(d.kind, d.period)) {
            err = "period '" + p + "' not valid for " + d.name + (d.kind == kQ65 ? " (15, 30, 60, 120, 300)" : " (15, 30, 60, 120, 300, 900, 1800)");
            return false;
        }
        char lab[32];
        std::snprintf(lab, sizeof lab, "%d", (int)std::lround(d.period));
        d.label = lab;
        if (d.kind == kQ65) d.label += (char)('A' + d.sub);
        d.txSeconds = nspsSeconds(d.kind, d.period);
        out.push_back(std::move(d));
        if (e >= spec.size()) break;
    }
    if (out.empty()) { err = "no decoders given"; return false; }
    return true;
}

int usage(FILE* f) {
    std::fprintf(f,
        "weakmodes2 " HELPER_VERSION " - FST4, FST4W and Q65 receiver (FoxSDR GPL helper, pipe protocol 1)\n"
        "usage: weakmodes2-helper --foxsdr-helper 1 --format s16|f32 --rate <Hz> [options] < audio.raw\n"
        "  --decoders LIST     comma list of mode:period, default fst4:60,fst4w:120,q65:60A\n"
        "                      fst4 / fst4w periods 15 30 60 120 300 900 1800; q65 periods 15 30 60 120 300 with sub-mode A..E\n"
        "  --fmin Hz --fmax Hz audio band searched (default 200 .. 3000)\n"
        "  --dt-min s --dt-max s  start offset window against the nominal start (default -2.5 .. 2.5)\n"
        "  --slot-offset s     seconds already elapsed in the current slot at the first sample (default 0)\n"
        "  --start-utc epoch   UTC time of the first sample (slots then follow the UTC clock; the JSON gets \"utc\")\n"
        "  --foxsdr-helper-info  print the manifest and exit\n"
        "  --version             print the version and licence and exit\n"
        "Input is mono audio only (s16 or f32) read from standard input until end of file; 12000 Hz is recommended.\n"
        "Output is JSON Lines on standard output (hello line first). Receive only; no network, no transmit.\n"
        "%s\n", ATTRIBUTION);
    return f == stdout ? 0 : 2;
}

bool numArg(const char* s, double& v) {
    char* e = nullptr;
    v = std::strtod(s, &e);
    return s[0] && !*e && std::isfinite(v);
}

// ------------------------------------------------------------------------------------------------ the decode worker

struct Worker {
    std::mutex m;
    std::condition_variable cvJob, cvSpace;
    std::deque<Job> q;
    bool closed = false;
    size_t limit = 2;
    const Options* opt = nullptr;
    long long decodes = 0, slots = 0;
    std::thread th;

    void push(Job&& j) {
        std::unique_lock<std::mutex> lk(m);
        cvSpace.wait(lk, [&] { return q.size() < limit; });
        q.push_back(std::move(j));
        cvJob.notify_one();
    }
    void close() {
        { std::lock_guard<std::mutex> lk(m); closed = true; }
        cvJob.notify_one();
    }
    void run() {
        for (;;) {
            Job j;
            {
                std::unique_lock<std::mutex> lk(m);
                cvJob.wait(lk, [&] { return closed || !q.empty(); });
                if (q.empty()) return;
                j = std::move(q.front());
                q.pop_front();
                cvSpace.notify_one();
            }
            decode(j);
        }
    }

    void decode(const Job& j) {
        const Dec& d = opt->decs[(size_t)j.dec];
        wsjt::Slot s;
        s.x = j.x.data();
        s.n = j.x.size();
        s.x0 = 0;
        s.dtMin = opt->dtMin;
        s.dtMax = opt->dtMax;
        s.sub = d.sub;
        s.period = d.period;
        s.fLo = opt->fLo;
        s.fHi = opt->fHi;
        s.rate = opt->rate;
        s.slotUtc = opt->haveUtc ? slotStartUtc(d, j.slot) : 0;
        std::vector<FtxDecode> res;
        if (d.kind == kQ65) res = wsjt::decodeQ65(s);
        else res = wsjt::decodeFst4(s, d.kind == kFst4w);
        slots++;
        // a stable order: strongest frequency order is not guaranteed by the decoder, so sort by frequency then text
        std::sort(res.begin(), res.end(), [](const FtxDecode& a, const FtxDecode& b) {
            if (a.hz != b.hz) return a.hz < b.hz;
            return a.msg < b.msg;
        });
        for (const FtxDecode& r : res) {
            std::string l = "{\"t\":" + fmt("%.3f", j.endT) + ",\"type\":\"decode\",\"mode\":";
            jsonStr(l, d.name);
            l += ",\"sub\":";
            jsonStr(l, d.label);
            l += ",\"period\":" + fmt("%.0f", d.period);
            l += ",\"slot\":" + std::to_string(j.slot);
            if (opt->haveUtc) l += ",\"utc\":" + fmt("%.0f", s.slotUtc);
            l += ",\"freq\":" + fmt("%.1f", r.hz);
            l += ",\"snr\":" + std::to_string((int)std::lround(r.snrDb));
            l += ",\"dt\":" + fmt("%.1f", r.dt);
            if (d.kind == kFst4w) l += ",\"dbm\":" + std::to_string(r.dbm);
            if (!r.call.empty()) { l += ",\"call\":"; jsonStr(l, r.call); }
            if (!r.grid.empty()) { l += ",\"grid\":"; jsonStr(l, r.grid); }
            if (j.partial) l += ",\"partial\":true";
            l += ",\"text\":";
            jsonStr(l, r.msg);
            l += "}";
            emit(l);
            decodes++;
        }
    }

    double slotStartUtc(const Dec& d, long long slot) const {
        // slot 0 contains sample 0, which is at startUtc; the slot began `offset` seconds earlier
        return opt->startUtc - d.offset + (double)slot * d.period;
    }
};

// ------------------------------------------------------------------------------------------------ input

bool readAll(FILE* f, void* p, size_t n, size_t& got) {
    got = std::fread(p, 1, n, f);
    return got == n;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(0, _O_BINARY);
    _setmode(1, _O_BINARY); /* the manifest printed by --foxsdr-helper-info and any --help text must not get CRLF from text mode */
#endif
    Options o;
    std::string decSpec = "fst4:60,fst4w:120,q65:60A";
    bool rateSet = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "weakmodes2: %s needs a value\n", name); std::exit(2); }
            return argv[++i];
        };
        double v;
        if (a == "--foxsdr-helper") {
            const char* s = need("--foxsdr-helper");
            if (std::strcmp(s, "1") != 0) { std::fprintf(stderr, "weakmodes2: unsupported protocol version '%s' (this helper speaks 1)\n", s); return 2; }
            o.protocolSeen = true;
        } else if (a == "--format") o.format = need("--format");
        else if (a == "--rate") { if (!numArg(need("--rate"), o.rate)) { std::fprintf(stderr, "weakmodes2: bad --rate\n"); return 2; } rateSet = true; }
        else if (a == "--decoders") decSpec = need("--decoders");
        else if (a == "--fmin") { if (!numArg(need("--fmin"), o.fLo)) { std::fprintf(stderr, "weakmodes2: bad --fmin\n"); return 2; } }
        else if (a == "--fmax") { if (!numArg(need("--fmax"), o.fHi)) { std::fprintf(stderr, "weakmodes2: bad --fmax\n"); return 2; } }
        else if (a == "--dt-min") { if (!numArg(need("--dt-min"), o.dtMin)) { std::fprintf(stderr, "weakmodes2: bad --dt-min\n"); return 2; } }
        else if (a == "--dt-max") { if (!numArg(need("--dt-max"), o.dtMax)) { std::fprintf(stderr, "weakmodes2: bad --dt-max\n"); return 2; } }
        else if (a == "--slot-offset") { if (!numArg(need("--slot-offset"), o.slotOffset) || o.slotOffset < 0) { std::fprintf(stderr, "weakmodes2: bad --slot-offset\n"); return 2; } }
        else if (a == "--start-utc") { if (!numArg(need("--start-utc"), v) || v < 0) { std::fprintf(stderr, "weakmodes2: bad --start-utc\n"); return 2; } o.startUtc = v; o.haveUtc = true; }
        else if (a == "--foxsdr-helper-info") o.info = true;
        else if (a == "--version") o.version = true;
        else if (a == "--help" || a == "-h") o.help = true;
        else { std::fprintf(stderr, "weakmodes2: unknown option '%s' (try --help)\n", a.c_str()); return 2; }
    }
    if (o.info) { std::fputs(WM2_MANIFEST_JSON, stdout); return 0; }
    if (o.version) {
        std::printf("weakmodes2 " HELPER_VERSION " (" HELPER_LICENCE ")\nupstream: " HELPER_UPSTREAM "\n%s\n", ATTRIBUTION);
        return 0;
    }
    if (o.help) return usage(stdout);
    if (!o.protocolSeen) { std::fprintf(stderr, "weakmodes2: run with --foxsdr-helper 1 --format s16|f32 --rate <Hz> (see --help)\n"); return 2; }
    if (o.format != "s16" && o.format != "f32") {
        std::fprintf(stderr, "weakmodes2: --format '%s' not supported: this helper takes mono audio, s16 or f32\n", o.format.c_str());
        return 2;
    }
    if (!rateSet) { std::fprintf(stderr, "weakmodes2: --rate is required\n"); return 2; }
    if (!(o.rate >= 8000 && o.rate <= 192000) || o.rate != std::floor(o.rate)) {
        std::fprintf(stderr, "weakmodes2: --rate %g not supported (a whole number of Hz from 8000 to 192000; 12000 recommended)\n", o.rate);
        return 2;
    }
    if (!(o.fLo >= 0 && o.fHi > o.fLo && o.fHi <= 0.46 * o.rate) || !(o.dtMax > o.dtMin)) {
        std::fprintf(stderr, "weakmodes2: bad --fmin/--fmax/--dt-min/--dt-max (need 0 <= fmin < fmax <= 0.46 x rate, dt-min < dt-max)\n");
        return 2;
    }
    {
        std::string err;
        if (!parseDecoders(decSpec, o.decs, err)) { std::fprintf(stderr, "weakmodes2: %s\n", err.c_str()); return 2; }
    }
    for (Dec& d : o.decs) {
        d.periodSamples = (long long)std::llround(d.period * o.rate);
        d.offset = o.haveUtc ? std::fmod(o.startUtc, d.period) : std::fmod(o.slotOffset, d.period);
        d.buf.reserve((size_t)d.periodSamples);
        // zero fill for the part of the first slot before sample 0
        d.buf.assign((size_t)std::llround(d.offset * o.rate), 0.f);
    }

    // The protocol stream is a duplicate of fd 1; fd 1 is pointed at stderr so stray prints (debug output of the decoders) cannot corrupt it.
#ifdef _WIN32
    const int jfd = _dup(1);
    _setmode(jfd, _O_BINARY);   // no CRLF: JSON Lines are LF-terminated
    _dup2(2, 1);
    g_json = _fdopen(jfd, "wb");
#else
    const int jfd = dup(1);
    dup2(2, 1);
    g_json = fdopen(jfd, "wb");
#endif
    if (!g_json) { std::fprintf(stderr, "weakmodes2: cannot open the output stream\n"); return 1; }

    {
        std::string h = "{\"helper\":\"" HELPER_NAME "\",\"version\":\"" HELPER_VERSION "\",\"protocol\":1,\"licence\":\"" HELPER_LICENCE
                        "\",\"source\":\"" HELPER_SOURCE "\",\"upstream\":\"" HELPER_UPSTREAM "\",\"attribution\":\"" ATTRIBUTION "\"";
        h += ",\"format\":\"" + o.format + "\",\"rate\":" + fmt("%.0f", o.rate) + ",\"decoders\":[";
        for (size_t i = 0; i < o.decs.size(); i++) {
            if (i) h += ",";
            h += "\"" + o.decs[i].name + ":" + o.decs[i].label + "\"";
        }
        h += "]}";
        emit(h);
    }

    Worker w;
    w.opt = &o;
    w.th = std::thread([&] { w.run(); });

    const size_t bytesPer = o.format == "s16" ? 2 : 4;
    std::vector<unsigned char> raw(o.chunk * bytesPer);
    std::vector<float> fl(o.chunk);
    long long total = 0;   // samples read
    size_t carry = 0;      // bytes of a partial sample at the end of the last read (dropped at end of file)
    for (;;) {
        size_t got = 0;
        readAll(stdin, raw.data(), raw.size(), got);
        if (got == 0) break;
        const size_t n = got / bytesPer;
        carry = got % bytesPer;
        if (o.format == "s16") {
            for (size_t i = 0; i < n; i++) {
                const int16_t v = (int16_t)(raw[2 * i] | (raw[2 * i + 1] << 8));
                fl[i] = (float)v / 32768.0f;
            }
        } else {
            for (size_t i = 0; i < n; i++) {
                uint32_t u = (uint32_t)raw[4 * i] | ((uint32_t)raw[4 * i + 1] << 8) | ((uint32_t)raw[4 * i + 2] << 16) | ((uint32_t)raw[4 * i + 3] << 24);
                float v;
                std::memcpy(&v, &u, 4);
                fl[i] = std::isfinite(v) ? v : 0.f;   // garbage input must not poison the FFTs
            }
        }
        for (size_t i = 0; i < n; i++) {
            for (size_t di = 0; di < o.decs.size(); di++) {
                Dec& d = o.decs[di];
                d.buf.push_back(fl[i]);
                if ((long long)d.buf.size() == d.periodSamples) {
                    Job j;
                    j.dec = (int)di;
                    j.slot = d.slotIndex;
                    j.x.swap(d.buf);
                    j.endT = (double)(d.slotIndex + 1) * d.period - d.offset;
                    j.partial = false;
                    d.buf.clear();
                    d.buf.reserve((size_t)d.periodSamples);
                    d.slotIndex++;
                    w.push(std::move(j));
                }
            }
        }
        total += (long long)n;
        if (got < raw.size()) break;
    }
    (void)carry;
    // End of file: a slot that is part full is decoded when it holds the whole transmission (zero padded to the full period).
    for (size_t di = 0; di < o.decs.size(); di++) {
        Dec& d = o.decs[di];
        const double have = (double)d.buf.size() / o.rate;
        const double t0 = d.period < 20 ? 0.5 : 1.0;
        const bool real = (long long)d.buf.size() > (long long)std::llround(d.offset * o.rate);   // something beyond the zero fill
        if (!real) continue;
        if (have >= t0 + 0.9 * d.txSeconds) {
            Job j;
            j.dec = (int)di;
            j.slot = d.slotIndex;
            j.endT = (double)total / o.rate;
            j.partial = true;
            j.x.swap(d.buf);
            j.x.resize((size_t)d.periodSamples, 0.f);
            w.push(std::move(j));
        } else if (have >= t0) {   // less than the quiet lead-in of a transmission is just the end of a recording, not worth a line
            char b[160];
            std::snprintf(b, sizeof b, "%s %s: input ended %.1f s into a slot, too short for a transmission; not decoded", d.name.c_str(), d.label.c_str(), have);
            emitStatus((double)total / o.rate, b);
        }
    }
    w.close();
    w.th.join();
    {
        std::string l = "{\"t\":" + fmt("%.3f", (double)total / o.rate) + ",\"type\":\"stats\",\"samples\":" + std::to_string(total) +
                        ",\"slots_decoded\":" + std::to_string(w.slots) + ",\"decodes\":" + std::to_string(w.decodes) + "}";
        emit(l);
    }
    std::fclose(g_json);
    return 0;
}
