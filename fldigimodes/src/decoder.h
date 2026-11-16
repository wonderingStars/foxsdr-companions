// ----------------------------------------------------------------------------
// decoder.h -- FoxSDR fldigimodes helper: the interface between the helper's main program and one fldigi modem.
//
// Written for this helper (not fldigi code); GPL-3.0-only like the rest of the helper.
// ----------------------------------------------------------------------------
#ifndef FM_DECODER_H
#define FM_DECODER_H

#include <cmath>
#include <memory>
#include <string>

namespace fm {

// What the modem knows about the signal it is decoding. NaN means "this modem does not provide it".
struct Quality {
    double offset_hz = NAN;  // tuning offset the modem has found (Hz, signed)
    double snr_db = NAN;     // signal to noise (dB), where the modem computes one
    double sync = NAN;       // the modem's own sync/FEC ratio (Olivia and Contestia: Jalocha's S/N, linear)
    double metric = NAN;     // fldigi's 0..100 signal metric
    double freq_hz = NAN;    // the audio frequency the modem is now tuned to (after any AFC)
};

// One receive-only modem. Decoded characters are delivered through fm_rx_char_sink (see fm_shim.h).
class Decoder {
public:
    virtual ~Decoder() {}
    // Any number of samples (the caller feeds fixed blocks). Samples are the audio as doubles in -1..+1.
    virtual void process(const double *buf, int n) = 0;
    // End of input: push out what the modem still holds.
    virtual void flush() {}
    virtual Quality quality() const = 0;
    virtual std::string name() const = 0;  // the --mode text that selects it, normalised
    virtual std::string family() const = 0;
    virtual int rate() const { return 8000; }
};

// A mode selection parsed from the command line.
struct ModeSpec {
    std::string family;  // olivia, contestia, mfsk, mt63, thor
    int tones = 0;       // olivia / contestia
    int bandwidth = 0;   // olivia / contestia, MT63 (500, 1000, 2000)
    int variant = 0;     // mfsk: the number (4, 8, 11, 16, 22, 31, 32, 64, 128), thor: the number, MT63: interleave 32 or 64
    int smargin = 8;     // olivia / contestia sync margin (tone spacings)
    int sinteg = 4;      // olivia / contestia sync integration (FEC blocks)
    bool mt63_long_integral = false;
};

struct Options {
    ModeSpec mode;
    double freq = 1500.0;        // centre audio frequency of the signal (Hz)
    bool squelch_on = true;      // fldigi's squelch (its default is on at 5.0)
    double squelch = 5.0;        // the slider value
    bool reverse = false;        // sideband reversed (the tones run the other way)
};

// Parses text such as "olivia-8-500", "contestia-16-1000", "mfsk16", "mt63-1000l", "thor16". Returns false and
// fills err if the text names no mode this helper has.
bool parse_mode(const std::string &text, ModeSpec &out, std::string &err);
std::string mode_name(const ModeSpec &m);

std::unique_ptr<Decoder> make_decoder(const Options &opt, std::string &err);

// one factory per modem family (each in its own source file)
std::unique_ptr<Decoder> make_olivia_decoder(const Options &opt, bool contestia, std::string &err);
std::unique_ptr<Decoder> make_mfsk_decoder(const Options &opt, std::string &err);
std::unique_ptr<Decoder> make_thor_decoder(const Options &opt, std::string &err);
std::unique_ptr<Decoder> make_mt63_decoder(const Options &opt, std::string &err);

// fldigi's mode number (the MODE_xxx of src/include/globals.h) for a mode, and the sample rate it runs at
long long fldigi_mode_id(const ModeSpec &m);
int mode_rate(const ModeSpec &m);

}  // namespace fm

#endif
