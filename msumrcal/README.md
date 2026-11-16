# msumrcal - Meteor-M MSU-MR infrared calibration (GPL helper)

Turns the raw infrared counts of the Meteor-M MSU-MR scanner (channels 4, 5, 6: 3.7, 10.8 and 12 micrometres) into
radiance and brightness temperature, using the two on-board reference targets (cold about 258 K, hot about 313 K).
It is a port of the infrared branch of SatDump's `MeteorMsuMrCalibrator` (GPL-3.0). **It is not a signal decoder**:
its input is text lines that a Meteor LRPT decoder already knows (see UPSTREAM.md for where SatDump gets them).

Licence: GNU GPL version 3 (`LICENSE`). Upstream and every change: `UPSTREAM.md`. No network access, no telemetry, no window.

## Run it by hand
```
msumrcal --foxsdr-helper 1 --format msumr-lines < pass.txt
msumrcal --foxsdr-helper-info          (prints the manifest, msumrcal.helper.json)
```
Options: `--lines-per-tlm N` (image lines per telemetry record, default 8 = Meteor LRPT; 1 if you give one record per line),
`--cold-offset-k K` (added to the two cold temperatures; SatDump adds 40 for the MSU-MR unit id 2, "M2-2 patch"),
`--emit coeffs|bt|both` (default both), `--rate N` (accepted and ignored: the input is text, not samples).
Any other `--format` is refused (exit 2, message on stderr). The whole input is read to the end of file before anything
but the hello line is written, because the reference temperatures of the pass are compared with each other.

## Input: `--format msumr-lines`
UTF-8 or ASCII text, one record per line, fields separated by spaces or tabs, `\n` or `\r\n`. Empty lines and lines
starting with `#` are ignored. Lines over 256 KiB, unknown records and malformed records are skipped and counted (`bad_lines`).

    T <k> <cold1> <cold2> <hot1> <hot2> <c4cold> <c4hot> <c5cold> <c5hot> <c6cold> <c6hot>
    P <y> <ch> <count0> <count1> ... <countN>

* `T` = one MSU-MR telemetry packet (APID 70 in LRPT), numbered `k` = 0, 1, 2 ... in time order. It covers 8 image
  lines (strip `k` = image lines 8k .. 8k+7).
  * `cold1 cold2 hot1 hot2`: the cold-body temperature 1 and 2 and hot-body temperature 1 and 2 in kelvin, taken from the
    analog telemetry frame (SatDump `analog_tlm` names "Cold Body Temperature 1/2 (258K)" and "Hot Body Temperature 1/2 (313K)").
    From the raw telemetry frame bytes SatDump's `msumr_tlm.h` computes `K = 273.15 - 0.5 * (signed 8-bit byte)` (frame bytes
    22..27 for cold3, cold2, cold1, hot3, hot2, hot1, counted from the start of the 8-byte-offset MSU-MR frame, i.e. payload
    bytes 30..35). Telemetry packets that carry the digital frame instead have no temperatures: write `- - - -`; the helper
    then uses the next packet that has them, else the nearest earlier one.
  * `c4cold c4hot ... c6hot`: the cold-view and hot-view 10-bit words of channels 4, 5 and 6. SatDump takes them from the
    packet payload as `words10_bits[channel*2]` (cold) and `[channel*2+1]` (hot), channel counted from 0, five bytes holding four
    10-bit words from payload byte 43; channels 4..6 are words 6..11. `0` means missing.
* `P` = one row of image pixels of one infrared channel: `y` the image line (0 = first line of the pass), `ch` 4, 5 or 6,
  then the raw 10-bit counts (0..1023 as SatDump keeps them; 0 = no data). A row belongs to telemetry strip `y / lines-per-tlm`.
  Rows may come in any order and before or after the `T` records.

## Output (JSON Lines, protocol version 1)
First line: hello `{"helper":"msumrcal","version":"1.0.0","protocol":1,"licence":"GPL-3.0-only","source":...,"format":"msumr-lines","upstream":"SatDump <commit>"}`.
`t` is 0 on every event (there are no samples). An input without any record gives the hello line only. After the end of input:

* `cal`, one per strip `k` and channel `ch` (4..6) with the per-strip line `radiance = offset + count * gain`:
  `{"t":0,"type":"cal","k":0,"ch":4,"valid":true,"cold_view":115,"hot_view":441,"cold_k":258.00,"hot_k":313.10,"cold_rad":0.06897,"hot_rad":0.97215,"gain":0.002770487,"offset":-0.24963,"filled":false,"outlier":false}`
  Radiance unit: mW / (m2 sr cm-1). `filled` = the temperatures came from another strip, `outlier` = a temperature was more than 5 K
  from the most common value of the pass and was replaced by it. Invalid: `"valid":false,"reason":"no_telemetry|no_view|equal_views|no_temps"`.
* `bt`, one per `P` record: `{"t":0,"type":"bt","y":0,"ch":4,"k":0,"unit":"K","bt":[285.59,null,...]}` (kelvin, 2 decimals; `null` = no value:
  count 0, strip invalid, or non-positive radiance).
* `status` (`text`) for dropped rows or repeated strip indices; `stats` last: strips, strips_with_tlm, strips_with_temps, cold_mode_k,
  hot_mode_k, pixel_rows, pixels, pixels_invalid, bad_lines, dropped_rows.

## Wavenumbers and their origin
Channel 4 2695.9743, channel 5 925.4075, channel 6 839.8979 cm-1 (`resources/calibration/MSU-MR.json` of SatDump). SatDump gives no
source for them in the file; they are identical to the NOAA AVHRR/3 channel 3B/4/5 central wavenumbers in SatDump's
`AVHRR.json`, so they are a stand-in for the MSU-MR's own, not measured values for it. Absolute accuracy is therefore not established
(see UPSTREAM.md). For comparison, NWP SAF lists different central wavenumbers for the Meteor-M MSU-MR infrared channels.

## Build and test
Visual Studio 2022, CMake: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Release && ctest --test-dir build -C Release`.
The tests build their input with `tools/gen_msumr_lines.cpp` from the same Planck constants, so they prove the arithmetic, the filling and
outlier rules and the failure paths, not agreement with a real satellite pass.
