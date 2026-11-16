// See fleetsync2.h / common.h for the notices.
#include "fleetsync2.h"

namespace trunkfmt {

// CRCFleetsync.sCHECKSUMS: 48 per-bit CRC-15 contributions then the 15 single-bit CRC errors (indices 48..62)
static const int sCHECKSUMS[63] = {
    0x740A, 0x3A05, 0x6908, 0x3484, 0x1A42, 0x0D21, 0x729A, 0x394D, 0x68AC, 0x3456, 0x1A2B, 0x791F,
    0x4885, 0x5048, 0x2824, 0x1412, 0x0A09, 0x710E, 0x3887, 0x6849, 0x402E, 0x2017, 0x6401, 0x460A,
    0x2305, 0x6588, 0x32C4, 0x1962, 0x0CB1, 0x7252, 0x3929, 0x689E, 0x344F, 0x6E2D, 0x431C, 0x218E,
    0x10C7, 0x7C69, 0x4A3E, 0x251F, 0x6685, 0x4748, 0x23A4, 0x11D2, 0x08E9, 0x707E, 0x383F, 0x6815,
    0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100, 0x0080, 0x0040, 0x0020, 0x0010, 0x0008,
    0x0004, 0x0002, 0x0001};

static int getChecksum(const BitMsg& msg)      // CRCFleetsync.getChecksum(): bits 48..62, MSB first
{
    int v = 0;
    for(int x = 0; x < 15; x++)
    {
        if(msg.get((size_t)(x + 48))) { v += 1 << (14 - x); }
    }
    return v;
}

static int cardinality(const BitMsg& msg, size_t n)
{
    int c = 0;
    for(size_t i = 0; i < n; i++) { if(msg.get(i)) { c++; } }
    return c;
}

CrcResult fleetsyncCheck(const BitMsg& msg)    // CRCFleetsync.check()
{
    // even parity over the whole block (BitSet cardinality of the copied range)
    if(cardinality(msg, 64) % 2 == 0)
    {
        int calculated = 1;                    // starting value
        for(int i = 0; i < 48; i++) { if(msg.get((size_t)i)) { calculated ^= sCHECKSUMS[i]; } }
        return calculated == getChecksum(msg) ? CrcResult::PASSED : CrcResult::FAILED_CRC;
    }
    return CrcResult::FAILED_PARITY;
}

// CRCFleetsync.findBitErrors(): returns the bit position of a single-bit error, or -1
static int findBitError(const BitMsg& msg)
{
    int checksum = getChecksum(msg);
    checksum ^= 1;                              // remove the initial fill value
    for(int i = 0; i < 48; i++) { if(msg.get((size_t)i)) { checksum ^= sCHECKSUMS[i]; } }
    if(checksum == 0) { return 63; }            // parity bit error
    int found = -1;
    for(int x = 0; x < 63; x++) { if(checksum == sCHECKSUMS[x]) { found = x; } }   // no break upstream: last match wins
    return found;
}

// Fleetsync2Message.detectAndCorrect(message, start, end); MPT1327Message.detectAndCorrect is the same code
CrcResult fleetsyncDetectAndCorrect(BitMsg& message, int start, int end)
{
    BitMsg original((size_t)(end - start));
    for(int i = start; i < end; i++) { original.add(message.get((size_t)i)); }
    CrcResult r = fleetsyncCheck(original);
    if(r == CrcResult::FAILED_PARITY)
    {
        int pos = findBitError(original);
        if(pos >= 0)
        {
            message.flip((size_t)(start + pos));
            r = CrcResult::CORRECTED;
        }
    }
    return r;
}

// Fleetsync2Message bit positions
static const int FLAG_EMERGENCY_WORKER = 22, FLAG_LONE_WORKER = 24, FLAG_PAGING = 26;
static const int FLAG_AUTOMATIC_NUMBER_IDENTIFIER = 29, FLAG_STATUS = 30, FLAG_ACKNOWLEDGE = 31;
static const int FLAG_GPS_EXTENSION = 35, FLAG_FLEET_EXTENSION = 36;
static const int STATUS_FIELD[] = {21, 22, 23, 24, 25, 26, 27};
// FLEET 37..44, FROM_IDENT 45..56, TO_IDENT 57..68 (contiguous, MSB first); FLEET_TO 85..92 (second block)

static const char* abbreviation(CrcResult r)
{
    switch(r)
    {
        case CrcResult::PASSED: return "*";
        case CrcResult::CORRECTED: return "C";
        case CrcResult::FAILED_CRC: return "f";
        case CrcResult::FAILED_PARITY: return "p";
        default: return "-";
    }
}

static std::string formatted(int value)        // FleetsyncIdentifier.formatted(): "%03d:%04d"
{
    char b[32];
    snprintf(b, sizeof b, "%03d:%04d", (value >> 12) & 0xFF, value & 0xFFF);
    return b;
}

// LocationReport.convertDDMToDD()
static double convertDDMToDD(int hemisphere, int degreesMinutes, int decimalDegrees)
{
    double v = 0.0;
    if(degreesMinutes != 0)
    {
        v += (double)(degreesMinutes / 100);
        int whole = degreesMinutes % 100;
        if(whole != 0) { v += (double)(whole / 60.0); }
    }
    if(decimalDegrees != 0) { v += (double)(decimalDegrees / 600000.0); }
    if(hemisphere == 1) { v = -v; }
    return v;
}

Fleetsync2Message decodeFleetsync2(BitMsg m)
{
    Fleetsync2Message o;

    // message type (Fleetsync2Message.getMessageType()); flags are active-low for emergency / lone worker / paging
    bool ack = m.get(FLAG_ACKNOWLEDGE), gps = m.get(FLAG_GPS_EXTENSION), status = m.get(FLAG_STATUS);
    bool ani = m.get(FLAG_AUTOMATIC_NUMBER_IDENTIFIER);
    bool paging = !m.get(FLAG_PAGING), lone = !m.get(FLAG_LONE_WORKER), emerg = !m.get(FLAG_EMERGENCY_WORKER);
    bool fleetExt = m.get(FLAG_FLEET_EXTENSION);
    if(ack) { o.kind = Fleetsync2Message::ACKNOWLEDGE; o.kindLabel = "ACK"; }
    else if(gps) { o.kind = Fleetsync2Message::GPS; o.kindLabel = "GPS"; }
    else if(status) { o.kind = Fleetsync2Message::STATUS; o.kindLabel = "STATUS"; }
    else if(ani) { o.kind = Fleetsync2Message::ANI; o.kindLabel = "ANI"; }
    else if(paging) { o.kind = Fleetsync2Message::PAGING; o.kindLabel = "PAGE"; }
    else if(lone && emerg) { o.kind = Fleetsync2Message::LONE_WORKER_EMERGENCY; o.kindLabel = "LONE WORKER"; }
    else { o.kind = Fleetsync2Message::UNKNOWN; o.kindLabel = "UNK"; }

    // block count: LocationReport 8; ANI and Status 1 or 2 (fleet extension); the processor routes
    // GPS -> LocationReport, STATUS -> Status, everything else -> AutomaticNumberIdentification
    int blocks = (o.kind == Fleetsync2Message::GPS) ? 8 : (fleetExt ? 2 : 1);

    // Fleetsync2Message.checkParity()
    std::vector<CrcResult> crcs;
    crcs.push_back(fleetsyncDetectAndCorrect(m, 21, 85));
    for(int x = 1; x < blocks; x++)
    {
        int start = 21 + x * 64;
        crcs.push_back(fleetsyncDetectAndCorrect(m, start, start + 64));
    }
    for(CrcResult r : crcs) { o.crc += abbreviation(r); }
    o.valid = (crcs[0] == CrcResult::PASSED || crcs[0] == CrcResult::CORRECTED);

    o.emergency = emerg;
    o.loneWorker = lone;
    o.paging = paging;
    o.acknowledge = ack;
    o.ani = ani;
    o.fleetExtension = fleetExt;

    // identities.  getCommonFleet() = (FLEET + 99) << 12; ident = value + 999
    int commonFleet = ((int)m.getRange(37, 44) + 99) << 12;
    int fromValue = commonFleet + (int)m.getRange(45, 56) + 999;
    int toBase = fleetExt ? (((int)m.getRange(85, 92) + 99) << 12) : commonFleet;
    int toValue = toBase + (int)m.getRange(57, 68) + 999;
    o.fromFleet = (fromValue >> 12) & 0xFF;
    o.fromIdent = fromValue & 0xFFF;
    o.toFleet = (toValue >> 12) & 0xFF;
    o.toIdent = toValue & 0xFFF;
    o.from = formatted(fromValue);
    o.to = formatted(toValue);

    if(o.kind == Fleetsync2Message::STATUS) { o.status = (int)m.getInt(STATUS_FIELD); }

    if(o.kind == Fleetsync2Message::GPS)
    {
        // LocationReport.  Hemispheres: upstream always uses north (latitude) and west (longitude) - "TODO: determine
        // the correct hemisphere indicator".
        o.latitude = convertDDMToDD(0, (int)m.getRange(221, 236), (int)m.getRange(238, 251));
        o.longitude = convertDDMToDD(1, (int)m.getRange(301, 316), (int)m.getRange(318, 331));
        int headingValue = (int)m.getRange(353, 365);
        o.hasHeading = (headingValue != 4095);
        o.heading = o.hasHeading ? (double)headingValue / 10.0 : 0.0;
        o.speed = (double)m.getRange(484, 491) + ((double)m.getRange(492, 499) / 255.0);
        o.gpsHour = (int)m.getRange(172, 176);
        o.gpsMinute = (int)m.getRange(177, 182);
        o.gpsSecond = (int)m.getRange(183, 188);
        static const int GPS_YEAR[] = {284, 286, 287, 288, 289, 290, 291};   // upstream list (284 repeated from GPS_CENTURY)
        o.gpsYear = (int)m.getInt(GPS_YEAR);
        o.gpsMonth = (int)m.getRange(292, 295);
        o.gpsDay = (int)m.getRange(296, 300);
    }
    o.rawHex = m.toHex();
    return o;
}

Fleetsync2Decoder::Fleetsync2Decoder(Sink sink) : mFramer(syncFleetsync2(), MESSAGE_LENGTH), mSink(std::move(sink))
{
    mFramer.setListener([this](const BitMsg& m) {
        Fleetsync2Message d = decodeFleetsync2(m);
        // Fleetsync2DecoderState.receive(): only messages whose first block is valid are used
        if(d.valid) { if(mSink) { mSink(d); } }
        else { invalid++; }
    });
}

}  // namespace trunkfmt
