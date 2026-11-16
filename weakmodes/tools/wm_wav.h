// weakmodes test tools: WAV writing and the test-signal noise convention (not part of the helper program).
// Licence: GPL-3.0-or-later (see LICENSE).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace wmtest {

// 12000 Hz, mono, 16-bit PCM WAV. Returns false on a write error.
inline bool write_wav16(const char *path, const std::vector<int16_t> &s, int rate = 12000) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 255, f); };
    auto u16 = [&](uint16_t v) { for (int i = 0; i < 2; i++) fputc((v >> (8 * i)) & 255, f); };
    uint32_t bytes = (uint32_t)s.size() * 2;
    fwrite("RIFF", 1, 4, f); u32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    fwrite("data", 1, 4, f); u32(bytes);
    for (int16_t v : s) u16((uint16_t)v);
    bool ok = fflush(f) == 0;
    fclose(f);
    return ok;
}

// Reads a 16-bit mono PCM WAV (any rate); returns false if it is not one. Skips unknown chunks.
inline bool read_wav16(const char *path, std::vector<int16_t> &s, int *rate = nullptr) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    unsigned char h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); return false; }
    bool ok = false;
    for (;;) {
        unsigned char c[8];
        if (fread(c, 1, 8, f) != 8) break;
        uint32_t len = c[4] | (c[5] << 8) | (c[6] << 16) | ((uint32_t)c[7] << 24);
        if (!memcmp(c, "fmt ", 4)) {
            std::vector<unsigned char> b(len);
            if (fread(b.data(), 1, len, f) != len) break;
            if (len >= 16 && (b[0] | (b[1] << 8)) == 1 && (b[2] | (b[3] << 8)) == 1 && (b[14] | (b[15] << 8)) == 16) {
                if (rate) *rate = b[4] | (b[5] << 8) | (b[6] << 16) | (b[7] << 24);
                ok = true;
            }
        } else if (!memcmp(c, "data", 4)) {
            if (!ok) break;
            s.resize(len / 2);
            size_t n = fread(s.data(), 2, len / 2, f);
            s.resize(n);
            fclose(f);
            return true;
        } else {
            fseek(f, (long)len + (len & 1), SEEK_CUR);
        }
    }
    fclose(f);
    return false;
}

inline int16_t clip16(double v) {
    if (v > 32767.0) return 32767;
    if (v < -32768.0) return -32768;
    return (int16_t)std::lrint(v);
}

} // namespace wmtest
