// trunkctl - small pieces shared by the SmartNet and EDACS decoders. This file is part of trunkctl (GPL-3.0).
#ifndef TRUNKCTL_COMMON_H
#define TRUNKCTL_COMMON_H

#include <cstdint>
#include <map>

namespace trunkctl {

// Remembers when a call (talkgroup + channel) was last reported so that the control channel's repeats of
// the same assignment do not become a flood of events. interval_samples == 0 reports everything.
class Throttle {
public:
    // true when the event should be reported
    bool pass(uint64_t key, uint64_t sample, uint64_t interval_samples) {
        if (interval_samples == 0)
            return true;
        auto it = last_.find(key);
        if (it != last_.end() && sample - it->second < interval_samples)
            return false;
        if (last_.size() > 4096)
            last_.clear();
        last_[key] = sample;
        return true;
    }

private:
    std::map<uint64_t, uint64_t> last_;
};

struct Stats {
    uint64_t frames = 0;     // frames accepted (valid) after de-duplication across timing phases
    uint64_t bad = 0;        // EDACS: frames seen with a good sync but no valid message
    uint64_t corrected = 0;  // EDACS: messages that needed BCH correction
    uint64_t messages = 0;   // decoded protocol messages (events other than raw osw)
    uint64_t unknown = 0;    // valid frames/OSWs whose meaning is not decoded
    uint64_t gaps = 0;       // SmartNet: OSWs missing between two valid ones
    uint64_t lost = 0;       // number of times the control channel was lost
};

} // namespace trunkctl

#endif
