// ----------------------------------------------------------------------------
// modem.h -- FoxSDR helper: the base class of fldigi's modems, reduced to what the receive code (and, in the test
// tool only, the transmit code) of MFSK and THOR use. fldigi's own src/include/modem.h is a 511-line header tied to
// its sound card, rig, waterfall and GUI code; none of that is taken. The member names and meanings are fldigi's.
//
// Written for this helper; distributed under the licence of the rest of the helper (GPL-3.0-only).
// ----------------------------------------------------------------------------
#ifndef MODEM_H
#define MODEM_H

#include <string>

#include "fm_shim.h"
#include "filters.h"

#define OUTBUFSIZE 65536
#define SIGSEARCH 5
#define QUALITYDEPTH 5

#define GET_TX_CHAR_ETX 3
#define GET_TX_CHAR_NODATA -1
#define GET_TX_CHAR_CR 0xD

// supplied by the test tool when the transmitter is compiled in
int get_tx_char(void);

class modem {
public:
    static double frequency;
    static double tx_frequency;
    static bool freqlock;

protected:
    trx_mode mode;
    bool stopflag;
    int fragmentsize;
    int samplerate;
    bool reverse;
    int sigsearch;
    bool sig_start;
    bool sig_stop;

    double bandwidth;
    double freqerr;
    double rx_corr;
    double tx_corr;

    double squelch;
    double metric;
    double syncpos;

    double outbuf[OUTBUFSIZE];

    // extended s/n reporting (pskmail): kept so the unchanged code compiles; never reported
    double s2n_ncount, s2n_sum, s2n_sum2, s2n_metric;
    bool s2n_valid;

    unsigned cap;

public:
    modem();
    virtual ~modem() {}

    virtual void init();
    virtual void tx_init() = 0;
    virtual void rx_init() = 0;
    virtual void restart() = 0;
    virtual void rx_flush() {}
    virtual int tx_process() { return -1; }
    virtual int rx_process(const double *, int len) = 0;
    virtual void shutdown() {}
    virtual void send_color_image(std::string) {}
    virtual void send_Grey_image(std::string) {}
    virtual void thor_send_image(std::string = "", bool = false) {}
    virtual void thor_send_avatar() {}
    virtual void m_thor_send_avatar() {}

    trx_mode get_mode() const { return mode; }
    virtual void set_freq(double);
    int get_freq() const { return (int)(frequency + 0.5); }
    double get_txfreq() const { return tx_frequency; }
    double get_txfreq_woffset() const { return tx_frequency; }
    void set_sigsearch(int n)
    {
        sigsearch = n;
        freqerr = 0.0;
    }
    void set_metric(double m) { metric = m; }
    void display_metric(double m) { set_metric(m); }
    double get_metric() const { return metric; }
    void set_reverse(bool on) { reverse = on; }
    bool get_reverse() const { return reverse; }
    double get_bandwidth() const { return bandwidth; }
    void set_bandwidth(double bw) { bandwidth = bw; }
    int get_samplerate() const { return samplerate; }
    void set_stopflag(bool b) { stopflag = b; }
    bool get_stopflag() const { return stopflag; }
    unsigned get_cap() const { return cap; }
    enum { CAP_AFC = 1 << 0, CAP_AFC_SR = 1 << 1, CAP_REV = 1 << 2, CAP_IMG = 1 << 3, CAP_BW = 1 << 4, CAP_RX = 1 << 5, CAP_TX = 1 << 6 };

    // decode-quality averaging, copied from fldigi's modem::update_quality / get_quality (src/trx/modem.cxx)
    int update_quality(int value, int mode = 0);
    int get_quality(int mode = 0);

    // transmit side: only the test tool has a real one (it collects the samples)
    void ModulateXmtr(double *buffer, int len);
    void videoText() {}
    void pretone() {}

    // number of audio samples the transmitter has made (test tool)
    static unsigned long tx_sample_count;

protected:
    virtual void s2nreport(void) {}

private:
    int quality[QUALITYDEPTH];
};

// the active modem (the modems ask whether a picture transfer is being timed)
extern modem *active_modem;

inline void set_video(double *, int, bool = false) {}

#endif
