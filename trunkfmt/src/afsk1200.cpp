// See afsk1200.h / common.h for the notices.
#include "afsk1200.h"
#include <algorithm>
#include <numeric>

namespace trunkfmt {

// ------------------------------------------------------------------------------------------------
// Resampler: replacement for upstream RealResampler (which wraps libresample4j, a separate library
// that is not on this PC).  Windowed-sinc (Blackman) interpolation with an exact rational step.
// ------------------------------------------------------------------------------------------------
Resampler::Resampler(double inRate, double outRate)
{
    long long in = std::llround(inRate), out = std::llround(outRate);
    if(in <= 0) { in = 1; }
    if(out <= 0) { out = 1; }
    long long g = std::gcd(in, out);
    mM = in / g;                 // input samples per L outputs
    mL = out / g;
    double fc = 0.5 * std::min(1.0, outRate / inRate) * 0.9;     // cycles per input sample
    mHalf = (int)std::ceil(8.0 / (2.0 * fc));                    // 8 zero crossings each side
    if(mHalf < 4) { mHalf = 4; }
    const double PI = 3.14159265358979323846;
    mTable.assign((size_t)mL, std::vector<float>((size_t)(2 * mHalf), 0.0f));
    for(long long p = 0; p < mL; p++)
    {
        double sum = 0.0;
        std::vector<double> w((size_t)(2 * mHalf));
        for(int j = 0; j < 2 * mHalf; j++)
        {
            double x = (double)(j - mHalf + 1) - (double)p / (double)mL;   // input index minus output position
            double u = x / (double)mHalf;
            double win = (std::fabs(u) >= 1.0) ? 0.0 : (0.42 + 0.5 * std::cos(PI * u) + 0.08 * std::cos(2.0 * PI * u));
            double s = (std::fabs(x) < 1e-12) ? 1.0 : std::sin(2.0 * PI * fc * x) / (2.0 * PI * fc * x);
            w[(size_t)j] = 2.0 * fc * s * win;
            sum += w[(size_t)j];
        }
        for(int j = 0; j < 2 * mHalf; j++) { mTable[(size_t)p][(size_t)j] = (float)(w[(size_t)j] / sum); }
    }
}

void Resampler::push(float x, const Sink& sink)
{
    mBuf.push_back(x);
    mCount++;
    for(;;)
    {
        long long num = mK * mM;                 // position = num / L
        long long base = num / mL;
        long long phase = num % mL;
        long long last = base + mHalf;           // last input index needed
        if(last >= mCount) { break; }
        const std::vector<float>& tap = mTable[(size_t)phase];
        float acc = 0.0f;
        for(int j = 0; j < 2 * mHalf; j++)
        {
            long long idx = base - mHalf + 1 + j;
            if(idx < 0) { continue; }            // before the start of the stream: zero
            acc += tap[(size_t)j] * mBuf[(size_t)(idx - mFirst)];
        }
        sink(acc);
        mK++;
        // drop samples that can no longer be needed
        long long nextBase = (mK * mM) / mL;
        long long keepFrom = nextBase - mHalf + 1;
        while(mFirst < keepFrom && !mBuf.empty()) { mBuf.pop_front(); mFirst++; }
    }
}

// ------------------------------------------------------------------------------------------------
// AFSKSampleBuffer.java
// ------------------------------------------------------------------------------------------------
AFSKSampleBuffer::AFSKSampleBuffer(int samplesPerSymbol, float symbolTimingGain)
    : mSamplesPerSymbol(samplesPerSymbol), mSymbolTimingGain(symbolTimingGain)
{
    mHalfIntegerSamplesPerSymbol = samplesPerSymbol / 2;
    mSymbolSamplingPoint = (float)samplesPerSymbol;
    mTwiceSamplesPerSymbol = 2 * samplesPerSymbol;
    mDelayLine.assign((size_t)(2 * mTwiceSamplesPerSymbol), 0);
}

void AFSKSampleBuffer::receive(bool sample)
{
    mSymbolSamplingPoint--;
    mDelayLine[(size_t)mDelayLinePointer] = sample;
    mDelayLine[(size_t)(mDelayLinePointer + mTwiceSamplesPerSymbol)] = sample;
    mDelayLinePointer++;
    mDelayLinePointer = mDelayLinePointer % mTwiceSamplesPerSymbol;
}

bool AFSKSampleBuffer::getSymbol()
{
    int positive = 0;
    int start = mDelayLinePointer;
    int end = mDelayLinePointer + mSamplesPerSymbol - 1;
    for(int x = start; x <= end; x++)
    {
        if(mDelayLine[(size_t)x]) { positive++; }
    }
    return positive > mHalfIntegerSamplesPerSymbol;
}

// ------------------------------------------------------------------------------------------------
// AFSKTimingErrorDetector.java
// ------------------------------------------------------------------------------------------------
AFSKTimingErrorDetector::AFSKTimingErrorDetector(int samplesPerSymbol)
    : mBuffer((size_t)samplesPerSymbol, 0), mSymbolMiddle((float)(samplesPerSymbol - 1) / 2.0f) {}

void AFSKTimingErrorDetector::receive(bool sample)
{
    for(size_t i = 0; i + 1 < mBuffer.size(); i++) { mBuffer[i] = mBuffer[i + 1]; }
    mBuffer.back() = sample;
}

int AFSKTimingErrorDetector::getError()
{
    int count = 0, detected = 0;
    for(size_t x = 0; x + 1 < mBuffer.size(); x++)
    {
        if(mBuffer[x] != mBuffer[x + 1])
        {
            detected = (int)x + 1;
            count++;
            if(count > 1) { return 0; }
        }
    }
    if(count == 1)
    {
        if((float)detected > mSymbolMiddle) { return -((int)mBuffer.size() - detected); }
        return detected + 1;
    }
    return 0;
}

// ------------------------------------------------------------------------------------------------
// FloatAveragingBuffer.java
// ------------------------------------------------------------------------------------------------
float FloatAveragingBuffer::get(float value)
{
    float old = mBuffer[(size_t)mPointer];
    if(std::isinf(value) || std::isnan(value))
    {
        mAverage = mAverage - (old / mSize);
        mBuffer[(size_t)mPointer++] = 0.0f;
    }
    else
    {
        mAverage = mAverage - (old / mSize) + (value / mSize);
        mBuffer[(size_t)mPointer++] = value;
    }
    if(mPointer >= mSize) { mPointer = 0; }
    return mAverage;
}

// ------------------------------------------------------------------------------------------------
// AFSK1200Decoder.Correlator (+ ScalarRealOscillator / AbstractOscillator for the reference tones)
// ------------------------------------------------------------------------------------------------
Correlator::Correlator(double sampleRate, double frequency, int averagingPeriod, int correlationPeriod)
    : mAveraging(averagingPeriod), mReference((size_t)correlationPeriod), mDemodulated((size_t)correlationPeriod, 0.0f)
{
    // ScalarRealOscillator.generate(): float phase, advanced before use, wrapped with %= TWO_PI
    const double TWO_PI = 3.14159265358979323846 * 2.0;
    float anglePerSample = (float)(TWO_PI * frequency / sampleRate);
    float phase = 0.0f;
    for(int i = 0; i < correlationPeriod; i++)
    {
        phase += anglePerSample;
        phase = (float)std::fmod((double)phase, TWO_PI);
        mReference[(size_t)i] = (float)std::sin((double)phase);
    }
}

float Correlator::process(float sample)
{
    for(size_t i = 0; i + 1 < mDemodulated.size(); i++) { mDemodulated[i] = mDemodulated[i + 1]; }
    mDemodulated.back() = sample;
    float acc = 0.0f;
    for(size_t y = 0; y < mDemodulated.size(); y++) { acc += mDemodulated[y] * mReference[y]; }
    // absolute value: we do not care whether the signal is in phase with the reference
    return mAveraging.get(std::fabs(acc));
}

// ------------------------------------------------------------------------------------------------
// AFSK1200Decoder.java
// ------------------------------------------------------------------------------------------------
AFSK1200Decoder::AFSK1200Decoder(double inputRate, bool invert, SymbolSink sink)
    : mResampler(inputRate, SAMPLE_RATE),
      mMark(SAMPLE_RATE, MARK, AVERAGING_PERIOD, CORRELATION_PERIOD),
      mSpace(SAMPLE_RATE, SPACE, AVERAGING_PERIOD, CORRELATION_PERIOD),
      mSampleBuffer(SAMPLES_PER_SYMBOL, 1.0f / 3.0f),
      mTiming(SAMPLES_PER_SYMBOL),
      mNormal(!invert),
      mSink(std::move(sink))
{
}

void AFSK1200Decoder::push(float x)
{
    mResampler.push(x, [this](float y) { decode(y); });
}

void AFSK1200Decoder::decode(float x)
{
    float cm = mMark.process(x);
    float cs = mSpace.process(x);
    bool decision = cm > cs;          // 1200 = Mark (1), 1800 = Space (0)
    mSampleBuffer.receive(decision);
    mTiming.receive(decision);
    if(mSampleBuffer.hasSymbol())
    {
        bool sym = mSampleBuffer.getSymbol();
        mSink(mNormal ? sym : !sym);          // AFSK1200Decoder.dispatch()
        mSampleBuffer.resetAndAdjust((float)mTiming.getError());
    }
}

}  // namespace trunkfmt
