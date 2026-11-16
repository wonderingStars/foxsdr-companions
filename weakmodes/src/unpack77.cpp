// weakmodes: unpack of the 77-bit WSJT-X message format.
//
// C++ port of the receive side of lib/77bit/packjt77.f90 from WSJT-X (b4f9a43): subroutines unpack77, unpack28,
// unpacktext77 (with mp_short_div), to_grid4, to_grid6, to_grid, hash10, hash12, hash22, ihashcall,
// save_hash_call.
//
// WSJT-X, Copyright (C) 2001-2025 by Joe Taylor, K1JT, and the WSJT Development Group.
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU General
// Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. It is distributed WITHOUT ANY WARRANTY. See LICENSE and UPSTREAM.md.
//
// Changes: Fortran character handling replaced by std::string (trailing blanks trimmed); the my-call / DX-call
// substitutions (mycall13, dxcall13, nrx) are left out because the helper has no station call; the
// recent-calls list (add_call_to_recent_calls) is left out because it only feeds the WSJT-X GUI.
#include "wm_common.h"

#include <cstring>

namespace wm {

namespace {

const int NTOKENS = 2063592;
const int MAX22 = 4194304;
const int MAXGRID4 = 32400;
const int MAXHASH = 1000;

// hash tables of calls seen in this run (packjt77: calls10, calls12, calls22, ihash22)
struct HashState {
    std::string calls10[1024];
    std::string calls12[4096];
    std::string calls22[MAXHASH + 1];
    int ihash22[MAXHASH + 1];
    int nzhash = 0;
    HashState() { for (int i = 0; i <= MAXHASH; i++) ihash22[i] = -1; }
};
HashState g_hash;

std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && s[a] == ' ') a++;
    while (b > a && s[b - 1] == ' ') b--;
    return s.substr(a, b - a);
}

// ihashcall(c0, m): 38-based number of the first 11 characters times a constant, top m bits
int ihashcall(const std::string &c0, int m) {
    static const char c[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/";
    uint64_t n8 = 0;
    for (int i = 0; i < 11; i++) {
        char ch = i < (int)c0.size() ? c0[i] : ' ';
        const char *p = ch ? strchr(c, ch) : nullptr;
        int64_t j = p ? (int64_t)(p - c) : -1; // Fortran index(c,ch)-1: -1 if not found
        n8 = 38 * n8 + (uint64_t)j;
    }
    uint64_t prod = 47055833459ull * n8;
    return (int)(prod >> (64 - m));
}

void save_hash_call(const std::string &c13) {
    std::string cw = c13;
    if (cw.empty() || cw[0] == ' ' || cw.compare(0, 5, "<...>") == 0) return;
    if (cw[0] == '<') cw = cw.substr(1);
    size_t i = cw.find('>');
    if (i != std::string::npos) cw = cw.substr(0, i);
    cw = trim(cw);
    if (cw.size() < 3) return;
    int n10 = ihashcall(cw, 10);
    if (n10 >= 0 && n10 <= 1023) g_hash.calls10[n10] = cw;
    int n12 = ihashcall(cw, 12);
    if (n12 >= 0 && n12 <= 4095) g_hash.calls12[n12] = cw;
    int n22 = ihashcall(cw, 22);
    for (int k = 1; k <= g_hash.nzhash; k++) {
        if (g_hash.ihash22[k] == n22) { // entry exists: keep the most recent spelling
            g_hash.calls22[k] = cw;
            return;
        }
    }
    for (int k = MAXHASH; k >= 2; k--) { // new entry on top, table moves down
        g_hash.ihash22[k] = g_hash.ihash22[k - 1];
        g_hash.calls22[k] = g_hash.calls22[k - 1];
    }
    g_hash.ihash22[1] = n22;
    g_hash.calls22[1] = cw;
    if (g_hash.nzhash < MAXHASH) g_hash.nzhash++;
}

std::string hash10(int n10) {
    if (n10 < 0 || n10 > 1023 || g_hash.calls10[n10].empty()) return "<...>";
    return "<" + g_hash.calls10[n10] + ">";
}
std::string hash12(int n12) {
    if (n12 < 0 || n12 > 4095 || g_hash.calls12[n12].empty()) return "<...>";
    return "<" + g_hash.calls12[n12] + ">";
}
std::string hash22(int n22) {
    for (int i = 1; i <= g_hash.nzhash; i++)
        if (g_hash.ihash22[i] == n22) return "<" + g_hash.calls22[i] + ">";
    return "<...>";
}

// bits[a..a+n) as an unsigned number (a is 0-based)
uint64_t getbits(const uint8_t *b, int a, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v = (v << 1) | (b[a + i] & 1);
    return v;
}

// unpack28: returns the call text (blanks trimmed); false if the Fortran routine would flag a failure
bool unpack28(int n28, std::string &c13) {
    static const char *c1 = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const char *c2 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const char *c3 = "0123456789";
    static const char *c4 = " ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    bool special = false;
    if (n28 < NTOKENS) {
        if (n28 == 0) { c13 = "DE"; special = true; }
        else if (n28 == 1) { c13 = "QRZ"; special = true; }
        else if (n28 == 2) { c13 = "CQ"; special = true; }
        else if (n28 <= 1002) { char b[16]; snprintf(b, sizeof b, "CQ_%03d", n28 - 3); c13 = b; special = true; }
        else if (n28 <= 532443) {
            int n = n28 - 1003;
            int i1 = n / (27 * 27 * 27);
            n -= 27 * 27 * 27 * i1;
            int i2 = n / (27 * 27);
            n -= 27 * 27 * i2;
            int i3 = n / 27;
            int i4 = n - 27 * i3;
            std::string t;
            t += c4[i1]; t += c4[i2]; t += c4[i3]; t += c4[i4];
            // c13=adjustl(c13); c13='CQ_'//c13(1:10)
            size_t a = t.find_first_not_of(' ');
            t = a == std::string::npos ? std::string() : t.substr(a);
            c13 = "CQ_" + t;
            special = true;
        }
    }
    if (!special) {
        n28 -= NTOKENS;
        if (n28 < MAX22) {
            c13 = hash22(n28); // 22-bit hash of a callsign
        } else {
            int n = n28 - MAX22;
            int i1 = n / (36 * 10 * 27 * 27 * 27);
            n -= 36 * 10 * 27 * 27 * 27 * i1;
            int i2 = n / (10 * 27 * 27 * 27);
            n -= 10 * 27 * 27 * 27 * i2;
            int i3 = n / (27 * 27 * 27);
            n -= 27 * 27 * 27 * i3;
            int i4 = n / (27 * 27);
            n -= 27 * 27 * i4;
            int i5 = n / 27;
            int i6 = n - 27 * i5;
            if (i1 > 36) return false;
            std::string t;
            t += c1[i1]; t += c2[i2]; t += c3[i3]; t += c4[i4]; t += c4[i5]; t += c4[i6];
            c13 = trim(t);
        }
    }
    // an embedded blank (text before the first blank shorter than the trimmed text) marks a bad call
    c13 = trim(c13);
    if (c13.find(' ') != std::string::npos) { c13 = "QU1RK"; return false; }
    return true;
}

bool to_grid4(int n, char *g) {
    int j1 = n / (18 * 10 * 10);
    if (j1 < 0 || j1 > 17) return false;
    n -= j1 * 18 * 10 * 10;
    int j2 = n / (10 * 10);
    if (j2 < 0 || j2 > 17) return false;
    n -= j2 * 10 * 10;
    int j3 = n / 10;
    if (j3 < 0 || j3 > 9) return false;
    int j4 = n - j3 * 10;
    if (j4 < 0 || j4 > 9) return false;
    g[0] = (char)('A' + j1); g[1] = (char)('A' + j2); g[2] = (char)('0' + j3); g[3] = (char)('0' + j4); g[4] = 0;
    return true;
}

bool to_grid6(int n, char *g) {
    int j1 = n / (18 * 10 * 10 * 24 * 24);
    if (j1 < 0 || j1 > 17) return false;
    n -= j1 * 18 * 10 * 10 * 24 * 24;
    int j2 = n / (10 * 10 * 24 * 24);
    if (j2 < 0 || j2 > 17) return false;
    n -= j2 * 10 * 10 * 24 * 24;
    int j3 = n / (10 * 24 * 24);
    if (j3 < 0 || j3 > 9) return false;
    n -= j3 * 10 * 24 * 24;
    int j4 = n / (24 * 24);
    if (j4 < 0 || j4 > 9) return false;
    n -= j4 * 24 * 24;
    int j5 = n / 24;
    if (j5 < 0 || j5 > 23) return false;
    int j6 = n - j5 * 24;
    if (j6 < 0 || j6 > 23) return false;
    g[0] = (char)('A' + j1); g[1] = (char)('A' + j2); g[2] = (char)('0' + j3); g[3] = (char)('0' + j4);
    g[4] = (char)('A' + j5); g[5] = (char)('A' + j6); g[6] = 0;
    return true;
}

bool to_grid(int n, char *g) { // 4- or 6-character grid (WSPR type 3)
    int j1 = n / (18 * 10 * 10 * 25 * 25);
    if (j1 < 0 || j1 > 17) return false;
    n -= j1 * 18 * 10 * 10 * 25 * 25;
    int j2 = n / (10 * 10 * 25 * 25);
    if (j2 < 0 || j2 > 17) return false;
    n -= j2 * 10 * 10 * 25 * 25;
    int j3 = n / (10 * 25 * 25);
    if (j3 < 0 || j3 > 9) return false;
    n -= j3 * 10 * 25 * 25;
    int j4 = n / (25 * 25);
    if (j4 < 0 || j4 > 9) return false;
    n -= j4 * 25 * 25;
    int j5 = n / 25;
    if (j5 < 0 || j5 > 24) return false;
    int j6 = n - j5 * 25;
    if (j6 < 0 || j6 > 24) return false;
    g[0] = (char)('A' + j1); g[1] = (char)('A' + j2); g[2] = (char)('0' + j3); g[3] = (char)('0' + j4); g[4] = 0;
    if (j5 != 24 || j6 != 24) { g[4] = (char)('A' + j5); g[5] = (char)('A' + j6); g[6] = 0; }
    return true;
}

// unpacktext77 + mp_short_div: 71 bits (7 + 8*8) divided by 42 thirteen times
std::string unpacktext77(const uint8_t *b) {
    static const char *c = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?";
    int qa[9];
    qa[0] = (int)getbits(b, 0, 7);
    for (int i = 1; i < 9; i++) qa[i] = (int)getbits(b, 7 + 8 * (i - 1), 8);
    char out[14];
    for (int i = 12; i >= 0; i--) {
        int ir = 0, qb[9];
        for (int j = 0; j < 9; j++) {
            int v = 256 * ir + qa[j];
            qb[j] = v / 42;
            ir = v % 42;
        }
        out[i] = c[ir];
        for (int j = 0; j < 9; j++) qa[j] = qb[j];
    }
    out[13] = 0;
    return std::string(out);
}

std::string rpt3(int v, bool plus) { // Fortran i3.2: at least two digits, width 3; blank lead becomes '+' when asked
    char b[16];
    snprintf(b, sizeof b, "%3.2d", v);
    if (b[0] == ' ' && plus) b[0] = '+';
    return b;
}

const char *csec[86] = {
    "AB", "AK", "AL", "AR", "AZ", "BC", "CO", "CT", "DE", "EB", "EMA", "ENY", "EPA", "EWA", "GA", "GH", "IA", "ID", "IL", "IN",
    "KS", "KY", "LA", "LAX", "NS", "MB", "MDC", "ME", "MI", "MN", "MO", "MS", "MT", "NC", "ND", "NE", "NFL", "NH", "NL", "NLI",
    "NM", "NNJ", "NNY", "TER", "NTX", "NV", "OH", "OK", "ONE", "ONN", "ONS", "OR", "ORG", "PAC", "PR", "QC", "RI", "SB", "SC", "SCV",
    "SD", "SDG", "SF", "SFL", "SJV", "SK", "SNJ", "STX", "SV", "TN", "UT", "VA", "VI", "VT", "WCF", "WI", "WMA", "WNY", "WPA", "WTX",
    "WV", "WWA", "WY", "DX", "PE", "NB"};

const char *cmult[171] = {
    "AL", "AK", "AZ", "AR", "CA", "CO", "CT", "DE", "FL", "GA", "HI", "ID", "IL", "IN", "IA", "KS", "KY", "LA", "ME", "MD",
    "MA", "MI", "MN", "MS", "MO", "MT", "NE", "NV", "NH", "NJ", "NM", "NY", "NC", "ND", "OH", "OK", "OR", "PA", "RI", "SC",
    "SD", "TN", "TX", "UT", "VT", "VA", "WA", "WV", "WI", "WY", "NB", "NS", "QC", "ON", "MB", "SK", "AB", "BC", "NWT", "NF",
    "LB", "NU", "YT", "PEI", "DC", "DR", "FR", "GD", "GR", "OV", "ZH", "ZL", "X01", "X02", "X03", "X04", "X05", "X06", "X07", "X08",
    "X09", "X10", "X11", "X12", "X13", "X14", "X15", "X16", "X17", "X18", "X19", "X20", "X21", "X22", "X23", "X24", "X25", "X26", "X27", "X28",
    "X29", "X30", "X31", "X32", "X33", "X34", "X35", "X36", "X37", "X38", "X39", "X40", "X41", "X42", "X43", "X44", "X45", "X46", "X47", "X48",
    "X49", "X50", "X51", "X52", "X53", "X54", "X55", "X56", "X57", "X58", "X59", "X60", "X61", "X62", "X63", "X64", "X65", "X66", "X67", "X68",
    "X69", "X70", "X71", "X72", "X73", "X74", "X75", "X76", "X77", "X78", "X79", "X80", "X81", "X82", "X83", "X84", "X85", "X86", "X87", "X88",
    "X89", "X90", "X91", "X92", "X93", "X94", "X95", "X96", "X97", "X98", "X99"};

} // namespace

void unpack77_reset() { g_hash = HashState(); }

std::string unpack77(const uint8_t *bits, bool *okp) {
    bool ok = true;
    std::string msg;
    int n3 = (int)getbits(bits, 71, 3);
    int i3 = (int)getbits(bits, 74, 3);
    std::string call_1, call_2, call_3;

    if (i3 == 0 && n3 == 0) { // 0.0 free text
        // adjustl of the 13 characters: leading blanks removed; all blank -> failure
        std::string t = unpacktext77(bits);
        size_t a = t.find_first_not_of(' ');
        msg = a == std::string::npos ? std::string() : trim(t.substr(a));
        if (msg.empty()) ok = false;
    } else if (i3 == 0 && n3 == 1) { // 0.1 DXpedition mode
        int n28a = (int)getbits(bits, 0, 28), n28b = (int)getbits(bits, 28, 28), n10 = (int)getbits(bits, 56, 10),
            n5 = (int)getbits(bits, 66, 5);
        int irpt = 2 * n5 - 30;
        std::string crpt = rpt3(irpt, irpt >= 0);
        if (irpt >= 0) crpt[0] = '+';
        if (!unpack28(n28a, call_1) || n28a <= 2) ok = false;
        if (!unpack28(n28b, call_2) || n28b <= 2) ok = false;
        call_3 = hash10(n10);
        msg = call_1 + " RR73; " + call_2 + " " + call_3 + " " + crpt;
    } else if (i3 == 0 && n3 == 2) {
        ok = false;
    } else if (i3 == 0 && (n3 == 3 || n3 == 4)) { // ARRL Field Day
        int n28a = (int)getbits(bits, 0, 28), n28b = (int)getbits(bits, 28, 28), ir = (int)getbits(bits, 56, 1),
            intx = (int)getbits(bits, 57, 4), nclass = (int)getbits(bits, 61, 3), isec = (int)getbits(bits, 64, 7);
        if (isec > 86 || isec < 1) { ok = false; isec = 1; }
        if (!unpack28(n28a, call_1) || n28a <= 2) ok = false;
        if (!unpack28(n28b, call_2) || n28b <= 2) ok = false;
        int ntx = intx + 1;
        if (n3 == 4) ntx += 16;
        char cntx[8];
        snprintf(cntx, sizeof cntx, "%2d%c", ntx, 'A' + nclass);
        std::string sec = csec[isec - 1];
        if (ir == 0 && ntx < 10) msg = call_1 + " " + call_2 + cntx + " " + sec;
        if (ir == 1 && ntx < 10) msg = call_1 + " " + call_2 + " R" + cntx + " " + sec;
        if (ir == 0 && ntx >= 10) msg = call_1 + " " + call_2 + " " + cntx + " " + sec;
        if (ir == 1 && ntx >= 10) msg = call_1 + " " + call_2 + " R " + cntx + " " + sec;
    } else if (i3 == 0 && n3 == 5) { // telemetry, 18 hex digits
        uint64_t a = getbits(bits, 0, 23), b = getbits(bits, 23, 24), c = getbits(bits, 47, 24);
        char buf[32];
        snprintf(buf, sizeof buf, "%06llX%06llX%06llX", (unsigned long long)a, (unsigned long long)b, (unsigned long long)c);
        std::string t = buf;
        size_t k = 0;
        while (k < 18 && t[k] == '0') k++;
        msg = t.substr(k);
        if (msg.empty()) msg = ""; // all zero: Fortran leaves blanks
    } else if (i3 == 0 && n3 == 6) { // WSPR in the 77-bit format
        int j48 = bits[47], j49 = bits[48], j50 = bits[49];
        int itype;
        if (j50 == 1) itype = 2;
        else if (j49 == 0) itype = 1;
        else if (j48 == 0) itype = 3;
        else { itype = -1; ok = false; }
        if (itype == 1) {
            int n28 = (int)getbits(bits, 0, 28), igrid4 = (int)getbits(bits, 28, 15), idbm = (int)getbits(bits, 43, 5);
            idbm = (int)std::lrint(idbm * 10.0 / 3.0);
            if (idbm < 0 || idbm > 60) ok = false;
            if (!unpack28(n28, call_1)) ok = false;
            char g[8] = "";
            if (!to_grid4(igrid4, g)) ok = false;
            char crpt[16];
            snprintf(crpt, sizeof crpt, "%d", idbm);
            msg = call_1 + " " + g + " " + crpt;
            if (ok) save_hash_call(call_1);
        } else if (itype == 2) {
            int n28 = (int)getbits(bits, 0, 28), npfx = (int)getbits(bits, 28, 16), idbm = (int)getbits(bits, 44, 5);
            idbm = (int)std::lrint(idbm * 10.0 / 3.0);
            if (idbm < 0 || idbm > 60) ok = false;
            if (!unpack28(n28, call_1)) ok = false;
            char crpt[16];
            snprintf(crpt, sizeof crpt, "%d", idbm);
            static const char *a2 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            char cpfx[4] = {' ', ' ', ' ', 0};
            if (npfx < 46656) {
                for (int i = 2; i >= 0; i--) {
                    cpfx[i] = a2[npfx % 36];
                    npfx /= 36;
                    if (npfx == 0) break;
                }
                std::string p = trim(cpfx);
                msg = p + "/" + call_1 + " " + crpt;
                save_hash_call(p + "/" + call_1);
            } else {
                npfx -= 46656;
                if (npfx <= 35) cpfx[0] = a2[npfx];
                else if (npfx <= 1295) { cpfx[0] = a2[npfx / 36]; cpfx[1] = a2[npfx % 36]; }
                else if (npfx <= 12959) { cpfx[0] = a2[npfx / 360]; cpfx[1] = a2[(npfx / 10) % 36]; cpfx[2] = a2[npfx % 10]; }
                else { *okp = false; return ""; }
                std::string p = trim(cpfx);
                msg = call_1 + "/" + p + " " + crpt;
                save_hash_call(call_1 + "/" + p);
            }
        } else if (itype == 3) {
            int n22 = (int)getbits(bits, 0, 22);
            int igrid6 = (int)getbits(bits, 22, 25);
            if (!unpack28(n22 + 2063592, call_1)) ok = false;
            char g[8] = "";
            if (!to_grid(igrid6, g)) ok = false;
            msg = call_1 + " " + g;
        }
    } else if (i3 == 0 && n3 > 6) {
        ok = false;
    } else if (i3 == 1 || i3 == 2) { // standard message (type 2 is the /P form)
        int n28a = (int)getbits(bits, 0, 28), ipa = (int)getbits(bits, 28, 1), n28b = (int)getbits(bits, 29, 28),
            ipb = (int)getbits(bits, 57, 1), ir = (int)getbits(bits, 58, 1), igrid4 = (int)getbits(bits, 59, 15);
        if (!unpack28(n28a, call_1)) ok = false;
        if (!unpack28(n28b, call_2)) ok = false;
        if (call_1.compare(0, 3, "CQ_") == 0) call_1[2] = ' ';
        if (call_1.find('<') == std::string::npos) {
            size_t i = call_1.find(' ') == std::string::npos ? call_1.size() : call_1.find(' '); // 0-based index of the first blank
            if (i >= 3 && ipa == 1 && i3 == 1) call_1 += "/R";
            if (i >= 3 && ipa == 1 && i3 == 2) call_1 += "/P";
        }
        if (call_2.find('<') == std::string::npos) {
            size_t i = call_2.find(' ') == std::string::npos ? call_2.size() : call_2.find(' ');
            if (i >= 3 && ipb == 1 && i3 == 1) call_2 += "/R";
            if (i >= 3 && ipb == 1 && i3 == 2) call_2 += "/P";
            if (i >= 3) save_hash_call(call_2); // only the "from" call is hashed
        }
        if (igrid4 <= MAXGRID4) {
            char g[8] = "";
            if (!to_grid4(igrid4, g)) ok = false;
            if (ir == 0) msg = call_1 + " " + call_2 + " " + g;
            if (ir == 1) msg = call_1 + " " + call_2 + " R " + g;
            if (msg.compare(0, 3, "CQ ") == 0 && ir == 1) ok = false;
        } else {
            int irpt = igrid4 - MAXGRID4;
            if (irpt == 1) msg = call_1 + " " + call_2;
            if (irpt == 2) msg = call_1 + " " + call_2 + " RRR";
            if (irpt == 3) msg = call_1 + " " + call_2 + " RR73";
            if (irpt == 4) msg = call_1 + " " + call_2 + " 73";
            if (irpt >= 5) {
                int isnr = irpt - 35;
                if (isnr > 50) isnr -= 101;
                std::string crpt = rpt3(isnr, true);
                if (ir == 0) msg = call_1 + " " + call_2 + " " + crpt;
                if (ir == 1) msg = call_1 + " " + call_2 + " R" + crpt;
            }
            if (msg.compare(0, 3, "CQ ") == 0 && irpt >= 2) ok = false;
        }
    } else if (i3 == 3) { // ARRL RTTY contest
        int itu = (int)getbits(bits, 0, 1), n28a = (int)getbits(bits, 1, 28), n28b = (int)getbits(bits, 29, 28),
            ir = (int)getbits(bits, 57, 1), irpt = (int)getbits(bits, 58, 3), nexch = (int)getbits(bits, 61, 13);
        char crpt[8];
        snprintf(crpt, sizeof crpt, "5%d9", irpt + 2);
        if (!unpack28(n28a, call_1)) ok = false;
        if (!unpack28(n28b, call_2)) ok = false;
        int imult = 0, nserial = 0;
        if (nexch > 8000) imult = nexch - 8000;
        if (nexch < 8000) nserial = nexch;
        std::string pre = itu ? "TU; " : "";
        std::string rr = ir ? " R " : " ";
        if (imult >= 1 && imult <= 171) {
            msg = pre + call_1 + " " + call_2 + rr + crpt + " " + cmult[imult - 1];
        } else if (nserial >= 1 && nserial <= 7999) {
            char cs[8];
            snprintf(cs, sizeof cs, "%04d", nserial);
            msg = pre + call_1 + " " + call_2 + rr + crpt + " " + cs;
        }
    } else if (i3 == 4) { // non-standard call with 12-bit hash of the other call
        static const char *c = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/";
        int n12 = (int)getbits(bits, 0, 12);
        uint64_t n58 = getbits(bits, 12, 58);
        int iflip = (int)getbits(bits, 70, 1), nrpt = (int)getbits(bits, 71, 2), icq = (int)getbits(bits, 73, 1);
        char c11[12];
        for (int i = 10; i >= 0; i--) {
            c11[i] = c[n58 % 38];
            n58 /= 38;
        }
        c11[11] = 0;
        std::string nonstd = trim(c11);
        call_3 = hash12(n12);
        if (iflip == 0) {
            call_1 = call_3;
            call_2 = nonstd;
            save_hash_call(call_2);
        } else {
            call_1 = nonstd;
            call_2 = call_3;
        }
        if (icq == 0) {
            if (nrpt == 0) msg = call_1 + " " + call_2;
            if (nrpt == 1) msg = call_1 + " " + call_2 + " RRR";
            if (nrpt == 2) msg = call_1 + " " + call_2 + " RR73";
            if (nrpt == 3) msg = call_1 + " " + call_2 + " 73";
        } else {
            msg = "CQ " + call_2;
        }
    } else if (i3 == 5) { // EU VHF contest
        int n12 = (int)getbits(bits, 0, 12), n22 = (int)getbits(bits, 12, 22), ir = (int)getbits(bits, 34, 1),
            irpt = (int)getbits(bits, 35, 3), iserial = (int)getbits(bits, 38, 11);
        long igrid6 = (long)getbits(bits, 49, 25);
        if (igrid6 < 0 || igrid6 > 18662399) { *okp = false; return ""; }
        call_1 = hash12(n12);
        call_2 = hash22(n22);
        char cexch[16];
        snprintf(cexch, sizeof cexch, "%2d%04d", 52 + irpt, iserial);
        char g[8] = "";
        if (!to_grid6((int)igrid6, g)) ok = false;
        if (ir == 0) msg = call_1 + " " + call_2 + " " + cexch + " " + g;
        if (ir == 1) msg = call_1 + " " + call_2 + " R " + cexch + " " + g;
    } else {
        ok = false; // i3 6 and 7 are not defined
    }
    if (msg.compare(0, 4, "CQ <") == 0) ok = false;
    msg = trim(msg);
    if (msg.size() > 37) msg.resize(37);
    *okp = ok;
    return msg;
}

} // namespace wm
