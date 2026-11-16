// compat/appglobal.h - NOT from QSSTV. Replaces QSSTV's application-wide header for the stand-alone HamDRM test-signal
// generator (hamdrm_txgen). QSSTV's real appglobal.h drags in Qt, the dispatcher, the sound system and the GUI; the
// transmit chain only needs the logging call, the LOG* category names and the unsigned-int type names Qt provides.
// Licence: GNU GPL version 3 or later (same as the helper).
#ifndef HAMDRM_TXGEN_APPGLOBAL_H
#define HAMDRM_TXGEN_APPGLOBAL_H

#include <cstdint>
#include <string>
#include <vector>
#include "appdefs.h"

typedef unsigned int uint;
typedef unsigned long ulong;
typedef uint32_t quint32;

// QSSTV's logging (addToLog(QString, category)) is dropped: the macro discards its arguments unevaluated, so the
// original call sites (some of them build QString objects) need no edit.
#define addToLog(...) ((void)0)
enum elogCategory { LOGDRMTX, LOGPERFORM, LOGFFT, LOGSOUND, LOGDRMMOT };

#endif
