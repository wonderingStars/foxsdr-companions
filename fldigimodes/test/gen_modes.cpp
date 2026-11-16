// ----------------------------------------------------------------------------
// gen_modes.cpp -- test tool: signal generation through fldigi's transmitters (FOXSDR_TEST_TX). NOT part of the helper.
// The Olivia / Contestia part is the transmit half of fldigi's olivia::tx_process() / contestia::tx_process()
// (src/olivia/olivia.cxx, src/contestia/contestia.cxx) without the user interface. Written for this helper;
// GPL-3.0-only.
// ----------------------------------------------------------------------------
#include "gen_modes.h"

#include <cmath>
#include <cstring>
#include <memory>

#include "decoder.h"
#include "rsid_detector.h"
#include "fm_shim.h"
#include "jalocha/pj_mfsk.h"
#include "dsp.h"
#include "mfsk.h"
#include "mt63base.h"
#include "modem.h"
#include "thor.h"

// ---- the transmit hooks of fldigi's modem base class, as the test tool needs them -------------------------------------
static std::vector<double> *g_tx_out = nullptr;
static std::string g_tx_text;
static size_t g_tx_pos = 0;

void modem::ModulateXmtr(double *buffer, int len)
{
    if (g_tx_out)
        g_tx_out->insert(g_tx_out->end(), buffer, buffer + len);
    tx_sample_count += len;
}

// fldigi asks for the next character to send: text, then end of transmission
int get_tx_char(void)
{
    if (g_tx_pos < g_tx_text.size())
        return (unsigned char)g_tx_text[g_tx_pos++];
    return GET_TX_CHAR_ETX;
}

namespace gen {

static void tail_silence(std::vector<double> &o, int n) { o.insert(o.end(), n, 0.0); }

static bool gen_olivia(const Request &rq, const fm::ModeSpec &m, std::vector<double> &out, std::string &err)
{
    const bool contestia = (m.family == "contestia");
    MFSK_Transmitter<double> Tx;
    Tx.bContestia = contestia;
    Tx.Tones = (size_t)m.tones;
    Tx.Bandwidth = (size_t)m.bandwidth;
    Tx.SampleRate = 8000;
    Tx.OutputSampleRate = 8000;
    int fc_offset = contestia ? (m.bandwidth / 2) : (int)(m.bandwidth * (1.0 - 0.5 / m.tones) / 2.0);
    if (rq.reverse) {
        Tx.FirstCarrierMultiplier = (float)((rq.freq + fc_offset) / 500.0);
        Tx.Reverse = 1;
    } else {
        Tx.FirstCarrierMultiplier = (float)((rq.freq - fc_offset) / 500.0);
        Tx.Reverse = 0;
    }
    if (Tx.Preset() < 0) {
        err = "transmitter preset failed";
        return false;
    }
    std::vector<double> buf(Tx.MaxOutputLen);
    Tx.Start();
    Tx.PutChar(0);  // "Olivia Transmitter class requires at least one character"
    // idle characters (NUL) in front let the receiver's synchroniser integrate before the text; and behind
    std::string text = std::string((size_t)Tx.BitsPerSymbol * (size_t)(m.sinteg + 3), (char)0) + rq.text +
                       std::string((size_t)Tx.BitsPerSymbol * (size_t)(m.sinteg + 4), (char)0);
    size_t pos = 0;
    bool stopped = false;
    for (int guard = 0; guard < 50000000; guard++) {
        if (!stopped && Tx.GetReadReady() < Tx.BitsPerSymbol) {
            if (pos >= text.size()) {
                Tx.Stop();
                stopped = true;
            } else {
                unsigned c = (unsigned char)text[pos++];
                if (c > 127) {  // 8-bit extension: 127 then the low seven bits
                    Tx.PutChar(127);
                    Tx.PutChar(c & 127);
                } else
                    Tx.PutChar((uint8_t)c);
            }
        }
        int len = Tx.Output(buf.data());
        if (len > 0)
            out.insert(out.end(), buf.begin(), buf.begin() + len);
        if (!Tx.Running())
            break;
    }
    return true;
}

// MT63: the transmit half of fldigi's mt63::tx_process() (src/mt63/mt63.cxx) without the user interface and without
// the optional start tones
static bool gen_mt63(const Request &rq, const fm::ModeSpec &m, std::vector<double> &out, std::string &err)
{
    MT63tx Tx;
    if (Tx.Preset(rq.freq, m.bandwidth, m.variant == 64 ? 1 : 0) != 0) {
        err = "MT63 transmitter preset failed";
        return false;
    }
    double maxval = 0.0;
    auto emit = [&]() {
        int len = Tx.Comb.Output.Len;
        for (int i = 0; i < len; i++)
            if (fabs(Tx.Comb.Output.Data[i]) > maxval)
                maxval = fabs(Tx.Comb.Output.Data[i]);
        for (int i = 0; i < len; i++)
            out.push_back(Tx.Comb.Output.Data[i] / maxval);
    };
    int flush = Tx.DataInterleave;
    for (int i = 0; i < Tx.DataInterleave; i++) {
        Tx.SendChar(0);  // idle characters while the receiver's synchroniser locks (fldigi sends start tones here)
        emit();
    }
    for (size_t k = 0; k < rq.text.size(); k++) {
        unsigned c = (unsigned char)rq.text[k];
        if (c > 127) {
            Tx.SendChar(127);
            emit();
            c &= 127;
        }
        Tx.SendChar((char)c);
        emit();
    }
    while (--flush) {
        Tx.SendChar(0);
        emit();
    }
    Tx.SendJam();
    emit();
    return true;
}

// MFSK and THOR: the transmit half of the modem class itself, driven the way fldigi's trx thread drives it
static bool gen_class(const Request &rq, const fm::ModeSpec &m, std::vector<double> &out, std::string &err)
{
    long long id = fm::fldigi_mode_id(m);
    std::unique_ptr<modem> md;
    if (m.family == "mfsk")
        md.reset(new mfsk((trx_mode)id));
    else if (m.family == "thor")
        md.reset(new thor((trx_mode)id));
    else {
        err = "no transmitter class";
        return false;
    }
    modem::frequency = modem::tx_frequency = rq.freq;
    md->set_reverse(rq.reverse);
    g_tx_out = &out;
    g_tx_text = rq.text;
    g_tx_pos = 0;
    md->tx_init();
    int guard = 0;
    while (md->tx_process() != -1) {
        if (++guard > 10000000) {
            err = "the transmitter does not end";
            g_tx_out = nullptr;
            return false;
        }
    }
    g_tx_out = nullptr;
    return true;
}

// RSID: the transmit half of fldigi's cRsId::send() (src/rsid/rsid.cxx), without the user interface: 5 symbol periods of
// silence, the 15 tones, (for the second table: ESCAPE, 10 periods of silence, the second code), 5 periods of silence.
static void rsid_tones(std::vector<double> &out, int code, double txfreq, int sr, bool reverse)
{
    unsigned char rsid[15];
    fm::RsidDetector::encode(code, rsid);
    size_t symlen = (size_t)std::floor((1024.0 / 11025.0) * sr);
    double fr = txfreq - (11025.0 * 7 / 1024);
    double phase = 0.0;
    for (int i = 0; i < 15; i++) {
        int iTone = rsid[i];
        if (reverse)
            iTone = 15 - iTone;
        double freq = fr + iTone * 11025.0 / 1024;
        double phaseincr = 2.0 * M_PI * freq / sr;
        for (size_t j = 0; j < symlen; j++) {
            phase += phaseincr;
            if (phase > 2.0 * M_PI)
                phase -= 2.0 * M_PI;
            out.push_back(std::sin(phase));
        }
    }
}

static void gen_rsid(const Request &rq, std::vector<double> &out, int sr, std::string &err)
{
    bool ext = false;
    int code = fm::RsidDetector::code_for(rq.rsid_name, ext);
    if (code < 0) {
        err = "no RSID named " + rq.rsid_name;
        return;
    }
    size_t symlen = (size_t)std::floor((1024.0 / 11025.0) * sr);
    out.insert(out.end(), 5 * symlen, 0.0);
    if (ext) {
        rsid_tones(out, fm::RsidDetector::escape_code(), rq.freq, sr, rq.reverse);
        out.insert(out.end(), 10 * symlen, 0.0);
    }
    rsid_tones(out, code, rq.freq, sr, rq.reverse);
    out.insert(out.end(), 5 * symlen, 0.0);
}

bool generate(const Request &rq, std::vector<double> &out, int &rate, std::string &err)
{
    if (!rq.rsid_name.empty()) {
        rate = 8000;
        gen_rsid(rq, out, rate, err);
        if (!err.empty())
            return false;
        if (rq.rsid_only)
            return true;
    }
    if (rq.rsid_only) {
        err = "--rsid-only needs --rsid <name>";
        return false;
    }
    fm::ModeSpec m;
    if (!fm::parse_mode(rq.mode, m, err))
        return false;
    rate = 8000;
    if (m.family == "olivia" || m.family == "contestia")
        return gen_olivia(rq, m, out, err);
    if (m.family == "mt63")
        return gen_mt63(rq, m, out, err);
    if (m.family == "mfsk" || m.family == "thor") {
        rate = fm::mode_rate(m);
        return gen_class(rq, m, out, err);
    }
    err = "no transmitter for " + rq.mode + " yet";
    return false;
}

}  // namespace gen

// the receive code in the same objects reports skipped pictures; the test tool receives nothing
void fm_picture_skipped(int, int, bool) {}
