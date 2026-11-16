// sigtool - tiny deterministic signal tool for the hamdrm tests (not part of the helper; GPL-3.0-or-later like the helper).
//   sigtool noise <out.s16> <seconds> <seed> [amplitude]        white gaussian noise, 12000 Hz mono s16
//   sigtool cut   <in.s16> <out.s16> <seconds>                  keep the first N seconds
//   sigtool gap   <in.s16> <out.s16> <from_s> <length_s>        replace a stretch by silence (a fade-out / drop-out)
//   sigtool mix   <in.s16> <out.s16> <seed> <noise_rms>         add white gaussian noise of the given rms (in s16 counts)
//   sigtool join  <a.s16> <b.s16> <out.s16> <silence_s>         a, then silence, then b
//   sigtool rsenc <in> <out> <rs1|rs2|rs3|rs4> <ext>             Reed-Solomon code a file the way QSSTV does (.rsN files)
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "rs_file.h"

static std::vector<int16_t> load(const char *p)
{
    std::vector<int16_t> v;
    FILE *f = fopen(p, "rb");
    if (!f)
    {
        fprintf(stderr, "cannot open %s\n", p);
        exit(2);
    }
    int16_t buf[4096];
    size_t n;
    while ((n = fread(buf, sizeof(int16_t), 4096, f)) > 0)
        v.insert(v.end(), buf, buf + n);
    fclose(f);
    return v;
}
static void save(const char *p, const std::vector<int16_t> &v)
{
    FILE *f = fopen(p, "wb");
    if (!f)
    {
        fprintf(stderr, "cannot write %s\n", p);
        exit(2);
    }
    fwrite(v.data(), sizeof(int16_t), v.size(), f);
    fclose(f);
}
static int16_t clip(double x) { return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : std::lround(x)); }

int main(int argc, char **argv)
{
    if (argc >= 5 && !strcmp(argv[1], "noise"))
    {
        std::mt19937 g((unsigned)atoi(argv[4]));
        std::normal_distribution<double> nd(0.0, argc > 5 ? atof(argv[5]) : 3000.0);
        std::vector<int16_t> v((size_t)(atof(argv[3]) * 12000));
        for (auto &s : v)
            s = clip(nd(g));
        save(argv[2], v);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "cut"))
    {
        auto v = load(argv[2]);
        size_t n = (size_t)(atof(argv[4]) * 12000);
        if (n < v.size())
            v.resize(n);
        save(argv[3], v);
        return 0;
    }
    if (argc == 6 && !strcmp(argv[1], "gap"))
    {
        auto v = load(argv[2]);
        size_t a = (size_t)(atof(argv[4]) * 12000), n = (size_t)(atof(argv[5]) * 12000);
        for (size_t i = a; i < a + n && i < v.size(); i++)
            v[i] = 0;
        save(argv[3], v);
        return 0;
    }
    if (argc == 6 && !strcmp(argv[1], "mix"))
    {
        auto v = load(argv[2]);
        std::mt19937 g((unsigned)atoi(argv[4]));
        std::normal_distribution<double> nd(0.0, atof(argv[5]));
        for (auto &s : v)
            s = clip(s + nd(g));
        save(argv[3], v);
        return 0;
    }
    if (argc == 6 && !strcmp(argv[1], "join"))
    {
        auto a = load(argv[2]), b = load(argv[3]);
        a.insert(a.end(), (size_t)(atof(argv[5]) * 12000), 0);
        a.insert(a.end(), b.begin(), b.end());
        save(argv[4], a);
        return 0;
    }
    if (argc == 6 && !strcmp(argv[1], "rsenc"))
    {
        FILE *f = fopen(argv[2], "rb");
        if (!f)
            return 2;
        Bytes b;
        unsigned char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0)
            b.insert(b.end(), buf, buf + n);
        fclose(f);
        std::string t = argv[4];
        eRSType rt = t == "rs1" ? RST1 : t == "rs2" ? RST2 : t == "rs3" ? RST3 : t == "rs4" ? RST4 : RSTNONE;
        reedSolomonCoder enc;
        if (!enc.encode(b, argv[5], rt))
            return 3;
        f = fopen(argv[3], "wb");
        if (!f)
            return 2;
        fwrite(b.data(), 1, b.size(), f);
        fclose(f);
        return 0;
    }
    fprintf(stderr, "usage: see the header of sigtool.cpp\n");
    return 2;
}
