/*
 * helper_main.cpp - the FoxSDR "sondes" GPL helper: radiosonde decoders from rs1729/RS behind the FoxSDR helper
 * pipe protocol (version 1; see docs/PIPE-PROTOCOL.md of the helpers repository).
 *
 * Licence: GNU GPL v3 (see ../LICENSE). Decoder code: Copyright (C) zilog80 (rs1729/RS), see UPSTREAM.md.
 *
 *   sondes --foxsdr-helper 1 --format s16|f32 --rate <Hz> [options] < fm_audio.raw
 *
 * Reads FM-DEMODULATED AUDIO (mono) on stdin until end of file; prints JSON Lines on stdout. Each decoder runs on
 * its own thread over the same audio and sees nothing of the others. After every input chunk the main thread waits
 * until all decoders have used it up, so the output order does not depend on thread timing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#include "sondes_stream.h"
#include "helper_manifest.h"

#define HELPER_VERSION "1.0.0"

extern "C++" {
int lms6_main(int, char **);
int mk2a_main(int, char **);
int mrz_main(int, char **);
int mts01_main(int, char **);
int cf06_main(int, char **);
int rs92_main(int, char **);
}

struct Decoder {
    const char *name;        /* --types name */
    const char *describes;
    int (*fn)(int, char **);
    int min_rate;            /* below this the decoder is not started */
};

static const Decoder kDecoders[] = {
    {"lms6",  "Lockheed Martin Sippican LMS6-400 (403 MHz, 4800 baud) and LMS-X",     lms6_main,  19200},
    {"mk2a",  "Lockheed Martin Sippican LMS6-1680 / Mk IIa (1680 MHz, 9600 baud)",    mk2a_main,  38400},
    {"mrz",   "Meteo-Radiy MRZ-N1 / MP3-H1 (400 MHz)",                                  mrz_main,   19200},
    {"mts01", "Meteo-Radiy MTS01 (400 MHz)",                                            mts01_main, 19200},
    {"cf06",  "Changfeng CF-06-AH, HT03G / GTH3 (400 MHz)",                            cf06_main,  19200},
    {"rs92",  "Vaisala RS92-SGP/AGP/NGP (needs a GPS ephemeris or almanac file)",       rs92_main,  19200},
};
static const int kNDecoders = (int)(sizeof kDecoders / sizeof kDecoders[0]);

struct Worker {
    const Decoder *dec = nullptr;
    sondes_stream st;
    std::vector<std::string> args;
    sondes_hub *hub = nullptr;
#ifdef _WIN32
    HANDLE th = nullptr;
#else
    pthread_t th;
    bool started = false;
#endif
};

static FILE *g_out = nullptr;   /* the protocol channel: a private copy of the original stdout */

static void emit(const std::string &s) {
    fwrite(s.data(), 1, s.size(), g_out);
    fputc('\n', g_out);
    fflush(g_out);
}

static std::string jstr(const std::string &v) {
    std::string o = "\"";
    for (size_t i = 0; i < v.size(); i++) {
        unsigned char c = (unsigned char)v[i];
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

static void run_worker(Worker *w) {
    sondes_tls = &w->st;
    std::vector<char *> argv;
    for (size_t i = 0; i < w->args.size(); i++) argv.push_back(const_cast<char *>(w->args[i].c_str()));
    argv.push_back(nullptr);
    int rc = -1;
    try {
        rc = w->dec->fn((int)w->args.size(), argv.data());
    } catch (...) {
        rc = -2;
    }
    std::lock_guard<std::mutex> lk(w->hub->m);
    w->st.exit_code = rc;
    w->st.finished = true;
    w->hub->cv.notify_all();
}

#ifdef _WIN32
static DWORD WINAPI thread_main(LPVOID p) { run_worker(static_cast<Worker *>(p)); return 0; }
static bool start_thread(Worker *w) {
    w->th = CreateThread(nullptr, 64u << 20, thread_main, w, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    return w->th != nullptr;
}
static void join_thread(Worker *w) { if (w->th) { WaitForSingleObject(w->th, INFINITE); CloseHandle(w->th); w->th = nullptr; } }
#else
static void *thread_main(void *p) { run_worker(static_cast<Worker *>(p)); return nullptr; }
static bool start_thread(Worker *w) {
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, 64u << 20);
    w->started = pthread_create(&w->th, &a, thread_main, w) == 0;
    pthread_attr_destroy(&a);
    return w->started;
}
static void join_thread(Worker *w) { if (w->started) { pthread_join(w->th, nullptr); w->started = false; } }
#endif

static void usage(FILE *f) {
    fprintf(f,
        "usage: sondes --foxsdr-helper 1 --format s16|f32 --rate <Hz> [options] < fm_audio\n"
        "       sondes --foxsdr-helper-info\n"
        "  Reads FM-demodulated mono audio on stdin (raw, no header) until end of file and prints JSON Lines.\n"
        "  --rate <Hz>            sample rate of the audio, 38400 .. 192000 (48000 recommended)\n"
        "  --types <list>         comma list of: lms6,mk2a,mrz,mts01,cf06,rs92 (default: all but rs92 unless an\n"
        "                         --rs92-ephemeris/--rs92-almanac file is given)\n"
        "  --rs92-ephemeris <f>   RINEX GPS navigation file (e.g. brdc*.n), needed for RS92 positions\n"
        "  --rs92-almanac <f>     SEM almanac file, alternative to the ephemeris (less accurate)\n"
        "  --chunk <samples>      input chunk (and ordering granularity), default rate/10\n"
        "  --foxsdr-helper-info   print the manifest and exit\n");
}

int main(int argc, char **argv) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY); /* the manifest printed by --foxsdr-helper-info and any --help text must not get CRLF from text mode */
#endif
    int proto = 0, rate = 0;
    long chunk = 0;
    std::string format, types, ephem, almanac;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&](const char *what) -> const char * {
            if (i + 1 >= argc) { fprintf(stderr, "sondes: %s needs a value\n", what); exit(2); }
            return argv[++i];
        };
        if (a == "--foxsdr-helper-info") { fputs(HELPER_MANIFEST_JSON, stdout); fputc('\n', stdout); return 0; }
        else if (a == "--foxsdr-helper") proto = atoi(val("--foxsdr-helper"));
        else if (a == "--format") format = val("--format");
        else if (a == "--rate") rate = atoi(val("--rate"));
        else if (a == "--types") types = val("--types");
        else if (a == "--rs92-ephemeris") ephem = val("--rs92-ephemeris");
        else if (a == "--rs92-almanac") almanac = val("--rs92-almanac");
        else if (a == "--chunk") chunk = atol(val("--chunk"));
        else if (a == "-h" || a == "--help") { usage(stdout); return 0; }
        else { fprintf(stderr, "sondes: unknown option %s\n", a.c_str()); usage(stderr); return 2; }
    }
    if (proto != 1) { fprintf(stderr, "sondes: --foxsdr-helper 1 is required (protocol version 1)\n"); usage(stderr); return 2; }
    if (format != "s16" && format != "f32") {
        fprintf(stderr, "sondes: unsupported --format '%s': this helper takes FM-demodulated audio, s16 or f32\n", format.c_str());
        return 2;
    }
    if (rate < 38400 || rate > 192000) {
        fprintf(stderr, "sondes: --rate %d is outside 38400..192000 Hz\n", rate);
        return 2;
    }
    if (chunk <= 0) chunk = rate / 10;
    if (chunk < 256) chunk = 256;
    if (chunk > (1 << 22)) chunk = 1 << 22;

    std::vector<const Decoder *> sel;
    if (types.empty() || types == "all") {
        for (int i = 0; i < kNDecoders; i++)
            if (strcmp(kDecoders[i].name, "rs92") != 0 || !ephem.empty() || !almanac.empty()) sel.push_back(&kDecoders[i]);
    } else {
        size_t p = 0;
        while (p <= types.size()) {
            size_t q = types.find(',', p);
            if (q == std::string::npos) q = types.size();
            std::string n = types.substr(p, q - p);
            p = q + 1;
            if (n.empty()) continue;
            const Decoder *d = nullptr;
            for (int i = 0; i < kNDecoders; i++) if (n == kDecoders[i].name) d = &kDecoders[i];
            if (!d) { fprintf(stderr, "sondes: unknown decoder '%s' in --types\n", n.c_str()); return 2; }
            if (std::find(sel.begin(), sel.end(), d) == sel.end()) sel.push_back(d);
        }
    }
    {
        const std::string *files[2] = {&ephem, &almanac};
        const char *names[2] = {"--rs92-ephemeris", "--rs92-almanac"};
        for (int k = 0; k < 2; k++) {
            if (files[k]->empty()) continue;
            FILE *t = fopen(files[k]->c_str(), "rb");
            if (!t) { fprintf(stderr, "sondes: cannot open the %s file '%s'\n", names[k], files[k]->c_str()); return 2; }
            fclose(t);
        }
    }
    bool want_rs92 = false;
    for (size_t i = 0; i < sel.size(); i++) if (strcmp(sel[i]->name, "rs92") == 0) want_rs92 = true;
    if (want_rs92 && ephem.empty() && almanac.empty())
        fprintf(stderr, "sondes: note: rs92 gives no positions without --rs92-ephemeris or --rs92-almanac\n");

    /* the protocol channel is a private copy of stdout; fd 1 itself is pointed at stderr so that no stray write
       from decoder code can corrupt the JSON lines */
#ifdef _WIN32
    int outfd = _dup(1);
    _dup2(2, 1);
    _setmode(outfd, _O_BINARY);
    g_out = _fdopen(outfd, "wb");
#else
    int outfd = dup(1);
    dup2(2, 1);
    g_out = fdopen(outfd, "wb");
#endif
    if (!g_out) { fprintf(stderr, "sondes: cannot open the output channel\n"); return 1; }

    {
        std::string h = "{\"helper\":\"sondes\",\"version\":\"" HELPER_VERSION "\",\"protocol\":1,"
                        "\"licence\":\"GPL-3.0-only\",\"source\":\"https://github.com/rs1729/RS\","
                        "\"format\":" + jstr(format) + ",\"rate\":" + std::to_string(rate) + ",\"decoders\":[";
        for (size_t i = 0; i < sel.size(); i++) { if (i) h += ","; h += jstr(sel[i]->name); }
        h += "]}";
        emit(h);
    }

    sondes_hub hub;
    std::vector<std::unique_ptr<Worker> > workers;
    for (size_t i = 0; i < sel.size(); i++) {
        if (rate < sel[i]->min_rate) {
            emit("{\"t\":0.000,\"type\":\"status\",\"text\":" + jstr(std::string(sel[i]->name) + " not started: rate too low") + "}");
            continue;
        }
        std::unique_ptr<Worker> w(new Worker());
        w->dec = sel[i];
        w->hub = &hub;
        w->st.hub = &hub;
        w->st.index = (int)workers.size();
        w->st.name = sel[i]->name;
        w->st.rate = rate;
        std::string n = sel[i]->name;
        w->args.push_back(n);
        if (n == "lms6") { w->args.push_back("--json"); }
        else if (n == "mk2a") { w->args.push_back("--json"); w->args.push_back("--crc"); w->args.push_back("--lpFM"); }
        else if (n == "mrz") { w->args.push_back("--json"); w->args.push_back("--ptu"); w->args.push_back("--auto"); }
        else if (n == "mts01") { w->args.push_back("--json"); }
        else if (n == "cf06") { w->args.push_back("--json"); w->args.push_back("--auto"); w->args.push_back("--ecc2"); }
        else if (n == "rs92") {
            w->args.push_back("--json"); w->args.push_back("--crc"); w->args.push_back("--ecc");
            if (!ephem.empty()) { w->args.push_back("-e"); w->args.push_back(ephem); }
            else if (!almanac.empty()) { w->args.push_back("-a"); w->args.push_back(almanac); }
        }
        workers.push_back(std::move(w));
    }
    for (size_t i = 0; i < workers.size(); i++) {
        if (!start_thread(workers[i].get())) {
            std::lock_guard<std::mutex> lk(hub.m);
            workers[i]->st.finished = true;
            emit("{\"t\":0.000,\"type\":\"status\",\"text\":\"could not start a decoder thread\"}");
        }
    }

    auto flush_events = [&]() {
        std::vector<std::pair<std::pair<double, int>, std::string> > ev;
        {
            std::lock_guard<std::mutex> lk(hub.m);
            for (size_t i = 0; i < workers.size(); i++) {
                for (size_t k = 0; k < workers[i]->st.out.size(); k++)
                    ev.push_back(std::make_pair(std::make_pair(workers[i]->st.out[k].t, workers[i]->st.index),
                                                workers[i]->st.out[k].line));
                workers[i]->st.out.clear();
            }
        }
        std::stable_sort(ev.begin(), ev.end(),
                         [](const std::pair<std::pair<double, int>, std::string> &a,
                            const std::pair<std::pair<double, int>, std::string> &b) { return a.first < b.first; });
        for (size_t i = 0; i < ev.size(); i++) emit(ev[i].second);
    };

    const bool f32 = (format == "f32");
    const size_t in_bytes = (size_t)chunk * (f32 ? 4 : 2);
    std::vector<uint8_t> raw(in_bytes);
    uint64_t total = 0;
    for (;;) {
        size_t got = fread(raw.data(), 1, in_bytes, stdin);
        size_t n = got / (f32 ? 4 : 2);
        if (n > 0) {
            std::shared_ptr<std::vector<uint8_t> > c(new std::vector<uint8_t>(n * 2));
            if (f32) {
                for (size_t i = 0; i < n; i++) {
                    float f;
                    memcpy(&f, raw.data() + 4 * i, 4);
                    float v = f * 32767.0f;
                    if (!(v == v)) v = 0.0f;
                    if (v > 32767.0f) v = 32767.0f;
                    if (v < -32768.0f) v = -32768.0f;
                    int16_t s = (int16_t)(v < 0 ? v - 0.5f : v + 0.5f);
                    memcpy(c->data() + 2 * i, &s, 2);
                }
            } else {
                memcpy(c->data(), raw.data(), n * 2);
            }
            total += n;
            sondes_chunk sc = c;
            std::unique_lock<std::mutex> lk(hub.m);
            for (size_t i = 0; i < workers.size(); i++) if (!workers[i]->st.finished) workers[i]->st.q.push_back(sc);
            hub.cv.notify_all();
            /* barrier: every decoder has used the chunk up and is waiting for the next, or has ended */
            hub.cv.wait(lk, [&]() {
                for (size_t i = 0; i < workers.size(); i++) {
                    const sondes_stream &s = workers[i]->st;
                    if (!s.finished && !(s.waiting && s.q.empty())) return false;
                }
                return true;
            });
            lk.unlock();
            flush_events();
        }
        if (got < in_bytes) break;
    }
    {
        std::unique_lock<std::mutex> lk(hub.m);
        for (size_t i = 0; i < workers.size(); i++) workers[i]->st.eof = true;
        hub.cv.notify_all();
        hub.cv.wait(lk, [&]() {
            for (size_t i = 0; i < workers.size(); i++) if (!workers[i]->st.finished) return false;
            return true;
        });
    }
    for (size_t i = 0; i < workers.size(); i++) join_thread(workers[i].get());
    flush_events();

    char tb[48];
    snprintf(tb, sizeof tb, "%.3f", (double)total / (double)rate);
    std::string st = std::string("{\"t\":") + tb + ",\"type\":\"stats\",\"samples\":" + std::to_string(total) + ",\"frames\":{";
    for (size_t i = 0; i < workers.size(); i++) {
        if (i) st += ",";
        st += jstr(workers[i]->st.name) + ":" + std::to_string(workers[i]->st.frames);
    }
    st += "}}";
    emit(st);
    return 0;
}
