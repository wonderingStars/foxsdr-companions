// CIS 36-50 decoder, a C++ port of Rivet's CIS3650.java. See cis3650.h and UPSTREAM.md.
//
// Rivet Copyright (C) 2011 Ian Wraith (GPL-3.0-or-later); this port Copyright (C) 2026 for the FoxSDR GPL helpers, GPL-3.0-or-later.
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or (at your option) any later version. It comes with ABSOLUTELY NO WARRANTY.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "cis3650.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hfm {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kSps50 = 160;          // samplesPerSymbol(50 Bd, 8000 Hz)
constexpr int kFft = 80;             // FFT_80_SIZE
// FSK.java ITA3VALS
const int kIta3[35] = {13, 37, 56, 100, 69, 21, 50, 112, 70, 74, 26, 42, 28, 19, 97, 82, 35, 11, 98, 49, 22, 76, 73, 25, 84, 81, 67, 88, 38, 14, 41, 44, 52, 104, 7};

struct Tables {
    double win[kFft];
    double c[kFft][kFft / 2], s[kFft][kFft / 2];   // [n][k-1]
    Tables() {
        for (int i = 0; i < kFft; i++) win[i] = 0.42 - 0.5 * std::cos(2 * kPi * i / kFft) + 0.08 * std::cos(4 * kPi * i / kFft);   // windowBlackman, m = 80
        for (int n = 0; n < kFft; n++) for (int k = 1; k <= kFft / 2; k++) { c[n][k - 1] = std::cos(2 * kPi * k * n / kFft); s[n][k - 1] = std::sin(2 * kPi * k * n / kFft); }
    }
};
const Tables& tables() { static const Tables t; return t; }
std::string hex2(int v) { char b[8]; std::snprintf(b, sizeof b, "0x%02x", v); return b; }
}  // namespace

bool Cis3650::validItaChar(int c) { for (int v : kIta3) if (v == c) return true; return false; }

Cis3650::Cis3650() : ring_(kMax, 0) { reset(); }

void Cis3650::reset() {
    std::fill(ring_.begin(), ring_.end(), 0);
    wpos_ = 0; state_ = 1; syncState_ = 0;
    sampleCount_ = -kMax; symbolCounter_ = 0;       // Rivet: "sampleCount must start negative to account for the buffer gradually filling"
    buffer7_ = buffer21_ = characterCount_ = startCount_ = b7Count_ = countSinceSync_ = 0;
    totalChars_ = totalErrors_ = 0; messages_ = goodChars_ = badChars_ = 0;
    std::fill(std::begin(startBuffer_), std::end(startBuffer_), false);
    kalmanNew_ = kalmanOld_ = 0;
    highTone_ = lowTone_ = highBin_ = lowBin_ = 0;
}

std::string Cis3650::detail() const {
    if (state_ == 1) return "sync hunt";
    if (state_ == 2) return "validating sync";
    return syncState_ == 2 ? "decoding message" : "50 Bd sync found";
}

void Cis3650::push(const float* x, size_t n) {
    for (size_t i = 0; i < n; i++) {
        double v = std::floor((double)x[i] * 32768.0 + 0.5);    // Rivet reads 16 bit WAV samples
        if (v > 32767) v = 32767; if (v < -32768) v = -32768;
        onSample((int)v);
    }
}

// FSK.java do80FFT + FFT.java getSpectrum / getFFTFreq: 80 samples from `pos` after the oldest, Blackman window, real FFT, the spectrum entries are bins 1 .. 39
// (entry i is bin i+1, 100 Hz per bin), the highest entry gives the frequency; *bin is the entry index (Rivet getFreqBin), *pct the percentage of the total.
int Cis3650::freq80(int pos, int* bin, double* pct) {
    const Tables& t = tables();
    double x[kFft];
    int idx = wpos_ + pos;
    for (int i = 0; i < kFft; i++) { if (idx >= kMax) idx -= kMax; x[i] = (double)ring_[(size_t)idx] * t.win[i]; idx++; }
    double high = -1, total = 0; int highEntry = -1;
    for (int e = 0; e < 40; e++) {
        double m = 0;
        if (e < 39) {
            double re = 0, im = 0;
            for (int n = 0; n < kFft; n++) { re += x[n] * t.c[n][e]; im -= x[n] * t.s[n][e]; }
            m = std::sqrt(re * re + im * im);
            total += m;
        }
        if (m > high) { high = m; highEntry = e; }
    }
    if (pct) *pct = total > 0 ? high / total * 100.0 : 0.0;
    if (bin) *bin = highEntry;
    return (int)((8000.0 / (double)kFft) * (double)(highEntry + 1));
}

// FSK.java do80FFTBinRequest: the spectrum entries bin0 and bin1
void Cis3650::bins80(int pos, int bin0, int bin1, double* v0, double* v1) {
    const Tables& t = tables();
    double x[kFft];
    int idx = wpos_ + pos;
    for (int i = 0; i < kFft; i++) { if (idx >= kMax) idx -= kMax; x[i] = (double)ring_[(size_t)idx] * t.win[i]; idx++; }
    auto mag = [&](int e) {
        if (e < 0 || e >= 39) return 0.0;
        double re = 0, im = 0;
        for (int n = 0; n < kFft; n++) { re += x[n] * t.c[n][e]; im -= x[n] * t.s[n][e]; }
        return std::sqrt(re * re + im * im);
    };
    *v0 = mag(bin0); *v1 = mag(bin1);
}

// CIS3650.java detect50Sync: two consecutive symbols with different tones 200 Hz (+-10) apart, the first with a peak of at least 10 % of the spectrum
bool Cis3650::detect50Sync() {
    int b0 = 0, b1 = 0; double pct = 0;
    const int f0 = freq80(0, &b0, &pct);
    if (pct < 10.0) return false;
    const int f1 = freq80(kSps50, &b1, nullptr);
    if (f0 == f1) return false;
    if (f0 > f1) { highTone_ = f0; highBin_ = b0; lowTone_ = f1; lowBin_ = b1; }
    else { highTone_ = f1; highBin_ = b1; lowTone_ = f0; lowBin_ = b0; }
    if (lowBin_ == 0 || highBin_ == 0) return false;
    const int dif = highTone_ - lowTone_;
    const int ashift = dif > shift_ ? dif - shift_ : shift_ - dif;
    return ashift <= 10;
}

// CIS3650.java getSymbolFreqBin: early and late half symbols, the stronger of the two tone bins gives the bit (true = the LOWER tone), early/late gate through the Kalman filter
bool Cis3650::symbolBit() {
    double e0, e1, l0, l1;
    bins80(0, lowBin_, highBin_, &e0, &e1);
    bins80(kSps50 / 2, lowBin_, highBin_, &l0, &l1);
    const double lowTotal = e0 + l0, highTotal = e1 + l1;
    const bool bit = lowTotal > highTotal;
    const double x = lowTotal > highTotal ? e0 : e1, y = lowTotal > highTotal ? l0 : l1;
    if (x + y > 0) {      // Rivet divides by zero here (NaN, and the filter then stays NaN); an empty spectrum leaves the filter alone instead
        const double in = ((x - y) / (x + y)) * 100.0;
        const double nw = 0.99 * kalmanOld_ + 0.009 * in;     // kalmanFilter(in, KALMAN1, KALMAN2)
        kalmanOld_ = kalmanNew_;
        kalmanNew_ = nw;
    }
    double r = std::fabs(kalmanNew_) / 2.0;                    // adjAdjust, EARLYLATEADJUST = 2
    if (kalmanNew_ < 0) r = 0 - r;
    symbolCounter_ = (long long)r;
    return bit;
}

int Cis3650::intFromStart(int pos) const {
    int v = 0;
    if (startBuffer_[pos]) v = 64;
    if (startBuffer_[pos + 1]) v += 32;
    if (startBuffer_[pos + 2]) v += 16;
    if (startBuffer_[pos + 3]) v += 8;
    if (startBuffer_[pos + 4]) v += 4;
    if (startBuffer_[pos + 5]) v += 2;
    if (startBuffer_[pos + 6]) v++;
    return v;
}

// CIS3650.java checkStartBuffer: 21 ones in the first 44 bits, two session keys at most one bit apart, at least 8 valid characters in the key
bool Cis3650::checkStartBuffer() const {
    int count = 0;
    for (int a = 0; a < 44; a++) if (startBuffer_[a]) count++;
    if (count != 21) return false;
    count = 0;
    for (int a = 0; a < 70; a++) if (startBuffer_[a + 44] != startBuffer_[a + 44 + 70]) count++;
    if (count > 1) return false;
    count = 0;
    for (int a = 44; a < 44 + 70; a += 7) if (validItaChar(intFromStart(a))) count++;
    return count >= 8;
}

void Cis3650::onSample(int s) {
    ring_[(size_t)wpos_] = s;
    wpos_ = (wpos_ + 1) % kMax;
    if (state_ == 1) {
        sampleCount_++;
        if (sampleCount_ < 0) return;
        if (detect50Sync()) {
            totalErrors_ = 0; totalChars_ = 0;
            syncState_ = 1; state_ = 2;
            buffer7_ = 0; b7Count_ = 0;
            return;
        }
    } else if (state_ == 2) {
        if (symbolCounter_ >= (long long)kSps50) {
            const bool bit = symbolBit();
            add7(bit);
            b7Count_++;
            if (b7Count_ == 14) {                 // 14 bits to let the early/late gate settle, only the last 3 are looked at
                buffer7_ &= 0x7;
                if (buffer7_ == 5 || buffer7_ == 2) {
                    state_ = 3;
                    b7Count_ = 0; countSinceSync_ = 0;
                    std::fill(std::begin(startBuffer_), std::end(startBuffer_), false);
                    emitEvent("50 Bd sync found, tones " + std::to_string(lowTone_) + " and " + std::to_string(highTone_) + " Hz (analysis bins " + std::to_string(lowBin_) + " and " + std::to_string(highBin_) + ")");
                } else {
                    state_ = 1;
                }
            }
        }
    } else if (state_ == 3) {
        if (symbolCounter_ >= (long long)kSps50) {
            const bool bit = symbolBit();
            if (syncState_ == 1) {
                countSinceSync_++;
                add7(bit);
                if (buffer7_ == 85 || buffer7_ == 42) countSinceSync_ = 0;
                if (countSinceSync_ >= 250) { state_ = 1; emitEvent("50 Bd sync timeout"); }
            }
            if (syncState_ == 1) {
                for (int a = 1; a < 184; a++) startBuffer_[a - 1] = startBuffer_[a];
                startBuffer_[183] = bit;
                if (checkStartBuffer()) {
                    syncState_ = 2;
                    messages_++;
                    unsigned long long sync = 0;
                    for (int a = 0; a < 44; a++) sync = (sync << 1) | (startBuffer_[a] ? 1ULL : 0ULL);
                    char b[40]; std::snprintf(b, sizeof b, "0x%llx", sync);
                    std::string key;
                    for (int a = 44; a < 44 + 70; a += 7) { const int o = intFromStart(a); key += validItaChar(o) ? hex2(o) + " " : "<ERROR> "; }
                    emitEvent(std::string("message start, sync ") + b + ", session key " + key);
                    buffer21_ = 0; buffer7_ = 0; startCount_ = 0; totalChars_ = 0; totalErrors_ = 0;
                }
            } else if (syncState_ == 2) {
                add7(bit); add21(bit);
                startCount_++;
                if (buffer21_ == 0x1DFBF7) { characterCount_ = 0; syncState_ = 4; }      // 1110111 1110111 1110111
                if (startCount_ == 7) {
                    if (validItaChar(buffer7_)) { emitText(hex2(buffer7_) + " "); goodChars_++; }
                    else if (buffer7_ == 0x77) emitText("<EOM> ");
                    else { emitText("[" + hex2(buffer7_) + "] "); totalErrors_++; badChars_++; }
                    startCount_ = 0; buffer7_ = 0;
                    totalChars_++;
                    if (totalChars_ > 5000) syncState_ = 4;      // a message that long must be a problem: end it
                }
            } else if (syncState_ == 4) {
                emitEvent("end of message (" + std::to_string(totalChars_) + " characters in this message, " + std::to_string(totalErrors_) + " of them with errors)");
                countSinceSync_ = 0;
                syncState_ = 1;
                std::fill(std::begin(startBuffer_), std::end(startBuffer_), false);
                state_ = 3;
            }
        }
    }
    sampleCount_++; symbolCounter_++;
}

}  // namespace hfm
