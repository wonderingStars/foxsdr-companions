// trunkctl - SmartNet / SmartZone control channel decoder. See smartnet.h for the origin of each part.
// This file is part of trunkctl and is licensed under the GNU General Public License, version 3.
#include "smartnet.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace trunkctl {

// ------------------------------------------------------------------------------------------------------------
// Constants from rx_smartnet.h / frame_sync_magics.h (OP25)
static const int SMARTNET_FRAME_LENGTH = 84;
static const int SMARTNET_PAYLOAD_LENGTH = 76;
static const int SMARTNET_DATA_LENGTH = 27;
static const int SMARTNET_CRC_LENGTH = 10;
static const uint8_t SMARTNET_SYNC_MAGIC = 0xAC;
static const int SMARTNET_ID_XOR = 0x33C7;
static const int SMARTNET_CMD_XOR = 0x32A;
static const int SMARTNET_ID_INV_XOR = ~SMARTNET_ID_XOR & 0xffff;
static const int SMARTNET_CMD_INV_XOR = ~SMARTNET_CMD_XOR & 0x3ff;

// ------------------------------------------------------------------------------------------------------------
// SmartnetFramer: port of rx_smartnet::rx_sym / deinterleave / error_correction / crc_check

SmartnetFramer::SmartnetFramer(uint64_t timeout_samples) : timeout_(timeout_samples) {
    std::memset(cbuf_, 0, sizeof cbuf_);
    std::memset(raw_frame_, 0, sizeof raw_frame_);
    std::memset(ecc_frame_, 0, sizeof ecc_frame_);
}

bool SmartnetFramer::sym(int bit, uint64_t sample, Osw *out) {
    bool sync_detected = false;
    sync_reg_ = (uint8_t)(((sync_reg_ << 1) & 0xff) | (bit & 1));
    if (sync_reg_ == SMARTNET_SYNC_MAGIC)
        sync_detected = true;
    // cbuf_insert
    cbuf_[cbuf_idx_] = (uint8_t)bit;
    cbuf_[cbuf_idx_ + SMARTNET_FRAME_LENGTH] = (uint8_t)bit;
    cbuf_idx_ = (cbuf_idx_ + 1) % SMARTNET_FRAME_LENGTH;
    rx_count_++;

    // upstream: sync_timer.expired() on a wall clock; here the sample clock (CHANGE 3 in UPSTREAM.md)
    if (sample - last_reset_ > timeout_) {
        in_sync_ = false;
        rx_count_ = 0;
        last_reset_ = sample; // sync_timeout() resets the timer
        return false;
    }
    if (sync_detected && !in_sync_) { // first sync marks the start of data collection
        in_sync_ = true;
        rx_count_ = 0;
        return false;
    }
    if (!in_sync_ || rx_count_ < SMARTNET_FRAME_LENGTH)
        return false;
    if (!sync_detected) { // a frame must end with a sync
        in_sync_ = false;
        rx_count_ = 0;
        return false;
    }
    rx_count_ = 0;
    deinterleave(cbuf_ + cbuf_idx_);
    error_correction();
    if (!crc_check(out))
        return false; // frames with a failed CRC are discarded
    out->t = sample;
    last_reset_ = sample;
    return true;
}

// Incoming frame is 76 bits {1,2,3,4,...,75,76}; shuffle into position {1,20,39,58,2,21,40,59,...}
void SmartnetFramer::deinterleave(const uint8_t *buf) {
    for (int k = 0; k < (SMARTNET_PAYLOAD_LENGTH / 4); k++)
        for (int l = 0; l < 4; l++)
            raw_frame_[k * 4 + l] = buf[k + l * 19];
}

// Calculate parity and correct where possible: data bit i is flipped when both parity bits that cover it fail.
void SmartnetFramer::error_correction() {
    uint8_t expected[SMARTNET_PAYLOAD_LENGTH];
    uint8_t syndrome[SMARTNET_PAYLOAD_LENGTH];
    expected[0] = raw_frame_[0] & 1;
    expected[1] = raw_frame_[0] & 1;
    for (int k = 2; k < SMARTNET_PAYLOAD_LENGTH; k += 2) {
        expected[k] = raw_frame_[k] & 1;
        expected[k + 1] = (raw_frame_[k] & 1) ^ (raw_frame_[k - 2] & 1);
    }
    for (int k = 0; k < SMARTNET_PAYLOAD_LENGTH; k++)
        syndrome[k] = expected[k] ^ (raw_frame_[k] & 1);
    for (int k = 0; k < ((SMARTNET_PAYLOAD_LENGTH / 2) - 1); k++) {
        if (syndrome[2 * k + 1] && syndrome[2 * k + 3])
            ecc_frame_[k] = (uint8_t)(~raw_frame_[2 * k] & 1);
        else
            ecc_frame_[k] = (uint8_t)(raw_frame_[2 * k] & 1);
    }
}

// Validate the CRC and break out the address, group flag and command.
bool SmartnetFramer::crc_check(Osw *out) {
    uint16_t crcaccum = 0x0393;
    uint16_t crcop = 0x036E;
    for (int j = 0; j < SMARTNET_DATA_LENGTH; j++) {
        if (crcop & 0x01)
            crcop = (uint16_t)((crcop >> 1) ^ 0x0225);
        else
            crcop >>= 1;
        if (ecc_frame_[j] & 1)
            crcaccum ^= crcop;
    }
    uint16_t crcgiven = 0;
    for (int j = 0; j < SMARTNET_CRC_LENGTH; j++) {
        crcgiven <<= 1;
        crcgiven += ~ecc_frame_[j + SMARTNET_DATA_LENGTH] & 0x01;
    }
    if (crcgiven != crcaccum)
        return false;
    // data bits are inverted in the buffer, so the inverted xor masks are used
    uint32_t address = 0;
    for (int j = 0; j < 16; j++)
        address = (address << 1) + (ecc_frame_[j] & 1);
    address ^= (uint32_t)SMARTNET_ID_INV_XOR;
    uint32_t command = 0;
    for (int j = 17; j < 27; j++)
        command = (command << 1) + (ecc_frame_[j] & 1);
    command ^= (uint32_t)SMARTNET_CMD_INV_XOR;
    out->addr = address & 0xffff;
    out->grp = (~ecc_frame_[16] & 1) != 0;
    out->cmd = command & 0x3ff;
    out->gap = false;
    return true;
}

// ------------------------------------------------------------------------------------------------------------
// BandPlan: port of SmartnetParser::get_bandplan_details / get_freq / is_chan

bool BandPlan::configure(const SmartnetOptions &o, std::string *err) {
    const std::string &b = o.bandplan;
    if (b == "none") kind_ = None;
    else if (b == "800_standard") kind_ = S800;
    else if (b == "800_reband") kind_ = R800;
    else if (b == "800_splinter") kind_ = Spl800;
    else if (b == "900") kind_ = B900;
    else if (b == "obt" || b == "400") {
        kind_ = Obt;
        if (!(o.obt_base > 0 && o.obt_high > o.obt_base && o.obt_spacing > 0 && o.obt_offset > 0)) {
            *err = "--bandplan obt needs --obt-base, --obt-high, --obt-spacing (MHz) and --obt-offset (channel number)";
            return false;
        }
        base_ = o.obt_base; high_ = o.obt_high; spacing_ = o.obt_spacing; offset_ = o.obt_offset;
    } else {
        *err = "unknown --bandplan '" + b + "' (none, 800_standard, 800_reband, 800_splinter, 900, obt)";
        return false;
    }
    return true;
}

double BandPlan::freq_mhz(int chan, bool tx) const {
    double freq = 0.0;
    if (kind_ == S800 || kind_ == R800 || kind_ == Spl800) {
        if (kind_ == R800) {
            if (chan <= 0x1b7) freq = 851.0125 + (0.025 * chan);
            else if (chan >= 0x1b8 && chan <= 0x22f) freq = 851.0250 + (0.025 * (chan - 0x1b8));
        } else if (kind_ == Spl800) {
            if (chan <= 0x257) freq = 851.0000 + (0.025 * chan);
            else if (chan >= 0x258 && chan <= 0x2cf) freq = 866.0125 + (0.025 * (chan - 0x258));
        } else {
            if (chan <= 0x2cf) freq = 851.0125 + (0.025 * chan);
        }
        if (chan >= 0x2d0 && chan <= 0x2f7) freq = 866.0000 + (0.025 * (chan - 0x2d0));
        else if (chan >= 0x32f && chan <= 0x33f) freq = 867.0000 + (0.025 * (chan - 0x32f));
        else if (chan >= 0x3c1 && chan <= 0x3fe) freq = 867.4250 + (0.025 * (chan - 0x3c1));
        else if (chan == 0x3be) freq = 868.9750;
        if (tx && freq != 0.0) freq -= 45.0;
    } else if (kind_ == B900) {
        freq = 935.0125 + (0.0125 * chan);
        if (tx && freq != 0.0) freq -= 39.0;
    } else if (kind_ == Obt) {
        double high_cmd = offset_ + (high_ - base_) / spacing_;
        if (!tx) {
            if (chan >= offset_ && chan < high_cmd)
                freq = base_ + (spacing_ * (chan - offset_));
        }
    }
    return std::round(freq * 100000.0) / 100000.0;
}

bool BandPlan::is_chan(int chan, bool tx) const {
    if (chan < 0) return false;
    if (kind_ == None || kind_ == S800 || kind_ == R800 || kind_ == Spl800) {
        if ((chan >= 0x2d0 && chan <= 0x2f7) || (chan >= 0x32f && chan <= 0x33f) || (chan >= 0x3c1 && chan <= 0x3fe) ||
            chan == 0x3be)
            return true;
        // CHANGE 5 (UPSTREAM.md): upstream also accepts 0x230..0x2cf on a rebanded plan, where it has no frequency
        if (kind_ == R800) return chan <= 0x22f;
        return chan <= 0x2cf;
    }
    if (kind_ == B900) return chan <= 0x1de;
    if (kind_ == Obt) {
        double high_cmd = offset_ + (high_ - base_) / spacing_;
        int tx_base = offset_ - 380; // default assumption of upstream
        if (tx) return chan >= tx_base && chan < offset_;
        return chan >= offset_ && chan < high_cmd;
    }
    return false;
}

// ------------------------------------------------------------------------------------------------------------
// SmartnetParser: port of SmartnetParser::process_osws, with the output reduced to JSON events.

static const size_t LOOKAHEAD = 6; // upstream OSW_QUEUE_SIZE (5 + 1)

SmartnetParser::SmartnetParser(Emitter &em, const BandPlan &bp, const SmartnetOptions &o, double rate, Stats &st)
    : em_(em), bp_(bp), opt_(o), st_(st), interval_((uint64_t)std::llround(o.update_interval * rate)) {}

Line SmartnetParser::msg(const char *name, uint64_t t) {
    st_.messages++;
    Line l = em_.line("msg", "smartnet", t);
    l.str("name", name);
    return l;
}

Line SmartnetParser::sysinfo(const char *kind, uint64_t t) {
    st_.messages++;
    Line l = em_.line("sysinfo", "smartnet", t);
    l.str("kind", kind);
    return l;
}

void SmartnetParser::cc_info(uint64_t t, const char *kind, long sys, int chan, double f_mhz) {
    Line l = sysinfo(kind, t);
    if (sys >= 0) l.hex("sys_id", (uint32_t)sys, 4);
    if (chan >= 0) l.num("channel", chan);
    if (f_mhz > 0) l.num("freq_hz", std::llround(f_mhz * 1e6));
    em_.write(l);
}

// src/dst < 0: absent. raw_tg is the 16-bit address of the OSW: talkgroup in the high 12 bits, status in the low 4.
void SmartnetParser::grant(uint64_t t, const char *call, const char *mode, uint32_t raw_tg, long src, long dst,
                           uint32_t chan, double f_mhz, bool update) {
    uint32_t tgid = raw_tg & 0xfff0;
    uint64_t key = ((uint64_t)tgid << 12) | chan;
    if (!thr_.pass(key, t, interval_))
        return;
    st_.messages++;
    Line l = em_.line("grant", "smartnet", t);
    l.str("call", call);
    if (mode) l.str("mode", mode);
    if (std::strcmp(call, "group") == 0) {
        em_.id(l, "tgid", 'T', tgid);
        if (src >= 0) em_.id(l, "rid", 'R', src);
    } else {
        if (src >= 0) em_.id(l, "rid", 'R', src);
        if (dst >= 0) em_.id(l, "dst_rid", 'R', dst);
    }
    l.num("channel", chan);
    if (f_mhz > 0) l.num("freq_hz", std::llround(f_mhz * 1e6));
    if (std::strcmp(call, "group") == 0) {
        int options = raw_tg & 7;
        l.boolean("encrypted", ((raw_tg & 0x8) >> 3) != 0);
        l.boolean("emergency", options == 2 || options == 4 || options == 5);
    }
    l.boolean("update", update);
    em_.write(l);
}

void SmartnetParser::push(Osw o) {
    if (!o.gap) {
        o.ch_rx = bp_.is_chan((int)o.cmd, false);
        o.ch_tx = bp_.is_chan((int)o.cmd, true);
        if (o.ch_rx) o.f_rx = bp_.freq_mhz((int)o.cmd, false);
        if (o.ch_tx) o.f_tx = bp_.freq_mhz((int)o.cmd, true);
        if (opt_.raw) {
            Line l = em_.line("osw", "smartnet", o.t);
            em_.id(l, "addr", 'R', o.addr); // an address is a radio or a talkgroup; aliased as one id
            l.boolean("group", o.grp);
            l.hex("cmd", o.cmd, 3);
            em_.write(l);
        }
    }
    q_.push_back(o);
    process(false);
}

void SmartnetParser::flush() { process(true); }

void SmartnetParser::process(bool flushing) {
    while (!q_.empty()) {
        if (q_.front().gap) {
            q_.pop_front();
            continue;
        }
        if (!flushing && q_.size() < LOOKAHEAD)
            break;
        size_t before = q_.size();
        int n = decode();
        if (n < 0)
            break;
        if (n == 0 && q_.size() == before)
            n = 1; // never stall
        for (int i = 0; i < n && !q_.empty(); i++)
            q_.pop_front();
    }
}

// Look at the k-th OSW of the queue; null when the queue is shorter or there is a gap in front of it.
#define GET(var, k)                                                                                                    \
    const Osw *var##_p = ((k) < q_.size() && !q_[(k)].gap) ? &q_[(k)] : nullptr;                                       \
    if (!var##_p) { st_.unknown++; return 1; }                                                                         \
    const Osw var = *var##_p;

static bool is_patch_group(uint32_t tg) { return (tg & 7) == 3 || (tg & 7) == 4; }
static bool is_multiselect_group(uint32_t tg) { return (tg & 7) == 5 || (tg & 7) == 7; }

// Returns the number of OSWs consumed from the front of the queue. Interleaved idle OSWs are deleted from the
// middle of the queue instead (the same effect as upstream's "put back osw0 and osw2").
int SmartnetParser::decode() {
    const Osw osw2 = q_[0];
    const uint64_t t0 = osw2.t;

    // --- OBT systems: separate tx and rx channel words (upstream line 850 of the Python original)
    if (bp_.is_obt() && osw2.ch_tx) {
        GET(osw1, 1);
        if (osw1.cmd == 0x320 && osw2.grp && osw1.grp) {
            GET(osw0, 2);
            if (osw0.cmd == 0x30b && (osw0.addr & 0xfc00) == 0x6000) {
                long sys = osw2.addr;
                int site = (int)((osw1.addr & 0xfc00) >> 10) + 1;
                int cc_chan = (int)(osw0.addr & 0x3ff);
                Line l = sysinfo(osw0.grp ? "adjacent_site" : "site", osw0.t);
                l.hex("sys_id", (uint32_t)sys, 4);
                l.num("site", site);
                l.num("cc_channel", cc_chan);
                double f = bp_.freq_mhz(cc_chan, false);
                if (f > 0) l.num("cc_freq_hz", std::llround(f * 1e6));
                em_.write(l);
                return 3;
            }
            st_.unknown++;
            return 2;
        }
        if (osw1.cmd == 0x2f8 && osw2.ch_tx)
            return 2; // OBT idle
        if (osw2.ch_tx && osw1.ch_rx && osw1.grp && osw1.addr != 0 && osw2.addr != 0) {
            grant(osw1.t, "group", osw2.grp ? "analog" : "digital", osw1.addr, osw2.addr, -1, osw1.cmd, osw1.f_rx, false);
            return 2;
        }
        if (osw2.ch_tx && osw1.ch_rx && !osw1.grp && osw1.addr != 0 && osw2.addr != 0) {
            grant(osw1.t, "private", nullptr, 0, osw2.addr, osw1.addr, osw1.cmd, osw1.f_rx, false);
            return 2;
        }
        st_.unknown++;
        return 1;
    }
    // --- one-OSW voice update
    if (osw2.ch_rx && osw2.grp) {
        grant(t0, "group", nullptr, osw2.addr, -1, -1, osw2.cmd, osw2.f_rx, true);
        return 1;
    }
    // --- one-OSW control channel broadcast
    if (osw2.ch_rx && !osw2.grp && ((osw2.addr & 0xff00) == 0x1f00)) {
        cc_info(t0, "control_channel", -1, (int)osw2.cmd, osw2.f_rx);
        return 1;
    }
    if (osw2.cmd == 0x2F8 && !osw2.grp)
        return 1; // idle
    if (osw2.cmd == 0x300 && osw2.grp) {
        Line l = msg("group_busy", t0);
        em_.id(l, "tgid", 'T', osw2.addr & 0xfff0);
        em_.write(l);
        return 1;
    }
    if (osw2.cmd == 0x303 && osw2.grp) {
        Line l = msg("emergency_busy", t0);
        em_.id(l, "tgid", 'T', osw2.addr & 0xfff0);
        em_.write(l);
        return 1;
    }
    // --- two- or three-OSW messages
    if (osw2.cmd == 0x308) {
        GET(osw1, 1);
        const uint64_t t1 = osw1.t;
        // system ID + control channel broadcast
        if (osw1.ch_rx && !osw1.grp && ((osw1.addr & 0xff00) == 0x1f00)) {
            cc_info(t1, "system", osw2.addr, (int)osw1.cmd, osw1.f_rx);
            return 2;
        }
        // analog group voice grant
        if (osw1.ch_rx && osw1.grp && osw1.addr != 0 && osw2.addr != 0) {
            grant(t1, "group", "analog", osw1.addr, osw2.addr, -1, osw1.cmd, osw1.f_rx, false);
            return 2;
        }
        // analog private call grant/update (source in osw1, destination in osw2, as logged upstream)
        if (osw1.ch_rx && !osw1.grp && osw1.addr != 0 && osw2.addr != 0) {
            grant(t1, "private", "analog", 0, osw1.addr, osw2.addr, osw1.cmd, osw1.f_rx, false);
            return 2;
        }
        // one- or two-OSW system idle
        if (osw1.cmd == 0x2f8) {
            GET(osw0, 2);
            static const uint32_t valid_cmds[] = {0x30a, 0x30b, 0x30d, 0x310, 0x311, 0x317, 0x318,
                                                  0x319, 0x31a, 0x320, 0x322, 0x32e, 0x340};
            bool found = std::find(std::begin(valid_cmds), std::end(valid_cmds), osw0.cmd) != std::end(valid_cmds);
            if (!found)
                return 2; // two-OSW system idle
            q_.erase(q_.begin() + 1); // idle delayed into a different message: drop it, look at osw2 again
            return 0;
        }
        if (osw1.cmd == 0x300 && osw1.grp) {
            Line l = msg("group_busy", t1);
            em_.id(l, "tgid", 'T', osw1.addr & 0xfff0);
            em_.write(l);
            return 2;
        }
        if (osw1.cmd == 0x302 && !osw1.grp) {
            Line l = msg("private_call_busy", t1);
            em_.id(l, "rid", 'R', osw1.addr);
            em_.write(l);
            return 2;
        }
        if (osw1.cmd == 0x303 && osw1.grp) {
            Line l = msg("emergency_busy", t1);
            em_.id(l, "tgid", 'T', osw1.addr & 0xfff0);
            em_.write(l);
            return 2;
        }
        if (osw1.cmd == 0x308) {
            // a two-OSW idle (0x308 + 0x2f8) separated and interleaved with another two-OSW message
            GET(osw0, 2);
            if (osw0.cmd == 0x2f8) {
                q_.erase(q_.begin() + 2);
                q_.erase(q_.begin());
                return 0;
            }
            st_.unknown++;
            return 1;
        }
        if (osw1.cmd == 0x30a && !osw1.grp && !osw2.grp) {
            Line l = msg("dynamic_regroup", t1);
            em_.id(l, "rid", 'R', osw2.addr);
            em_.id(l, "tgid", 'T', osw1.addr & 0xfff0);
            em_.write(l);
            return 2;
        }
        if (osw1.cmd == 0x30b) {
            GET(osw0, 2);
            const Osw *cur = &osw0;
            Osw delayed_next;
            if (osw0.cmd == 0x2f8 && !osw0.grp) {
                // one-OSW idle delayed by two OSWs and stuck inside a three-OSW message: drop it
                q_.erase(q_.begin() + 2);
                GET(next, 2);
                delayed_next = next;
                cur = &delayed_next;
            }
            const Osw o0 = *cur;
            // three-OSW system ID + control channel broadcast
            if (osw1.grp && !o0.grp && o0.ch_rx && (o0.addr & 0xff00) == 0x1f00 && (osw1.addr & 0xfc00) == 0x2800 &&
                (osw1.addr & 0x3ff) == o0.cmd) {
                cc_info(o0.t, "system", osw2.addr, (int)o0.cmd, o0.f_rx);
                return 3;
            }
            // two-OSW messages: osw0 belongs to the next message
            if ((osw1.addr & 0xFC00) == 0x2800 && osw1.grp) {
                // CHANGE 4 (UPSTREAM.md): the control channel is the channel in osw1 (low 10 bits); upstream
                // reads osw0's frequency, which belongs to the next message
                int chan = (int)(osw1.addr & 0x3ff);
                double f = bp_.is_chan(chan, false) ? bp_.freq_mhz(chan, false) : 0.0;
                cc_info(t1, "system", osw2.addr, chan, f);
                return 2;
            }
            if ((osw1.addr & 0xFC00) == 0x6000) {
                int chan = (int)(osw1.addr & 0x3ff);
                double f = bp_.freq_mhz(chan, false);
                cc_info(t1, osw1.grp ? "adjacent_cc" : "alt_cc", osw2.addr, chan, f);
                return 2;
            }
            if (osw1.grp) {
                if (osw1.addr == 0x2021 && (is_patch_group(osw2.addr) || is_multiselect_group(osw2.addr))) {
                    Line l = msg("patch_cancel", t1);
                    em_.id(l, "tgid", 'T', osw2.addr & 0xfff0);
                    em_.write(l);
                } else {
                    Line l = msg("group_extended_function", t1);
                    em_.id(l, "tgid", 'T', osw2.addr & 0xfff0);
                    l.hex("opcode", osw1.addr, 4);
                    em_.write(l);
                }
                return 2;
            }
            individual_ext(osw1, osw2, t1);
            return 2;
        }
        if (osw1.cmd == 0x30d && !osw1.grp && !osw2.grp) {
            Line l = msg("status_ack", t1); // status / emergency / dynamic regroup acknowledgement
            em_.id(l, "rid", 'R', osw2.addr);
            em_.id(l, "tgid", 'T', osw1.addr & 0xfff0);
            l.num("opcode", osw1.addr & 0xf);
            em_.write(l);
            return 2;
        }
        if (osw1.cmd == 0x310 && !osw1.grp && !osw2.grp) {
            st_.messages++;
            Line l = em_.line("affiliation", "smartnet", t1);
            em_.id(l, "rid", 'R', osw2.addr);
            em_.id(l, "tgid", 'T', osw1.addr & 0xfff0);
            em_.write(l);
            return 2;
        }
        if (osw1.cmd == 0x311 && !osw1.grp && !osw2.grp) {
            Line l = msg("message", t1);
            em_.id(l, "rid", 'R', osw2.addr);
            em_.id(l, "tgid", 'T', osw1.addr & 0xfff0);
            l.num("message", (osw1.addr & 0xf) + 1);
            em_.write(l);
            return 2;
        }
        if ((osw1.cmd == 0x315 || osw1.cmd == 0x317 || osw1.cmd == 0x318 || osw1.cmd == 0x319 || osw1.cmd == 0x31a) &&
            !osw1.grp && !osw2.grp) {
            const char *name = osw1.cmd == 0x315   ? "private_call_ring_encrypted"
                               : osw1.cmd == 0x317 ? "private_call_ring"
                               : osw1.cmd == 0x318 ? "private_call_ring_ack"
                               : osw1.cmd == 0x319 ? "call_alert"
                                                   : "call_alert_ack";
            Line l = msg(name, t1);
            em_.id(l, "rid", 'R', osw1.addr);
            em_.id(l, "dst_rid", 'R', osw2.addr);
            l.str("mode", "analog");
            em_.write(l);
            return 2;
        }
        if (osw1.cmd == 0x31b && !osw1.grp && !osw2.grp) {
            Line l = msg("omnilink_trespass_permitted", t1);
            l.hex("sys_id", osw1.addr, 4);
            em_.id(l, "rid", 'R', osw2.addr);
            em_.write(l);
            return 2;
        }
        if (osw1.cmd == 0x320) {
            // three-OSW system information
            GET(osw0, 2);
            if (osw0.cmd == 0x2f8 && !osw0.grp) {
                // delayed idle: it and the following OSW are consumed (upstream does not decode this case)
                q_.erase(q_.begin() + 2);
                GET(next, 2);
                (void)next;
                return 3;
            }
            if (osw0.cmd == 0x30b && (osw0.addr & 0xfc00) == 0x6000) {
                Line l = sysinfo("adjacent_site", osw0.t); // upstream only logs it; the fields are not decoded
                em_.write(l);
                return 3;
            }
            st_.unknown++;
            return 2;
        }
        if (osw1.cmd == 0x322 && osw2.grp && osw1.grp) {
            static const char *dow[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
            int year = (int)((osw2.addr & 0xFE00) >> 9) + 2000;
            int month = (int)(osw2.addr & 0x1E0) >> 5;
            int day = (int)(osw2.addr & 0x1F);
            int d = (int)((osw1.addr & 0xE000) >> 13);
            int hour = (int)((osw1.addr & 0x1F00) >> 8);
            int minute = (int)(osw1.addr & 0xFF);
            Line l = sysinfo("datetime", t1);
            char b[32];
            snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
            l.str("datetime", b);
            l.str("weekday", d <= 6 ? dow[d] : "unknown");
            em_.write(l);
            return 2;
        }
        if (osw1.cmd == 0x32e && osw2.grp && osw1.grp) {
            Line l = msg("emergency_ptt", t1);
            em_.id(l, "rid", 'R', osw2.addr);
            em_.id(l, "tgid", 'T', osw1.addr & 0xfff0);
            em_.write(l);
            return 2;
        }
        if (osw1.cmd == 0x340 && osw2.grp && osw1.grp && (is_patch_group(osw2.addr) || is_multiselect_group(osw2.addr))) {
            Line l = msg(is_patch_group(osw2.addr) ? "patch" : "multiselect", t1);
            em_.id(l, "tgid", 'T', (osw1.addr & 0xfff) << 4);
            em_.id(l, "sub_tgid", 'T', osw2.addr & 0xfff0);
            l.num("mode", osw2.addr & 0xf);
            em_.write(l);
            return 2;
        }
        st_.unknown++;
        return 1;
    }
    if (osw2.cmd == 0x321) {
        GET(osw1, 1);
        const uint64_t t1 = osw1.t;
        if (osw1.ch_rx && osw2.grp && osw1.grp && osw1.addr != 0) {
            grant(t1, "group", "digital", osw1.addr, osw2.addr, -1, osw1.cmd, osw1.f_rx, false);
            return 2;
        }
        if (osw1.ch_rx && !osw1.grp && osw1.addr != 0 && osw2.addr != 0) {
            grant(t1, "private", "digital", 0, osw1.addr, osw2.addr, osw1.cmd, osw1.f_rx, false);
            return 2;
        }
        if (osw1.cmd == 0x2f8) {
            GET(osw0, 2);
            if (osw0.cmd != 0x317 && osw0.cmd != 0x318)
                return 2; // digital idle
            q_.erase(q_.begin() + 1); // delayed idle inside another message
            return 0;
        }
        if ((osw1.cmd == 0x315 || osw1.cmd == 0x317) && !osw1.grp && !osw2.grp) {
            Line l = msg(osw1.cmd == 0x315 ? "private_call_ring_encrypted" : "private_call_ring", t1);
            em_.id(l, "rid", 'R', osw1.addr);
            em_.id(l, "dst_rid", 'R', osw2.addr);
            l.str("mode", "digital");
            em_.write(l);
            return 2;
        }
        st_.unknown++;
        return 1;
    }
    if (osw2.cmd == 0x324 && osw2.grp) {
        Line l = msg("interconnect_reject", t0);
        em_.id(l, "rid", 'R', osw2.addr);
        em_.write(l);
        return 1;
    }
    if (osw2.cmd == 0x32a && osw2.grp) {
        Line l = msg("send_affiliation_request", t0);
        em_.id(l, "rid", 'R', osw2.addr);
        em_.write(l);
        return 1;
    }
    if (osw2.cmd == 0x32b && !osw2.grp) {
        Line l = sysinfo("scan_marker", t0);
        l.hex("sys_id", osw2.addr, 4);
        em_.write(l);
        return 1;
    }
    if (osw2.cmd == 0x32c && !osw2.grp) {
        Line l = msg("roaming", t0);
        em_.id(l, "rid", 'R', osw2.addr);
        em_.write(l);
        return 1;
    }
    if (osw2.cmd >= 0x360 && osw2.cmd <= 0x39f) {
        Line l = msg("amss", t0); // automatic multiple site select
        l.num("site", (int)osw2.cmd - 0x360 + 1);
        em_.write(l);
        return 1;
    }
    if (osw2.cmd == 0x3a0 && osw2.grp) {
        Line l = msg("bsi_diagnostic", t0);
        l.num("opcode", (osw2.addr & 0xf000) >> 12);
        em_.write(l);
        return 1;
    }
    if (osw2.cmd == 0x3bf || osw2.cmd == 0x3c0) {
        Line l = msg("system_status", t0);
        em_.write(l);
        return 1;
    }
    st_.unknown++;
    return 1;
}

// Extended functions on individuals (osw1.addr is the opcode, osw2.addr the radio): table from upstream.
void SmartnetParser::individual_ext(const Osw &o1, const Osw &o2, uint64_t t) {
    struct Ent { uint32_t lo, hi; const char *name; const char *reason; };
    static const Ent tab[] = {
        {0x261b, 0x261b, "radio_check", nullptr},
        {0x261c, 0x261c, "deaffiliation", nullptr},
        {0x26e8, 0x26e8, "emergency_alarm_ack", nullptr},
        {0x2c04, 0x2c04, "denied", "invalid_talkgroup"},
        {0x2c11, 0x2c11, "denied", "announcement_listen_only"},
        {0x2c12, 0x2c12, "denied", "clear_tx_only"},
        {0x2c13, 0x2c13, "denied", "listen_only"},
        {0x2c14, 0x2c14, "denied", "no_private_call"},
        {0x2c15, 0x2c15, "denied", "private_call_invalid_id"},
        {0x2c16, 0x2c16, "denied", "no_interconnect"},
        {0x2c20, 0x2c20, "denied", "unsupported_mode"},
        {0x2c41, 0x2c41, "denied", "private_call_target_offline"},
        {0x2c47, 0x2c47, "denied", "group_busy_call_in_progress"},
        {0x2c48, 0x2c48, "denied", "private_call_ring_target_offline"},
        {0x2c4a, 0x2c4a, "denied", "forbidden_on_site"},
        {0x2c4e, 0x2c4e, "denied", "call_alert_invalid_id"},
        {0x2c4f, 0x2c4f, "denied", "call_alert_target_offline"},
        {0x2c56, 0x2c56, "denied", "wrong_modulation"},
        {0x2c60, 0x2c60, "denied", "omnilink_trespass"},
        {0x2c65, 0x2c65, "denied", "radio_id"},
        {0x2c6a, 0x2c6a, "denied", "talkgroup_id"},
        {0x2c66, 0x2c66, "denied", "group_busy_call_starting"},
        {0x2c90, 0x2c90, "denied", "private_call_target_busy"},
        {0x8301, 0x8301, "failsoft_assign", nullptr},
        {0x8302, 0x8302, "selector_unlocked", nullptr},
        {0x8303, 0x8303, "selector_locked", nullptr},
        {0x8305, 0x8305, "failsoft_canceled", nullptr},
        {0x8306, 0x8306, "radio_inhibited", nullptr},
        {0x8307, 0x8307, "radio_uninhibited", nullptr},
        {0x8312, 0x8312, "selector_unlock", nullptr},
        {0x8313, 0x8313, "selector_lock", nullptr},
        {0x8315, 0x8315, "failsoft_cancel", nullptr},
        {0x8316, 0x8316, "radio_inhibit", nullptr},
        {0x8317, 0x8317, "radio_uninhibit", nullptr},
    };
    uint32_t a = o1.addr;
    if (a >= 0x26e0 && a <= 0x26e7) {
        Line l = msg("status_ack", t);
        em_.id(l, "rid", 'R', o2.addr);
        l.num("status", (a & 7) + 1);
        em_.write(l);
        return;
    }
    if (a >= 0x26f0 && a <= 0x26ff) {
        Line l = msg("message_ack", t);
        em_.id(l, "rid", 'R', o2.addr);
        l.num("message", (a & 0xf) + 1);
        em_.write(l);
        return;
    }
    for (const Ent &e : tab) {
        if (a >= e.lo && a <= e.hi) {
            Line l = msg(e.name, t);
            em_.id(l, "rid", 'R', o2.addr);
            if (e.reason) l.str("reason", e.reason);
            em_.write(l);
            return;
        }
    }
    if ((a & 0xfc00) == 0x2c00) {
        Line l = msg("denied", t);
        em_.id(l, "rid", 'R', o2.addr);
        l.str("reason", "other");
        l.hex("code", a & 0x3ff, 3);
        em_.write(l);
        return;
    }
    Line l = msg("individual_extended_function", t);
    em_.id(l, "rid", 'R', o2.addr);
    l.hex("opcode", a, 4);
    em_.write(l);
}

// ------------------------------------------------------------------------------------------------------------
// SmartnetDecoder: the glue (not from upstream)

SmartnetDecoder::SmartnetDecoder(Emitter &em, double rate, double sps, const SmartnetOptions &o, Stats &st)
    : em_(em), opt_(o), st_(st), parser_(em, bp_, o, rate, st), sps_(sps), rate_(rate) {
    ok_ = bp_.configure(o, &err_);
    min_gap_ = (uint64_t)(0.5 * SMARTNET_FRAME_LENGTH * sps);
    frame_samples_ = (uint64_t)std::llround(SMARTNET_FRAME_LENGTH * sps);
    lost_after_ = (uint64_t)std::llround(rate); // one second without a valid OSW
    for (int p = 0; p < phases_; p++)
        for (int pol = 0; pol < 2; pol++)
            fr_.emplace_back(new SmartnetFramer(lost_after_));
}

void SmartnetDecoder::bit(int phase, int b, uint64_t end_sample) {
    for (int pol = 0; pol < 2; pol++) {
        if (opt_.polarity == 1 && pol == 1) continue;
        if (opt_.polarity == 2 && pol == 0) continue;
        Osw o;
        if (fr_[(size_t)(phase * 2 + pol)]->sym(pol ? b ^ 1 : b, end_sample, &o))
            accept(o);
    }
}

void SmartnetDecoder::accept(const Osw &o) {
    // The same OSW is usually decoded by several timing phases: keep the first, drop the others.
    if (have_last_ && o.t < last_end_ + min_gap_)
        return;
    if (have_last_ && locked_) {
        double frames = (double)(o.t - last_end_) / (double)frame_samples_;
        if (std::llround(frames) >= 2) {
            Osw g;
            g.gap = true;
            g.t = o.t;
            parser_.push(g);
            st_.gaps++;
        }
    }
    if (!locked_) {
        locked_ = true;
        em_.write(em_.line("status", "smartnet", o.t).str("text", "smartnet: control channel locked"));
    }
    have_last_ = true;
    last_end_ = o.t;
    st_.frames++;
    parser_.push(o);
}

void SmartnetDecoder::tick(uint64_t sample) {
    if (locked_ && sample > last_end_ + lost_after_) {
        locked_ = false;
        have_last_ = false;
        st_.lost++;
        parser_.flush();
        em_.write(em_.line("status", "smartnet", sample).str("text", "smartnet: control channel lost"));
    }
}

void SmartnetDecoder::finish(uint64_t) { parser_.flush(); }

} // namespace trunkctl
