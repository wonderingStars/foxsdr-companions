// appdefs.h - the constants of QSSTV's appdefs.h that the receive chain uses (hamdrm helper). Values as upstream.
#pragma once
#define BASESAMPLERATE 48000
#define SUBSAMPLINGFACTOR 4
#define RXSTRIPE 1024
#define FILTERPARAMTYPE double
#define SAMPLERATE (BASESAMPLERATE / SUBSAMPLINGFACTOR)
typedef double DSPFLOAT;
