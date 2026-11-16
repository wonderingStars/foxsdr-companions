// trunkfmt: FoxSDR GPL helper, protocol version 1.  See common.h for the licence notice.
//
//   trunkfmt --foxsdr-helper 1 --format s16|f32 --rate <Hz> [--decoders lj1200,tait,fleetsync2,mpt1327] [--invert] [--no-unchecked]
//   trunkfmt --foxsdr-helper 1 --format csbk-hex
//   trunkfmt --foxsdr-helper-info
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

#include "afsk1200.h"
#include "common.h"
#include "dmr_connectplus.h"
#include "fleetsync2.h"
#include "lj1200.h"
#include "ltr.h"
#include "ltrdsp.h"
#include "manifest.h"
#include "mpt1327.h"
#include "passport.h"
#include "tait1200.h"

namespace trunkfmt {

void emitLine(const std::string& line)
{
    fwrite(line.data(), 1, line.size(), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static const char* VERSION = "0.1.0";

static void hello()
{
    emitLine(Json().str("helper", "trunkfmt").str("version", VERSION).num("protocol", 1)
                 .str("licence", "GPL-3.0-or-later").str("source", "https://github.com/DSheirer/sdrtrunk")
                 .str("upstream", "DSheirer/sdrtrunk a53f6e9d992dcd94adc73a2591c3b4690a66a6b1").done());
}

static void usage(FILE* f)
{
    fprintf(f,
            "trunkfmt %s - LoJack LJ1200, Tait 1200 and Motorola Connect Plus decoders (port of parts of sdrtrunk)\n"
            "usage:\n"
            "  trunkfmt --foxsdr-helper 1 --format s16|f32 --rate <Hz> [--decoders lj1200,tait,fleetsync2,mpt1327] [--invert] [--no-unchecked]\n"
            "      stdin: mono FM-demodulated audio (discriminator output), 8000 Hz recommended (4000..192000 accepted)\n"
            "  trunkfmt --foxsdr-helper 1 --format csbk-hex\n"
            "      stdin: text lines 'csbk <24 hex digits>' (12-byte CSBK with CRC) or 20 digits (no CRC),\n"
            "             'slc <9 or 11 hex digits>' (Short LC, 36 bits, optionally + CRC-8)\n"
            "  trunkfmt --foxsdr-helper-info\n"
            "stdout: JSON Lines.  Licence: GPL-3.0-or-later.  Source: https://github.com/DSheirer/sdrtrunk (original)\n",
            VERSION);
}

struct Options
{
    std::string format;
    double rate = 0;
    bool haveRate = false;
    bool lj = true, tait = true, fleetsync = true, mpt = true;
    bool mptFrench = false;
    bool ltr = false, ltrnet = false, passport = false;     // sub-audible 300 baud family: only on request
    bool invert = false;
    bool noUnchecked = false;
};

static int runAudio(const Options& o)
{
    const bool s16 = (o.format == "s16");
    long long consumed = 0;                    // input samples consumed so far (timestamps come from this)
    unsigned long long symbols = 0, ljTower = 0, ljTransponder = 0, taitAni = 0, taitGps = 0, suppressed = 0, fsMessages = 0, mptMessages = 0, ltrMessages = 0, ltrnetMessages = 0, passportMessages = 0;

    auto stamp = [&](Json& j) {
        double t = (double)consumed / o.rate;
        j.fix("t", t, 3);
    };

    LJ1200Decoder lj(
        [&](const LJ1200Message& m) {
            ljTower++;
            Json j;
            stamp(j);
            j.str("type", "lj1200").str("crc", m.crc == CrcResult::PASSED ? "pass" : "corrected");
            j.str("function", m.function).num("function_code", m.functionCode);
            j.str("address", m.address).str("reply_code", m.replyCode);
            if(m.function == "1Y-SITE ID") { j.str("site", m.replyCode); }
            j.str("vrc", m.vrc).str("lrc", m.lrc).str("crc_field", m.crcField).str("msg", m.rawHex);
            emitLine(j.done());
        },
        [&](const LJ1200TransponderMessage& m) {
            if(o.noUnchecked) { suppressed++; return; }
            ljTransponder++;
            Json j;
            stamp(j);
            j.str("type", "lj1200_transponder").str("crc", "unchecked").str("crc_field", m.crcField).str("msg", m.rawHex);
            emitLine(j.done());
        });

    Tait1200Decoder tait(
        [&](const Tait1200GPSMessage& m) {
            if(o.noUnchecked) { suppressed++; return; }
            taitGps++;
            Json j;
            stamp(j);
            j.str("type", "tait_gps").str("crc", "unchecked").str("from", m.from).str("to", m.to);
            j.fix("lat", m.latitude, 6).fix("lon", m.longitude, 6).str("gps_time", m.time).num("day", m.day);
            j.num("field_388_391", m.field388).fix("speed_kph", m.speedKph, 1);
            j.num("size", m.size1).num("size2", m.size2).str("msg", m.rawHex);
            emitLine(j.done());
        },
        [&](const Tait1200ANIMessage& m) {
            if(o.noUnchecked) { suppressed++; return; }
            taitAni++;
            Json j;
            stamp(j);
            j.str("type", "tait_ani").str("crc", "unchecked").str("from", m.from).str("to", m.to);
            j.num("size", m.size1).num("size2", m.size2).str("msg", m.rawHex);
            emitLine(j.done());
        });

    Fleetsync2Decoder fleetsync([&](const Fleetsync2Message& m) {
        fsMessages++;
        Json j;
        stamp(j);
        const char* type = m.kind == Fleetsync2Message::GPS ? "fleetsync_gps" : m.kind == Fleetsync2Message::STATUS ? "fleetsync_status" : "fleetsync";
        j.str("type", type).str("crc", m.crc).str("kind", m.kindLabel).str("from", m.from);
        if(m.kind == Fleetsync2Message::GPS || m.kind == Fleetsync2Message::STATUS) { j.str("to", m.to); }
        if(m.kind == Fleetsync2Message::STATUS) { j.num("status", m.status); }
        if(m.kind == Fleetsync2Message::GPS)
        {
            j.fix("lat", m.latitude, 6).fix("lon", m.longitude, 6);
            if(m.hasHeading) { j.fix("heading", m.heading, 1); } else { j.null("heading"); }
            j.fix("speed", m.speed, 2);
            char tb[32];
            snprintf(tb, sizeof tb, "%02d:%02d:%02d", m.gpsHour, m.gpsMinute, m.gpsSecond);
            j.str("gps_time", tb).num("gps_year_field", m.gpsYear).num("gps_month", m.gpsMonth).num("gps_day", m.gpsDay);
        }
        j.boolean("emergency", m.emergency).boolean("lone_worker", m.loneWorker).boolean("paging", m.paging);
        j.boolean("acknowledge", m.acknowledge).boolean("ani", m.ani);
        j.str("msg", m.rawHex);
        emitLine(j.done());
    });

    MPT1327Decoder mpt1327(o.mptFrench, [&](const MPT1327Message& m, bool control) {
        mptMessages++;
        Json j;
        stamp(j);
        j.str("type", "mpt1327").str("sync", control ? "control" : "traffic").str("message", m.typeCode);
        j.str("crc", m.parity).str("description", m.typeText);
        j.num("prefix", m.prefix).num("ident1", m.ident1).num("ident2", m.ident2).num("channel", m.channel);
        j.str("from", m.fromId).str("to", m.toId);
        if(!m.siteId.empty()) { j.str("system", m.siteId); }
        j.str("text", m.text).str("msg", m.rawHex);
        emitLine(j.done());
    });

    LTRStandardDecoder ltrStd([&](const LTRStandardDecoder::Message& m) {
        ltrMessages++;
        Json j;
        stamp(j);
        j.str("type", "ltr").str("direction", m.direction == Direction::OSW ? "osw" : "isw").str("message", m.type);
        j.str("crc", m.inverted ? "pass-inverted" : "pass").num("area", m.area).num("lcn", m.channel).num("home", m.home);
        j.num("group", m.group).num("free", m.free).str("talkgroup", m.talkgroup).str("msg", m.rawHex);
        emitLine(j.done());
    });
    LTRNetDecoder ltrNet([&](const LTRNetDecoder::Message& m) {
        ltrnetMessages++;
        Json j;
        stamp(j);
        std::string body = m.json.substr(1);              // the Json object text without its opening brace
        std::string head = j.done();                      // {"t":...}
        head.pop_back();
        emitLine(head + "," + body);
    });
    PassportDecoder passportDec([&](const PassportDecoder::Message& m) {
        passportMessages++;
        Json j;
        stamp(j);
        j.str("type", "passport").str("message", m.type).num("dcc", m.colorCode).num("site", m.site).num("lcn", m.lcn);
        j.num("free", m.free).num("type_number", m.typeNumber);
        if(m.hasTo) { j.num("group", m.group); }
        if(m.hasFrom) { j.num("radio_id", m.radioId); }
        j.num("lcn_freq_hz", m.lcnFrequency).num("free_freq_hz", m.freeFrequency).str("site_band", m.siteBand);
        if(m.type == "SY_IDLE") { j.str("neighbor_band", m.neighborBand).num("neighbor_freq_hz", m.neighborFrequency); }
        j.boolean("corrected", m.correctedBits).str("text", m.text).str("msg", m.rawHex);
        emitLine(j.done());
    });
    const bool wantAfsk = o.lj || o.tait || o.fleetsync || o.mpt;
    const bool wantSubAudible = o.ltr || o.ltrnet || o.passport;
    LTRDecoder ltrSlicer(o.rate, [&](bool sym) {
        if(o.ltr) { ltrStd.process(sym); }
        if(o.ltrnet) { ltrNet.process(sym); }
        if(o.passport) { passportDec.process(sym); }
    });

    AFSK1200Decoder afsk(o.rate, o.invert, [&](bool sym) {
        symbols++;
        if(o.lj) { lj.process(sym); }
        if(o.tait) { tait.process(sym); }
        if(o.fleetsync) { fleetsync.process(sym); }
        if(o.mpt) { mpt1327.process(sym); }
    });

    std::vector<unsigned char> buf(65536);
    size_t carry = 0;
    const size_t width = s16 ? 2 : 4;
    for(;;)
    {
        size_t n = fread(buf.data() + carry, 1, buf.size() - carry, stdin);
        if(n == 0) { break; }
        n += carry;
        size_t whole = n / width;
        for(size_t i = 0; i < whole; i++)
        {
            const unsigned char* p = buf.data() + i * width;
            float x;
            if(s16)
            {
                int16_t v = (int16_t)(p[0] | (p[1] << 8));
                x = (float)v / 32768.0f;
            }
            else
            {
                uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
                memcpy(&x, &u, 4);
                if(!(x == x) || x > 1e6f || x < -1e6f) { x = 0.0f; }     // NaN / absurd values: treat as silence
            }
            consumed++;
            if(wantAfsk) { afsk.push(x); }
            if(wantSubAudible) { ltrSlicer.push(x); }
        }
        carry = n - whole * width;
        if(carry) { memmove(buf.data(), buf.data() + whole * width, carry); }
    }
    // flush: a short run of silence pushes the last symbols through the resampler and symbol-timing delay lines
    long long flush = (long long)(0.03 * o.rate) + 16;
    for(long long i = 0; i < flush; i++)
    {
        if(wantAfsk) { afsk.push(0.0f); }
        if(wantSubAudible) { ltrSlicer.push(0.0f); }
    }

    Json st;
    stamp(st);
    st.str("type", "stats").num("samples", consumed).num("symbols", (long long)symbols);
    st.num("lj1200", (long long)ljTower).num("lj1200_failed_crc", (long long)lj.failedCrc);
    st.num("lj1200_transponder", (long long)ljTransponder).num("tait_ani", (long long)taitAni);
    st.num("tait_gps", (long long)taitGps).num("suppressed_unchecked", (long long)suppressed);
    st.num("fleetsync2", (long long)fsMessages).num("fleetsync2_invalid", (long long)fleetsync.invalid);
    st.num("mpt1327", (long long)mptMessages).num("mpt1327_invalid", (long long)mpt1327.invalid);
    st.num("ltr", (long long)ltrMessages).num("ltrnet", (long long)ltrnetMessages).num("ltrnet_failed_crc", (long long)ltrNet.failedCrc);
    st.num("passport", (long long)passportMessages).num("passport_failed_crc", (long long)passportDec.failedCrc);
    emitLine(st.done());
    return 0;
}

static int runCsbkHex()
{
    const long long MAX_ERROR_LINES = 20;
    long long lineNo = 0, decoded = 0, notDecoded = 0, errors = 0;
    std::string line;
    int c;
    auto handle = [&]() {
        lineNo++;
        // skip blank lines and comment-only lines
        bool blank = true;
        for(char ch : line)
        {
            if(ch == '#') { break; }
            if(!isspace((unsigned char)ch)) { blank = false; break; }
        }
        if(blank) { return; }
        std::string out = decodeConnectPlusLine(line, lineNo);
        if(out.find("\"type\":\"parse_error\"") != std::string::npos)
        {
            errors++;
            if(errors > MAX_ERROR_LINES) { return; }      // bound the output on garbage input; the stats line has the count
        }
        else if(out.find("\"decoded\":true") != std::string::npos) { decoded++; }
        else { notDecoded++; }
        emitLine(out);
    };
    while((c = fgetc(stdin)) != EOF)
    {
        if(c == '\n')
        {
            handle();
            line.clear();
        }
        else if(line.size() < 4096)
        {
            line.push_back((char)c);
        }
        else if(line.size() == 4096)
        {
            line.push_back('?');       // over-long line: it will fail to parse
        }
    }
    if(!line.empty()) { handle(); }
    emitLine(Json().num("t", 0).str("type", "stats").num("lines", lineNo).num("decoded", decoded)
                 .num("not_decoded", notDecoded).num("errors", errors)
                 .num("errors_not_printed", errors > MAX_ERROR_LINES ? errors - MAX_ERROR_LINES : 0).done());
    return 0;
}

}  // namespace trunkfmt

int main(int argc, char** argv)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    using namespace trunkfmt;
    Options o;
    bool helperMode = false, info = false;
    for(int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto need = [&](const char* name) -> const char* {
            if(i + 1 >= argc) { fprintf(stderr, "trunkfmt: %s needs a value\n", name); exit(2); }
            return argv[++i];
        };
        if(a == "--foxsdr-helper")
        {
            std::string v = need("--foxsdr-helper");
            if(v != "1") { fprintf(stderr, "trunkfmt: unsupported protocol version '%s' (this helper speaks 1)\n", v.c_str()); return 2; }
            helperMode = true;
        }
        else if(a == "--foxsdr-helper-info") { info = true; }
        else if(a == "--format") { o.format = need("--format"); }
        else if(a == "--rate") { o.rate = atof(need("--rate")); o.haveRate = true; }
        else if(a == "--centre" || a == "--center") { need("--centre"); }     // accepted, unused (audio helper)
        else if(a == "--invert") { o.invert = true; }
        else if(a == "--no-unchecked") { o.noUnchecked = true; }
        else if(a == "--mpt-french") { o.mptFrench = true; }
        else if(a == "--decoders")
        {
            std::string v = need("--decoders");
            o.lj = o.tait = o.fleetsync = o.mpt = false;
            o.ltr = o.ltrnet = o.passport = false;
            size_t pos = 0;
            while(pos <= v.size())
            {
                size_t e = v.find(',', pos);
                if(e == std::string::npos) { e = v.size(); }
                std::string t = v.substr(pos, e - pos);
                if(t == "lj1200") { o.lj = true; }
                else if(t == "tait") { o.tait = true; }
                else if(t == "fleetsync2") { o.fleetsync = true; }
                else if(t == "mpt1327") { o.mpt = true; }
                else if(t == "ltr") { o.ltr = true; }
                else if(t == "ltrnet") { o.ltrnet = true; }
                else if(t == "passport") { o.passport = true; }
                else if(!t.empty()) { fprintf(stderr, "trunkfmt: unknown decoder '%s' (lj1200, tait, fleetsync2, mpt1327, ltr, ltrnet, passport)\n", t.c_str()); return 2; }
                pos = e + 1;
            }
        }
        else if(a == "--help" || a == "-h") { usage(stdout); return 0; }
        else if(a == "--version") { printf("trunkfmt %s\n", VERSION); return 0; }
        else
        {
            fprintf(stderr, "trunkfmt: unknown option '%s'\n", a.c_str());
            usage(stderr);
            return 2;
        }
    }

    if(info)
    {
        fputs(MANIFEST_JSON, stdout);
        fflush(stdout);
        return 0;
    }
    if(!helperMode)
    {
        usage(stderr);
        return 2;
    }

    if(o.format == "csbk-hex")
    {
        hello();
        return runCsbkHex();
    }
    if(o.format != "s16" && o.format != "f32")
    {
        fprintf(stderr, "trunkfmt: unsupported --format '%s' (use s16, f32 or csbk-hex)\n", o.format.c_str());
        return 2;
    }
    if(!o.haveRate || !(o.rate >= 4000.0 && o.rate <= 192000.0))
    {
        fprintf(stderr, "trunkfmt: --rate <Hz> is required for audio input (4000..192000)\n");
        return 2;
    }
    hello();
    return runAudio(o);
}
