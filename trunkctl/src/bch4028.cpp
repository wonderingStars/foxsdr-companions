// trunkctl - EDACS BCH(40,28), see bch4028.h. This file is part of trunkctl (GPL-3.0).
#include "bch4028.h"

namespace trunkctl {
namespace {

struct Gf {
    int exp[126];
    int log[64];
    uint32_t gen; // generator polynomial g(x), 13 bits (degree 12)
    Gf() {
        // GF(2^6) with p(x) = x^6 + x + 1
        int x = 1;
        for (int i = 0; i < 63; i++) {
            exp[i] = x;
            exp[i + 63] = x;
            log[x] = i;
            x <<= 1;
            if (x & 0x40)
                x ^= 0x43;
        }
        log[0] = -1;
        // g(x) = lcm of the minimal polynomials of alpha^1..alpha^4 = m1(x) * m3(x):
        // multiply out (x + alpha^e) over the cyclotomic cosets {1,2,4,8,16,32} and {3,6,12,24,48,33}.
        int poly[13] = {1}; // coefficients in GF(64), poly[i] = coefficient of x^i, start with 1
        int deg = 0;
        const int roots[12] = {1, 2, 4, 8, 16, 32, 3, 6, 12, 24, 48, 33};
        for (int r = 0; r < 12; r++) {
            int a = exp[roots[r]];
            int next[13] = {0};
            for (int i = 0; i <= deg; i++) {
                next[i + 1] ^= poly[i];       // x * poly
                next[i] ^= mul(poly[i], a);   // alpha^e * poly
            }
            deg++;
            for (int i = 0; i <= deg; i++)
                poly[i] = next[i];
        }
        gen = 0;
        for (int i = 0; i <= 12; i++)
            if (poly[i] & 1) // all coefficients are 0 or 1 for a binary code
                gen |= 1u << i;
    }
    int mul(int a, int b) const {
        if (!a || !b)
            return 0;
        return exp[log[a] + log[b]];
    }
    int inv(int a) const { return exp[63 - log[a]]; }
};

const Gf &gf() {
    static const Gf g;
    return g;
}

} // namespace

uint64_t bch4028_encode(uint32_t data28) {
    const uint32_t g = gf().gen;
    uint64_t r = (uint64_t)(data28 & 0xFFFFFFFu) << 12;
    for (int i = 39; i >= 12; i--)
        if (r & (1ull << i))
            r ^= (uint64_t)g << (i - 12);
    return ((uint64_t)(data28 & 0xFFFFFFFu) << 12) | (r & 0xFFF);
}

int bch4028_decode(uint64_t w, int max_errors, uint64_t *corrected) {
    const Gf &f = gf();
    w &= 0xFFFFFFFFFFull;
    int s1 = 0, s3 = 0;
    for (int i = 0; i < 40; i++) {
        if (w & (1ull << i)) {
            s1 ^= f.exp[i % 63];
            s3 ^= f.exp[(3 * i) % 63];
        }
    }
    if (s1 == 0 && s3 == 0) {
        *corrected = w;
        return 0;
    }
    if (max_errors < 1 || s1 == 0)
        return -1; // s1 == 0 with s3 != 0 cannot be 1 or 2 errors
    // one error: s3 == s1^3, error position = log(s1)
    int s1c = f.mul(f.mul(s1, s1), s1);
    if (s3 == s1c) {
        int pos = f.log[s1];
        if (pos >= 40)
            return -1;
        *corrected = w ^ (1ull << pos);
        return 1;
    }
    if (max_errors < 2)
        return -1;
    // two errors: x1 + x2 = s1, x1 * x2 = (s3 + s1^3) / s1
    int sigma2 = f.mul(s3 ^ s1c, f.inv(s1));
    for (int i = 0; i < 40; i++) {
        int x1 = f.exp[i];
        int x2 = s1 ^ x1;
        if (x2 == 0 || f.mul(x1, x2) != sigma2)
            continue;
        int j = f.log[x2];
        if (j >= 40 || j == i)
            continue;
        *corrected = w ^ (1ull << i) ^ (1ull << j);
        return 2;
    }
    return -1;
}

} // namespace trunkctl
