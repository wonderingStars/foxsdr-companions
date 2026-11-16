// See tait1200.h / common.h for the notices.
#include "tait1200.h"

namespace trunkfmt {

// Tait1200ANIMessage / Tait1200GPSMessage bit positions (all fields are listed MSB first, bit 0 = first bit
// received, the sync pattern included in the 440 bits).  Contiguous fields are given as {start, end}.
// REVS_1 0..3, SYNC 4..19, SIZE 20..35, FROM_DIGIT_1..8 36..99 (one 8-bit character each), CHECKSUM_1 100..115,
// REVS_2 116..131, SIZE_2 188..203, TO_DIGIT_1..8 204..267, UNKNOWN_1 268..275, CHECKSUM_2 276..291.
// CHECKSUM_1/2 and UNKNOWN_1 are not validated upstream ("Override until we figure out the CRC").
struct Range { int start, end; };
static const Range SIZE = {20, 35};
static const Range SIZE_2 = {188, 203};

// Tait1200GPSMessage
static const Range HOUR_TENS = {293, 295}, HOUR_ONES = {296, 299};
static const Range MINUTES_TENS = {301, 303}, MINUTES_ONES = {304, 307};
static const Range SECONDS_TENS = {309, 311}, SECONDS_ONES = {312, 315};
static const Range LATITUDE_SIGN = {317, 318};
static const Range LATITUDE_DEGREES_TENS = {320, 323}, LATITUDE_DEGREES_ONES = {324, 327};
static const Range LATITUDE_MINUTES_TENS = {329, 331}, LATITUDE_MINUTES_ONES = {332, 335};
static const Range LATITUDE_SECONDS_HUND = {336, 339};
// Upstream: LATITUDE_SECONDS_TENS = {340, 341, 342, 344}  (344 also appears in LATITUDE_SECONDS_ONES; kept as is)
static const int LATITUDE_SECONDS_TENS[] = {340, 341, 342, 344};
static const Range LATITUDE_SECONDS_ONES = {344, 347};
static const Range LONGITUDE_SIGN = {349, 350};
static const int LONGITUDE_DEGREES_HUNDREDS = 351;
static const Range LONGITUDE_DEGREES_TENS = {352, 355}, LONGITUDE_DEGREES_ONES = {356, 359};
static const Range LONGITUDE_MINUTES_TENS = {361, 363}, LONGITUDE_MINUTES_ONES = {364, 367};
static const Range LONGITUDE_SECONDS_HUND = {368, 371}, LONGITUDE_SECONDS_TENS = {372, 375};
static const Range LONGITUDE_SECONDS_ONES = {376, 379};
static const Range DATE_DAY_TENS = {381, 383}, DATE_DAY_ONES = {384, 387};
static const Range DATE_MONTH = {388, 391};     // also SPEED_HUNDREDS upstream
static const Range SPEED_HUNDREDS = {388, 391};
static const Range SPEED_TENS = {392, 395}, SPEED_ONES = {396, 399}, SPEED_TENTHS = {400, 403};

static int gi(const BitMsg& m, Range r) { return (int)m.getRange(r.start, r.end); }

// Tait1200ANIMessage.getCharacter() + String.trim()
static std::string getText(const BitMsg& m, int firstBit)
{
    std::string s;
    for(int d = 0; d < 8; d++)
    {
        s.push_back((char)m.getRange(firstBit + 8 * d, firstBit + 8 * d + 7));
    }
    size_t a = 0, b = s.size();
    while(a < b && (unsigned char)s[a] <= 0x20) { a++; }
    while(b > a && (unsigned char)s[b - 1] <= 0x20) { b--; }
    return s.substr(a, b - a);
}

Tait1200ANIMessage decodeTaitANI(const BitMsg& m)
{
    Tait1200ANIMessage o;
    o.from = getText(m, 36);       // FROM_DIGIT_1..8
    o.to = getText(m, 204);        // TO_DIGIT_1..8
    o.size1 = gi(m, SIZE);
    o.size2 = gi(m, SIZE_2);
    o.rawHex = m.toHex();
    return o;
}

// Tait1200GPSMessage.getDigit(): a digit field outside 0..9 prints as '?'
static std::string digit(const BitMsg& m, Range r)
{
    int v = gi(m, r);
    return (0 <= v && v <= 9) ? std::to_string(v) : "?";
}

Tait1200GPSMessage decodeTaitGPS(const BitMsg& m)
{
    Tait1200GPSMessage o;
    o.from = getText(m, 36);
    o.to = getText(m, 204);
    o.size1 = gi(m, SIZE);
    o.size2 = gi(m, SIZE_2);

    // getGPSLocation()
    double lat = gi(m, LATITUDE_DEGREES_TENS) * 10.0;
    lat += gi(m, LATITUDE_DEGREES_ONES);
    lat += (double)gi(m, LATITUDE_MINUTES_TENS) / 6.0;
    lat += (double)gi(m, LATITUDE_MINUTES_ONES) / 60.0;
    lat += (double)gi(m, LATITUDE_SECONDS_HUND) / 600.0;
    lat += (double)m.getInt(LATITUDE_SECONDS_TENS) / 6000.0;
    lat += (double)gi(m, LATITUDE_SECONDS_ONES) / 60000.0;
    if(gi(m, LATITUDE_SIGN) == 0) { lat *= -1; }

    double lon = m.get(LONGITUDE_DEGREES_HUNDREDS) ? 100.0 : 0.0;
    lon += gi(m, LONGITUDE_DEGREES_TENS) * 10.0;
    lon += gi(m, LONGITUDE_DEGREES_ONES);
    lon += (double)gi(m, LONGITUDE_MINUTES_TENS) / 6.0;
    lon += (double)gi(m, LONGITUDE_MINUTES_ONES) / 60.0;
    lon += (double)gi(m, LONGITUDE_SECONDS_HUND) / 600.0;
    lon += (double)gi(m, LONGITUDE_SECONDS_TENS) / 6000.0;
    lon += (double)gi(m, LONGITUDE_SECONDS_ONES) / 60000.0;
    // Upstream has "longitude = -1" here (the whole value is thrown away).  That is clearly a typo for
    // "longitude *= -1" and is changed in this port; see UPSTREAM.md.
    if(gi(m, LONGITUDE_SIGN) == 0) { lon *= -1; }
    o.latitude = lat;
    o.longitude = lon;

    // getGPSTime(): upstream builds a calendar date from the current year; only the time of day and the
    // day-of-month field are decoded here so the output does not depend on the wall clock.
    char tbuf[32];
    snprintf(tbuf, sizeof tbuf, "%02d:%02d:%02d", gi(m, HOUR_TENS) * 10 + gi(m, HOUR_ONES),
             gi(m, MINUTES_TENS) * 10 + gi(m, MINUTES_ONES), gi(m, SECONDS_TENS) * 10 + gi(m, SECONDS_ONES));
    o.time = tbuf;
    o.day = gi(m, DATE_DAY_TENS) * 10 + gi(m, DATE_DAY_ONES);
    o.field388 = gi(m, DATE_MONTH);

    // getSpeed(): "HHH.T" digit string
    std::string sp = digit(m, SPEED_HUNDREDS) + digit(m, SPEED_TENS) + digit(m, SPEED_ONES) + "." + digit(m, SPEED_TENTHS);
    char* endp = nullptr;
    double v = strtod(sp.c_str(), &endp);
    o.speedKph = (endp && *endp == '\0') ? v : 0.0;     // Double.parseDouble failure -> 0
    o.rawHex = m.toHex();
    return o;
}

Tait1200Decoder::Tait1200Decoder(GpsSink gps, AniSink ani)
    : mFramerGPS(syncTaitGps(), MESSAGE_LENGTH), mFramerANI(syncTaitSelcal(), MESSAGE_LENGTH)
{
    // Tait1200GPSMessageProcessor / Tait1200ANIMessageProcessor: every framed message is passed on and both
    // message classes report isValid() == true (no CRC check upstream).
    mFramerGPS.setListener([gps](const BitMsg& m) { if(gps) { gps(decodeTaitGPS(m)); } });
    mFramerANI.setListener([ani](const BitMsg& m) { if(ani) { ani(decodeTaitANI(m)); } });
}

}  // namespace trunkfmt
