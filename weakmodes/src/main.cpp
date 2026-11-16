// weakmodes: FoxSDR GPL helper. Receive-only decoder for the weak-signal modes FT2L, MSK144 and JT9, behind the
// FoxSDR helper pipe protocol version 1 (raw audio on stdin, JSON Lines on stdout).
//
// The decoders are C++ ports of WSJT-X (Joe Taylor K1JT and the WSJT Development Group) and of the wsjtx-ft2libre
// FT2L code. Licence: GPL-3.0-or-later. See LICENSE, UPSTREAM.md and README.md.
// This file (the program shell) is new code written for the helper.

#include "helper_manifest.h"
#include "wm_common.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

#define HELPER_NAME "weakmodes"
#define HELPER_VERSION "1.0.0"
#define HELPER_LICENCE "GPL-3.0-or-later"
#define HELPER_SOURCE "https://sourceforge.net/p/wsjt/wsjtx/ci/master/tree/"
#define HELPER_UPSTREAM "WSJT-X b4f9a43 (2025-02-04); wsjtx-ft2libre 356be92 (2026-02-26)"

static FILE *g_out = nullptr;

static void usage(FILE *f) {
    fprintf(f,
            "weakmodes - FoxSDR GPL helper: receive-only FT2L, MSK144 and JT9 decoder (GPL-3.0-or-later)\n"
            "usage: weakmodes --foxsdr-helper 1 --format s16|f32 --rate 12000 [options] < audio.raw\n"
            "       weakmodes --foxsdr-helper-info\n"
            "  --mode <list>    comma list of ft2,msk144,jt9 (default: all three)\n"
            "  --fmin <Hz> --fmax <Hz>  audio search range (default 200..4000)\n"
            "  --depth <1..3>   decoding depth (default 3)\n"
            "  --offset <s>     seconds from the first sample to the first period boundary (default 0)\n"
            "  --chunk <n>      samples per read, 64..65536 (default 4096)\n"
            "Input: mono audio at 12000 Hz, 16-bit little-endian (s16) or float (f32; full scale = +-1.0).\n"
            "Output: JSON Lines, a hello line then {\"t\":..,\"type\":\"text\",\"mode\":..,\"freq\":..,\"snr\":..,\"dt\":..,\"text\":..}.\n"
            "FT2L is a reverse-engineered format and may not match other FT2 software.\n");
}

static void json_escape(std::string &out, const std::string &s) {
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; }
            else out += (char)c;
        }
    }
}

static void out_line(const std::string &s) {
    if (fwrite(s.data(), 1, s.size(), g_out) != s.size() || fputc('\n', g_out) == EOF || fflush(g_out) != 0) {
        fprintf(stderr, "weakmodes: cannot write to standard output; stopping\n");
        exit(3);
    }
}

static size_t read_full(void *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        size_t r = fread((char *)buf + got, 1, n - got, stdin);
        if (r == 0) break;
        got += r;
    }
    return got;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    bool helper = false, info = false;
    std::string format;
    long rate = 0;
    std::string modes = "ft2,msk144,jt9";
    wm::Options opt;
    size_t chunk = 4096;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto need = [&](const char *what) -> const char * {
            if (i + 1 >= argc) { fprintf(stderr, "weakmodes: %s needs a value\n", what); exit(2); }
            return argv[++i];
        };
        if (a == "--foxsdr-helper") { const char *v = need("--foxsdr-helper"); if (strcmp(v, "1")) { fprintf(stderr, "weakmodes: unsupported protocol version %s (this helper speaks 1)\n", v); return 2; } helper = true; }
        else if (a == "--foxsdr-helper-info") info = true;
        else if (a == "--format") format = need("--format");
        else if (a == "--rate") rate = atol(need("--rate"));
        else if (a == "--mode") modes = need("--mode");
        else if (a == "--fmin") opt.fmin = atoi(need("--fmin"));
        else if (a == "--fmax") opt.fmax = atoi(need("--fmax"));
        else if (a == "--depth") opt.depth = atoi(need("--depth"));
        else if (a == "--offset") opt.offset = atof(need("--offset"));
        else if (a == "--chunk") chunk = (size_t)atol(need("--chunk"));
        else if (a == "--help" || a == "-h") { usage(stdout); return 0; }
        else { fprintf(stderr, "weakmodes: unknown option %s\n", a.c_str()); usage(stderr); return 2; }
    }
    if (info) {
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        fputs(HELPER_MANIFEST_JSON, stdout);
        return 0;
    }
    if (!helper) { fprintf(stderr, "weakmodes: --foxsdr-helper 1 is required\n"); usage(stderr); return 2; }
    if (format != "s16" && format != "f32") { fprintf(stderr, "weakmodes: --format must be s16 or f32 (got '%s'): this helper reads mono audio only\n", format.c_str()); return 2; }
    if (rate != wm::kRate) { fprintf(stderr, "weakmodes: --rate must be 12000 (got %ld)\n", rate); return 2; }
    if (chunk < 64 || chunk > 65536) { fprintf(stderr, "weakmodes: --chunk must be 64..65536\n"); return 2; }
    if (opt.fmin < 0 || opt.fmax > 6000 || opt.fmin >= opt.fmax) { fprintf(stderr, "weakmodes: bad --fmin/--fmax\n"); return 2; }
    if (opt.depth < 1 || opt.depth > 3) { fprintf(stderr, "weakmodes: --depth must be 1..3\n"); return 2; }
    bool want_ft2 = false, want_msk = false, want_jt9 = false;
    {
        std::string m = modes + ",";
        size_t p = 0, q;
        while ((q = m.find(',', p)) != std::string::npos) {
            std::string t = m.substr(p, q - p);
            p = q + 1;
            if (t == "ft2" || t == "ft2l") want_ft2 = true;
            else if (t == "msk144") want_msk = true;
            else if (t == "jt9") want_jt9 = true;
            else if (t == "all") want_ft2 = want_msk = want_jt9 = true;
            else { fprintf(stderr, "weakmodes: unknown mode '%s' (ft2, msk144, jt9)\n", t.c_str()); return 2; }
        }
    }

    // JSON goes to a duplicate of stdout; fd 1 itself is pointed at stderr so stray prints cannot corrupt the stream.
#ifdef _WIN32
    int fd = _dup(1);
    _setmode(fd, _O_BINARY); // JSON Lines end in LF alone, not CR LF
    _dup2(2, 1);
    g_out = _fdopen(fd, "wb");
#else
    int fd = dup(1);
    dup2(2, 1);
    g_out = fdopen(fd, "wb");
#endif
    if (!g_out) { fprintf(stderr, "weakmodes: cannot open standard output\n"); return 3; }

    std::string hello = "{\"helper\":\"" HELPER_NAME "\",\"version\":\"" HELPER_VERSION "\",\"protocol\":1,\"licence\":\"" HELPER_LICENCE
                        "\",\"source\":\"" HELPER_SOURCE "\",\"upstream\":\"" HELPER_UPSTREAM "\",\"format\":\"" + format +
                        "\",\"rate\":12000,\"modes\":[";
    bool first = true;
    auto add = [&](bool w, const char *n) { if (w) { if (!first) hello += ","; hello += std::string("\"") + n + "\""; first = false; } };
    add(want_ft2, "FT2L"); add(want_msk, "MSK144"); add(want_jt9, "JT9");
    hello += "],\"note\":\"FT2L is a reverse-engineered format and may not match other FT2 software\"}";
    out_line(hello);

    double sample_clock = 0; // seconds of audio read so far
    auto emit = [&](const wm::Decode &d) {
        char b[160];
        std::string s;
        snprintf(b, sizeof b, "{\"t\":%.3f,\"type\":\"text\",\"mode\":\"", sample_clock);
        s += b;
        s += d.mode;
        snprintf(b, sizeof b, "\",\"freq\":%.1f,\"snr\":%d,\"dt\":%.2f,\"tstart\":%.3f,\"text\":\"", d.freq, d.snr, d.dt, d.t_start);
        s += b;
        json_escape(s, d.text);
        s += "\"";
        if (!d.note.empty()) { s += ",\"note\":\""; json_escape(s, d.note); s += "\""; }
        s += "}";
        out_line(s);
    };
    std::vector<std::unique_ptr<wm::StreamDecoder>> decs;
    if (want_ft2) decs.emplace_back(wm::make_ft2_decoder(opt, emit));
    if (want_msk) decs.emplace_back(wm::make_msk144_decoder(opt, emit));
    if (want_jt9) decs.emplace_back(wm::make_jt9_decoder(opt, emit));

    std::vector<unsigned char> raw(chunk * (format == "s16" ? 2 : 4));
    std::vector<float> x(chunk);
    unsigned long long total = 0;
    for (;;) {
        size_t bytes_per = format == "s16" ? 2 : 4;
        size_t got = read_full(raw.data(), raw.size());
        size_t n = got / bytes_per;
        if (got % bytes_per) fprintf(stderr, "weakmodes: input ended in the middle of a sample; the last %zu byte(s) are ignored\n", got % bytes_per);
        if (format == "s16") {
            for (size_t i = 0; i < n; i++) {
                int16_t v = (int16_t)(raw[2 * i] | (raw[2 * i + 1] << 8));
                x[i] = (float)v;
            }
        } else {
            for (size_t i = 0; i < n; i++) {
                float v;
                memcpy(&v, &raw[4 * i], 4);
                if (!(v == v) || v > 1e6f || v < -1e6f) v = 0.f; // NaN or absurd -> silence
                if (v > 1.f) v = 1.f;
                if (v < -1.f) v = -1.f;
                x[i] = v * 32767.f; // float full scale +-1.0 -> 16-bit scale
            }
        }
        total += n;
        sample_clock = (double)total / wm::kRate;
        if (n) for (auto &d : decs) d->feed(x.data(), n);
        if (got < raw.size()) break;
    }
    for (auto &d : decs) d->finish();
    return 0;
}
