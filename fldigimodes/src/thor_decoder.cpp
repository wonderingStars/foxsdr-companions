// ----------------------------------------------------------------------------
// thor_decoder.cpp -- FoxSDR fldigimodes helper: THOR receive (fldigi's class thor, upstream/thor/thor.cxx, receive
// side only). This file only sets the class up and reads its state. Written for this helper; GPL-3.0-only.
// ----------------------------------------------------------------------------
#include "decoder.h"

#include "fm_shim.h"
#include "modem.h"
#include "thor.h"

namespace fm {

namespace {

class ThorProbe : public thor {
public:
    explicit ThorProbe(trx_mode m) : thor(m) {}
    double s2n_db() const { return s2n; }
    double current_metric() const { return metric; }
    int rate() const { return samplerate; }
};

class ThorDecoder : public Decoder {
public:
    ThorDecoder(const Options &opt, trx_mode id) : opt_(opt), m_(id)
    {
        progStatus.sqlonoff = opt.squelch_on;
        progStatus.sldrSquelchValue = opt.squelch;
        m_.set_reverse(opt.reverse);
        m_.set_freq(opt.freq);
        m_.rx_init();
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
        q.snr_db = m_.s2n_db();  // fldigi shows "s/n %3.0f dB" = s2n (already in dB for THOR)
        q.metric = m_.current_metric();
        return q;
    }

    std::string name() const override { return mode_name(opt_.mode); }
    std::string family() const override { return "thor"; }
    int rate() const override { return m_.rate(); }

private:
    Options opt_;
    mutable ThorProbe m_;
    double start_freq_ = 0;
};

}  // namespace

std::unique_ptr<Decoder> make_thor_decoder(const Options &opt, std::string &err)
{
    long long id = fldigi_mode_id(opt.mode);
    if (id == MODE_NULL) {
        err = "no such THOR mode";
        return nullptr;
    }
    return std::unique_ptr<Decoder>(new ThorDecoder(opt, (trx_mode)id));
}

}  // namespace fm
