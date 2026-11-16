// ----------------------------------------------------------------------------
// helper_main.cpp -- FoxSDR fldigimodes helper: command line, input loop and JSON Lines output.
//
// Written for this helper (GPL-3.0-only). The decoders it drives come from fldigi; see UPSTREAM.md.
//
//   fldigimodes --foxsdr-helper 1 --format s16 --rate 8000 --mode olivia-8-500 [--freq 1500] < audio.raw
// ----------------------------------------------------------------------------
#include <algorithm>
#include <cmath>
#include <cstdint>
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

#include "decoder.h"
#include "fm_shim.h"
#include "helper_manifest.h"
#include "rsid_detector.h"

#define HELPER_NAME "fldigimodes"
#define HELPER_VERSION "0.1.0"
#define HELPER_LICENCE "GPL-3.0-only"
#define HELPER_SOURCE "https://github.com/w1hkj/fldigi"
#ifndef HELPER_UPSTREAM
#define HELPER_UPSTREAM "fldigi"
#endif

namespace {

const int BLOCK = 512;          // samples per internal block: fixed, so output does not depend on the read size
const size_t MAX_PENDING = 80;  // characters of text in one event
const double MAX_HOLD_S = 2.0;  // seconds a character may wait for company

FILE *jout = nullptr;
unsigned long long g_samples = 0;
int g_rate = 8000;

// ---- JSON writing -------------------------------------------------------------------------------------------------
std::string jesc(const std::string &s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                char b[8];
                snprintf(b, sizeof b, "\\u%04x", c);
                o += b;
            } else
                o += (char)c;
        }
    }
    return o;
}

std::string num(double v, int dec)
{
    char b[48];
    if (!std::isfinite(v))
        return "null";
    snprintf(b, sizeof b, "%.*f", dec, v);
    return b;
}

void out_line(const std::string &s)
{
    if (fwrite(s.data(), 1, s.size(), jout) != s.size() || fputc('\n', jout) == EOF || fflush(jout) != 0) {
        fprintf(stderr, "fldigimodes: cannot write to standard output; stopping\n");
        exit(3);
    }
}

double now_s() { return (double)g_samples / g_rate; }

// A character code 0..255 becomes UTF-8; codes above 127 are read as ISO-8859-1.
void append_utf8(std::string &o, int c)
{
    if (c < 0x80)
        o += (char)c;
    else {
        o += (char)(0xC0 | (c >> 6));
        o += (char)(0x80 | (c & 0x3F));
    }
}

// ---- text events ---------------------------------------------------------------------------------------------------
std::unique_ptr<fm::Decoder> g_dec;
std::string g_mode_name;
std::string g_pending;
double g_pending_since = 0;
unsigned long long g_chars = 0, g_events = 0, g_rsid_hits = 0, g_pictures = 0;

void emit_text()
{
    if (g_pending.empty() || !g_dec)
        return;
    fm::Quality q = g_dec->quality();
    std::string l = "{\"t\":" + num(now_s(), 3) + ",\"type\":\"text\",\"text\":\"" + jesc(g_pending) + "\",\"mode\":\"" + g_mode_name + "\"";
    if (std::isfinite(q.freq_hz))
        l += ",\"freq\":" + num(q.freq_hz, 1);
    if (std::isfinite(q.offset_hz))
        l += ",\"offset\":" + num(q.offset_hz, 2);
    if (std::isfinite(q.snr_db))
        l += ",\"snr\":" + num(q.snr_db, 1);
    if (std::isfinite(q.sync))
        l += ",\"sync\":" + num(q.sync, 2);
    if (std::isfinite(q.metric))
        l += ",\"metric\":" + num(q.metric, 0);
    l += "}";
    out_line(l);
    g_events++;
    g_pending.clear();
}

void on_char(int c)
{
    if (c <= 0 || c > 255)
        return;
    // control codes other than backspace, tab, line feed and carriage return are noise
    if (c < 32 && c != 8 && c != 9 && c != 10 && c != 13)
        return;
    if (g_pending.empty())
        g_pending_since = now_s();
    append_utf8(g_pending, c);
    g_chars++;
    if (c == '\n' || g_pending.size() >= MAX_PENDING)
        emit_text();
}

}  // namespace

void fm_picture_skipped(int width, int height, bool colour)
{
    emit_text();
    g_pictures++;
    out_line("{\"t\":" + num(now_s(), 3) + ",\"type\":\"status\",\"text\":\"" + g_mode_name + ": a " + std::to_string(width) + "x" +
             std::to_string(height) + (colour ? " colour" : " grey") + " picture is being sent; the pixels are skipped\"}");
}

namespace {

void usage(FILE *f)
{
    fprintf(f,
            "fldigimodes " HELPER_VERSION " - fldigi's Olivia, Contestia, MFSK, MT63 and THOR receivers, and RSID\n"
            "usage: fldigimodes --foxsdr-helper 1 --format s16|f32 --rate <Hz> --mode <mode> [options] < audio\n"
            "       fldigimodes --foxsdr-helper-info\n"
            "modes: olivia-<tones>-<bw>  contestia-<tones>-<bw>  (tones 2..256, bw 125|250|500|1000|2000)\n"
            "       mfsk4 mfsk8 mfsk11 mfsk16 mfsk22 mfsk31 mfsk32 mfsk64 mfsk128 mfsk64l mfsk128l\n"
            "       mt63-500s mt63-500l mt63-1000s mt63-1000l mt63-2000s mt63-2000l\n"
            "       thormicro thor4 thor5 thor8 thor11 thor16 thor22 thor25x4 thor50x1 thor50x2 thor100\n"
            "       rsid   (only the RSID detector)\n"
            "options: --freq <Hz>        audio centre frequency of the signal (default 1500)\n"
            "         --squelch <0..100>|off   fldigi's squelch slider (default 5, on)\n"
            "         --reverse          sideband reversed\n"
            "         --tones <n> --bandwidth <Hz>   with --mode olivia or contestia\n"
            "         --sync-margin <n> --sync-integ <n>   Olivia/Contestia sync search (default 8 and 4)\n"
            "         --mt63-long-integral   MT63 long receive integration\n"
            "         --rsid on|off      run the RSID detector alongside the mode (default on)\n"
            "         --rsid-search wide|narrow   RSID: the whole audio band (default) or +-200 Hz round --freq\n"
            "         --rate 8000        (11025 for mfsk11, mfsk22, thor5, thor11, thor22)\n");
}

int fail(const std::string &msg)
{
    fprintf(stderr, "fldigimodes: %s\n", msg.c_str());
    return 2;
}

}  // namespace

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    jout = stdout;

    int proto = 0;
    std::string format, modetext;
    int rate = 0;
    bool info = false, rsid_on = true, rsid_wide = true;
    fm::Options opt;
    bool have_rate = false;
    int tones = 0, bw = 0;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto need = [&](const char *what) -> const char * {
            if (i + 1 >= argc) {
                fprintf(stderr, "fldigimodes: %s needs %s\n", a.c_str(), what);
                exit(2);
            }
            return argv[++i];
        };
        if (a == "--foxsdr-helper") {
            proto = atoi(need("a protocol number"));
            if (proto != 1)
                return fail("unsupported protocol " + std::to_string(proto) + " (this helper speaks protocol 1)");
        } else if (a == "--foxsdr-helper-info")
            info = true;
        else if (a == "--format")
            format = need("s16 or f32");
        else if (a == "--rate") {
            rate = atoi(need("a sample rate"));
            have_rate = true;
        } else if (a == "--centre" || a == "--center")
            need("a frequency");  // accepted and ignored: audio input has no RF centre
        else if (a == "--mode")
            modetext = need("a mode");
        else if (a == "--freq")
            opt.freq = atof(need("a frequency in Hz"));
        else if (a == "--squelch") {
            std::string v = need("a level or off");
            if (v == "off")
                opt.squelch_on = false;
            else {
                opt.squelch_on = true;
                opt.squelch = atof(v.c_str());
            }
        } else if (a == "--reverse")
            opt.reverse = true;
        else if (a == "--tones")
            tones = atoi(need("a tone count"));
        else if (a == "--bandwidth")
            bw = atoi(need("a bandwidth in Hz"));
        else if (a == "--sync-margin")
            opt.mode.smargin = atoi(need("a number"));
        else if (a == "--sync-integ")
            opt.mode.sinteg = atoi(need("a number"));
        else if (a == "--mt63-long-integral")
            opt.mode.mt63_long_integral = true;
        else if (a == "--rsid") {
            std::string v = need("on or off");
            if (v == "on")
                rsid_on = true;
            else if (v == "off")
                rsid_on = false;
            else
                return fail("--rsid takes on or off");
        } else if (a == "--rsid-search") {
            std::string v = need("wide or narrow");
            if (v == "wide")
                rsid_wide = true;
            else if (v == "narrow")
                rsid_wide = false;
            else
                return fail("--rsid-search takes wide or narrow");
        } else if (a == "--help" || a == "-h") {
            usage(stdout);
            return 0;
        } else
            return fail("unknown option " + a);
    }

    if (info) {
        fputs(HELPER_MANIFEST_JSON, stdout);
        fflush(stdout);
        return 0;
    }
    if (proto != 1)
        return fail("--foxsdr-helper 1 is required");
    if (format != "s16" && format != "f32")
        return fail("unsupported --format '" + format + "' (this helper takes mono audio: s16 or f32)");
    if (!have_rate)
        return fail("--rate is required");
    if (modetext.empty())
        return fail("--mode is required (see --help)");
    if (!(opt.freq >= 100 && opt.freq <= 3900))
        return fail("--freq must lie between 100 and 3900 Hz");
    if (opt.squelch < 0 || opt.squelch > 100)
        return fail("--squelch must lie between 0 and 100");

    bool rsid_only = (modetext == "rsid");
    std::string err;
    if (!rsid_only) {
        opt.mode.tones = tones;
        opt.mode.bandwidth = bw;
        if (!fm::parse_mode(modetext, opt.mode, err))
            return fail(err);
    }
    if (rsid_only) {
        if (rate < 4000 || rate > 48000)
            return fail("unsupported --rate " + std::to_string(rate) + " (RSID needs 4000..48000)");
    } else {
        g_dec = fm::make_decoder(opt, err);
        if (!g_dec)
            return fail(err);
        if (rate != g_dec->rate())
            return fail("unsupported --rate " + std::to_string(rate) + ": " + g_dec->name() + " runs at " + std::to_string(g_dec->rate()) + " Hz");
        g_mode_name = g_dec->name();
    }
    g_rate = rate;

    std::unique_ptr<fm::RsidDetector> rsid;
    if (rsid_on || rsid_only) {
        rsid.reset(new fm::RsidDetector(rate));
        rsid->set_search(rsid_wide, opt.freq, opt.reverse);
        rsid->set_callback([&](const fm::RsidHit &h) {
            emit_text();
            g_rsid_hits++;
            std::string l = "{\"t\":" + num(now_s(), 3) + ",\"type\":\"rsid\",\"name\":\"" + jesc(h.name) + "\",\"code\":" + std::to_string(h.code);
            if (!h.fldigi_mode.empty())
                l += ",\"mode\":\"" + jesc(h.fldigi_mode) + "\"";
            l += std::string(",\"known\":") + (h.known_to_fldigi ? "true" : "false");
            l += ",\"freq\":" + num(h.freq_hz, 1);
            l += std::string(",\"extended\":") + (h.extended ? "true" : "false");
            if (h.eot)
                l += ",\"eot\":true";
            l += "}";
            out_line(l);
        });
    }

    // hello
    {
        std::string h = std::string("{\"helper\":\"" HELPER_NAME "\",\"version\":\"" HELPER_VERSION "\",\"protocol\":1,\"licence\":\"" HELPER_LICENCE
                                    "\",\"source\":\"" HELPER_SOURCE "\",\"upstream\":\"" HELPER_UPSTREAM "\",\"format\":\"") +
                        format + "\",\"rate\":" + std::to_string(rate) + ",\"mode\":\"" + (rsid_only ? "rsid" : g_mode_name) + "\"";
        if (!rsid_only)
            h += ",\"freq\":" + num(opt.freq, 1);
        h += std::string(",\"rsid\":") + ((rsid_on || rsid_only) ? "true" : "false") + "}";
        out_line(h);
    }

    fm_rx_char_sink = on_char;

    const bool is_f32 = (format == "f32");
    const size_t bps = is_f32 ? 4 : 2;
    std::vector<unsigned char> inbuf(8192 * bps);
    std::vector<unsigned char> carry;  // a partial sample left from the last read
    std::vector<double> blk;
    blk.reserve(BLOCK);

    auto push_sample = [&](double v) {
        blk.push_back(v);
        if ((int)blk.size() == BLOCK) {
            if (g_dec)
                g_dec->process(blk.data(), BLOCK);
            if (rsid)
                rsid->process(blk.data(), BLOCK);
            g_samples += BLOCK;
            blk.clear();
            if (g_dec && !g_pending.empty() && now_s() - g_pending_since >= MAX_HOLD_S)
                emit_text();
        }
    };
    auto decode = [&](const unsigned char *p) -> double {
        if (is_f32) {
            float f;
            memcpy(&f, p, 4);
            double v = std::isfinite(f) ? (double)f : 0.0;
            return std::max(-1.0, std::min(1.0, v));
        }
        int16_t s = (int16_t)(p[0] | (p[1] << 8));
        return s / 32768.0;
    };

    for (;;) {
        size_t got = fread(inbuf.data(), 1, inbuf.size(), stdin);
        if (got == 0)
            break;
        size_t off = 0;
        while (!carry.empty() && carry.size() < bps && off < got)
            carry.push_back(inbuf[off++]);
        if (carry.size() == bps) {
            push_sample(decode(carry.data()));
            carry.clear();
        }
        while (off + bps <= got) {
            push_sample(decode(&inbuf[off]));
            off += bps;
        }
        for (; off < got; off++)
            carry.push_back(inbuf[off]);
    }
    bool cut = !carry.empty();
    if (!blk.empty()) {
        if (g_dec)
            g_dec->process(blk.data(), (int)blk.size());
        if (rsid)
            rsid->process(blk.data(), (int)blk.size());
        g_samples += blk.size();
        blk.clear();
    }
    if (g_dec) {
        g_dec->flush();
        emit_text();
    }
    if (cut)
        fprintf(stderr, "fldigimodes: input ended in the middle of a sample; the last bytes were dropped\n");

    out_line("{\"t\":" + num(now_s(), 3) + ",\"type\":\"stats\",\"samples\":" + std::to_string(g_samples) + ",\"chars\":" + std::to_string(g_chars) +
             ",\"text_events\":" + std::to_string(g_events) + ",\"rsid_hits\":" + std::to_string(g_rsid_hits) + ",\"pictures_skipped\":" +
             std::to_string(g_pictures) + "}");
    return 0;
}
