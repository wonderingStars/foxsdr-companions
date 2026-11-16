// Port of sdrtrunk module/decode/lj1200: LJ1200Decoder, LJ1200MessageProcessor, LJ1200Message,
// LJ1200TransponderMessage, FunctionAndReplyCode(Identifier) and edac/CRCLJ.
// See common.h for the licence notice.
#pragma once
#include <functional>
#include <string>
#include "common.h"
#include "framer.h"

namespace trunkfmt {

// CRCLJ.checkAndCorrect(): CRC over the function and address fields (bits 32..63), single-bit repair.
CrcResult crcljCheckAndCorrect(BitMsg& message);

// Decoded tower message (LJ1200Message).
struct LJ1200Message
{
    CrcResult crc;
    std::string function;        // Function label, e.g. "4-ACTIVATE"
    int functionCode;            // 4-bit function value
    std::string address;         // 7 hex digits (28 bits)
    std::string replyCode;       // 5 characters, REPLY_CODE alphabet
    std::string vrc, lrc, crcField;
    std::string rawHex;          // all 80 bits as 20 hex digits
};

// Decoded transponder message (LJ1200TransponderMessage).  Upstream has no CRC for these.
struct LJ1200TransponderMessage
{
    std::string crcField;        // bits 64..79 read in upstream (reversed) order, 4 hex digits
    std::string rawHex;
};

LJ1200Message decodeLJ1200(BitMsg message);       // copies: CRC repair flips bits
LJ1200TransponderMessage decodeLJ1200Transponder(const BitMsg& message);
std::string lj1200FunctionLabel(int value, int replyCodeDigit3);

// LJ1200Decoder.java: two MessageFramers (tower sync 0x550F, transponder sync 0x2AD5), 80-bit messages.
class LJ1200Decoder
{
public:
    static constexpr size_t MESSAGE_LENGTH = 80;
    using TowerSink = std::function<void(const LJ1200Message&)>;
    using TransponderSink = std::function<void(const LJ1200TransponderMessage&)>;
    LJ1200Decoder(TowerSink tower, TransponderSink transponder);
    void process(bool symbol)   // LJ1200Decoder.process(boolean)
    {
        mTowerFramer.process(symbol);
        mTransponderFramer.process(symbol);
    }
    unsigned long long failedCrc = 0;    // tower messages with an uncorrectable CRC (dropped, as upstream)

private:
    MessageFramer mTowerFramer, mTransponderFramer;
    TowerSink mTower;
    TransponderSink mTransponder;
};

}  // namespace trunkfmt
