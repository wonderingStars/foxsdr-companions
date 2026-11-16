// weakmodes / JT9: message packing and unpacking, convolutional encoder, interleaver.
// C++ port of parts of WSJT-X lib/packjt.f90, lib/gen9.f90, lib/encode232.f90, lib/entail.f90, lib/interleave9.f90,
// lib/graycode.f90, lib/jt9sync.f90, lib/conv232.f90, lib/fmtmsg.f90, lib/grid2deg.f90, lib/deg2grid.f90.
// Upstream routine names are given in comments beside each ported part.
//
// Copyright (C) 2001-2025 Joseph H. Taylor, Jr., K1JT and the WSJT Development Group.
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU General
// Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option)
// any later version. It is distributed WITHOUT ANY WARRANTY. See LICENSE.

#include "jt9_pack.h"
#include "jt9_tables.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace wm {
namespace jt9 {

// jt9sync.f90
const int kIsync[85] = {1, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0,
                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
                        0, 0, 0, 0, 0, 0, 1, 0, 1};
const int kIi[16] = {1, 2, 5, 10, 16, 23, 33, 35, 51, 52, 55, 60, 66, 73, 83, 85};
const int kIi2[16] = {1, 3, 9, 19, 31, 45, 65, 69, 101, 103, 109, 119, 131, 145, 165, 169};

// conv232.f90
const int32_t kNpoly1 = -221228207;
const int32_t kNpoly2 = -463389625;
int parity8(int n) { // partab(0:255)
    n &= 255;
    n ^= n >> 4;
    n ^= n >> 2;
    n ^= n >> 1;
    return n & 1;
}

namespace {

const int kNBase = 37 * 36 * 10 * 27 * 27 * 27; // 262177560
const int kNGBase = 180 * 180;

char at(const std::string &s, size_t i) { return i < s.size() ? s[i] : ' '; } // blank-padded Fortran string, 0-based
std::string rtrim(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
int nint(double x) { return (int)std::lround(x); }

// packjt.f90 nchar: 0..36 for callsign packing
int nchar(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    return 36;
}

// grid2deg.f90: Maidenhead locator (6 characters) to degrees west longitude and north latitude
void grid2deg(std::string grid0, double *dlong, double *dlat) {
    std::string g = grid0;
    while (g.size() < 6) g += ' ';
    int i = (unsigned char)g[4];
    if (g[4] == ' ' || i <= 64 || i >= 128) { g[4] = 'm'; g[5] = 'm'; }
    if (g[0] >= 'a' && g[0] <= 'z') g[0] = (char)(g[0] + 'A' - 'a');
    if (g[1] >= 'a' && g[1] <= 'z') g[1] = (char)(g[1] + 'A' - 'a');
    if (g[4] >= 'A' && g[4] <= 'Z') g[4] = (char)(g[4] - 'A' + 'a');
    if (g[5] >= 'A' && g[5] <= 'Z') g[5] = (char)(g[5] - 'A' + 'a');
    int nlong = 180 - 20 * (g[0] - 'A');
    int n20d = 2 * (g[2] - '0');
    double xminlong = 5 * (g[4] - 'a' + 0.5);
    *dlong = nlong - n20d - xminlong / 60.0;
    int nlat = -90 + 10 * (g[1] - 'A') + g[3] - '0';
    double xminlat = 2.5 * (g[5] - 'a' + 0.5);
    *dlat = nlat + xminlat / 60.0;
}

// deg2grid.f90
std::string deg2grid(double dlong, double dlat) {
    if (dlong < -180.0) dlong += 360.0;
    if (dlong > 180.0) dlong -= 360.0;
    int nlong = (int)(60.0 * (180.0 - dlong) / 5.0);
    int n1 = nlong / 240, n2 = (nlong - 240 * n1) / 24, n3 = nlong - 240 * n1 - 24 * n2;
    std::string g(6, ' ');
    g[0] = (char)('A' + n1);
    g[2] = (char)('0' + n2);
    g[4] = (char)('a' + n3);
    int nlat = (int)(60.0 * (dlat + 90) / 2.5);
    n1 = nlat / 240;
    n2 = (nlat - 240 * n1) / 24;
    n3 = nlat - 240 * n1 - 24 * n2;
    g[1] = (char)('A' + n1);
    g[3] = (char)('0' + n2);
    g[5] = (char)('a' + n3);
    return g;
}

int grid2k(const std::string &grid6) { // packjt.f90 grid2k
    double xlong, xlat;
    grid2deg(grid6, &xlong, &xlat);
    int nlong = nint(xlong), nlat = nint(xlat);
    int k = 0;
    if (nlat >= 85) k = 5 * (nlong + 179) / 2 + nlat - 84;
    return k;
}

// packjt.f90 getpfx2: put the prefix or suffix k0 on a callsign
void getpfx2(int k0, std::string &call) {
    int k = k0;
    if (k > 450) k -= 450;
    if (k >= 1 && k <= kNPfx) {
        call = std::string(kPfx[k - 1]) + "/" + call;
    } else if (k >= 401 && k <= 400 + 12) {
        call = rtrim(call) + "/" + std::string(1, kSfx[k - 401]);
    }
    // k == 449 is the user's own "add-on prefix" setting in WSJT-X; this helper has none, so the call is left as it is
}

// packjt.f90 unpackcall
void unpackcall(int ncall, std::string &word, int &iv2, std::string &psfx) {
    static const char *c = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ ";
    word = "......";
    psfx = "";
    int n = ncall;
    iv2 = 0;
    if (n < 262177560) {
        char w[7] = "......";
        w[5] = c[n % 27 + 10]; n /= 27;
        w[4] = c[n % 27 + 10]; n /= 27;
        w[3] = c[n % 27 + 10]; n /= 27;
        w[2] = c[n % 10]; n /= 10;
        w[1] = c[n % 36]; n /= 36;
        w[0] = c[n];
        std::string s(w, 6);
        size_t i = 0;
        while (i < 4 && s[i] == ' ') i++;
        if (i < 4) word = rtrim(s.substr(i)); else word = rtrim(s); // all four blank: the Fortran leaves the blanks
    } else if (n < 267796946) {
        auto fill4 = [&](int nn) { // four characters of base 37
            std::string p(4, ' ');
            for (int q = 3; q >= 0; q--) { p[q] = c[nn % 37]; nn /= 37; }
            return p;
        };
        auto fill3 = [&](int nn) {
            std::string p(3, ' ');
            for (int q = 2; q >= 0; q--) { p[q] = c[nn % 37]; nn /= 37; }
            return p;
        };
        if (n >= 262178563 && n <= 264002071) { iv2 = 1; psfx = rtrim(fill4(n - 262178563)); }
        else if (n >= 264002072 && n <= 265825580) { iv2 = 2; psfx = rtrim(fill4(n - 264002072)); }
        else if (n >= 265825581 && n <= 267649089) { iv2 = 3; psfx = rtrim(fill4(n - 265825581)); }
        else if (n >= 267649090 && n <= 267698374) { iv2 = 4; psfx = rtrim(fill3(n - 267649090)); }
        else if (n >= 267698375 && n <= 267747659) { iv2 = 5; psfx = rtrim(fill3(n - 267698375)); }
        else if (n >= 267747660 && n <= 267796944) { iv2 = 6; psfx = rtrim(fill3(n - 267747660)); }
        else if (n == 267796945) { iv2 = 7; psfx = ""; }
    }
    if (word.compare(0, 3, "3D0") == 0) word = "3DA0" + word.substr(3);
    if (word.size() >= 2 && word[0] == 'Q' && word[1] >= 'A' && word[1] <= 'Z') word = "3X" + word.substr(1);
}

// packjt.f90 unpackgrid
std::string unpackgrid(int ng) {
    std::string grid = "    ";
    auto i3_2 = [](int n) { // format i3.2
        char b[16];
        snprintf(b, sizeof b, "%3s", "");
        if (n < 0) snprintf(b, sizeof b, "-%02d", -n);
        else snprintf(b, sizeof b, " %02d", n);
        return std::string(b);
    };
    if (ng < 32400) {
        double dlat = (ng % 180) - 90;
        double dlong = (ng / 180) * 2 - 180 + 2;
        std::string g6 = deg2grid(dlong, dlat);
        grid = g6.substr(0, 4);
        if (grid.compare(0, 2, "KA") == 0) {
            int n = atoi(grid.substr(2, 2).c_str()) - 50;
            std::string t = i3_2(n);
            grid = t;
            if (grid[0] == ' ') grid[0] = '+';
        } else if (grid.compare(0, 2, "LA") == 0) {
            int n = atoi(grid.substr(2, 2).c_str()) - 50;
            std::string t = i3_2(n);
            grid = "R" + t;
            if (grid[1] == ' ') grid[1] = '+';
        }
    } else {
        int n = ng - kNGBase - 1;
        if (n >= 1 && n <= 30) grid = i3_2(-n);
        else if (n >= 31 && n <= 60) grid = "R" + i3_2(-(n - 30));
        else if (n == 61) grid = "RO";
        else if (n == 62) grid = "RRR";
        else if (n == 63) grid = "73";
    }
    return rtrim(grid);
}

// packjt.f90 unpacktext
std::string unpacktext(int nc1, int nc2, int nc3a) {
    static const char *c = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?";
    int nc3 = nc3a & 32767;
    if (nc1 & 1) nc3 += 32768;
    nc1 /= 2;
    if (nc2 & 1) nc3 += 65536;
    nc2 /= 2;
    std::string msg(22, ' ');
    for (int i = 5; i >= 1; i--) { msg[i - 1] = c[nc1 % 42]; nc1 /= 42; }
    for (int i = 10; i >= 6; i--) { msg[i - 1] = c[nc2 % 42]; nc2 /= 42; }
    for (int i = 13; i >= 11; i--) { msg[i - 1] = c[nc3 % 42]; nc3 /= 42; }
    return rtrim(msg);
}

// packjt.f90 packtext
void packtext(const std::string &msg, int *nc1, int *nc2, int *nc3) {
    static const char *c = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?";
    auto code = [&](char ch) {
        for (int j = 0; j < 42; j++) if (ch == c[j]) return j;
        return 36;
    };
    int a = 0, b = 0, d = 0;
    for (int i = 0; i < 5; i++) a = 42 * a + code(at(msg, i));
    for (int i = 5; i < 10; i++) b = 42 * b + code(at(msg, i));
    for (int i = 10; i < 13; i++) d = 42 * d + code(at(msg, i));
    a = a + a;
    if (d & 32768) a++;
    b = b + b;
    if (d & 65536) b++;
    d &= 32767;
    *nc1 = a; *nc2 = b; *nc3 = d;
}

// packjt.f90 packcall: returns true if the call must be sent as text
bool packcall(const std::string &callsign_in, int *ncall) {
    if (callsign_in.size() > 6) return true; // (the Fortran silently cuts such a token at 6 characters)
    std::string cs = callsign_in;
    while (cs.size() < 6) cs += ' ';
    if (cs.compare(0, 4, "3DA0") == 0) cs = "3D0" + cs.substr(4, 2) + " ";
    if (cs.compare(0, 2, "3X") == 0 && cs[2] >= 'A' && cs[2] <= 'Z') cs = "Q" + cs.substr(2, 4) + " ";
    if (cs.compare(0, 3, "CQ ") == 0) {
        *ncall = kNBase + 1;
        if (isdigit((unsigned char)cs[3]) && isdigit((unsigned char)cs[4]) && isdigit((unsigned char)cs[5]))
            *ncall = kNBase + 3 + atoi(cs.substr(3, 3).c_str());
        return false;
    } else if (cs.compare(0, 4, "QRZ ") == 0) {
        *ncall = kNBase + 2;
        return false;
    } else if (cs.compare(0, 3, "DE ") == 0) {
        *ncall = 267796945;
        return false;
    }
    std::string tmp = "      ";
    if (cs[2] >= '0' && cs[2] <= '9') {
        tmp = cs;
    } else if (cs[1] >= '0' && cs[1] <= '9') {
        if (cs[5] != ' ') return true;
        tmp = " " + cs.substr(0, 5);
    } else {
        return true;
    }
    for (int i = 0; i < 6; i++)
        if (tmp[i] >= 'a' && tmp[i] <= 'z') tmp[i] = (char)(tmp[i] - 'a' + 'A');
    auto isup = [](char ch) { return ch >= 'A' && ch <= 'Z'; };
    auto isdig = [](char ch) { return ch >= '0' && ch <= '9'; };
    int n1 = 0, n2 = 0, n3 = 0, n4 = 0, n5 = 0, n6 = 0;
    if (isup(tmp[0]) || tmp[0] == ' ') n1 = 1;
    if (isdig(tmp[0])) n1 = 1;
    if (isup(tmp[1]) || isdig(tmp[1])) n2 = 1;
    if (isdig(tmp[2])) n3 = 1;
    if (isup(tmp[3]) || tmp[3] == ' ') n4 = 1;
    if (isup(tmp[4]) || tmp[4] == ' ') n5 = 1;
    if (isup(tmp[5]) || tmp[5] == ' ') n6 = 1;
    if (n1 + n2 + n3 + n4 + n5 + n6 != 6) return true;
    int n = nchar(tmp[0]);
    n = 36 * n + nchar(tmp[1]);
    n = 10 * n + nchar(tmp[2]);
    n = 27 * n + nchar(tmp[3]) - 10;
    n = 27 * n + nchar(tmp[4]) - 10;
    n = 27 * n + nchar(tmp[5]) - 10;
    *ncall = n;
    return false;
}

// Fortran list-directed read of one integer from a short string; false if it is not an integer
bool read_int(const std::string &s, int *n) {
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') i++;
    size_t b = i;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
    size_t d = i;
    while (i < s.size() && isdigit((unsigned char)s[i])) i++;
    if (i == d) return false;
    size_t e = i;
    while (i < s.size() && s[i] == ' ') i++;
    if (i != s.size()) return false;
    *n = atoi(s.substr(b, e - b).c_str());
    return true;
}

// packjt.f90 packgrid: returns true if the field must be sent as text
bool packgrid(std::string grid, int *ng) {
    while (grid.size() < 4) grid += ' ';
    grid = grid.substr(0, 4);
    if (grid == "    ") { *ng = kNGBase + 1; return false; }
    int n;
    bool skip = false;
    if (grid[0] == '-') {
        if (!read_int(grid.substr(1, 2), &n)) return true;
        if (n >= 1 && n <= 30) { *ng = kNGBase + 1 + n; return false; }
    } else if (grid.compare(0, 2, "R-") == 0) {
        if (!read_int(grid.substr(2, 2), &n)) return true;
        if (n >= 1 && n <= 30) { *ng = kNGBase + 31 + n; return false; }
    } else if (grid == "RO  ") { *ng = kNGBase + 62; return false; }
    else if (grid == "RRR ") { *ng = kNGBase + 63; return false; }
    else if (grid == "73  ") { *ng = kNGBase + 64; return false; }
    (void)skip;
    // extended-range reports: -50 to -31, and 0 to +49
    n = 99;
    char c1 = grid[0];
    if (!read_int(grid, &n)) {
        n = 99;
        read_int(grid.substr(1, 3), &n);
    }
    if (n >= -50 && n <= 49) {
        char b[8];
        snprintf(b, sizeof b, "%c%c%02d", c1 == 'R' ? 'L' : 'K', 'A', n + 50);
        grid = b;
    } else {
        bool text = false;
        if (grid[0] < 'A' || grid[0] > 'R') text = true;
        if (grid[1] < 'A' || grid[1] > 'R') text = true;
        if (grid[2] < '0' || grid[2] > '9') text = true;
        if (grid[3] < '0' || grid[3] > '9') text = true;
        if (text) return true;
    }
    double dlong, dlat;
    grid2deg(grid + "mm", &dlong, &dlat);
    int lng = (int)dlong;
    int lat = (int)(dlat + 90.0);
    *ng = ((lng + 180) / 2) * 180 + lat;
    return false;
}

// fmtmsg.f90: upper case, collapse multiple blanks (the message is then blank padded to 22 characters)
std::string fmtmsg(const std::string &in) {
    std::string m;
    for (char ch : in) m += (ch >= 'a' && ch <= 'z') ? (char)(ch - 'a' + 'A') : ch;
    m = rtrim(m);
    std::string out;
    for (size_t i = 0; i < m.size(); i++) {
        if (m[i] == ' ' && !out.empty() && out.back() == ' ') continue;
        out += m[i];
    }
    return out;
}

} // namespace

// packjt.f90 unpackmsg
std::string unpackmsg(const int *dat) {
    int nc1 = (dat[0] << 22) + (dat[1] << 16) + (dat[2] << 10) + (dat[3] << 4) + ((dat[4] >> 2) & 15);
    int nc2 = ((dat[4] & 3) << 26) + (dat[5] << 20) + (dat[6] << 14) + (dat[7] << 8) + (dat[8] << 2) + ((dat[9] >> 4) & 3);
    int ng = ((dat[9] & 15) << 12) + (dat[10] << 6) + dat[11];
    std::string msg;
    if (ng >= 32768) {
        msg = unpacktext(nc1, nc2, ng);
    } else {
        std::string c1, c2, psfx, junk;
        int iv2 = 0, junk1 = 0;
        bool cqnnn = false;
        unpackcall(nc1, c1, iv2, psfx);
        if (iv2 == 0) {
            if (nc1 == kNBase + 1) c1 = "CQ";
            if (nc1 == kNBase + 2) c1 = "QRZ";
            int nfreq = nc1 - kNBase - 3;
            if (nfreq >= 0 && nfreq <= 999) {
                char b[16];
                snprintf(b, sizeof b, "CQ %03d", nfreq);
                c1 = b;
                cqnnn = true;
            }
        }
        unpackcall(nc2, c2, junk1, junk);
        std::string grid = unpackgrid(ng);
        if (iv2 > 0) {
            // JT65v2 style messages: CQ / QRZ / DE with an add-on prefix or suffix
            std::string g = grid;
            if (iv2 == 1) msg = "CQ " + psfx + "/" + c2 + " " + g;
            if (iv2 == 2) msg = "QRZ " + psfx + "/" + c2 + " " + g;
            if (iv2 == 3) msg = "DE " + psfx + "/" + c2 + " " + g;
            if (iv2 == 4) msg = "CQ " + c2 + "/" + psfx + " " + g;
            if (iv2 == 5) msg = "QRZ " + c2 + "/" + psfx + " " + g;
            if (iv2 == 6) msg = "DE " + c2 + "/" + psfx + " " + g;
            if (iv2 == 7) {
                int k = grid2k(grid + "ma");
                // grid is padded by the callee below; grid6 = grid//'ma' needs the 4-character field
                std::string g4 = grid;
                while (g4.size() < 4) g4 += ' ';
                k = grid2k(g4 + "ma");
                if (k >= 451 && k <= 900) {
                    getpfx2(k, c2);
                    msg = "DE " + c2;
                } else {
                    msg = "DE " + c2 + " " + grid;
                }
            }
        } else {
            std::string g4 = grid;
            while (g4.size() < 4) g4 += ' ';
            int k = grid2k(g4 + "ma");
            if (k >= 1 && k <= 450) getpfx2(k, c1);
            if (k >= 451 && k <= 900) getpfx2(k, c2);
            msg = c1 + " " + c2;
            if (k == 0) msg += " " + grid;
        }
    }
    msg = rtrim(msg);
    if (msg.size() > 22) msg.resize(22);
    // end of unpackmsg: undo the packing tricks for CQ DX, CQ xx and CQ nnn
    if (msg.compare(0, 6, "CQ9DX ") == 0) msg[2] = ' ';
    if (msg.compare(0, 2, "E9") == 0 && at(msg, 2) >= 'A' && at(msg, 2) <= 'Z' && at(msg, 3) >= 'A' && at(msg, 3) <= 'Z' &&
        at(msg, 4) == ' ')
        msg = "CQ " + msg.substr(2);
    if (msg.compare(0, 5, "CQ 00") == 0 && at(msg, 5) >= '0' && at(msg, 5) <= '9') msg = "CQ " + msg.substr(5);
    return rtrim(msg);
}

// packjt.f90 packmsg (standard messages and free text)
int packmsg(const std::string &msg0, int *dat) {
    std::string msg = fmtmsg(msg0);
    while (!msg.empty() && msg[0] == ' ') msg.erase(0, 1); // gen9.f90 strips leading blanks
    if (msg.size() > 22) msg.resize(22);
    // (fmtmsg works on the blank-padded 22-character message)
    if (msg.compare(0, 3, "CQ ") == 0 && at(msg, 3) >= '0' && at(msg, 3) <= '9' && at(msg, 4) == ' ') msg = "CQ 00" + msg.substr(3);
    if (msg.compare(0, 6, "CQ DX ") == 0) msg[2] = '9';
    if (msg.compare(0, 3, "CQ ") == 0 && at(msg, 3) >= 'A' && at(msg, 3) <= 'Z' && at(msg, 4) >= 'A' && at(msg, 4) <= 'Z' &&
        at(msg, 5) == ' ')
        msg = "E9" + msg.substr(3);
    if (msg.size() > 22) msg.resize(22);
    msg.resize(22, ' '); // the Fortran message is always 22 characters, blank padded

    int itype = 1;
    int nc1 = 0, nc2 = 0, ng = 0;
    bool as_text = false;
    std::string c1, c2, c3;
    int ia = 0;
    if (msg.compare(0, 3, "CQ ") == 0) {
        ia = 3;
        if (at(msg, 3) >= '0' && at(msg, 3) <= '9' && at(msg, 4) >= '0' && at(msg, 4) <= '9' && at(msg, 5) >= '0' && at(msg, 5) <= '9')
            ia = 7;
    } else {
        ia = 0;
        for (int i = 1; i <= 22; i++) if (at(msg, i - 1) == ' ') { ia = i; break; }
        if (ia == 0) as_text = true;
    }
    int ib = 0, ic = 0;
    if (!as_text) {
        c1 = msg.substr(0, ia - 1);
        for (int i = ia + 1; i <= 22; i++) if (at(msg, i - 1) == ' ') { ib = i; break; }
        if (ib == 0) as_text = true;
    }
    if (!as_text) {
        c2 = msg.substr(ia, ib - 1 - ia);
        for (int i = ib + 1; i <= 22; i++) if (at(msg, i - 1) == ' ') { ic = i; break; }
        if (ic == 0) as_text = true;
    }
    if (!as_text) {
        c3 = ic >= ib + 1 ? msg.substr(ib, ic - ib) : std::string();
        if (c3.size() > 4) c3.resize(4);
        if (c3 == "OOO ") c3 = "    ";
        if (c1.find('/') != std::string::npos || c2.find('/') != std::string::npos) return -1; // JT65v2 prefix/suffix
        bool text1 = packcall(c1, &nc1);
        bool text2 = text1 ? true : packcall(c2, &nc2);
        bool text3 = (text1 || text2) ? true : packgrid(c3, &ng);
        if (text1 || text2 || text3) as_text = true;
    }
    if (as_text) {
        itype = 6;
        std::string padded = msg;
        packtext(padded, &nc1, &nc2, &ng);
        ng += 32768;
    }
    dat[0] = (nc1 >> 22) & 63;
    dat[1] = (nc1 >> 16) & 63;
    dat[2] = (nc1 >> 10) & 63;
    dat[3] = (nc1 >> 4) & 63;
    dat[4] = 4 * (nc1 & 15) + ((nc2 >> 26) & 3);
    dat[5] = (nc2 >> 20) & 63;
    dat[6] = (nc2 >> 14) & 63;
    dat[7] = (nc2 >> 8) & 63;
    dat[8] = (nc2 >> 2) & 63;
    dat[9] = 16 * (nc2 & 3) + ((ng >> 12) & 15);
    dat[10] = (ng >> 6) & 63;
    dat[11] = ng & 63;
    return itype;
}

// interleave9.f90
void interleave9(const int8_t *ia, int ndir, int8_t *ib) {
    static int j0[206];
    static bool first = true;
    if (first) {
        int k = -1;
        for (int i = 0; i <= 255; i++) {
            int m = i, n = m & 1;
            n = 2 * n + ((m / 2) & 1);
            n = 2 * n + ((m / 4) & 1);
            n = 2 * n + ((m / 8) & 1);
            n = 2 * n + ((m / 16) & 1);
            n = 2 * n + ((m / 32) & 1);
            n = 2 * n + ((m / 64) & 1);
            n = 2 * n + ((m / 128) & 1);
            if (n <= 205) j0[++k] = n;
        }
        first = false;
    }
    if (ndir > 0) {
        for (int i = 0; i <= 205; i++) ib[j0[i]] = ia[i];
    } else {
        for (int i = 0; i <= 205; i++) ib[i] = ia[j0[i]];
    }
}

// entail.f90 + encode232.f90 (nsym = 206)
void encode_words(const int *dgen, int8_t *symbol) {
    // entail: 72 bits from twelve 6-bit words into 9 bytes, then a zero tail (13 bytes in all)
    int dat[13];
    int i4 = 0, k = 0, m = 0;
    for (int i = 0; i < 12; i++) {
        int n = dgen[i];
        for (int j = 1; j <= 6; j++) {
            k++;
            i4 = i4 + i4 + (1 & (n >> (6 - j)));
            i4 &= 255;
            if (k == 8) { dat[m++] = i4; k = 0; }
        }
    }
    for (; m < 13; m++) dat[m] = 0;
    // encode232
    uint32_t nstate = 0;
    int kk = 0;
    for (int j = 0; j < 13 && kk < 206; j++) {
        for (int i = 7; i >= 0; i--) {
            nstate = (nstate << 1) | (uint32_t)((dat[j] >> i) & 1);
            uint32_t n = nstate & (uint32_t)kNpoly1;
            n ^= n >> 16;
            symbol[kk++] = (int8_t)parity8((int)((n ^ (n >> 8)) & 255));
            n = nstate & (uint32_t)kNpoly2;
            n ^= n >> 16;
            symbol[kk++] = (int8_t)parity8((int)((n ^ (n >> 8)) & 255));
            if (kk >= 206) break;
        }
    }
}

// gen9.f90
int gen9(const std::string &msg, int *tone, std::string *msgsent) {
    int words[12];
    int itype = packmsg(msg, words);
    if (itype < 0) return itype;
    if (msgsent) *msgsent = unpackmsg(words);
    int8_t enc[207], scr[207];
    encode_words(words, enc);
    enc[206] = 0;
    interleave9(enc, 1, scr);
    scr[206] = 0;
    int j = 0;
    for (int i = 0; i < 85; i++) {
        if (kIsync[i] == 1) { tone[i] = 0; continue; }
        // packbits(...,69,3) then graycode(idir=+1): igray(n,1) = n ^ (n>>1)
        int n = (scr[3 * j] << 2) | (scr[3 * j + 1] << 1) | scr[3 * j + 2];
        tone[i] = (n ^ (n >> 1)) + 1;
        j++;
    }
    return itype;
}

} // namespace jt9
} // namespace wm
