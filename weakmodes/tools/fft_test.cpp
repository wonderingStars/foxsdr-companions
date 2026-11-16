// weakmodes test tool: wm::fft against a direct DFT for a range of lengths, both signs. Exit 0 when all agree.
#include <cstdio>
#include "wm_common.h"

int main() {
    const int sizes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 15, 16, 17, 25, 32, 49, 64, 100, 121, 169, 288, 360, 500, 997, 1152, 5000};
    wm::Rng rng(7);
    int bad = 0;
    for (int n : sizes) {
        for (int isign = -1; isign <= 1; isign += 2) {
            std::vector<wm::cplx> x(n), ref(n), y;
            for (int i = 0; i < n; i++) x[i] = wm::cplx(rng.gauss(), rng.gauss());
            for (int k = 0; k < n; k++) {
                wm::cplx s(0, 0);
                for (int j = 0; j < n; j++) {
                    double a = isign * 6.283185307179586 * (double)((long long)j * k % n) / n;
                    s += x[j] * wm::cplx(std::cos(a), std::sin(a));
                }
                ref[k] = s;
            }
            y = x;
            wm::fft(y.data(), n, isign);
            double err = 0, mag = 0;
            for (int k = 0; k < n; k++) { err = std::max(err, std::abs(y[k] - ref[k])); mag = std::max(mag, std::abs(ref[k])); }
            bool ok = err <= 1e-9 * (mag + 1);
            if (!ok) { printf("DIFF n=%d isign=%d err=%g\n", n, isign, err); bad++; }
        }
    }
    printf(bad ? "FAILED %d\n" : "fft ok for all lengths\n", bad);
    return bad ? 1 : 0;
}
