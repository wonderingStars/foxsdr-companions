/*
 * stdc_main.cpp -- FoxSDR "stdc" GPL helper: command line, input loop and JSON Lines output.
 *
 * Drives the demodulator, frame decoder and packet parser of cropinghigh/inmarsatc (GPL-3.0, see UPSTREAM.md) and
 * prints one JSON line per Inmarsat-C signalling / broadcast packet. This file is new code written for the helper;
 * it is distributed under the same licence as the library it drives (GNU GPL version 3).
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details. You should have received a copy of the GNU General Public License along with this program (the
 * file LICENSE); if not, see <https://www.gnu.org/licenses/>.
 */
#include "stdc_privacy.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <complex>

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <inmarsatc_demodulator.h>
#include <inmarsatc_decoder.h>
#include <inmarsatc_parser.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#define HELPER_NAME "stdc"
#define HELPER_VERSION "1.0.0"
#define HELPER_LICENCE "GPL-3.0"
#define HELPER_SOURCE "https://github.com/cropinghigh/inmarsatc"
#define HELPER_UPSTREAM "inmarsatc cda1242 (2023-07-10)"
#define AUDIO_RATE 48000
#define SYMBOL_RATE 1200
#define BLOCK_SAMPLES 4800     /* samples per demodulator call: fixed, because the demodulator normalises per call */
#define STATS_SECONDS 30       /* a stats line every this many seconds of input */

namespace {

enum Fmt { F_S16, F_F32, F_CS16, F_CF32, F_SYM };

struct Options {
    Fmt fmt = F_S16;
    std::string fmtName = "s16";
    long rate = -1;
    bool hideIds = false;
    bool frames = false;
    int loFreq = 500;
    int hiFreq = 4500;
    double centFreq = 2600.0;
    int tolerance = 9;     /* the value upstream's stdc_decoder program uses */
    double iqShift = 2500.0;
};

FILE* g_out = NULL;

void emit(const std::string& line) {
    std::fwrite(line.data(), 1, line.size(), g_out);
    std::fputc('\n', g_out);
    std::fflush(g_out);
}

std::string num(double v, int decimals = 3) {
    char b[64];
    std::snprintf(b, sizeof(b), "%.*f", decimals, v);
    return b;
}

const char* kManifest =
    "{\n"
    "  \"name\": \"stdc\",\n"
    "  \"version\": \"" HELPER_VERSION "\",\n"
    "  \"licence\": \"" HELPER_LICENCE "\",\n"
    "  \"source\": \"" HELPER_SOURCE "\",\n"
    "  \"upstream\": \"" HELPER_UPSTREAM "\",\n"
    "  \"executable\": \"stdc-helper.exe\",\n"
    "  \"protocol\": 1,\n"
    "  \"input\": \"audio\",\n"
    "  \"formats\": [\"s16\", \"f32\", \"cs16\", \"cf32\", \"sym\"],\n"
    "  \"rate\": 48000,\n"
    "  \"events\": {\n"
    "    \"packet\": [\"frame\", \"descriptor\", \"name\", \"length\", \"stage\", \"reassembled\", \"uncertain\", \"ber\", \"fields\"],\n"
    "    \"frame\": [\"frame\", \"ber\", \"reversed\", \"midstream_reversed\", \"uncertain\"],\n"
    "    \"stats\": [\"samples\", \"symbols\", \"frames\", \"packets\", \"packets_bad\", \"carrier_hz\", \"in_sync\", \"no_sync_blocks\"]\n"
    "  }\n"
    "}\n";

void usage(FILE* f) {
    std::fprintf(f,
        "usage: stdc-helper --foxsdr-helper 1 --format <s16|f32|cs16|cf32|sym> --rate 48000 [options] < samples\n"
        "       stdc-helper --foxsdr-helper-info\n"
        "Inmarsat-C (STD-C) TDM channel: BPSK 1200 symbols/s. Reads raw samples on stdin until end of file and\n"
        "prints JSON Lines on stdout (hello line first). Only broadcast and signalling packets are reported.\n"
        "  --format s16|f32   mono audio at 48000 Hz (USB audio with the signal between 500 and 4500 Hz)\n"
        "  --format cs16|cf32 interleaved I/Q at 48000 Hz, signal near 0 Hz (shifted up by --iq-shift)\n"
        "  --format sym       one byte per demodulated symbol (0 or 1) at 1200 Hz (--rate 1200); for test and\n"
        "                     for the output of other demodulators\n"
        "  --hide-ids         replace terminal identifiers by run-local pseudonyms (#1, #2 ...)\n"
        "  --frames           also print one 'frame' line per decoded frame\n"
        "  --lo-freq N / --hi-freq N / --cent-freq N   demodulator carrier search range and start (Hz)\n"
        "  --tolerance N      frames with more than N wrong unique-word bits are dropped (default 9)\n"
        "  --iq-shift N       I/Q only: shift up by N Hz before use as audio (default 2500)\n");
}

bool parse_args(int argc, char** argv, Options& o, bool& info, bool& proto, std::string& err) {
    info = false;
    proto = false;
    for(int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto need = [&](const char* name) -> const char* {
            if(i + 1 >= argc) {
                err = std::string("option ") + name + " needs a value";
                return NULL;
            }
            return argv[++i];
        };
        if(a == "--foxsdr-helper-info") {
            info = true;
        } else if(a == "--foxsdr-helper") {
            const char* v = need("--foxsdr-helper");
            if(!v) return false;
            if(std::strcmp(v, "1") != 0) {
                err = std::string("unsupported protocol version '") + v + "' (this helper speaks protocol 1)";
                return false;
            }
            proto = true;
        } else if(a == "--format") {
            const char* v = need("--format");
            if(!v) return false;
            std::string f = v;
            if(f == "s16") o.fmt = F_S16;
            else if(f == "f32") o.fmt = F_F32;
            else if(f == "cs16") o.fmt = F_CS16;
            else if(f == "cf32") o.fmt = F_CF32;
            else if(f == "sym") o.fmt = F_SYM;
            else {
                err = "unsupported --format '" + f + "' (use s16, f32, cs16, cf32 or sym)";
                return false;
            }
            o.fmtName = f;
        } else if(a == "--rate") {
            const char* v = need("--rate");
            if(!v) return false;
            char* end = NULL;
            o.rate = std::strtol(v, &end, 10);
            if(!end || *end) {
                err = std::string("--rate '") + v + "' is not a number";
                return false;
            }
        } else if(a == "--centre" || a == "--center") {
            if(!need("--centre")) return false; /* accepted for protocol compatibility, not used */
        } else if(a == "--hide-ids") {
            o.hideIds = true;
        } else if(a == "--frames") {
            o.frames = true;
        } else if(a == "--lo-freq" || a == "--hi-freq" || a == "--tolerance") {
            const char* v = need(a.c_str());
            if(!v) return false;
            int n = std::atoi(v);
            if(a == "--lo-freq") o.loFreq = n;
            else if(a == "--hi-freq") o.hiFreq = n;
            else o.tolerance = n;
        } else if(a == "--cent-freq" || a == "--iq-shift") {
            const char* v = need(a.c_str());
            if(!v) return false;
            double d = std::atof(v);
            if(a == "--cent-freq") o.centFreq = d;
            else o.iqShift = d;
        } else if(a == "-h" || a == "--help") {
            usage(stdout);
            std::exit(0);
        } else {
            err = "unknown option '" + a + "'";
            return false;
        }
    }
    return true;
}

/* The helper's whole pipeline state. */
struct Pipeline {
    Options opt;
    inmarsatc::demodulator::Demodulator demod;
    inmarsatc::decoder::Decoder* decoder;
    inmarsatc::frameParser::FrameParser parser;
    stdc::Privacy privacy;
    std::vector<uint8_t> symPending;     /* symbols waiting to make a full 5000 chunk (sym input only) */
    double now = 0.0;                    /* seconds of input consumed */
    unsigned long long samples = 0, symbols = 0, frames = 0, packets = 0, packetsBad = 0, noSyncBlocks = 0;
    double nextStats = STATS_SECONDS;

    explicit Pipeline(const Options& o) : opt(o), decoder(NULL), privacy(o.hideIds) {
        decoder = new inmarsatc::decoder::Decoder(o.tolerance);
        demod.setLowFreq(o.loFreq);
        demod.setHighFreq(o.hiFreq);
        demod.setCenterFreq(o.centFreq);
    }
    ~Pipeline() { delete decoder; }

    std::string tstr() const { return num(now, 3); }

    void on_symbols(uint8_t* chunk) {
        symbols += DEMODULATOR_SYMBOLSPERCHUNK;
        std::vector<inmarsatc::decoder::Decoder::decoder_result> fr = decoder->decode(chunk);
        for(size_t i = 0; i < fr.size(); i++) {
            frames++;
            if(opt.frames) {
                emit("{\"t\":" + tstr() + ",\"type\":\"frame\",\"frame\":" + std::to_string(fr[i].frameNumber) +
                     ",\"ber\":" + std::to_string(fr[i].BER) +
                     ",\"reversed\":" + (fr[i].isReversedPolarity ? "true" : "false") +
                     ",\"midstream_reversed\":" + (fr[i].isMidStreamReversePolarity ? "true" : "false") +
                     ",\"uncertain\":" + (fr[i].isUncertain ? "true" : "false") + "}");
            }
            std::vector<inmarsatc::frameParser::FrameParser::frameParser_result> pk = parser.parseFrame(fr[i]);
            for(size_t k = 0; k < pk.size(); k++) {
                const inmarsatc::frameParser::PacketDecoder::packetDecoder_result& p = pk[k].decoding_result;
                if(!p.isDecodedPacket) continue;
                if(!p.isCrc) {
                    packetsBad++;
                    continue;
                }
                packets++;
                emit("{\"t\":" + tstr() + ",\"type\":\"packet\"," + privacy.render(p));
            }
        }
    }

    void stats_line(bool final_) {
        emit("{\"t\":" + tstr() + ",\"type\":\"stats\",\"samples\":" + std::to_string(samples) +
             ",\"symbols\":" + std::to_string(symbols) + ",\"frames\":" + std::to_string(frames) +
             ",\"packets\":" + std::to_string(packets) + ",\"packets_bad\":" + std::to_string(packetsBad) +
             ",\"carrier_hz\":" + num(demod.getCenterFreq(), 1) +
             ",\"in_sync\":" + (demod.getIsInSync() ? "true" : "false") +
             ",\"no_sync_blocks\":" + std::to_string(noSyncBlocks) + (final_ ? ",\"final\":true}" : "}"));
    }

    void tick() {
        while(now >= nextStats) {
            stats_line(false);
            nextStats += STATS_SECONDS;
        }
    }

    /* audio block through the demodulator */
    void run_block(std::vector<std::complex<double>>& blk) {
        if(blk.empty()) return;
        int before = demod.getNoSyncCount();
        std::vector<inmarsatc::demodulator::Demodulator::demodulator_result> res = demod.demodulate(blk.data(), (int)blk.size());
        noSyncBlocks += (unsigned)(demod.getNoSyncCount() - before);
        samples += blk.size();
        now = (double)samples / AUDIO_RATE;
        for(size_t i = 0; i < res.size(); i++) {
            on_symbols(res[i].bitsDemodulated);
        }
        tick();
        blk.clear();
    }

    void feed_symbols(const uint8_t* p, size_t n) {
        for(size_t i = 0; i < n; i++) {
            symPending.push_back(p[i] ? 1 : 0);
            if(symPending.size() == DEMODULATOR_SYMBOLSPERCHUNK) {
                samples += DEMODULATOR_SYMBOLSPERCHUNK;
                now = (double)samples / SYMBOL_RATE;
                on_symbols(symPending.data());
                symPending.clear();
                tick();
            }
        }
    }
};

int run(const Options& opt) {
    Pipeline pl(opt);
    emit(std::string("{\"helper\":\"" HELPER_NAME "\",\"version\":\"" HELPER_VERSION "\",\"protocol\":1,\"licence\":\"" HELPER_LICENCE "\",") +
         "\"source\":\"" HELPER_SOURCE "\",\"upstream\":\"" HELPER_UPSTREAM "\",\"format\":\"" + opt.fmtName +
         "\",\"rate\":" + std::to_string(opt.rate) + ",\"hide_ids\":" + (opt.hideIds ? "true" : "false") + "}");

    static unsigned char buf[65536];
    std::vector<std::complex<double>> blk;
    blk.reserve(BLOCK_SAMPLES);
    std::vector<unsigned char> carry; /* bytes of an incomplete sample at the end of a read */
    size_t bytesPer = (opt.fmt == F_S16) ? 2 : (opt.fmt == F_F32) ? 4 : (opt.fmt == F_CS16) ? 4 : (opt.fmt == F_CF32) ? 8 : 1;
    unsigned long long audioIndex = 0; /* sample counter for the I/Q shift oscillator */
    const double shiftStep = 2.0 * M_PI * opt.iqShift / AUDIO_RATE;

    for(;;) {
        size_t got = std::fread(buf, 1, sizeof(buf), stdin);
        if(got == 0) break;
        const unsigned char* p = buf;
        size_t n = got;
        std::vector<unsigned char> joined;
        if(!carry.empty()) {
            joined = carry;
            joined.insert(joined.end(), buf, buf + got);
            p = joined.data();
            n = joined.size();
            carry.clear();
        }
        size_t whole = n / bytesPer;
        if(opt.fmt == F_SYM) {
            pl.feed_symbols(p, n);
            continue;
        }
        for(size_t i = 0; i < whole; i++) {
            const unsigned char* s = p + i * bytesPer;
            double re = 0.0;
            if(opt.fmt == F_S16) {
                re = (double)(int16_t)(s[0] | (s[1] << 8));
            } else if(opt.fmt == F_F32) {
                float f;
                std::memcpy(&f, s, 4);
                re = std::isfinite(f) ? (double)f : 0.0;
            } else {
                double I, Q;
                if(opt.fmt == F_CS16) {
                    I = (double)(int16_t)(s[0] | (s[1] << 8));
                    Q = (double)(int16_t)(s[2] | (s[3] << 8));
                } else {
                    float fi, fq;
                    std::memcpy(&fi, s, 4);
                    std::memcpy(&fq, s + 4, 4);
                    I = std::isfinite(fi) ? (double)fi : 0.0;
                    Q = std::isfinite(fq) ? (double)fq : 0.0;
                }
                /* real part of (I + jQ) * exp(+j 2 pi shift n / fs): the signal ends up at +shift Hz as audio */
                double ph = std::fmod(shiftStep * (double)audioIndex, 2.0 * M_PI);
                re = I * std::cos(ph) - Q * std::sin(ph);
                audioIndex++;
            }
            /* upstream's stdc_demod hands the demodulator the same value in both components */
            blk.push_back(std::complex<double>(re, re));
            if(blk.size() == BLOCK_SAMPLES) {
                pl.run_block(blk);
            }
        }
        if(n % bytesPer) {
            carry.assign(p + whole * bytesPer, p + n);
        }
    }
    if(!carry.empty()) {
        std::fprintf(stderr, "stdc: input ended in the middle of a sample (%zu stray byte(s) ignored)\n", carry.size());
    }
    if(opt.fmt != F_SYM) {
        pl.run_block(blk);
    }
    if(pl.samples > 0) { /* nothing was read: the hello line alone */
        pl.stats_line(true);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    Options opt;
    bool info = false, proto = false;
    std::string err;
    if(!parse_args(argc, argv, opt, info, proto, err)) {
        std::fprintf(stderr, "stdc: %s\n", err.c_str());
        usage(stderr);
        return 2;
    }
    if(info) {
        std::fputs(kManifest, stdout);
        return 0;
    }
    if(!proto) {
        std::fprintf(stderr, "stdc: not started as a helper: --foxsdr-helper 1 is required\n");
        usage(stderr);
        return 2;
    }
    long wantRate = (opt.fmt == F_SYM) ? SYMBOL_RATE : AUDIO_RATE;
    if(opt.rate != wantRate) {
        std::fprintf(stderr, "stdc: --format %s needs --rate %ld (got %ld); this helper does not resample\n",
                     opt.fmtName.c_str(), wantRate, opt.rate);
        return 2;
    }
    if(opt.loFreq >= opt.hiFreq) {
        std::fprintf(stderr, "stdc: --lo-freq must be below --hi-freq\n");
        return 2;
    }
    /* JSON goes to a private copy of stdout; the real fd 1 is pointed at stderr so nothing else can reach it */
#ifdef _WIN32
    int outfd = _dup(1);
    if(outfd < 0 || _dup2(2, 1) < 0) {
        std::fprintf(stderr, "stdc: cannot set up output\n");
        return 2;
    }
    g_out = _fdopen(outfd, "wb");
#else
    int outfd = dup(1);
    if(outfd < 0 || dup2(2, 1) < 0) {
        std::fprintf(stderr, "stdc: cannot set up output\n");
        return 2;
    }
    g_out = fdopen(outfd, "wb");
#endif
    if(!g_out) {
        std::fprintf(stderr, "stdc: cannot open output\n");
        return 2;
    }
    std::setvbuf(g_out, NULL, _IOFBF, 1 << 16);
    return run(opt);
}
