// compat/txglobals.cpp - NOT from QSSTV. Definitions of globals that QSSTV defines in its receiver/application code
// but which the transmit chain uses (see compat/drm.h). Licence: GNU GPL version 3 or later (same as the helper).
#include "drm.h"

bool stopDRM = false;
uint txTransportID = 0;
std::vector<std::vector<unsigned char> > *txGroupDump = nullptr;
