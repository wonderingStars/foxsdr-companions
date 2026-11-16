// See common.h for the licence notice.  SyncPattern.java constants.
#include "framer.h"

namespace trunkfmt {

static std::vector<bool> fromBits(uint32_t value, int n)
{
    std::vector<bool> v;
    for(int i = n - 1; i >= 0; i--) { v.push_back(((value >> i) & 1u) != 0); }
    return v;
}

const std::vector<bool>& syncLJ1200()
{
    // false,true,false,true, false,true,false,true, false,false,false,false, true,true,true,true
    static const std::vector<bool> s = fromBits(0x550F, 16);
    return s;
}
const std::vector<bool>& syncLJ1200Transponder()
{
    // false,false,true,false, true,false,true,false, true,true,false,true, false,true,false,true
    static const std::vector<bool> s = fromBits(0x2AD5, 16);
    return s;
}
const std::vector<bool>& syncTaitGps()
{
    // true,false,true,false, false,false,false,true, false,true,false,false, true,true,false,true, true,true,false,false
    static const std::vector<bool> s = fromBits(0xA14DC, 20);
    return s;
}
const std::vector<bool>& syncTaitSelcal()
{
    // false,true,false,false, false,false,false,true, true,false,false,false, true,true,true,false, true,true,true,false
    static const std::vector<bool> s = fromBits(0x418EE, 20);
    return s;
}

const std::vector<bool>& syncFleetsync2()
{
    // false,true,false,true,false (end of bit revs), then 0x23EB = 0010 0011 1110 1011
    static const std::vector<bool> s = fromBits(0xA23EB, 21);
    return s;
}

const std::vector<bool>& syncMpt1327Control(bool french)
{
    // 1010 1100 0100 1101 0111 (4 rev bits + 0xC4D7)   /  French: 1010 1011 0100 0011 0011 (4 rev bits + 0xB433)
    static const std::vector<bool> n = fromBits(0xAC4D7, 20), fr = fromBits(0xAB433, 20);
    return french ? fr : n;
}
const std::vector<bool>& syncMpt1327Traffic(bool french)
{
    // 1010 0011 1011 0010 1000 (4 rev bits + 0x3B28)   /  French: 1010 0100 1011 1100 1100 (4 rev bits + 0x4BCC)
    static const std::vector<bool> n = fromBits(0xA3B28, 20), fr = fromBits(0xA4BCC, 20);
    return french ? fr : n;
}

const std::vector<bool>& syncLtrOsw()
{
    // SyncPattern.LTR_STANDARD_OSW = PASSPORT = 101011000 (0x158)
    static const std::vector<bool> s = fromBits(0x158, 9);
    return s;
}
const std::vector<bool>& syncLtrIsw()
{
    // SyncPattern.LTR_STANDARD_ISW = 010100111 (0xA7)
    static const std::vector<bool> s = fromBits(0xA7, 9);
    return s;
}
const std::vector<bool>& syncPassport() { return syncLtrOsw(); }     // SyncPattern.PASSPORT has the same bits as LTR_STANDARD_OSW

}  // namespace trunkfmt
