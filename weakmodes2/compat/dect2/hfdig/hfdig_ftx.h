// FoxSDR GPL helper "weakmodes2": replacement for OnAir's core/include/dect2/hfdig/hfdig_ftx.h, which declares the whole HF digital receiver
// (threads, telemetry, the FT8 decoder). The upstream files taken into this helper only need the decode record, so that is all that is here.
// The field names and meanings are OnAir's (fields the decoders never touch are kept so the upstream files compile unchanged).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dect2 {

struct FtxDecode {
    double slotUtc = 0;
    int mode = 0;            // ftx::Mode
    float snrDb = 0;         // in 2500 Hz, as WSJT-X
    float dt = 0;            // s, the start against the nominal one
    float hz = 0;            // audio frequency of the lowest tone
    std::string msg;
    bool mirrored = false;
    bool cq = false;
    std::string call, grid;  // the calling station and its locator when the message has them
    int dbm = -1;            // FST4W: the power sent
    int pass = 0;            // FST4: 0 one-symbol metric, 1 two coherent symbols, 2 four; Q65: the subtraction pass
    double dialHz = 0;
    int how = 0;
    bool early = false;
};

} // namespace dect2
