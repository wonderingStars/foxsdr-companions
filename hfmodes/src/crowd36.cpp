// CROWD36 decoder, a C++ port of Rivet's CROWD36.java. See crowd36.h and UPSTREAM.md.
//
// Rivet Copyright (C) 2011 Ian Wraith (GPL-3.0-or-later); this port Copyright (C) 2026 for the FoxSDR GPL helpers, GPL-3.0-or-later.
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or (at your option) any later version. It comes with ABSOLUTELY NO WARRANTY.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "crowd36.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>

namespace hfm {
namespace {
constexpr double kPi = 3.14159265358979323846;

// CROWD36.java TONES[]: 34 tones from 1410 Hz in 40 Hz steps
const int kTones[34] = {1410, 1450, 1490, 1530, 1570, 1610, 1650, 1690, 1730, 1770, 1810, 1850, 1890, 1930, 1970, 2010, 2050, 2090, 2130, 2170, 2210, 2250, 2290, 2330, 2370, 2410, 2450, 2490, 2530, 2570, 2610, 2650, 2690, 2730};
// CROWD36.java getChar(): C36A (letters) and F36A (figures). "\xD0\xA9" = U+0429 etc., written as UTF-8. Entries "<*n>" are tones whose meaning Rivet does not know.
const char* const kLetters[34] = {"Q", "X", "W", "V", "E", "K", " ", "B", "R", "J", "<*10>", "G", "T", "F", "<fs>", "M", "Y", "C", "cr", "Z", "U", "L", "<*22>", "D", "I", "H", "<ls>", "S", "O", "N", "<*30>", "A", "P", "<*33>"};
const char* const kFigures[34] = {"1", "\xD0\xA9", "2", "=", "3", "(", " ", "?", "4", "\xD0\xAE", "<*10>", "8", "5", "\xD0\xAD", "<fs>", ".", "6", ":", "cr", "+", "7", ")", "<*22>", " ", "8", "\xD1\x89", "<ls>", "", "9", ",", "<*30>", "-", "0", "<*33>"};

typedef std::complex<double> cd;
void fft(std::vector<cd>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t b = n >> 1;
        for (; j & b; b >>= 1) j ^= b;
        j ^= b;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * kPi / (double)len;
        const cd wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cd w(1, 0);
            for (size_t k = 0; k < len / 2; k++) {
                const cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}
}  // namespace

const int* Crowd36::toneTable() { return kTones; }

int Crowd36::toneForChar(char c, bool figures) {
    const std::string s(1, c);
    for (int t = 0; t < 34; t++) if (s == (figures ? kFigures[t] : kLetters[t])) return t;
    return -1;
}

Crowd36::Crowd36(int baud) : baud_(baud), sps_(kRate / baud), ring_(kMax, 0) { reset(); }

void Crowd36::reset() {
    std::fill(ring_.begin(), ring_.end(), 0);
    wpos_ = 0; state_ = 0; sampleCount_ = -kMax; symbolCounter_ = 0; energyStart_ = 0; energyN_ = 0; huntSkip_ = 0;
    figureShift_ = false; correction_ = 0; quiet_ = 0; good_ = bad_ = 0;
    energy_.assign((size_t)kMax, 0);
}

void Crowd36::push(const float* x, size_t n) {
    for (size_t i = 0; i < n; i++) {
        // Rivet reads 16 bit WAV samples: scale to the same integers
        double v = std::floor((double)x[i] * 32768.0 + 0.5);
        if (v > 32767) v = 32767; if (v < -32768) v = -32768;
        onSample((int)v);
    }
}

// Tone frequency of the 200 samples that start `pos` samples after the OLDEST sample in the buffer (Rivet doCR36_8000FFT + getFFTFreq).
// peakPct = the highest spectral value as a percentage of the summed spectrum (Rivet getPercentageOfTotal).
int Crowd36::toneFreq(int pos, double* peakPct) {
    std::vector<cd> a(512, cd(0, 0));
    int idx = wpos_ + pos;                  // wpos_ is the next write position = the oldest sample
    for (int i = 0; i < 200; i++) { if (idx >= kMax) idx -= kMax; a[(size_t)i] = cd((double)ring_[(size_t)idx], 0); idx++; }
    for (int i = 0; i < 512; i++) {         // Blackman over all 512 points, including the 312 zeros, as Rivet does
        const double r = 0.42 - 0.5 * std::cos(2 * kPi * i / 512) + 0.08 * std::cos(4 * kPi * i / 512);
        a[(size_t)i] *= r;
    }
    fft(a);
    double high = -1, total = 0; int highBin = -1;
    for (int k = 1; k <= 255; k++) {        // bins 1 .. 255 (Rivet getSpectrum skips DC)
        const double m = std::abs(a[(size_t)k]);
        total += m;
        if (m > high) { high = m; highBin = k; }
    }
    if (peakPct) *peakPct = total > 0 ? high / total * 100.0 : 0.0;
    const double f = (kRate / 512.0) * highBin;
    return (int)f + correction_;
}

bool Crowd36::syncHunt(std::string& line) {
    double pct = 0;
    const int sps = (int)sps_;
    const int f1 = toneFreq(0, &pct);
    if (pct < 5.0) return false;                 // not just noise
    const int f2 = toneFreq(sps * 1, nullptr);
    if (f2 > f1) return false;                   // a high then a low
    if (f1 - f2 < 100) return false;             // at least 100 Hz apart
    const int f3 = toneFreq(sps * 2, nullptr);
    if (f1 != f3) return false;
    const int f4 = toneFreq(sps * 3, nullptr);
    if (f1 != f3 || f2 != f4) return false;
    if (f1 == f2 || f3 == f4) return false;
    const int diff = f1 - f2;
    correction_ = kTones[syncHighTone_] - f1;
    line = "sync tones found, correcting by " + std::to_string(correction_) + " Hz, sync tone difference " + std::to_string(diff) + " Hz, sync high tone " + std::to_string(syncHighTone_);
    return true;
}

std::string Crowd36::charFor(int tone) {
    if (tone < 0 || tone > 33) { bad_++; return "?"; }     // changed from Rivet, which clamps to tones 0 / 33 (see UPSTREAM.md)
    const std::string out = figureShift_ ? kFigures[tone] : kLetters[tone];
    if (out == "<ls>") figureShift_ = false;
    else if (out == "<fs>") figureShift_ = true;
    else if (out == "cr") figureShift_ = false;
    return out;
}

void Crowd36::onSymbol(int freq, double peakPct) {
    // Rivet getTone(): nearest table tone; below tone 0 or above tone 33 by more than 40 Hz it is out of range
    int index = -1, lowVal = 999;
    for (int a = 0; a < 34; a++) { const int d = std::abs(kTones[a] - freq); if (d < lowVal) { lowVal = d; index = a; } }
    int tone = index;
    if (index == 0 && lowVal > 40) tone = -1;
    else if (index == 33 && lowVal > 40) tone = 34;
    // added: no clear tone in the spectrum is noise, not a character
    if (peakPct < 5.0) {
        if (++quiet_ >= 40) {                   // one second of nothing
            quiet_ = 0; state_ = 0; correction_ = 0; figureShift_ = false; huntSkip_ = 0;
            if (event_) event_("signal lost, hunting for the sync tones again");
        }
        return;
    }
    quiet_ = 0;
    const std::string ch = charFor(tone);
    if (tone >= 0 && tone <= 33) good_++;
    if (ch == "<fs>" || ch == "<ls>") return;     // Rivet shows neither shift
    if (ch == "cr") { if (text_) text_("\n"); return; }
    if (text_ && !ch.empty()) text_(ch);
}

void Crowd36::onSample(int s) {
    // the buffer: the new sample overwrites the oldest, so the oldest is always 10240 samples behind
    ring_[(size_t)wpos_] = s;
    wpos_ = (wpos_ + 1) % kMax;
    const int sps = (int)sps_;
    if (state_ == 0) {
        // Rivet hunts at every sample; here every fourth (the timing step that follows does not depend on the hunt phase)
        if ((huntSkip_++ & 3) == 0) {
            std::string line;
            if (syncHunt(line)) {
                state_ = 1;
                energyStart_ = sampleCount_; energyN_ = 0;
                if (event_) event_(line);
            }
        }
    } else if (state_ == 1) {
        double tot = 0;
        for (int i = 0; i < (int)(sps_ / 20.0); i++) { int idx = wpos_ + i; if (idx >= kMax) idx -= kMax; tot += std::abs(ring_[(size_t)idx]); }
        energy_[(size_t)energyN_++] = (int)(tot / (int)(sps_ / 20.0));
        if (energyN_ > sps) {
            int low = -1, lowVal = 0x7fffffff;
            for (int a = 0; a < energyN_; a++) if (energy_[(size_t)a] < lowVal) { lowVal = energy_[(size_t)a]; low = a; }
            const long long perfect = low + energyStart_ + sps;
            symbolCounter_ = sps - (perfect - sampleCount_);
            state_ = 2;
            sampleCount_++; symbolCounter_++;
            if (event_) event_("symbol timing found");
            return;
        }
    } else if (state_ == 2) {
        if (symbolCounter_ >= sps) {
            symbolCounter_ = 0;
            double pct = 0;
            const int freq = toneFreq(0, &pct);
            onSymbol(freq, pct);
        }
    }
    sampleCount_++; symbolCounter_++;
}

}  // namespace hfm
