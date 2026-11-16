#ifndef INMARSATC_PARSER_H
#define INMARSATC_PARSER_H

/* FoxSDR helper: static linking, no export/import decoration (see UPSTREAM.md). */
#include <vector>
#include <map>
#include <string>
#include <iomanip>
#include <cstdio>
#include <inmarsatc_decoder.h>

namespace inmarsatc {
    namespace frameParser {
        /// FOXSDR: replaces upstream's std::map<std::string, std::string> packetVars. Same keys, in the order they
        /// were added, but each value is already a JSON value (number, true/false, string, array or object).
        struct PacketVars {
            std::vector<std::pair<std::string, std::string>> items;
            static std::string quote(const std::string& s);
            void add(const std::string& k, int v) { items.emplace_back(k, std::to_string(v)); }
            void add(const std::string& k, double v);
            void add(const std::string& k, bool v) { items.emplace_back(k, v ? "true" : "false"); }
            void add(const std::string& k, const std::string& v) { items.emplace_back(k, quote(v)); }
            void add(const std::string& k, const char* v) { items.emplace_back(k, quote(v)); }
            void raw(const std::string& k, const std::string& json) { items.emplace_back(k, json); }
            bool has(const std::string& k) const;
            std::string get(const std::string& k) const;
        };

        class PacketDecoder {
            public:
                struct packetDecoder_multiFramePacket {
                    bool isMFP = false;
                    int multiFramePacketDescriptor = 0;
                    bool isReady = false;
                    std::vector<uint8_t> packetData;
                    int firstPartCount = 0;
                };
                #define PACKETDECODER_PRESENTATION_IA5 0
                #define PACKETDECODER_PRESENTATION_ITA2 6
                #define PACKETDECODER_PRESENTATION_BINARY 7
                struct packetDecoder_payload {
                    int presentation = -1;
                    std::vector<uint8_t> data8Bit;
                };
                #define PACKETDECODER_DECODING_STAGE_NONE 0
                #define PACKETDECODER_DECODING_STAGE_PARTIAL 1
                #define PACKETDECODER_DECODING_STAGE_COMPLETE 2
                struct packetDecoder_result {
                    bool isDecodedPacket = false;
                    int frameNumber = 0;
                    std::chrono::time_point<std::chrono::high_resolution_clock> timestamp;
                    uint8_t packetDescriptor = 0;
                    int packetLength = 0;
                    int decodingStage = 0;
                    bool isCrc = false;
                    packetDecoder_payload payload;
                    PacketVars packetVars;
                    packetDecoder_multiFramePacket mfp;
                    // FOXSDR additions: where the packet sits in the frame, frame quality, and whether it was
                    // reassembled from a BD/BE multiframe chain (then it carries no CRC of its own to check)
                    int offset = 0;
                    int ber = 0;
                    bool isUncertain = false;
                    bool reassembled = false;
                };
                // FOXSDR: true while decoding a packet reassembled from BD/BE (its CRC bytes were never received)
                bool allowZeroCrc = false;
                packetDecoder_result basicDecode(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_27(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_2A(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_08(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_6C(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_7D(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_81(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_83(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_91(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_92(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_9A(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_A0(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_A3(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_A8(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_AA(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_AB(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_AC(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_AD(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_B1(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_B2(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_BD(decoder::Decoder::decoder_result inputFrame, int* pos);
                packetDecoder_result decode_BE(decoder::Decoder::decoder_result inputFrame, int* pos, packetDecoder_multiFramePacket* mfa);
            private:
                // 2-byte CRC for Inmarsat-C calculator.
                // Arithmetic is modulo 256.
                // Perform encoding on data + zeros in checksum field.
                // Perform checker on all data bytes including checksum.
                // CB2 forms last byte of the packet.
                //
                // How to implement this as a lookup?
                static int computeCRC(uint8_t decodedFrame[DESCRAMBLER_FRAME_LENGTH], int pos, int length);
                static std::string getSatName(int sat);
                static std::string getLesName(int sat, int lesId);
                static std::string getServiceCodeAndAddressName(int code);
                static std::string getPriority(int priority);
                static int getAddressLength(int messageType);
                static bool IsBinary(std::vector<uint8_t> data, bool checkAll = false);
                static void decodeEgc(decoder::Decoder::decoder_result& inputFrame, int* pos, packetDecoder_result& ret);
                static std::string getStations(uint8_t data[], int stationCount, int pos, int limit);
                static std::string getServices_short(uint8_t is8);
                static std::string hexBytes(const uint8_t* p, int n);
                static std::string getServices(int iss);
                static std::string getDescriptorAsText(uint8_t descriptor_b);
        };

        class Ita2Decoder {
            public:
                std::vector<uint8_t> decode(std::vector<uint8_t> data);
            private:
                //tables got from https://github.com/paulsmith/baudot
                const std::map<int, char> ita2_ltrs2ascii = {
                    {0x00, '\0'},	{0x01, 'E'},	{0x02, '\n'},	{0x03, 'A'},	{0x04, ' '},
                    {0x05, 'S'},	{0x06, 'I'},	{0x07, 'U'},	{0x08, '\r'},	{0x09, 'D'},
                    {0x0a, 'R'},	{0x0b, 'J'},	{0x0c, 'N'},	{0x0d, 'F'},	{0x0e, 'C'},
                    {0x0f, 'K'},	{0x10, 'T'},	{0x11, 'Z'},	{0x12, 'L'},	{0x13, 'W'},
                    {0x14, 'H'},	{0x15, 'Y'},	{0x16, 'P'},	{0x17, 'Q'},	{0x18, 'O'},
                    {0x19, 'B'},	{0x1a, 'G'},	{0x1c, 'M'},	{0x1d, 'X'},	{0x1e, 'V'}
                };
                const std::map<int, char> ita2_figs2ascii = {
                    {0x00, '\0'},	{0x01, '3'},	{0x02, '\n'},	{0x03, '-'},	{0x04, ' '},
                    {0x05, '\''},	{0x06, '8'},	{0x07, '7'},	{0x08, '\r'},	{0x09, 0x05},
                    {0x0a, '4'},	{0x0b, '\a'},	{0x0c, ','},	{0x0d, '!'},	{0x0e, ':'},
                    {0x0f, '('},	{0x10, '5'},	{0x11, '+'},	{0x12, ')'},	{0x13, '2'},
                    {0x14, '$'},	{0x15, '6'},	{0x16, '0'},	{0x17, '1'},	{0x18, '9'},
                    {0x19, '?'},	{0x1a, '&'},	{0x1c, '.'},	{0x1d, '/'},	{0x1e, ';'}
                };
        };

        class PacketDetector {
            public:
                PacketDetector();
                std::vector<PacketDecoder::packetDecoder_result> process(decoder::Decoder::decoder_result inputFrame);
                PacketDecoder::packetDecoder_result detect(decoder::Decoder::decoder_result inputFrame, int* pos);
            private:
                PacketDecoder::packetDecoder_multiFramePacket multiStreamFrameElements;
                PacketDecoder* packetDecoder;
                Ita2Decoder* ita2Decoder;
        };

        class INMARSATC_EXPORT FrameParser {
            public:
                struct frameParser_result {
                    PacketDecoder::packetDecoder_result decoding_result;
                };
                FrameParser();
                std::vector<frameParser_result> parseFrame(decoder::Decoder::decoder_result inputFrame);
            private:
                PacketDetector* packetDetector;
        };
    }
}

#endif // INMARSATC_PARSER_H
