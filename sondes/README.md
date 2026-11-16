# sondes - radiosonde decoders (LMS6, Mk IIa, MRZ, MTS01, CF-06 / RS92) as a FoxSDR GPL helper

A standalone command-line program under the GNU GPL v3. It reads **FM-demodulated audio** on standard input and prints one JSON
line per decoded sonde frame. It is the rs1729/RS decoder collection (https://github.com/rs1729/RS, commit 89b1ea7), run together
on one audio stream. See `UPSTREAM.md` for licence, changes and what is left out.

| decoder (`--types`) | sonde | band / modulation |
|---|---|---|
| `lms6` | Lockheed Martin Sippican **LMS6-400** (LMS6-403, LMS-X) | 403 MHz, 4800 baud FSK, Viterbi + Reed-Solomon |
| `mk2a` | Sippican **LMS6-1680** / Mk IIa | 1680 MHz, 9600 baud, 8N1 bytes (needs a wideband FM demodulator) |
| `mrz` | Meteo-Radiy MRZ-N1 / MP3-H1 | 400 MHz, 2400 baud Manchester |
| `mts01` | Meteo-Radiy MTS01 | 400 MHz, 1200 baud |
| `cf06` | Changfeng CF-06-AH, HT03G / GTH3 | 400 MHz, 2400 baud, Reed-Solomon |
| `rs92` | Vaisala RS92 (retired type) | only with `--rs92-ephemeris` or `--rs92-almanac`; **unverified** |

## Run by hand
```
sondes --foxsdr-helper 1 --format s16 --rate 48000 < fm_audio.raw
```
- `--format s16` (mono, little-endian 16 bit) or `f32` (mono float). I/Q formats are refused (exit 2).
- `--rate` 38400 .. 192000 Hz; **48000 recommended** (4800 baud gives 10 samples per bit; the 9600 baud Mk IIa gets 5, and 96000 is better for it).
- The audio is the discriminator output of a narrow-FM receiver tuned to the sonde (the sondes are found by frequency elsewhere: the
  helper does not scan). Polarity does not matter. The Mk IIa needs the audio bandwidth of a +-50 kHz deviation signal.
- `--types lms6,mk2a,...` picks decoders (default: all except rs92). `--chunk <samples>` sets the input chunk (default rate/10), which
  only changes the grouping of output, not the events. `--foxsdr-helper-info` prints `sondes.helper.json`.
- Ends at end of file, exit 0. One decoder thread each; the output order is the same on every run.

## Output (JSON Lines)
First line, the hello: `{"helper":"sondes","version":"1.0.0","protocol":1,"licence":"GPL-3.0-only","source":"https://github.com/rs1729/RS","format":"s16","rate":48000,"decoders":[...]}`.
Then one event per good frame (CRC ok):
```
{"t":1.171,"type":"sonde","sonde":"LMS","frame":1000,"serial":"LMS6-8000052","time":"12:34:05.000Z","lat":51.50740,"lon":-0.12780,"alt":120.00000,"vel_h":3.53412,"heading":115.11483,"vel_v":5.00000,"gpstow":390845000,"subtype":"LMS6-403","ref_datetime":"GPS","ref_position":"GPS"}
```
`t` is seconds of audio read when the frame was reported (a decoder reads a little ahead of the frame it prints).
Fields (the upstream's auto_rx names; a field is present only when the sonde sends it): `sonde` (family: LMS, MRZ, MTS01, CF6 / GTH,
RS92), `subtype`, `serial`, `frame`, `time` (UTC or GPS time of day, or a full date for MRZ / MTS01), `lat`, `lon` (degrees), `alt` (m),
`vel_h`, `vel_v` (m/s), `heading` (deg), `sats`, `temp` (C), `humidity` (%), `pressure` (hPa), `batt` (V), `gpstow` (ms), `aux`, `freq`,
`tx_frequency`, `ref_datetime`, `ref_position` (GPS = ellipsoid height, MSL = sea level). The LMS6 frames carry no temperature or
humidity (the upstream decodes none). A last `{"type":"stats","samples":N,"frames":{...}}` line, and `{"type":"status","text":...}` if a decoder cannot start.

## Build and test
```
cmake -S sondes -B <build dir> -G "Visual Studio 17 2022" -A x64
cmake --build <build dir> --config Release --parallel 4 --target sondes sondes-gen
ctest --test-dir <build dir> -C Release --output-on-failure
```
`sondes-gen` makes test audio (LMS6, MTS01, CF-06, MRZ from the upstream's constants; Mk IIa from the real subframes in
`FOXSDR_SAMPLES_DIR/Mk2a_9600.bytes`) and compares the helper's events with what it put in. `SONDES_UPDATE=1 ctest ...` rewrites the pinned
`test/expected/*.jsonl` (review the diff).
