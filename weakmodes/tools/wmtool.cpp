// weakmodes test tool: small utilities used by CTest (WAV to raw, feeding a pipe in small writes, float conversion,
// garbage, line count). Not part of the helper. Licence: GPL-3.0-or-later.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include "wm_wav.h"

static std::vector<unsigned char> slurp(const char *p) {
    std::vector<unsigned char> v;
    FILE *f = fopen(p, "rb");
    if (!f) return v;
    unsigned char b[65536];
    size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) v.insert(v.end(), b, b + n);
    fclose(f);
    return v;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "wmtool wav2raw|feed|s16tof32|garbage|lines|oddbyte ...\n"); return 2; }
    std::string c = argv[1];
    if (c == "wav2raw" && argc == 4) {
        std::vector<int16_t> s;
        int rate = 0;
        if (!wmtest::read_wav16(argv[2], s, &rate) || rate != 12000) { fprintf(stderr, "wmtool: not a 12000 Hz 16-bit mono WAV\n"); return 1; }
        FILE *f = fopen(argv[3], "wb");
        if (!f) return 1;
        fwrite(s.data(), 2, s.size(), f);
        fclose(f);
        return 0;
    }
    if (c == "feed" && argc == 4) { // raw file to stdout in writes of N bytes
        std::vector<unsigned char> v = slurp(argv[2]);
        size_t n = (size_t)atol(argv[3]);
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        for (size_t i = 0; i < v.size(); i += n) {
            size_t k = v.size() - i < n ? v.size() - i : n;
            if (fwrite(&v[i], 1, k, stdout) != k) return 0; // reader closed early: fine
            fflush(stdout);
        }
        return 0;
    }
    if (c == "s16tof32" && argc == 4) {
        std::vector<unsigned char> v = slurp(argv[2]);
        FILE *f = fopen(argv[3], "wb");
        if (!f) return 1;
        for (size_t i = 0; i + 1 < v.size(); i += 2) {
            int16_t s = (int16_t)(v[i] | (v[i + 1] << 8));
            float x = (float)s / 32767.0f;
            fwrite(&x, 4, 1, f);
        }
        fclose(f);
        return 0;
    }
    if (c == "garbage" && argc == 5) { // seed bytes out
        unsigned long long s = strtoull(argv[2], nullptr, 10) * 2862933555777941757ull + 3037000493ull;
        size_t n = (size_t)atol(argv[3]);
        FILE *f = fopen(argv[4], "wb");
        if (!f) return 1;
        for (size_t i = 0; i < n; i++) { s = s * 6364136223846793005ull + 1442695040888963407ull; fputc((int)(s >> 56), f); }
        fclose(f);
        return 0;
    }
    if (c == "lines" && argc == 3) {
        std::vector<unsigned char> v = slurp(argv[2]);
        long n = 0;
        for (unsigned char ch : v) if (ch == '\n') n++;
        printf("%ld\n", n);
        return 0;
    }
    if (c == "oddbyte" && argc == 4) {
        std::vector<unsigned char> v = slurp(argv[2]);
        v.push_back(0x55);
        FILE *f = fopen(argv[3], "wb");
        if (!f) return 1;
        fwrite(v.data(), 1, v.size(), f);
        fclose(f);
        return 0;
    }
    fprintf(stderr, "wmtool: bad arguments\n");
    return 2;
}
