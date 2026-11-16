// msumrcal - Meteor-M MSU-MR infrared two-point calibration (GPL helper).
//
// This file is a C++ port, with no JSON library and no SatDump types, of the infrared branch of
//   plugins/meteor_support/meteor/instruments/msumr/msumr_calibrator.h  (class MeteorMsuMrCalibrator)
// from SatDump (https://github.com/SatDump/SatDump, GPL-3.0), commit 9fee0793c7d42bea2d401d330422e18c33c126fa.
// The Planck functions come from src-core/common/calibration.cpp and most_common() from src-core/utils/stats.h.
// Every ported part carries the upstream names in a comment. Changes from upstream are listed in UPSTREAM.md.
//
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU General
// Public License as published by the Free Software Foundation, version 3 of the License. It is distributed
// WITHOUT ANY WARRANTY. See the LICENSE file in this folder.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace msumr {

// src-core/common/calibration.cpp: c1, c2, e_num (units: radiance in mW / (m^2 sr cm^-1), wavenumber in cm^-1)
constexpr double kC1 = 1.1910427e-05;
constexpr double kC2 = 1.4387752;
constexpr double kEnum = 2.7182818; // upstream uses this truncated constant, not exp(1); kept for identical results

// calibration.cpp: temperature_to_radiance(t, v)
inline double temperature_to_radiance(double t, double v) { return (kC1 * v * v * v) / (std::pow(kEnum, kC2 * v / t) - 1); }
// calibration.cpp: radiance_to_temperature(L, v)
inline double radiance_to_temperature(double L, double v) { return (kC2 * v) / (std::log(kC1 * v * v * v / L + 1)); }

// stats.h: satdump::most_common(begin, end, def). Ties resolve to the smallest value (std::map order,
// std::max_element keeps the first maximum), exactly as upstream.
inline double most_common(const std::vector<double> &v, double def)
{
    if (v.empty())
        return def;
    std::map<double, int> counts;
    for (double x : v)
        ++counts[x];
    return std::max_element(counts.begin(), counts.end(), [](const std::pair<const double, int> &a, const std::pair<const double, int> &b) { return a.second < b.second; })->first;
}

// Infrared channels of MSU-MR. resources/calibration/MSU-MR.json "wavenumbers"[3..5] (channels 4, 5, 6), cm^-1.
// ORIGIN: SatDump keeps no source for these; the three numbers are identical to the AVHRR/3 table (channels 3B, 4, 5)
// in SatDump's resources/calibration/AVHRR.json. See UPSTREAM.md.
constexpr int kFirstIrChannel = 4; // channel numbers 4..6 are the infrared ones
constexpr int kIrChannels = 3;
constexpr double kWavenumber[kIrChannels] = {2695.9743, 925.4075, 839.8979};

// One telemetry record: the on-board reference temperatures (K) and the cold/hot view counts of channels 4..6.
struct Strip
{
    bool present = false;       // a T record was received for this strip
    bool has_temps = false;     // temperatures are not null (SatDump: temps[k] not null)
    double cold1 = 0, cold2 = 0, hot1 = 0, hot2 = 0;
    uint32_t view[kIrChannels][2] = {{0, 0}, {0, 0}, {0, 0}}; // [ch][0]=cold view, [ch][1]=hot view (views[i][pos_y].first/second)
};

struct StripRef
{
    double cold_k = 0, hot_k = 0; // after search for neighbours and outlier replacement
    bool filled_from_neighbour = false;
    bool outlier_replaced = false;
};

struct Calibration
{
    std::vector<StripRef> ref;
    double cold_mode = 0, hot_mode = 0;
};

// Port of the MeteorMsuMrCalibrator constructor, the "can_ir_calib" branch: for every strip i take the mean of the two cold
// and the two hot temperatures from the first strip at or after i that has them, else (only if still zero) from the
// nearest earlier one, then replace values more than 5 K from the most common value of the whole pass.
inline Calibration build_reference(const std::vector<Strip> &strips)
{
    Calibration cal;
    const int max_lcnt = (int)strips.size();
    std::vector<double> cold_temps, hot_temps;
    std::vector<bool> filled;
    for (int i = 0; i < max_lcnt; i++)
    {
        double coldt = 0, hott = 0;
        bool own = strips[i].has_temps;
        int next_x_calib = i;
        while (next_x_calib < max_lcnt)
        {
            if (strips[next_x_calib].has_temps)
            {
                coldt = (strips[next_x_calib].cold1 + strips[next_x_calib].cold2) / 2.0;
                hott = (strips[next_x_calib].hot1 + strips[next_x_calib].hot2) / 2.0;
                break;
            }
            next_x_calib++;
        }
        if (coldt == 0 || hott == 0)
        {
            int back = i;
            // NOTE: upstream loops while (0 < back), so index 0 itself is never examined going backwards. Kept.
            while (0 < back)
            {
                if (strips[back].has_temps)
                {
                    coldt = (strips[back].cold1 + strips[back].cold2) / 2.0;
                    hott = (strips[back].hot1 + strips[back].hot2) / 2.0;
                    break;
                }
                back--;
            }
        }
        cold_temps.push_back(coldt);
        hot_temps.push_back(hott);
        filled.push_back(!own);
    }
    cal.cold_mode = most_common(cold_temps, 0);
    cal.hot_mode = most_common(hot_temps, 0);
    cal.ref.resize(max_lcnt);
    for (int i = 0; i < max_lcnt; i++)
    {
        cal.ref[i].filled_from_neighbour = filled[i];
        if (std::fabs(cal.cold_mode - cold_temps[i]) > 5)
        {
            cold_temps[i] = cal.cold_mode;
            cal.ref[i].outlier_replaced = true;
        }
        if (std::fabs(cal.hot_mode - hot_temps[i]) > 5)
        {
            hot_temps[i] = cal.hot_mode;
            cal.ref[i].outlier_replaced = true;
        }
        cal.ref[i].cold_k = cold_temps[i];
        cal.ref[i].hot_k = hot_temps[i];
    }
    return cal;
}

// Per strip and channel two-point line: radiance = offset + count * gain.
struct Line
{
    bool valid = false;
    double cold_rad = 0, hot_rad = 0, gain = 0, offset = 0;
    double cold_view = 0;
};

// Port of MeteorMsuMrCalibrator::compute(), infrared branch, split in two: this is the part that depends only on the strip.
//   cold_view == 0 || hot_view == 0           -> invalid
//   cold_ref == 0 && hot_ref == 0             -> invalid
//   gain_rad = (hot_rad - cold_rad) / (hot_view - cold_view);  rad = cold_rad + (px - cold_view) * gain_rad
// CHANGE: hot_view == cold_view is invalid here (upstream divides by zero and yields inf/NaN).
inline Line two_point(int ch_index, uint32_t cold_view, uint32_t hot_view, double cold_ref, double hot_ref)
{
    Line l;
    if (cold_view == 0 || hot_view == 0 || cold_view == hot_view)
        return l;
    if (cold_ref == 0 && hot_ref == 0)
        return l;
    const double wn = kWavenumber[ch_index];
    l.cold_rad = temperature_to_radiance(cold_ref, wn);
    l.hot_rad = temperature_to_radiance(hot_ref, wn);
    l.gain = (l.hot_rad - l.cold_rad) / ((double)hot_view - (double)cold_view);
    l.cold_view = (double)cold_view;
    l.offset = l.cold_rad - (double)cold_view * l.gain;
    l.valid = std::isfinite(l.gain) && std::isfinite(l.offset);
    return l;
}

// Pixel radiance for a valid line; px == 0 is invalid (upstream: px_val == 0 -> CALIBRATION_INVALID_VALUE).
// Returns false when invalid.
inline bool pixel_radiance(const Line &l, uint32_t px, double &rad)
{
    if (!l.valid || px == 0)
        return false;
    rad = l.cold_rad + ((double)px - l.cold_view) * l.gain; // upstream: cold_rad + (px_val - cold_view) * gain_rad
    return true;
}

// Brightness temperature in K from radiance, as the converter em_rad_to_bright_temp.h does (radiance_to_temperature).
// CHANGE: non-positive radiance gives no value (upstream log() of a negative number gives NaN).
inline bool brightness_temperature(double rad, int ch_index, double &kelvin)
{
    if (!(rad > 0))
        return false;
    kelvin = radiance_to_temperature(rad, kWavenumber[ch_index]);
    return std::isfinite(kelvin);
}

} // namespace msumr
