// hfmodes helper: audio front end, see frontend.h. The Half class is a copy of the one in OnAir's core/src/cishf/cishf_rx.cpp
// (Copyright (C) 2026 Kristijonas Adomaitis and OnAir contributors, GPL-3.0-or-later); the rest is written for this helper.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend.h"
#include <algorithm>
#include <cmath>

namespace hfm {
namespace {
constexpr double kPi = 3.14159265358979323846;
double blackman(double x) {   // x in [-1, 1]
    if (x <= -1 || x >= 1) return 0;
    const double u = 0.5 * (x + 1);
    return 0.42 - 0.5 * std::cos(2 * kPi * u) + 0.08 * std::cos(4 * kPi * u);
}
}  // namespace

Resampler::Resampler(double rin, double rout) : rin_(rin), rout_(rout), step_(rin / rout), pass_(std::fabs(rin - rout) < 1e-9) {
    // cut-off: just under the lower Nyquist rate; filter half length 24 zero crossings of the cut-off
    fc_ = 0.46 * std::min(rin, rout);
    half_ = (int)std::ceil(24.0 * rin / (2 * fc_));
}

void Resampler::push(const float* x, size_t n, std::vector<float>& out) {
    if (pass_) { out.insert(out.end(), x, x + n); return; }
    buf_.insert(buf_.end(), x, x + n);
    const double scale = 2 * fc_ / rin_;   // sinc(2 fc t / rin) with unit DC gain
    for (;;) {
        const double centre = pos_;                       // in buf_ coordinates
        const long lo = (long)std::ceil(centre - half_), hi = (long)std::floor(centre + half_);
        if (hi >= (long)buf_.size()) break;
        double acc = 0;
        for (long i = lo; i <= hi; i++) {
            const double d = (double)i - centre;
            const double a = d * scale;
            const double s = std::fabs(a) < 1e-12 ? 1.0 : std::sin(kPi * a) / (kPi * a);
            const double v = i < 0 ? 0.0 : (double)buf_[(size_t)i];
            acc += v * s * blackman(d / half_);
        }
        out.push_back((float)(acc * scale));
        pos_ += step_;
    }
    // drop history we no longer need
    const long keepFrom = (long)std::floor(pos_) - half_ - 1;
    if (keepFrom > 4096) {
        buf_.erase(buf_.begin(), buf_.begin() + keepFrom);
        pos_ -= (double)keepFrom;
    }
}

Analytic::Analytic() : h_(kTaps, 0.f), ring_(kTaps, 0.f) {
    const int c = kTaps / 2;
    for (int i = 0; i < kTaps; i++) {
        const int k = i - c;
        if (k == 0 || (k & 1) == 0) { h_[(size_t)i] = 0; continue; }     // ideal Hilbert: 2/(pi k) for odd k
        const double w = 0.42 - 0.5 * std::cos(2 * kPi * i / (kTaps - 1)) + 0.08 * std::cos(4 * kPi * i / (kTaps - 1));
        h_[(size_t)i] = (float)(2.0 / (kPi * k) * w);
    }
}

void Analytic::push(const float* x, size_t n, std::vector<cplx>& out) {
    const int c = kTaps / 2;
    for (size_t i = 0; i < n; i++) {
        ring_[(size_t)w_] = x[i];
        float im = 0;
        for (int k = 0; k < kTaps; k++) {
            int idx = w_ - k; if (idx < 0) idx += kTaps;
            im += h_[(size_t)k] * ring_[(size_t)idx];
        }
        int ri = w_ - c; if (ri < 0) ri += kTaps;
        out.push_back(cplx(ring_[(size_t)ri], im));      // x[n-c] + j * H{x}[n-c]: positive frequencies only (sign checked by the unit test)
        w_ = (w_ + 1) % kTaps;
    }
}

Half::Half() {
    const int c = kTaps / 2;
    double s = 0;
    for (int i = 0; i < kTaps; i++) {
        const double t = i - c, fc = 5600.0 / 24000.0;
        const double v = t == 0 ? 2 * fc : std::sin(2 * kPi * fc * t) / (kPi * t);
        h_[i] = (float)(v * (0.42 - 0.5 * std::cos(2 * kPi * i / (kTaps - 1)) + 0.08 * std::cos(4 * kPi * i / (kTaps - 1))));
        s += h_[i];
    }
    for (float& v : h_) v = (float)(v / s);
    std::fill(std::begin(d_), std::end(d_), cplx(0, 0));
}

void Half::push(const std::vector<cplx>& in, std::vector<cplx>& out) {
    for (const cplx& x : in) {
        d_[w_] = d_[w_ + kTaps] = x;
        w_ = (w_ + 1) % kTaps;
        odd_ = !odd_;
        if (!odd_) continue;
        cplx acc(0, 0);
        for (int i = 0; i < kTaps; i++) acc += d_[w_ + i] * h_[i];
        out.push_back(acc);
    }
}

}  // namespace hfm
