// ----------------------------------------------------------------------------
// modes.cpp -- FoxSDR fldigimodes helper: mode names and the decoder factory. Written for this helper; GPL-3.0-only.
// ----------------------------------------------------------------------------
#include "decoder.h"
#include "fm_modes.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace fm {

static std::string lower(std::string s)
{
    for (auto &c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

static bool to_int(const std::string &s, int &v)
{
    if (s.empty())
        return false;
    char *end = nullptr;
    long x = std::strtol(s.c_str(), &end, 10);
    if (*end != 0)
        return false;
    v = (int)x;
    return true;
}

static std::vector<std::string> split(const std::string &s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else
            cur += c;
    }
    out.push_back(cur);
    return out;
}

static bool valid_tones(int t) { return t == 2 || t == 4 || t == 8 || t == 16 || t == 32 || t == 64 || t == 128 || t == 256; }
static bool valid_bw(int b) { return b == 125 || b == 250 || b == 500 || b == 1000 || b == 2000; }

bool parse_mode(const std::string &text_in, ModeSpec &out, std::string &err)
{
    std::string text = lower(text_in);
    ModeSpec m = out;  // keeps values (tones, bandwidth ...) given by other options
    std::vector<std::string> p = split(text, '-');
    const std::string &fam = p[0];

    if (fam == "olivia" || fam == "contestia") {
        m.family = fam;
        if (p.size() == 3) {
            int t, b;
            if (!to_int(p[1], t) || !to_int(p[2], b)) {
                err = "mode " + text_in + ": expected " + fam + "-<tones>-<bandwidth>, for example " + fam + "-8-500";
                return false;
            }
            m.tones = t;
            m.bandwidth = b;
        } else if (p.size() != 1) {
            err = "mode " + text_in + ": expected " + fam + "-<tones>-<bandwidth>, for example " + fam + "-8-500";
            return false;
        }
        if (m.tones == 0)
            m.tones = 8;
        if (m.bandwidth == 0)
            m.bandwidth = 500;
        if (!valid_tones(m.tones)) {
            err = fam + ": tones must be 2, 4, 8, 16, 32, 64, 128 or 256";
            return false;
        }
        if (!valid_bw(m.bandwidth)) {
            err = fam + ": bandwidth must be 125, 250, 500, 1000 or 2000";
            return false;
        }
        out = m;
        return true;
    }

    if (fam.compare(0, 4, "mfsk") == 0 && p.size() == 1) {
        std::string v = fam.substr(4);
        bool lng = false;
        if (!v.empty() && v.back() == 'l') {
            lng = true;
            v.pop_back();
        }
        int n;
        if (!to_int(v, n)) {
            err = "mode " + text_in + ": expected mfsk<n>, for example mfsk16";
            return false;
        }
        static const int ok[] = {4, 8, 11, 16, 22, 31, 32, 64, 128};
        bool found = std::find(std::begin(ok), std::end(ok), n) != std::end(ok);
        if (lng && n != 64 && n != 128)
            found = false;
        if (!found) {
            err = "mode " + text_in + ": MFSK modes are mfsk4 mfsk8 mfsk11 mfsk16 mfsk22 mfsk31 mfsk32 mfsk64 mfsk128 mfsk64l mfsk128l";
            return false;
        }
        m.family = "mfsk";
        m.variant = lng ? -n : n;  // negative: the long-interleave variant
        out = m;
        return true;
    }

    if (fam.compare(0, 4, "thor") == 0 && p.size() == 1) {
        std::string v = fam.substr(4);
        static const char *ok[] = {"micro", "4", "5", "8", "11", "16", "22", "25x4", "50x1", "50x2", "100"};
        int idx = -1;
        for (int i = 0; i < 11; i++)
            if (v == ok[i])
                idx = i;
        if (idx < 0) {
            err = "mode " + text_in + ": THOR modes are thormicro thor4 thor5 thor8 thor11 thor16 thor22 thor25x4 thor50x1 thor50x2 thor100";
            return false;
        }
        m.family = "thor";
        m.variant = idx + 1;  // index into the list above, 1-based
        out = m;
        return true;
    }

    if (fam.compare(0, 4, "mt63") == 0) {
        // mt63-500s, mt63-1000l ... ; also mt63500s
        std::string v = fam.size() > 4 ? fam.substr(4) : (p.size() == 2 ? p[1] : "");
        if (fam.size() == 4 && p.size() == 2)
            v = p[1];
        else if (p.size() != 1) {
            err = "mode " + text_in + ": expected mt63-500s, mt63-500l, mt63-1000s, mt63-1000l, mt63-2000s or mt63-2000l";
            return false;
        }
        if (v.size() < 2 || (v.back() != 's' && v.back() != 'l')) {
            err = "mode " + text_in + ": expected mt63-<500|1000|2000><s|l>, for example mt63-1000l";
            return false;
        }
        int b;
        if (!to_int(v.substr(0, v.size() - 1), b) || (b != 500 && b != 1000 && b != 2000)) {
            err = "mode " + text_in + ": MT63 bandwidth must be 500, 1000 or 2000";
            return false;
        }
        m.family = "mt63";
        m.bandwidth = b;
        m.variant = v.back() == 'l' ? 64 : 32;
        out = m;
        return true;
    }

    err = "unknown mode '" + text_in + "' (families: olivia-<tones>-<bw>, contestia-<tones>-<bw>, mfsk<n>, mt63-<bw><s|l>, thor<n>)";
    return false;
}

std::string mode_name(const ModeSpec &m)
{
    if (m.family == "olivia" || m.family == "contestia")
        return m.family + "-" + std::to_string(m.tones) + "-" + std::to_string(m.bandwidth);
    if (m.family == "mfsk") {
        std::string s = "mfsk" + std::to_string(m.variant < 0 ? -m.variant : m.variant);
        if (m.variant < 0)
            s += "l";
        return s;
    }
    if (m.family == "mt63")
        return "mt63-" + std::to_string(m.bandwidth) + (m.variant == 64 ? "l" : "s");
    if (m.family == "thor") {
        static const char *ok[] = {"micro", "4", "5", "8", "11", "16", "22", "25x4", "50x1", "50x2", "100"};
        return std::string("thor") + ok[m.variant - 1];
    }
    return m.family;
}

long long fldigi_mode_id(const ModeSpec &m)
{
    if (m.family == "mfsk") {
        switch (m.variant) {
        case 4: return MODE_MFSK4;
        case 8: return MODE_MFSK8;
        case 11: return MODE_MFSK11;
        case 16: return MODE_MFSK16;
        case 22: return MODE_MFSK22;
        case 31: return MODE_MFSK31;
        case 32: return MODE_MFSK32;
        case 64: return MODE_MFSK64;
        case 128: return MODE_MFSK128;
        case -64: return MODE_MFSK64L;
        case -128: return MODE_MFSK128L;
        }
    } else if (m.family == "thor") {
        static const long long ids[] = {MODE_THORMICRO, MODE_THOR4,   MODE_THOR5,   MODE_THOR8,   MODE_THOR11, MODE_THOR16,
                                        MODE_THOR22,    MODE_THOR25x4, MODE_THOR50x1, MODE_THOR50x2, MODE_THOR100};
        return ids[m.variant - 1];
    } else if (m.family == "mt63") {
        if (m.bandwidth == 500) return m.variant == 64 ? MODE_MT63_500L : MODE_MT63_500S;
        if (m.bandwidth == 1000) return m.variant == 64 ? MODE_MT63_1000L : MODE_MT63_1000S;
        return m.variant == 64 ? MODE_MT63_2000L : MODE_MT63_2000S;
    }
    return MODE_NULL;
}

int mode_rate(const ModeSpec &m)
{
    if (m.family == "mfsk" && (m.variant == 11 || m.variant == 22))
        return 11025;
    if (m.family == "thor" && (m.variant == 3 || m.variant == 5 || m.variant == 7))  // thor5, thor11, thor22
        return 11025;
    return 8000;
}

}  // namespace fm
