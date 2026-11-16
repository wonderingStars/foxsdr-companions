// Port of sdrtrunk module/decode/mpt1327: MPT1327Decoder, MPT1327MessageProcessor, MPT1327Message (incl. its inner
// enums SDMFormat, IdentType, Descriptor, Slots, SystemDefinition, MPTMessageType) and Sync.
// The MPT1327 block CRC is upstream's CRCFleetsync (CRC-15 + parity), see fleetsync2.cpp.
// See common.h for the licence notice.
#pragma once
#include <functional>
#include <string>
#include "common.h"
#include "framer.h"

namespace trunkfmt {

struct MPT1327Message
{
    bool valid = false;           // block 1 passed or was corrected
    std::string typeCode;         // short code of MPTMessageType, e.g. "GTC", "ALH", "HEAD_PLUS2"
    std::string typeText;         // MPTMessageType description, e.g. "GTC - Goto Channel"
    std::string parity;           // "[" + one character per block + "]": '*' pass, 'C' corrected, 'f'/'p' fail, '-' not checked
    int prefix = 0, ident1 = 0, ident2 = 0, channel = 0;
    std::string fromId, toId;     // "PPP-IIII" or a named ident
    std::string siteId;           // empty when the message has none
    std::string text;             // MPT1327Message.getMessage()
    std::string rawHex;
};

MPT1327Message decodeMpt1327(BitMsg message);

// MPT1327Decoder.java: a control-channel framer and a traffic-channel framer, 350-bit messages.
class MPT1327Decoder
{
public:
    static constexpr size_t MESSAGE_LENGTH = 350;
    using Sink = std::function<void(const MPT1327Message&, bool controlSync)>;
    MPT1327Decoder(bool french, Sink sink);
    void process(bool symbol)
    {
        mControl.process(symbol);
        mTraffic.process(symbol);
    }
    unsigned long long invalid = 0;     // framed messages whose block 1 failed its check (dropped)

private:
    MessageFramer mControl, mTraffic;
    Sink mSink;
};

}  // namespace trunkfmt
