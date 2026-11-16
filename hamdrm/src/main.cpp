// hamdrm - GPL helper: HamDRM (digital SSTV) receive modem and file reassembly (pipe protocol version 1).
// Based on QSSTV (https://github.com/ON4QZ/QSSTV, GPL); see UPSTREAM.md and COPYING. This program is based in whole or in part on QSSTV.
//
//   hamdrm --foxsdr-helper 1 --format s16|f32 --rate 12000 [--out-dir DIR] < audio.raw
//   hamdrm --foxsdr-helper-info
//
// Mono audio at 12000 Hz on stdin until end of file; JSON Lines on stdout. No picture bytes on stdout.
#ifdef _WIN32
#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "drm.h"
#include "drmrx.h"
#include "sourcedecoder.h"

static const char *kVersion = "1.0.0";
static const char *kSource = "https://github.com/ON4QZ/QSSTV";
static const char *kUpstreamCommit = "31c323ccb848bade1dbab0a96890073f7b9455ea";

static const char *kManifest =
    "{\"name\":\"hamdrm\",\"version\":\"1.0.0\",\"licence\":\"GPL-3.0-or-later\",\"source\":\"https://github.com/ON4QZ/QSSTV\","
    "\"executable\":\"hamdrm.exe\",\"input\":\"audio\",\"format\":\"s16\",\"formats\":[\"s16\",\"f32\"],\"rate\":12000,"
    "\"events\":{\"sync\":[\"state\",\"mode\",\"snr_db\"],\"mode\":[\"mode\",\"callsign\",\"snr_db\"],\"header\":[\"transport_id\",\"name\",\"size\",\"mode\"],"
    "\"segment\":[\"transport_id\",\"name\",\"segment\",\"received\",\"total\",\"percent\",\"snr_db\"],"
    "\"file\":[\"transport_id\",\"name\",\"size\",\"sha256\",\"saved\",\"path\",\"fec\",\"note\",\"mode\",\"callsign\",\"snr_db\"],"
    "\"stats\":[\"seconds\",\"locked\",\"snr_db\",\"packets\",\"crc_failures\",\"dropped\",\"files\"],\"status\":[\"text\"]}}";

static void out_line(const std::string &s)
{
    fwrite(s.data(), 1, s.size(), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

// JSON string of bytes that came over the air: Latin-1 as QSSTV reads it, written as UTF-8
static std::string jstr(const std::string &in)
{
    std::string o = "\"";
    for (unsigned char c : in)
    {
        switch (c)
        {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20)
            {
                char b[8];
                snprintf(b, sizeof b, "\\u%04x", c);
                o += b;
            }
            else if (c < 0x80)
                o += (char)c;
            else
            {
                o += (char)(0xC0 | (c >> 6));
                o += (char)(0x80 | (c & 0x3F));
            }
        }
    }
    return o + "\"";
}

static std::string fnum(double v, const char *fmt)
{
    char b[64];
    snprintf(b, sizeof b, fmt, v);
    return b;
}

static std::string tfield(double t) { return "{\"t\":" + fnum(t, "%.3f") + ","; }

static const char *modeLetter(int m)
{
    switch (m)
    {
    case 0: return "A";
    case 1: return "B";
    case 2: return "E"; // QSSTV's name for the third robustness mode (compactModeToString)
    default: return "-";
    }
}

static void usage_error(const char *msg)
{
    fprintf(stderr, "hamdrm: %s\n", msg);
    fprintf(stderr, "usage: hamdrm --foxsdr-helper 1 --format s16|f32 --rate 12000 [--out-dir DIR]\n       hamdrm --foxsdr-helper-info\n");
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    int protocol = 0;
    std::string format, outDir;
    long rate = 0;
    bool info = false;
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
            if (strcmp(need("--foxsdr-helper"), "1") != 0)
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
            rate = atol(need("--rate"));
        else if (a == "--out-dir")
            outDir = need("--out-dir");
        else if (a == "--version")
        {
            printf("hamdrm %s, based in whole or in part on QSSTV (ON4QZ), commit %s\n", kVersion, kUpstreamCommit);
            return 0;
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
    if (format != "s16" && format != "f32")
    {
        usage_error("unsupported --format (use s16 or f32: mono audio)");
        return 2;
    }
    if (rate != 12000)
    {
        usage_error("--rate must be 12000 (the receiver of QSSTV works on audio at 12000 Hz)");
        return 2;
    }
    if (!outDir.empty())
    {
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(outDir.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
        {
            usage_error("--out-dir is not an existing folder");
            return 2;
        }
#endif
    }

    out_line(std::string("{\"helper\":\"hamdrm\",\"version\":\"") + kVersion + "\",\"protocol\":1,\"licence\":\"GPL-3.0-or-later\",\"source\":\"" + kSource +
             "\",\"format\":\"" + format + "\",\"rate\":12000,\"upstream\":\"QSSTV " + kUpstreamCommit + "\"}");

    drmRx rx;
    rx.init();
    long long samples = 0;
    double now = 0;
    unsigned long files = 0, syncs = 0;
    std::string lastModeStr;
    bool wasTime = false, wasFreq = false, wasFrame = false;
    double lastStats = 0;

    srcDecoder->setOutDir(outDir);
    srcDecoder->setSink([&](const RxEvent &ev) {
        std::string s = tfield(now);
        switch (ev.kind)
        {
        case RxEvent::Header:
            s += "\"type\":\"header\",\"transport_id\":" + std::to_string(ev.transportId) + ",\"name\":" + jstr(ev.name) + ",\"size\":" + std::to_string(ev.bodySize) +
                 ",\"mode\":" + jstr(ev.mode) + ",\"snr_db\":" + fnum(ev.snr_db, "%.1f") + "}";
            break;
        case RxEvent::Segment:
        {
            int pct = ev.total > 0 ? (ev.received * 100) / ev.total : 0;
            s += "\"type\":\"segment\",\"transport_id\":" + std::to_string(ev.transportId) + ",\"name\":" + jstr(ev.name) + ",\"segment\":" + std::to_string(ev.segment) +
                 ",\"received\":" + std::to_string(ev.received) + ",\"total\":" + std::to_string(ev.total) + ",\"last_seen\":" + (ev.lastKnown ? "true" : "false") +
                 ",\"percent\":" + std::to_string(pct) + ",\"snr_db\":" + fnum(ev.snr_db, "%.1f") + "}";
            break;
        }
        case RxEvent::File:
            files++;
            s += "\"type\":\"file\",\"transport_id\":" + std::to_string(ev.transportId) + ",\"name\":" + jstr(ev.name) + ",\"size\":" + std::to_string(ev.size) +
                 ",\"sha256\":\"" + ev.sha256 + "\",\"saved\":" + (ev.savedName.empty() ? std::string("null") : jstr(ev.savedName)) +
                 ",\"path\":" + (ev.savedPath.empty() ? std::string("null") : jstr(ev.savedPath)) + ",\"fec\":" + (ev.fec.empty() ? std::string("null") : jstr(ev.fec)) +
                 ",\"segments\":" + std::to_string(ev.total) + ",\"mode\":" + jstr(ev.mode) + ",\"callsign\":" + jstr(ev.callsign) + ",\"note\":" +
                 (ev.note.empty() ? std::string("null") : jstr(ev.note)) + ",\"snr_db\":" + fnum(ev.snr_db, "%.1f") + "}";
            break;
        case RxEvent::Skipped:
            s += "\"type\":\"status\",\"text\":" + jstr("transport " + std::to_string(ev.transportId) + " (" + ev.name + ") not saved: " + ev.note) + "}";
            break;
        }
        out_line(s);
    });

    auto emit_sync = [&](const char *state) {
        syncs++;
        std::string s = tfield(now) + "\"type\":\"sync\",\"state\":\"" + state + "\"";
        if (strcmp(state, "lost") != 0)
            s += std::string(",\"mode\":\"") + modeLetter(robustness_mode) + "\"";
        s += ",\"snr_db\":" + fnum(avgSNR, "%.1f") + "}";
        out_line(s);
    };

    std::vector<double> stripe(RXSTRIPE);
    size_t fill = 0;
    const size_t bytesPer = format == "s16" ? 2 : 4;
    std::vector<unsigned char> raw(65536 - (65536 % bytesPer));
    std::vector<unsigned char> carry;
    size_t n;
    auto process_stripe = [&]() {
        rx.run(stripe.data());
        samples += RXSTRIPE;
        now = (double)samples / 12000.0;
        const demodulator *d = rx.demod();
        bool t = d->isTimeSync(), f = d->isFrequencySync(), fr = d->isFrameSync();
        if (t && !wasTime)
            emit_sync("time");
        if (f && !wasFreq)
            emit_sync("frequency");
        if (fr && !wasFrame)
            emit_sync("frame");
        if (wasTime && !t)
        {
            emit_sync("lost");
            lastModeStr.clear();
        }
        wasTime = t;
        wasFreq = f;
        wasFrame = fr;
        // FAC decoded: report robustness mode, spectrum occupancy, protection, QAM, interleaver and the callsign when they change
        if (fr && fac_valid == 1 && spectrum_occupancy_new >= 0 && spectrum_occupancy_new <= 1)
        {
            int mCode = 1;
            if (msc_mode_new == 3)
                mCode = 0;
            if (msc_mode_new == 0)
                mCode = 2;
            int protection = multiplex_description.PL_PartB == 1 ? 1 : 0;
            unsigned code = (unsigned)(robustness_mode * 10000 + spectrum_occupancy_new * 1000 + protection * 100 + mCode * 10 + interleaver_depth_new);
            std::string ms = compactModeToString(code) + (callsignValid ? "|" + drmCallsign : std::string());
            if (ms != lastModeStr)
            {
                lastModeStr = ms;
                out_line(tfield(now) + "\"type\":\"mode\",\"mode\":" + jstr(compactModeToString(code)) + ",\"callsign\":" + (callsignValid ? jstr(drmCallsign) : std::string("null")) +
                         ",\"snr_db\":" + fnum(avgSNR, "%.1f") + "}");
            }
        }
        if (now - lastStats >= 10.0)
        {
            lastStats = now;
            out_line(tfield(now) + "\"type\":\"stats\",\"seconds\":" + fnum(now, "%.1f") + ",\"locked\":" + (fr ? "true" : "false") + ",\"snr_db\":" + fnum(avgSNR, "%.1f") +
                     ",\"packets\":" + std::to_string(srcDecoder->statPackets) + ",\"crc_failures\":" + std::to_string(srcDecoder->statCrcFail) + ",\"dropped\":" +
                     std::to_string(srcDecoder->statDropped) + ",\"files\":" + std::to_string(files) + "}");
        }
    };
    while ((n = fread(raw.data(), 1, raw.size(), stdin)) > 0)
    {
        carry.insert(carry.end(), raw.begin(), raw.begin() + n);
        size_t whole = carry.size() / bytesPer;
        for (size_t i = 0; i < whole; i++)
        {
            double v;
            if (bytesPer == 2)
                v = (double)(int16_t)(carry[2 * i] | (carry[2 * i + 1] << 8));
            else
            {
                uint32_t u = (uint32_t)carry[4 * i] | ((uint32_t)carry[4 * i + 1] << 8) | ((uint32_t)carry[4 * i + 2] << 16) | ((uint32_t)carry[4 * i + 3] << 24);
                float fv;
                memcpy(&fv, &u, 4);
                if (!std::isfinite(fv))
                    fv = 0;
                v = (double)fv * 32767.0;
            }
            stripe[fill++] = v;
            if (fill == RXSTRIPE)
            {
                process_stripe();
                fill = 0;
            }
        }
        carry.erase(carry.begin(), carry.begin() + whole * bytesPer);
    }
    // a partial last stripe is dropped (upstream works on whole stripes only)
    if (samples == 0)
        return 0; // no audio: the hello line only
    out_line(tfield(now) + "\"type\":\"stats\",\"seconds\":" + fnum(now, "%.1f") + ",\"locked\":" + (rx.demod()->isFrameSync() ? "true" : "false") + ",\"snr_db\":" + fnum(avgSNR, "%.1f") +
             ",\"packets\":" + std::to_string(srcDecoder->statPackets) + ",\"crc_failures\":" + std::to_string(srcDecoder->statCrcFail) + ",\"dropped\":" +
             std::to_string(srcDecoder->statDropped) + ",\"files\":" + std::to_string(files) + ",\"final\":true}");
    (void)syncs;
    return 0;
}
