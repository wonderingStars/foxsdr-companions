// appglobal.h - replacement for QSSTV's appglobal.h (hamdrm helper). NOT from QSSTV.
// QSSTV's global application header pulls in Qt, the logger and the configuration. The receive chain only needs
// addToLog() (a debug log, dropped here: the macro discards its arguments unevaluated), the type `uint`, and the
// sample-rate constants of appdefs.h.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef _UINT_DEFINED_HAMDRM
#define _UINT_DEFINED_HAMDRM
typedef unsigned int uint;
#endif
typedef unsigned short ushort;
typedef std::uint32_t quint32;
typedef std::uint16_t quint16;
#define addToLog(...) ((void)0)
#include "appdefs.h"
