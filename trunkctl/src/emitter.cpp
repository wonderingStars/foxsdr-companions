// trunkctl - JSON Lines output. This file is part of trunkctl (GPL-3.0).
#include "emitter.h"

#include <cstdio>

namespace trunkctl {

static void json_escape(std::string &out, const std::string &s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char b[8];
                snprintf(b, sizeof b, "\\u%04x", c);
                out += b;
            } else {
                out += (char)c;
            }
        }
    }
    out += '"';
}

Line::Line(const char *type, const char *proto, double t) {
    char b[48];
    snprintf(b, sizeof b, "{\"t\":%.6f", t);
    buf_ = b;
    str("type", type);
    str("proto", proto);
}

void Line::key(const char *k) {
    buf_ += ",\"";
    buf_ += k;
    buf_ += "\":";
}

Line &Line::str(const char *k, const std::string &v) {
    key(k);
    json_escape(buf_, v);
    return *this;
}

Line &Line::num(const char *k, int64_t v) {
    key(k);
    buf_ += std::to_string((long long)v);
    return *this;
}

Line &Line::flt(const char *k, double v, int decimals) {
    key(k);
    char b[48];
    snprintf(b, sizeof b, "%.*f", decimals, v);
    buf_ += b;
    return *this;
}

Line &Line::boolean(const char *k, bool v) {
    key(k);
    buf_ += v ? "true" : "false";
    return *this;
}

Line &Line::hex(const char *k, uint32_t v, int digits) {
    char b[24];
    snprintf(b, sizeof b, "0x%0*x", digits, v);
    return str(k, std::string(b));
}

Line &Emitter::id(Line &l, const char *key, char kind, int64_t value) {
    if (!hide_)
        return l.num(key, value);
    std::map<int64_t, int> &m = (kind == 'R') ? rid_ : tg_;
    auto it = m.find(value);
    int n;
    if (it == m.end()) {
        n = (int)m.size() + 1;
        m[value] = n;
    } else {
        n = it->second;
    }
    return l.str(key, std::string(1, kind) + std::to_string(n));
}

void Emitter::write(const Line &l) { write_raw(l.finish()); }

void Emitter::write_raw(const std::string &s) {
    fwrite(s.data(), 1, s.size(), out_);
    fputc('\n', out_);
    fflush(out_);
    lines_++;
}

} // namespace trunkctl
