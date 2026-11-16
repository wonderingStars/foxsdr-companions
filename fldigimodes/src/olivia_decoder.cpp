// ----------------------------------------------------------------------------
// olivia_decoder.cpp -- FoxSDR fldigimodes helper: Olivia and Contestia receive.
//
// The decoding is Pawel Jalocha's MFSK_Receiver (upstream/include/jalocha/pj_mfsk.h, unchanged). This file is the
// receive half of fldigi's olivia::restart(), olivia::rx_process() and olivia::unescape() (src/olivia/olivia.cxx)
// and contestia::restart(), contestia::rx_process() and contestia::unescape() (src/contestia/contestia.cxx), with
// the user interface calls removed. Same settings, same formulas.
//
//   Olivia:   Copyright (C) 2006-2010 Dave Freese, W1HKJ (fldigi); adapted from gmfsk, Copyright (C) 2005
//             Tomi Manninen (oh2bns@sral.fi); the MFSK engine is Pawel Jalocha's.
//   Contestia: the same authors.
// Distributed under the GNU General Public License, version 3 or (at your option) any later version.
// ----------------------------------------------------------------------------
#include "decoder.h"

#include <algorithm>

#include "jalocha/pj_mfsk.h"

#include "fm_shim.h"
#include "misc.h"

namespace fm {

namespace {

class OliviaDecoder : public Decoder {
public:
    OliviaDecoder(const Options &opt, bool contestia) : contestia_(contestia), opt_(opt)
    {
        // fldigi's tone index t gives 2 * (1 << t) tones; the helper takes the tone count itself
        tones_ = opt.mode.tones;
        bw_ = opt.mode.bandwidth;
        Rx_.bContestia = contestia_;
        configure();
    }

    void process(const double *buf, int n) override
    {
        if (n <= 0)
            return;
        // fldigi: olivia::rx_process / contestia::rx_process
        Rx_.Process(buf, (size_t)n);
        drain();
    }

    // fldigi calls olivia::rx_flush (Rx->Flush()) only when it switches the modem to transmit. At the end of the input the
    // helper does not: Flush() pushes zeros through the synchroniser and prints a few characters of noise.
    void flush() override {}

    Quality quality() const override
    {
        Quality q;
        q.offset_hz = const_cast<MFSK_Receiver<double> &>(Rx_).FrequencyOffset();
        q.sync = const_cast<MFSK_Receiver<double> &>(Rx_).SignalToNoiseRatio();
        // fldigi: metric = clamp(5.0 * (rx_snr - 3.0), 0, 100)
        q.metric = clamp(5.0 * (q.sync - 3.0), 0, 100);
        q.freq_hz = opt_.freq;
        return q;
    }

    std::string name() const override { return mode_name(opt_.mode); }
    std::string family() const override { return contestia_ ? "contestia" : "olivia"; }

private:
    void configure()
    {
        const int samplerate = 8000;
        Rx_.Tones = (size_t)tones_;
        Rx_.Bandwidth = (size_t)bw_;
        Rx_.SyncMargin = (size_t)opt_.mode.smargin;
        Rx_.SyncIntegLen = (size_t)opt_.mode.sinteg;
        // fldigi: olivia sets the lower limit 0, contestia 3.0; Preset() raises both to 3.0
        double thr = opt_.squelch_on ? clamp(opt_.squelch / 5.0 + 3.0, contestia_ ? 3.0 : 0.0, 90.0) : (contestia_ ? 3.0 : 0.0);
        Rx_.SyncThreshold = thr;
        Rx_.SampleRate = samplerate;
        Rx_.InputSampleRate = samplerate;
        Rx_.Reverse = opt_.reverse ? 1 : 0;
        // The carrier multiplier is the lowest tone in units of 500 Hz: fldigi's formulas.
        // (fldigi keeps these offsets as int: Olivia truncates, Contestia divides integers)
        int fc_offset;
        if (contestia_)
            fc_offset = bw_ / 2;
        else
            fc_offset = (int)(bw_ * (1.0 - 0.5 / (double)tones_) / 2.0);
        if (opt_.reverse)
            Rx_.FirstCarrierMultiplier = (float)((opt_.freq + fc_offset) / 500.0);
        else
            Rx_.FirstCarrierMultiplier = (float)((opt_.freq - fc_offset) / 500.0);
        if (Rx_.Preset() < 0) {
            failed_ = true;
        }
    }

    // fldigi: olivia::unescape. 127 announces a character above 127 (the next one plus 128).
    int unescape(int c)
    {
        if (escape_) {
            escape_ = 0;
            return c + 128;
        }
        if (c == 127) {
            escape_ = 1;
            return -1;
        }
        return c;
    }

    void drain()
    {
        uint8_t ch = 0;
        while (Rx_.GetChar(ch) > 0) {
            int c = unescape(ch);
            if (c != -1 && c > 7)
                put_rx_char((unsigned)c);
        }
    }

    bool contestia_;
    Options opt_;
    int tones_ = 8, bw_ = 500;
    int escape_ = 0;
    bool failed_ = false;
    MFSK_Receiver<double> Rx_;

public:
    bool ok() const { return !failed_; }
};

}  // namespace

std::unique_ptr<Decoder> make_olivia_decoder(const Options &opt, bool contestia, std::string &err)
{
    std::unique_ptr<OliviaDecoder> d(new OliviaDecoder(opt, contestia));
    if (!d->ok()) {
        err = "the Olivia/Contestia receiver could not be set up (out of memory?)";
        return nullptr;
    }
    return std::unique_ptr<Decoder>(d.release());
}

}  // namespace fm
