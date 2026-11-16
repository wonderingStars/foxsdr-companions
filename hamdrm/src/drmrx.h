// drmrx.h - HamDRM receiver driver without Qt (hamdrm helper).
// Replaces class drmRx of QSSTV src/drmrx/drmrx.h / drmrx.cpp (Copyright Johan Maes ON4QZ and the QSSTV authors, GNU GPL).
// Same sequence of calls as upstream: init() = demodulator init, initGetmode, channel_decoding power-on and init, source decoder init;
// run() = one stripe of RXSTRIPE (1024) audio samples at 12000 Hz through the Hilbert filter and the demodulator.
// The Qt GUI events (status bar, SNR display) are dropped; the smoothed SNR (avgSNR) is kept because the file event reports it.
#pragma once
#include "appglobal.h"
#include "demodulator.h"
#include "drm_hilbert.h"
#include "drmdefs.h"

class drmRx
{
public:
    drmRx();
    ~drmRx();
    void init();
    void run(const double *dataPtr); // RXSTRIPE samples
    const demodulator *demod() const { return demodulatorPtr_; }

private:
    int n, im;
    float rRation;
    float resamp_signal[2 * DRMBUFSIZE];
    drmHilbertFilter iqFilter;
    demodulator *demodulatorPtr_;
};
