// Port of sdrtrunk dsp/afsk/AFSK1200Decoder.java (correlation AFSK decoder), AFSKSampleBuffer.java,
// AFSKTimingErrorDetector.java and buffer/FloatAveragingBuffer.java.  The sample-rate converter is NOT
// libresample4j (the library behind upstream RealResampler): see Resampler below and UPSTREAM.md.
// See common.h for the licence notice.
#pragma once
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

namespace trunkfmt {

// Rational polyphase windowed-sinc resampler, any input rate -> 7200 Hz.  Fed one sample at a time so
// that the caller always knows the exact input sample index of every output (deterministic timestamps).
class Resampler
{
public:
    using Sink = std::function<void(float)>;
    Resampler(double inRate, double outRate);
    void push(float x, const Sink& sink);

private:
    long long mL = 1, mM = 1;      // output k is at input position k*M/L
    int mHalf = 1;                 // taps are indices base-mHalf+1 .. base+mHalf
    std::vector<std::vector<float>> mTable;
    std::deque<float> mBuf;        // input samples; mBuf[0] has absolute index mFirst
    long long mFirst = 0;          // absolute index of mBuf[0]
    long long mCount = 0;          // number of input samples pushed
    long long mK = 0;              // next output index
};

// AFSKSampleBuffer.java
class AFSKSampleBuffer
{
public:
    AFSKSampleBuffer(int samplesPerSymbol, float symbolTimingGain);
    void receive(bool sample);
    void resetAndAdjust(float symbolTimingError) { mSymbolSamplingPoint += (mSamplesPerSymbol + (symbolTimingError * mSymbolTimingGain)); }
    bool hasSymbol() const { return mSymbolSamplingPoint < 1.0f; }
    bool getSymbol();

private:
    std::vector<uint8_t> mDelayLine;
    int mDelayLinePointer = 0;
    int mHalfIntegerSamplesPerSymbol;
    int mTwiceSamplesPerSymbol;
    int mSamplesPerSymbol;
    float mSymbolSamplingPoint;
    float mSymbolTimingGain;
};

// AFSKTimingErrorDetector.java
class AFSKTimingErrorDetector
{
public:
    explicit AFSKTimingErrorDetector(int samplesPerSymbol);
    void receive(bool sample);
    int getError();

private:
    std::vector<uint8_t> mBuffer;
    float mSymbolMiddle;
};

// FloatAveragingBuffer.java (non rapid-fill variant, the only one the AFSK decoder uses)
class FloatAveragingBuffer
{
public:
    explicit FloatAveragingBuffer(int size) : mSize(size), mBuffer((size_t)size, 0.0f) {}
    float get(float value);

private:
    int mSize;
    std::vector<float> mBuffer;
    float mAverage = 0.0f;
    int mPointer = 0;
};

// AFSK1200Decoder.Correlator
class Correlator
{
public:
    Correlator(double sampleRate, double frequency, int averagingPeriod, int correlationPeriod);
    float process(float sample);

private:
    FloatAveragingBuffer mAveraging;
    std::vector<float> mReference;
    std::vector<float> mDemodulated;
};

// AFSK1200Decoder.java: input is FM-demodulated audio, output is one bool per 1200-baud symbol.
// 1200 Hz = mark = 1, 1800 Hz = space = 0 (Output.NORMAL); `invert` selects Output.INVERTED.
class AFSK1200Decoder
{
public:
    static constexpr double SAMPLE_RATE = 7200.0;
    static constexpr int SAMPLES_PER_SYMBOL = 6;
    static constexpr int CORRELATION_PERIOD = SAMPLES_PER_SYMBOL + 2;
    static constexpr int AVERAGING_PERIOD = SAMPLES_PER_SYMBOL + 1;
    static constexpr double MARK = 1200.0;
    static constexpr double SPACE = 1800.0;

    using SymbolSink = std::function<void(bool)>;
    AFSK1200Decoder(double inputRate, bool invert, SymbolSink sink);

    // One input sample at the caller's sample rate.
    void push(float x);

private:
    void decode(float x7200);   // AFSK1200Decoder.Decoder.receive() for one 7200 Hz sample

    Resampler mResampler;
    Correlator mMark, mSpace;
    AFSKSampleBuffer mSampleBuffer;
    AFSKTimingErrorDetector mTiming;
    bool mNormal;
    SymbolSink mSink;
};

}  // namespace trunkfmt
