// trunkctl - EDACS control channel decoder. See edacs.h for the origin of each part.
// This file is part of trunkctl and is licensed under the GNU General Public License, version 3.
#include "edacs.h"

#include <cmath>
#include <cstdio>

#include "bch4028.h"

namespace trunkctl {

// edacs-fm.c: SYNC_FRAME 0x555557125555 (48 bits). The 288-bit frame is the sync plus six 40-bit messages.
static const uint64_t EDACS_SYNC = 0x555557125555ull;
static const uint64_t MASK48 = 0xFFFFFFFFFFFFull;

static int popcount64(uint64_t x) {
    int n = 0;
    while (x) {
        x &= x - 1;
        n++;
    }
    return n;
}

bool EdacsFramer::sym(int bit) {
    if (collecting_) {
        bits_[count_++] = (uint8_t)bit;
        if (count_ == 240) {
            collecting_ = false;
            sr_ = 0;
            return true;
        }
        return false;
    }
    sr_ = ((sr_ << 1) | (uint64_t)(bit & 1)) & MASK48;
    if (popcount64(sr_ ^ EDACS_SYNC) <= max_err_) {
        collecting_ = true;
        count_ = 0;
    }
    return false;
}

// ------------------------------------------------------------------------------------------------------------

EdacsDecoder::EdacsDecoder(Emitter &em, double rate, double sps, const EdacsOptions &o, Stats &st)
    : em_(em), opt_(o), st_(st), sps_(sps), rate_(rate), variant_(o.variant), xmask_(o.esk ? 0xA0u : 0u) {
    min_gap_ = (uint64_t)(0.5 * 288 * sps);
    lost_after_ = (uint64_t)std::llround(3.0 * rate);
    interval_ = (uint64_t)std::llround(o.update_interval * rate);
    for (int i = 0; i < phases_ * 2; i++)
        fr_.emplace_back(new EdacsFramer(2));
}

void EdacsDecoder::bit(int phase, int b, uint64_t end_sample) {
    for (int pol = 0; pol < 2; pol++) {
        if (opt_.polarity == 1 && pol == 1) continue;
        if (opt_.polarity == 2 && pol == 0) continue;
        if (fr_[(size_t)(phase * 2 + pol)]->sym(pol ? b ^ 1 : b))
            frame(phase, pol, end_sample);
    }
}

// Best of the three copies of one message: fewest corrected errors, first copy on a tie.
static void best_copy(const uint64_t w[3], int maxerr, bool *ok, uint64_t *out, int *err) {
    *ok = false;
    *out = 0;
    *err = 0;
    int best = 99;
    for (int i = 0; i < 3; i++) {
        uint64_t c;
        int e = bch4028_decode(w[i], maxerr, &c);
        if (e >= 0 && e < best) {
            best = e;
            *out = c;
            *ok = true;
            *err = e;
            if (e == 0)
                break;
        }
    }
}

void EdacsDecoder::frame(int phase, int pol, uint64_t end) {
    const uint8_t *bits = fr_[(size_t)(phase * 2 + pol)]->bits();
    uint64_t w[6];
    for (int m = 0; m < 6; m++) {
        uint64_t v = 0;
        for (int j = 0; j < 40; j++)
            v = (v << 1) | bits[m * 40 + j];
        w[m] = v;
    }
    Cand c;
    c.valid = true;
    c.first_end = end;
    best_copy(w, opt_.bch_correct, &c.ok_a, &c.a, &c.err_a);
    best_copy(w + 3, opt_.bch_correct, &c.ok_b, &c.b, &c.err_b);

    if (pend_.valid) {
        if (end <= pend_.first_end + min_gap_) { // the same frame seen by another timing phase / polarity
            if (c.score() < pend_.score()) {
                c.first_end = pend_.first_end;
                pend_ = c;
            }
            return;
        }
        release();
    }
    pend_ = c;
}

void EdacsDecoder::tick(uint64_t sample) {
    if (pend_.valid && sample > pend_.first_end + (uint64_t)(2.0 * sps_) + 2)
        release();
    if (locked_ && sample > last_ok_ + lost_after_) {
        locked_ = false;
        st_.lost++;
        em_.write(em_.line("status", "edacs", sample).str("text", "edacs: control channel lost"));
    }
}

void EdacsDecoder::finish(uint64_t) {
    if (pend_.valid)
        release();
}

void EdacsDecoder::release() {
    Cand c = pend_;
    pend_ = Cand();
    handle(c);
}

Line EdacsDecoder::sysinfo(const char *kind, uint64_t t) {
    st_.messages++;
    Line l = em_.line("sysinfo", "edacs", t);
    l.str("kind", kind);
    return l;
}

double EdacsDecoder::lcn_mhz(int lcn) const {
    auto it = opt_.lcn_mhz.find(lcn);
    return it == opt_.lcn_mhz.end() ? 0.0 : it->second;
}

void EdacsDecoder::handle(const Cand &c) {
    const uint64_t t = c.first_end;
    if (!c.ok_a && !c.ok_b) {
        st_.bad++;
        return;
    }
    st_.frames++;
    if (c.ok_a) {
        last_ok_ = t;
        if (!locked_) {
            locked_ = true;
            em_.write(em_.line("status", "edacs", t).str("text", "edacs: control channel locked"));
        }
    }
    if (c.err_a || c.err_b)
        st_.corrected++;
    if (opt_.raw) {
        Line l = em_.line("frame", "edacs", t);
        if (!em_.hide_ids()) {
            if (c.ok_a) l.str("a", [&] { char b[16]; snprintf(b, sizeof b, "%010llx", (unsigned long long)c.a); return std::string(b); }());
            if (c.ok_b) l.str("b", [&] { char b[16]; snprintf(b, sizeof b, "%010llx", (unsigned long long)c.b); return std::string(b); }());
        }
        l.boolean("ok_a", c.ok_a).boolean("ok_b", c.ok_b).num("errors", c.err_a + c.err_b);
        em_.write(l);
    }
    if (!c.ok_a) {
        st_.unknown++;
        return;
    }
    // variant detection (edacs-fm "Autodetect", reduced): peer-site relay (0xF88, plain or ESK) means extended
    // addressing; a command byte 0xEE / 0xEF / 0xF3 means standard / networked
    if (variant_ == 0) {
        uint32_t top12 = (uint32_t)(c.a >> 28);
        uint32_t cmd8 = (uint32_t)((c.a >> 32) & 0xFF) ^ xmask_;
        int found = 0;
        if (top12 == 0xF88 || top12 == (0xF88 ^ 0xA00)) found = 2;
        else if (cmd8 == 0xEE || cmd8 == 0xEF || cmd8 == 0xF3) found = 1;
        if (!found) {
            st_.unknown++;
            return;
        }
        variant_ = found;
        em_.write(em_.line("status", "edacs", t).str("text", found == 2 ? "edacs: extended addressing detected"
                                                                        : "edacs: standard/networked detected"));
    }
    if (variant_ == 2) handle_ea(c);
    else handle_std(c);
}

void EdacsDecoder::emit_call(uint64_t t, const char *variant, const char *mode, int lcn, int64_t tgid, int64_t rid,
                             int status, int64_t afs) {
    if (lcn == 0) {
        st_.unknown++;
        return;
    }
    if (lcn >= 26) {
        // LCNs above 25 are not channels: they are call states reported on the control channel
        static const char *names[] = {"downlink", "reserved", "convert_to_callee", "caller_queued", "system_busy", "radio_id_denied"};
        st_.messages++;
        Line l = em_.line("msg", "edacs", t);
        l.str("name", "lcn_status").str("state", names[lcn - 26]).num("lcn", lcn).str("variant", variant);
        em_.id(l, "tgid", 'T', tgid);
        em_.id(l, "rid", 'R', rid);
        em_.write(l);
        return;
    }
    if (!thr_.pass(((uint64_t)tgid << 8) | (uint64_t)lcn, t, interval_))
        return;
    st_.messages++;
    Line l = em_.line("grant", "edacs", t);
    l.str("variant", variant).str("call", "group").str("mode", mode);
    em_.id(l, "tgid", 'T', tgid);
    em_.id(l, "rid", 'R', rid);
    if (afs >= 0 && !em_.hide_ids()) {
        int s_len = 11 - (opt_.afs_a + opt_.afs_f);
        l.num("agency", (afs >> (11 - opt_.afs_a)) & ((1 << opt_.afs_a) - 1));
        l.num("fleet", (afs >> s_len) & ((1 << opt_.afs_f) - 1));
        l.num("subfleet", afs & ((1 << s_len) - 1));
    }
    l.num("lcn", lcn).num("channel", lcn);
    double f = lcn_mhz(lcn);
    if (f > 0) l.num("freq_hz", std::llround(f * 1e6));
    if (status >= 0) l.num("status", status);
    l.boolean("update", false);
    em_.write(l);
}

// Extended addressing: edacs-fm x_choice == 1
void EdacsDecoder::handle_ea(const Cand &c) {
    const uint64_t t = c.first_end;
    const uint64_t a = c.a, b = c.b;
    uint32_t cmd = (uint32_t)((a >> 32) & 0xFF) ^ xmask_;
    uint32_t mt1 = (cmd & 0xF8) >> 3;
    uint32_t mt2 = (uint32_t)((a >> 31) & 0xF);
    int lcn = (int)((a >> 29) & 0x1F);
    if (mt1 == 0x1F) {
        if (mt2 == 0xA) { // site ID
            int site = (int)(((a >> 12) & 0x1F) | (((a >> 24) & 0x1F) << 5));
            Line l = sysinfo("site", t);
            l.num("site", site).str("variant", "ea");
            em_.write(l);
        } else if (mt2 == 0x1 && ((a >> 12) & 0xFF) > 0) { // peer site listing
            int peer = (int)((a >> 12) & 0xFF);
            int plcn = (int)((a >> 24) & 0x1F);
            Line l = sysinfo("peer_site", t);
            l.num("site", peer).num("control_lcn", plcn).str("variant", "ea");
            em_.write(l);
        } else if (mt2 == 0x8 && c.ok_b && ((b >> 12) & 0x1F) != 0) { // control channel LCN
            int cc = (int)((b >> 12) & 0x1F);
            Line l = sysinfo("control_channel", t);
            l.num("lcn", cc).str("variant", "ea");
            double f = lcn_mhz(cc);
            if (f > 0) l.num("freq_hz", std::llround(f * 1e6));
            em_.write(l);
        } else if (mt2 == 0xC && c.ok_b) { // patch listing
            st_.messages++;
            Line l = em_.line("msg", "edacs", t);
            l.str("name", "patch");
            em_.id(l, "from_tgid", 'T', (a >> 12) & 0xFFFF);
            em_.id(l, "to_tgid", 'T', (b >> 12) & 0xFFFF);
            em_.write(l);
        } else if (mt2 == 0xB && c.ok_b) { // kick listing
            st_.messages++;
            Line l = em_.line("msg", "edacs", t);
            l.str("name", "kick");
            em_.id(l, "rid", 'R', (b >> 12) & 0xFFFFF);
            em_.write(l);
        } else {
            st_.unknown++;
        }
        return;
    }
    if (mt1 >= 1 && mt1 <= 3 && c.ok_b) {
        static const char *mode[] = {"", "tdma", "data", "digital"};
        emit_call(t, "ea", mode[mt1], lcn, (int64_t)((a >> 12) & 0xFFFF), (int64_t)((b >> 12) & 0xFFFFF), -1, -1);
        return;
    }
    st_.unknown++;
}

// Standard / networked: edacs-fm x_choice == 2
void EdacsDecoder::handle_std(const Cand &c) {
    const uint64_t t = c.first_end;
    const uint64_t a = c.a, b = c.b;
    uint32_t cmd = (uint32_t)((a >> 32) & 0xFF) ^ xmask_;
    if (cmd == 0xFD) { // site ID
        Line l = sysinfo("site", t);
        l.num("site", (int64_t)((a >> 12) & 0xFF)).str("variant", "standard");
        em_.write(l);
        int cc = (int)((a >> 24) & 0x1F);
        Line m = sysinfo("control_channel", t);
        m.num("lcn", cc).str("variant", "standard");
        double f = lcn_mhz(cc);
        if (f > 0) m.num("freq_hz", std::llround(f * 1e6));
        em_.write(m);
        return;
    }
    if ((cmd == 0xEE || cmd == 0xEF) && c.ok_b) {
        int lcn = (int)((a >> 27) & 0x1F);
        int64_t afs = (int64_t)((a >> 12) & 0x7FF);
        emit_call(t, "standard", cmd == 0xEE ? "analog" : "digital", lcn, afs, (int64_t)((b >> 12) & 0xFFFFF), -1, afs);
        return;
    }
    st_.unknown++;
}

} // namespace trunkctl
