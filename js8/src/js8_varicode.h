// SPDX-License-Identifier: GPL-3.0-only
//
// Part of the FoxSDR "js8" GPL helper (see ../UPSTREAM.md).
//
// Port of JS8Call's varicode.cpp / jsc.cpp / decodedtext.cpp (GPL-3.0, (C) 2018 Jordan Sherer) from Qt to the C++
// standard library: the receive side (unpacking a decoded 12-character frame into text) and, for the test tool
// only, the transmit side (packing text into frames). Names of the upstream functions are kept in comments.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Js8
{
    // Varicode::TransmissionType: the low three bits that travel with a frame.
    enum TransmissionBits : int
    {
        BitsFirst = 1,
        BitsLast  = 2,
        BitsData  = 4
    };

    // Varicode::FrameType
    enum FrameType : int
    {
        FrameHeartbeat        = 0,
        FrameCompound         = 1,
        FrameCompoundDirected = 2,
        FrameDirected         = 3,
        FrameData             = 4,
        FrameUnknown          = 255
    };

    char const * frameTypeName(int frameType);

    // The result of interpreting a decoded frame (upstream DecodedText).
    struct Frame
    {
        int         frameType = FrameUnknown;
        std::string text;        // DecodedText::message(), UTF-8
        std::string from;        // sender where the frame carries one (heartbeat, compound, directed)
        std::string to;          // addressee of a directed frame
        std::string cmd;         // directed command, without its leading space ("SNR", "ACK", "HEARTBEAT SNR" ...)
        std::string num;         // number or SNR carried with the command, as upstream formats it ("-05", "+12")
        std::string grid;        // grid locator where the frame carries one
        std::string cq;          // heartbeat kind: "HB", "CQ", "CQ CQ CQ" ...
        bool        alt = false; // heartbeat alt flag: true for a CQ, false for an HB
        bool        interpreted = false;
    };

    // DecodedText: try the unpack strategies in upstream order (fast data, data, heartbeat, compound, directed).
    // `frame` is the 12 characters from the decoder, `bits` the three frame bits.
    Frame interpret(std::string const & frame, int bits);

    // JSC::decompress for a bit vector; the result is UTF-8.
    std::string jscDecompress(std::vector<bool> const & bits);

    // --- transmit side, used by the test tool only (js8gen) -----------------------------------------------------
    // These build frames the way Varicode::pack* do. `std::string()` is returned when a field cannot be packed.
    std::string packHeartbeat(std::string const & callsign, std::string const & kind /* "HB", "CQ", "CQ CQ CQ" ...*/,
                              std::string const & grid);
    std::string packCompound(std::string const & callsign, std::string const & grid);
    std::string packCompoundDirected(std::string const & callsign, std::string const & cmd, int num, bool hasNum);
    std::string packDirected(std::string const & from, std::string const & to, std::string const & cmd, int num,
                             bool hasNum);
    // Data text: slow=false packs the legacy Normal-speed data frame (Huffman or JSC, with a 2-bit prefix); fast=true
    // packs the JSC "fast data" frame used by the other speeds. `consumed` gets the number of characters packed.
    std::string packData(std::string const & text, bool fast, int * consumed);
}
