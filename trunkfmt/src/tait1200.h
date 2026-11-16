// Port of sdrtrunk module/decode/tait: Tait1200Decoder, Tait1200ANIMessage(Processor),
// Tait1200GPSMessage(Processor), identifier/TaitIdentifier.  See common.h for the licence notice.
#pragma once
#include <functional>
#include <string>
#include "common.h"
#include "framer.h"

namespace trunkfmt {

struct Tait1200ANIMessage
{
    std::string from, to;        // 8 characters each, trimmed (Java String.trim())
    int size1 = 0, size2 = 0;    // SIZE (bits 20..35) and SIZE_2 (bits 188..203)
    std::string rawHex;          // all 440 bits
};

struct Tait1200GPSMessage
{
    std::string from, to;
    int size1 = 0, size2 = 0;
    double latitude = 0, longitude = 0;
    std::string time;            // "HH:MM:SS" from the hour/minute/second digit fields (GPS time of day)
    int day = 0;                 // DATE_DAY_TENS * 10 + DATE_DAY_ONES
    int field388 = 0;            // bits 388..391: upstream does not know if this is the month or speed hundreds
    double speedKph = 0;         // see Tait1200GPSMessage.getSpeed()
    std::string rawHex;
};

Tait1200ANIMessage decodeTaitANI(const BitMsg& m);
Tait1200GPSMessage decodeTaitGPS(const BitMsg& m);

// Tait1200Decoder.java: sync TAIT_CCDI_GPS_MESSAGE / TAIT_SELCAL_MESSAGE, 440-bit messages.
class Tait1200Decoder
{
public:
    static constexpr size_t MESSAGE_LENGTH = 440;
    using GpsSink = std::function<void(const Tait1200GPSMessage&)>;
    using AniSink = std::function<void(const Tait1200ANIMessage&)>;
    Tait1200Decoder(GpsSink gps, AniSink ani);
    void process(bool symbol)    // Tait1200Decoder.process(boolean): ANI framer first, then GPS framer
    {
        mFramerANI.process(symbol);
        mFramerGPS.process(symbol);
    }

private:
    MessageFramer mFramerGPS, mFramerANI;
};

}  // namespace trunkfmt
