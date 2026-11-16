// weakmodes test tool: FT2L signal generator (writes a 12000 Hz mono 16-bit WAV). NOT part of the helper.
//
// Ported from lib/ft2l/ft2lsim.f90 of wsjtx-ft2libre 356be92 (WSJT-X derivative, GPL-3.0): the noise and signal
// scaling convention (SNR in 2500 Hz, noise sigma 1 per sample before the gain of 100) and the placement of the
// transmission 0.5 s into the 3.75 s period are the same. The Watterson fading option is left out.
// Licence: GPL-3.0-or-later, see LICENSE.
//
//   ft2l_gen --msg "CQ K1ABC FN42" --freq 1500 --dt 0.0 --snr -12 --seed 1 --periods 2 [--msg2 .. --freq2 .. --snr2 ..] --out x.wav
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ft2_wave.h"
#include "pack77.h"
#include "wm_common.h"
#include "wm_wav.h"

struct Sig { std::string msg; double f0 = 1500, dt = 0, snr = -15; bool on = false; };

static bool make_wave(const Sig &s, std::vector<wm::cplx> &c0) {
    uint8_t bits[77];
    if (!wmtest::pack77(s.msg, bits)) { fprintf(stderr, "ft2l_gen: cannot pack '%s'\n", s.msg.c_str()); return false; }
    int itone[wm::FT2_NN];
    wm::ft2l_tones_from_77bits(bits, itone);
    std::vector<wm::cplx> w((wm::FT2_NN + 2) * wm::FT2_NSPS);
    wm::gen_ft2lwave(itone, wm::FT2_NN, wm::FT2_NSPS, 12000.0, s.f0, w.data());
    c0.assign(wm::FT2_NMAX, wm::cplx(0, 0));
    long k = std::lrint((s.dt + 0.5) * 12000.0) - wm::FT2_NSPS; // c0=cshift(c0,-k)
    for (size_t i = 0; i < w.size(); i++) {
        long j = (long)i + k;
        if (j >= 0 && j < wm::FT2_NMAX) c0[j] = w[i];
    }
    return true;
}

int main(int argc, char **argv) {
    Sig a, b;
    a.on = true;
    const char *out = nullptr;
    int periods = 1;
    bool noise_only = false;
    double lead = 0.0; // seconds of noise before the first period (for --offset tests)
    unsigned long seed = 1;
    for (int i = 1; i < argc; i++) {
        std::string o = argv[i];
        auto val = [&]() -> const char * { if (i + 1 >= argc) { fprintf(stderr, "ft2l_gen: %s needs a value\n", o.c_str()); exit(2); } return argv[++i]; };
        if (o == "--msg") a.msg = val();
        else if (o == "--freq") a.f0 = atof(val());
        else if (o == "--dt") a.dt = atof(val());
        else if (o == "--snr") a.snr = atof(val());
        else if (o == "--msg2") { b.msg = val(); b.on = true; }
        else if (o == "--freq2") b.f0 = atof(val());
        else if (o == "--dt2") b.dt = atof(val());
        else if (o == "--snr2") b.snr = atof(val());
        else if (o == "--noise-only") noise_only = true;
        else if (o == "--lead") lead = atof(val());
        else if (o == "--periods") periods = atoi(val());
        else if (o == "--seed") seed = strtoul(val(), nullptr, 10);
        else if (o == "--out") out = val();
        else { fprintf(stderr, "ft2l_gen: unknown option %s\n", o.c_str()); return 2; }
    }
    if (!out || a.msg.empty() || periods < 1) { fprintf(stderr, "usage: ft2l_gen --msg M --freq F --dt D --snr S [--msg2 ...] [--periods N] [--seed N] --out file.wav\n"); return 2; }
    std::vector<wm::cplx> ca, cb;
    if (!make_wave(a, ca)) return 1;
    if (b.on && !make_wave(b, cb)) return 1;
    const double bandwidth_ratio = 2500.0 / (12000.0 / 2.0);
    auto amp = [&](double snr) { return snr > 90.0 ? 1.0 : std::sqrt(2 * bandwidth_ratio) * std::pow(10.0, 0.05 * snr); };
    wm::Rng rng(seed);
    std::vector<int16_t> pcm;
    pcm.reserve((size_t)periods * wm::FT2_NMAX);
    bool clean = a.snr > 90.0 && !noise_only;
    for (long i = 0; i < std::lrint(lead * 12000.0); i++) pcm.push_back(clean ? 0 : wmtest::clip16(100.0 * rng.gauss()));
    for (int p = 0; p < periods; p++) {
        std::vector<double> wave(wm::FT2_NMAX);
        for (int i = 0; i < wm::FT2_NMAX; i++) {
            double v = noise_only ? 0.0 : amp(a.snr) * ca[i].real();
            if (b.on && !noise_only) v += amp(b.snr) * cb[i].real();
            if (!clean) v += rng.gauss();
            wave[i] = v;
        }
        double gain = 100.0;
        if (clean) {
            double pk = 0;
            for (double v : wave) pk = std::max(pk, std::fabs(v));
            gain = 32766.9 / pk;
        }
        for (double v : wave) pcm.push_back(wmtest::clip16(gain * v));
    }
    if (!wmtest::write_wav16(out, pcm)) { fprintf(stderr, "ft2l_gen: cannot write %s\n", out); return 1; }
    printf("ft2l_gen: %s f0=%.1f dt=%.2f snr=%.1f periods=%d seed=%lu\n", a.msg.c_str(), a.f0, a.dt, a.snr, periods, seed);
    return 0;
}
