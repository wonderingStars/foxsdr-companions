// See mpt1327.h / common.h for the notices.
#include "mpt1327.h"
#include "fleetsync2.h"
#include <vector>

namespace trunkfmt {

// MPT1327Message tables
static const char* TELEX_LETTERS[32] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q",
                                        "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "\n", "\n", "", "", " ", " "};
static const char* TELEX_FIGURES[32] = {"-", "?", ":", "WRU", "3", "{6}", "{7}", "{8}", "8", "{BEEP}", "(", ")", ".", ",", "9",
                                        "0", "1", "4", "'", "5", "7", "=", "2", "/", "6", "+", "\n", "\n", "", "", " ", " "};
static const char* BCD[13] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", " ", "*", "#"};

static const int BLOCK_1_START = 20, BLOCK_2_START = 84, BLOCK_3_START = 148, BLOCK_4_START = 212, BLOCK_5_START = 276,
                 BLOCK_6_START = 340;

// field index lists (first index = MSB)
static const int B1_SYSDEF[] = {21, 22, 23, 24, 25};
static const int B1_PREFIX[] = {21, 22, 23, 24, 25, 26, 27};
static const int B1_TRAFFIC_CHANNEL[] = {21, 22, 23, 24, 25, 26, 27, 28, 29, 30};
static const int B1_SYSTEM_ID[] = {26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40};
static const int B1_IDENT1[] = {28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40};
static const int B1_CONTROL_CHANNEL[] = {31, 32, 33, 34, 35, 36, 37, 38, 39, 40};
static const int B1_CHANNEL[] = {35, 36, 37, 38, 39, 40, 41, 42, 43, 44};
static const int B1_MESSAGE_TYPE[] = {41, 42, 43, 44, 45, 46, 47, 48, 49};
static const int B1_GTC_CHAN[] = {43, 44, 45, 46, 47, 48, 49, 50, 51, 52};
static const int B1_IDENT2[] = {50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62};
static const int B1_IDENT2_GTC[] = {53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65};
static const int B1_IDENT2_HEAD[] = {55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67};
static const int B1_ADJSITE[] = {49, 50, 51, 52};
static const int B1_PERIODIC_CALL_MAINT_MESSAGES = 50;
static const int B1_MAINT_MESSAGE_INTERVAL[] = {51, 52, 53, 54, 55};
static const int B1_WAIT_TIME[] = {54, 55, 56};
static const int B1_PRESSEL_ON_REQUIRED = 56;
static const int B1_ALOHA_RSVD[] = {57, 58};
static const int B1_ALOHA_M[] = {59, 60, 61, 62, 63};
static const int B1_ALOHA_N[] = {64, 65, 66, 67};
static const int B1_IDENT1_ID_VALUE = 57;
static const int B1_SLOTS[] = {63, 64};
static const int B1_DESCRIPTOR[] = {65, 66, 67};
static const int B1_STATUS_MESSAGE[] = {63, 64, 65, 66, 67};
static const int B2_SYSTEM_ID[] = {85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99};
static const int B2_PREFIX[] = {112, 113, 114, 115, 116, 117, 118};
static const int B2_IDENT2[] = {119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131};
static const int B2_SDM_SEGMENT_TRANSACTION_FLAG = 85;
static const int B2_SDM_GENERAL_FORMAT[] = {86, 87, 88};
static const int B2_SDM_STF0_START = 86, B2_SDM_STF0_END = 131, B2_SDM_STF1_START = 90, B2_SDM_STF1_END = 131;
static const int B3_SDM_START = 150, B3_SDM_END = 195;
static const int B4_SDM_STF0_START = 214, B4_SDM_STF0_END = 259, B4_SDM_STF1_START = 218, B4_SDM_STF1_END = 259;
static const int B5_SDM_START = 278, B5_SDM_END = 323;

// MPTMessageType
enum class MT {
    UNKN, GTC, ALH, ALHS, ALHD, ALHE, ALHR, ALHX, ALHF, ACK, ACKI, ACKQ, ACKX, ACKV, ACKE, ACKT, ACKB, AHOY, AHYX, AHYP,
    AHYQ, AHYC, MARK, MAINT, CLEAR, MOVE, BCAST, SAMO, SAMIS, SAMIU, HEAD_PLUS1, HEAD_PLUS2, HEAD_PLUS3, HEAD_PLUS4, GTT,
    SACK, DACK_DAL, DACK_DALG, DACK_DALN, DACK_GO, DACKZ, DACKD, DAHY, DRQG, DRQZ, DAHYZ, DAHYX, DRQX, RLA, SITH
};
struct MTInfo { MT t; const char* code; const char* description; };
static const MTInfo MT_INFO[] = {
    {MT::UNKN, "UNKN", "Unknown"}, {MT::GTC, "GTC", "GTC - Goto Channel"}, {MT::ALH, "ALH", "ALH - Aloha"},
    {MT::ALHS, "ALHS", "ALHS - Standard Data Excluded"}, {MT::ALHD, "ALHD", "ALHD - Simple Calls Excluded"},
    {MT::ALHE, "ALHE", "ALHE - Emergency Calls Only"}, {MT::ALHR, "ALHR", "ALHR - Emergency or Registration"},
    {MT::ALHX, "ALHX", "ALHX - Registration Excluded"}, {MT::ALHF, "ALHF", "ALHF - Fallback Mode"},
    {MT::ACK, "ACK", "ACK - Acknowledge"}, {MT::ACKI, "ACKI", "ACKI - More To Follow"},
    {MT::ACKQ, "ACKQ", "ACKQ - Call Queued"}, {MT::ACKX, "ACKX", "ACKX - Message Rejected"},
    {MT::ACKV, "ACKV", "ACKV - Called Unit Unavailable"}, {MT::ACKE, "ACKE", "ACKE - Emergency"},
    {MT::ACKT, "ACKT", "ACKT - Try On Given Address"}, {MT::ACKB, "ACKB", "ACKB - Call Back/Negative Ack"},
    {MT::AHOY, "AHOY", "AHOY - General Availability Check"}, {MT::AHYX, "AHYX", "AHYX - Cancel Alert/Waiting Status"},
    {MT::AHYP, "AHYP", "AHYP - Called Unit Presence Monitoring"}, {MT::AHYQ, "AHYQ", "AHYQ - Status Message"},
    {MT::AHYC, "AHYC", "AHYC - Short Data Message"}, {MT::MARK, "MARK", "MARK - Control Channel Marker"},
    {MT::MAINT, "MAINT", "MAINT - Call Maintenance Message"}, {MT::CLEAR, "CLEAR", "CLEAR - Down From Allocated Channel"},
    {MT::MOVE, "MOVE", "MOVE - To Specified Channel"}, {MT::BCAST, "BCAST", "BCAST - System Parameters"},
    {MT::SAMO, "SAMO", "SAMO - Outbound Single Address"}, {MT::SAMIS, "SAMIS", "SAMIS - Inbound Solicited Single Address"},
    {MT::SAMIU, "SAMIU", "SAMIU - Inbound Unsolicited Single Address"}, {MT::HEAD_PLUS1, "HEAD_PLUS1", "HEAD - 1 DATA CODEWORD"},
    {MT::HEAD_PLUS2, "HEAD_PLUS2", "HEAD - 2 DATA CODEWORDS"}, {MT::HEAD_PLUS3, "HEAD_PLUS3", "HEAD - 3 DATA CODEWORDS"},
    {MT::HEAD_PLUS4, "HEAD_PLUS4", "HEAD - 4 DATA CODEWORDS"}, {MT::GTT, "GTT", "GTT - Go To Transaction"},
    {MT::SACK, "SACK", "SACK - Standard Data Selective Ack Header"}, {MT::DACK_DAL, "DACK_DAL", "DACK - Data Ack + DAL"},
    {MT::DACK_DALG, "DACK_DALG", "DACK - Data Ack + DALG"}, {MT::DACK_DALN, "DACK_DALN", "DACK - Data Ack + DALN"},
    {MT::DACK_GO, "DACK_GO", "DACK - GO Fragment Transmit Invitation"}, {MT::DACKZ, "DACKZ", "DACKZ - Data Ack For Expedited Data"},
    {MT::DACKD, "DACKD", "DACKD - Data Ack For Standard Data"}, {MT::DAHY, "DAHY", "DAHY - Standard Data Ahoy"},
    {MT::DRQG, "DRQG", "DRQG - Repeat Group Message"}, {MT::DRQZ, "DRQZ", "DRQZ - Request Containing Expedited Data"},
    {MT::DAHYZ, "DAHYZ", "DAHYZ - Expedited Data Ahoy"}, {MT::DAHYX, "DAHYX", "DAHYX - Standard Data For Closing TRANS"},
    {MT::DRQX, "DRQX", "DRQX - Request To Close A Transaction"}, {MT::RLA, "RLA", "RLA - Repeat Last ACK"},
    {MT::SITH, "SITH", "SITH - Standard Data Address Codeword Data Item"}};

static const MTInfo& info(MT t)
{
    for(const auto& i : MT_INFO) { if(i.t == t) { return i; } }
    return MT_INFO[0];
}

// MPTMessageType.fromNumber()
static MT messageTypeFromNumber(int n)
{
    if(n < 256) { return MT::GTC; }
    switch(n)
    {
        case 256: return MT::ALH;
        case 257: return MT::ALHS;
        case 258: return MT::ALHD;
        case 259: return MT::ALHE;
        case 260: return MT::ALHR;
        case 261: return MT::ALHX;
        case 262: return MT::ALHF;
        case 264: return MT::ACK;
        case 265: return MT::ACKI;
        case 266: return MT::ACKQ;
        case 267: return MT::ACKX;
        case 268: return MT::ACKV;
        case 269: return MT::ACKE;
        case 270: return MT::ACKT;
        case 271: return MT::ACKB;
        case 272: return MT::AHOY;
        case 274: return MT::AHYX;
        case 277: return MT::AHYP;
        case 278: return MT::AHYQ;
        case 279: return MT::AHYC;
        case 280: return MT::MARK;
        case 281: return MT::MAINT;
        case 282: return MT::CLEAR;
        case 283: return MT::MOVE;
        case 284: return MT::BCAST;
        case 416: return MT::DACK_DAL;
        case 417: return MT::DACK_DALG;
        case 418: return MT::DACK_DALN;
        case 419: return MT::DACK_GO;
        case 420: return MT::DACKZ;
        case 421: return MT::DACKD;
        case 424: return MT::DAHY;
        case 426: return MT::DRQG;
        case 428: return MT::DAHYZ;
        case 430: return MT::DAHYX;
        default: break;
    }
    if(n >= 288 && n <= 303) { return MT::SAMO; }
    if(n >= 304 && n <= 307) { return MT::HEAD_PLUS1; }
    if(n >= 308 && n <= 311) { return MT::HEAD_PLUS2; }
    if(n >= 312 && n <= 315) { return MT::HEAD_PLUS3; }
    if(n >= 316 && n <= 319) { return MT::HEAD_PLUS4; }
    if(n >= 320 && n <= 335) { return MT::GTT; }
    if(n >= 440 && n <= 443) { return MT::SITH; }
    return MT::UNKN;
}

// IdentType
enum class IT { ALLI, DIVERTI, DNI, DUMMYI, INCI, IPFIXI, PABXI, PSTNGI, PSTNSI, REGI, RESERVED, SDMI, SPARE, TSCI, USER, UNKNOWN };
struct IdentInfo { IT t; int pstn; const char* label; };    // pstn: 1..15 for PSTNSIn, else 0

static IdentInfo identFromValue(int ident)
{
    switch(ident)
    {
        case 0: return {IT::DUMMYI, 0, "DUMMY IDENT"};
        case 8101: return {IT::PSTNGI, 0, "PSTN GATEWAY"};
        case 8102: return {IT::PABXI, 0, "PABX GATEWAY"};
        case 8103: return {IT::DNI, 0, "DATA NETWORK GATEWAY"};
        case 8181: case 8182: case 8183: case 8184: return {IT::RESERVED, 0, "RESERVED"};
        case 8185: return {IT::REGI, 0, "REGISTRATION"};
        case 8186: return {IT::INCI, 0, "INCLUDE IN CALL"};
        case 8187: return {IT::DIVERTI, 0, "DIVERT"};
        case 8188: return {IT::SDMI, 0, "SHORT DATA MESSAGE"};
        case 8189: return {IT::IPFIXI, 0, "INTER-PREFIX"};
        case 8190: return {IT::TSCI, 0, "SYSTEM CONTROLLER"};
        case 8191: return {IT::ALLI, 0, "SYSTEM-WIDE"};
        default: break;
    }
    if(ident >= 8121 && ident <= 8135)
    {
        static std::vector<std::string> labels;
        if(labels.empty()) { for(int i = 1; i <= 15; i++) { labels.push_back("PSTN OR NETWORK " + std::to_string(i)); } }
        return {IT::PSTNSI, ident - 8120, labels[(size_t)(ident - 8121)].c_str()};
    }
    if(1 <= ident && ident <= 8100) { return {IT::USER, 0, "COMMON-PREFIX IDENT"}; }
    if((8104 <= ident && ident <= 8120) || (8136 <= ident && ident <= 8180)) { return {IT::SPARE, 0, "SPARE"}; }
    return {IT::UNKNOWN, 0, "UNKNOWN"};
}

static std::string pad(const std::string& v, size_t places) { return v.size() >= places ? v : v + std::string(places - v.size(), ' '); }
static std::string fmt(int number, int places)
{
    std::string s = std::to_string(number);
    return s.size() >= (size_t)places ? s : std::string((size_t)places - s.size(), '0') + s;
}

// SDMFormat
enum class SDMF { MPT1327, BINARY, BCD, TELEX, ASCII, RESERVED, SPARE, COMMAND, MAP27, UNKNOWN };
static SDMF sdmFormat(bool stf, int gfi)
{
    if(!stf) { return SDMF::MPT1327; }
    switch(gfi)
    {
        case 0: return SDMF::BINARY;
        case 1: return SDMF::BCD;
        case 2: return SDMF::TELEX;
        case 3: return SDMF::ASCII;
        case 4: return SDMF::RESERVED;
        case 5: return SDMF::SPARE;
        case 6: return SDMF::COMMAND;
        case 7: return SDMF::MAP27;
        default: return SDMF::UNKNOWN;
    }
}

namespace {

struct Msg
{
    BitMsg& m;
    MT type;

    int ident1() const { return (int)m.getInt(B1_IDENT1); }
    int prefix() const { return (int)m.getInt(B1_PREFIX); }
    int block2Prefix() const { return (int)m.getInt(B2_PREFIX); }
    int block2Ident2() const { return (int)m.getInt(B2_IDENT2); }
    int ident2() const
    {
        if(type == MT::GTC) { return (int)m.getInt(B1_IDENT2_GTC); }
        if(type == MT::HEAD_PLUS1 || type == MT::HEAD_PLUS2 || type == MT::HEAD_PLUS3 || type == MT::HEAD_PLUS4)
        {
            return (int)m.getInt(B1_IDENT2_HEAD);
        }
        return (int)m.getInt(B1_IDENT2);
    }
    int channel() const
    {
        switch(type)
        {
            case MT::CLEAR: return (int)m.getInt(B1_TRAFFIC_CHANNEL);
            case MT::GTC: return (int)m.getInt(B1_GTC_CHAN);
            default: return (int)m.getInt(B1_CHANNEL);
        }
    }
    int returnToChannel() const { return type == MT::CLEAR ? (int)m.getInt(B1_CONTROL_CHANNEL) : 0; }

    // getSiteID(): null (printed "null" by StringBuilder.append) unless BCAST or ALH
    bool hasSiteId() const { return type == MT::BCAST || type == MT::ALH; }
    std::string siteId() const
    {
        if(type == MT::BCAST) { return std::to_string(m.getInt(B1_SYSTEM_ID)); }
        if(type == MT::ALH) { return std::to_string(m.getInt(B2_SYSTEM_ID)); }
        return "null";
    }

    std::string fromId() const
    {
        int id2 = ident2();
        if(identFromValue(id2).t == IT::IPFIXI) { return fmt(block2Prefix(), 3) + "-" + fmt(block2Ident2(), 4); }
        return fmt(prefix(), 3) + "-" + fmt(id2, 4);
    }
    std::string toId() const
    {
        int pre = prefix(), id = ident1();
        IdentInfo t = identFromValue(id);
        switch(t.t)
        {
            case IT::IPFIXI: return "INTER-PREFIX";
            case IT::ALLI: return "ALL RADIOS";
            case IT::PABXI: return "PABX EXT";
            case IT::PSTNSI: return "PRE-DEFINED PSTN";
            case IT::PSTNGI: return "PSTN GATEWAY";
            case IT::TSCI: return "SYSTEM CONTROLLER";
            case IT::DIVERTI: return "CALL DIVERT";
            default:
                if(pre != 0 || id != 0) { return fmt(pre, 3) + "-" + fmt(id, 4); }
                return "";
        }
    }

    const char* slotsLabel() const
    {
        switch(m.getInt(B1_SLOTS))
        {
            case 0: return "RESERVED";
            case 1: return "ADDRESS CODEWORD ONLY";
            case 2: return "ADDRESS CODEWORD & 1-2 DATA CODEWORDS";
            case 3: return "ADDRESS CODEWORD & 3-4 DATA CODEWORDS";
            default: return "UNKNOWN";
        }
    }
    int slotsNumber() const { return (int)m.getInt(B1_SLOTS); }
    const char* mode2Label() const
    {
        static const char* L[8] = {"ESN", "RESERVED", "RESERVED", "N/A", "N/A", "N/A", "N/A", "RESERVED"};
        return L[m.getInt(B1_DESCRIPTOR) & 7];
    }

    std::string requestString() const
    {
        std::string sb;
        IdentInfo t2 = identFromValue(ident2());
        if(t2.t == IT::USER)
        {
            IdentInfo t1 = identFromValue(ident1());
            int slots = slotsNumber();
            std::string sl = slotsLabel();
            switch(t1.t)
            {
                case IT::DIVERTI: sb = "SEND BLOCKED ADDRESS FOR THIRD-PARTY CALL DIVERSION USING " + sl; break;
                case IT::IPFIXI: sb = "SEND INTER-PREFIX CALL EXTENDED ADDRESSING INFORMATION USING " + sl; break;
                case IT::PABXI: sb = "SEND PABX EXTENSION USING" + sl; break;     // no space before the slots label upstream
                case IT::PSTNGI:
                    if(slots == 1) { sb = "SEND UP TO 9 PSTN DIALED DIGITS"; }
                    else if(slots == 2) { sb = "SEND 10 TO 31 PSTN DIALED DIGITS"; }
                    else { sb = "SEND PSTN DIALED DIGITS USING " + sl; }
                    break;
                case IT::SDMI: sb = "SEND SHORT DATA MESSAGE USING " + sl; break;
                default: break;
            }
        }
        else
        {
            sb = std::string("SEND ") + mode2Label();
        }
        return sb;
    }

    std::string statusMessage() const
    {
        int status = (int)m.getInt(B1_STATUS_MESSAGE);
        if(status == 0) { return "STATUS: Request Speech Call"; }
        if(status == 31) { return "STATUS: Cancel Request Speech Call"; }
        return "STATUS: " + std::to_string(status);
    }

    int blocksOf(MT t) const { return t == MT::HEAD_PLUS1 ? 1 : t == MT::HEAD_PLUS2 ? 2 : t == MT::HEAD_PLUS3 ? 3 : t == MT::HEAD_PLUS4 ? 4 : 0; }

    std::string sdmBinary(SDMF format) const
    {
        bool mpt = (format == SDMF::MPT1327);
        int s2 = mpt ? B2_SDM_STF0_START : B2_SDM_STF1_START, e2 = mpt ? B2_SDM_STF0_END : B2_SDM_STF1_END;
        int s4 = mpt ? B4_SDM_STF0_START : B4_SDM_STF1_START, e4 = mpt ? B4_SDM_STF0_END : B4_SDM_STF1_END;
        std::string sb;
        switch(type)
        {
            case MT::HEAD_PLUS1: sb = m.getHex(s2, e2); break;
            case MT::HEAD_PLUS2: sb = m.getHex(s2, e2) + " " + m.getHex(B3_SDM_START, B3_SDM_END); break;
            case MT::HEAD_PLUS3:
                sb = m.getHex(s2, e2) + " " + m.getHex(B3_SDM_START, B3_SDM_END) + " " + m.getHex(s4, e4);
                break;
            case MT::HEAD_PLUS4:
                sb = m.getHex(s2, e2) + " " + m.getHex(B3_SDM_START, B3_SDM_END) + " " + m.getHex(s4, e4) + " " +
                     m.getHex(B5_SDM_START, B5_SDM_END);
                break;
            default: break;
        }
        return sb;
    }

    static void putChar(std::string& sb, uint32_t v) { sb.push_back((char)v); }

    std::string sdmAscii() const
    {
        std::string sb;
        int blocks = blocksOf(type);
        if(blocks >= 1)
        {
            putChar(sb, m.getRange(91, 97)); putChar(sb, m.getRange(98, 104)); putChar(sb, m.getRange(105, 111));
            putChar(sb, m.getRange(112, 118)); putChar(sb, m.getRange(119, 125));
        }
        if(blocks >= 2)
        {
            static const int a[] = {126, 127, 128, 129, 130, 131, 150};
            putChar(sb, m.getInt(a));
            putChar(sb, m.getRange(151, 157)); putChar(sb, m.getRange(158, 164)); putChar(sb, m.getRange(165, 171));
            putChar(sb, m.getRange(172, 178)); putChar(sb, m.getRange(179, 185)); putChar(sb, m.getRange(186, 192));
        }
        if(blocks >= 3)
        {
            static const int a[] = {193, 194, 195, 218, 219, 220, 221};
            putChar(sb, m.getInt(a));
            putChar(sb, m.getRange(222, 228)); putChar(sb, m.getRange(229, 235)); putChar(sb, m.getRange(236, 242));
            putChar(sb, m.getRange(243, 249)); putChar(sb, m.getRange(250, 256));
        }
        if(blocks == 4)
        {
            static const int a[] = {257, 258, 259, 278, 279, 280, 281};
            putChar(sb, m.getInt(a));
            putChar(sb, m.getRange(282, 288)); putChar(sb, m.getRange(289, 295)); putChar(sb, m.getRange(296, 302));
            putChar(sb, m.getRange(303, 309)); putChar(sb, m.getRange(310, 316)); putChar(sb, m.getRange(317, 323));
        }
        return sb;
    }

    static std::string bcd(uint32_t v) { return v <= 12 ? BCD[v] : "?"; }

    std::string sdmBcd() const
    {
        std::string sb;
        int blocks = blocksOf(type);
        if(blocks >= 1)
        {
            for(int s = 90; s <= 126; s += 4) { sb += bcd(m.getRange(s, s + 3)); }
        }
        if(blocks >= 2)
        {
            static const int a[] = {130, 131, 150, 151};
            sb += bcd(m.getInt(a));
            for(int s = 152; s <= 192; s += 4) { sb += bcd(m.getRange(s, s + 3)); }
        }
        if(blocks >= 3)
        {
            for(int s = 218; s <= 254; s += 4) { sb += bcd(m.getRange(s, s + 3)); }
        }
        if(blocks == 4)
        {
            static const int a[] = {258, 259, 278, 279};
            sb += bcd(m.getInt(a));
            for(int s = 280; s <= 316; s += 4) { sb += bcd(m.getRange(s, s + 3)); }
            sb += bcd(m.getRange(320, 232));          // upstream bug: end < start reads as 0; kept
        }
        return sb;
    }

    std::string sdmTelex() const
    {
        std::vector<uint32_t> values;
        int blocks = blocksOf(type);
        if(blocks >= 1)
        {
            for(int s = 91; s <= 126; s += 5) { values.push_back(m.getRange(s, s + 4)); }
        }
        if(blocks >= 2)
        {
            static const int a[] = {131, 150, 151, 152, 153};
            values.push_back(m.getInt(a));
            for(int s = 154; s <= 189; s += 5) { values.push_back(m.getRange(s, s + 4)); }
        }
        if(blocks >= 3)
        {
            static const int a[] = {194, 195, 218, 219, 220};
            values.push_back(m.getInt(a));
            for(int s = 221; s <= 241; s += 5) { values.push_back(m.getRange(s, s + 4)); }
            values.push_back(m.getRange(246, 250));
        }
        if(blocks == 4)
        {
            static const int a[] = {251, 252, 253, 254, 278};
            values.push_back(m.getInt(a));
            for(int s = 279; s <= 319; s += 5) { values.push_back(m.getRange(s, s + 4)); }
        }
        std::string sb;
        bool figure = false;
        for(uint32_t v : values)
        {
            if(v < 32)
            {
                if(v == 28) { figure = false; }
                else if(v == 29) { figure = true; }
                else { sb += figure ? TELEX_FIGURES[v] : TELEX_LETTERS[v]; }
            }
        }
        return sb;
    }

    std::string sdm() const
    {
        std::string sb = "SDM:";
        switch(type)
        {
            case MT::HEAD_PLUS1: case MT::HEAD_PLUS2: case MT::HEAD_PLUS3: case MT::HEAD_PLUS4:
            {
                SDMF f = sdmFormat(m.get(B2_SDM_SEGMENT_TRANSACTION_FLAG), (int)m.getInt(B2_SDM_GENERAL_FORMAT));
                switch(f)
                {
                    case SDMF::MPT1327: sb += "MPT1327 " + sdmBinary(f); break;
                    case SDMF::BINARY: sb += "BINARY " + sdmBinary(f); break;
                    case SDMF::COMMAND: sb += "COMMAND " + sdmBinary(f); break;
                    case SDMF::MAP27: sb += "MAP27 " + sdmBinary(f); break;
                    case SDMF::RESERVED: sb += "RESERVED " + sdmBinary(f); break;
                    case SDMF::SPARE: sb += "SPARE " + sdmBinary(f); break;
                    case SDMF::ASCII: sb += "ASCII " + sdmAscii(); break;
                    case SDMF::BCD: sb += "BCD " + sdmBcd(); break;
                    case SDMF::TELEX: sb += "TELEX " + sdmTelex(); break;
                    default: sb += "UNKNOWN "; break;
                }
                break;
            }
            case MT::SAMO: sb += "SAMO "; break;
            case MT::SAMIS: sb += "SAMIS "; break;
            case MT::SAMIU: sb += "SAMIU "; break;
            default: break;
        }
        return sb;
    }

    // MPT1327Message.getMessage()
    std::string message() const
    {
        std::string sb = pad(info(type).description, 5);
        auto fromTo = [&]() {
            // hasFromID() is always true; hasToID() when the to-id string is not empty
            std::string s = " FROM:" + fromId();
            if(!toId().empty()) { s += " TO:" + toId(); }
            return s;
        };
        switch(type)
        {
            case MT::ACK:
            {
                sb += " ACKNOWLEDGE ";
                sb += fromId();
                IdentInfo t = identFromValue(ident1());
                switch(t.t)
                {
                    case IT::ALLI: case IT::IPFIXI: case IT::PABXI: case IT::PSTNGI: case IT::PSTNSI: case IT::USER:
                        sb += std::string(t.label) + " CALL REQUEST";
                        break;
                    case IT::TSCI: sb += " RQQ or RQC TRANSACTION"; break;
                    case IT::DIVERTI: sb += " CANCELLATION OF CALL DIVERSION REQUEST"; break;
                    default: sb += std::string(" ") + t.label + " REQUEST"; break;
                }
                break;
            }
            case MT::ACKI:
                sb += " MESSAGE ACKNOWLEDGED - MORE TO FOLLOW" + fromTo();
                break;
            case MT::ACKT:
                sb += " SITE:" + siteId() + " LONG ACK MESSAGE" + fromTo() + "**********************************";
                break;
            case MT::ACKQ:
                sb += " SYSTEM:" + siteId() + " CALL QUEUED FROM:" + fromTo();
                break;
            case MT::ACKX:
                sb += " SYSTEM:" + siteId() + " MESSAGE REJECTED FROM:" + fromTo();
                break;
            case MT::AHYC:
                sb += " FM:" + fromId() + " TO:" + toId() + " " + requestString();
                break;
            case MT::AHYQ:
                sb += " STATUS MESSAGE" + fromTo() + " " + statusMessage();
                break;
            case MT::ALH: case MT::ALHD: case MT::ALHS: case MT::ALHE: case MT::ALHR: case MT::ALHX: case MT::ALHF:
                sb += " SYSTEM:" + siteId();
                if(!toId().empty()) { sb += " ID:" + toId(); }
                sb += " WAIT:" + std::to_string(m.getInt(B1_WAIT_TIME));
                sb += " RSVD:" + std::to_string(m.getInt(B1_ALOHA_RSVD));
                sb += " M:" + std::to_string(m.getInt(B1_ALOHA_M));
                sb += " N:" + std::to_string(m.getInt(B1_ALOHA_N));
                break;
            case MT::BCAST:
            {
                sb += " SYSTEM:" + siteId();
                int sd = (int)m.getInt(B1_SYSDEF);
                static const char* LAB[6] = {"ANNOUNCE CONTROL CHANNEL", "WITHDRAW CONTROL CHANNEL", "CALL MAINT PARAMETERS",
                                             "REGISTRATION PARAMETERS", "NEIGHBOR", "VOTE NOW"};
                sb += std::string(" ") + (sd >= 0 && sd <= 5 ? LAB[sd] : "UNKNOWN");
                switch(sd)
                {
                    case 0: case 1:
                        sb += " CHAN:" + std::to_string(channel());
                        break;
                    case 4:
                        sb += " CHAN:" + std::to_string(channel()) + " SER:" + std::to_string(m.getInt(B1_ADJSITE));
                        break;
                    case 2:
                        sb += std::string(" PERIODIC MAINT MSG REQD:") + (m.get(B1_PERIODIC_CALL_MAINT_MESSAGES) ? "true" : "false");
                        sb += " INTERVAL:" + std::to_string(m.getInt(B1_MAINT_MESSAGE_INTERVAL));
                        sb += std::string("(sec) PRESSEL ON REQD:") + (m.get(B1_PRESSEL_ON_REQUIRED) ? "true" : "false");
                        sb += std::string(" IDENT1 VALUE:") + (m.get(B1_IDENT1_ID_VALUE) ? "GROUP ADDRESS" : "INDIVIDUAL ADDRESS");
                        break;
                    default: break;
                }
                break;
            }
            case MT::CLEAR:
                sb += " TRAFFIC CHANNEL:" + std::to_string(channel()) + " RETURN TO CONTROL CHANNEL:" + std::to_string(returnToChannel());
                break;
            case MT::GTC:
                sb += fromTo() + " CHAN:" + std::to_string(channel());
                break;
            case MT::MAINT:
                if(!toId().empty()) { sb += " ID:" + toId(); }
                break;
            case MT::HEAD_PLUS1: case MT::HEAD_PLUS2: case MT::HEAD_PLUS3: case MT::HEAD_PLUS4:
                sb += " " + sdm();
                break;
            default: break;
        }
        return sb;
    }
};

}  // namespace

MPT1327Message decodeMpt1327(BitMsg m)
{
    MPT1327Message o;
    CrcResult crc[5] = {CrcResult::UNKNOWN, CrcResult::UNKNOWN, CrcResult::UNKNOWN, CrcResult::UNKNOWN, CrcResult::UNKNOWN};
    bool checked[5] = {false, false, false, false, false};
    auto check = [&](int section, int start, int end) {
        crc[section] = fleetsyncDetectAndCorrect(m, start, end);
        checked[section] = true;
    };
    check(0, BLOCK_1_START, BLOCK_2_START);
    o.valid = (crc[0] == CrcResult::PASSED || crc[0] == CrcResult::CORRECTED);
    MT type = MT::UNKN;
    if(o.valid)
    {
        type = messageTypeFromNumber((int)m.getInt(B1_MESSAGE_TYPE));
        switch(type)
        {
            case MT::AHYQ: case MT::ALH: case MT::HEAD_PLUS1:
                check(1, BLOCK_2_START, BLOCK_3_START);
                break;
            case MT::HEAD_PLUS2:
                check(1, BLOCK_2_START, BLOCK_3_START);
                check(2, BLOCK_3_START, BLOCK_4_START);
                break;
            case MT::ACKT: case MT::HEAD_PLUS3:
                check(1, BLOCK_2_START, BLOCK_3_START);
                check(2, BLOCK_3_START, BLOCK_4_START);
                check(3, BLOCK_4_START, BLOCK_5_START);
                break;
            case MT::HEAD_PLUS4:
                check(1, BLOCK_2_START, BLOCK_3_START);
                check(2, BLOCK_3_START, BLOCK_4_START);
                check(3, BLOCK_4_START, BLOCK_5_START);
                check(4, BLOCK_5_START, BLOCK_6_START);
                break;
            default: break;
        }
    }
    o.parity = "[";
    for(int i = 0; i < 5; i++)
    {
        if(!checked[i]) { o.parity += "-"; continue; }
        switch(crc[i])
        {
            case CrcResult::PASSED: o.parity += "*"; break;
            case CrcResult::CORRECTED: o.parity += "C"; break;
            case CrcResult::FAILED_CRC: o.parity += "f"; break;
            case CrcResult::FAILED_PARITY: o.parity += "p"; break;
            default: o.parity += "-"; break;
        }
    }
    o.parity += "]";
    o.typeCode = info(type).code;
    o.typeText = info(type).description;
    o.rawHex = m.toHex();
    if(!o.valid) { return o; }

    Msg msg{m, type};
    o.prefix = msg.prefix();
    o.ident1 = msg.ident1();
    o.ident2 = msg.ident2();
    o.channel = msg.channel();
    o.fromId = msg.fromId();
    o.toId = msg.toId();
    if(msg.hasSiteId()) { o.siteId = msg.siteId(); }
    o.text = msg.message();
    return o;
}

MPT1327Decoder::MPT1327Decoder(bool french, Sink sink)
    : mControl(syncMpt1327Control(french), MESSAGE_LENGTH), mTraffic(syncMpt1327Traffic(french), MESSAGE_LENGTH),
      mSink(std::move(sink))
{
    mControl.setListener([this](const BitMsg& b) {
        MPT1327Message d = decodeMpt1327(b);
        if(d.valid) { if(mSink) { mSink(d, true); } } else { invalid++; }
    });
    mTraffic.setListener([this](const BitMsg& b) {
        MPT1327Message d = decodeMpt1327(b);
        if(d.valid) { if(mSink) { mSink(d, false); } } else { invalid++; }
    });
}

}  // namespace trunkfmt
