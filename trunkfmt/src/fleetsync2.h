// Port of sdrtrunk module/decode/fleetsync2: Fleetsync2Decoder, Fleetsync2MessageProcessor, message/Fleetsync2Message,
// AutomaticNumberIdentification, Status, LocationReport, identifier/FleetsyncIdentifier, FleetsyncMessageType;
// edac/CRCFleetsync.  See common.h for the licence notice.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "common.h"
#include "framer.h"

namespace trunkfmt {

struct Fleetsync2Message
{
    enum Kind { ACKNOWLEDGE, ANI, EMERGENCY, GPS, LONE_WORKER_EMERGENCY, PAGING, STATUS, UNKNOWN } kind = UNKNOWN;
    std::string kindLabel;                 // FleetsyncMessageType label
    std::string crc;                       // one CRC abbreviation per block checked: '*' pass, 'C' corrected, 'f' CRC fail, 'p' parity fail
    bool valid = false;                    // block 1 passed or was corrected (Fleetsync2Message.isValid())
    int fromFleet = 0, fromIdent = 0, toFleet = 0, toIdent = 0;
    std::string from, to;                  // "FFF:IIII"
    bool emergency = false, loneWorker = false, paging = false, acknowledge = false, ani = false, fleetExtension = false;
    int status = -1;                       // STATUS messages: 7-bit status value
    // GPS (LocationReport)
    double latitude = 0, longitude = 0, heading = 0, speed = 0;
    int gpsHour = 0, gpsMinute = 0, gpsSecond = 0, gpsYear = 0, gpsMonth = 0, gpsDay = 0;
    bool hasHeading = false;
    std::string rawHex;                    // the 537 bits as hex
};

Fleetsync2Message decodeFleetsync2(BitMsg message);

// check a 64-bit block at [start,end) of `message` and repair a single-bit error in place
CrcResult fleetsyncDetectAndCorrect(BitMsg& message, int start, int end);

// CRCFleetsync.check / findBitErrors on a 64-bit block (48 data, 15 CRC, 1 parity).
CrcResult fleetsyncCheck(const BitMsg& block);

// Fleetsync2Decoder.java: one MessageFramer, 537-bit messages.
class Fleetsync2Decoder
{
public:
    static constexpr size_t MESSAGE_LENGTH = 537;
    using Sink = std::function<void(const Fleetsync2Message&)>;
    explicit Fleetsync2Decoder(Sink sink);
    void process(bool symbol) { mFramer.process(symbol); }
    unsigned long long invalid = 0;        // messages whose first block failed (dropped, as Fleetsync2DecoderState does)

private:
    MessageFramer mFramer;
    Sink mSink;
};

}  // namespace trunkfmt
