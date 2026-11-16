// Port of sdrtrunk module/decode/passport: PassportDecoder, PassportMessageProcessor (incl. IdleMessageFinder),
// PassportMessage, PassportBand, identifier/PassportRadioId + PassportTalkgroup, edac/CRCPassport.
// See common.h for the licence notice.
#pragma once
#include <functional>
#include <map>
#include <string>
#include "common.h"
#include "framer.h"

namespace trunkfmt {

class PassportDecoder
{
public:
    static constexpr size_t MESSAGE_LENGTH = 68;
    struct Message
    {
        std::string type;                 // SY_IDLE, CA_STRT, CA_ENDD, CA_PAGE, ID_RDIO, ID_TGAS, DA_STRT, RA_REGI, UN_KNWN
        int colorCode, site, lcn, free, typeNumber, group, radioId;
        long long lcnFrequency, freeFrequency, neighborFrequency;
        std::string siteBand, neighborBand;
        bool hasFrom, hasTo, correctedBits;
        std::string text;
        std::string rawHex;
    };
    using Sink = std::function<void(const Message&)>;
    explicit PassportDecoder(Sink sink);
    void process(bool symbol) { mFramer.process(symbol); }
    unsigned long long failedCrc = 0;

private:
    MessageFramer mFramer;
    Sink mSink;
    // PassportMessageProcessor state
    bool mIdleFound = false;
    int mIdleSiteBand = 17;               // index into the band table of the idle message found
    bool mHaveIdle = false;
    std::map<std::string, int> mIdleCounts;
    void receive(const BitMsg& m);
};

}  // namespace trunkfmt
