// ----------------------------------------------------------------------------
// mt63_decoder.cpp -- FoxSDR fldigimodes helper: MT63 receive.
//
// The decoding is Pawel Jalocha's MT63 engine as carried in fldigi (upstream/mt63/dsp.cxx, mt63base.cxx,
// include/dsp.h, mt63base.h). This file is the receive half of fldigi's mt63::rx_init(), mt63::rx_process() and
// mt63::mt63() (src/mt63/mt63.cxx) with the user interface calls removed. Same calls, same order.
//
//   mt63.cxx: Copyright (C) 2006-2009 Dave Freese, W1HKJ (fldigi); the MT63 engine: Copyright (C) 1999-2004 Pawel
//   Jalocha, SP9VRC; Copyright (c) 2007-2019 Dave Freese, W1HKJ.
// Distributed under the GNU General Public License, version 3 or (at your option) any later version.
// ----------------------------------------------------------------------------
#include "decoder.h"

#include <algorithm>

#include "dsp.h"
#include "fm_shim.h"
#include "misc.h"
#include "mt63base.h"

namespace fm {

namespace {

class Mt63Decoder : public Decoder {
public:
    explicit Mt63Decoder(const Options &opt) : opt_(opt)
    {
        interleave_ = opt.mode.variant;  // 32 (short) or 64 (long)
        bandwidth_ = opt.mode.bandwidth;
        long_integral_ = opt.mode.mt63_long_integral;
        progStatus.sqlonoff = opt.squelch_on;
        progStatus.sldrSquelchValue = opt.squelch;
        // fldigi: mt63::rx_init
        err_ = Rx_.Preset(opt.freq, bandwidth_, interleave_ == 64 ? 1 : 0, long_integral_ ? 32 : 16);
        InpLevel_.Preset(64.0, 0.75);
    }

    bool ok() const { return err_ == 0; }

    void process(const double *buf, int len) override
    {
        if (len <= 0)
            return;
        // fldigi: mt63::rx_process
        if (InpBuff_.EnsureSpace(len) == -1)
            return;
        for (int i = 0; i < len; i++)
            InpBuff_.Data[i] = buf[i];
        InpBuff_.Len = len;
        InpLevel_.Process(&InpBuff_);
        Rx_.Process(&InpBuff_);
        snr_ = Rx_.FEC_SNR();
        if (progStatus.sqlonoff && snr_ < progStatus.sldrSquelchValue)
            return;  // fldigi drops the characters while the FEC S/N is below its squelch
        for (int i = 0; i < Rx_.Output.Len; i++) {
            unsigned int c = Rx_.Output.Data[i];
            // mt63_8bit is on in fldigi by default
            if ((c < 8) && (escape_ == 0))
                continue;
            if (c == 127) {
                escape_ = 1;
                continue;
            }
            if (escape_) {
                c += 128;
                escape_ = 0;
            }
            put_rx_char(c);
        }
        offset_ = Rx_.TotalFreqOffset();
    }

    Quality quality() const override
    {
        Quality q;
        q.freq_hz = opt_.freq;
        q.offset_hz = offset_;
        // fldigi: display_metric(snr) (capped at 99.9) and "s/n %2d dB" = 10 log10(snr)
        double s = snr_ > 99.9 ? 99.9 : snr_;
        q.metric = s;
        q.snr_db = 10.0 * std::log10(s == 0 ? 0.001 : s);
        q.sync = snr_;
        return q;
    }

    std::string name() const override { return mode_name(opt_.mode); }
    std::string family() const override { return "mt63"; }

private:
    Options opt_;
    int interleave_ = 32, bandwidth_ = 1000;
    bool long_integral_ = false;
    int escape_ = 0;
    int err_ = 0;
    double snr_ = 0, offset_ = 0;
    MT63rx Rx_;
    dspLevelMonitor InpLevel_;
    double_buff InpBuff_;
};

}  // namespace

std::unique_ptr<Decoder> make_mt63_decoder(const Options &opt, std::string &err)
{
    std::unique_ptr<Mt63Decoder> d(new Mt63Decoder(opt));
    if (!d->ok()) {
        err = "the MT63 receiver could not be set up";
        return nullptr;
    }
    return std::unique_ptr<Decoder>(d.release());
}

}  // namespace fm
