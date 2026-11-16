// CIS 36-50 (BEE) message decoder: a C++ port of Ian Wraith's Rivet decoder (src/org/e2k/CIS3650.java, with FSK.java do80FFT /
// do80FFTBinRequest / kalmanFilter / checkITA3Char and FFT.java getSpectrum / getFFTFreq, Rivet Copyright (C) 2011 Ian Wraith,
// GPL-3.0-or-later). It is a different decoder from OnAir's CIS-36-50 (third_party/onair cishf, mode name CIS-36-50-ONAIR) and shares no code
// with it. See UPSTREAM.md for every change.
//
// What Rivet reads: 50 Bd 2-FSK with a 200 Hz shift (the 1 bit is the LOWER tone), an alternating sync, a 44-bit start word with 21 ones, the 70-bit
// session key twice, then 7-bit 3-of-7 characters shown as hex, and the end of message 1110111 x3. Nothing is decrypted. Rivet only reads 50 Bd at 200 Hz.
//
// Input: mono audio at 8000 Hz.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hfm {

class Cis3650 {
public:
    static constexpr double kRate = 8000.0;
    Cis3650();
    void reset();
    void push(const float* x, size_t n);
    void setTextSink(std::function<void(const std::string&)> cb) { text_ = std::move(cb); }
    void setEventSink(std::function<void(const std::string&)> cb) { event_ = std::move(cb); }
    int state() const { return state_; }          // 1 sync hunt, 2 validating the 50 Bd sync, 3 reading (see syncState)
    int syncState() const { return syncState_; }  // 1 waiting for the start word, 2 reading a message
    uint64_t messages() const { return messages_; }
    uint64_t good() const { return goodChars_; }
    uint64_t bad() const { return badChars_; }
    std::string detail() const;
    static bool validItaChar(int c);               // Rivet ITA3VALS: the 35 seven-bit words with three ones
private:
    void onSample(int s);
    int freq80(int pos, int* bin, double* pct);
    void bins80(int pos, int bin0, int bin1, double* v0, double* v1);
    bool detect50Sync();
    bool symbolBit();
    void add7(bool b) { buffer7_ = ((buffer7_ << 1) & 0x7F) | (b ? 1 : 0); }
    void add21(bool b) { buffer21_ = ((buffer21_ << 1) & 0x1FFFFF) | (b ? 1 : 0); }
    bool checkStartBuffer() const;
    int intFromStart(int pos) const;
    void emitText(const std::string& s) { if (text_) text_(s); }
    void emitEvent(const std::string& s) { if (event_) event_(s); }

    static constexpr int kMax = 10240;             // Rivet's circular buffer: reads start at the OLDEST sample, 1.28 s behind the newest
    std::vector<int> ring_;
    int wpos_ = 0;
    int state_ = 1, syncState_ = 0;
    long long sampleCount_ = 0, symbolCounter_ = 0;
    int shift_ = 200;
    int highTone_ = 0, lowTone_ = 0, highBin_ = 0, lowBin_ = 0;
    int buffer7_ = 0, buffer21_ = 0, characterCount_ = 0, startCount_ = 0, b7Count_ = 0, countSinceSync_ = 0;
    int totalChars_ = 0, totalErrors_ = 0;
    uint64_t messages_ = 0, goodChars_ = 0, badChars_ = 0;
    bool startBuffer_[184];
    double kalmanNew_ = 0, kalmanOld_ = 0;
    std::function<void(const std::string&)> text_, event_;
};

}  // namespace hfm
