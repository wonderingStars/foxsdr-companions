// trunkctl - symbol slicer: discriminator audio -> bits.
//
// Written for this helper (neither upstream has a timing recovery that fits an arbitrary sample rate:
// edacs-fm averages three samples per symbol at a fixed 28800 Hz, trunk-recorder uses GNU Radio blocks).
// The slicer integrates the signal over one symbol period (a box matched filter) at P evenly spaced
// timing phases at once and slices each phase against the midpoint of its own tracked high/low levels.
// Every phase feeds its own framer; the decoder above keeps the first valid frame. Nothing here depends
// on the protocol except the baud rate.
//
// This file is part of trunkctl (GPL-3.0).
#ifndef TRUNKCTL_SLICER_H
#define TRUNKCTL_SLICER_H

#include <cstdint>
#include <vector>

namespace trunkctl {

class BitSink {
public:
    virtual ~BitSink() {}
    // phase: timing phase 0..P-1; bit: 1 when the symbol was above the midpoint; end_sample: index of the
    // last input sample of the symbol (counted from 0).
    virtual void bit(int phase, int bit, uint64_t end_sample) = 0;
};

class Slicer {
public:
    Slicer(double rate, double baud, int phases);
    void push(float sample, BitSink &sink);
    double samples_per_symbol() const { return sps_; }
    int phases() const { return (int)ph_.size(); }

private:
    struct Phase {
        double next_boundary = 0;
        double acc = 0;
        int count = 0;
        double hi = 0, lo = 0;
        bool primed = false;
    };
    double sps_;
    std::vector<Phase> ph_;
    uint64_t n_ = 0;
};

} // namespace trunkctl

#endif
