// Port of sdrtrunk module/decode/ltrstandard (LTRStandardDecoder, LTRStandardMessageProcessor, message/*),
// module/decode/ltrnet (LTRNetDecoder, LTRNetMessageProcessor, message/isw/*, message/osw/*) and edac/CRCLTR.
// LTR Standard and LTR-Net share one bit stream; the decoders are separate here, as upstream.
// See common.h for the licence notice.
#pragma once
#include <functional>
#include <map>
#include <string>
#include "common.h"
#include "framer.h"

namespace trunkfmt {

enum class Direction { OSW, ISW };

// CRCLTR.check(): 7-bit checksum over bits 9..32, transmitted in bits 33..39.  `inverted` is set for PASSED_INV.
CrcResult crcltrCheck(const BitMsg& msg, Direction d, bool* inverted = nullptr);
int crcltrTransmitted(const BitMsg& msg);

// Frames 40-bit LTR words in both directions (sync 0x158 outbound, 0xA7 inbound).
class LTRStandardDecoder
{
public:
    static constexpr size_t MESSAGE_LENGTH = 40;
    // type: CALL, CALL_END, IDLE, UNKNOWN
    struct Message
    {
        Direction direction;
        bool inverted;               // CRC passed in inverted form (ISW)
        std::string type;
        int area, channel, home, group, free;
        std::string talkgroup;       // "HH-GGG" (LTRTalkgroup.formatted(): home channel and group, no area)
        std::string rawHex;
    };
    using Sink = std::function<void(const Message&)>;
    explicit LTRStandardDecoder(Sink sink);
    void process(bool symbol) { mOsw.process(symbol); mIsw.process(symbol); }

private:
    MessageFramer mOsw, mIsw;
    Sink mSink;
    void receive(const BitMsg& m, Direction d);
};

class LTRNetDecoder
{
public:
    static constexpr size_t MESSAGE_LENGTH = 40;
    struct Message
    {
        Direction direction;
        std::string type;            // LtrNetMessageType name
        std::string crc;             // "pass" or "pass-inverted"
        std::string json;            // type specific fields, already as `,"k":v` pairs (no leading comma)
        std::string text;            // the toString() of the upstream message, without its MSG: part
        std::string rawHex;
    };
    using Sink = std::function<void(const Message&)>;
    explicit LTRNetDecoder(Sink sink);
    void process(bool symbol) { mOsw.process(symbol); mIsw.process(symbol); }
    unsigned long long failedCrc = 0;

private:
    MessageFramer mOsw, mIsw;
    Sink mSink;
    void receive(BitMsg m, Direction d);
    // LTRNetMessageProcessor state: the last frequency halves per channel
    std::map<int, long long> mRxHigh, mRxLow, mTxHigh, mTxLow;
};

}  // namespace trunkfmt
