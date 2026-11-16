// weakmodes2: fast-fading Q65 decode (see wm2_q65ff.h). New code for this helper, licence GPL-3.0-or-later; the codec it calls is WSJT-X's
// (lib/qra/q65/*.c, IV3NWV, GPL-3.0-or-later).
#include "wm2_q65ff.h"
#include "hfdig_wsjt_int.h"
extern "C" {
#include "q65.h"
#include "qra15_65_64_irr_e23.h"
}
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace dect2 {
namespace wsjt {

namespace {
q65_codec_ds g_codec;
bool g_ready = false;
bool initCodec() {
    if (!g_ready) {
        if (q65_init(&g_codec, &qra15_65_64_irr_e23) < 0) return false;
        g_ready = true;
    }
    return true;
}
} // namespace

bool q65FastFading(const std::vector<float>& y, double rate, int N, long start, double bf, int sub, const int* isync, int* msg13,
                   float* esnodb) {
    if (sub < 0 || sub > 4 || !initCodec()) return false;
    const double baud = rate / N;
    const int mult = 1 << sub;
    const int LL = 64 * (2 + mult);   // bins per data symbol in WSJT-X's s3: -64 .. LL - 65 around tone 1 (the first data tone)
    const int b0 = (int)std::lround(bf / baud);
    const double shift = bf - b0 * baud;
    // s3[n][k] = energy of data symbol n at bin (tone 1 + k - 64); symbol values 0 .. 63 sit at k = 64 + v * mult
    std::vector<float> s3((size_t)63 * (size_t)LL);
    {
        int j0 = 0, n = 0;
        for (int i = 1; i <= 85; i++) {
            if (j0 < 22 && i == isync[j0]) { j0++; continue; }
            symbolPowers(y, rate, start + (long)(i - 1) * N, N, shift, b0 + mult - 64, LL, &s3[(size_t)n * (size_t)LL]);
            n++;
        }
    }
    // WSJT-X q65_decode.f90, ndepth 3 settings: the spread bandwidths 1.72^ibw Hz tried, Lorentzian fading model
    int ibwa = sub == 0 ? 1 : sub == 1 ? 3 : 8;
    int ibwb = std::min(15, ibwa + 6);
    ibwa = std::max(1, ibwa - 2);
    ibwb = std::min(15, ibwb + 2);
    std::vector<float> prob((size_t)63 * 64);
    int apmask[13] = {}, apsym[13] = {};
    for (int ibw = ibwa; ibw <= ibwb; ibw++) {
        const float b90ts = (float)(std::pow(1.72, ibw) / baud);
        if (q65_intrinsics_fastfading(&g_codec, prob.data(), s3.data(), sub, b90ts, 1) < 0) return false;
        int ydec[63], xdec[13];
        const int rc = q65_decode(&g_codec, ydec, xdec, prob.data(), apmask, apsym, 100);
        if (wsjtDebug()) printf("    q65ff b=%.1f Hz (ibw %d) rc %d\n", std::pow(1.72, ibw), ibw, rc);
        int sum = 0;
        for (int k = 0; k < 13; k++) sum += xdec[k];
        if (rc < 0 || sum <= 0) continue;
        float e = 0;
        if (q65_esnodb_fastfading(&g_codec, &e, ydec, s3.data()) < 0) continue;
        std::memcpy(msg13, xdec, 13 * sizeof(int));
        // WSJT-X: snr2 = esnodb - db(2500 / baud) + 3.0 (empirical)
        *esnodb = e - (float)(10.0 * std::log10(2500.0 / baud)) + 3.0f;
        return true;
    }
    return false;
}

} // namespace wsjt
} // namespace dect2
