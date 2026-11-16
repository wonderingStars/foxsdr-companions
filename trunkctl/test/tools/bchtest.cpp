// bchtest - unit test of the EDACS BCH(40,28) module (not shipped). Exit 0 and "bchtest ok" when everything holds.
// This file is part of trunkctl (GPL-3.0).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "../../src/bch4028.h"
using namespace trunkctl;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main() {
    // The BCH(63,51) t=2 generator polynomial is x^12+x^10+x^8+x^5+x^4+x^3+1 (octal 12471, Lin & Costello table):
    // the parity of the message x^0 is x^12 mod g = g without its leading term.
    CHECK((bch4028_encode(1) & 0xFFF) == 0x539);
    CHECK(bch4028_encode(0) == 0);
    uint32_t s = 12345;
    int n1 = 0, n2 = 0, n3_rejected = 0, n3 = 0;
    for (int iter = 0; iter < 300; iter++) {
        s = s * 1664525u + 1013904223u;
        uint32_t d = (s >> 4) & 0xFFFFFFF;
        uint64_t w = bch4028_encode(d);
        CHECK((w >> 12) == d);
        uint64_t c;
        CHECK(bch4028_decode(w, 1, &c) == 0 && c == w);
        for (int i = 0; i < 40; i++) {
            CHECK(bch4028_decode(w ^ (1ull << i), 1, &c) == 1 && c == w);
            CHECK(bch4028_decode(w ^ (1ull << i), 0, &c) == -1);
            n1++;
        }
        if (iter < 20) {
            for (int i = 0; i < 40; i++)
                for (int j = i + 1; j < 40; j++) {
                    uint64_t r = w ^ (1ull << i) ^ (1ull << j);
                    CHECK(bch4028_decode(r, 2, &c) == 2 && c == w);
                    CHECK(bch4028_decode(r, 1, &c) == -1);
                    n2++;
                }
        }
        // three errors: never returns the original word (d_min = 5 ... a 3-error word is either rejected or
        // "corrected" to a different codeword)
        for (int k = 0; k < 10; k++) {
            s = s * 1664525u + 1013904223u;
            int a = (int)((s >> 8) % 40);
            s = s * 1664525u + 1013904223u;
            int b = (int)((s >> 8) % 40);
            s = s * 1664525u + 1013904223u;
            int e = (int)((s >> 8) % 40);
            if (a == b || b == e || a == e) continue;
            uint64_t r = w ^ (1ull << a) ^ (1ull << b) ^ (1ull << e);
            int rc = bch4028_decode(r, 2, &c);
            n3++;
            if (rc < 0) n3_rejected++;
            else CHECK(c != w);
        }
    }
    printf("bchtest: %d single, %d double error cases corrected; %d/%d triple-error words rejected\n", n1, n2, n3_rejected, n3);
    if (failures) { printf("bchtest FAILED (%d)\n", failures); return 1; }
    printf("bchtest ok\n");
    return 0;
}
