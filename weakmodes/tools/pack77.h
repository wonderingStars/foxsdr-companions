// weakmodes test tools: encoder for the common 77-bit WSJT-X messages (TEST TOOL only; the helper never transmits).
//
// C++ port of the parts of lib/77bit/packjt77.f90 (WSJT-X b4f9a43) needed to make test signals: pack28 (special
// tokens, standard calls), pack77_1 (type 1 standard messages), packtext77 (type 0.0 free text).
// WSJT-X, Copyright (C) 2001-2025 by Joe Taylor, K1JT, and the WSJT Development Group. GPL-3.0-or-later, see LICENSE.
// Left out: types 0.1, 0.3, 0.4, 0.5, 3, 4, 5, /P (type 2) and hashed <call> forms.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace wmtest {

inline bool p77_is_digit(char c) { return c >= '0' && c <= '9'; }
inline bool p77_is_letter(char c) { return c >= 'A' && c <= 'Z'; }

// pack28 for a special token or a standard call (c without trailing blanks); returns -1 if not packable
inline int pack28(const std::string &c) {
    const int NTOKENS = 2063592, MAX22 = 4194304;
    if (c == "DE") return 0;
    if (c == "QRZ") return 1;
    if (c == "CQ") return 2;
    std::string cs = c;
    if (cs.size() < 3 || cs.size() > 6) return -1;
    int n = (int)cs.size();
    int iarea = -1;
    for (int i = n; i >= 2; i--)
        if (p77_is_digit(cs[i - 1])) { iarea = i; break; }
    if (iarea < 2 || iarea > 3) return -1;
    int npdig = 0, nplet = 0, nslet = 0;
    for (int i = 1; i <= iarea - 1; i++) {
        if (p77_is_digit(cs[i - 1])) npdig++;
        if (p77_is_letter(cs[i - 1])) nplet++;
    }
    for (int i = iarea + 1; i <= n; i++)
        if (p77_is_letter(cs[i - 1])) nslet++;
    if (nplet == 0 || npdig >= iarea - 1 || nslet > 3) return -1;
    std::string call = iarea == 2 ? " " + cs : cs;
    while (call.size() < 6) call += ' ';
    const char *a1 = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ", *a2 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ",
               *a3 = "0123456789", *a4 = " ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    auto idx = [](const char *a, char ch) { const char *p = strchr(a, ch); return p ? (int)(p - a) : -1; };
    int i1 = idx(a1, call[0]), i2 = idx(a2, call[1]), i3 = idx(a3, call[2]), i4 = idx(a4, call[3]), i5 = idx(a4, call[4]),
        i6 = idx(a4, call[5]);
    if (i1 < 0 || i2 < 0 || i3 < 0 || i4 < 0 || i5 < 0 || i6 < 0) return -1;
    return 36 * 10 * 27 * 27 * 27 * i1 + 10 * 27 * 27 * 27 * i2 + 27 * 27 * 27 * i3 + 27 * 27 * i4 + 27 * i5 + i6 + NTOKENS + MAX22;
}

inline void p77_put(uint8_t *b, int &pos, uint64_t v, int n) {
    for (int i = n - 1; i >= 0; i--) b[pos++] = (uint8_t)((v >> i) & 1);
}

// Packs msg (upper case) into 77 bits. Returns false if the message is not one of the supported kinds.
inline bool pack77(std::string msg, uint8_t *bits) {
    // normalise: upper case, single blanks, trimmed
    std::string m;
    for (char ch : msg) m += (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
    std::vector<std::string> w;
    {
        std::string t;
        for (char ch : m + " ") {
            if (ch == ' ') { if (!t.empty()) w.push_back(t); t.clear(); }
            else t += ch;
        }
    }
    // type 1 standard message
    if (w.size() >= 2 && w.size() <= 4) {
        auto strip_r = [](std::string c, int &ip) {
            ip = 0;
            if (c.size() > 2 && c.compare(c.size() - 2, 2, "/R") == 0) { ip = 1; c.resize(c.size() - 2); }
            return c;
        };
        int ipa, ipb;
        std::string c1 = strip_r(w[0], ipa), c2 = strip_r(w[1], ipb);
        int n28a = pack28(c1), n28b = pack28(c2);
        if (n28a >= 0 && n28b >= 0 && c2 != "CQ" && c2 != "DE" && c2 != "QRZ") {
            int ir = 0, igrid4 = -1;
            if (w.size() == 2) {
                igrid4 = 32400 + 1;
            } else {
                std::string last = w.back();
                if (w.size() == 4 && w[2] != "R") goto freetext;
                if (w.size() == 4) ir = 1;
                bool grid = last.size() == 4 && last[0] >= 'A' && last[0] <= 'R' && last[1] >= 'A' && last[1] <= 'R' &&
                            p77_is_digit(last[2]) && p77_is_digit(last[3]);
                if (grid) {
                    igrid4 = (last[0] - 'A') * 18 * 10 * 10 + (last[1] - 'A') * 10 * 10 + (last[2] - '0') * 10 + (last[3] - '0');
                } else if (last == "RRR") { ir = 0; igrid4 = 32400 + 2; }
                else if (last == "RR73") { ir = 0; igrid4 = 32400 + 3; }
                else if (last == "73") { ir = 0; igrid4 = 32400 + 4; }
                else if ((last[0] == '+' || last[0] == '-' || (last[0] == 'R' && last.size() > 1 && (last[1] == '+' || last[1] == '-')))) {
                    std::string num = last[0] == 'R' ? last.substr(1) : last;
                    if (last[0] == 'R') ir = 1;
                    int irpt = atoi(num.c_str());
                    if (irpt >= -50 && irpt <= -31) irpt += 101;
                    igrid4 = 32400 + irpt + 35;
                } else goto freetext;
            }
            int pos = 0;
            p77_put(bits, pos, (uint64_t)n28a, 28); p77_put(bits, pos, ipa, 1);
            p77_put(bits, pos, (uint64_t)n28b, 28); p77_put(bits, pos, ipb, 1);
            p77_put(bits, pos, ir, 1); p77_put(bits, pos, (uint64_t)igrid4, 15); p77_put(bits, pos, 1, 3);
            return true;
        }
    }
freetext:
    // type 0.0 free text, up to 13 characters
    {
        const char *cset = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?";
        std::string t = m;
        if (t.size() > 13) return false;
        while (t.size() < 13) t = " " + t; // adjustr
        int qa[9] = {0};
        for (char ch : t) {
            const char *p = strchr(cset, ch);
            if (!p || !ch) return false;
            int j = (int)(p - cset);
            int carry = j;
            for (int k = 8; k >= 0; k--) { // qa = qa*42 + j, base 256
                int v = qa[k] * 42 + carry;
                qa[k] = v & 255;
                carry = v >> 8;
            }
            if (carry) return false;
        }
        if (qa[0] > 127) return false;
        int pos = 0;
        p77_put(bits, pos, (uint64_t)qa[0], 7);
        for (int k = 1; k < 9; k++) p77_put(bits, pos, (uint64_t)qa[k], 8);
        p77_put(bits, pos, 0, 3); p77_put(bits, pos, 0, 3);
        return true;
    }
}

} // namespace wmtest
