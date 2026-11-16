// See ltr.h / common.h for the notices.
#include "ltr.h"

namespace trunkfmt {

// CRCLTR.sCHECKSUMS: bit 9 (area) .. bit 32 (free 0)
static const int LTR_CHECKSUMS[24] = {
    0x38,                                // Area
    0x1C, 0x0E, 0x46, 0x23, 0x51,        // Channel 4..0
    0x68, 0x75, 0x7A, 0x3D, 0x1F,        // Home 4..0
    0x4F, 0x26, 0x52, 0x29, 0x15, 0x0B, 0x45, 0x62,    // Group 7..0
    0x31, 0x19, 0x0D, 0x07, 0x43};       // Free 4..0

static int calculatedChecksum(const BitMsg& msg)       // CRCLTR.getCalculatedChecksum()
{
    int calculated = 0;
    for(int i = 9; i < 33; i++) { if(msg.get((size_t)i)) { calculated ^= LTR_CHECKSUMS[i - 9]; } }
    return calculated;
}

int crcltrTransmitted(const BitMsg& msg)                // CRCLTR.getTransmittedChecksum()
{
    int v = 0;
    for(int x = 0; x < 7; x++) { if(msg.get((size_t)(x + 33))) { v += 1 << (6 - x); } }
    return v;
}

CrcResult crcltrCheck(const BitMsg& msg, Direction d, bool* inverted)
{
    if(inverted) { *inverted = false; }
    int calculated = calculatedChecksum(msg), transmitted = crcltrTransmitted(msg);
    if(d == Direction::OSW)
    {
        return calculated == transmitted ? CrcResult::PASSED : CrcResult::FAILED_CRC;
    }
    if((calculated ^ 127) == transmitted)
    {
        if(inverted) { *inverted = true; }
        return CrcResult::PASSED;                       // PASSED_INV
    }
    return calculated == transmitted ? CrcResult::PASSED : CrcResult::FAILED_CRC;
}

// LTRMessage field positions
static const int AREA_BIT = 9;
static int fArea(const BitMsg& m) { return m.get(AREA_BIT) ? 1 : 0; }
static int fChannel(const BitMsg& m) { return (int)m.getRange(10, 14); }
static int fHome(const BitMsg& m) { return (int)m.getRange(15, 19); }
static int fGroup(const BitMsg& m) { return (int)m.getRange(20, 27); }
static int fFree(const BitMsg& m) { return (int)m.getRange(28, 32); }

// LTRTalkgroup.formatted(): "%02d-%03d" of (home channel, talkgroup)
static std::string ltrTalkgroup(const BitMsg& m)
{
    int value = (fArea(m) << 13) + (fHome(m) << 8) + fGroup(m);
    char b[32];
    snprintf(b, sizeof b, "%02d-%03d", (value & 0x1F00) >> 8, value & 0xFF);
    return b;
}

static void flipAll(BitMsg& m) { for(size_t i = 0; i < 40; i++) { m.flip(i); } }    // BinaryMessage.flip(0, 40)

// ---------------------------------------------------------------------------------------------------------
// LTR Standard
// ---------------------------------------------------------------------------------------------------------
static bool validChannel(int c) { return 1 <= c && c <= 20; }
static bool validFree(int c) { return 0 <= c && c <= 20; }

LTRStandardDecoder::LTRStandardDecoder(Sink sink)
    : mOsw(syncLtrOsw(), MESSAGE_LENGTH), mIsw(syncLtrIsw(), MESSAGE_LENGTH), mSink(std::move(sink))
{
    mOsw.setListener([this](const BitMsg& m) { receive(m, Direction::OSW); });
    mIsw.setListener([this](const BitMsg& m) { receive(m, Direction::ISW); });
}

// LTRStandardMessageProcessor.receive()
void LTRStandardDecoder::receive(const BitMsg& framed, Direction d)
{
    BitMsg m = framed;
    if(d == Direction::ISW) { flipAll(m); }      // the inbound word is the bit-flipped version of the outbound word
    bool inv = false;
    if(crcltrCheck(m, d, &inv) != CrcResult::PASSED) { return; }
    int channel = fChannel(m), home = fHome(m), free = fFree(m), group = fGroup(m);
    Message o;
    o.direction = d;
    o.inverted = inv;
    if(validChannel(channel) && validChannel(home) && validFree(free))
    {
        o.type = (channel == free && group == 255) ? "IDLE" : "CALL";
    }
    else if(channel == 31 && validChannel(home) && validFree(free))
    {
        o.type = "CALL_END";
    }
    else
    {
        o.type = "UNKNOWN";
    }
    o.area = fArea(m); o.channel = channel; o.home = home; o.group = group; o.free = free;
    o.talkgroup = ltrTalkgroup(m);
    o.rawHex = m.toHex();
    if(mSink) { mSink(o); }
}

// ---------------------------------------------------------------------------------------------------------
// LTR-Net
// ---------------------------------------------------------------------------------------------------------
static const int CHANNEL_FREQUENCY_MESSAGE_TYPE_BIT = 20;
static const int CHANNEL_MAP_MESSAGE_TYPE_BIT = 17;

static std::string oswType(const BitMsg& m)     // LtrNetOswMessage.getMessageType()
{
    int channel = fChannel(m), home = fHome(m);
    if(home != 31 && (channel > 20 || home > 20))
    {
        switch(channel)
        {
            case 17: return "OSW_REGISTRATION_ACCEPT";
            case 18: return "OSW_SITE_ID";
            case 24: return m.get(CHANNEL_FREQUENCY_MESSAGE_TYPE_BIT) ? "OSW_TRANSMIT_FREQUENCY_HIGH" : "OSW_TRANSMIT_FREQUENCY_LOW";
            case 25: return m.get(CHANNEL_FREQUENCY_MESSAGE_TYPE_BIT) ? "OSW_RECEIVE_FREQUENCY_HIGH" : "OSW_RECEIVE_FREQUENCY_LOW";
            case 26: return "OSW_NEIGHBOR_ID";
            case 28: return m.get(CHANNEL_MAP_MESSAGE_TYPE_BIT) ? "OSW_CHANNEL_MAP_HIGH" : "OSW_CHANNEL_MAP_LOW";
            case 31: return "OSW_CALL_END";
            default: break;
        }
    }
    else
    {
        return fGroup(m) == 255 ? "OSW_SYSTEM_IDLE" : "OSW_CALL_START";
    }
    return "OSW_UNKNOWN";
}

static std::string iswType(const BitMsg& m)     // LtrNetIswMessage.getMessageType()
{
    int channel = fChannel(m);
    if(channel == 31) { return "ISW_CALL_END"; }
    if(channel > 20)
    {
        switch(channel)
        {
            case 24: return "ISW_UNIQUE_ID";
            case 27: return "ISW_REGISTRATION_REQUEST_ESN_LOW";
            case 29: return "ISW_REGISTRATION_REQUEST_ESN_HIGH";
            default: break;
        }
    }
    else if(channel > 0)
    {
        switch(fFree(m))
        {
            case 21: return "ISW_CALL_START";
            case 23: return "ISW_CALL_END";
            case 31: return "ISW_REQUEST_ACCESS";
            default: break;
        }
    }
    return "ISW_UNKNOWN";
}

LTRNetDecoder::LTRNetDecoder(Sink sink)
    : mOsw(syncLtrOsw(), MESSAGE_LENGTH), mIsw(syncLtrIsw(), MESSAGE_LENGTH), mSink(std::move(sink))
{
    mOsw.setListener([this](const BitMsg& m) { receive(m, Direction::OSW); });
    mIsw.setListener([this](const BitMsg& m) { receive(m, Direction::ISW); });
}

static std::string listOf(const std::vector<int>& v)
{
    std::string s = "[";
    for(size_t i = 0; i < v.size(); i++) { if(i) { s += ","; } s += std::to_string(v[i]); }
    return s + "]";
}

// LtrNetMessageFactory.create() + the message classes + LTRNetMessageProcessor.receive()
void LTRNetDecoder::receive(BitMsg m, Direction d)
{
    if(d == Direction::ISW) { flipAll(m); }         // ISW messages are inverted
    std::string type = (d == Direction::ISW) ? iswType(m) : oswType(m);

    // LtrNetMessage constructor: CRC check; LtrNetIswMessage constructor: all-ones checksum with free 31 or 23 passes
    bool inv = false;
    CrcResult crc = crcltrCheck(m, d, &inv);
    if(d == Direction::ISW && crc == CrcResult::FAILED_CRC && crcltrTransmitted(m) == 127)
    {
        int free = fFree(m);
        if(free == 31 || free == 23) { crc = CrcResult::PASSED; inv = false; }
    }
    if(crc != CrcResult::PASSED) { failedCrc++; return; }

    Message o;
    o.direction = d;
    o.type = type;
    o.crc = inv ? "pass-inverted" : "pass";
    o.rawHex = m.toHex();

    Json j;
    j.str("type", "ltrnet").str("direction", d == Direction::OSW ? "osw" : "isw").str("message", type).str("crc", o.crc);
    int area = fArea(m), channel = fChannel(m), home = fHome(m), group = fGroup(m), free = fFree(m);
    std::string tg = ltrTalkgroup(m);
    std::string text;
    auto callFields = [&]() { j.str("talkgroup", tg).num("area", area).num("lcn", channel).num("free", free); };
    auto unknownFields = [&]() { j.num("area", area).num("lcn", channel).num("home", home).num("group", group).num("free", free); };

    if(type == "OSW_CALL_START" || type == "ISW_CALL_START")
    {
        callFields();
        text = "CALL START TALKGROUP:" + tg + " AREA:" + std::to_string(area) + " LCN:" + std::to_string(channel) + " FREE:" + std::to_string(free);
    }
    else if(type == "OSW_CALL_END" || type == "ISW_CALL_END")
    {
        callFields();
        text = "CALL END TALKGROUP:" + tg + " AREA:" + std::to_string(area) + " LCN:" + std::to_string(channel) + " FREE:" + std::to_string(free);
    }
    else if(type == "OSW_SYSTEM_IDLE")
    {
        unknownFields();
        text = "IDLE - AREA:" + std::to_string(area) + " LCN:" + std::to_string(channel) + " HOME:" + std::to_string(home) +
               " GROUP:" + std::to_string(group) + " FREE:" + std::to_string(free);
    }
    else if(type == "OSW_CHANNEL_MAP_HIGH" || type == "OSW_CHANNEL_MAP_LOW")
    {
        std::vector<int> ch;
        bool high = (type == "OSW_CHANNEL_MAP_HIGH");
        for(int x = 27; x >= 18; x--) { if(m.get((size_t)x)) { ch.push_back((high ? 38 : 28) - x); } }
        j.raw("channels", listOf(ch));
        text = std::string(high ? "CHANNEL MAP HIGH " : "CHANNEL MAP LOW ") + listOf(ch);
    }
    else if(type == "OSW_TRANSMIT_FREQUENCY_HIGH" || type == "OSW_TRANSMIT_FREQUENCY_LOW" ||
            type == "OSW_RECEIVE_FREQUENCY_HIGH" || type == "OSW_RECEIVE_FREQUENCY_LOW")
    {
        bool tx = (type.find("TRANSMIT") != std::string::npos), high = (type.find("HIGH") != std::string::npos);
        // Frequency.getChannel() is the HOME_REPEATER field.  FrequencyHigh units = 4 bits (29..32) << 12,
        // FrequencyLow units = 12 bits (21..32); frequency = 150 MHz + (high + low) * 1250 Hz once both halves are known.
        auto& highMap = tx ? mTxHigh : mRxHigh;
        auto& lowMap = tx ? mTxLow : mRxLow;
        long long frequency = 0;
        if(high)
        {
            long long units = (long long)(m.getRange(29, 32) << 12);
            highMap[home] = units;
            auto it = lowMap.find(home);
            if(it != lowMap.end()) { frequency = 150000000LL + (units + it->second) * 1250; }
        }
        else
        {
            long long units = (long long)m.getRange(21, 32);
            lowMap[home] = units;
            auto it = highMap.find(home);
            if(it != highMap.end()) { frequency = 150000000LL + (it->second + units) * 1250; }
        }
        j.num("lcn", home).num("frequency_hz", frequency).str("half", high ? "high" : "low");
        text = "LCN:" + std::to_string(home) + (tx ? " TRANSMIT FREQUENCY:" : " RECEIVE FREQUENCY:") + std::to_string(frequency) +
               (high ? " - HIGH" : " - LOW");
    }
    else if(type == "OSW_NEIGHBOR_ID")
    {
        int neighbor = (int)m.getRange(23, 32), rank = (int)m.getRange(15, 18) + 1;
        j.num("neighbor", neighbor).num("rank", rank);
        text = "NEIGHBOR:" + std::to_string(neighbor) + " RANK:" + std::to_string(rank);
    }
    else if(type == "OSW_REGISTRATION_ACCEPT")
    {
        int id = (int)m.getRange(17, 32);
        j.num("unique_id", id);
        text = "REGISTRATION ACCEPT - RADIO UNIQUE ID: " + std::to_string(id);
    }
    else if(type == "OSW_SITE_ID")
    {
        int site = (int)m.getRange(23, 32);
        j.num("site", site);
        text = "SITE:" + std::to_string(site);
    }
    else if(type == "ISW_UNIQUE_ID")
    {
        int id = (int)m.getRange(17, 32);
        j.num("unique_id", id);
        text = "RADIO UNIQUE ID: " + std::to_string(id);
    }
    else if(type == "ISW_REGISTRATION_REQUEST_ESN_HIGH" || type == "ISW_REGISTRATION_REQUEST_ESN_LOW")
    {
        // The upstream processor never keeps the other half, so the missing half always prints as "xxxx".
        bool high = (type == "ISW_REGISTRATION_REQUEST_ESN_HIGH");
        std::string half = hexN(m.getRange(17, 32) & 0xFFFF, 4);
        std::string esn = high ? half + "xxxx" : "xxxx" + half;
        j.str("esn", esn).str(high ? "esn_high" : "esn_low", half);
        text = std::string(high ? "ESN HIGH: " : "ESN LOW: ") + esn;
    }
    else if(type == "ISW_REQUEST_ACCESS")
    {
        callFields();
        text = "REQUEST ACCESS TALKGROUP:" + tg + " AREA:" + std::to_string(area) + " LCN:" + std::to_string(channel) + " FREE:" + std::to_string(free);
    }
    else
    {
        unknownFields();
        text = std::string(d == Direction::OSW ? "OSW" : "ISW") + " UNKNOWN MESSAGE - AREA:" + std::to_string(area) + " LCN:" +
               std::to_string(channel) + " HOME:" + std::to_string(home) + " GROUP" + std::to_string(group) + " FREE:" + std::to_string(free);
    }
    j.str("text", text).str("msg", o.rawHex);
    o.text = text;
    o.json = j.done();
    if(mSink) { mSink(o); }
}

}  // namespace trunkfmt
