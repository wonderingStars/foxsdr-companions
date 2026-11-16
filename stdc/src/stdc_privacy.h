/*
 * stdc_privacy.h -- what the stdc helper is allowed to print about a packet, and how.
 *
 * New code for the FoxSDR stdc helper, GNU GPL version 3 (same licence as the rest of this folder).
 *
 * Rules (owner's brief, 2026-11-12): broadcast and signalling packets only. For packets addressed to one terminal
 * the packet type, length and addressing fields are reported, never the message text. The parser in
 * inmarsatc_parser.cpp already does not keep text or EGC payloads; this table is a second, independent barrier:
 * only the keys listed here are ever printed, per packet descriptor. A key the parser might add later, or a
 * descriptor not listed, prints nothing but the descriptor, name and length.
 * Terminal identifiers (mesId, and the 3-byte id of a login acknowledgement) are replaced by run-local
 * pseudonyms with --hide-ids.
 */
#ifndef STDC_PRIVACY_H
#define STDC_PRIVACY_H

#include <map>
#include <string>
#include <vector>
#include <cstdio>
#include <inmarsatc_parser.h>

namespace stdc {

class Privacy {
public:
    explicit Privacy(bool hideIds) : hide_(hideIds) {}

    /* The part of the packet line after {"t":..,"type":"packet", : fields and closing brace. */
    std::string render(const inmarsatc::frameParser::PacketDecoder::packetDecoder_result& p) {
        using inmarsatc::frameParser::PacketVars;
        char hex[8];
        std::snprintf(hex, sizeof(hex), "0x%02X", p.packetDescriptor);
        std::string o = "\"frame\":" + std::to_string(p.frameNumber) + ",\"descriptor\":\"" + hex + "\"" +
                        ",\"name\":" + p.packetVars.get("packetDescriptorText") +
                        ",\"length\":" + std::to_string(p.packetLength) +
                        ",\"stage\":\"" + (p.decodingStage == PACKETDECODER_DECODING_STAGE_COMPLETE ? "complete" :
                                          p.decodingStage == PACKETDECODER_DECODING_STAGE_PARTIAL ? "partial" : "none") + "\"" +
                        ",\"reassembled\":" + (p.reassembled ? "true" : "false") +
                        ",\"uncertain\":" + (p.isUncertain ? "true" : "false") +
                        ",\"ber\":" + std::to_string(p.ber);
        const std::vector<std::string>& keys = allowed(p.packetDescriptor);
        for(size_t i = 0; i < keys.size(); i++) {
            if(!p.packetVars.has(keys[i])) continue;
            std::string v = p.packetVars.get(keys[i]);
            if(isId(keys[i])) {
                o += ",\"" + keys[i] + "\":" + (hide_ ? pseudonym(v) : v);
            } else {
                o += ",\"" + keys[i] + "\":" + v;
            }
        }
        return o + "}";
    }

    static bool isId(const std::string& key) { return key == "mesId" || key == "les"; }

private:
    bool hide_;
    std::map<std::string, int> ids_;

    std::string pseudonym(const std::string& raw) {
        std::map<std::string, int>::iterator it = ids_.find(raw);
        int n;
        if(it == ids_.end()) {
            n = (int)ids_.size() + 1;
            ids_[raw] = n;
        } else {
            n = it->second;
        }
        return "\"#" + std::to_string(n) + "\"";
    }

    static const std::vector<std::string>& allowed(uint8_t d) {
        typedef std::vector<std::string> V;
        static std::map<int, V> t;
        static const V none;
        if(t.empty()) {
            const V satles = {"sat", "satName", "lesId", "lesName"};
            auto cat = [&](V a, const V& b) { a.insert(a.end(), b.begin(), b.end()); return a; };
            t[0x27] = cat({"mesId"}, cat(satles, {"logicalChannelNo"}));
            t[0x2A] = cat({"mesId"}, cat(satles, {"logicalChannelNo", "unknown1Hex"}));
            t[0x08] = cat(satles, {"logicalChannelNo", "uplinkChannelMhz"});
            t[0x6C] = {"services", "uplinkChannelMhz", "tdmSlots"};
            t[0x7D] = cat({"networkVersion"}, cat(satles, {"signallingChannel", "count", "channelType", "channelTypeName", "local", "status", "services", "randomInterval", "timestamp_str"}));
            t[0x81] = cat({"mesId"}, cat(satles, {"logicalChannelNo", "downlinkChannelMhz", "presentation", "unknown1Hex", "unknown2Hex", "unknown3Hex"}));
            t[0x83] = cat({"mesId"}, cat(satles, {"status_bits", "logicalChannelNo", "frameLength", "duration", "downlinkChannelMhz", "uplinkChannelMhz", "frameOffset", "packetDescriptor1"}));
            t[0x92] = {"loginAckLength", "downlinkChannelMhz", "les", "stationStartHex", "stationCount", "stations"};
            /* addressed to one terminal: addressing fields and sizes only, never text */
            t[0xA3] = cat({"mesId"}, cat(satles, {"shortMessageChars"}));
            t[0xA8] = cat({"mesId"}, cat(satles, {"unknown1Hex", "shortMessageLength", "unknown2Hex"}));
            t[0xAA] = cat(satles, {"logicalChannelNo", "packetNo", "payloadBytes"});
            t[0xAB] = {"lesListLength", "stationStartHex", "stationCount", "stations"};
            /* EGC (SafetyNET / FleetNET) headers only: the text is decoded by FoxSDR's own EGC plugin */
            const V egc = {"messageType", "serviceCodeAndAddressName", "continuation", "priority", "priorityText", "isDistress", "repetition", "messageId", "packetNo", "isNewPayload", "presentation", "addressHex", "payloadBytes"};
            t[0xB1] = egc;
            t[0xB2] = egc;
            t[0xBD] = {"multiframePacketDescriptor", "multiframePacketLength"};
            t[0xBE] = {"continuedBytes"};
        }
        std::map<int, V>::const_iterator it = t.find(d);
        return it == t.end() ? none : it->second;
    }
};

} // namespace stdc

#endif
