// trunkfmt - FoxSDR GPL helper built from parts of sdrtrunk (C++ port).
//
// Copyright (C) 2014-2018 Dennis Sheirer (sdrtrunk, the Java source this is ported from)
// Copyright (C) 2026 FoxSDR helpers repository contributors (the C++ port)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace trunkfmt {

// ---------------------------------------------------------------------------------------------
// BitMsg: a fixed-length bit string, the C++ stand-in for sdrtrunk's CorrectedBinaryMessage /
// BinaryMessage (java.util.BitSet subclass).  Index 0 is the first bit received.
// ---------------------------------------------------------------------------------------------
class BitMsg
{
public:
    BitMsg() = default;
    explicit BitMsg(size_t length) : mLength(length) { mBits.assign(length, 0); }

    size_t length() const { return mLength; }
    size_t size() const { return mCount; }          // bits added so far (BinaryMessage.size() semantics for a framer message)
    bool isFull() const { return mCount >= mLength; }
    void add(bool b) { if(mCount < mLength) { mBits[mCount++] = b ? 1 : 0; } }
    bool get(size_t i) const { return i < mBits.size() && mBits[i] != 0; }
    void set(size_t i, bool b) { if(i < mBits.size()) { mBits[i] = b ? 1 : 0; } }
    void flip(size_t i) { if(i < mBits.size()) { mBits[i] ^= 1; } }

    // BinaryMessage.getInt(int[] bits): the first listed index is the most significant bit.
    uint32_t getInt(const int* idx, size_t n) const
    {
        uint32_t v = 0;
        for(size_t k = 0; k < n; k++) { v = (v << 1) | (get((size_t)idx[k]) ? 1u : 0u); }
        return v;
    }
    template <size_t N> uint32_t getInt(const int (&idx)[N]) const { return getInt(idx, N); }

    // BinaryMessage.getInt(int start, int end) with start < end: contiguous range, start is the MSB.
    uint32_t getRange(int start, int end) const
    {
        uint32_t v = 0;
        for(int x = start; x <= end; x++) { v = (v << 1) | (get((size_t)x) ? 1u : 0u); }
        return v;
    }

    uint64_t getLong(const int* idx, size_t n) const
    {
        uint64_t v = 0;
        for(size_t k = 0; k < n; k++) { v = (v << 1) | (get((size_t)idx[k]) ? 1u : 0u); }
        return v;
    }
    template <size_t N> uint64_t getLong(const int (&idx)[N]) const { return getLong(idx, N); }

    // BinaryMessage.getHex(int start, int end): nibbles at start, start+4 ... <= end.  getNibble() reads four bits from
    // each nibble start, so the last nibble reads past `end` when the range is not a multiple of four bits (kept).
    std::string getHex(int start, int end) const
    {
        std::string s;
        for(int x = start; x <= end; x += 4)
        {
            int v = 0;
            for(int k = 0; k < 4; k++)
            {
                size_t idx = (size_t)(x + k);
                v = (v << 1) | ((idx <= mLength && get(idx)) ? 1 : 0);
            }
            s.push_back("0123456789ABCDEF"[v]);
        }
        return s;
    }

    // BinaryMessage.toHexString(): the bits in order, four per hex digit, zero padded at the end.
    std::string toHex() const
    {
        static const char* d = "0123456789ABCDEF";
        std::string s;
        for(size_t i = 0; i < mLength; i += 4)
        {
            int v = 0;
            for(size_t k = 0; k < 4; k++) { v = (v << 1) | (get(i + k) ? 1 : 0); }
            s.push_back(d[v]);
        }
        return s;
    }

private:
    size_t mLength = 0;
    size_t mCount = 0;
    std::vector<uint8_t> mBits;
};

// edac/CRC.java (the values the decoders in this helper use)
enum class CrcResult { PASSED, CORRECTED, FAILED_CRC, FAILED_PARITY, UNKNOWN };

// "%0*X" helper
inline std::string hexN(uint64_t v, int digits)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%0*llX", digits, (unsigned long long)v);
    return buf;
}

// ---------------------------------------------------------------------------------------------
// JSON Lines output.  One object per line, flushed per line.  ASCII-only output.
// ---------------------------------------------------------------------------------------------
class Json
{
public:
    Json() { mS = "{"; }
    Json& str(const char* key, const std::string& v) { k(key); quote(v); return *this; }
    Json& num(const char* key, long long v) { k(key); mS += std::to_string(v); return *this; }
    Json& fix(const char* key, double v, int decimals)
    {
        k(key);
        char b[64];
        snprintf(b, sizeof b, "%.*f", decimals, v);
        mS += b;
        return *this;
    }
    Json& boolean(const char* key, bool v) { k(key); mS += v ? "true" : "false"; return *this; }
    Json& null(const char* key) { k(key); mS += "null"; return *this; }
    Json& raw(const char* key, const std::string& rawJson) { k(key); mS += rawJson; return *this; }
    std::string done() const { return mS + "}"; }

    static std::string quoted(const std::string& v)
    {
        Json j;
        j.quote(v);
        return j.mS.substr(1);
    }

private:
    void k(const char* key)
    {
        if(mS.size() > 1) { mS += ','; }
        quote(key);
        mS += ':';
    }
    void quote(const std::string& v)
    {
        // note: mS may be "{" when called from quoted(); that is fine, callers strip it.
        mS += '"';
        for(unsigned char c : v)
        {
            switch(c)
            {
                case '"': mS += "\\\""; break;
                case '\\': mS += "\\\\"; break;
                case '\n': mS += "\\n"; break;
                case '\r': mS += "\\r"; break;
                case '\t': mS += "\\t"; break;
                default:
                    if(c < 0x20 || c >= 0x7F)
                    {
                        char b[8];
                        snprintf(b, sizeof b, "\\u%04x", c);   // Latin-1 code point, as a Java char would be
                        mS += b;
                    }
                    else
                    {
                        mS += (char)c;
                    }
            }
        }
        mS += '"';
    }
    std::string mS;
};

// Line sink: writes one line to stdout and flushes.
void emitLine(const std::string& line);

}  // namespace trunkfmt
