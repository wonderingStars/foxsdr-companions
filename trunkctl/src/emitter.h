// trunkctl - JSON Lines output (one object per line on a private copy of stdout) and the --hide-ids aliasing.
// This file is part of trunkctl (GPL-3.0).
#ifndef TRUNKCTL_EMITTER_H
#define TRUNKCTL_EMITTER_H

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

namespace trunkctl {

// One JSON object being built. Keys are written in call order.
class Line {
public:
    Line(const char *type, const char *proto, double t);
    Line &str(const char *key, const std::string &v);
    Line &str(const char *key, const char *v) { return str(key, std::string(v)); }
    Line &num(const char *key, int64_t v);
    Line &flt(const char *key, double v, int decimals);
    Line &boolean(const char *key, bool v);
    Line &hex(const char *key, uint32_t v, int digits); // "0x2f8" as a string
    const std::string &text() const { return buf_; }
    std::string finish() const { return buf_ + "}"; }

private:
    void key(const char *k);
    std::string buf_;
};

class Emitter {
public:
    // out: stream that receives the JSON Lines. rate: sample rate, to turn sample counts into seconds.
    Emitter(FILE *out, double rate, bool hide_ids) : out_(out), rate_(rate), hide_(hide_ids) {}

    double seconds(uint64_t sample) const { return (double)sample / rate_; }
    bool hide_ids() const { return hide_; }

    Line line(const char *type, const char *proto, uint64_t sample) const {
        return Line(type, proto, seconds(sample));
    }
    // Radio IDs and talkgroups: the number, or with --hide-ids a per-run alias "R3" / "T1" in order of
    // first appearance. kind is 'R' (radio) or 'T' (talkgroup).
    Line &id(Line &l, const char *key, char kind, int64_t value);

    void write(const Line &l);
    void write_raw(const std::string &s); // a complete line without the closing brace handling
    uint64_t lines() const { return lines_; }

private:
    FILE *out_;
    double rate_;
    bool hide_;
    uint64_t lines_ = 0;
    std::map<int64_t, int> rid_, tg_;
};

} // namespace trunkctl

#endif
