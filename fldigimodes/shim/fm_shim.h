// ----------------------------------------------------------------------------
// fm_shim.h -- FoxSDR helper: the small piece of fldigi's program environment that the modem sources expect.
//
// fldigi's modem classes call into its user interface (progdefaults, progStatus, REQ(...) queued GUI calls,
// put_rx_char, set_scope, the waterfall pointer wf ...). This helper has none of that. This header, and the
// one-line headers beside it that include it, give those names a definition that does nothing, or that hands the
// decoded character to the helper's output (fm_rx_char_sink). It is written for this helper and is not fldigi code;
// it is distributed under the same licence as the rest of the helper (GPL-3.0-only).
// ----------------------------------------------------------------------------
#ifndef FM_SHIM_H
#define FM_SHIM_H

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "fm_modes.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef CLAMP
#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#endif
#define likely(x) (x)
#define unlikely(x) (x)
#define TWOPI (2.0 * M_PI)

// ---- the helper's sinks ------------------------------------------------------------------------------------
// Every decoded character of the running modem arrives here (set by the helper's main program).
typedef void (*fm_rx_char_sink_t)(int c);
extern fm_rx_char_sink_t fm_rx_char_sink;
// A picture transmission was seen and its pixels were skipped (MFSK, THOR).
void fm_picture_skipped(int width, int height, bool colour);

inline void put_rx_char(unsigned int c, int = 0)
{
    if (fm_rx_char_sink)
        fm_rx_char_sink((int)c);
}
inline void put_sec_char(int) {}
inline void put_echo_char(unsigned int, int = 0) {}
inline void put_status(const char *, double = 0, int = 0) {}
inline void put_Status1(const char *, double = 0, int = 0) {}
inline void put_Status2(const char *, double = 0, int = 0) {}
template <class T> inline void put_MODEstatus(T, ...) {}
inline void put_Bandwidth(int) {}
inline void put_freq(double) {}
#define STATUS_CLEAR 0
#define STATUS_KEEP 1

// fldigi queues calls into the GUI thread with REQ(...): there is no GUI thread here.
#define REQ(...) ((void)0)
#define REQ_SYNC(...) ((void)0)
#define REQ_FLUSH(...) ((void)0)
#define GET_THREAD_ID() 0

// ---- logging -----------------------------------------------------------------------------------------------
namespace debug {
enum { LOG_MODEM = 0 };
}
#define LOG_FILE_SOURCE(x)
#define LOG_INFO(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define LOG_VERBOSE(...) ((void)0)
#define LOG_PERROR(...) ((void)0)

// ---- configuration and status: the values fldigi starts with (src/include/configuration.h, status.cxx) -----------
struct fm_progdefaults {
    bool StartAtSweetSpot = false;
    int PSKsweetspot = 1000;
    bool Pskmails2nreport = false;
    bool slowcpu = true;
    // THOR
    double THOR_BW = 2.0;
    bool THOR_FILTER = true;
    std::string THORsecText;
    double ThorCWI = 0.0;
    bool THOR_PREAMBLE = true;
    bool THOR_SOFTSYMBOLS = true;
    bool THOR_SOFTBITS = true;
    std::string myCall;
    // MT63
    bool mt63_8bit = true;
    bool mt63_rx_integration = false;
};
struct fm_progstatus {
    double carrier = 0;
    bool sqlonoff = true;
    double sldrSquelchValue = 5.0;
    bool afconoff = true;
};
extern fm_progdefaults progdefaults;
extern fm_progstatus progStatus;
#define mailserver false
#define mailclient false

// ---- the waterfall: the modems only ask where the carrier is -----------------------------------------------------
struct fm_waterfall {
    double carrier = 1500;
    double Carrier() const { return carrier; }
    bool Reverse() const { return false; }
    bool USB() const { return true; }
};
extern fm_waterfall *wf;

// ---- sound-card constants and misc -------------------------------------------------------------------------------
#define SCBLOCKSIZE 512
#define SOH 1
#define EOT 4
#define BENCHMARK_MODE 0

void MilliSleep(long msecs);

struct Digiscope {
    enum scope_mode { SCOPE, PHASE, RTTY, DOMDATA, BLANK, XHAIRS };
};
inline void set_scope_mode(Digiscope::scope_mode) {}
inline void set_scope(double *, int, bool = false) {}

#endif
