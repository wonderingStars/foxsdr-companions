// sigtool - small file utilities for the trunkctl tests (not shipped).
//   sigtool garbage <seed> <bytes> <out>      pseudo-random bytes
//   sigtool lines <file>                      print the number of lines
//   sigtool oddbyte <in> <out>                copy and append one stray byte
//   sigtool s16tof32 <in> <out>               int16 samples -> float32 (x / 32768)
//   sigtool feed <in> <chunk>                 write the file to stdout in small writes, flushing each
// This file is part of trunkctl (GPL-3.0).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static std::vector<unsigned char> slurp(const char *p) {
    std::vector<unsigned char> v;
    FILE *f = fopen(p, "rb");
    if (!f) { perror(p); exit(1); }
    unsigned char b[65536];
    size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) v.insert(v.end(), b, b + n);
    fclose(f);
    return v;
}

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    const char *m = argv[1];
    if (!strcmp(m, "garbage") && argc == 5) {
        uint32_t s = (uint32_t)atol(argv[2]);
        long n = atol(argv[3]);
        FILE *f = fopen(argv[4], "wb");
        if (!f) return 1;
        for (long i = 0; i < n; i++) {
            s = s * 1664525u + 1013904223u;
            fputc((int)(s >> 24), f);
        }
        fclose(f);
        return 0;
    }
    if (!strcmp(m, "lines") && argc == 3) {
        std::vector<unsigned char> v = slurp(argv[2]);
        long n = 0;
        for (unsigned char c : v) if (c == '\n') n++;
        printf("%ld\n", n);
        return 0;
    }
    if (!strcmp(m, "oddbyte") && argc == 4) {
        std::vector<unsigned char> v = slurp(argv[2]);
        v.push_back(0x55);
        FILE *f = fopen(argv[3], "wb");
        fwrite(v.data(), 1, v.size(), f);
        fclose(f);
        return 0;
    }
    if (!strcmp(m, "s16tof32") && argc == 4) {
        std::vector<unsigned char> v = slurp(argv[2]);
        FILE *f = fopen(argv[3], "wb");
        for (size_t i = 0; i + 1 < v.size(); i += 2) {
            float x = (float)(int16_t)(v[i] | (v[i + 1] << 8)) / 32768.0f;
            fwrite(&x, 4, 1, f);
        }
        fclose(f);
        return 0;
    }
    if (!strcmp(m, "feed") && argc == 4) {
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        std::vector<unsigned char> v = slurp(argv[2]);
        size_t chunk = (size_t)atol(argv[3]);
        if (!chunk) chunk = 1;
        for (size_t o = 0; o < v.size(); o += chunk) {
            size_t n = v.size() - o < chunk ? v.size() - o : chunk;
            fwrite(v.data() + o, 1, n, stdout);
            fflush(stdout);
        }
        return 0;
    }
    fprintf(stderr, "sigtool: bad arguments\n");
    return 2;
}
