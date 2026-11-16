// ----------------------------------------------------------------------------
// mfsk_decoder.cpp -- FoxSDR fldigimodes helper: MFSK receive (fldigi's class mfsk, upstream/mfsk/mfsk.cxx, receive
// side only). This file only sets the class up and reads its state. Written for this helper; GPL-3.0-only.
// ----------------------------------------------------------------------------
#include "decoder.h"

#include "fm_shim.h"
#include "mfsk.h"
#include "modem.h"

namespace fm {

namespace {

// the class keeps its signal estimates protected
class MfskProbe : public mfsk {
public:
    explicit MfskProbe(trx_mode m) : mfsk(m) {}
    double s2n_ratio() const { return s2n; }
    double current_metric() const { return metric; }
    int rate() const { return samplerate; }
};

class MfskDecoder : public Decoder {
public:
    MfskDecoder(const Options &opt, trx_mode id) : opt_(opt), m_(id)
    {
        progStatus.sqlonoff = opt.squelch_on;
        progStatus.sldrSquelchValue = opt.squelch;
        m_.init();
        m_.set_reverse(opt.reverse);
        m_.set_freq(opt.freq);
        // The carrier frequency the receiver actually took (fldigi limits it to the band the signal fits in).
        start_freq_ = modem::frequency;
    }

    void process(const double *buf, int n) override
    {
        if (n > 0)
            m_.rx_process(buf, n);
    }

    Quality quality() const override
    {
        Quality q;
        q.freq_hz = modem::frequency;
        q.offset_hz = modem::frequency - start_freq_;
        double r = m_.s2n_ratio();
        if (r > 0)
            q.snr_db = 20.0 * std::log10(r);  // fldigi shows "s/n %3.0f dB" = 20 log10(s2n)
        q.metric = m_.current_metric();
        return q;
    }

    std::string name() const override { return mode_name(opt_.mode); }
    std::string family() const override { return "mfsk"; }
    int rate() const override { return m_.rate(); }

private:
    Options opt_;
    mutable MfskProbe m_;
    double start_freq_ = 0;
};

}  // namespace

std::unique_ptr<Decoder> make_mfsk_decoder(const Options &opt, std::string &err)
{
    long long id = fldigi_mode_id(opt.mode);
    if (id == MODE_NULL) {
        err = "no such MFSK mode";
        return nullptr;
    }
    return std::unique_ptr<Decoder>(new MfskDecoder(opt, (trx_mode)id));
}

}  // namespace fm
