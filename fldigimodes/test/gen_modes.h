// gen_modes.h -- test tool: signal generation through fldigi's transmitters. Written for this helper; GPL-3.0-only.
#ifndef GEN_MODES_H
#define GEN_MODES_H
#include <string>
#include <vector>
namespace gen {
struct Request {
    std::string mode;     // as the helper's --mode: olivia-8-500, mfsk16, ...
    std::string text;
    double freq = 1500;   // audio centre of the signal
    bool reverse = false;
    std::string rsid_name; // if not empty: send this RSID (a name from fldigi's tables, e.g. MFSK16) before the signal
    bool rsid_only = false;
};
// Makes the clean transmission (peak not normalised). rate is set to the modem's sample rate.
bool generate(const Request &rq, std::vector<double> &out, int &rate, std::string &err);
}  // namespace gen
#endif
