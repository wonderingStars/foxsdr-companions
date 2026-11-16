/*
 * sondes_glue.cpp - the stream and text-capture functions behind sondes_glue.h.
 *
 * Part of the FoxSDR "sondes" GPL helper. Licence: GNU GPL v3 (see ../LICENSE).
 * Compiled WITHOUT the force-included sondes_glue.h (it uses the real stdio).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include <utility>

#include "sondes_stream.h"

thread_local sondes_stream *sondes_tls = nullptr;

extern "C++" {
FILE *sondes_stdin(void);
size_t sondes_fread(void *ptr, size_t size, size_t n, FILE *f);
int sondes_fgetc(FILE *f);
int sondes_fclose(FILE *f);
int sondes_printf(const char *fmt, ...);
int sondes_fprintf(FILE *f, const char *fmt, ...);
int sondes_decoder_rate(void);
}

/* The "FILE *" the decoders see for the sample stream is the stream object itself; only identity is ever tested. */
FILE *sondes_stdin(void) { return reinterpret_cast<FILE *>(sondes_tls); }
int sondes_decoder_rate(void) { return sondes_tls ? sondes_tls->rate : 0; }

/* Make the next chunk current; blocks (and tells the main thread so) until one arrives or the input ends. */
static bool refill(sondes_stream *s) {
    if (s->dry) return false;
    std::unique_lock<std::mutex> lk(s->hub->m);
    while (s->q.empty()) {
        if (s->eof) { s->dry = true; return false; }
        s->waiting = true;
        s->hub->cv.notify_all();
        s->hub->cv.wait(lk);
    }
    s->waiting = false;
    s->held = s->q.front();
    s->q.pop_front();
    s->cur = s->held->data();
    s->left = s->held->size();
    return true;
}

size_t sondes_fread(void *ptr, size_t size, size_t n, FILE *f) {
    sondes_stream *s = reinterpret_cast<sondes_stream *>(f);
    if (s != sondes_tls || s == nullptr) return fread(ptr, size, n, f);   /* a real file (e.g. an ephemeris) */
    if (size == 0 || n == 0) return 0;
    uint8_t *dst = static_cast<uint8_t *>(ptr);
    size_t want = size * n, got = 0;
    while (got < want) {
        if (s->left == 0 && !refill(s)) break;
        size_t k = want - got < s->left ? want - got : s->left;
        memcpy(dst + got, s->cur, k);
        s->cur += k; s->left -= k; got += k; s->consumed += k;
    }
    return got / size;
}

int sondes_fgetc(FILE *f) {
    sondes_stream *s = reinterpret_cast<sondes_stream *>(f);
    if (s != sondes_tls || s == nullptr) return fgetc(f);
    unsigned char c;
    return sondes_fread(&c, 1, 1, f) == 1 ? (int)c : EOF;
}

int sondes_fclose(FILE *f) {
    if (f == reinterpret_cast<FILE *>(sondes_tls) && f != nullptr) return 0;   /* the stream is the helper's */
    return fclose(f);
}

/* ---------------------------------------------------------------------------------------------------------- */
/* captured text: JSON lines of the upstream "auto_rx" output become events, everything else is dropped        */

static std::string esc(const std::string &v) {
    std::string o;
    for (size_t i = 0; i < v.size(); i++) {
        unsigned char c = (unsigned char)v[i];
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}

static bool is_number(const std::string &v) {
    if (v.empty()) return false;
    char *end = nullptr;
    double d = strtod(v.c_str(), &end);
    if (end == v.c_str() || *end != '\0') return false;
    if (!(d == d) || d > 1e300 || d < -1e300) return false;                /* nan, inf */
    char c = v[0];
    if (!((c >= '0' && c <= '9') || c == '-')) return false;               /* strtod also accepts "inf", "0x1p3" */
    for (size_t i = 0; i < v.size(); i++) {
        char x = v[i];
        if (!((x >= '0' && x <= '9') || x == '-' || x == '+' || x == '.' || x == 'e' || x == 'E')) return false;
    }
    return true;
}

/* Turns one upstream JSON line into the helper's event line. Returns false if it is not a flat object. */
static bool convert(const std::string &in, double t, std::string &out) {
    size_t i = in.find('{');
    if (i == std::string::npos) return false;
    i++;
    std::vector<std::pair<std::string, std::string> > kv;       /* value: JSON text */
    for (;;) {
        while (i < in.size() && (in[i] == ' ' || in[i] == ',' || in[i] == '\t')) i++;
        if (i >= in.size() || in[i] == '}') break;
        if (in[i] != '"') return false;
        size_t e = in.find('"', i + 1);
        if (e == std::string::npos) return false;
        std::string key = in.substr(i + 1, e - i - 1);
        i = e + 1;
        while (i < in.size() && (in[i] == ' ' || in[i] == ':')) i++;
        std::string val;
        if (i < in.size() && in[i] == '"') {
            size_t j = i + 1;
            std::string sv;
            while (j < in.size() && in[j] != '"') { if (in[j] == '\\' && j + 1 < in.size()) j++; sv += in[j]; j++; }
            if (j >= in.size()) return false;
            val = "\"" + esc(sv) + "\"";
            i = j + 1;
        } else {
            size_t j = i;
            while (j < in.size() && in[j] != ',' && in[j] != '}') j++;
            std::string nv = in.substr(i, j - i);
            while (!nv.empty() && (nv.back() == ' ' || nv.back() == '\n' || nv.back() == '\r')) nv.pop_back();
            val = is_number(nv) ? nv : "null";
            i = j;
        }
        kv.push_back(std::make_pair(key, val));
    }
    if (kv.empty()) return false;
    char tb[48];
    snprintf(tb, sizeof tb, "%.3f", t);
    out = std::string("{\"t\":") + tb + ",\"type\":\"sonde\"";
    for (size_t k = 0; k < kv.size(); k++) {
        std::string key = kv[k].first;
        if (key == "version") continue;                                  /* upstream's own build tag (empty) */
        if (key == "type") key = "sonde";
        else if (key == "id") key = "serial";
        else if (key == "datetime") key = "time";
        out += ",\"" + esc(key) + "\":" + kv[k].second;
    }
    out += "}";
    return true;
}

static void line_done(sondes_stream *s) {
    std::string line;
    line.swap(s->linebuf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    size_t a = line.find_first_not_of(" \t");
    if (a == std::string::npos) return;
    if (line[a] != '{') { s->text_lines++; return; }
    double t = (double)(s->consumed / 2) / (double)(s->rate > 0 ? s->rate : 1);
    std::string ev;
    if (!convert(line, t, ev)) { s->text_lines++; return; }
    std::lock_guard<std::mutex> lk(s->hub->m);
    sondes_event e;
    e.t = t;
    e.line.swap(ev);
    s->out.push_back(std::move(e));
    s->frames++;
}

static void capture(sondes_stream *s, const char *buf, size_t n) {
    for (size_t i = 0; i < n; i++) {
        s->linebuf += buf[i];
        if (buf[i] == '\n') line_done(s);
        else if (s->linebuf.size() > 8192) { s->linebuf.clear(); s->text_lines++; }   /* never grow without bound */
    }
}

static int vcapture(sondes_stream *s, const char *fmt, va_list ap) {
    char stackbuf[2048];
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(stackbuf, sizeof stackbuf, fmt, ap);
    if (n < 0) { va_end(ap2); return n; }
    if ((size_t)n < sizeof stackbuf) capture(s, stackbuf, (size_t)n);
    else {
        std::vector<char> big((size_t)n + 1);
        vsnprintf(big.data(), big.size(), fmt, ap2);
        capture(s, big.data(), (size_t)n);
    }
    va_end(ap2);
    return n;
}

int sondes_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n;
    if (sondes_tls) n = vcapture(sondes_tls, fmt, ap);
    else n = vfprintf(stderr, fmt, ap);
    va_end(ap);
    return n;
}

int sondes_fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n;
    if (f == stdout && sondes_tls) n = vcapture(sondes_tls, fmt, ap);
    else n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}
