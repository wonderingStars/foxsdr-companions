// msumrcal - GPL helper: Meteor-M MSU-MR infrared calibration from text lines (pipe protocol version 1).
// Based on SatDump (GPL-3.0); see UPSTREAM.md and msumr_calib.h. Licence: GNU GPL version 3 (LICENSE in this folder).
//
//   msumrcal --foxsdr-helper 1 --format msumr-lines [--lines-per-tlm N] [--cold-offset-k K] [--emit coeffs|bt|both] < lines.txt
//   msumrcal --foxsdr-helper-info
//
// Input is text, not samples: see README.md for the two record types (T = telemetry, P = pixel row).
#ifdef _WIN32
#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "msumr_calib.h"

static const char *kVersion = "1.0.0";
static const char *kSource = "https://github.com/SatDump/SatDump";
static const char *kUpstreamCommit = "9fee0793c7d42bea2d401d330422e18c33c126fa";

static const size_t kMaxLine = 262144;          // bytes in one input line; longer lines are skipped and counted
static const long kMaxStrip = 1000000;          // highest telemetry index accepted
static const size_t kMaxPixels = 200u * 1000000; // pixels held in memory before further rows are dropped

static const char *kManifest =
    "{\"name\":\"msumrcal\",\"version\":\"1.0.0\",\"licence\":\"GPL-3.0-only\",\"source\":\"https://github.com/SatDump/SatDump\","
    "\"executable\":\"msumrcal.exe\",\"input\":\"text\",\"format\":\"msumr-lines\",\"rate\":null,"
    "\"events\":{\"cal\":[\"k\",\"ch\",\"valid\",\"reason\",\"cold_view\",\"hot_view\",\"cold_k\",\"hot_k\",\"cold_rad\",\"hot_rad\",\"gain\",\"offset\",\"filled\",\"outlier\"],"
    "\"bt\":[\"y\",\"ch\",\"k\",\"unit\",\"bt\"],\"stats\":[\"strips\",\"strips_with_tlm\",\"strips_with_temps\",\"pixel_rows\",\"pixels\",\"pixels_invalid\",\"bad_lines\"],"
    "\"status\":[\"text\"]}}";

struct PixelRow
{
    long y;
    int ch; // 4..6
    std::vector<uint32_t> px;
};

static void out_line(const std::string &s)
{
    fwrite(s.data(), 1, s.size(), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static std::string num(double v, const char *fmt)
{
    char b[64];
    snprintf(b, sizeof b, fmt, v);
    return b;
}

static void hello()
{
    std::string s = "{\"helper\":\"msumrcal\",\"version\":\"";
    s += kVersion;
    s += "\",\"protocol\":1,\"licence\":\"GPL-3.0-only\",\"source\":\"";
    s += kSource;
    s += "\",\"format\":\"msumr-lines\",\"upstream\":\"SatDump ";
    s += kUpstreamCommit;
    s += "\"}";
    out_line(s);
}

static bool parse_ulong(const char *s, unsigned long &v)
{
    if (!*s || *s == '-' || *s == '+')
        return false;
    char *e = nullptr;
    errno = 0;
    v = strtoul(s, &e, 10);
    return !errno && e && !*e;
}

static bool parse_double(const char *s, double &v)
{
    if (!*s)
        return false;
    char *e = nullptr;
    errno = 0;
    v = strtod(s, &e);
    return !errno && e && !*e && std::isfinite(v);
}

static std::vector<char *> split(char *line)
{
    std::vector<char *> f;
    char *p = line;
    while (*p)
    {
        while (*p == ' ' || *p == '\t' || *p == '\r')
            *p++ = 0;
        if (!*p)
            break;
        f.push_back(p);
        while (*p && *p != ' ' && *p != '\t' && *p != '\r')
            p++;
    }
    return f;
}

struct State
{
    std::vector<msumr::Strip> strips;
    std::vector<PixelRow> rows;
    size_t pixels_held = 0;
    unsigned long bad_lines = 0, dropped_rows = 0, dup_tlm = 0, tlm_records = 0;
    unsigned long bad_logged = 0;
    long max_p_y = -1;
};

static void bad(State &st, unsigned long lineno, const char *why)
{
    st.bad_lines++;
    if (st.bad_logged++ < 10)
        fprintf(stderr, "msumrcal: line %lu ignored: %s\n", lineno, why);
}

static void handle_line(State &st, char *line, unsigned long lineno, double cold_offset)
{
    std::vector<char *> f = split(line);
    if (f.empty() || f[0][0] == '#')
        return;
    if (!strcmp(f[0], "T"))
    {
        // T k cold1 cold2 hot1 hot2 c4cold c4hot c5cold c5hot c6cold c6hot     (temperatures in K, or "-" for all four)
        if (f.size() != 12)
            return bad(st, lineno, "T record needs 12 fields");
        unsigned long k;
        if (!parse_ulong(f[1], k) || (long)k > kMaxStrip)
            return bad(st, lineno, "T record: bad strip index");
        msumr::Strip s;
        s.present = true;
        bool dash = true, all_num = true;
        for (int i = 2; i < 6; i++)
        {
            if (strcmp(f[i], "-"))
                dash = false;
        }
        double t[4] = {0, 0, 0, 0};
        if (!dash)
        {
            for (int i = 0; i < 4; i++)
                if (!parse_double(f[2 + i], t[i]))
                    all_num = false;
            if (!all_num)
                return bad(st, lineno, "T record: temperatures must be four numbers or four '-'");
            for (int i = 0; i < 4; i++)
                if (t[i] <= 0 || t[i] > 1000)
                    return bad(st, lineno, "T record: temperature out of range (K)");
            s.has_temps = true;
            s.cold1 = t[0] + cold_offset;
            s.cold2 = t[1] + cold_offset;
            s.hot1 = t[2];
            s.hot2 = t[3];
        }
        for (int i = 0; i < 6; i++)
        {
            unsigned long v;
            if (!parse_ulong(f[6 + i], v) || v > 65535)
                return bad(st, lineno, "T record: view counts must be integers 0..65535");
            s.view[i / 2][i % 2] = (uint32_t)v;
        }
        if ((size_t)k >= st.strips.size())
            st.strips.resize(k + 1);
        if (st.strips[k].present)
            st.dup_tlm++;
        st.strips[k] = s;
        st.tlm_records++;
    }
    else if (!strcmp(f[0], "P"))
    {
        // P y ch count0 count1 ...
        if (f.size() < 4)
            return bad(st, lineno, "P record needs y, channel and at least one count");
        unsigned long y, ch;
        if (!parse_ulong(f[1], y) || y > 100000000ul)
            return bad(st, lineno, "P record: bad line number");
        if (!parse_ulong(f[2], ch) || ch < 4 || ch > 6)
            return bad(st, lineno, "P record: channel must be 4, 5 or 6");
        if (st.pixels_held + (f.size() - 3) > kMaxPixels)
        {
            st.dropped_rows++;
            return;
        }
        PixelRow r;
        r.y = (long)y;
        r.ch = (int)ch;
        r.px.reserve(f.size() - 3);
        for (size_t i = 3; i < f.size(); i++)
        {
            unsigned long v;
            if (!parse_ulong(f[i], v) || v > 65535)
                return bad(st, lineno, "P record: counts must be integers 0..65535");
            r.px.push_back((uint32_t)v);
        }
        st.pixels_held += r.px.size();
        if ((long)y > st.max_p_y)
            st.max_p_y = (long)y;
        st.rows.push_back(std::move(r));
    }
    else
        bad(st, lineno, "unknown record type (expected T or P)");
}

static void usage_error(const char *msg)
{
    fprintf(stderr, "msumrcal: %s\n", msg);
    fprintf(stderr, "usage: msumrcal --foxsdr-helper 1 --format msumr-lines [--rate N] [--lines-per-tlm N] [--cold-offset-k K] [--emit coeffs|bt|both]\n"
                    "       msumrcal --foxsdr-helper-info\n");
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    int protocol = 0;
    std::string format;
    unsigned long lines_per_tlm = 8;
    double cold_offset = 0;
    bool emit_cal = true, emit_bt = true, info = false;

    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto need = [&](const char *name) -> const char * {
            if (i + 1 >= argc)
            {
                usage_error((std::string("missing value for ") + name).c_str());
                exit(2);
            }
            return argv[++i];
        };
        if (a == "--foxsdr-helper")
        {
            unsigned long p;
            if (!parse_ulong(need("--foxsdr-helper"), p) || p != 1)
            {
                usage_error("only pipe protocol version 1 is supported");
                return 2;
            }
            protocol = 1;
        }
        else if (a == "--foxsdr-helper-info")
            info = true;
        else if (a == "--format")
            format = need("--format");
        else if (a == "--rate")
        {
            unsigned long r;
            if (!parse_ulong(need("--rate"), r))
            {
                usage_error("--rate must be a number (it is accepted and ignored: the input is text)");
                return 2;
            }
        }
        else if (a == "--lines-per-tlm")
        {
            if (!parse_ulong(need("--lines-per-tlm"), lines_per_tlm) || lines_per_tlm < 1 || lines_per_tlm > 1000)
            {
                usage_error("--lines-per-tlm must be 1..1000 (8 for Meteor LRPT)");
                return 2;
            }
        }
        else if (a == "--cold-offset-k")
        {
            if (!parse_double(need("--cold-offset-k"), cold_offset) || std::fabs(cold_offset) > 200)
            {
                usage_error("--cold-offset-k must be a number of kelvin between -200 and 200");
                return 2;
            }
        }
        else if (a == "--emit")
        {
            std::string e = need("--emit");
            if (e == "coeffs")
                emit_bt = false;
            else if (e == "bt")
                emit_cal = false;
            else if (e != "both")
            {
                usage_error("--emit must be coeffs, bt or both");
                return 2;
            }
        }
        else
        {
            usage_error(("unknown option " + a).c_str());
            return 2;
        }
    }
    if (info)
    {
        puts(kManifest);
        return 0;
    }
    if (protocol != 1)
    {
        usage_error("--foxsdr-helper 1 is required");
        return 2;
    }
    if (format != "msumr-lines")
    {
        usage_error("unsupported --format (this helper reads --format msumr-lines only)");
        return 2;
    }

    hello();

    State st;
    std::string buf;
    bool overflow = false;
    unsigned long lineno = 0;
    char chunk[65536];
    size_t n;
    auto finish_line = [&]() {
        lineno++;
        if (overflow)
        {
            bad(st, lineno, "line longer than 256 KiB skipped");
            overflow = false;
        }
        else
        {
            std::vector<char> tmp(buf.begin(), buf.end());
            tmp.push_back(0);
            handle_line(st, tmp.data(), lineno, cold_offset);
        }
        buf.clear();
    };
    while ((n = fread(chunk, 1, sizeof chunk, stdin)) > 0)
    {
        for (size_t i = 0; i < n; i++)
        {
            if (chunk[i] == '\n')
                finish_line();
            else if (!overflow)
            {
                if (buf.size() >= kMaxLine)
                {
                    overflow = true;
                    buf.clear();
                }
                else
                    buf.push_back(chunk[i] == '\0' ? ' ' : chunk[i]);
            }
        }
    }
    if (!buf.empty() || overflow)
        finish_line();

    if (st.tlm_records == 0 && st.rows.empty() && st.bad_lines == 0 && st.dropped_rows == 0)
        return 0; // empty input: the hello line only

    // The strip count is the highest telemetry index or pixel row's strip, plus one (SatDump: one entry per image timestamp).
    size_t n_strips = st.strips.size();
    if (st.max_p_y >= 0)
        n_strips = std::max(n_strips, (size_t)(st.max_p_y / (long)lines_per_tlm) + 1);
    st.strips.resize(n_strips);

    msumr::Calibration cal = msumr::build_reference(st.strips);

    size_t strips_tlm = 0, strips_temps = 0;
    for (auto &s : st.strips)
    {
        strips_tlm += s.present;
        strips_temps += s.has_temps;
    }

    // lines[k][ch]
    std::vector<msumr::Line> lines(n_strips * msumr::kIrChannels);
    std::vector<const char *> reasons(n_strips * msumr::kIrChannels, "");
    for (size_t k = 0; k < n_strips; k++)
        for (int c = 0; c < msumr::kIrChannels; c++)
        {
            const msumr::Strip &s = st.strips[k];
            msumr::Line &l = lines[k * msumr::kIrChannels + c];
            l = msumr::two_point(c, s.view[c][0], s.view[c][1], cal.ref[k].cold_k, cal.ref[k].hot_k);
            if (!l.valid)
            {
                const char *r;
                if (!s.present)
                    r = "no_telemetry";
                else if (s.view[c][0] == 0 || s.view[c][1] == 0)
                    r = "no_view";
                else if (s.view[c][0] == s.view[c][1])
                    r = "equal_views";
                else
                    r = "no_temps";
                reasons[k * msumr::kIrChannels + c] = r;
            }
        }

    if (emit_cal)
        for (size_t k = 0; k < n_strips; k++)
            for (int c = 0; c < msumr::kIrChannels; c++)
            {
                const msumr::Line &l = lines[k * msumr::kIrChannels + c];
                std::string s = "{\"t\":0,\"type\":\"cal\",\"k\":" + std::to_string(k) + ",\"ch\":" + std::to_string(c + msumr::kFirstIrChannel);
                if (!l.valid)
                {
                    s += ",\"valid\":false,\"reason\":\"";
                    s += reasons[k * msumr::kIrChannels + c];
                    s += "\"}";
                }
                else
                {
                    const msumr::Strip &sp = st.strips[k];
                    s += ",\"valid\":true,\"cold_view\":" + std::to_string(sp.view[c][0]) + ",\"hot_view\":" + std::to_string(sp.view[c][1]);
                    s += ",\"cold_k\":" + num(cal.ref[k].cold_k, "%.2f") + ",\"hot_k\":" + num(cal.ref[k].hot_k, "%.2f");
                    s += ",\"cold_rad\":" + num(l.cold_rad, "%.5f") + ",\"hot_rad\":" + num(l.hot_rad, "%.5f");
                    s += ",\"gain\":" + num(l.gain, "%.7g") + ",\"offset\":" + num(l.offset, "%.5f");
                    s += std::string(",\"filled\":") + (cal.ref[k].filled_from_neighbour ? "true" : "false");
                    s += std::string(",\"outlier\":") + (cal.ref[k].outlier_replaced ? "true" : "false") + "}";
                }
                out_line(s);
            }

    unsigned long px_total = 0, px_invalid = 0;
    for (const PixelRow &r : st.rows)
    {
        size_t k = (size_t)(r.y / (long)lines_per_tlm);
        int c = r.ch - msumr::kFirstIrChannel;
        const msumr::Line &l = lines[k * msumr::kIrChannels + c];
        std::string s;
        if (emit_bt)
        {
            s = "{\"t\":0,\"type\":\"bt\",\"y\":" + std::to_string(r.y) + ",\"ch\":" + std::to_string(r.ch) + ",\"k\":" + std::to_string(k) + ",\"unit\":\"K\",\"bt\":[";
        }
        for (size_t i = 0; i < r.px.size(); i++)
        {
            double rad = 0, kel = 0;
            bool ok = msumr::pixel_radiance(l, r.px[i], rad) && msumr::brightness_temperature(rad, c, kel);
            px_total++;
            if (!ok)
                px_invalid++;
            if (emit_bt)
            {
                if (i)
                    s += ',';
                s += ok ? num(kel, "%.2f") : std::string("null");
            }
        }
        if (emit_bt)
        {
            s += "]}";
            out_line(s);
        }
    }

    if (st.dropped_rows)
        out_line("{\"t\":0,\"type\":\"status\",\"text\":\"pixel memory limit reached: " + std::to_string(st.dropped_rows) + " pixel rows dropped\"}");
    if (st.dup_tlm)
        out_line("{\"t\":0,\"type\":\"status\",\"text\":\"" + std::to_string(st.dup_tlm) + " telemetry records repeated a strip index; the last one was used\"}");

    std::string s = "{\"t\":0,\"type\":\"stats\",\"strips\":" + std::to_string(n_strips) + ",\"strips_with_tlm\":" + std::to_string(strips_tlm) +
                    ",\"strips_with_temps\":" + std::to_string(strips_temps) + ",\"cold_mode_k\":" + num(cal.cold_mode, "%.2f") + ",\"hot_mode_k\":" + num(cal.hot_mode, "%.2f") +
                    ",\"pixel_rows\":" + std::to_string(st.rows.size()) + ",\"pixels\":" + std::to_string(px_total) + ",\"pixels_invalid\":" + std::to_string(px_invalid) +
                    ",\"bad_lines\":" + std::to_string(st.bad_lines) + ",\"dropped_rows\":" + std::to_string(st.dropped_rows) + "}";
    out_line(s);
    return 0;
}
