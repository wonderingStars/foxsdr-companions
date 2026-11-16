// trunkctl - EDACS (standard and extended addressing) 9600 baud control channel decoder.
//
// Frame structure, message layouts and the sync word follow lwvmobile/edacs-fm (edacs-fm.c); the BCH(40,28)
// code is implemented independently in bch4028.cpp (edacs-fm's bch3.h is under a non-commercial licence and is
// not used). Error correction is an addition: edacs-fm only detects errors.
//
// This file is part of trunkctl and is licensed under the GNU General Public License, version 3.
#ifndef TRUNKCTL_EDACS_H
#define TRUNKCTL_EDACS_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common.h"
#include "emitter.h"
#include "slicer.h"

namespace trunkctl {

struct EdacsOptions {
    int variant = 0;             // 0 auto, 1 standard (and networked), 2 extended addressing
    bool esk = false;            // command byte is XOR 0xA0 (ESK)
    int afs_a = 4, afs_f = 4;    // agency and fleet bit lengths of the 11-bit AFS (subfleet gets the rest)
    int bch_correct = 1;         // bit errors corrected per 40-bit message (0..2)
    std::map<int, double> lcn_mhz; // logical channel number -> MHz
    double update_interval = 2.0;
    bool raw = false;
    int polarity = 0;            // 0 auto, 1 normal, 2 inverted
};

// One 9600 baud framer: 48-bit sync, then 240 bits (two 40-bit messages, each sent three times).
class EdacsFramer {
public:
    explicit EdacsFramer(int max_sync_errors) : max_err_(max_sync_errors) {}
    // true when a complete frame (240 bits after the sync) is in bits()
    bool sym(int bit);
    const uint8_t *bits() const { return bits_; }

private:
    int max_err_;
    uint64_t sr_ = 0;
    bool collecting_ = false;
    int count_ = 0;
    uint8_t bits_[240];
};

class EdacsDecoder : public BitSink {
public:
    EdacsDecoder(Emitter &em, double rate, double sps, const EdacsOptions &o, Stats &st);
    void bit(int phase, int b, uint64_t end_sample) override;
    void tick(uint64_t sample);
    void finish(uint64_t sample);
    bool locked() const { return locked_; }

private:
    struct Cand {
        bool valid = false;
        uint64_t first_end = 0;
        bool ok_a = false, ok_b = false;
        uint64_t a = 0, b = 0; // corrected 40-bit messages
        int err_a = 0, err_b = 0;
        int score() const { return (ok_a ? 0 : 100) + (ok_b ? 0 : 100) + err_a + err_b; }
    };
    void frame(int phase, int pol, uint64_t end);
    void release();
    void handle(const Cand &c);
    void handle_ea(const Cand &c);
    void handle_std(const Cand &c);
    void emit_call(uint64_t t, const char *variant, const char *mode, int lcn, int64_t tgid, int64_t rid, int status,
                   int64_t afs);
    Line sysinfo(const char *kind, uint64_t t);
    double lcn_mhz(int lcn) const;

    Emitter &em_;
    EdacsOptions opt_;
    Stats &st_;
    std::vector<std::unique_ptr<EdacsFramer>> fr_;
    double sps_;
    double rate_;
    uint64_t min_gap_, lost_after_, interval_;
    Cand pend_;
    int variant_;
    int phases_ = 4;
    bool locked_ = false;
    uint64_t last_ok_ = 0;
    uint32_t xmask_;
    Throttle thr_;
};

} // namespace trunkctl

#endif
