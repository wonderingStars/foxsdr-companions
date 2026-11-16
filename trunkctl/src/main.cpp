// trunkctl - FoxSDR GPL helper: trunking control channel decoder (Motorola SmartNet/SmartZone Type II, EDACS).
// Speaks the FoxSDR helper pipe protocol version 1: samples on stdin, JSON Lines on stdout.
//
// This file is part of trunkctl and is licensed under the GNU General Public License, version 3.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "common.h"
#include "edacs.h"
#include "emitter.h"
#include "helper_manifest.h"
#include "slicer.h"
#include "smartnet.h"

using namespace trunkctl;

#define HELPER_NAME "trunkctl"
#define HELPER_VERSION "0.1.0"
#define HELPER_LICENCE "GPL-3.0-only"
#define HELPER_SOURCE "https://github.com/TrunkRecorder/trunk-recorder"
#define HELPER_UPSTREAM "trunk-recorder 1aac86c (SmartNet, from OP25 rx_smartnet), edacs-fm d92422e (EDACS)"

enum Fmt { F_S16, F_F32, F_CS16, F_CF32 };

static void usage(FILE *f) {
    fprintf(f,
            "trunkctl " HELPER_VERSION " - SmartNet/SmartZone Type II and EDACS control channel decoder\n"
            "usage: trunkctl --foxsdr-helper 1 --format s16|f32|cs16|cf32 --rate <Hz> [options] < samples\n"
            "  input   s16/f32: FM discriminator audio (positive = higher frequency), mono; cs16/cf32: complex\n"
            "          baseband of one channel (FM-demodulated here). Rate at least 2x the baud rate of the\n"
            "          protocol: SmartNet 3600 Bd, EDACS 9600 Bd (19200 Hz or more). 28800 Hz suits both.\n"
            "  --protocol auto|smartnet|edacs      (default auto: every protocol the rate allows)\n"
            "  --polarity auto|normal|inverted     (default auto: found from the sync word)\n"
            "  --bandplan none|800_standard|800_reband|800_splinter|900|obt   SmartNet channel -> frequency\n"
            "  --obt-base MHz --obt-high MHz --obt-spacing MHz --obt-offset N   (with --bandplan obt)\n"
            "  --variant auto|standard|ea          EDACS addressing (default auto)\n"
            "  --esk                               EDACS: command byte XOR 0xA0\n"
            "  --afs A,F                           EDACS standard: agency,fleet bit lengths (default 4,4; subfleet gets 11-A-F)\n"
            "  --bch-correct 0|1|2                 EDACS: bit errors corrected per message (default 1)\n"
            "  --lcn N=MHz                         EDACS: frequency of logical channel N (repeatable)\n"
            "  --lcn-file FILE                     EDACS: lines 'N MHz' (or 'N,MHz'), '#' starts a comment\n"
            "  --update-interval S                 report the same call again after S seconds (default 2, 0 = always)\n"
            "  --stats-interval S                  stats line every S seconds of signal (default 10, 0 = only at the end)\n"
            "  --hide-ids                          radio IDs and talkgroups become per-run aliases (R1, T1 ...)\n"
            "  --raw                               also print every valid OSW / EDACS frame\n"
            "  --foxsdr-helper-info                print the manifest and exit\n");
}

static int fail(const char *msg) {
    fprintf(stderr, "trunkctl: %s\n", msg);
    return 2;
}

static bool parse_double(const char *s, double *out) {
    char *end = nullptr;
    double v = strtod(s, &end);
    if (end == s || *end != 0 || !std::isfinite(v))
        return false;
    *out = v;
    return true;
}

static bool add_lcn(EdacsOptions &o, const std::string &spec) {
    size_t p = spec.find_first_of("=, \t");
    if (p == std::string::npos)
        return false;
    int n;
    double f;
    char *end = nullptr;
    std::string a = spec.substr(0, p), b = spec.substr(p + 1);
    while (!b.empty() && (b[0] == ' ' || b[0] == '\t' || b[0] == '=' || b[0] == ',')) b.erase(0, 1);
    n = (int)strtol(a.c_str(), &end, 10);
    if (end == a.c_str() || *end != 0 || n < 1 || n > 25)
        return false;
    if (!parse_double(b.c_str(), &f) || f < 1.0 || f > 6000.0)
        return false;
    o.lcn_mhz[n] = f;
    return true;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY); /* the manifest printed by --foxsdr-helper-info and any --help text must not get CRLF from text mode */
#endif

    int helper_ver = 0;
    bool have_fmt = false, have_rate = false, info = false;
    Fmt fmt = F_S16;
    double rate = 0;
    std::string protocol = "auto";
    SmartnetOptions sn;
    EdacsOptions ed;
    bool hide = false;
    double stats_interval = 10.0;
    double update_interval = 2.0;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto need = [&](const char *name) -> const char * {
            if (i + 1 >= argc) {
                fprintf(stderr, "trunkctl: %s needs a value\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        const char *v;
        if (a == "--foxsdr-helper") {
            if (!(v = need("--foxsdr-helper"))) return 2;
            helper_ver = atoi(v);
        } else if (a == "--foxsdr-helper-info") {
            info = true;
        } else if (a == "--help" || a == "-h") {
            usage(stdout);
            return 0;
        } else if (a == "--version") {
            printf("trunkctl " HELPER_VERSION "\n");
            return 0;
        } else if (a == "--format") {
            if (!(v = need("--format"))) return 2;
            std::string f = v;
            if (f == "s16") fmt = F_S16;
            else if (f == "f32") fmt = F_F32;
            else if (f == "cs16") fmt = F_CS16;
            else if (f == "cf32") fmt = F_CF32;
            else {
                fprintf(stderr, "trunkctl: unsupported --format '%s' (s16, f32, cs16, cf32)\n", v);
                return 2;
            }
            have_fmt = true;
        } else if (a == "--rate") {
            if (!(v = need("--rate"))) return 2;
            if (!parse_double(v, &rate) || rate < 1000 || rate > 1e8) return fail("--rate must be a sample rate in Hz (1000 .. 1e8)");
            have_rate = true;
        } else if (a == "--centre" || a == "--center") {
            if (!(v = need("--centre"))) return 2; // accepted and ignored: audio-type input has no centre
        } else if (a == "--protocol") {
            if (!(v = need("--protocol"))) return 2;
            protocol = v;
            if (protocol != "auto" && protocol != "smartnet" && protocol != "edacs") return fail("--protocol must be auto, smartnet or edacs");
        } else if (a == "--polarity") {
            if (!(v = need("--polarity"))) return 2;
            std::string p = v;
            int pol = p == "auto" ? 0 : p == "normal" ? 1 : p == "inverted" ? 2 : -1;
            if (pol < 0) return fail("--polarity must be auto, normal or inverted");
            sn.polarity = ed.polarity = pol;
        } else if (a == "--bandplan") {
            if (!(v = need("--bandplan"))) return 2;
            sn.bandplan = v;
        } else if (a == "--obt-base" || a == "--obt-high" || a == "--obt-spacing") {
            if (!(v = need(a.c_str()))) return 2;
            double d;
            if (!parse_double(v, &d) || d <= 0) return fail("--obt-base/--obt-high/--obt-spacing need a positive number in MHz");
            (a == "--obt-base" ? sn.obt_base : a == "--obt-high" ? sn.obt_high : sn.obt_spacing) = d;
        } else if (a == "--obt-offset") {
            if (!(v = need("--obt-offset"))) return 2;
            sn.obt_offset = atoi(v);
        } else if (a == "--variant") {
            if (!(v = need("--variant"))) return 2;
            std::string s = v;
            ed.variant = s == "auto" ? 0 : s == "standard" ? 1 : s == "ea" ? 2 : -1;
            if (ed.variant < 0) return fail("--variant must be auto, standard or ea");
        } else if (a == "--esk") {
            ed.esk = true;
        } else if (a == "--afs") {
            if (!(v = need("--afs"))) return 2;
            int x, y;
            if (sscanf(v, "%d,%d", &x, &y) != 2 || x < 1 || y < 1 || x + y > 11) return fail("--afs needs A,F with 1 <= A, 1 <= F and A+F <= 11");
            ed.afs_a = x;
            ed.afs_f = y;
        } else if (a == "--bch-correct") {
            if (!(v = need("--bch-correct"))) return 2;
            ed.bch_correct = atoi(v);
            if (ed.bch_correct < 0 || ed.bch_correct > 2) return fail("--bch-correct must be 0, 1 or 2");
        } else if (a == "--lcn") {
            if (!(v = need("--lcn"))) return 2;
            if (!add_lcn(ed, v)) return fail("--lcn needs N=MHz with N in 1..25");
        } else if (a == "--lcn-file") {
            if (!(v = need("--lcn-file"))) return 2;
            std::ifstream f(v);
            if (!f) {
                fprintf(stderr, "trunkctl: cannot read --lcn-file '%s'\n", v);
                return 2;
            }
            std::string line;
            while (std::getline(f, line)) {
                size_t h = line.find('#');
                if (h != std::string::npos) line.erase(h);
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
                size_t s = line.find_first_not_of(" \t");
                if (s == std::string::npos) continue;
                if (!add_lcn(ed, line.substr(s))) {
                    fprintf(stderr, "trunkctl: bad line in --lcn-file: '%s'\n", line.c_str());
                    return 2;
                }
            }
        } else if (a == "--update-interval") {
            if (!(v = need("--update-interval"))) return 2;
            if (!parse_double(v, &update_interval) || update_interval < 0) return fail("--update-interval must be 0 or more seconds");
        } else if (a == "--stats-interval") {
            if (!(v = need("--stats-interval"))) return 2;
            if (!parse_double(v, &stats_interval) || stats_interval < 0) return fail("--stats-interval must be 0 or more seconds");
        } else if (a == "--hide-ids") {
            hide = true;
        } else if (a == "--raw") {
            sn.raw = ed.raw = true;
        } else {
            fprintf(stderr, "trunkctl: unknown option '%s' (see --help)\n", a.c_str());
            return 2;
        }
    }
    sn.update_interval = ed.update_interval = update_interval;

    if (info) {
        fputs(HELPER_MANIFEST_JSON, stdout);
        return 0;
    }
    if (helper_ver != 1) {
        usage(stderr);
        return fail("--foxsdr-helper 1 is required (pipe protocol version 1)");
    }
    if (!have_fmt || !have_rate)
        return fail("--format and --rate are required");

    bool want_sn = protocol == "auto" || protocol == "smartnet";
    bool want_ed = protocol == "auto" || protocol == "edacs";
    const double SN_BAUD = 3600.0, ED_BAUD = 9600.0;
    if (rate < 2 * SN_BAUD) {
        if (protocol == "smartnet") return fail("--rate must be at least 7200 Hz for SmartNet (3600 baud)");
        want_sn = false;
    }
    if (rate < 2 * ED_BAUD) {
        if (protocol == "edacs") return fail("--rate must be at least 19200 Hz for EDACS (9600 baud)");
        want_ed = false;
    }
    if (!want_sn && !want_ed)
        return fail("--rate is too low for every protocol (SmartNet needs 7200 Hz, EDACS 19200 Hz)");

    // JSON goes to a private copy of stdout; anything else that prints to fd 1 lands on stderr.
    FILE *jout;
#ifdef _WIN32
    int fd = _dup(1);
    if (fd < 0) return fail("cannot duplicate stdout");
    _dup2(2, 1);
    _setmode(fd, _O_BINARY);
    jout = _fdopen(fd, "wb");
#else
    int fd = dup(1);
    if (fd < 0) return fail("cannot duplicate stdout");
    dup2(2, 1);
    jout = fdopen(fd, "wb");
#endif
    if (!jout) return fail("cannot open the output stream");

    Emitter em(jout, rate, hide);
    Stats sn_stats, ed_stats;
    std::unique_ptr<Slicer> sn_slicer, ed_slicer;
    std::unique_ptr<SmartnetDecoder> sn_dec;
    std::unique_ptr<EdacsDecoder> ed_dec;
    if (want_sn) {
        sn_slicer.reset(new Slicer(rate, SN_BAUD, 4));
        sn_dec.reset(new SmartnetDecoder(em, rate, sn_slicer->samples_per_symbol(), sn, sn_stats));
        if (!sn_dec->ok()) {
            fprintf(stderr, "trunkctl: %s\n", sn_dec->error().c_str());
            return 2;
        }
    }
    if (want_ed) {
        ed_slicer.reset(new Slicer(rate, ED_BAUD, 4));
        ed_dec.reset(new EdacsDecoder(em, rate, ed_slicer->samples_per_symbol(), ed, ed_stats));
    }

    {
        const char *fn = fmt == F_S16 ? "s16" : fmt == F_F32 ? "f32" : fmt == F_CS16 ? "cs16" : "cf32";
        char b[640];
        snprintf(b, sizeof b,
                 "{\"helper\":\"" HELPER_NAME "\",\"version\":\"" HELPER_VERSION "\",\"protocol\":1,"
                 "\"licence\":\"" HELPER_LICENCE "\",\"source\":\"" HELPER_SOURCE "\","
                 "\"upstream\":\"" HELPER_UPSTREAM "\",\"format\":\"%s\",\"rate\":%.0f,\"protocols\":[%s%s%s]}",
                 fn, rate, want_sn ? "\"smartnet\"" : "", (want_sn && want_ed) ? "," : "", want_ed ? "\"edacs\"" : "");
        em.write_raw(b);
    }

    const uint64_t stats_every = stats_interval > 0 ? (uint64_t)std::llround(stats_interval * rate) : 0;
    uint64_t next_stats = stats_every;
    uint64_t n = 0; // samples consumed
    auto stats_line = [&](const char *proto, const Stats &s, bool locked, uint64_t at) {
        Line l = em.line("stats", proto, at);
        l.num("frames", (int64_t)s.frames).num("messages", (int64_t)s.messages).num("unknown", (int64_t)s.unknown);
        if (std::strcmp(proto, "edacs") == 0)
            l.num("bad", (int64_t)s.bad).num("corrected", (int64_t)s.corrected);
        else
            l.num("gaps", (int64_t)s.gaps);
        l.num("lost", (int64_t)s.lost).boolean("locked", locked);
        em.write(l);
    };
    auto emit_stats = [&](uint64_t at) {
        if (sn_dec) stats_line("smartnet", sn_stats, sn_dec->locked(), at);
        if (ed_dec) stats_line("edacs", ed_stats, ed_dec->locked(), at);
    };

    auto feed = [&](float x) {
        if (sn_dec) {
            sn_slicer->push(x, *sn_dec);
            sn_dec->tick(n);
        }
        if (ed_dec) {
            ed_slicer->push(x, *ed_dec);
            ed_dec->tick(n);
        }
        n++;
        if (stats_every && n >= next_stats) {
            emit_stats(n);
            next_stats += stats_every;
        }
    };

    const bool iq = (fmt == F_CS16 || fmt == F_CF32);
    const size_t unit = fmt == F_S16 ? 2 : fmt == F_F32 ? 4 : fmt == F_CS16 ? 4 : 8;
    std::vector<unsigned char> buf(65536 + 16);
    size_t have = 0;
    float pi = 0, pq = 0; // previous complex sample for the discriminator
    for (;;) {
        size_t got = fread(buf.data() + have, 1, 65536, stdin);
        if (got == 0)
            break;
        have += got;
        size_t usable = have - have % unit;
        for (size_t o = 0; o < usable; o += unit) {
            const unsigned char *p = buf.data() + o;
            if (!iq) {
                if (fmt == F_S16) {
                    int16_t s = (int16_t)(p[0] | (p[1] << 8));
                    feed((float)s * (1.0f / 32768.0f));
                } else {
                    float x;
                    std::memcpy(&x, p, 4);
                    if (!std::isfinite(x)) x = 0;
                    feed(x);
                }
            } else {
                float re, im;
                if (fmt == F_CS16) {
                    re = (float)(int16_t)(p[0] | (p[1] << 8)) * (1.0f / 32768.0f);
                    im = (float)(int16_t)(p[2] | (p[3] << 8)) * (1.0f / 32768.0f);
                } else {
                    std::memcpy(&re, p, 4);
                    std::memcpy(&im, p + 4, 4);
                    if (!std::isfinite(re)) re = 0;
                    if (!std::isfinite(im)) im = 0;
                }
                // quadrature discriminator: phase step between consecutive samples
                float dot = re * pi + im * pq, cross = im * pi - re * pq;
                feed(std::atan2(cross, dot));
                pi = re;
                pq = im;
            }
        }
        size_t rest = have - usable;
        if (rest) std::memmove(buf.data(), buf.data() + usable, rest);
        have = rest;
    }
    if (have)
        fprintf(stderr, "trunkctl: input ended in the middle of a sample (%zu stray byte(s) ignored)\n", have);

    if (sn_dec) sn_dec->finish(n);
    if (ed_dec) ed_dec->finish(n);
    emit_stats(n);
    fflush(jout);
    return 0;
}
