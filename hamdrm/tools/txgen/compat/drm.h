// compat/drm.h - NOT from QSSTV. Replaces QSSTV's drmrx/drm.h (the receiver's global header) for the stand-alone
// HamDRM test-signal generator: only the two globals the transmit chain reads/writes are declared (defined in
// compat/txglobals.cpp). Licence: GNU GPL version 3 or later (same as the helper).
#ifndef HAMDRM_TXGEN_DRM_H
#define HAMDRM_TXGEN_DRM_H

#include "appglobal.h"
#include <vector>

extern bool stopDRM;    // set by the MOT slide-show encoder when the whole object has been sent (+5 extra frames)
extern uint txTransportID; // MOT transport id chosen by CMOTDABEnc::SetMOTObject (from the file name, "ham code")

// hamdrm_txgen only: when non-null CMOTDABEnc::GetDataGroup() appends every data group it generates (bytes).
extern std::vector<std::vector<unsigned char> > *txGroupDump;

#endif
