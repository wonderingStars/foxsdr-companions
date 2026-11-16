// trunkctl - Motorola SmartNet / SmartZone (Type II) control channel decoder.
//
// Framing, de-interleave, error correction and CRC are a C++ port of rx_smartnet.cc (OP25, Graham J. Norbury,
// as shipped in trunk-recorder lib/op25_repeater/lib/). The outbound signalling word (OSW) meanings and the
// band plans are ported from trunk-recorder/systems/smartnet_parser.cc. See UPSTREAM.md for the list of changes.
//
// Copyright (C) 2020 Graham J. Norbury (rx_smartnet); the trunk-recorder authors (smartnet_parser).
// This file is part of trunkctl and is licensed under the GNU General Public License, version 3.
#ifndef TRUNKCTL_SMARTNET_H
#define TRUNKCTL_SMARTNET_H

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "common.h"
#include "emitter.h"
#include "slicer.h"

namespace trunkctl {

struct SmartnetOptions {
    std::string bandplan = "none"; // none | 800_standard | 800_reband | 800_splinter | 900 | obt
    double obt_base = 0, obt_high = 0, obt_spacing = 0; // MHz (obt only)
    int obt_offset = 0;                                  // channel number of obt_base (obt only)
    double update_interval = 2.0;                        // seconds; 0 = report every repeat
    bool raw = false;                                    // also print every valid OSW as an "osw" event
    int polarity = 0;                                    // 0 auto, 1 normal, 2 inverted
};

// Channel number -> frequency (port of SmartnetParser::get_freq / is_chan).
class BandPlan {
public:
    enum Kind { None, S800, R800, Spl800, B900, Obt };
    bool configure(const SmartnetOptions &o, std::string *err);
    bool is_chan(int chan, bool tx) const;
    double freq_mhz(int chan, bool tx) const; // 0 when unknown
    bool is_obt() const { return kind_ == Obt; }
    bool known() const { return kind_ != None; }

private:
    Kind kind_ = None;
    double base_ = 0, high_ = 0, spacing_ = 0;
    int offset_ = 0;
};

struct Osw {
    uint32_t addr = 0; // 16 bits
    bool grp = false;
    uint32_t cmd = 0;  // 10 bits
    uint64_t t = 0;    // sample index at the end of the frame
    bool gap = false;  // marker: one or more OSWs are missing before the next one
    bool ch_rx = false, ch_tx = false;
    double f_rx = 0, f_tx = 0; // MHz
};

// OSW stream -> messages (port of SmartnetParser::process_osws).
class SmartnetParser {
public:
    SmartnetParser(Emitter &em, const BandPlan &bp, const SmartnetOptions &o, double rate, Stats &st);
    void push(Osw o);
    void flush();

private:
    void process(bool flushing);
    int decode();
    Line msg(const char *name, uint64_t t);
    Line sysinfo(const char *kind, uint64_t t);
    void grant(uint64_t t, const char *call, const char *mode, uint32_t raw_tg, long src, long dst, uint32_t chan,
               double f_mhz, bool update);
    void individual_ext(const Osw &o1, const Osw &o2, uint64_t t);
    void cc_info(uint64_t t, const char *kind, long sys, int chan, double f_mhz);

    Emitter &em_;
    const BandPlan &bp_;
    SmartnetOptions opt_;
    Stats &st_;
    std::deque<Osw> q_;
    Throttle thr_;
    uint64_t interval_;
};

// One 3600 baud framer (sync 0xAC, 84-symbol frame of 76 payload symbols + sync).
class SmartnetFramer {
public:
    explicit SmartnetFramer(uint64_t timeout_samples);
    // Feed one symbol (already polarity-corrected). Returns true when a CRC-valid OSW completed.
    bool sym(int bit, uint64_t sample, Osw *out);

private:
    void deinterleave(const uint8_t *buf);
    void error_correction();
    bool crc_check(Osw *out);
    uint64_t timeout_;
    uint64_t last_reset_ = 0;
    bool in_sync_ = false;
    uint8_t sync_reg_ = 0;
    uint8_t cbuf_[168];
    uint8_t raw_frame_[76];
    uint8_t ecc_frame_[37];
    unsigned cbuf_idx_ = 0;
    int rx_count_ = 0;
};

// All timing phases and both polarities, de-duplication, lock state, the parser.
class SmartnetDecoder : public BitSink {
public:
    SmartnetDecoder(Emitter &em, double rate, double sps, const SmartnetOptions &o, Stats &st);
    void bit(int phase, int b, uint64_t end_sample) override;
    void tick(uint64_t sample);
    void finish(uint64_t sample);
    bool ok() const { return ok_; }
    const std::string &error() const { return err_; }
    bool locked() const { return locked_; }

private:
    void accept(const Osw &o);
    Emitter &em_;
    SmartnetOptions opt_;
    BandPlan bp_;
    Stats &st_;
    SmartnetParser parser_;
    std::vector<std::unique_ptr<SmartnetFramer>> fr_; // [phase * 2 + polarity]
    double sps_;
    double rate_;
    uint64_t min_gap_, frame_samples_, lost_after_;
    bool have_last_ = false, locked_ = false;
    uint64_t last_end_ = 0;
    bool ok_ = true;
    std::string err_;
    int phases_ = 4;
};

} // namespace trunkctl

#endif
