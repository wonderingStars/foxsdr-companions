// ----------------------------------------------------------------------------
// rsid_detector.h -- FoxSDR fldigimodes helper: the RSID (Reed-Solomon ID) detector. Written for this helper;
// GPL-3.0-only. The detection itself is in rsid_detector.cpp, built from fldigi's rsid.cxx.
// ----------------------------------------------------------------------------
#ifndef FM_RSID_DETECTOR_H
#define FM_RSID_DETECTOR_H

#include <functional>
#include <memory>
#include <string>

namespace fm {

struct RsidHit {
    int code = 0;                 // the RSID number as sent
    std::string name;             // the RSID name as fldigi lists it (for example MFSK16, OLIVIA_8_500)
    std::string fldigi_mode;      // fldigi's mode id name when fldigi has the mode, else empty
    bool known_to_fldigi = false; // false: the ID is in the table but fldigi has no modem for it
    bool extended = false;        // found in the second (extended) table, after an ESCAPE code
    double freq_hz = 0;           // audio frequency fldigi would tune to
    bool eot = false;             // the end-of-transmission RSID
};

class RsidDetector {
public:
    // rate: sample rate of the audio that will be passed to process()
    explicit RsidDetector(int rate);
    ~RsidDetector();
    void set_callback(std::function<void(const RsidHit &)> cb);
    // wide: search the whole audio band (fldigi's "RSID detector searches the entire passband"); otherwise +-200 Hz
    // around centre. reverse: the sideband is reversed.
    void set_search(bool wide, double centre, bool reverse);
    void process(const double *buf, int n);
    // sizes of the two name tables (for the README and the report)
    static int table1_size();
    static int table2_size();
    // test support: the 15 symbols (tones 0..15) fldigi sends for an RSID code (Encode()), and the code of a name
    // such as "MFSK16" (-1 if the name is not in the tables; extended is set if it is in the second table)
    static void encode(int code, unsigned char symbols[15]);
    static int code_for(const std::string &name, bool &extended);
    // the code and name of entry i of table 1 / table 2 (i < table1_size() / table2_size()); known: fldigi has the mode
    static void entry(int table, int i, int &code, std::string &name, bool &known);
    // the number of the ESCAPE code that announces the second table
    static int escape_code();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace fm

#endif
