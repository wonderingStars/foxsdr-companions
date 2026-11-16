// Port of the Motorola Connect Plus parts of sdrtrunk module/decode/dmr:
//   message/data/csbk/motorola/ConnectPlus*.java (CSBK opcodes), message/data/csbk/CSBKMessage.java,
//   CSBKMessageFactory.java (the Connect Plus branches), message/type/Vendor.java,
//   message/data/lc/shorty/ConnectPlusControlChannel.java and ConnectPlusTrafficChannel.java (Short LC),
//   channel/DMRLsn.java, ip/mototrbo/xcmp/XCMPMessageType.java, edac/CRCDMR.java (CCITT-80 and CRC-8).
// See common.h for the licence notice.
#pragma once
#include <string>
#include "common.h"

namespace trunkfmt {

// Result of "csbk-hex" text-mode decoding of one input line.
// kind: "csbk" (12 bytes with CRC, or 10 bytes without) or "slc" (Short LC, 36 bits, optionally + CRC-8).
// Returns the JSON object text for the event, or an error object with type "parse_error".
std::string decodeConnectPlusLine(const std::string& line, long long lineNo);

}  // namespace trunkfmt
