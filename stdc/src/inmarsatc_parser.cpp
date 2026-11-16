/*
 * FoxSDR "stdc" GPL helper: packet parser part of cropinghigh/inmarsatc (commit cda1242), GPL-3.0.
 * Based on Scytale-C (Copyright microp11 2017, GPL-3.0). Changes against upstream are marked "FOXSDR:" and
 * listed in ../UPSTREAM.md. Privacy: the parser keeps no message text and no EGC payload (see UPSTREAM.md).
 */
#include <inmarsatc_parser.h>
#include <sstream>

namespace inmarsatc {
    namespace frameParser {
        //START FOXSDR PacketVars
        std::string PacketVars::quote(const std::string& in) {
            std::string o = "\"";
            for(size_t i = 0; i < in.size(); i++) {
                unsigned char c = (unsigned char)in[i];
                if(c == '"') { o += "\\\""; }
                else if(c == '\\') { o += "\\\\"; }
                else if(c < 0x20 || c >= 0x7F) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
                else { o += (char)c; }
            }
            return o + "\"";
        }
        void PacketVars::add(const std::string& k, double v) {
            char b[64];
            std::snprintf(b, sizeof(b), "%.4f", v);
            items.emplace_back(k, b);
        }
        bool PacketVars::has(const std::string& k) const {
            for(size_t i = 0; i < items.size(); i++) { if(items[i].first == k) { return true; } }
            return false;
        }
        std::string PacketVars::get(const std::string& k) const {
            for(size_t i = 0; i < items.size(); i++) { if(items[i].first == k) { return items[i].second; } }
            return std::string();
        }
        //END FOXSDR PacketVars

        //START CLASS PacketDecoder
        PacketDecoder::packetDecoder_result PacketDecoder::basicDecode(decoder::Decoder::decoder_result inputFrame, int* pos) {
            packetDecoder_result ret;
            ret.decodingStage = PACKETDECODER_DECODING_STAGE_NONE;
            // As a safe precaution, in case we cannot correctly ascertain the packet length,
            // setting the length this way will allow us to discard the whole remaining frame
            // and avoid an infinite loop.
            ret.packetLength = inputFrame.length - *pos;
            // Packet descriptor
            ret.packetDescriptor = inputFrame.decodedFrame[*pos];
            /// There are 2 variations of packet descriptor
            /// Short packet descriptor and Medium packet descriptor
            /// They give us the packet lengths
            if (ret.packetDescriptor >> 7 == 0) {
                /// Short packet descriptor
                /// The packet length including CRC does not include byte 0, we add 1
                ret.packetLength = (ret.packetDescriptor & 0x0F) + 1;
            } else if (ret.packetDescriptor >> 6 == 0x02) {
                /// Medium packet descriptor
                /// The packet length including CRC does not include the first 2 bytes, we add 2
                ret.packetLength = inputFrame.decodedFrame[*pos + 1] + 2;
            }
            // At this stage we do not know for sure if the CRC is correct.
            /// We compute the 2-byte CRC and compare with the packet 2-byte CRC
            /// The packet 2-byte CRC position is given by the packet descriptor
            // FOXSDR: a packet that runs past the end of the frame (or is too short to hold a CRC) cannot be
            // checked; upstream read the CRC bytes from outside the frame (before it, for length 1) and could
            // accept it.
            bool fits = ret.packetLength >= 3 && *pos + ret.packetLength <= inputFrame.length;
            int packetCRC = -1;
            int computedCRC = -2;
            if (fits) {
                packetCRC = (inputFrame.decodedFrame[*pos + ret.packetLength - 2] << 8) | inputFrame.decodedFrame[*pos + ret.packetLength - 1];
                computedCRC = computeCRC(inputFrame.decodedFrame, *pos, ret.packetLength);
            }
            // Upstream: "Added a check to zero. The BD-BE packet content is a packet that does not have a CRC.
            // This is a workaround to avoid computing the CRC for the content. TODO".
            // FOXSDR: the zero exception now applies only to a packet reassembled from BD/BE (allowZeroCrc), where
            // the CRC bytes are never received; upstream accepted any packet whose CRC field was 00 00.
            ret.isCrc = fits && (packetCRC == computedCRC || (allowZeroCrc && packetCRC == 0));
            ret.frameNumber = inputFrame.frameNumber;
            ret.offset = *pos;
            ret.ber = inputFrame.BER;
            ret.isUncertain = inputFrame.isUncertain;
            ret.reassembled = allowZeroCrc;
            ret.packetVars.add("packetDescriptorText", getDescriptorAsText(ret.packetDescriptor));
            ret.timestamp = inputFrame.timestamp;
            return ret;
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_27(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            ret.decodingStage = PACKETDECODER_DECODING_STAGE_COMPLETE;
            int mesId = inputFrame.decodedFrame[*pos + 1] << 16 | inputFrame.decodedFrame[*pos + 1 + 1] << 8 | inputFrame.decodedFrame[*pos + 1 + 2];
            int sat = inputFrame.decodedFrame[*pos + 4] >> 6 & 0x03;
            std::string satName = getSatName(sat);
            int lesId = inputFrame.decodedFrame[*pos + 4] & 0x3F;
            std::string lesName = getLesName(sat, lesId);
            int logicalChannelNo = inputFrame.decodedFrame[*pos + 5];
            ret.packetVars.add("mesId", mesId);
            ret.packetVars.add("sat", sat);
            ret.packetVars.add("satName", satName);
            ret.packetVars.add("lesId", lesId);
            ret.packetVars.add("lesName", lesName);
            ret.packetVars.add("logicalChannelNo", logicalChannelNo);
            return ret;
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_2A(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            ret.decodingStage = PACKETDECODER_DECODING_STAGE_PARTIAL;
            int mesId = inputFrame.decodedFrame[*pos + 1] << 16 | inputFrame.decodedFrame[*pos + 1 + 1] << 8 | inputFrame.decodedFrame[*pos + 1 + 2];
            int sat = inputFrame.decodedFrame[*pos + 4] >> 6 & 0x03;
            std::string satName = getSatName(sat);
            int lesId = inputFrame.decodedFrame[*pos + 4] & 0x3F;
            std::string lesName = getLesName(sat, lesId);
            int logicalChannelNo = inputFrame.decodedFrame[*pos + 5];
            std::string unknown1Hex = hexBytes(&inputFrame.decodedFrame[*pos + 6], 3);
            ret.packetVars.add("mesId", mesId);
            ret.packetVars.add("sat", sat);
            ret.packetVars.add("satName", satName);
            ret.packetVars.add("lesId", lesId);
            ret.packetVars.add("lesName", lesName);
            ret.packetVars.add("logicalChannelNo", logicalChannelNo);
            ret.packetVars.add("unknown1Hex", unknown1Hex);
            return ret;
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_08(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            ret.decodingStage = PACKETDECODER_DECODING_STAGE_COMPLETE;
            int sat = inputFrame.decodedFrame[*pos + 1] >> 6 & 0x03;
            std::string satName = getSatName(sat);
            int lesId = inputFrame.decodedFrame[*pos + 1] & 0x3F;
            std::string lesName = getLesName(sat, lesId);
            int logicalChannelNo = inputFrame.decodedFrame[*pos + 2];
            double uplinkChannelMhz = ((inputFrame.decodedFrame[*pos + 3] << 8 | (uint16_t)inputFrame.decodedFrame[*pos + 3 + 1]) - 6000) * 0.0025 + 1626.5;
            ret.packetVars.add("sat", sat);
            ret.packetVars.add("satName", satName);
            ret.packetVars.add("lesId", lesId);
            ret.packetVars.add("lesName", lesName);
            ret.packetVars.add("logicalChannelNo", logicalChannelNo);
            ret.packetVars.add("uplinkChannelMhz", uplinkChannelMhz);
            return ret;
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_6C(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            ret.decodingStage = PACKETDECODER_DECODING_STAGE_COMPLETE;
            //The second byte in 6C packets is the services byte.
            //Identical to the first byte of the services bytes of the 7D packet.
            int is8 = inputFrame.decodedFrame[*pos + 1];
            std::string services = getServices_short(is8);
            double uplinkChannelMhz = ((inputFrame.decodedFrame[*pos + 2] << 8 | (uint16_t)inputFrame.decodedFrame[*pos + 2 + 1]) - 6000) * 0.0025 + 1626.5;
            int tdmslots_int[28];
            int j = 0;
            for (int i = 0; i < 28; i += 4) {
                tdmslots_int[i] = inputFrame.decodedFrame[*pos + 4 + j] >> 6;
                tdmslots_int[i + 1] = inputFrame.decodedFrame[*pos + 4 + j] >> 4 & 3;
                tdmslots_int[i + 2] = inputFrame.decodedFrame[*pos + 4 + j] >> 2 & 3;
                tdmslots_int[i + 3] = inputFrame.decodedFrame[*pos + 4 + j] & 3;
                j++;
            }
            // FOXSDR: upstream made one "slot: value" text per line; here a JSON array of the 28 slot values
            std::string tdmSlots = "[";
            for(int i = 0; i < 28; i++) {
                tdmSlots += (i ? "," : "") + std::to_string(tdmslots_int[i]);
            }
            tdmSlots += "]";
            ret.packetVars.raw("services", services);
            ret.packetVars.add("uplinkChannelMhz", uplinkChannelMhz);
            ret.packetVars.raw("tdmSlots", tdmSlots);
            return ret;
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_7D(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            ret.decodingStage = PACKETDECODER_DECODING_STAGE_COMPLETE;
            int networkVersion = inputFrame.decodedFrame[*pos + 1];
            ret.frameNumber = inputFrame.decodedFrame[*pos + 2] << 8 | inputFrame.decodedFrame[*pos + 3];
            double timestamp_seconds_d = ret.frameNumber * 8.64;
            int timestamp_hours = floor(timestamp_seconds_d/3600.0);
            int timestamp_min = floor((((int)timestamp_seconds_d)%3600)/60.0);
            int timestamp_sec = ((int)timestamp_seconds_d)%60;
            int timestamp_msec = (((int)(timestamp_seconds_d*1000))%1000);
            std::string timestamp_str = std::to_string(timestamp_hours) + ":" + std::to_string(timestamp_min) + ":" + std::to_string(timestamp_sec) + "." + std::to_string(timestamp_msec);
            int signallingChannel = inputFrame.decodedFrame[*pos + 4] >> 2;
            int count = (inputFrame.decodedFrame[*pos + 5] >> 4 & 0x0F) * 0x02;
            uint8_t channelType = inputFrame.decodedFrame[*pos + 6] >> 0x05;
            std::string channelTypeName;
            // FOXSDR: upstream's switch had no break statements, so every packet was named "Reserved"
            switch (channelType) {
                case 1:
                    channelTypeName = "NCS";
                    break;
                case 2:
                    channelTypeName = "LES TDM";
                    break;
                case 3:
                    channelTypeName = "Joint NCS and TDM";
                    break;
                case 4:
                    channelTypeName = "ST-BY NCS";
                    break;
                default:
                    channelTypeName = "Reserved";
                    break;
            }
            int local = inputFrame.decodedFrame[*pos + 6] >> 2 & 0x07;
            int sat = inputFrame.decodedFrame[*pos + 7] >> 6 & 0x03;
            std::string satName = getSatName(sat);
            int lesId = inputFrame.decodedFrame[*pos + 7] & 0x3F;
            std::string lesName = getLesName(sat, lesId);
            uint8_t status_b = inputFrame.decodedFrame[*pos + 8];
            std::string status = "{";
            status += std::string("\"Bauds600\":") + (((status_b & 0x80) >> 7 == 1) ? "true" : "false");
            status += std::string(",\"Operational\":") + (((status_b & 0x40) >> 6 == 1) ? "true" : "false");
            status += std::string(",\"InService\":") + (((status_b & 0x20) >> 5 == 1) ? "true" : "false");
            status += std::string(",\"Clear\":") + (((status_b & 0x10) >> 4 == 1) ? "true" : "false");
            status += std::string(",\"LinksOpen\":") + (((status_b & 0x08) >> 3 == 1) ? "true" : "false");
            status += "}";
            int iss = inputFrame.decodedFrame[*pos + 9] << 8 | inputFrame.decodedFrame[*pos + 10];
            std::string services = getServices(iss);
            int randomInterval = inputFrame.decodedFrame[*pos + 11];
            ret.packetVars.add("networkVersion", networkVersion);
            ret.packetVars.add("sat", sat);
            ret.packetVars.add("satName", satName);
            ret.packetVars.add("lesId", lesId);
            ret.packetVars.add("lesName", lesName);
            ret.packetVars.add("signallingChannel", signallingChannel);
            ret.packetVars.add("count", count);
            ret.packetVars.add("channelType", (int)channelType);
            ret.packetVars.add("channelTypeName", channelTypeName);
            ret.packetVars.add("local", local);
            ret.packetVars.raw("status", status);
            ret.packetVars.raw("services", services);
            ret.packetVars.add("randomInterval", randomInterval);
            ret.packetVars.add("timestamp_str", timestamp_str);
            return ret;
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_81(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            ret.decodingStage = PACKETDECODER_DECODING_STAGE_PARTIAL;
            // FOXSDR: upstream's third byte was [pos+1+2] = [pos+3], i.e. the middle byte twice; the neighbouring
            // packets (83, A3, A8) and the sat byte at +5 put the identity at +2..+4
            int mesId = inputFrame.decodedFrame[*pos + 2] << 16 | inputFrame.decodedFrame[*pos + 2 + 1] << 8 | inputFrame.decodedFrame[*pos + 2 + 2];
            int sat = inputFrame.decodedFrame[*pos + 5] >> 6 & 0x03;
            std::string satName = getSatName(sat);
            int lesId = inputFrame.decodedFrame[*pos + 5] & 0x3F;
            std::string lesName = getLesName(sat, lesId);
            int logicalChannelNo = inputFrame.decodedFrame[*pos + 9];
            // FOXSDR: upstream added 15305 here; every other downlink channel in the library (83, 92, AB) adds 1530.5
            double downlinkChannelMhz = ((inputFrame.decodedFrame[*pos + 6] << 8 | inputFrame.decodedFrame[*pos + 6 + 1]) - 8000) * 0.0025 + 1530.5;
            int presentation = inputFrame.decodedFrame[*pos + 14];
            // FOXSDR: upstream reused one ostringstream and os.clear() does not empty it, so unknown2Hex and
            // unknown3Hex repeated the earlier fields; each is now just its own bytes
            std::string unknown1Hex = hexBytes(&inputFrame.decodedFrame[*pos + 8], 1);
            std::string unknown2Hex = hexBytes(&inputFrame.decodedFrame[*pos + 10], 4);
            std::string unknown3Hex = hexBytes(&inputFrame.decodedFrame[*pos + 15], 2);
            ret.packetVars.add("mesId", mesId);
            ret.packetVars.add("sat", sat);
            ret.packetVars.add("satName", satName);
            ret.packetVars.add("lesId", lesId);
            ret.packetVars.add("lesName", lesName);
            ret.packetVars.add("logicalChannelNo", logicalChannelNo);
            ret.packetVars.add("downlinkChannelMhz", downlinkChannelMhz);
            ret.packetVars.add("presentation", presentation);
            ret.packetVars.add("unknown1Hex", unknown1Hex);
            ret.packetVars.add("unknown2Hex", unknown2Hex);
            ret.packetVars.add("unknown3Hex", unknown3Hex);
            return ret;
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_83(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            ret.decodingStage = PACKETDECODER_DECODING_STAGE_PARTIAL;
            int mesId = inputFrame.decodedFrame[*pos + 2] << 16 | inputFrame.decodedFrame[*pos + 2 + 1] << 8 | inputFrame.decodedFrame[*pos + 2 + 2];
            int sat = inputFrame.decodedFrame[*pos + 5] >> 6 & 0x03;
            std::string satName = getSatName(sat);
            int lesId = inputFrame.decodedFrame[*pos + 5] & 0x3F;
            std::string lesName = getLesName(sat, lesId);
            uint8_t status_bits = inputFrame.decodedFrame[*pos + 6];
            int logicalChannelNo = inputFrame.decodedFrame[*pos + 7];
            int frameLength = inputFrame.decodedFrame[*pos + 8];
            int duration = inputFrame.decodedFrame[*pos + 9];
            double downlinkChannelMhz = ((inputFrame.decodedFrame[*pos + 10] << 8 | inputFrame.decodedFrame[*pos + 10 + 1]) - 8000) * 0.0025 + 1530.5;
            double uplinkChannelMhz = ((inputFrame.decodedFrame[*pos + 12] << 8 | inputFrame.decodedFrame[*pos + 12 + 1]) - 6000) * 0.0025 + 1626.5;
            int frameOffset = inputFrame.decodedFrame[*pos + 14];
            uint8_t packetDescriptor1 = inputFrame.decodedFrame[*pos + 15];
            ret.packetVars.add("mesId", mesId);
            ret.packetVars.add("sat", sat);
            ret.packetVars.add("satName", satName);
            ret.packetVars.add("lesId", lesId);
            ret.packetVars.add("lesName", lesName);
            ret.packetVars.add("status_bits", status_bits);
            ret.packetVars.add("logicalChannelNo", logicalChannelNo);
            ret.packetVars.add("frameLength", frameLength);
            ret.packetVars.add("duration", duration);
            ret.packetVars.add("downlinkChannelMhz", downlinkChannelMhz);
            ret.packetVars.add("uplinkChannelMhz", uplinkChannelMhz);
            ret.packetVars.add("frameOffset", frameOffset);
            ret.packetVars.add("packetDescriptor1", packetDescriptor1);
            return ret;
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_91(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            return ret; //not implemented yet
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_92(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            ret.decodingStage = PACKETDECODER_DECODING_STAGE_COMPLETE;
            int loginAckLength = inputFrame.decodedFrame[*pos + 1];
            std::string les = hexBytes(&inputFrame.decodedFrame[*pos + 2], 3);
            double downlinkChannelMhz = ((inputFrame.decodedFrame[*pos + 5] << 8 | inputFrame.decodedFrame[*pos + 5 + 1]) - 8000) * 0.0025 + 1530.5;
            std::string stationStartHex = hexBytes(&inputFrame.decodedFrame[*pos + 7], 1);
            ret.packetVars.add("loginAckLength", loginAckLength);
            ret.packetVars.add("downlinkChannelMhz", downlinkChannelMhz);
            ret.packetVars.add("les", les);
            ret.packetVars.add("stationStartHex", stationStartHex);
            if(loginAckLength > 7) {
                //stations
                int stationCount = inputFrame.decodedFrame[*pos + 8];
                std::string stations = getStations(inputFrame.decodedFrame, stationCount, *pos + 9, *pos + ret.packetLength - 2);
                ret.packetVars.add("stationCount", stationCount);
                ret.packetVars.raw("stations", stations);
            }
            return ret;
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_9A(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            return ret; //not implemented yet
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_A0(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            return ret; //not implemented yet
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_A3(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            ret.decodingStage = PACKETDECODER_DECODING_STAGE_PARTIAL;
            int mesId = inputFrame.decodedFrame[*pos + 2] << 16 | inputFrame.decodedFrame[*pos + 2 + 1] << 8 | inputFrame.decodedFrame[*pos + 2 + 2];
            int sat = inputFrame.decodedFrame[*pos + 5] >> 6 & 0x03;
            std::string satName = getSatName(sat);
            int lesId = inputFrame.decodedFrame[*pos + 5] & 0x3F;
            std::string lesName = getLesName(sat, lesId);
            // FOXSDR (privacy): the individual poll can carry a short message for one terminal. Upstream put the
            // text in "shortMessage" and the bytes before it in "unknown1Hex"; neither is kept. Only the number of
            // message characters is reported.
            int shortMessageChars = 0;
            if(ret.packetLength >= 38) {
                shortMessageChars = ret.packetLength - 2 - 13;
            }
            ret.packetVars.add("mesId", mesId);
            ret.packetVars.add("sat", sat);
            ret.packetVars.add("satName", satName);
            ret.packetVars.add("lesId", lesId);
            ret.packetVars.add("lesName", lesName);
            ret.packetVars.add("shortMessageChars", shortMessageChars);
            return ret;
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_A8(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            ret.decodingStage = PACKETDECODER_DECODING_STAGE_PARTIAL;
            int mesId = inputFrame.decodedFrame[*pos + 2] << 16 | inputFrame.decodedFrame[*pos + 2 + 1] << 8 | inputFrame.decodedFrame[*pos + 2 + 2];
            int sat = inputFrame.decodedFrame[*pos + 5] >> 6 & 0x03;
            std::string satName = getSatName(sat);
            int lesId = inputFrame.decodedFrame[*pos + 5] & 0x3F;
            std::string lesName = getLesName(sat, lesId);
            std::string unknown1Hex = hexBytes(&inputFrame.decodedFrame[*pos + 6], 3);
            int shortMessageLength = inputFrame.decodedFrame[*pos + 9];
            std::string unknown2Hex = hexBytes(&inputFrame.decodedFrame[*pos + 10], 1);
            // FOXSDR (privacy): upstream put the message text of a confirmation in "shortMessage"; it is not kept.
            ret.packetVars.add("mesId", mesId);
            ret.packetVars.add("sat", sat);
            ret.packetVars.add("satName", satName);
            ret.packetVars.add("lesId", lesId);
            ret.packetVars.add("lesName", lesName);
            ret.packetVars.add("unknown1Hex", unknown1Hex);
            ret.packetVars.add("shortMessageLength", shortMessageLength);
            ret.packetVars.add("unknown2Hex", unknown2Hex);
            return ret;
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_AA(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            ret.decodingStage = PACKETDECODER_DECODING_STAGE_COMPLETE;
            int sat = inputFrame.decodedFrame[*pos + 2] >> 6 & 0x03;
            std::string satName = getSatName(sat);
            int lesId = inputFrame.decodedFrame[*pos + 2] & 0x3F;
            std::string lesName = getLesName(sat, lesId);
            int logicalChannelNo = inputFrame.decodedFrame[*pos + 3];
            int packetNo = inputFrame.decodedFrame[*pos + 4];
            // FOXSDR (privacy): upstream copied the packet's data bytes into ret.payload; that is message content for
            // one terminal, so only its size is kept.
            int payloadBytes = ret.packetLength - 2 - 5;
            if(payloadBytes < 0) {
                payloadBytes = 0;
            }
            ret.packetVars.add("sat", sat);
            ret.packetVars.add("satName", satName);
            ret.packetVars.add("lesId", lesId);
            ret.packetVars.add("lesName", lesName);
            ret.packetVars.add("logicalChannelNo", logicalChannelNo);
            ret.packetVars.add("packetNo", packetNo);
            ret.packetVars.add("payloadBytes", payloadBytes);
            return ret;
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_AB(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            ret.decodingStage = PACKETDECODER_DECODING_STAGE_COMPLETE;
            int lesListLength = inputFrame.decodedFrame[*pos + 1];
            std::string stationStartHex = hexBytes(&inputFrame.decodedFrame[*pos + 2], 1);
            int stationCount = inputFrame.decodedFrame[*pos + 3];
            std::string stations = getStations(inputFrame.decodedFrame, stationCount, *pos + 4, *pos + ret.packetLength - 2);
            ret.packetVars.add("lesListLength", lesListLength);
            ret.packetVars.add("stationStartHex", stationStartHex);
            ret.packetVars.raw("stations", stations);
            ret.packetVars.add("stationCount", stationCount);
            return ret;
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_AC(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            return ret; //not implemented yet
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_AD(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            return ret; //not implemented yet
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_B1(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            ret.decodingStage = PACKETDECODER_DECODING_STAGE_PARTIAL;
            decodeEgc(inputFrame, pos, ret);
            return ret;
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_B2(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            ret.decodingStage = PACKETDECODER_DECODING_STAGE_PARTIAL;
            decodeEgc(inputFrame, pos, ret);
            return ret;
        }
        // FOXSDR: upstream had the same 40 lines in decode_B1 and decode_B2; shared here
        void PacketDecoder::decodeEgc(decoder::Decoder::decoder_result& inputFrame, int* pos, packetDecoder_result& ret) {
            int messageType = inputFrame.decodedFrame[*pos + 2];
            std::string serviceCodeAndAddressName = getServiceCodeAndAddressName(messageType);
            int continuation = (inputFrame.decodedFrame[*pos + 3] & 0x80) >> 7;
            int priority = (inputFrame.decodedFrame[*pos + 3] & 0x60) >> 5;
            std::string priorityText = getPriority(priority);
            bool isDistress = priority == 3;
            int repetition = inputFrame.decodedFrame[*pos + 3] & 0x1F;
            int messageId = inputFrame.decodedFrame[*pos + 4] << 8 | inputFrame.decodedFrame[*pos + 5];
            int packetNo = inputFrame.decodedFrame[*pos + 6];
            bool isNewPayload = packetNo == 1;
            ret.payload.presentation = inputFrame.decodedFrame[*pos + 7];
            ret.packetVars.add("messageType", messageType);
            ret.packetVars.add("serviceCodeAndAddressName", serviceCodeAndAddressName);
            ret.packetVars.add("continuation", continuation);
            ret.packetVars.add("priority", priority);
            ret.packetVars.add("priorityText", priorityText);
            ret.packetVars.add("isDistress", isDistress);
            ret.packetVars.add("repetition", repetition);
            ret.packetVars.add("messageId", messageId);
            ret.packetVars.add("packetNo", packetNo);
            ret.packetVars.add("isNewPayload", isNewPayload);
            ret.packetVars.add("presentation", ret.payload.presentation);
            int addressLength = getAddressLength(messageType);
            //NAV/MET coordinator... area... TODO
            if(*pos + 8 + addressLength >= inputFrame.length) {
                return;
            }
            // FOXSDR: upstream used a variable-length array here
            uint8_t address[8] = {};
            std::copy(&inputFrame.decodedFrame[*pos+8], &inputFrame.decodedFrame[*pos+8+addressLength], address);
            std::string addressHex = hexBytes(&address[1], addressLength - 1);
            // FOXSDR (privacy/scope): upstream copied the broadcast text into ret.payload.data8Bit. EGC text is decoded
            // by FoxSDR's own EGC plugin; here only the size is reported.
            int payloadLength = ret.packetLength - 2 - 8 - addressLength;
            if(payloadLength < 0) {
                payloadLength = 0;
            }
            ret.packetVars.add("addressHex", addressHex);
            ret.packetVars.add("payloadBytes", payloadLength);
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_BD(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            ret.decodingStage = PACKETDECODER_DECODING_STAGE_COMPLETE;
            int multiframePacketLength = 0;
            int multiframePacketDescriptor = inputFrame.decodedFrame[*pos + 2] & 0xFF;
            std::string multiframePacketDescriptorHex = hexBytes(&inputFrame.decodedFrame[*pos + 2], 1);
            if(multiframePacketDescriptor >> 7 == 0) {
                // Short packet descriptor
                // The packet length including CRC does not include byte 0, we add 1
                multiframePacketLength = (multiframePacketDescriptor & 0x0F) + 1;
            } else if(multiframePacketDescriptor >> 6 == 0x02) {
                /// Medium packet descriptor
                /// The packet length including CRC does not include the first 2 bytes, we add 2
                multiframePacketLength = inputFrame.decodedFrame[*pos + 3] + 2;
            }
            //compose encapsulated packet, length includes the CRC for the composed packet
            ret.mfp.packetData.assign(multiframePacketLength, 0);
            //set the length of payload data to be used for the new packet
            //the payload starts at index + 2 and does not include the CRC
            ret.mfp.firstPartCount = ret.packetLength - 2 - 2;
            // FOXSDR: upstream copied firstPartCount bytes into a vector of multiframePacketLength bytes without
            // comparing the two (heap overflow for an inner length of 0 or a short inner packet)
            if(ret.mfp.firstPartCount > multiframePacketLength) {
                ret.mfp.firstPartCount = multiframePacketLength;
            }
            if(ret.mfp.firstPartCount < 0) {
                ret.mfp.firstPartCount = 0;
            }
            ret.mfp.isMFP = true;
            ret.mfp.multiFramePacketDescriptor = multiframePacketDescriptor;
            std::copy(&inputFrame.decodedFrame[*pos + 2], &inputFrame.decodedFrame[*pos + 2 + ret.mfp.firstPartCount], ret.mfp.packetData.begin());
            ret.packetVars.add("multiframePacketDescriptor", "0x" + multiframePacketDescriptorHex);
            ret.packetVars.add("multiframePacketLength", multiframePacketLength);
            return ret;
        }
        PacketDecoder::packetDecoder_result PacketDecoder::decode_BE(decoder::Decoder::decoder_result inputFrame, int* pos, PacketDecoder::packetDecoder_multiFramePacket* mfa) {
            PacketDecoder::packetDecoder_result ret = basicDecode(inputFrame, pos);
            if(!ret.isCrc) {
                return ret;
            }
            ret.decodingStage = PACKETDECODER_DECODING_STAGE_COMPLETE;
            //2 for CRC
            //2 for starting of packet
            int j = *pos + 2;
            int actualLength = 0;
            // FOXSDR (privacy): upstream also copied the bytes into ret.payload.data8Bit; only the reassembly buffer
            // keeps them (it is decoded, and the decoded packet is filtered like any other).
            for(int i = 0; j < *pos + ret.packetLength - 2; i++) {
                if((int)mfa->packetData.size() > i + mfa->firstPartCount) {
                    mfa->packetData[i + mfa->firstPartCount] = (inputFrame.decodedFrame[j]);
                }
                j++;
                actualLength++;
            }
            mfa->firstPartCount += actualLength;
            mfa->isReady = mfa->isMFP && mfa->firstPartCount >= (int)mfa->packetData.size() - 2;
            ret.packetVars.add("continuedBytes", actualLength);
            return ret;
        }
        int PacketDecoder::computeCRC(uint8_t *decodedFrame, int pos, int length) {
            short C0 = 0;
            short C1 = 0;
            uint8_t CB1;
            uint8_t CB2;
            uint8_t B;
            int i = 0;
            while (i < length) {
                if (i < length - 2) {
                    B = decodedFrame[pos + i];
                } else {
                    B = 0;
                }
                C0 += B;
                C1 += C0;
                i++;
            }
            CB1 = (uint8_t)(C0 - C1);
            CB2 = (uint8_t)(C1 - 2 * C0);
            return (CB1 << 8) | CB2;
        }
        std::string PacketDecoder::getSatName(int sat) {
            switch (sat) {
                case 0:
                    return "Atlantic Ocean Region West (AOR-W)";
                case 1:
                    return "Atlantic Ocean Region East (AOR-E)";
                case 2:
                    return "Pacific Ocean Region (POR)";
                case 3:
                    return "Indian Ocean Region (IOR)";
                case 9:
                    return "All Ocean Regions Covered by the LES";
                default:
                    return "Unknown";
            }
        }
        // FOXSDR: the case labels below were written with leading zeros (001, 044, 012 ...), which C++ reads as OCTAL
        // (044 is 36, not 44, so the NCS was never named). They are plain decimal numbers here.
        std::string PacketDecoder::getLesName(int sat, int lesId) {
            int value = lesId + sat * 100;
            std::string name;
            switch (value) {
                case 1:
                case 101:
                case 201:
                case 301:
                    name = "Vizada-Telenor, USA";
                    break;

                case 2:
                case 102:
                case 302:
                    name = "Stratos Global (Burum-2), Netherlands";
                    break;

                case 202:
                    name = "Stratos Global (Aukland), New Zealand";
                    break;

                case 3:
                case 103:
                case 203:
                case 303:
                    name = "KDDI Japan";
                    break;

                case 4:
                case 104:
                case 204:
                case 304:
                    name = "Vizada-Telenor, Norway";
                    break;

                case 44:
                case 144:
                case 244:
                case 344:
                    name = "NCS";
                    break;

                case 105:
                case 335:
                    name = "Telecom, Italia";
                    break;

                case 305:
                case 120:
                    name = "OTESTAT, Greece";
                    break;

                case 306:
                    name = "VSNL, India";
                    break;

                case 110:
                case 310:
                    name = "Turk Telecom, Turkey";
                    break;

                case 211:
                case 311:
                    name = "Beijing MCN, China";
                    break;

                case 12:
                case 112:
                case 212:
                case 312:
                    name = "Stratos Global (Burum), Netherlands";
                    break;

                case 114:
                    name = "Embratel, Brazil";
                    break;

                case 116:
                case 316:
                    name = "Telekomunikacja Polska, Poland";
                    break;

                case 117:
                case 217:
                case 317:
                    name = "Morsviazsputnik, Russia";
                    break;

                case 21:
                case 121:
                case 221:
                case 321:
                    name = "Vizada (FT), France";
                    break;

                case 127:
                case 327:
                    name = "Bezeq, Israel";
                    break;

                case 210:
                case 328:
                    name = "Singapore Telecom, Singapore";
                    break;

                case 330:
                    name = "VISHIPEL, Vietnam";
                    break;

                default:
                    name = "Unknown";
                    break;
            }
            return std::to_string(value) + ", " + name;
        }
        std::string PacketDecoder::getServiceCodeAndAddressName(int code) {
            switch (code) {
                case 0x00:
                    return "System, All ships (general call)";
                case 0x02:
                    return "FleetNET, Group Call";
                case 0x04:
                    return "SafetyNET, Navigational, Meteorological or Piracy Warning to a Rectangular Area";
                case 0x11:
                    return "System, Inmarsat System Message";
                case 0x13:
                    return "SafetyNET, Navigational, Meteorological or Piracy Coastal Warning";
                case 0x14:
                    return "SafetyNET, Shore-to-Ship Distress Alert to Circular Area";
                case 0x23:
                    return "System, EGC System Message";
                case 0x24:
                    return "SafetyNET, Navigational, Meteorological or Piracy Warning to a Circular Area";
                case 0x31:
                    return "SafetyNET, NAVAREA/METAREA Warning, MET Forecast or Piracy Warning to NAVAREA/METAREA";
                case 0x33:
                    return "System, Download Group Identity";
                case 0x34:
                    return "SafetyNET, SAR Coordination to a Rectangular Area";
                case 0x44:
                    return "SafetyNET, SAR Coordination to a Circular Area";
                case 0x72:
                    return "FleetNET, Chart Correction Service";
                case 0x73:
                    return "SafetyNET, Chart Correction Service for Fixed Areas";
                default:
                    return "Unknown";
            }
        }
        std::string PacketDecoder::getPriority(int priority) {
            switch (priority) {
                case -1:
                    return "Message";
                case 0:
                    return "Routine";
                case 1:
                    return "Safety";
                case 2:
                    return "Urgency";
                case 3:
                    return "Distress";
                default:
                    return "Unknown";
            }
        }
        int PacketDecoder::getAddressLength(int messageType) {
            switch (messageType) {
                case 0x00:
                    return 3;
                case 0x11:
                case 0x31:
                    return 4;
                case 0x02:
                case 0x72:
                    return 5;
                case 0x13:
                case 0x23:
                case 0x33:
                case 0x73:
                    return 6;
                case 0x04:
                case 0x14:
                case 0x24:
                case 0x34:
                case 0x44:
                    return 7;
                default:
                    return 3;
            }
        }
        bool PacketDecoder::IsBinary(std::vector<uint8_t> data, bool checkAll) {
            bool isBinary = false;
            //try first 13 characters if not check all
            int check = 13;
            if (!checkAll) {
                check = std::min(check, (int)data.size()-2);
            } else {
                check = data.size();
            }
            for (int i = 0; i < check; i++) {
                char chr = (char)(data[i] & 0x7F);
                switch (chr) {
                    case 0x01:
                    case 0x05:
                    case 0x06:
                    case 0x07:
                    case 0x08:
                    case 0x0B:
                    case 0x0C:
                    case 0x0E:
                    case 0x0F:
                    case 0x10:
                    case 0x11:
                    case 0x12:
                    case 0x13:
                    case 0x14:
                    case 0x15:
                    case 0x16:
                    case 0x17:
                    case 0x18:
                    case 0x19:
                    case 0x1A:
                    case 0x1C:
                    case 0x1D:
                    case 0x1E:
                    case 0x1F:
                    //case 0xA4:
                    case '$':
                        isBinary = true;
                        break;
                }
            }
            return isBinary;
        }
        std::string PacketDecoder::hexBytes(const uint8_t* p, int n) {
            std::string o;
            char b[4];
            for(int i = 0; i < n; i++) {
                std::snprintf(b, sizeof(b), "%02x", p[i]);
                o += b;
            }
            return o;
        }
        // FOXSDR: upstream built one text blob; this builds a JSON array of station objects, never reads past the
        // packet (limit = index of the CRC), and reads the downlink channel from bytes j+4 and j+5 (upstream used
        // data[j+4] for both halves).
        std::string PacketDecoder::getStations(uint8_t data[], int stationCount, int pos, int limit) {
            std::string stations = "[";
            int j = pos;
            for(int i=0; i < stationCount && j + 6 <= limit; i++) {
                int sat = data[j] >> 6 & 0x03;
                int lesId = data[j] & 0x3F;
                int servicesStart = data[j + 1];
                int iss = data[j + 2] << 8 | data[j + 3];
                double downlinkChannelMhz = ((data[j + 4] << 8 | data[j + 5]) - 8000) * 0.0025 + 1530.5;
                char dl[32];
                std::snprintf(dl, sizeof(dl), "%.4f", downlinkChannelMhz);
                stations += std::string(i ? "," : "") + "{\"station\":" + std::to_string(i) +
                            ",\"sat\":" + std::to_string(sat) +
                            ",\"satName\":" + PacketVars::quote(getSatName(sat)) +
                            ",\"lesId\":" + std::to_string(lesId) +
                            ",\"lesName\":" + PacketVars::quote(getLesName(sat, lesId)) +
                            ",\"servicesStart\":" + std::to_string(servicesStart) +
                            ",\"services\":" + getServices(iss) +
                            ",\"downlinkChannelMhz\":" + dl + "}";
                j+= 6;
            }
            return stations + "]";
        }
        // FOXSDR: services come out as a JSON object of booleans (upstream: "Name: 0/1" lines)
        std::string PacketDecoder::getServices_short(uint8_t is8) {
            static const char* names[8] = {"MaritimeDistressAlerting", "SafetyNet", "InmarsatC", "StoreFwd", "HalfDuplex", "FullDuplex", "ClosedNetwork", "FleetNet"};
            std::string o = "{";
            for(int b = 0; b < 8; b++) {
                o += std::string(b ? "," : "") + "\"" + names[b] + "\":" + (((is8 >> (7 - b)) & 1) ? "true" : "false");
            }
            return o + "}";
        }
        std::string PacketDecoder::getServices(int iss) {
            static const char* names[16] = {"MaritimeDistressAlerting", "SafetyNet", "InmarsatC", "StoreFwd", "HalfDuplex", "FullDuplex", "ClosedNetwork", "FleetNet",
                                            "PrefixSF", "LandMobileAlerting", "AeroC", "ITA2", "DATA", "BasicX400", "EnhancedX400", "LowPowerCMES"};
            std::string o = "{";
            for(int b = 0; b < 16; b++) {
                o += std::string(b ? "," : "") + "\"" + names[b] + "\":" + (((iss >> (15 - b)) & 1) ? "true" : "false");
            }
            return o + "}";
        }
        std::string PacketDecoder::getDescriptorAsText(uint8_t descriptor_b) {
            std::string descriptor;
            switch(descriptor_b) {
                case 0x27:
                    descriptor = "Logical Channel Clear";
                    break;
                case 0x2A:
                    descriptor = "Inbound Message Ack";
                    break;
                case 0x08:
                    descriptor = "Acknowledgement Request";
                    break;
                case 0x6C:
                    descriptor = "Signalling Channel";
                    break;
                case 0x7D:
                    descriptor = "Bulletin Board";
                    break;
                case 0x81:
                    descriptor = "Announcement";
                    break;
                case 0x83:
                    descriptor = "Logical Channel Assignment";
                    break;
                case 0x91:
                    descriptor = "Distress Alert Ack";
                    break;
                case 0x92:
                    descriptor = "Login Ack";
                    break;
                case 0x9A:
                    descriptor = "Enhanced Data Report Ack";
                    break;
                case 0xA0:
                    descriptor = "Distress Test Request";
                    break;
                case 0xA3:
                    descriptor = "Individual Poll";
                    break;
                case 0xA8:
                    descriptor = "Confirmation";
                    break;
                case 0xAA:
                    descriptor = "Message";
                    break;
                case 0xAB:
                    descriptor = "Les List";
                    break;
                case 0xAC:
                    descriptor = "Request Status";
                    break;
                case 0xAD:
                    descriptor = "Test Result";
                    break;
                case 0xB1:
                    descriptor = "EGC double header, part 1";
                    break;
                case 0xB2:
                    descriptor = "EGC double header, part 2";
                    break;
                case 0xBD:
                    descriptor = "Multiframe Packet Start";
                    break;
                case 0xBE:
                    descriptor = "Multiframe Packet Continue";
                    break;
                default:
                    descriptor = "Unknown";
                    break;
            }
            return descriptor;
        }
        //END CLASS PacketDecoder

        //START CLASS PacketDetector
        PacketDetector::PacketDetector() {
            packetDecoder = new PacketDecoder();
        }
        std::vector<PacketDecoder::packetDecoder_result> PacketDetector::process(decoder::Decoder::decoder_result inputFrame) {
            std::vector<PacketDecoder::packetDecoder_result> ret;
            int pos = 0;
            do {
                PacketDecoder::packetDecoder_result res = detect(inputFrame, &pos);
                ret.push_back(res);
            } while(pos > 0);
            return ret;
        }
        PacketDecoder::packetDecoder_result PacketDetector::detect(decoder::Decoder::decoder_result inputFrame, int* pos) {
            PacketDecoder::packetDecoder_result ret;
            ret.isDecodedPacket = false;
            if(*pos >= inputFrame.length) {
                *pos = 0;
                return ret;
            }
            // FOXSDR: how far the position moves. Upstream advanced by ret.packetLength AFTER ret had been replaced by
            // the reassembled inner packet, so the rest of the frame was read from the wrong place.
            int advance = 0;
            uint8_t descriptor = inputFrame.decodedFrame[*pos];
            // FOXSDR: inside a reassembled packet a BD/BE is not followed (it could loop forever)
            if(packetDecoder->allowZeroCrc && (descriptor == 0xBD || descriptor == 0xBE)) {
                descriptor = 0xFF;
            }
            switch(descriptor) {
                //No more data
                case 0x00:
                    *pos = 0;
                    return ret;
                //27 - Logical Channel Clear
                case 0x27:
                    ret = packetDecoder->decode_27(inputFrame, pos);
                    break;
                //2A - Inbound Message Ack.
                case 0x2A:
                    ret = packetDecoder->decode_2A(inputFrame, pos);
                    break;
                //08 - Acknowledgement Request
                case 0x08:
                    ret = packetDecoder->decode_08(inputFrame, pos);
                    break;
                //6C - Signalling Channel
                case 0x6C:
                    ret = packetDecoder->decode_6C(inputFrame, pos);
                    break;
                //7D - Bulletin Board
                case 0x7D:
                    ret = packetDecoder->decode_7D(inputFrame, pos);
                    break;
                //81 - Announcement
                case 0x81:
                    ret = packetDecoder->decode_81(inputFrame, pos);
                    break;
                //83 - Logical Channel Assignment
                case 0x83:
                    ret = packetDecoder->decode_83(inputFrame, pos);
                    break;
                //91 - Distress Alert Ack.
                case 0x91:
                    ret = packetDecoder->decode_91(inputFrame, pos);
                    break;
                //92 - Login Ack.
                case 0x92:
                    ret = packetDecoder->decode_92(inputFrame, pos);
                    break;
                //9A - Enhanced Data Report Ack.
                case 0x9A:
                    ret = packetDecoder->decode_9A(inputFrame, pos);
                    break;
                //A0 - Distress Test Request
                case 0xA0:
                    ret = packetDecoder->decode_A0(inputFrame, pos);
                    break;
                //A3 - Individual Poll
                case 0xA3:
                    ret = packetDecoder->decode_A3(inputFrame, pos);
                    break;
                //A8 - Confirmation
                case 0xA8:
                    ret = packetDecoder->decode_A8(inputFrame, pos);
                    break;
                //AA - Message
                case 0xAA:
                    ret = packetDecoder->decode_AA(inputFrame, pos);
                    break;
                //AB - Les List
                case 0xAB:
                    ret = packetDecoder->decode_AB(inputFrame, pos);
                    break;
                //AC - Request Status
                case 0xAC:
                    ret = packetDecoder->decode_AC(inputFrame, pos);
                    break;
                //AD - Test Result
                case 0xAD:
                    ret = packetDecoder->decode_AD(inputFrame, pos);
                    break;
                //B1 - EGC double header, part 1 (16 bytes of data)
                case 0xB1:
                    ret = packetDecoder->decode_B1(inputFrame, pos);
                    break;
                //B2 - EGC double header, part 2
                case 0xB2:
                    ret = packetDecoder->decode_B2(inputFrame, pos);
                    break;
                //BD - Multiframe Packet
                case 0xBD:
                    ret = packetDecoder->decode_BD(inputFrame, pos);
                    multiStreamFrameElements = ret.mfp;
                    break;
                //BE - Multiframe Packet Continue
                case 0xBE:
                    ret = packetDecoder->decode_BE(inputFrame, pos, &multiStreamFrameElements);
                    advance = ret.packetLength;
                    if(!ret.isCrc || !multiStreamFrameElements.isMFP) {
                        //if BD is not received before, don't try to decode BE
                        break;
                    }
                    if(multiStreamFrameElements.isReady) {
                        //decode
                        decoder::Decoder::decoder_result ndfa;
                        for(int i = 0; i < (int)multiStreamFrameElements.packetData.size(); i++) {
                            ndfa.decodedFrame[i] = multiStreamFrameElements.packetData[i];
                        }
                        ndfa.length = multiStreamFrameElements.packetData.size();
                        ndfa.frameNumber = inputFrame.frameNumber;
                        ndfa.BER = inputFrame.BER;
                        ndfa.isHardDecision = inputFrame.isHardDecision;
                        ndfa.isMidStreamReversePolarity = inputFrame.isMidStreamReversePolarity;
                        ndfa.isReversedPolarity = inputFrame.isReversedPolarity;
                        ndfa.isUncertain = inputFrame.isUncertain;
                        ndfa.timestamp = inputFrame.timestamp;
                        multiStreamFrameElements.isMFP = false;
                        multiStreamFrameElements.isReady = false;
                        //recursively decode (FOXSDR: the reassembled packet has no CRC bytes of its own)
                        int pos1 = 0;
                        packetDecoder->allowZeroCrc = true;
                        ret = detect(ndfa, &pos1);
                        packetDecoder->allowZeroCrc = false;
                    }
                    break;
                default:
                    ret = packetDecoder->basicDecode(inputFrame, pos);
                    break;
            }
            if(advance == 0) {
                advance = ret.packetLength;
            }
            // FOXSDR: a packet always moves the position on (upstream relied on packetLength > 0)
            if(advance < 1) {
                advance = 1;
            }
            *pos += advance;
            ret.isDecodedPacket = true;
            return ret;
        }
        //END CLASS PacketDetector

        //START CLASS FrameParser
        FrameParser::FrameParser() {
            packetDetector = new PacketDetector();
        }
        std::vector<FrameParser::frameParser_result> FrameParser::parseFrame(decoder::Decoder::decoder_result inputFrame) {
            std::vector<frameParser_result> ret;
            std::vector<PacketDecoder::packetDecoder_result> detRes = packetDetector->process(inputFrame);
            for(int i = 0; i < (int)detRes.size();i++) {
                if(detRes[i].isDecodedPacket) {
                    frameParser_result res;
                    res.decoding_result = detRes[i];
                    ret.push_back(res);
                }
            }
            return ret;
        }
        //END CLASS FrameParser
    }
}
