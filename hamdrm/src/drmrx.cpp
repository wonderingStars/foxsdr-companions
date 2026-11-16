// drmrx.cpp - see drmrx.h. Port of QSSTV src/drmrx/drmrx.cpp (GNU GPL), Qt events removed.
#include "drmrx.h"
#include "drm.h"
#include "drmproto.h"
#include "sourcedecoder.h"


drmRx::drmRx()
{
    srcDecoder = new sourceDecoder;
    demodulatorPtr = new demodulator;
    demodulatorPtr_ = demodulatorPtr;
    n = im = 0;
    rRation = 1.0f;
    memset(resamp_signal, 0, sizeof resamp_signal);
}

drmRx::~drmRx()
{
    delete srcDecoder;
    srcDecoder = nullptr;
    delete demodulatorPtr;
    demodulatorPtr = nullptr;
}

void drmRx::init()
{
    avgSNR = 0;
    avgSNRAvailable = false;
    n = DRMBUFSIZE;
    /* initialisations */
    demodulatorPtr->init();
    initGetmode(n / 4);
    rRation = 1.000;
    samplerate_offset_estimation = 0.0;
    runstate = RUN_STATE_POWER_ON; /* POWER_ON */
    channel_decoding();
    runstate = RUN_STATE_INIT; /* INIT */
    channel_decoding();
    runstate = RUN_STATE_FIRST;  /* FIRSTRUN */
    runstate = RUN_STATE_NORMAL; /* NORMAL RUN */
    srcDecoder->init();
}

void drmRx::run(const double *dataPtr)
{
    float temp = WMERFAC;
    if (temp < 0)
        temp = 0;
    if (avgSNRAvailable)
        avgSNR = (float)((1 - 0.05) * avgSNR + 0.05 * temp);

    if (input_samples_buffer_request == 0)
        demodulatorPtr->demodulate(resamp_signal, 0);
    iqFilter.process(dataPtr, resamp_signal, RXSTRIPE);
    im = RXSTRIPE;
    demodulatorPtr->demodulate(resamp_signal, im);
}
