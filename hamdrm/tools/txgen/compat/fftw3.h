// compat/fftw3.h - NOT from QSSTV. Forwards the <fftw3.h> include of QSSTV's matlib to the helper's own
// header-only FFTW stand-in (hamdrm/src/fftw3.h, written for the helper, not from FFTW). A relative path is used so
// that only this one header of hamdrm/src is visible to the generator (hamdrm/src also holds the receiver's own
// appdefs.h/drm.h/... which must not shadow the generator's). Licence: GNU GPL version 3 or later.
#ifndef HAMDRM_TXGEN_FFTW3_FORWARD_H
#define HAMDRM_TXGEN_FFTW3_FORWARD_H
#include "../../../src/fftw3.h"
#endif
