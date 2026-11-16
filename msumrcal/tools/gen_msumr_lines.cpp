// Test tool for msumrcal (GPL-3.0, same licence as the helper). Not part of the helper.
//
//   gen_msumr_lines gen   <lines.txt> <truth.txt>   write a synthetic pass and the physical truth for it
//   gen_msumr_lines check <helper.jsonl> <truth.txt> compare the helper's "bt" events with the truth (exit 0 = all inside)
//
// The scene is built from the SAME constants and Planck functions as the helper (src/msumr_calib.h, taken from SatDump):
// a linear detector (count = base + slope * radiance) views a cold target at 258 K and a hot target at 313 K, then scenes of
// known brightness temperature. The truth file holds, per pixel, the temperature window that a count error of +-2 allows.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/msumr_calib.h"

struct Model
{
    double base, slope;
    double count(double rad) const { return base + slope * rad; }
    double rad(double count) const { return (count - base) / slope; }
};

static Model model_for(int c)
{
    Model m;
    m.base = 90 + 20 * c;
    double r_max = msumr::temperature_to_radiance(335.0, msumr::kWavenumber[c]);
    m.slope = (880.0 - m.base) / r_max;
    return m;
}

static double bt_of(const Model &m, double count, int c)
{
    double kel;
    double r = m.rad(count);
    if (!msumr::brightness_temperature(r, c, kel))
        return -1;
    return kel;
}

static int gen(const char *lines_path, const char *truth_path)
{
    std::ofstream L(lines_path, std::ios::binary), T(truth_path, std::ios::binary);
    L << "# synthetic MSU-MR pass: strips 0..5, 8 image lines per strip, 12 pixels per row\n";
    const int kStrips = 6, kPx = 12;
    // strip: 0 normal, 1 temperatures missing ('-'), 2 normal, 3 cold temperature glitch (+20 K), 4 views zero, 5 no T record at all
    for (int k = 0; k < 5; k++)
    {
        L << "T " << k << ' ';
        if (k == 1)
            L << "- - - - ";
        else if (k == 3)
            L << "277.9 278.1 313.0 313.2 ";
        else
            L << "257.9 258.1 313.0 313.2 ";
        for (int c = 0; c < 3; c++)
        {
            Model m = model_for(c);
            long cold = k == 4 ? 0 : lround(m.count(msumr::temperature_to_radiance(258.0, msumr::kWavenumber[c])));
            long hot = k == 4 ? 0 : lround(m.count(msumr::temperature_to_radiance(313.1, msumr::kWavenumber[c])));
            L << cold << ' ' << hot << (c < 2 ? " " : "\n");
        }
    }
    const int rows[] = {0, 7, 8, 17, 25, 33, 41};
    for (int y : rows)
        for (int c = 0; c < 3; c++)
        {
            Model m = model_for(c);
            L << "P " << y << ' ' << (c + 4);
            T << y << ' ' << (c + 4);
            for (int i = 0; i < kPx; i++)
            {
                double bt = 255.0 + 6.0 * i + 2.0 * c; // 255..329 K
                long cnt = lround(m.count(msumr::temperature_to_radiance(bt, msumr::kWavenumber[c])));
                bool expect_null = (y / 8 >= 4); // strip 4 has zero views, strip 5 has no telemetry
                if (y == 17 && i == 3)
                {
                    cnt = 0; // pixel value 0 is invalid
                    expect_null = true;
                }
                L << ' ' << cnt;
                if (expect_null)
                    T << " -1 -1";
                else
                {
                    double lo = bt_of(m, (double)cnt - 2.0, c), hi = bt_of(m, (double)cnt + 2.0, c);
                    char b[64];
                    snprintf(b, sizeof b, " %.4f %.4f", lo, hi);
                    T << b;
                }
            }
            L << "\n";
            T << "\n";
        }
    (void)kStrips;
    return 0;
}

static std::vector<std::string> split_ws(const std::string &s)
{
    std::istringstream is(s);
    std::vector<std::string> v;
    std::string w;
    while (is >> w)
        v.push_back(w);
    return v;
}

static int check(const char *json_path, const char *truth_path)
{
    std::ifstream J(json_path), T(truth_path);
    if (!J || !T)
    {
        fprintf(stderr, "check: cannot open inputs\n");
        return 2;
    }
    std::string line;
    int rows_checked = 0, px_checked = 0, px_null_ok = 0;
    std::vector<std::vector<std::string>> truth;
    while (std::getline(T, line))
        truth.push_back(split_ws(line));
    size_t ti = 0;
    while (std::getline(J, line))
    {
        if (line.find("\"type\":\"bt\"") == std::string::npos)
            continue;
        size_t a = line.find("\"bt\":[");
        if (a == std::string::npos)
            return 1;
        std::string arr = line.substr(a + 6, line.rfind(']') - (a + 6));
        for (char &ch : arr)
            if (ch == ',')
                ch = ' ';
        std::vector<std::string> v = split_ws(arr);
        if (ti >= truth.size())
        {
            fprintf(stderr, "check: more bt rows than truth rows\n");
            return 1;
        }
        const std::vector<std::string> &t = truth[ti++];
        if (t.size() != 2 + 2 * v.size())
        {
            fprintf(stderr, "check: row %zu has %zu values, truth has %zu\n", ti, v.size(), (t.size() - 2) / 2);
            return 1;
        }
        for (size_t i = 0; i < v.size(); i++)
        {
            double lo = atof(t[2 + 2 * i].c_str()), hi = atof(t[3 + 2 * i].c_str());
            if (lo < 0)
            {
                if (v[i] != "null")
                {
                    fprintf(stderr, "check: row %zu px %zu: expected null, got %s\n", ti, i, v[i].c_str());
                    return 1;
                }
                px_null_ok++;
                continue;
            }
            if (v[i] == "null")
            {
                fprintf(stderr, "check: row %zu px %zu: unexpected null\n", ti, i);
                return 1;
            }
            double bt = atof(v[i].c_str());
            if (bt < lo - 0.02 || bt > hi + 0.02)
            {
                fprintf(stderr, "check: row %zu px %zu: %.2f K outside %.2f..%.2f\n", ti, i, bt, lo, hi);
                return 1;
            }
            px_checked++;
        }
        rows_checked++;
    }
    if (ti != truth.size())
    {
        fprintf(stderr, "check: %zu truth rows but %zu bt rows\n", truth.size(), ti);
        return 1;
    }
    printf("check ok: %d rows, %d temperatures inside the window, %d expected nulls\n", rows_checked, px_checked, px_null_ok);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "gen"))
        return gen(argv[2], argv[3]);
    if (argc == 4 && !strcmp(argv[1], "check"))
        return check(argv[2], argv[3]);
    fprintf(stderr, "usage: gen_msumr_lines gen <lines.txt> <truth.txt> | check <helper.jsonl> <truth.txt>\n");
    return 2;
}
