// weakmodes test tool: LDPC(174,91) encode / BP / OSD round trips (clean, noisy, with a-priori mask) and timing.
// Exit 0 when every decode that claims success returns the transmitted message and the easy cases all succeed.
#include <chrono>
#include <cstdio>
#include <cstring>
#include "ldpc174_91.h"
#include "wm_common.h"

int main() {
    using namespace wm;
    Rng r(5);
    int bad = 0;
    // CRC check on an encoded word: m96 = 77 msg bits + 5 zeros + 14 crc bits has remainder 0
    for (int trial = 0; trial < 3; trial++) {
        uint8_t m77[77], cw[174];
        for (int i = 0; i < 77; i++) m77[i] = r.next() & 1;
        encode174_91(m77, cw);
        uint8_t m96[96] = {0};
        memcpy(m96, cw, 77);
        for (int i = 0; i < 14; i++) m96[82 + i] = cw[77 + i];
        int nbad;
        get_crc14(m96, 96, &nbad);
        if (nbad != 0) { printf("DIFF crc of an encoded word is %d\n", nbad); bad++; }
    }
    // sigma = noise on +-1 values: 0 (clean), 0.7 (BP usually works), 1.0 and 1.15 (OSD territory)
    const double sig[4] = {0.0, 0.7, 1.0, 1.15};
    for (int si = 0; si < 4; si++) {
        int ok = 0, wrong = 0, n = 30;
        double ms = 0;
        for (int t = 0; t < n; t++) {
            uint8_t m77[77], cw[174];
            for (int i = 0; i < 77; i++) m77[i] = r.next() & 1;
            encode174_91(m77, cw);
            float llr[174];
            for (int i = 0; i < 174; i++) llr[i] = 2.0f * ((cw[i] ? 1.f : -1.f) + (float)(sig[si] * r.gauss())) / (float)(sig[si] * sig[si] + 0.25);
            uint8_t ap[174] = {0}, m91[91], cwo[174];
            int nt, nh;
            float dmin;
            auto t0 = std::chrono::steady_clock::now();
            decode174_91(llr, 91, 3, 3, ap, m91, cwo, &nt, &nh, &dmin);
            ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (nh >= 0) {
                if (memcmp(m91, cw, 77) == 0) ok++; else wrong++;
            }
        }
        printf("sigma %.2f: %d/%d decoded, %d wrong, %.2f ms per call\n", sig[si], ok, n, wrong, ms / n);
        if (wrong) bad++;
        if (si <= 1 && ok != n) { printf("DIFF easy case did not decode every time\n"); bad++; }
    }
    printf(bad ? "FAILED %d\n" : "ldpc ok\n", bad);
    return bad ? 1 : 0;
}
