# hfdl: FoxSDR GPL helper for HFDL (aircraft HF data link, ARINC 635)

A complete command-line HFDL decoder under the **GNU GPL v3 or later**, built from [dumphfdl](https://github.com/szpajder/dumphfdl) 1.7.0
(Tomasz Lemiech) and [libacars](https://github.com/szpajder/libacars) (MIT). It reads raw I/Q on standard input and writes one
JSON line per decoded PDU on standard output. It links nothing from FoxSDR; FoxSDR starts it as a separate process
(protocol version 1, `docs/PIPE-PROTOCOL.md` of this repository). What was taken and changed: `UPSTREAM.md`.

## What it decodes
Several HFDL channels inside one I/Q stream at once (each channel is a 3 kHz USB channel; the carrier is 1440 Hz above the assigned frequency):

* physical layer: frequency-domain channelizer, resampler, AGC, matched filter, symbol timing, Costas loop, LMS equalizer,
  A1/A2/M1/M2 preamble correlation, 300 / 600 / 1200 / 1800 bit/s (BPSK, QPSK, 8PSK), single and double slots, soft-decision
  de-interleaving and Viterbi decoding, descrambling, CRC-16 frame checks
* MPDU (uplink and downlink headers; with `--output-mpdus`), SPDU (ground-station squitter with the ground-station status list),
  LPDU (logon request / confirm / resume / denied, logoff, unnumbered data), HFNPDU (performance data, frequency data, data
  with ACARS), ACARS (through libacars: ARINC-622 ADS-C and CPDLC, media advisory), system table assembly from received PDUs,
  aircraft-id to ICAO-address cache
* what libacars is built without (see `UPSTREAM.md`): MIAM decompression, XML pretty-printing, OHMA JSON parsing

Not offered (and why): the radio drivers and network outputs of dumphfdl, decryption (none exists in HFDL), the Basestation
aircraft database, saving the system table to a file.

## Input
`--format cs16` (interleaved int16 little endian, scaled by 32767.5 as dumphfdl does) or `--format cf32` (interleaved float32,
full scale 1.0). Any sample rate **from 10800 S/s up** (tested: 10800, 20250, 21600, 48000, 250000; 1 MS/s decodes too but is slow
to test). Below 10800 S/s a 3 kHz channel whose carrier sits at +1440 Hz does not fit: 5400 and 7200 S/s decoded nothing, so
the program refuses them. Input that ends in half a sample drops the half sample.

## Run it by hand
```
hfdl.exe --foxsdr-helper 1 --format cf32 --rate 20250 --centre 21931000 ^
         --channel-offset -3000 --channel-offset 0 --channel-offset 3000 --channel-offset 6000 --stats < recording.cf32
hfdl.exe --foxsdr-helper-info          # the manifest, hfdl.helper.json
```
`--channel-offset` is the assigned HFDL channel frequency minus the I/Q centre, in Hz (as many as you like, each at most half the
sample rate from the centre). `--centre` (Hz) is only used to print absolute frequencies. Other options: `--hide-ids`,
`--system-table <file>` (dumphfdl's `systable.conf`, `etc/systable.conf` is the sample; gives ground-station names and the frequency
of each channel id), `--start-time <unix seconds>` (the wall-clock time of the first sample; the frame timestamps count from it,
default 0), `--output-mpdus`, `--output-corrupted-pdus`, `--stats`, `--chunk <samples>` (read size, does not change the output).

Real output from the 127 s off-air recording from the Signal Identification Wiki (Skip.land, 21931 kHz, 20250 S/s;
`--start-time 1730841489`, i.e. the time in the file name, makes the frame times come out as 21:18 UTC), first event:
```
{"helper":"hfdl","version":"1.7.0","protocol":1,"licence":"GPL-3.0-or-later","source":"https://github.com/szpajder/dumphfdl","format":"cf32","rate":20250,"centre":21931000,"upstream":"dumphfdl 1.7.0 (bb7234bdf12ff1ca5f1dc99a5710555f968c4e6e) + libacars 2.2.1","channels":[21928000,21931000,21934000,21937000]}
{"t":5.133,"type":"hfdl","hfdl":{"app":{"name":"dumphfdl","ver":"1.7.0"},"t":{"sec":2,"usec":795777},"freq":21931000,"bit_rate":300,"sig_level":-26.885334,"noise_level":-43.004478,"freq_skew":-0.836593,"slot":"S","spdu":{"err":false,"src":{"type":"Ground station","id":4},"spdu_version":0,"rls":false,"iso":false,"change_note":"None","frame_index":2397,"frame_offset":1,"min_priority":0,"systable_version":52,"gs_status":[{"gs":{"type":"Ground station","id":4},"utc_sync":true,"freqs":[{"id":0},{"id":2}]},{"gs":{"type":"Ground station","id":2},"utc_sync":true,"freqs":[{"id":0},{"id":5},{"id":7}]},{"gs":{"type":"Ground station","id":3},"utc_sync":true,"freqs":[{"id":0},{"id":1},{"id":2}]}]}},"text":"[1970-01-01 00:00:02.796 UTC] [21931.0 kHz] [-0.8 Hz] [-26.9/-43.0 dBFS] [16.1 dB] [300 bps] [S] | Uplink SPDU: | Src GS: 4 | Squitter: ver: 0 rls: 0 iso: 0 | Change note: None | TDMA Frame: index: 2397 offset: 1 | Minimum priority: 0 | System table version: 52 | Ground station status: | ID: 4 | UTC sync: 1 | Frequencies in use: 0, 2 | ID: 2 | UTC sync: 1 | Frequencies in use: 0, 5, 7 | ID: 3 | UTC sync: 1 | Frequencies in use: 0, 1, 2"}
```
The recording gives 50 decoded PDUs on four channels (SPDUs, logon/logoff, performance data, ACARS, media advisory), all
with a good CRC, in 0.8 s.

## Output (protocol version 1)
First line, the hello: `helper`, `version`, `protocol`, `licence`, `source`, plus `format`, `rate`, `centre`, `upstream`, `channels`
(absolute frequencies, Hz). Then, per decoded LPDU or SPDU (an MPDU carrying several LPDUs gives one event per LPDU):

| field | meaning |
|---|---|
| `t` | seconds of input read when the frame was complete (from the sample count, not a clock) |
| `type` | `"hfdl"` |
| `hfdl` | **dumphfdl's own JSON object, unchanged**: `app`, `t{sec,usec}` (frame start = `--start-time` + position in the input), `freq` (Hz), `bit_rate`, `sig_level` / `noise_level` (dBFS), `freq_skew` (Hz), `slot` (`S` single / `D` double), and one of `spdu`, `lpdu`, `mpdu`; the keys below `lpdu` are libacars' and dumphfdl's (`src`, `dst`, `type`, `hfnpdu`, `acars`, `ac_info.icao`, ...) |
| `text` | the one-line form of dumphfdl's text output (the lines joined with ` | `) |

With `--stats` a last `{"t":..,"type":"stats","samples":N,"pdus":N}`. The program prints a hello line even for empty input (and then
exits 0). Bad arguments print a message on stderr, nothing on stdout, and exit 2. The output is a function of the input and the
arguments only (checked: same bytes on repeated runs and for any `--chunk`).

## --hide-ids
Blanks in both `hfdl` and `text`: the ICAO address (`icao`, "ICAO:", "(ABC123)" after an aircraft id), the registration (`reg`,
"Reg:"), the flight identifiers (`flight`, `flight_id`, "Flight:", "Flight ID:"). It leaves the temporary HFDL aircraft id, positions,
and the free text of ACARS messages, which can contain anything.

## Build and test (Windows, Visual Studio 2022 Build Tools, CMake)
```
cmake -S hfdl -B <build dir> -G "Visual Studio 17 2022" -A x64
cmake --build <build dir> --config Release --parallel 4
cd <build dir> && ctest -C Release
```
`hfdl_gen` (in `test/`) writes synthetic HFDL transmissions from the protocol constants (all four bit rates, single and double slot,
BPSK/QPSK/8PSK, SPDU and MPDUs with LPDUs, a damaged frame check sequence, a 7 Hz and a -11 Hz carrier error, noise); CTest pins the
decoded JSON (set `HFDL_UPDATE=1` to rewrite the pinned files after a change you mean), checks the failure paths, other sample
rates, `--hide-ids`, and counts PDUs on the real recording if `-DFOXSDR_SAMPLES_DIR=<folder>` points at it (it is never committed). The pinned
files contain float values (`sig_level`, `freq_skew`), so they are tied to this compiler and CPU family. `-DHFDL_ASAN=ON` builds with
AddressSanitizer (copy `clang_rt.asan_dynamic-x86_64.dll` from the MSVC `bin\Hostx64\x64` folder next to the program).
