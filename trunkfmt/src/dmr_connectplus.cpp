// See dmr_connectplus.h / common.h for the notices.
#include "dmr_connectplus.h"
#include <cctype>
#include <cstring>

namespace trunkfmt {

// ------------------------------------------------------------------------------------------------
// CRCDMR.CCITT_80_CHECKSUMS: 80 message-bit entries followed by 16 CRC-bit entries
// ------------------------------------------------------------------------------------------------
static const int CCITT_80_CHECKSUMS[96] = {
    0xE434, 0x721A, 0x390D, 0x9496, 0x4A4B, 0xAD35, 0xDE8A, 0x6F45, 0xBFB2,
    0x5FD9, 0xA7FC, 0x53FE, 0x29FF, 0x9CEF, 0xC667, 0xEB23, 0xFD81, 0xF6D0,
    0x7B68, 0x3DB4, 0x1EDA, 0xF6D, 0x8FA6, 0x47D3, 0xABF9, 0xDDEC, 0x6EF6,
    0x377B, 0x93AD, 0xC1C6, 0x60E3, 0xB861, 0xD420, 0x6A10, 0x3508, 0x1A84,
    0xD42, 0x6A1, 0x8B40, 0x45A0, 0x22D0, 0x1168, 0x8B4, 0x45A, 0x22D, 0x8906,
    0x4483, 0xAA51, 0xDD38, 0x6E9C, 0x374E, 0x1BA7, 0x85C3, 0xCAF1, 0xED68,
    0x76B4, 0x3B5A, 0x1DAD, 0x86C6, 0x4363, 0xA9A1, 0xDCC0, 0x6E60, 0x3730,
    0x1B98, 0xDCC, 0x6E6, 0x373, 0x89A9, 0xCCC4, 0x6662, 0x3331, 0x9188,
    0x48C4, 0x2462, 0x1231, 0x8108, 0x4084, 0x2042, 0x1021, 0x1, 0x2, 0x4, 0x8,
    0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000,
    0x8000};

static const int CSBK_CRC_MASK = 0xA5A5;    // CSBKMessage.CSBK_CRC_MASK

// CRCDMR.correctCCITT80(message, 0, 80, mask).  Returns 0 (clean), 1 (one bit corrected), 2 (uncorrectable).
static int correctCCITT80(BitMsg& message, int messageStart, int crcStart, int mask)
{
    int calculated = mask;
    for(int i = messageStart; i < crcStart; i++)
    {
        if(message.get((size_t)i)) { calculated ^= CCITT_80_CHECKSUMS[i - messageStart]; }
    }
    int checksum = (int)message.getRange(crcStart, crcStart + 15);
    int residual = calculated ^ checksum;
    if(residual == 0 || residual == 0xFFFF) { return 0; }
    for(int x = 0; x < 96; x++)                // CRCDMR.getBitError()
    {
        if(CCITT_80_CHECKSUMS[x] == residual)
        {
            message.flip((size_t)(x + messageStart));
            return 1;
        }
    }
    return 2;
}

// CRCDMR.crc8(bits, len): polynomial x^8+x^2+x+1, returns the remainder compared with the next 8 bits
static int crc8(const BitMsg& bits, int len)
{
    if(len + 8 > 256) { return 0; }
    bool buf[256] = {false};
    const bool poly[9] = {true, false, false, false, false, false, true, true, true};
    for(int i = 0; i < len + 8; i++) { buf[i] = bits.get((size_t)i); }
    for(int i = 0; i < len; i++)
    {
        if(buf[i]) { for(int j = 0; j < 9; j++) { buf[i + j] ^= poly[j]; } }
    }
    int crc = 0;
    for(int i = 0; i < 8; i++) { crc = (crc << 1) + (buf[len + i] ? 1 : 0); }
    return crc;
}

// ------------------------------------------------------------------------------------------------
// Vendor.java
// ------------------------------------------------------------------------------------------------
struct VendorEntry { int id; const char* name; };
static const VendorEntry VENDORS[] = {
    {0, "STANDARD"}, {4, "FYLDE MICRO"}, {5, "PROD-EL SPA"}, {6, "MOTOROLA CON+"}, {7, "RADIO DATA GMBH"},
    {8, "HYTERA"}, {9, "ASELSAN"}, {10, "KIRISUN"}, {11, "DMR ASSOCIATION"}, {12, "SEPURA"},
    {13, "ITALIA RED CROSS"}, {16, "MOTOROLA CAP+"}, {19, "ITALY MIN INTERIOR"}, {28, "EMC COMM SRL 28"},
    {32, "EMC COMM SRL 32"}, {51, "JVC-KENWOOD"}, {60, "RADIO ACTIVITY SRL"}, {84, "ZTE TRUNKING"},
    {88, "TAIT"}, {104, "HYTERA"}, {119, "VERTEX STANDARD"}, {120, "SIMOCO"}, {121, "TEST"}, {136, "HYTERA"}};
static const int VENDOR_CONNECT_PLUS = 6;

static const char* vendorName(int id)
{
    for(const auto& v : VENDORS) { if(v.id == id) { return v.name; } }
    return "UNKNOWN";
}

// Opcode.java: the Motorola Connect Plus rows (Vendor.MOTOROLA_CONNECT_PLUS)
struct OpcodeEntry { int value; const char* name; };
static const OpcodeEntry CONPLUS_OPCODES[] = {
    {1, "NEIGHBOR REPORT"}, {3, "VOICE CHANNEL USER"}, {6, "DATA CHANNEL GRANT"}, {10, "CSBKO 10"},
    {12, "TERMINATE CHANNEL GRANT"}, {16, "CSBKO 16"}, {17, "REGISTRATION REQUEST"}, {18, "REGISTRATION RESPONSE"},
    {24, "TALKGROUP AFFILIATION"}, {28, "ENHANCED DATA REVERT WINDOW ANNOUNCEMENT"},
    {29, "ENHANCED DATA REVERT WINDOW GRANT"}};

static const char* conPlusOpcodeName(int value)
{
    for(const auto& o : CONPLUS_OPCODES) { if(o.value == value) { return o.name; } }
    return nullptr;
}

// ------------------------------------------------------------------------------------------------
// field lists (bit indices into the 96-bit CSBK; bit 0 first received, listed MSB first)
// ------------------------------------------------------------------------------------------------
static const int LAST_BLOCK = 0, PROTECT_FLAG = 1;
static const int OPCODE_BITS[] = {2, 3, 4, 5, 6, 7};
static const int VENDOR_BITS[] = {8, 9, 10, 11, 12, 13, 14, 15};

// XCMPMessageType.java
static const char* xcmpMessageType(int v) { return v == 2 ? "NETWORK FREQUENCY FILE" : "UNKNOWN"; }

// DMRLsn.java: new DMRLsn(lsn) -> logical channel number and timeslot.  C++ integer division and remainder
// truncate toward zero exactly as Java does, so out-of-range values behave the same as upstream.
static void addLsn(Json& j, const char* prefix, int lsnArg)
{
    int lcn = ((lsnArg - 1) / 2) + 1;
    int ts = ((lsnArg - 1) % 2) + 1;
    std::string p = prefix;
    j.num((p + "lcn").c_str(), lcn);
    j.num((p + "timeslot").c_str(), ts);
    j.num((p + "lsn").c_str(), ((lcn - 1) * 2) + ts);     // DMRLsn.getLsn()
}

static std::string rangeHex(const BitMsg& m, int start, int end)
{
    int bits = end - start + 1;
    return hexN(m.getRange(start, end), (bits + 3) / 4);
}

static std::string unkHex(const BitMsg& m, int start, int end) { return rangeHex(m, start, end); }

// ------------------------------------------------------------------------------------------------
// ConnectPlus* CSBK decoders.  `j` already holds the common header fields.
// ------------------------------------------------------------------------------------------------
static void decodeConPlus(Json& j, const BitMsg& m, int opcode)
{
    switch(opcode)
    {
        case 1:   // ConnectPlusNeighborReport: six 8-bit site numbers from bit 16 (0 = unused slot)
        {
            std::string sites = "[";
            bool first = true;
            for(int x = 0; x <= 5; x++)
            {
                int start = 16 + x * 8;
                int site = (int)m.getRange(start, start + 7);
                if(site > 0)
                {
                    if(!first) { sites += ","; }
                    sites += std::to_string(site);
                    first = false;
                }
            }
            sites += "]";
            j.raw("neighbors", sites);
            j.str("unk", unkHex(m, 56, 79));
            break;
        }
        case 3:   // ConnectPlusVoiceChannelUser
            j.num("source", (long long)m.getRange(16, 39));
            j.num("talkgroup", (long long)m.getRange(40, 63));
            j.num("lsn_field", (long long)m.getRange(64, 68));
            addLsn(j, "", (int)m.getRange(64, 68) - 1);        // "always subtract 1 to get the repeater LSN"
            j.num("unk", (long long)m.getRange(72, 79));
            break;
        case 6:   // ConnectPlusDataChannelGrant
            j.num("target", (long long)m.getRange(16, 39));
            j.num("lsn_field", (long long)m.getRange(40, 44));
            addLsn(j, "", (int)m.getRange(40, 44) - 1);
            j.num("unk", (long long)m.getRange(48, 55));
            break;
        case 10:  // ConnectPlusOTAAnnouncement
        {
            int mt = (int)m.getRange(16, 23);
            j.num("message_type", mt);
            j.str("message_type_name", xcmpMessageType(mt));
            j.num("version", (long long)m.getRange(24, 39));
            j.num("lsn_field", (long long)m.getRange(63, 67));
            addLsn(j, "data_", (int)m.getRange(63, 67) - 1);
            j.str("unk", unkHex(m, 40, 62));
            break;
        }
        case 12:  // ConnectPlusTerminateChannelGrant
            j.num("target", (long long)m.getRange(16, 39));
            j.num("unk1", (long long)m.getRange(40, 47));
            j.num("unk2", (long long)m.getRange(48, 55));
            j.num("unk3", (long long)m.getRange(56, 63));
            j.num("unk4", (long long)m.getRange(64, 71));
            j.num("unk5", (long long)m.getRange(72, 79));
            break;
        case 16:  // ConnectPlusCSBKO_16
            j.num("source", (long long)m.getRange(16, 39));
            j.num("target", (long long)m.getRange(40, 63));
            j.str("unk", hexN(m.getRange(64, 79), 4));
            break;
        case 17:  // ConnectPlusRegistrationRequest
        case 18:  // ConnectPlusRegistrationResponse
            j.num("source", (long long)m.getRange(16, 39));
            j.num("target", (long long)m.getRange(40, 63));
            j.num("unk", (long long)m.getRange(64, 71));
            break;
        case 24:  // ConnectPlusTalkgroupAffiliation
            j.num("radio", (long long)m.getRange(16, 39));
            j.num("talkgroup", (long long)m.getRange(40, 63));
            j.num("unk", (long long)m.getRange(64, 71));
            break;
        case 28:  // ConnectPlusDataRevertWindowAnnouncement
        {
            j.num("window", (long long)m.getRange(16, 22));
            j.num("superframe", (long long)m.getRange(24, 27));
            uint32_t target = m.getRange(32, 55);
            if(target > 0) { j.num("reserved_for", target); }      // hasTargetRadio(): only when non-zero
            break;
        }
        case 29:  // ConnectPlusDataRevertWindowGrant
            j.num("target", (long long)m.getRange(16, 39));
            j.num("superframe", (long long)m.getRange(48, 51));
            j.num("window", (long long)m.getRange(70, 74));
            break;
        default:
            break;
    }
}

// ------------------------------------------------------------------------------------------------
// text input handling
// ------------------------------------------------------------------------------------------------
static bool parseHex(const std::string& s, size_t& digitsOut, BitMsg& bits, std::string& err)
{
    std::vector<int> nib;
    for(char c : s)
    {
        if(isxdigit((unsigned char)c))
        {
            nib.push_back(isdigit((unsigned char)c) ? c - '0' : (tolower((unsigned char)c) - 'a' + 10));
        }
        else if(c == ' ' || c == '\t' || c == ':' || c == '-' || c == '_' || c == ',' || c == '\r')
        {
            continue;
        }
        else
        {
            err = std::string("unexpected character '") + c + "'";
            return false;
        }
    }
    digitsOut = nib.size();
    bits = BitMsg(nib.size() * 4);
    for(int n : nib) { for(int b = 3; b >= 0; b--) { bits.add(((n >> b) & 1) != 0); } }
    return true;
}

static std::string parseError(long long lineNo, const std::string& why)
{
    return Json().num("t", 0).str("type", "parse_error").num("line", lineNo).str("text", why).done();
}

std::string decodeConnectPlusLine(const std::string& lineIn, long long lineNo)
{
    std::string line = lineIn;
    size_t hash = line.find('#');
    if(hash != std::string::npos) { line.erase(hash); }

    // optional keyword
    std::string kind = "csbk";
    size_t p = 0;
    while(p < line.size() && isspace((unsigned char)line[p])) { p++; }
    // an optional leading keyword: "csbk" or "slc" (neither is a valid hex string)
    size_t w = p;
    while(w < line.size() && isalpha((unsigned char)line[w])) { w++; }
    std::string word = line.substr(p, w - p);
    for(auto& c : word) { c = (char)tolower((unsigned char)c); }
    if(word == "csbk" || word == "slc")
    {
        kind = word;
        line = line.substr(w);
    }

    size_t digits = 0;
    BitMsg m;
    std::string err;
    if(!parseHex(line, digits, m, err)) { return parseError(lineNo, err); }

    if(kind == "slc")
    {
        if(digits != 9 && digits != 11) { return parseError(lineNo, "slc needs 9 hex digits (36 bits) or 11 (36 bits + CRC-8)"); }
        // ShortLCMessage.getOpcode(): the 4-bit opcode.  CHANGE FROM UPSTREAM (see UPSTREAM.md): upstream's
        // getVendor() reads bits 4..11 as a vendor id whenever bit 0 is set and the opcode is not 10, which for
        // opcode 9 (traffic channel, whose bits 4..15 are the NETWORK number) can turn a network number such as
        // 100 (0x064 -> "vendor" 6) into a vendor lookup that finds no opcode.  Opcodes 9 and 10 are decoded here
        // whatever those bits hold; no other Short LC opcode is defined for another vendor at 9 or 10.
        int opcode = (int)m.getRange(0, 3);
        Json j;
        j.num("t", 0).str("type", "connectplus_slc").num("line", lineNo);
        bool decoded = (opcode == 9 || opcode == 10);
        if(digits == 11) { j.str("crc", crc8(m, 36) == 0 ? "ok" : "error"); }
        else { j.str("crc", "none"); }
        j.num("slc_opcode", opcode);
        j.boolean("decoded", decoded);
        if(decoded)
        {
            j.str("name", opcode == 10 ? "CONTROL CHANNEL INFO" : "TRAFFIC CHANNEL INFO");
            j.num("network", (long long)m.getRange(4, 15));
            j.num("site", (long long)m.getRange(16, 23));
        }
        j.str("msg", m.toHex());
        return j.done();
    }

    // csbk
    if(digits != 24 && digits != 20) { return parseError(lineNo, "csbk needs 24 hex digits (12 bytes with CRC) or 20 (10 bytes, no CRC)"); }
    bool hasCrc = (digits == 24);
    BitMsg msg(96);
    for(size_t i = 0; i < m.length(); i++) { msg.add(m.get(i)); }
    std::string crcState = "none";
    if(hasCrc)
    {
        int n = correctCCITT80(msg, 0, 80, CSBK_CRC_MASK);
        crcState = (n == 0) ? "ok" : (n == 1 ? "corrected" : "error");
    }
    int opcode = (int)msg.getInt(OPCODE_BITS);
    int vendor = (int)msg.getInt(VENDOR_BITS);

    Json j;
    j.num("t", 0).num("line", lineNo);
    const char* opName = (vendor == VENDOR_CONNECT_PLUS) ? conPlusOpcodeName(opcode) : nullptr;
    if(opName == nullptr)
    {
        // not a Connect Plus opcode: reported, not decoded (the standard DMR CSBKs belong to the DMR plugin)
        j.str("type", "csbk").boolean("decoded", false);
        j.num("fid", vendor).str("vendor", vendorName(vendor)).num("csbko", opcode);
        j.str("crc", crcState).str("msg", msg.toHex());
        return j.done();
    }
    j.str("type", "connectplus_csbk").boolean("decoded", true);
    j.num("fid", vendor).str("vendor", vendorName(vendor)).num("csbko", opcode).str("name", opName);
    j.boolean("last_block", msg.get(LAST_BLOCK)).boolean("protected", msg.get(PROTECT_FLAG));
    j.str("crc", crcState);
    decodeConPlus(j, msg, opcode);
    j.str("msg", msg.toHex());
    return j.done();
}

}  // namespace trunkfmt
