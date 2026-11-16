// CROWD36 (CIS-36 MFSK, 36-tone 40 Hz-spaced, 40 Bd as Rivet reads it) character decoder: a C++ port of Ian Wraith's Rivet decoder
// (src/org/e2k/CROWD36.java, with MFSK.java doCR36_8000FFT and FFT.java getSpectrum / getFFTFreq, Rivet Copyright (C) 2011 Ian Wraith,
// GPL-3.0-or-later). The tone table, the sync tone hunt, the symbol timing from the energy dip, the FFT peak tone detector and both
// character tables are Rivet's. See UPSTREAM.md for every change.
//
// Input: mono audio at 8000 Hz, as a stream of samples. Output: characters and events through callbacks.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hfm {

class Crowd36 {
public:
    static constexpr double kRate = 8000.0;
    explicit Crowd36(int baud = 40);
    void reset();
    void push(const float* x, size_t n);   // audio at 8 kHz, full scale +-1
    void setTextSink(std::function<void(const std::string&)> cb) { text_ = std::move(cb); }
    void setEventSink(std::function<void(const std::string&)> cb) { event_ = std::move(cb); }
    void setSyncHighTone(int t) { syncHighTone_ = t; }
    int state() const { return state_; }          // 0 hunting for the sync tones, 1 timing, 2 reading symbols
    uint64_t good() const { return good_; }       // symbols that decoded to a character
    uint64_t bad() const { return bad_; }         // symbols with a tone outside the table, or no clear tone
    int correctionHz() const { return correction_; }

    // test helpers (same tables the decoder uses)
    static const int* toneTable();                // 34 tones, Hz
    static int toneForChar(char c, bool figures); // -1 when the table has no such character
private:
    void onSample(int s);
    int toneFreq(int pos, double* peakPct);
    bool syncHunt(std::string& line);
    void onSymbol(int freq, double peakPct);
    std::string charFor(int tone);

    int baud_;
    double sps_;
    static constexpr int kMax = 10240;           // Rivet's circular buffer: the decoder reads the OLDEST samples, 1.28 s behind the newest
    std::vector<int> ring_;
    int wpos_ = 0;
    std::vector<int> energy_;
    int state_ = 0;
    long long sampleCount_ = 0;
    long long symbolCounter_ = 0;
    long long energyStart_ = 0;
    int energyN_ = 0;
    int huntSkip_ = 0;
    bool figureShift_ = false;
    int correction_ = 0;
    int syncHighTone_ = 24;
    int quiet_ = 0;
    uint64_t good_ = 0, bad_ = 0;
    std::function<void(const std::string&)> text_, event_;
};

}  // namespace hfm
