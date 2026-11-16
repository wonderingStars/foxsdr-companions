// hfmodes helper: audio front end. Real audio at any sample rate -> (a) resampled real audio at a chosen rate, (b) analytic signal at 24 kHz
// (audio frequency f appears at +f Hz, as the OnAir decoders expect), (c) the same decimated to 12 kHz for the CIS decoders.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <complex>
#include <cstddef>
#include <vector>

namespace hfm {

using cplx = std::complex<float>;

// Streaming real resampler (windowed sinc, Blackman window, computed per output sample so it works for any ratio). Pass-through when rates are equal.
class Resampler {
public:
    Resampler(double rin, double rout);
    void push(const float* x, size_t n, std::vector<float>& out);   // appends
private:
    double rin_, rout_, step_, fc_;
    int half_;
    std::vector<float> buf_;     // input history
    double pos_ = 0;             // position of the next output in buf_ (input sample units)
    bool pass_;
};

// Hilbert-transform analytic signal (odd-length FIR, delay-matched real part). 24 kHz in, 24 kHz out; usable above about 100 Hz.
class Analytic {
public:
    Analytic();
    void push(const float* x, size_t n, std::vector<cplx>& out);    // appends
private:
    static constexpr int kTaps = 191;   // odd; delay (kTaps-1)/2
    std::vector<float> h_;
    std::vector<float> ring_;
    int w_ = 0;
};

// 24 kHz complex -> 12 kHz complex: windowed-sinc low-pass at 5.6 kHz, every second output (the same filter as OnAir's cishf_rx.cpp "Half").
class Half {
public:
    Half();
    void push(const std::vector<cplx>& in, std::vector<cplx>& out);   // appends
private:
    static constexpr int kTaps = 63;
    float h_[kTaps];
    cplx d_[2 * kTaps];
    int w_ = 0;
    bool odd_ = false;
};

}  // namespace hfm
