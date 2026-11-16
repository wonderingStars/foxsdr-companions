// weakmodes test tool: round trip of the 77-bit packer (tools/pack77.h) and the helper's unpacker (src/unpack77.cpp),
// plus a few fixed bit patterns. Exit code 0 when everything agrees. Licence: GPL-3.0-or-later.
#include <cstdio>
#include <cstring>
#include "pack77.h"
#include "wm_common.h"

int main() {
    const char *msgs[] = {"CQ K1ABC FN42", "K1ABC W9XYZ EN37", "W9XYZ K1ABC -11", "K1ABC W9XYZ R-09", "W9XYZ K1ABC RRR",
                          "K1ABC W9XYZ 73", "K1ABC W9XYZ RR73", "CQ SP6TLS KO02", "K1ABC/R W9XYZ/R R EN37",
                          "G4ABC EA8XYZ +05", "TNX BOB 73 GL", "FREE TEXT 123", "CQ TEST 123"};
    int bad = 0;
    for (const char *m : msgs) {
        uint8_t b[77];
        wm::unpack77_reset();
        if (!wmtest::pack77(m, b)) { printf("pack failed:   %s\n", m); bad++; continue; }
        bool ok = false;
        std::string u = wm::unpack77(b, &ok);
        // the packer does not know "CQ DX"-style tokens beyond what pack28 gives; compare after the round trip
        bool same = ok && u == m;
        printf("%s  %-28s -> %s\n", same ? "ok  " : "DIFF", m, u.c_str());
        if (!same) bad++;
    }
    // known good 77-bit vectors from WSJT-X documentation: none are shipped with the source, so the round trip above
    // is the check; also the failure paths:
    uint8_t z[77];
    memset(z, 1, sizeof z); // i3 = 7: undefined
    bool ok = true;
    wm::unpack77(z, &ok);
    if (ok) { printf("DIFF all-ones should fail\n"); bad++; }
    memset(z, 0, sizeof z); // free text of all blanks
    wm::unpack77(z, &ok);
    if (ok) { printf("DIFF all-zero should fail\n"); bad++; }
    printf(bad ? "FAILED %d\n" : "all round trips ok\n", bad);
    return bad ? 1 : 0;
}
