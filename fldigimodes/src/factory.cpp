// ----------------------------------------------------------------------------
// factory.cpp -- FoxSDR fldigimodes helper: picks the decoder for a mode. Written for this helper; GPL-3.0-only.
// ----------------------------------------------------------------------------
#include "decoder.h"

namespace fm {

std::unique_ptr<Decoder> make_decoder(const Options &opt, std::string &err)
{
    const ModeSpec &m = opt.mode;
    if (m.family == "olivia")
        return make_olivia_decoder(opt, false, err);
    if (m.family == "contestia")
        return make_olivia_decoder(opt, true, err);
    if (m.family == "mfsk")
        return make_mfsk_decoder(opt, err);
    if (m.family == "mt63")
        return make_mt63_decoder(opt, err);
    if (m.family == "thor")
        return make_thor_decoder(opt, err);
    err = "mode " + mode_name(m) + " is not built yet";
    return nullptr;
}

}  // namespace fm
