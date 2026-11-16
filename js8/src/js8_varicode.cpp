// SPDX-License-Identifier: GPL-3.0-only
//
// Part of the FoxSDR "js8" GPL helper (see ../UPSTREAM.md).
//
// Port of JS8Call's varicode.cpp, jsc.cpp and decodedtext.cpp to the C++ standard library. JS8Call is GPL-3.0,
//   (C) 2018 Jordan Sherer <kn4crd@gmail.com> - All Rights Reserved (varicode.cpp, jsc.cpp, decodedtext.cpp).
// Upstream function names are given in comments next to each ported part. Only what a receiver needs is here, plus
// the packing functions that the test tool uses to make frames; message reassembly over several frames, callsign
// validation by regular expression, the 16/32-bit checksums for buffered commands and the heartbeat/compound/directed
// parsing of typed text are not ported (they are transmit-side or application-level).

#include "js8_varicode.h"
#include "jsc.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>

namespace Js8
{
    namespace
    {
        using Bits = std::vector<bool>;

        const char * const alphabet72     = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-+/?.";
        const char * const alphanumeric   = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ /@"; // 39 characters

        constexpr std::uint32_t nbasecall = 37u * 36u * 10u * 27u * 27u * 27u;
        constexpr std::uint16_t nbasegrid = 180 * 180;
        constexpr std::uint16_t nusergrid = nbasegrid + 10;
        constexpr std::uint16_t nmaxgrid  = (1 << 15) - 1;

        // basecalls (group "callsigns"); the value is nbasecall + the number in the table.
        const char * const basecallNames[] =
        {
            nullptr,
            "<....>", "@ALLCALL", "@JS8NET",
            "@DX/NA", "@DX/SA", "@DX/EU", "@DX/AS", "@DX/AF", "@DX/OC", "@DX/AN",
            "@REGION/1", "@REGION/2", "@REGION/3",
            "@GROUP/0", "@GROUP/1", "@GROUP/2", "@GROUP/3", "@GROUP/4", "@GROUP/5", "@GROUP/6", "@GROUP/7",
            "@GROUP/8", "@GROUP/9",
            "@COMMAND", "@CONTROL", "@NET", "@NTS",
            "@RESERVE/0", "@RESERVE/1", "@RESERVE/2", "@RESERVE/3", "@RESERVE/4",
            "@APRSIS", "@RAGCHEW", "@JS8", "@EMCOMM", "@ARES", "@MARS", "@AMRRON", "@RACES", "@RAYNET", "@RADAR",
            "@SKYWARN", "@CQ", "@HB", "@QSO", "@QSOPARTY", "@CONTEST", "@FIELDDAY", "@SOTA", "@IOTA", "@POTA",
            "@QRP", "@QRO"
        };
        constexpr int basecallCount = sizeof(basecallNames) / sizeof(basecallNames[0]) - 1; // 54

        // directed_cmds: for each 5-bit value the key QMap::key() returns, which is the first key in ascending
        // order holding that value (" SNR?" before "?", " QUERY MSGS" before " QUERY MSGS?", " " before "  ").
        const char * const directedCmd[32] =
        {
            " SNR?", " DIT DIT", " NACK", " HEARING?", " GRID?", ">", " STATUS?", " STATUS", " HEARING", " MSG",
            " MSG TO:", " QUERY", " QUERY MSGS", " QUERY CALL", " ACK", " GRID", " INFO?", " INFO", " FB", " HW CPY?",
            " SK", " RR", " QSL?", " QSL", " CMD", " SNR", " NO", " YES", " 73", " HEARTBEAT SNR", " AGN?", " "
        };

        const char * const cqs[8] = {"CQ CQ CQ", "CQ DX", "CQ QRP", "CQ CONTEST", "CQ FIELD", "CQ FD", "CQ CQ", "CQ"};

        bool isSNRCommandValue(int v) { return v == 25 || v == 29; }

        // Varicode::defaultHuffTable
        struct Huff { const char * ch; const char * code; };
        const Huff huffTable[] =
        {
            {" ", "01"}, {"E", "100"}, {"T", "1101"}, {"A", "0011"}, {"O", "11111"}, {"I", "11100"}, {"N", "10111"},
            {"S", "10100"}, {"H", "00011"}, {"R", "00000"}, {"D", "111011"}, {"L", "110011"}, {"C", "110001"},
            {"U", "101101"}, {"M", "101011"}, {"W", "001011"}, {"F", "001001"}, {"G", "000101"}, {"Y", "000011"},
            {"P", "1111011"}, {"B", "1111001"}, {".", "1110100"}, {"V", "1100101"}, {"K", "1100100"},
            {"-", "1100001"}, {"+", "1100000"}, {"?", "1011001"}, {"!", "1011000"}, {"\"", "1010101"},
            {"X", "1010100"}, {"0", "0010101"}, {"J", "0010100"}, {"1", "0010001"}, {"Q", "0010000"},
            {"2", "0001001"}, {"Z", "0001000"}, {"3", "0000101"}, {"5", "0000100"}, {"4", "11110101"},
            {"9", "11110100"}, {"8", "11110001"}, {"6", "11110000"}, {"7", "11101011"}, {"/", "11101010"}
        };

        // Varicode::intToBits
        Bits intToBits(std::uint64_t value, int expected = 0)
        {
            Bits bits;
            while (value)
            {
                bits.insert(bits.begin(), (value & 1) != 0);
                value >>= 1;
            }
            if (expected)
            {
                while (static_cast<int>(bits.size()) < expected) bits.insert(bits.begin(), false);
            }
            return bits;
        }

        // Varicode::bitsToInt (vector, start, n)
        std::uint64_t bitsToInt(Bits const & bits, std::size_t start, std::size_t n)
        {
            std::uint64_t v = 0;
            for (std::size_t i = 0; i < n && start + i < bits.size(); ++i) v = (v << 1) + (bits[start + i] ? 1 : 0);
            return v;
        }

        void append(Bits & to, Bits const & from)
        {
            to.insert(to.end(), from.begin(), from.end());
        }

        int indexOf(const char * set, char c)
        {
            const char * p = std::strchr(set, c);
            return (p && c) ? static_cast<int>(p - set) : -1;
        }

        // Varicode::unpack72bits: the first 64 bits, and the last 8 in `rem`.
        std::uint64_t unpack72bits(std::string const & text, std::uint8_t * rem)
        {
            std::uint64_t value = 0;
            for (int i = 0; i < 10; ++i)
            {
                value |= static_cast<std::uint64_t>(indexOf(alphabet72, text[static_cast<std::size_t>(i)]) & 0x3f)
                         << (58 - 6 * i);
            }
            auto const remHigh = static_cast<std::uint8_t>(indexOf(alphabet72, text[10]) & 0x3f);
            value |= static_cast<std::uint64_t>(remHigh >> 2);
            auto const remLow = static_cast<std::uint8_t>(indexOf(alphabet72, text[11]) & 0x3f);
            if (rem) *rem = static_cast<std::uint8_t>(((remHigh & 3) << 6) | remLow);
            return value;
        }

        // Varicode::pack72bits
        std::string pack72bits(std::uint64_t value, std::uint8_t rem)
        {
            char packed[12];
            auto const remHigh = static_cast<std::uint8_t>(((value & 15) << 2) | (rem >> 6));
            auto const remLow  = static_cast<std::uint8_t>(rem & 63);
            value >>= 4;
            packed[11] = alphabet72[remLow];
            packed[10] = alphabet72[remHigh];
            for (int i = 0; i < 10; ++i)
            {
                packed[9 - i] = alphabet72[value & 63];
                value >>= 6;
            }
            return std::string(packed, 12);
        }

        // Varicode::unpackAlphaNumeric50
        std::string unpackAlphaNumeric50(std::uint64_t packed)
        {
            char word[11];
            auto take = [&](std::uint64_t m) { auto t = packed % m; packed /= m; return t; };

            word[10] = alphanumeric[take(38)];
            word[9]  = alphanumeric[take(38)];
            word[8]  = alphanumeric[take(38)];
            word[7]  = take(2) ? '/' : ' ';
            word[6]  = alphanumeric[take(38)];
            word[5]  = alphanumeric[take(38)];
            word[4]  = alphanumeric[take(38)];
            word[3]  = take(2) ? '/' : ' ';
            word[2]  = alphanumeric[take(38)];
            word[1]  = alphanumeric[take(38)];
            word[0]  = alphanumeric[take(39)];

            std::string value(word, 11);
            value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
            return value;
        }

        std::string trimmed(std::string s)
        {
            auto const a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) return "";
            auto const b = s.find_last_not_of(" \t\r\n");
            return s.substr(a, b - a + 1);
        }

        // Varicode::unpackCallsign
        std::string unpackCallsign(std::uint32_t value, bool portable)
        {
            if (value > nbasecall && value - nbasecall <= static_cast<std::uint32_t>(basecallCount))
            {
                return basecallNames[value - nbasecall];
            }

            char word[6];
            word[5] = alphanumeric[(value % 27) + 10]; value /= 27;
            word[4] = alphanumeric[(value % 27) + 10]; value /= 27;
            word[3] = alphanumeric[(value % 27) + 10]; value /= 27;
            word[2] = alphanumeric[value % 10];        value /= 10;
            word[1] = alphanumeric[value % 36];        value /= 36;
            word[0] = alphanumeric[value < 39 ? value : 0];

            std::string callsign(word, 6);
            if (callsign.compare(0, 3, "3D0") == 0) callsign = "3DA0" + callsign.substr(3);
            if (callsign[0] == 'Q' && callsign[1] >= 'A' && callsign[1] <= 'Z') callsign = "3X" + callsign.substr(1);
            if (portable) callsign = trimmed(callsign) + "/P";
            return trimmed(callsign);
        }

        // Varicode::deg2grid (first four characters only, which is all unpackGrid uses)
        std::string deg2grid4(float dlong, float dlat)
        {
            char grid[4];
            if (dlong < -180) dlong += 360;
            if (dlong > 180)  dlong -= 360;

            int nlong = static_cast<int>(60.0 * (180.0 - dlong) / 5);
            int n1    = nlong / 240;
            int n2    = (nlong - 240 * n1) / 24;
            grid[0]   = static_cast<char>('A' + n1);
            grid[2]   = static_cast<char>('0' + n2);

            int nlat = static_cast<int>(60.0 * (dlat + 90) / 2.5);
            n1       = nlat / 240;
            n2       = (nlat - 240 * n1) / 24;
            grid[1]  = static_cast<char>('A' + n1);
            grid[3]  = static_cast<char>('0' + n2);

            return std::string(grid, 4);
        }

        // Varicode::unpackGrid
        std::string unpackGrid(std::uint16_t value)
        {
            if (value > nbasegrid) return "";
            float dlat  = static_cast<float>(value % 180 - 90);
            float dlong = static_cast<float>(value / 180 * 2 - 180 + 2);
            return deg2grid4(dlong, dlat);
        }

        std::string formatSNR(int snr)
        {
            if (snr < -60 || snr > 60) return "";
            char buf[16];
            std::snprintf(buf, sizeof buf, "%+03d", snr);
            return buf;
        }

        // Varicode::unpackCmd: the command value and, for SNR commands, the packed number.
        int unpackCmd(std::uint8_t value, std::uint8_t * num)
        {
            if (value & (1 << 7))
            {
                if (num) *num = value & ((1 << 6) - 1);
                return (value & (1 << 6)) ? 29 : 25; // " HEARTBEAT SNR" : " SNR"
            }
            if (num) *num = 0;
            return value & ((1 << 7) - 1);
        }

        std::string cmdKey(int v)
        {
            return (v >= 0 && v < 32) ? directedCmd[v] : "";
        }

        // Latin-1 (what the JSC table holds) to UTF-8.
        void appendLatin1(std::string & out, const char * s)
        {
            for (; *s; ++s)
            {
                auto const c = static_cast<unsigned char>(*s);
                if (c < 0x80)
                {
                    out += static_cast<char>(c);
                }
                else
                {
                    out += static_cast<char>(0xC0 | (c >> 6));
                    out += static_cast<char>(0x80 | (c & 0x3F));
                }
            }
        }

        // Varicode::huffDecode, including its quirk of continuing to scan later keys against the already-advanced
        // bit string within one pass.
        std::string huffDecode(Bits const & bitvec)
        {
            std::string bits;
            for (bool b : bitvec) bits += b ? '1' : '0';

            // keys in ascending order, as QMap iterates
            std::vector<Huff const *> keys;
            for (auto const & h : huffTable) keys.push_back(&h);
            std::sort(keys.begin(), keys.end(), [](Huff const * a, Huff const * b) { return std::strcmp(a->ch, b->ch) < 0; });

            std::string text;

            while (bits.size() > 0)
            {
                bool found = false;
                for (auto const * key : keys)
                {
                    if (bits.compare(0, std::strlen(key->code), key->code) == 0)
                    {
                        text += key->ch;
                        bits.erase(0, std::strlen(key->code));
                        found = true;
                    }
                }
                if (!found) break;
            }
            return text;
        }

        // Qt's QVector::lastIndexOf(false)
        int lastZero(Bits const & bits)
        {
            for (int i = static_cast<int>(bits.size()) - 1; i >= 0; --i)
            {
                if (!bits[static_cast<std::size_t>(i)]) return i;
            }
            return -1;
        }

        // Qt's QVector::mid(pos, n) (n < 0 means everything from pos)
        Bits mid(Bits const & bits, int pos, int n)
        {
            Bits out;
            for (int i = pos; i < static_cast<int>(bits.size()) && (n < 0 || i < pos + n); ++i)
            {
                out.push_back(bits[static_cast<std::size_t>(i)]);
            }
            return out;
        }

        // Varicode::unpackDataMessage
        std::string unpackDataMessage(std::string const & text)
        {
            if (text.size() < 12 || text.find(' ') != std::string::npos) return "";

            std::uint8_t rem = 0;
            auto const value = unpack72bits(text, &rem);
            Bits bits = intToBits(value, 64);
            append(bits, intToBits(rem, 8));

            if (!bits.at(0)) return "";
            bits = mid(bits, 1, -1);

            bool const compressed = bits.at(0);
            int const  n          = lastZero(bits);

            bits = mid(bits, 1, n - 1);

            return compressed ? jscDecompress(bits) : huffDecode(bits);
        }

        // Varicode::unpackFastDataMessage (JS8_FAST_DATA_CAN_USE_HUFF is 0 upstream)
        std::string unpackFastDataMessage(std::string const & text)
        {
            if (text.size() < 12 || text.find(' ') != std::string::npos) return "";

            std::uint8_t rem = 0;
            auto const value = unpack72bits(text, &rem);
            Bits bits = intToBits(value, 64);
            append(bits, intToBits(rem, 8));

            int const n = lastZero(bits);
            bits = mid(bits, 0, n);

            return jscDecompress(bits);
        }

        // Varicode::unpackCompoundFrame: callsign, type, num, bits3; false when it is not a compound-type frame.
        bool unpackCompoundFrame(std::string const & text, int & type, std::uint16_t & num, std::uint8_t & bits3,
                                 std::string & callsign)
        {
            if (text.size() < 12 || text.find(' ') != std::string::npos) return false;

            std::uint8_t packed8 = 0;
            Bits const bits = intToBits(unpack72bits(text, &packed8), 64);

            auto const packed5 = static_cast<std::uint8_t>(packed8 >> 3);
            auto const packed3 = static_cast<std::uint8_t>(packed8 & 7);
            auto const flag    = static_cast<int>(bitsToInt(bits, 0, 3));

            if (flag == FrameData || flag == FrameDirected) return false;

            auto const packedCall = bitsToInt(bits, 3, 50);
            auto const packed11   = static_cast<std::uint16_t>(bitsToInt(bits, 53, 11));

            callsign = unpackAlphaNumeric50(packedCall);
            type     = flag;
            num      = static_cast<std::uint16_t>((packed11 << 5) | packed5);
            bits3    = packed3;
            return true;
        }
    }

    // --- JSC ---------------------------------------------------------------------------------------------------
    // JSC::decompress
    std::string jscDecompress(std::vector<bool> const & bitvec)
    {
        const std::uint32_t b = 4;
        const std::uint32_t s = 7;
        const std::uint32_t c = (1u << b) - s;

        std::uint32_t base[8];
        base[0] = 0;
        base[1] = s;
        base[2] = base[1] + s * c;
        base[3] = base[2] + s * c * c;
        base[4] = base[3] + s * c * c * c;
        base[5] = base[4] + s * c * c * c * c;
        base[6] = base[5] + s * c * c * c * c * c;
        base[7] = base[6] + s * c * c * c * c * c * c;

        std::vector<std::uint64_t> bytes;
        std::vector<std::uint32_t> separators;

        std::size_t       i     = 0;
        std::size_t const count = bitvec.size();
        while (i < count)
        {
            if (count - i < 4) break;
            std::uint64_t byte = bitsToInt(bitvec, i, 4);
            bytes.push_back(byte);
            i += 4;

            if (byte < s)
            {
                if (count > i && bitvec[i]) separators.push_back(static_cast<std::uint32_t>(bytes.size() - 1));
                i += 1;
            }
        }

        std::string  out;
        std::size_t  nextSeparator = 0;
        std::uint32_t start = 0;
        while (start < bytes.size())
        {
            std::uint32_t k = 0;
            std::uint32_t j = 0;

            while (start + k < bytes.size() && bytes[start + k] >= s)
            {
                j = j * c + static_cast<std::uint32_t>(bytes[start + k] - s);
                k++;
            }
            if (j >= JSC::size) break;
            if (start + k >= bytes.size()) break;
            if (k >= 8) break; // not in upstream: base[] has eight entries, upstream would read past it

            j = j * s + static_cast<std::uint32_t>(bytes[start + k]) + base[k];
            if (j >= JSC::size) break;

            appendLatin1(out, JSC::map[j].str); // map is in latin1 format, not utf-8

            if (nextSeparator < separators.size() && separators[nextSeparator] == start + k)
            {
                out += ' ';
                ++nextSeparator;
            }

            start = start + (k + 1);
        }

        return out;
    }

    char const * frameTypeName(int frameType)
    {
        switch (frameType)
        {
            case FrameHeartbeat:        return "heartbeat";
            case FrameCompound:         return "compound";
            case FrameCompoundDirected: return "compound_directed";
            case FrameDirected:         return "directed";
            case FrameData:             return "data";
            default:                    return "unknown";
        }
    }

    // --- DecodedText ---------------------------------------------------------------------------------------------
    Frame interpret(std::string const & frameText, int const bits)
    {
        Frame f;
        f.text = frameText;

        std::string const m = trimmed(frameText);
        if (m.size() < 12 || m.find(' ') != std::string::npos) return f;

        bool const dataFlag = (bits & BitsData) == BitsData;

        // tryUnpackFastData
        if (dataFlag)
        {
            auto const data = unpackFastDataMessage(m);
            if (!data.empty())
            {
                f.text        = data;
                f.frameType   = FrameData;
                f.interpreted = true;
                return f;
            }
        }
        else
        {
            // tryUnpackData
            auto const data = unpackDataMessage(m);
            if (!data.empty())
            {
                f.text        = data;
                f.frameType   = FrameData;
                f.interpreted = true;
                return f;
            }
        }

        // tryUnpackHeartbeat
        if (!dataFlag)
        {
            int type = FrameUnknown; std::uint16_t num = nmaxgrid; std::uint8_t bits3 = 0; std::string call;
            if (unpackCompoundFrame(m, type, num, bits3, call) && type == FrameHeartbeat)
            {
                std::string const grid = unpackGrid(num & ((1 << 15) - 1));
                bool const        alt  = (num & (1 << 15)) != 0;

                f.frameType   = type;
                f.from        = call;
                f.grid        = grid;
                f.alt         = alt;
                f.interpreted = true;
                f.text        = call + ": ";
                if (alt)
                {
                    f.cq = bits3 < 8 ? cqs[bits3] : "";
                    f.text += "@ALLCALL " + f.cq;
                }
                else
                {
                    f.cq = "HB"; // Varicode::hbString: every heartbeat flag value reads "HB"
                    f.text += "@HB HEARTBEAT";
                }
                f.text += ' ' + grid + ' ';
                return f;
            }
        }

        // tryUnpackCompound
        if (!dataFlag)
        {
            int type = FrameUnknown; std::uint16_t extra = nmaxgrid; std::uint8_t bits3 = 0; std::string call;
            if (unpackCompoundFrame(m, type, extra, bits3, call) && (type == FrameCompound || type == FrameCompoundDirected))
            {
                std::vector<std::string> extras; // parts after the callsign and the empty second part
                std::string grid;

                if (extra <= nbasegrid)
                {
                    grid = unpackGrid(extra);
                    extras.push_back(" " + grid);
                }
                else if (nusergrid <= extra && extra < nmaxgrid)
                {
                    std::uint8_t num = 0;
                    int const    cmd = unpackCmd(static_cast<std::uint8_t>(extra - nusergrid), &num);
                    std::string const cmdStr = cmdKey(cmd);
                    extras.push_back(cmdStr);
                    f.cmd = trimmed(cmdStr);
                    if (isSNRCommandValue(cmd))
                    {
                        f.num = formatSNR(num - 31);
                        extras.push_back(f.num);
                    }
                }

                f.frameType   = type;
                f.from        = call;
                f.grid        = trimmed(grid);
                f.interpreted = true;

                std::string joined;
                for (std::size_t i = 0; i < extras.size(); ++i) joined += (i ? " " : "") + extras[i];

                if (type == FrameCompound)
                {
                    f.text = call + ": ";
                }
                else
                {
                    f.text = call + joined + ' ';
                }
                return f;
            }
        }

        // tryUnpackDirected
        if (!dataFlag)
        {
            std::uint8_t extra = 0;
            Bits const   b     = intToBits(unpack72bits(m, &extra), 64);

            if (static_cast<int>(bitsToInt(b, 0, 3)) == FrameDirected)
            {
                auto const packedFrom = static_cast<std::uint32_t>(bitsToInt(b, 3, 28));
                auto const packedTo   = static_cast<std::uint32_t>(bitsToInt(b, 31, 28));
                auto const packedCmd  = static_cast<int>(bitsToInt(b, 59, 5));

                bool const portableFrom = ((extra >> 7) & 1) == 1;
                bool const portableTo   = ((extra >> 6) & 1) == 1;
                extra = static_cast<std::uint8_t>(extra % 64);

                f.from = unpackCallsign(packedFrom, portableFrom);
                f.to   = unpackCallsign(packedTo, portableTo);
                std::string const cmd = cmdKey(packedCmd % 32);
                f.cmd  = trimmed(cmd);

                std::string numPart;
                if (extra != 0)
                {
                    f.num = isSNRCommandValue(packedCmd % 32) ? formatSNR(static_cast<int>(extra) - 31)
                                                              : std::to_string(static_cast<int>(extra) - 31);
                    numPart = ' ' + f.num;
                }

                f.frameType   = FrameDirected;
                f.interpreted = true;
                f.text        = f.from + ": " + f.to + cmd + numPart + ' ';
                return f;
            }
        }

        return f;
    }

    // =============================================================================================================
    // Transmit side (test tool only)
    // =============================================================================================================
    namespace
    {
        // Varicode::grid2deg
        void grid2deg(std::string const & grid, float & dlong, float & dlat)
        {
            std::string g = grid;
            if (g.size() < 6) g = grid.substr(0, 4) + "mm";
            for (std::size_t i = 0; i < 4; ++i) g[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(g[i])));
            for (std::size_t i = g.size() - 2; i < g.size(); ++i) g[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(g[i])));

            int   nlong     = 180 - 20 * (g[0] - 'A');
            int   n20d      = 2 * (g[2] - '0');
            float xminlong  = static_cast<float>(5 * (g[4] - 'a' + 0.5));
            dlong           = nlong - n20d - xminlong / 60.0f;

            int   nlat      = -90 + 10 * (g[1] - 'A') + g[3] - '0';
            float xminlat   = static_cast<float>(2.5 * (g[5] - 'a' + 0.5));
            dlat            = nlat + xminlat / 60.0f;
        }

        // Varicode::packGrid
        std::uint16_t packGrid(std::string const & value)
        {
            std::string grid = trimmed(value);
            if (grid.size() < 4) return (1 << 15) - 1;

            float dlong = 0, dlat = 0;
            grid2deg(grid.substr(0, 4), dlong, dlat);
            int ilong = static_cast<int>(dlong);
            int ilat  = static_cast<int>(dlat + 90);

            return static_cast<std::uint16_t>(((ilong + 180) / 2) * 180 + ilat);
        }

        // Varicode::packAlphaNumeric50
        std::uint64_t packAlphaNumeric50(std::string const & value)
        {
            std::string word;
            for (char c : value)
            {
                if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '/' || c == '@') word += c;
            }
            if (word.size() > 3 && word[3] != '/') word.insert(3, " ");
            if (word.size() > 7 && word[7] != '/') word.insert(7, " ");
            if (word.size() < 11) word += std::string(11 - word.size(), ' ');

            auto ix = [&](std::size_t i) { return static_cast<std::uint64_t>(indexOf(alphanumeric, word[i])); };
            std::uint64_t const a = 38ull * 38 * 38 * 2 * 38 * 38 * 38 * 2 * 38 * 38 * ix(0);
            std::uint64_t const b = 38ull * 38 * 38 * 2 * 38 * 38 * 38 * 2 * 38 * ix(1);
            std::uint64_t const c = 38ull * 38 * 38 * 2 * 38 * 38 * 38 * 2 * ix(2);
            std::uint64_t const d = 38ull * 38 * 38 * 2 * 38 * 38 * 38 * (word[3] == '/' ? 1 : 0);
            std::uint64_t const e = 38ull * 38 * 38 * 2 * 38 * 38 * ix(4);
            std::uint64_t const f = 38ull * 38 * 38 * 2 * 38 * ix(5);
            std::uint64_t const g = 38ull * 38 * 38 * 2 * ix(6);
            std::uint64_t const h = 38ull * 38 * 38 * (word[7] == '/' ? 1 : 0);
            std::uint64_t const i = 38ull * 38 * ix(8);
            std::uint64_t const j = 38ull * ix(9);
            std::uint64_t const k = ix(10);

            return a + b + c + d + e + f + g + h + i + j + k;
        }

        bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
        bool isDigit(char c) { return c >= '0' && c <= '9'; }

        // Varicode::packCallsign (the regular expression pack_callsign_pattern written out)
        std::uint32_t packCallsign(std::string value, bool * portable)
        {
            std::string callsign = trimmed(value);
            for (auto & c : callsign) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

            for (int i = 0; i < basecallCount; ++i)
            {
                if (callsign == basecallNames[i + 1]) return nbasecall + static_cast<std::uint32_t>(i + 1);
            }

            if (callsign.size() >= 2 && callsign.compare(callsign.size() - 2, 2, "/P") == 0)
            {
                callsign.resize(callsign.size() - 2);
                if (portable) *portable = true;
            }
            if (callsign.compare(0, 4, "3DA0") == 0) callsign = "3D0" + callsign.substr(4);
            if (callsign.compare(0, 2, "3X") == 0 && callsign.size() > 2 && isUpper(callsign[2])) callsign = "Q" + callsign.substr(2);

            std::size_t const slen = callsign.size();
            if (slen < 2 || slen > 6) return 0;

            std::vector<std::string> permutations{callsign};
            if (slen == 2) permutations.push_back(" " + callsign + "   ");
            if (slen == 3) { permutations.push_back(" " + callsign + "  "); permutations.push_back(callsign + "   "); }
            if (slen == 4) { permutations.push_back(" " + callsign + " "); permutations.push_back(callsign + "  "); }
            if (slen == 5) { permutations.push_back(" " + callsign); permutations.push_back(callsign + " "); }

            std::string matched;
            for (auto const & p : permutations)
            {
                if (p.size() != 6) continue;
                bool const ok = (isDigit(p[0]) || isUpper(p[0]) || p[0] == ' ') && (isDigit(p[1]) || isUpper(p[1])) &&
                                isDigit(p[2]) && (isUpper(p[3]) || p[3] == ' ') && (isUpper(p[4]) || p[4] == ' ') &&
                                (isUpper(p[5]) || p[5] == ' ');
                if (ok) matched = p;
            }
            if (matched.size() < 6) return 0;

            std::uint32_t packed = static_cast<std::uint32_t>(indexOf(alphanumeric, matched[0]));
            packed = 36 * packed + static_cast<std::uint32_t>(indexOf(alphanumeric, matched[1]));
            packed = 10 * packed + static_cast<std::uint32_t>(indexOf(alphanumeric, matched[2]));
            packed = 27 * packed + static_cast<std::uint32_t>(indexOf(alphanumeric, matched[3])) - 10;
            packed = 27 * packed + static_cast<std::uint32_t>(indexOf(alphanumeric, matched[4])) - 10;
            packed = 27 * packed + static_cast<std::uint32_t>(indexOf(alphanumeric, matched[5])) - 10;
            return packed;
        }

        int cmdValue(std::string const & cmd) // value for a command written with its leading space
        {
            for (int i = 0; i < 32; ++i)
            {
                if (cmd == directedCmd[i]) return i;
            }
            return -1;
        }

        // Varicode::packNum
        std::uint8_t packNum(int num)
        {
            return static_cast<std::uint8_t>(std::max(-30, std::min(num, 31)) + 30 + 1);
        }

        // Varicode::packCmd
        std::uint8_t packCmd(int cmd, std::uint8_t num)
        {
            if (isSNRCommandValue(cmd))
            {
                int value = ((1 << 1) | (cmd == 29 ? 1 : 0)) << 6;
                value += num & ((1 << 6) - 1);
                return static_cast<std::uint8_t>(value);
            }
            return static_cast<std::uint8_t>(cmd & ((1 << 7) - 1));
        }

        // Varicode::packCompoundFrame
        std::string packCompoundFrame(std::string const & callsign, int type, std::uint16_t num, std::uint8_t bits3)
        {
            if (type == FrameData || type == FrameDirected) return "";

            std::uint64_t const packedCall = packAlphaNumeric50(callsign);
            if (packedCall == 0) return "";

            std::uint16_t const mask11 = ((1 << 11) - 1) << 5;
            std::uint8_t const  mask5  = (1 << 5) - 1;

            auto const packed11 = static_cast<std::uint16_t>((num & mask11) >> 5);
            auto const packed5  = static_cast<std::uint8_t>(num & mask5);
            auto const packed8  = static_cast<std::uint8_t>((packed5 << 3) | bits3);

            Bits bits = intToBits(static_cast<std::uint64_t>(type), 3);
            append(bits, intToBits(packedCall, 50));
            append(bits, intToBits(packed11, 11));

            return pack72bits(bitsToInt(bits, 0, bits.size()), packed8);
        }

        // --- JSC compress -----------------------------------------------------------------------------------------
        // JSC::lookup finds the longest table entry that is a prefix of the word (the upstream "list" is the same
        // strings sorted so the first prefix hit is the longest). Built from the map on first use.
        struct JscIndex
        {
            std::unordered_map<std::string, int> byString;
            std::size_t                          longest = 0;

            JscIndex()
            {
                for (std::uint32_t i = 0; i < JSC::size; ++i)
                {
                    std::string s = JSC::map[i].str;
                    if (s.empty()) continue;
                    if (byString.emplace(s, static_cast<int>(i)).second) longest = std::max(longest, s.size());
                }
            }
        };

        bool jscLookup(std::string const & w, int & index)
        {
            static JscIndex const idx;
            for (std::size_t len = std::min(idx.longest, w.size()); len >= 1; --len)
            {
                auto it = idx.byString.find(w.substr(0, len));
                if (it != idx.byString.end())
                {
                    index = it->second;
                    return true;
                }
            }
            return false;
        }

        // JSC::codeword
        Bits jscCodeword(std::uint32_t index, bool separate, std::uint32_t bytesize, std::uint32_t s, std::uint32_t c)
        {
            std::vector<Bits> out;

            std::uint32_t v = ((index % s) << 1) + (separate ? 1u : 0u);
            out.insert(out.begin(), intToBits(v, static_cast<int>(bytesize) + 1));

            std::uint32_t x = index / s;
            while (x > 0)
            {
                x -= 1;
                out.insert(out.begin(), intToBits((x % c) + s, static_cast<int>(bytesize)));
                x /= c;
            }

            Bits word;
            for (auto const & w : out) append(word, w);
            return word;
        }

        // JSC::compress: (codeword, number of characters)
        std::vector<std::pair<Bits, std::uint32_t>> jscCompress(std::string const & text)
        {
            std::vector<std::pair<Bits, std::uint32_t>> out;

            const std::uint32_t b = 4;
            const std::uint32_t s = 7;
            const std::uint32_t c = (1u << b) - s;

            std::vector<std::string> words;
            {
                std::size_t pos = 0;
                for (;;)
                {
                    auto const next = text.find(' ', pos);
                    if (next == std::string::npos) { words.push_back(text.substr(pos)); break; }
                    words.push_back(text.substr(pos, next - pos));
                    pos = next + 1;
                }
            }

            for (std::size_t i = 0; i < words.size(); i++)
            {
                std::string w = words[i];
                bool const  isLastWord = (i == words.size() - 1);
                bool        isSpaceCharacter = false;

                if (w.empty() && !isLastWord)
                {
                    w = " ";
                    isSpaceCharacter = true;
                }

                while (!w.empty())
                {
                    int index = 0;
                    if (!jscLookup(w, index)) break;

                    auto const size = static_cast<std::size_t>(JSC::map[index].size);
                    w = size >= w.size() ? std::string() : w.substr(size);

                    bool const isLast = w.empty();
                    bool const shouldAppendSpace = isLast && !isSpaceCharacter && !isLastWord;

                    out.push_back({jscCodeword(static_cast<std::uint32_t>(index), shouldAppendSpace, b, s, c),
                                   static_cast<std::uint32_t>(size) + (shouldAppendSpace ? 1u : 0u)});
                }
            }

            return out;
        }

        Bits padAndPack(Bits frameBits, std::string & frame)
        {
            int const frameSize = 72;
            int const pad       = frameSize - static_cast<int>(frameBits.size());
            for (int i = 0; i < pad; i++) frameBits.push_back(i != 0);

            auto const value = bitsToInt(frameBits, 0, 64);
            auto const rem   = static_cast<std::uint8_t>(bitsToInt(frameBits, 64, 8));
            frame            = pack72bits(value, rem);
            return frameBits;
        }

        // packHuffMessage
        std::string packHuff(std::string const & input, Bits prefix, int * n)
        {
            std::string frame;
            Bits        frameBits = prefix;
            int         i         = 0;

            for (char ch : input)
            {
                bool valid = false;
                for (auto const & h : huffTable) valid = valid || (h.ch[0] == std::toupper(static_cast<unsigned char>(ch)));
                if (!valid) { if (n) *n = 0; return frame; }
            }

            for (char ch : input)
            {
                Huff const * hit = nullptr;
                for (auto const & h : huffTable)
                {
                    if (h.ch[0] == ch) hit = &h;
                }
                if (!hit) continue; // upstream skips characters without a code

                std::size_t const len = std::strlen(hit->code);
                if (frameBits.size() + len < 72)
                {
                    for (std::size_t k = 0; k < len; ++k) frameBits.push_back(hit->code[k] == '1');
                    i += 1;
                    continue;
                }
                break;
            }

            padAndPack(frameBits, frame);
            if (n) *n = i;
            return frame;
        }

        // packCompressedMessage
        std::string packCompressed(std::string const & input, Bits prefix, int * n)
        {
            std::string frame;
            Bits        frameBits = prefix;
            int         i         = 0;

            for (auto const & pair : jscCompress(input))
            {
                if (frameBits.size() + pair.first.size() < 72)
                {
                    append(frameBits, pair.first);
                    i += static_cast<int>(pair.second);
                    continue;
                }
                break;
            }

            padAndPack(frameBits, frame);
            if (n) *n = i;
            return frame;
        }
    }

    std::string packHeartbeat(std::string const & callsign, std::string const & kind, std::string const & grid)
    {
        bool const    alt         = kind.compare(0, 2, "CQ") == 0;
        std::uint16_t packedExtra = nmaxgrid;
        if (grid.size() == 4) packedExtra = packGrid(grid);

        std::uint8_t number = 0; // hbs.key(type, 0)
        if (alt)
        {
            packedExtra |= (1 << 15);
            for (std::uint8_t k = 0; k < 8; ++k)
            {
                if (kind == cqs[k]) { number = k; break; }
            }
        }

        return packCompoundFrame(callsign, FrameHeartbeat, packedExtra, number);
    }

    std::string packCompound(std::string const & callsign, std::string const & grid)
    {
        std::uint16_t extra = nmaxgrid;
        if (!grid.empty()) extra = packGrid(grid);
        return packCompoundFrame(callsign, FrameCompound, extra, 0);
    }

    std::string packCompoundDirected(std::string const & callsign, std::string const & cmd, int num, bool hasNum)
    {
        int const value = cmdValue(cmd);
        if (value < 0) return "";
        std::uint8_t const inum = hasNum ? packNum(num) : 0;
        std::uint16_t const extra = static_cast<std::uint16_t>(nusergrid + packCmd(value, inum));
        return packCompoundFrame(callsign, FrameCompoundDirected, extra, 0);
    }

    std::string packDirected(std::string const & from, std::string const & to, std::string const & cmd, int num,
                             bool hasNum)
    {
        int const value = cmdValue(cmd);
        if (value < 0) return "";

        bool portableFrom = false, portableTo = false;
        std::uint32_t const packedFrom = packCallsign(from, &portableFrom);
        std::uint32_t const packedTo   = packCallsign(to, &portableTo);
        if (packedFrom == 0 || packedTo == 0) return "";

        std::uint8_t const inum   = hasNum ? packNum(num) : 0;
        auto const         extra  = static_cast<std::uint8_t>((portableFrom ? 128 : 0) + (portableTo ? 64 : 0) + inum);

        Bits bits = intToBits(FrameDirected, 3);
        append(bits, intToBits(packedFrom, 28));
        append(bits, intToBits(packedTo, 28));
        append(bits, intToBits(static_cast<std::uint64_t>(value % 32), 5));

        return pack72bits(bitsToInt(bits, 0, bits.size()), extra);
    }

    std::string packData(std::string const & text, bool fast, int * consumed)
    {
        if (fast)
        {
            int n = 0;
            auto frame = packCompressed(text, {}, &n);
            if (consumed) *consumed = n;
            return frame;
        }

        int huffChars = 0, compressedChars = 0;
        auto const huffFrame       = packHuff(text, {true, false}, &huffChars);
        auto const compressedFrame = packCompressed(text, {true, true}, &compressedChars);

        if (huffChars > compressedChars)
        {
            if (consumed) *consumed = huffChars;
            return huffFrame;
        }
        if (consumed) *consumed = compressedChars;
        return compressedFrame;
    }
}
