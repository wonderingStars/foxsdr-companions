// Port of sdrtrunk dsp/fsk/LTRDecoder.java: 300 baud sub-audible FSK slicer used by LTR Standard, LTR-Net and Passport.
// The upstream low-pass filter is a Remez design (FIRFilterSpecification / RemezFIRFilterDesigner); this port uses a
// Kaiser-windowed sinc with the same specification instead (see UPSTREAM.md).  See common.h for the licence notice.
#pragma once
#include <deque>
#include <functional>
#include <vector>
#include "afsk1200.h"

namespace trunkfmt {

class LTRDecoder
{
public:
    using SymbolSink = std::function<void(bool)>;
    // inputRate: sample rate of the audio pushed in; it is converted to the 8000 Hz that the slicer is built for.
    LTRDecoder(double inputRate, SymbolSink sink);
    void push(float x);

private:
    void process(float x8000);                       // LTRDecoder.receive(float[]) for one 8 kHz sample
    float calculateSlope() const;                    // LTRDecoder.calculateSlope() over the last 27 filtered samples

    Resampler mResampler;
    std::vector<float> mTaps;                        // low-pass FIR
    std::deque<float> mInput;                        // FIR history
    std::deque<float> mFiltered;                     // last 27 filtered samples, oldest first
    bool mSymbol = false;
    float mBaudCounter = 0.0f;
    float mMaxSlope = 0.0f;
    int mExcessiveOneSequenceCounter = 0;
    int mSamplesToSkip = 0;
    SymbolSink mSink;
};

}  // namespace trunkfmt
