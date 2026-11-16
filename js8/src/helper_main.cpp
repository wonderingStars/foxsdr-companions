// SPDX-License-Identifier: GPL-3.0-only
//
// FoxSDR "js8" GPL helper: command line, input loop and JSON Lines output. Written for this helper; it drives the
// JS8Call decoder (js8_decoder.cpp, a port of JS8Call's JS8.cpp, GPL-3.0) and the message layer (js8_varicode.cpp).
// See ../UPSTREAM.md and ../README.md.
//
//   js8-helper --foxsdr-helper 1 --format s16 --rate 12000 [--modes ABCE] [--start-time <epoch s>] < audio.raw
//
// Reads 12 kHz mono audio on standard input until end of file; prints one JSON object per line on standard output.
// Receive only: nothing is transmitted, no radio is controlled, no network is touched.

#include "js8_core.h"
#include "js8_varicode.h"
#include "helper_manifest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace
{
    constexpr char const * kName    = "js8";
    constexpr char const * kVersion = "1.0.0";
    constexpr char const * kLicence = "GPL-3.0-only";
    constexpr char const * kSource  = "https://github.com/js8call/js8call";
    constexpr char const * kUpstream = "JS8Call a7ff1be0b389d287fdc56e2ea0d06962aa68127d";
    constexpr int          kRate    = JS8_RX_SAMPLE_RATE;

    // --- output -------------------------------------------------------------------------------------------------
    void emitLine(std::string const & line)
    {
        if (std::fwrite(line.data(), 1, line.size(), stdout) != line.size() || std::fputc('\n', stdout) == EOF ||
            std::fflush(stdout) != 0)
        {
            std::fprintf(stderr, "js8: cannot write to standard output; stopping\n");
            std::exit(3);
        }
    }

    std::string jsonEscape(std::string const & s)
    {
        std::string out = "\"";
        for (unsigned char c : s)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20 || c == 0x7f)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof buf, "\\u%04x", c);
                        out += buf;
                    }
                    else
                    {
                        out += static_cast<char>(c);
                    }
            }
        }
        out += '"';
        return out;
    }

    std::string num(double v, int decimals)
    {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.*f", decimals, std::isfinite(v) ? v : 0.0);
        if (std::strcmp(buf, "-0.0") == 0 || std::strcmp(buf, "-0.00") == 0 || std::strcmp(buf, "-0.000") == 0)
        {
            return std::string(buf + 1);
        }
        return buf;
    }

    // --- time ---------------------------------------------------------------------------------------------------
    // Civil date from days since 1970-01-01 (proleptic Gregorian), so no C runtime time zone code is involved.
    void civilFromDays(long long z, int & y, int & m, int & d)
    {
        z += 719468;
        long long const era = (z >= 0 ? z : z - 146096) / 146097;
        long long const doe = z - era * 146097;
        long long const yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        long long const yy  = yoe + era * 400;
        long long const doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        long long const mp  = (5 * doy + 2) / 153;
        d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
        m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
        y = static_cast<int>(yy + (m <= 2 ? 1 : 0));
    }

    std::string isoUtc(long long epochSeconds, int & hhmmss)
    {
        long long days = epochSeconds / 86400;
        long long rem  = epochSeconds % 86400;
        if (rem < 0) { rem += 86400; days -= 1; }
        int y, mo, d;
        civilFromDays(days, y, mo, d);
        int const h = static_cast<int>(rem / 3600), mi = static_cast<int>(rem % 3600 / 60), s = static_cast<int>(rem % 60);
        hhmmss = h * 10000 + mi * 100 + s;
        char buf[40];
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ", y, mo, d, h, mi, s);
        return buf;
    }

    // --- options ------------------------------------------------------------------------------------------------
    struct Options
    {
        std::string format = "s16";
        int         rate   = kRate;
        std::string modes  = "ABCE";
        double      startTime = 0.0;
        int         nfa = 100;
        int         nfb = 4910;
        int         nfqso = 1500;
        bool        textEvents = false;
        bool        protocol = false;
        bool        info = false;
    };

    void usage()
    {
        std::fprintf(stderr,
            "usage: js8-helper --foxsdr-helper 1 --format s16|f32 --rate 12000 [options] < audio\n"
            "       js8-helper --foxsdr-helper-info\n"
            "options:\n"
            "  --modes ABCE       speeds to decode: A Normal 15 s, B Fast 10 s, C Turbo 6 s, E Slow 30 s, I Ultra 4 s\n"
            "  --start-time T     UTC of the first sample, seconds since 1970 (default 0: first sample is a boundary)\n"
            "  --nfa HZ --nfb HZ  audio frequency range searched (default 100 .. 4910)\n"
            "  --nfqso HZ         audio frequency tried first (default 1500)\n"
            "  --text-events      also print a reserved \"text\" event for every frame\n");
    }

    bool parseInt(char const * s, int lo, int hi, int & out)
    {
        char * end = nullptr;
        long v = std::strtol(s, &end, 10);
        if (!s[0] || *end || v < lo || v > hi) return false;
        out = static_cast<int>(v);
        return true;
    }

    bool parse(int argc, char ** argv, Options & o)
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string const a = argv[i];
            auto next = [&](char const * what) -> char const *
            {
                if (i + 1 >= argc) { std::fprintf(stderr, "js8: %s needs a value\n", what); return nullptr; }
                return argv[++i];
            };

            if (a == "--foxsdr-helper")
            {
                char const * v = next("--foxsdr-helper");
                if (!v) return false;
                if (std::strcmp(v, "1") != 0)
                {
                    std::fprintf(stderr, "js8: unsupported protocol version %s (this helper speaks 1)\n", v);
                    return false;
                }
                o.protocol = true;
            }
            else if (a == "--foxsdr-helper-info") o.info = true;
            else if (a == "--format")
            {
                char const * v = next("--format");
                if (!v) return false;
                o.format = v;
            }
            else if (a == "--rate")
            {
                char const * v = next("--rate");
                if (!v || !parseInt(v, 1, 100000000, o.rate)) { std::fprintf(stderr, "js8: bad --rate\n"); return false; }
            }
            else if (a == "--centre" || a == "--center")
            {
                if (!next("--centre")) return false; // accepted and ignored: audio input has no centre frequency
            }
            else if (a == "--modes")
            {
                char const * v = next("--modes");
                if (!v) return false;
                o.modes = v;
            }
            else if (a == "--start-time")
            {
                char const * v = next("--start-time");
                if (!v) return false;
                char * end = nullptr;
                o.startTime = std::strtod(v, &end);
                if (!v[0] || *end || !std::isfinite(o.startTime) || o.startTime < 0)
                {
                    std::fprintf(stderr, "js8: bad --start-time\n");
                    return false;
                }
            }
            else if (a == "--nfa" || a == "--nfb" || a == "--nfqso")
            {
                char const * v = next(a.c_str());
                int          n = 0;
                if (!v || !parseInt(v, 0, 6000, n)) { std::fprintf(stderr, "js8: bad %s\n", a.c_str()); return false; }
                (a == "--nfa" ? o.nfa : a == "--nfb" ? o.nfb : o.nfqso) = n;
            }
            else if (a == "--text-events") o.textEvents = true;
            else if (a == "--help" || a == "-h") { usage(); std::exit(0); }
            else
            {
                std::fprintf(stderr, "js8: unknown option %s\n", a.c_str());
                usage();
                return false;
            }
        }
        return true;
    }

    // --- decoding schedule ------------------------------------------------------------------------------------
    struct Slot
    {
        int                 submode;
        JS8::ModeInfo const * info;
        long long           period;    // samples
        long long           next;      // absolute sample index of the next period start
    };

    struct Stats
    {
        long long samples = 0;
        long long slots = 0;
        long long frames = 0;
        std::map<char, long long> byMode;
    };

    std::string frameLine(Options const & o, JS8::ModeInfo const & info, JS8::Event::Decoded const & d,
                          Js8::Frame const & f, double slotStart, double t, std::string const & utc)
    {
        bool const lowConfidence = d.quality < 0.17f; // DecodedText QUALITY_THRESHOLD
        std::string s = "{\"t\":" + num(t, 3) + ",\"type\":\"js8\",\"mode\":\"";
        s += info.letter;
        s += "\",\"speed\":\"";
        s += info.name;
        s += "\",\"slot_t\":" + num(slotStart, 3) + ",\"utc\":\"" + utc + "\"";
        s += ",\"freq\":" + num(d.frequency, 1) + ",\"dt\":" + num(d.xdt, 2) + ",\"snr\":" + std::to_string(d.snr);
        s += ",\"quality\":" + num(d.quality, 2) + ",\"low_confidence\":" + (lowConfidence ? "true" : "false");
        s += ",\"bits\":" + std::to_string(d.type);
        s += std::string(",\"first\":") + ((d.type & Js8::BitsFirst) ? "true" : "false");
        s += std::string(",\"last\":") + ((d.type & Js8::BitsLast) ? "true" : "false");
        s += std::string(",\"data_flag\":") + ((d.type & Js8::BitsData) ? "true" : "false");
        s += ",\"frame\":" + jsonEscape(d.data);
        s += std::string(",\"frame_type\":\"") + Js8::frameTypeName(f.frameType) + "\"";
        s += ",\"text\":" + jsonEscape(f.text);
        if (!f.from.empty()) s += ",\"from\":" + jsonEscape(f.from);
        if (!f.to.empty())   s += ",\"to\":" + jsonEscape(f.to);
        if (!f.cmd.empty())  s += ",\"cmd\":" + jsonEscape(f.cmd);
        if (!f.num.empty())  s += ",\"num\":" + jsonEscape(f.num);
        if (!f.grid.empty()) s += ",\"grid\":" + jsonEscape(f.grid);
        if (!f.cq.empty())   s += ",\"cq\":" + jsonEscape(f.cq);
        s += "}";
        (void)o;
        return s;
    }

    // Decode one period of one speed and print what was found.
    void runSlot(Options const & o, Slot & slot, std::vector<std::int16_t> const & buf, long long bufBase,
                 long long total, bool partial, Stats & stats)
    {
        long long const start = slot.next;
        long long const avail = std::min(total - start, slot.period);
        std::int16_t const * samples = buf.data() + (start - bufBase);

        long long const epochStart = static_cast<long long>(std::floor(o.startTime + static_cast<double>(start) / kRate + 0.5));
        int hhmmss = 0;
        std::string const utc = isoUtc(epochStart, hhmmss);

        JS8::DecodeParams params;
        params.nutc  = hhmmss;
        params.nfqso = o.nfqso;
        params.nfa   = o.nfa;
        params.nfb   = o.nfb;

        // Collect the decoder's events; a frame found again (on a later pass, say) replaces the earlier one only if
        // its SNR is better, which is what upstream's event stream amounts to. One line per distinct frame.
        std::vector<JS8::Event::Decoded> found;
        std::map<std::pair<int, std::string>, std::size_t> seen;

        JS8::decode(slot.submode, params, samples, static_cast<std::size_t>(avail),
                    [&](JS8::Event::Decoded const & d)
                    {
                        auto key = std::make_pair(d.type, d.data);
                        auto it  = seen.find(key);
                        if (it == seen.end())
                        {
                            seen[key] = found.size();
                            found.push_back(d);
                        }
                        else
                        {
                            found[it->second] = d;
                        }
                    });

        double const slotStart = static_cast<double>(start) / kRate;
        double const t         = static_cast<double>(start + avail) / kRate;

        ++stats.slots;
        for (auto const & d : found)
        {
            Js8::Frame const f = Js8::interpret(d.data, d.type);
            emitLine(frameLine(o, *slot.info, d, f, slotStart, t, utc));
            if (o.textEvents)
            {
                std::string text = std::string("{\"t\":") + num(t, 3) + ",\"type\":\"text\",\"text\":" +
                    jsonEscape(std::string("[") + slot.info->letter + " " + num(d.frequency, 0) + "Hz " +
                               std::to_string(d.snr) + "dB] " + f.text) + "}";
                emitLine(text);
            }
            ++stats.frames;
            ++stats.byMode[slot.info->letter];
        }
        (void)partial;
    }
}

int main(int argc, char ** argv)
{
#ifdef _WIN32
    // No fault box, no "insert disk" box, ever: a helper has no one to answer them.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    Options o;
    if (!parse(argc, argv, o)) return 2;

    if (o.info)
    {
        std::fputs(HELPER_MANIFEST_JSON, stdout);
        std::fflush(stdout);
        return 0;
    }

    if (!o.protocol)
    {
        std::fprintf(stderr, "js8: missing --foxsdr-helper 1\n");
        usage();
        return 2;
    }
    if (o.format != "s16" && o.format != "f32")
    {
        std::fprintf(stderr, "js8: unsupported --format %s (audio formats s16 and f32 only; this decoder does not take I/Q)\n",
                     o.format.c_str());
        return 2;
    }
    if (o.rate != kRate)
    {
        std::fprintf(stderr, "js8: unsupported --rate %d (JS8 audio is 12000 Hz mono; resample first)\n", o.rate);
        return 2;
    }
    if (o.nfb <= o.nfa)
    {
        std::fprintf(stderr, "js8: --nfb must be above --nfa\n");
        return 2;
    }

    std::vector<Slot> slots;
    for (char c : o.modes)
    {
        int sub = c == 'A' ? JS8::NORMAL : c == 'B' ? JS8::FAST : c == 'C' ? JS8::TURBO : c == 'E' ? JS8::SLOW :
                  c == 'I' ? JS8::ULTRA : -1;
        if (sub < 0)
        {
            std::fprintf(stderr, "js8: unknown mode letter '%c' in --modes (use A B C E I)\n", c);
            return 2;
        }
        bool dup = false;
        for (auto const & s : slots) dup = dup || s.submode == sub;
        if (dup) continue;

        auto const * info = JS8::modeInfo(sub);
        long long const period = static_cast<long long>(info->periodSeconds) * kRate;
        // first period boundary at or after the first sample: periods start where (UTC seconds mod period) == 0
        double const P = info->periodSeconds;
        double const first = std::ceil(o.startTime / P) * P - o.startTime;
        slots.push_back({sub, info, period, static_cast<long long>(std::floor(first * kRate + 0.5))});
    }
    if (slots.empty())
    {
        std::fprintf(stderr, "js8: --modes selects nothing\n");
        return 2;
    }
    // fixed order: A B C E I (also the tie-break when two periods end together)
    std::sort(slots.begin(), slots.end(), [](Slot const & a, Slot const & b)
    {
        auto rank = [](int s) { return s == JS8::NORMAL ? 0 : s == JS8::FAST ? 1 : s == JS8::TURBO ? 2 : s == JS8::SLOW ? 3 : 4; };
        return rank(a.submode) < rank(b.submode);
    });

    std::string hello = std::string("{\"helper\":\"") + kName + "\",\"version\":\"" + kVersion +
        "\",\"protocol\":1,\"licence\":\"" + kLicence + "\",\"source\":\"" + kSource + "\",\"upstream\":\"" +
        kUpstream + "\",\"format\":\"" + o.format + "\",\"rate\":" + std::to_string(o.rate) + ",\"modes\":\"";
    for (auto const & s : slots) hello += s.info->letter;
    hello += "\"}";
    emitLine(hello);

    Stats stats;
    std::vector<std::int16_t> buf;   // samples [bufBase, bufBase + size)
    long long bufBase = 0;
    long long total   = 0;

    std::vector<unsigned char> raw(1 << 16);
    std::size_t carry = 0;
    std::size_t const width = o.format == "s16" ? 2 : 4;

    auto runDue = [&](bool final)
    {
        for (;;)
        {
            Slot * best = nullptr;
            for (auto & s : slots)
            {
                if (total >= s.next + s.period && (!best || s.next + s.period < best->next + best->period)) best = &s;
            }
            if (!best) break;
            try
            {
                runSlot(o, *best, buf, bufBase, total, false, stats);
            }
            catch (std::exception const & e)
            {
                std::fprintf(stderr, "js8: decoder error: %s\n", e.what());
            }
            best->next += best->period;
        }

        if (final)
        {
            // The last, partial period of each speed, if it holds a whole frame.
            for (auto & s : slots)
            {
                if (total > s.next && total - s.next >= s.info->framesNeeded)
                {
                    try
                    {
                        runSlot(o, s, buf, bufBase, total, true, stats);
                    }
                    catch (std::exception const & e)
                    {
                        std::fprintf(stderr, "js8: decoder error: %s\n", e.what());
                    }
                    s.next = total;
                }
            }
        }

        // drop audio no later period can use
        long long keep = total;
        for (auto const & s : slots) keep = std::min(keep, std::max<long long>(s.next, 0));
        if (keep > bufBase)
        {
            buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(keep - bufBase));
            bufBase = keep;
        }
    };

    for (;;)
    {
        std::size_t const got = std::fread(raw.data() + carry, 1, raw.size() - carry, stdin);
        if (got == 0) break;

        std::size_t const have  = carry + got;
        std::size_t const whole = have / width;

        for (std::size_t i = 0; i < whole; ++i)
        {
            unsigned char const * p = raw.data() + i * width;
            std::int16_t v;
            if (width == 2)
            {
                v = static_cast<std::int16_t>(static_cast<std::uint16_t>(p[0] | (p[1] << 8)));
            }
            else
            {
                std::uint32_t bits = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
                                     (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
                float f;
                std::memcpy(&f, &bits, 4);
                // the decoder works on 16-bit sample values, so float +-1.0 is full scale
                double x = std::isfinite(f) ? static_cast<double>(f) * 32768.0 : 0.0;
                x = std::max(-32768.0, std::min(32767.0, x));
                v = static_cast<std::int16_t>(std::floor(x + 0.5));
            }
            buf.push_back(v);
        }
        total += static_cast<long long>(whole);

        carry = have - whole * width;
        if (carry) std::memmove(raw.data(), raw.data() + whole * width, carry);

        runDue(false);
    }

    runDue(true);
    stats.samples = total;

    std::string st = "{\"t\":" + num(static_cast<double>(total) / kRate, 3) + ",\"type\":\"stats\",\"samples\":" +
                     std::to_string(total) + ",\"slots\":" + std::to_string(stats.slots) + ",\"frames\":" +
                     std::to_string(stats.frames) + ",\"frames_by_mode\":{";
    bool firstMode = true;
    for (auto const & kv : stats.byMode)
    {
        if (!firstMode) st += ",";
        firstMode = false;
        st += std::string("\"") + kv.first + "\":" + std::to_string(kv.second);
    }
    st += "}}";
    emitLine(st);
    return 0;
}
