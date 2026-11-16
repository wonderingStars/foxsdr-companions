// See passport.h / common.h for the notices.
#include "passport.h"

namespace trunkfmt {

// CRCPassport.sCHECKSUMS: bit 9 (DCC 1) .. bit 59 (FREE 0)
static const uint8_t PASSPORT_CHECKSUMS[51] = {
    0x6E, 0xBF,                                                              // DCC 1, 0
    0xD6, 0xE3, 0xF8, 0x7C, 0x3E, 0x97, 0xC2, 0xE9, 0x75, 0x3B, 0x94,        // LCN 10..0
    0x4A, 0xAD, 0x57, 0xA2, 0xD9, 0x6D, 0x37,                                // SITE 6..0
    0x92, 0xC1, 0x61, 0x31, 0x19, 0x0D, 0x07, 0x8A, 0xCD, 0x67, 0xBA, 0xD5, 0x6B, 0xBC, 0x5E, 0xA7,   // GROUP 15..0
    0xDA, 0xE5, 0x73, 0xB0,                                                  // TYPE 3..0
    0x58, 0x2C, 0x16, 0x83, 0xC8, 0x64, 0x32, 0x91, 0x49, 0x25, 0x13};       // FREE 10..0

static uint8_t passportChecksum(const BitMsg& msg)         // CRCPassport.getChecksum(): bits 60..67
{
    uint8_t v = 0;
    for(int x = 0; x < 8; x++) { if(msg.get((size_t)(x + 60))) { v ^= (uint8_t)(1 << (7 - x)); } }
    return v;
}

static uint8_t passportResidual(const BitMsg& msg)         // CRCPassport.getResidual()
{
    uint8_t calculated = 0;
    for(int i = 9; i <= 59; i++) { if(msg.get((size_t)i)) { calculated ^= PASSPORT_CHECKSUMS[i - 9]; } }
    calculated ^= passportChecksum(msg);
    return calculated;
}

// CRCPassport.correct(): returns the number of bits flipped
static int passportCorrect(BitMsg& msg)
{
    int residual = passportResidual(msg);
    switch(residual)
    {
        case 1: case 136: case 153: msg.flip(67); return 1;
        case 3: case 138: msg.flip(66); msg.flip(67); return 2;
        case 2: case 139: msg.flip(66); return 1;
        case 140: msg.flip(65); msg.flip(67); return 2;
        case 142: msg.flip(65); msg.flip(66); msg.flip(67); return 3;
        default: return 0;
    }
}

// PassportBand: values()[band] for band 0..15, else BAND_UNKNOWN
struct Band { const char* name; long long base; };
static const Band BANDS[18] = {
    {"800", 800000000}, {"900", 900000000}, {"400", 400000000}, {"420", 420000000}, {"440", 440000000}, {"450", 450000000},
    {"460", 460000000}, {"470", 470000000}, {"480", 480000000}, {"490", 490000000}, {"CAN", 0}, {"NTIA", 0}, {"RESVD", 0},
    {"216", 216000000}, {"RESVD", 0}, {"700", 700000000}, {"VHF", 0}, {"UNK", 0}};
static const int BAND_UNKNOWN = 17;
static int bandLookup(int band) { return (band >= 0 && band < 16) ? band : BAND_UNKNOWN; }
static long long bandFrequency(int band, int channel) { return BANDS[band].base + (long long)channel * 12500; }   // CHANNEL_BANDWIDTH

// field index lists
static int fColor(const BitMsg& m) { return (int)m.getRange(9, 10); }
static int fLcn(const BitMsg& m) { return (int)m.getRange(11, 21); }
static int fSite(const BitMsg& m) { return (int)m.getRange(22, 28); }
static int fGroup(const BitMsg& m) { return (int)m.getRange(29, 44); }
static int fRadio(const BitMsg& m) { return (int)m.getRange(22, 44); }
static int fNeighborBand(const BitMsg& m) { return (int)m.getRange(33, 36); }
static int fSiteBand(const BitMsg& m) { return (int)m.getRange(41, 44); }
static int fTypeNumber(const BitMsg& m) { return (int)m.getRange(45, 48); }
static int fFree(const BitMsg& m) { return (int)m.getRange(49, 59); }

// PassportMessage.getMessageType()
static const char* messageType(const BitMsg& m)
{
    int type = fTypeNumber(m), lcn = fLcn(m);
    switch(type)
    {
        case 0: return "CA_STRT";
        case 1:
            if(fFree(m) == 2042) { return "ID_TGAS"; }
            if(lcn < 1792) { return "CA_STRT"; }
            if(lcn == 1792 || lcn == 1793) { return "SY_IDLE"; }
            if(lcn == 2047) { return "CA_ENDD"; }
            return "UN_KNWN";
        case 2: return "CA_STRT";
        case 5: return "CA_PAGE";
        case 6: return "ID_RDIO";
        case 9: return "DA_STRT";
        case 11: return "RA_REGI";
        default: return "UN_KNWN";
    }
}

static std::string leftPad(int number, int places)
{
    std::string s = std::to_string(number);
    return s.size() >= (size_t)places ? s : std::string((size_t)places - s.size(), '0') + s;
}

PassportDecoder::PassportDecoder(Sink sink) : mFramer(syncPassport(), MESSAGE_LENGTH), mSink(std::move(sink))
{
    mFramer.setListener([this](const BitMsg& m) { receive(m); });
}

// PassportMessageProcessor.receive() + PassportMessage
void PassportDecoder::receive(const BitMsg& framed)
{
    BitMsg m = framed;
    int corrected = passportCorrect(m);                 // PassportMessage constructor: CRCPassport.correct()
    bool valid = (passportResidual(m) == 0);            // CRCPassport.check()
    if(!valid) { failedCrc++; return; }

    Message o;
    o.type = messageType(m);
    o.colorCode = fColor(m);
    o.site = fSite(m);
    o.lcn = fLcn(m);
    o.free = fFree(m);
    o.typeNumber = fTypeNumber(m);
    o.group = fGroup(m);
    o.radioId = fRadio(m);
    o.hasFrom = (o.type == "ID_RDIO");
    o.hasTo = (o.type != "SY_IDLE");
    o.correctedBits = corrected > 0;
    o.siteBand = BANDS[bandLookup(fSiteBand(m))].name;
    o.neighborBand = BANDS[bandLookup(fNeighborBand(m))].name;

    // PassportMessage.getSiteFrequency(channel): needs the idle message found earlier, and 0 < channel < 1792
    auto siteFrequency = [&](int channel) -> long long {
        if(mHaveIdle && 0 < channel && channel < 1792 && mIdleSiteBand != BAND_UNKNOWN)
        {
            return bandFrequency(mIdleSiteBand, channel);
        }
        return 0;
    };
    o.lcnFrequency = siteFrequency(o.lcn);
    o.freeFrequency = siteFrequency(o.free);
    o.neighborFrequency = (o.type == "SY_IDLE") ? bandFrequency(bandLookup(fNeighborBand(m)), o.free) : 0;

    // text, as PassportMessage.toString() without the trailing MSG:
    std::string sb = "DCC:" + std::to_string(o.colorCode);
    std::string to = std::to_string(o.group), from = std::to_string(o.radioId);
    auto chan = [&]() { return " CHAN:" + leftPad(o.lcn, 4) + "/" + std::to_string(o.lcnFrequency); };
    auto freeS = [&]() { return " FREE:" + leftPad(o.free, 3) + "/" + std::to_string(o.freeFrequency); };
    if(o.type == "SY_IDLE")
    {
        sb += " IDLE SITE:" + leftPad(o.site, 3) + " NEIGHBOR:" + leftPad(o.free, 3) + "/" + std::to_string(o.freeFrequency);
    }
    else if(o.type == "CA_PAGE") { sb += " PAGING TG:" + to + " SITE:" + leftPad(o.site, 3) + chan() + freeS(); }
    else if(o.type == "CA_STRT") { sb += " CALL TG:" + to + " SITE:" + leftPad(o.site, 3) + chan() + freeS(); }
    else if(o.type == "DA_STRT") { sb += " ** DATA TG:" + to + " SITE:" + leftPad(o.site, 3) + chan() + freeS(); }
    else if(o.type == "CA_ENDD") { sb += " END  TG:" + to + " SITE:" + leftPad(o.site, 3) + chan() + freeS(); }
    else if(o.type == "ID_RDIO") { sb += " MOBILE ID MIN:" + from + freeS(); }
    else if(o.type == "ID_TGAS") { sb += " ASSIGN TALKGROUP:" + to + " SITE:" + leftPad(o.site, 3) + chan(); }
    else if(o.type == "RA_REGI") { sb += " RADIO REGISTER TG: " + to; }
    else
    {
        sb += " UNKNOWN SITE:" + leftPad(o.site, 3) + chan() + " FREE:" + leftPad(o.free, 3);
        if(o.free > 0 && o.free < 896) { sb += "/" + std::to_string(o.freeFrequency); }
        sb += " TYP:" + leftPad(o.typeNumber, 2) + " TG:" + to;
    }
    o.rawHex = m.toHex();
    std::string textWithMsg = sb + " MSG:" + o.rawHex;     // IdleMessageFinder counts identical toString() values
    o.text = sb;

    // PassportMessageProcessor: until an idle message has been seen four times, messages are built without idle
    // information (so frequencies are 0); the fourth identical idle message becomes the reference.
    if(!mHaveIdle && !mIdleFound && o.type == "SY_IDLE")
    {
        auto it = mIdleCounts.find(textWithMsg);
        if(it != mIdleCounts.end())
        {
            if(it->second >= 3)
            {
                mIdleFound = true;
                mHaveIdle = true;
                mIdleSiteBand = bandLookup(fSiteBand(m));
                mIdleCounts.clear();
            }
            else { it->second++; }
        }
        else { mIdleCounts[textWithMsg] = 1; }
    }
    if(mSink) { mSink(o); }
}

}  // namespace trunkfmt
