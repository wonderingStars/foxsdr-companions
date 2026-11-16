// Port of sdrtrunk io.github.dsheirer.bits.MessageFramer, SyncPatternMatcher and the SyncPattern
// constants used by the decoders in this helper.  See common.h for the licence notice.
#pragma once
#include <cstdint>
#include <functional>
#include <list>
#include <vector>
#include "common.h"

namespace trunkfmt {

// SyncPattern.java (only the entries this helper uses).  Bit 0 of the array is the first bit received.
const std::vector<bool>& syncLJ1200();            // SyncPattern.LJ1200            = 0x550F
const std::vector<bool>& syncLJ1200Transponder(); // SyncPattern.LJ1200_TRANSPONDER = 0x2AD5
const std::vector<bool>& syncTaitGps();           // SyncPattern.TAIT_CCDI_GPS_MESSAGE (20 bits)
const std::vector<bool>& syncTaitSelcal();        // SyncPattern.TAIT_SELCAL_MESSAGE (20 bits)
const std::vector<bool>& syncMpt1327Control(bool french);   // SyncPattern.MPT1327_CONTROL / _CONTROL_FRENCH (20 bits)
const std::vector<bool>& syncMpt1327Traffic(bool french);   // SyncPattern.MPT1327_TRAFFIC / _TRAFFIC_FRENCH (20 bits)
const std::vector<bool>& syncLtrOsw();    // SyncPattern.LTR_STANDARD_OSW (9 bits)
const std::vector<bool>& syncLtrIsw();    // SyncPattern.LTR_STANDARD_ISW (9 bits)
const std::vector<bool>& syncPassport();  // SyncPattern.PASSPORT (9 bits)
const std::vector<bool>& syncFleetsync2();        // SyncPattern.FLEETSYNC2 (5 rev bits + 0x23EB)

// SyncPatternMatcher.java (exact match only; soft mode is not used by these decoders).
class SyncPatternMatcher
{
public:
    explicit SyncPatternMatcher(const std::vector<bool>& sync)
    {
        mMask = (sync.size() >= 64) ? ~0ull : ((1ull << sync.size()) - 1ull);
        for(size_t x = 0; x < sync.size(); x++)
        {
            if(sync[x]) { mSync += 1ull << (sync.size() - 1 - x); }
        }
    }
    void receive(bool bit)
    {
        mBits = (mBits << 1) & mMask;
        if(bit) { mBits += 1; }
    }
    bool matches() const { return (mBits & mMask) == mSync; }

private:
    uint64_t mBits = 0, mMask = 0, mSync = 0;
};

// MessageFramer.java: finds the sync pattern in the bit stream and extracts messageLength bits, the
// sync pattern included.  A new assembler is started at every sync match, so overlapping messages
// are possible, exactly as upstream.
class MessageFramer
{
public:
    using Listener = std::function<void(const BitMsg&)>;

    MessageFramer(const std::vector<bool>& syncPattern, size_t messageLength)
        : mSync(syncPattern), mMatcher(syncPattern), mMessageLength(messageLength) {}

    void setListener(Listener l) { mListener = std::move(l); }

    // MessageFramer.process(boolean)
    void process(bool bit)
    {
        mMatcher.receive(bit);

        for(auto it = mAssemblers.begin(); it != mAssemblers.end();)
        {
            if(it->complete)
            {
                it = mAssemblers.erase(it);
            }
            else
            {
                // MessageAssembler.receive(): add the bit, and flush when the message is full
                it->msg.add(bit);
                if(it->msg.isFull())
                {
                    it->complete = true;
                    if(mListener) { mListener(it->msg); }
                }
                ++it;
            }
        }

        if(mMatcher.matches())
        {
            Assembler a;
            a.msg = BitMsg(mMessageLength);
            for(bool b : mSync) { a.msg.add(b); }     // MessageAssembler(int, boolean[]) pre-loads the sync pattern
            mAssemblers.push_back(std::move(a));
        }
    }

private:
    struct Assembler { BitMsg msg; bool complete = false; };
    std::vector<bool> mSync;
    SyncPatternMatcher mMatcher;
    size_t mMessageLength;
    std::list<Assembler> mAssemblers;
    Listener mListener;
};

}  // namespace trunkfmt
