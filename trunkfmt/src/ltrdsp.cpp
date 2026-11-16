// See ltrdsp.h / common.h for the notices.
#include "ltrdsp.h"
#include <cmath>

namespace trunkfmt {

// LTRDecoder constants
static const double SLOPE_CALCULATION_SUM_XX = 1637.999992057681;   // always the same for 27 evenly spaced samples
static const float BAUD_LENGTH = 8000.0f / 300.0f;
static const float SLOPE_THRESHOLD = 0.0048f;
static const int MAX_ONES_SEQUENCE = 18;
static const int POST_TRANSITION_SAMPLES_TO_SKIP = 26;
static const int SLOPE_CALCULATION_LENGTH = 27;
static const int SYMBOL_TRANSITION_IDEAL_MIN = 11;
static const int SYMBOL_TRANSITION_IDEAL_MAX = 16;

// Zeroth-order modified Bessel function for the Kaiser window
static double bessel0(double x)
{
    double sum = 1.0, term = 1.0;
    for(int k = 1; k < 50; k++)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

LTRDecoder::LTRDecoder(double inputRate, SymbolSink sink) : mResampler(inputRate, 8000.0), mSink(std::move(sink))
{
    // Upstream: lowPassBuilder, 8000 Hz, odd length, pass band to 300 Hz (ripple 0.01), stop band from 500 Hz (ripple 0.03).
    // Replacement: Kaiser window, beta 5.65 (about 60 dB), cut-off in the middle of the transition band (400 Hz), 145 taps.
    const int N = 145;
    const double PI = 3.14159265358979323846, beta = 5.65, fc = 400.0 / 8000.0;
    mTaps.resize((size_t)N);
    double sum = 0.0;
    for(int n = 0; n < N; n++)
    {
        double m = n - (N - 1) / 2.0;
        double s = (m == 0.0) ? 2.0 * fc : std::sin(2.0 * PI * fc * m) / (PI * m);
        double r = 2.0 * n / (N - 1) - 1.0;
        double w = bessel0(beta * std::sqrt(1.0 - r * r)) / bessel0(beta);
        mTaps[(size_t)n] = (float)(s * w);
        sum += s * w;
    }
    for(auto& t : mTaps) { t = (float)(t / sum); }        // unity gain at DC
    mInput.assign((size_t)N, 0.0f);
    mFiltered.assign((size_t)SLOPE_CALCULATION_LENGTH, 0.0f);       // mResidual starts as zeros
}

void LTRDecoder::push(float x)
{
    mResampler.push(x, [this](float y) { process(y); });
}

float LTRDecoder::calculateSlope() const
{
    double xbar = 0;
    double ybar = mFiltered[0];
    double sumXY = 0;
    double fact1, dx, dy;
    for(int x = 1; x < SLOPE_CALCULATION_LENGTH; x++)
    {
        fact1 = 1.0f + x;
        dx = x - xbar;
        dy = mFiltered[(size_t)x] - ybar;
        sumXY += dx * dy * ((float)x / (1.0f + (float)x));
        xbar += dx / fact1;
        ybar += dy / fact1;
    }
    return (float)(sumXY / SLOPE_CALCULATION_SUM_XX);
}

void LTRDecoder::process(float x)
{
    // low-pass filter
    mInput.pop_front();
    mInput.push_back(x);
    float acc = 0.0f;
    for(size_t i = 0; i < mTaps.size(); i++) { acc += mTaps[i] * mInput[mTaps.size() - 1 - i]; }
    mFiltered.pop_front();
    mFiltered.push_back(acc);

    // LTRDecoder.receive(): the per-sample body
    if(mSamplesToSkip > 0)
    {
        mSamplesToSkip--;
    }
    else
    {
        float slope = calculateSlope();
        int timingAdjust = 0;
        if(mSymbol)
        {
            // look for the crest of the slope at its lowest point below the negative threshold
            if(slope > mMaxSlope && mMaxSlope < -SLOPE_THRESHOLD)
            {
                mSymbol = false;
                if(mBaudCounter < SYMBOL_TRANSITION_IDEAL_MIN) { timingAdjust = 1; }
                else if(mBaudCounter > SYMBOL_TRANSITION_IDEAL_MAX) { timingAdjust = -1; }
                else { timingAdjust = 0; }
                mBaudCounter += (float)timingAdjust;
                mSamplesToSkip = POST_TRANSITION_SAMPLES_TO_SKIP + timingAdjust;
            }
            else if(slope < mMaxSlope)
            {
                mMaxSlope = slope;
            }
        }
        else
        {
            // look for the crest of the slope at its highest point above the positive threshold
            if(slope < mMaxSlope && mMaxSlope > SLOPE_THRESHOLD)
            {
                mSymbol = true;
                if(mBaudCounter < SYMBOL_TRANSITION_IDEAL_MIN) { timingAdjust = 1; }
                else if(mBaudCounter > SYMBOL_TRANSITION_IDEAL_MAX) { timingAdjust = -1; }
                else { timingAdjust = 0; }
                mBaudCounter += (float)timingAdjust;
                mSamplesToSkip = 26 + timingAdjust;
            }
            else if(slope > mMaxSlope)
            {
                mMaxSlope = slope;
            }
        }
    }

    mBaudCounter++;
    if(mBaudCounter > BAUD_LENGTH)
    {
        mSink(mSymbol);
        mBaudCounter -= BAUD_LENGTH;
        // a long string of ones is taken as loss of signal: force the state back to zero
        if(mSymbol)
        {
            mExcessiveOneSequenceCounter++;
            if(mExcessiveOneSequenceCounter > MAX_ONES_SEQUENCE)
            {
                mSymbol = false;
                mMaxSlope = -1.0f;
                mExcessiveOneSequenceCounter = 0;
            }
        }
        else
        {
            mExcessiveOneSequenceCounter = 0;
        }
    }
}

}  // namespace trunkfmt
