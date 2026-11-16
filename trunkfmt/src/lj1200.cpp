// See lj1200.h / common.h for the notices.
#include "lj1200.h"

namespace trunkfmt {

// CRCLJ.CHECKSUMS: per-bit CRC contributions for message bits 32..63 (function LSB..address MSB, i.e.
// bit 32 first) followed by the 16 single-bit CRC-field errors.  CRC-16, polynomial 0x6F63, initial fill 0.
static const int CHECKSUMS[48] = {
    0x26EA, 0x1375, 0xBE0B, 0xE8B4,                                                  // Function 0 (LSB) .. 3 (MSB)
    0x745A, 0x3A2D, 0xAAA7, 0xE2E2, 0x7171, 0x8F09, 0xF035, 0xCFAB, 0xD064, 0x6832,  // Address 0 .. 9
    0x3419, 0xADBD, 0xE16F, 0xC706, 0x6383, 0x8670, 0x4338, 0x219C, 0x10CE, 0x0867,  // Address 10 .. 19
    0xB382, 0x59C1, 0x9B51, 0xFA19, 0xCABD, 0xD2EF, 0xDEC6, 0x6F63,                  // Address 20 .. 27 (MSB)
    0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100,                  // CRC 0 .. 15
    0x0080, 0x0040, 0x0020, 0x0010, 0x0008, 0x0004, 0x0002, 0x0001};

static const int MESSAGE_START = 32;
static const int CRC_START = 64;

// CRCLJ.getChecksum()
static int getChecksum(const BitMsg& msg)
{
    int v = 0;
    for(int x = 0; x < 16; x++)
    {
        if(msg.get((size_t)(x + CRC_START))) { v += 1 << (15 - x); }
    }
    return v;
}

// CRCLJ.checkAndCorrect()
CrcResult crcljCheckAndCorrect(BitMsg& message)
{
    int calculated = 0;
    for(int i = MESSAGE_START; i < CRC_START; i++)
    {
        if(message.get((size_t)i)) { calculated ^= CHECKSUMS[i - MESSAGE_START]; }
    }
    int checksum = getChecksum(message);
    if(calculated == checksum) { return CrcResult::PASSED; }

    int err = calculated ^ checksum;
    for(int x = 0; x < 48; x++)           // CRCLJ.findBitErrors(): one bit errors only
    {
        if(CHECKSUMS[x] == err)
        {
            message.flip((size_t)(MESSAGE_START + x));
            return CrcResult::CORRECTED;
        }
    }
    return CrcResult::FAILED_CRC;
}

// LJ1200Message field index lists (big-endian listing: first index is the MSB of the value)
static const int VRC[] = {23, 22, 21, 20, 19, 18, 17, 16};
static const int LRC[] = {31, 30, 29, 28, 27, 26, 25, 24};
static const int FUNCTION[] = {35, 34, 33, 32};
static const int ADDRESS[] = {63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43,
                              42, 41, 40, 39, 38, 37, 36};
static const int REPLY_1[] = {39, 38, 37, 36, 43};
static const int REPLY_2[] = {42, 41, 40, 47, 46};
static const int REPLY_3[] = {45, 44, 51, 50, 49};
static const int REPLY_4[] = {48, 55, 54, 53, 52};
static const int REPLY_5[] = {59, 58, 57, 56, 63};
static const int MESSAGE_CRC[] = {79, 78, 77, 76, 75, 74, 73, 72, 71, 70, 69, 68, 67, 66, 65, 64};
static const int TRANSPONDER_CRC[] = {64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79};

// LJ1200Message.REPLY_CODE
static const char* REPLY_CODE[32] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "C", "D", "E", "F", "G", "H",
                                     "J", "K", "L", "M", "N", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y"};

// LJ1200Message.Function.fromValue()
std::string lj1200FunctionLabel(int value, int replyCodeDigit3)
{
    switch(value)
    {
        case 0: return "0-UNKNOWN";
        case 1: return (replyCodeDigit3 == 31) ? "1Y-SITE ID" : "1-SPEED UP";   // 'Y' middle character
        case 2: return "2-TOWER TEST";
        case 3: return "3-DEACTIVATE";
        case 4: return "4-ACTIVATE";
        case 5: return "5-TRANSPONDER TEST REPLY";
        case 6: return "6-TRANSPONDER TEST COMMAND";
        case 7: return "7-QUIET COMMAND";
        case 8: return "8-UNKNOWN";
        case 9: return "9-UNKNOWN";
        case 10: return "A-UNKNOWN";
        case 11: return "B-UNKNOWN";
        case 12: return "C-ALT-DEACTIVATE";
        case 13: return "D-UNKNOWN";
        case 14: return "E-UNKNOWN";
        case 15: return "F-TRACK PULSE";
        default: return "UNKNOWN";
    }
}

LJ1200Message decodeLJ1200(BitMsg m)
{
    LJ1200Message out;
    out.crc = crcljCheckAndCorrect(m);                 // LJ1200Message.checkCRC()
    int fn = (int)m.getInt(FUNCTION);
    int r3 = (int)m.getInt(REPLY_3);
    out.functionCode = fn;
    out.function = lj1200FunctionLabel(fn, r3);        // getFunction()
    out.address = hexN(m.getInt(ADDRESS), 7);          // getAddress()
    out.replyCode = std::string(REPLY_CODE[m.getInt(REPLY_1)]) + REPLY_CODE[m.getInt(REPLY_2)] +
                    REPLY_CODE[m.getInt(REPLY_3)] + REPLY_CODE[m.getInt(REPLY_4)] + REPLY_CODE[m.getInt(REPLY_5)];
    out.vrc = hexN(m.getInt(VRC), 2);
    out.lrc = hexN(m.getInt(LRC), 2);
    out.crcField = hexN(m.getInt(MESSAGE_CRC), 4);
    out.rawHex = m.toHex();
    return out;
}

LJ1200TransponderMessage decodeLJ1200Transponder(const BitMsg& m)
{
    LJ1200TransponderMessage out;
    out.crcField = hexN(m.getInt(TRANSPONDER_CRC), 4);  // LJ1200TransponderMessage.getCRC()
    out.rawHex = m.toHex();
    return out;
}

LJ1200Decoder::LJ1200Decoder(TowerSink tower, TransponderSink transponder)
    : mTowerFramer(syncLJ1200(), MESSAGE_LENGTH),
      mTransponderFramer(syncLJ1200Transponder(), MESSAGE_LENGTH),
      mTower(std::move(tower)),
      mTransponder(std::move(transponder))
{
    // LJ1200MessageProcessor.receive(): the sync field selects the message class.  Each framer matched its own
    // sync pattern, so the check on the first 16 bits is always true here.
    mTowerFramer.setListener([this](const BitMsg& m) {
        LJ1200Message d = decodeLJ1200(m);
        // LJ1200DecoderState.receive(): only messages whose CRC passed or was corrected are used
        if(d.crc == CrcResult::PASSED || d.crc == CrcResult::CORRECTED) { if(mTower) { mTower(d); } }
        else { failedCrc++; }
    });
    mTransponderFramer.setListener([this](const BitMsg& m) {
        if(mTransponder) { mTransponder(decodeLJ1200Transponder(m)); }
    });
}

}  // namespace trunkfmt
