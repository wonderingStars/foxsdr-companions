// drm_hilbert.h - the receive Hilbert (I/Q) filter of QSSTV, without Qt (hamdrm helper).
//
// Port of class drmHilbertFilter (src/dsp/filters.cpp, filters.h) and of filter::processIQ(), filter::init() and
// filter::allocate() (src/dsp/filter.cpp) from QSSTV (https://github.com/ON4QZ/QSSTV, commit 31c323c), which are
//   Copyright (C) 2004 by Johan Maes - ON4QZ, GNU General Public License version 2 or (at your option) any later version.
// The 153-tap coefficient table (drm_hilbert_coef.inc) is copied unchanged from src/dsp/filterparam.cpp (drmHilbertCoef,
// DRMHILBERTTAPS 153, DRMHILBERTGAIN 1.569749, "blFIR Hilbert, localSamplingrate = 12000, corner frequency 1000, beta 0.5").
// Changes: only the buffer handling (std::vector instead of new[]); the arithmetic is the same.
#pragma once
#include <cstring>
#include <vector>

#define DRMHILBERTTAPS 153
#define DRMHILBERTGAIN 1.569749

class drmHilbertFilter
{
public:
    drmHilbertFilter() : buf(DRMHILBERTTAPS, 0.0) {}
    // dataPtr: real input samples (12000 Hz); outputPtr: dataLength complex pairs (real part first), as upstream
    void process(const double *dataPtr, float *outputPtr, unsigned dataLength)
    {
        static const double coef[DRMHILBERTTAPS] = {
#include "drm_hilbert_coef.inc"
        };
        const unsigned nZeroes = DRMHILBERTTAPS - 1;
        const size_t bufSize = nZeroes * sizeof(double);
        for (unsigned k = 0; k < dataLength; k++)
        {
            double resQ = 0;
            const double *cf1 = coef;
            const double *fp1 = buf.data();
            memmove(buf.data() + 1, buf.data(), bufSize); // newest at index 0
            buf[0] = dataPtr[k];
            for (unsigned i = 0; i <= nZeroes; i++, fp1++, cf1++)
                resQ += (*fp1) * (*cf1);
            outputPtr[2 * k + 1] = (float)buf[(nZeroes + 1) / 2]; // just delay
            outputPtr[2 * k] = (float)(resQ / DRMHILBERTGAIN);
        }
    }

private:
    std::vector<double> buf;
};
