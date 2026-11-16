// weakmodes test tool: JT9 (one-minute sub-mode) signal generator. TEST TOOL ONLY: it writes a 12000 Hz mono 16-bit
// WAV file of 60 s and is not part of the helper.
//
// Ported from WSJT-X lib/jt9sim.f90 (signal and noise scaling, phase-continuous 9-FSK), lib/gen9.f90 and the packing
// code in lib/packjt.f90 (src/jt9_pack.cpp is shared with the decoder, it is included below as source).
// Differences from jt9sim: the file is always 60 s (jt9sim writes 54 s); the signal start can be moved with --dt;
// noise comes from wm::Rng (jt9sim uses the C library rand() in gran.c); several signals may have different
// messages, frequencies and SNRs.
//
// Copyright (C) 2001-2025 Joseph H. Taylor, Jr., K1JT and the WSJT Development Group. GPL-3.0-or-later; see LICENSE.

#include "../src/jt9_pack.cpp"
#include "wm_common.h"
#include "wm_wav.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

struct Sig {
    std::string msg;
    double freq = 1400.0; // Hz, frequency of tone 0 (as jt9sim f0)
    double snr = -20.0;   // dB, jt9sim convention
    double dt = 0.0;      // s against the nominal start 1.0 s
};

static void usage() {
    fprintf(stderr,
            "jt9_gen --out file.wav [--msg \"CQ K1ABC FN42\"] [--freq 1400] [--snr -20] [--dt 0] [--seed 1]\n"
            "        [--sig freq:snr:dt:message]...   (repeatable; replaces --msg/--freq/--snr/--dt)\n"
            "        [--nosignal]                      (noise only)\n"
            "        [--lead s]    s seconds of noise before the 60 s period (the helper then needs --offset s)\n"
            "        [--length s]  cut the file to s seconds\n"
            "SNR follows jt9sim.f90: noise sigma 1 per sample, signal amplitude 10^(snr/20); snr >= 90 means a noiseless\n"
            "signal at full scale. (In true 2500 Hz SNR terms jt9sim's value is 0.8 dB too pessimistic.)\n");
}

int main(int argc, char **argv) {
    std::string out;
    Sig single;
    single.msg = "CQ K1ABC FN42";
    std::vector<Sig> sigs;
    bool nosignal = false;
    double lead = 0.0, length = -1.0;
    unsigned long long seed = 1;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto need = [&]() -> const char * {
            if (i + 1 >= argc) { usage(); exit(2); }
            return argv[++i];
        };
        if (a == "--out") out = need();
        else if (a == "--msg") single.msg = need();
        else if (a == "--freq") single.freq = atof(need());
        else if (a == "--snr") single.snr = atof(need());
        else if (a == "--dt") single.dt = atof(need());
        else if (a == "--seed") seed = strtoull(need(), nullptr, 10);
        else if (a == "--nosignal") nosignal = true;
        else if (a == "--lead") lead = atof(need());
        else if (a == "--length") length = atof(need());
        else if (a == "--sig") {
            std::string s = need();
            Sig g;
            size_t p1 = s.find(':'), p2 = s.find(':', p1 + 1), p3 = s.find(':', p2 + 1);
            if (p3 == std::string::npos) { usage(); return 2; }
            g.freq = atof(s.substr(0, p1).c_str());
            g.snr = atof(s.substr(p1 + 1, p2 - p1 - 1).c_str());
            g.dt = atof(s.substr(p2 + 1, p3 - p2 - 1).c_str());
            g.msg = s.substr(p3 + 1);
            sigs.push_back(g);
        } else { usage(); return 2; }
    }
    if (out.empty()) { usage(); return 2; }
    if (sigs.empty() && !nosignal) sigs.push_back(single);
    if (nosignal) sigs.clear();

    const int nsps = 6912;
    const int nlead = (int)std::lround(lead * 12000.0);
    const int npts = nlead + 12000 * 60; // the file holds one 60 s period after the lead-in
    const double rmsdb = 25.0;
    const double rms = std::pow(10.0, 0.05 * rmsdb);
    const double twopi = 8.0 * std::atan(1.0);
    const double fsample = 12000.0, dt = 1.0 / fsample, baud = 12000.0 / nsps;
    bool noiseless = false;
    for (auto &s : sigs) if (s.snr >= 90.0) noiseless = true;
    if (single.snr >= 90.0 && sigs.empty() && !nosignal) noiseless = true;

    wm::Rng rng(seed);
    std::vector<double> dat(npts, 0.0);
    if (!noiseless)
        for (int i = 0; i < npts; i++) dat[i] = rng.gauss();

    for (auto &s : sigs) {
        int tone[85];
        std::string sent;
        int itype = wm::jt9::gen9(s.msg, tone, &sent);
        if (itype < 0) { fprintf(stderr, "jt9_gen: this tool cannot pack '%s' (add-on prefix/suffix calls are not supported)\n", s.msg.c_str()); return 1; }
        double sig = std::pow(10.0, 0.05 * s.snr);
        if (s.snr > 90.0) sig = 1.0;
        printf("%8.3f Hz  snr %6.1f  dt %5.2f  type %d  %s\n", s.freq, s.snr, s.dt, itype, sent.c_str());
        double phi = 0.0;
        long k = nlead + (long)std::lround((1.0 + s.dt) * 12000.0);
        for (int isym = 0; isym < 85; isym++) {
            double freq = s.freq + tone[isym] * baud;
            double dphi = twopi * freq * dt;
            for (int i = 0; i < nsps; i++) {
                phi += dphi;
                if (phi < -twopi) phi += twopi;
                if (phi > twopi) phi -= twopi;
                if (k >= 0 && k < npts) dat[k] += sig * std::sin(phi);
                k++;
            }
        }
    }
    std::vector<int16_t> iw(npts);
    (void)length;
    double fac = 32767.0 / (sigs.empty() ? 1 : (double)sigs.size());
    for (int i = 0; i < npts; i++) iw[i] = noiseless ? wmtest::clip16(fac * dat[i]) : wmtest::clip16(rms * dat[i]);
    if (length > 0 && (size_t)std::lround(length * 12000.0) < iw.size()) iw.resize((size_t)std::lround(length * 12000.0));
    if (!wmtest::write_wav16(out.c_str(), iw)) { fprintf(stderr, "jt9_gen: cannot write %s\n", out.c_str()); return 1; }
    return 0;
}
