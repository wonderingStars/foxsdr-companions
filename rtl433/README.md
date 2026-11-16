# rtl433 helper (FoxSDR GPL helper)

rtl_433's decoders for short-range devices on 433 / 868 / 915 MHz as a standalone command-line program: weather
stations, tyre-pressure sensors (TPMS), remote controls, door / window / alarm sensors, doorbells, energy and water
meters, ... 384 decoders (329 on by default, 55 off by default as upstream decides). Receive only, standard input only:
no radio driver, no network code, no telemetry. Licence GPL-2.0-or-later (see `LICENSE`, `UPSTREAM.md`).

Useful without FoxSDR: any program can pipe I/Q samples into it.

## Build (MSVC, Visual Studio 2022)
```
cmake -S rtl433 -B <build dir> -G "Visual Studio 17 2022" -A x64
cmake --build <build dir> --config Release --parallel 4 --target rtl433-helper
ctest --test-dir <build dir> -C Release --output-on-failure
```

## Run by hand
```
rtl433-helper.exe --foxsdr-helper 1 --format cu8 --rate 250000 [--centre 433920000] < recording.cu8
```
- `--format`: `cs16` (interleaved int16 I,Q little-endian), `cu8` (RTL-SDR native), `cs8`, `cf32`. I/Q only (no audio).
- `--rate`: 50000 to 4000000 Hz. 250000 is rtl_433's native rate and what its pulse widths are tuned for; 1000000 and
  2400000 also work (tested at 1 MS/s). `--centre` (default 433920000) is only reported in `freq` fields.
- `-R n` run only protocol n (repeat), `-R -n` switch protocol n off, `--list-protocols`, `-X <flex spec>` a user-defined
  decoder (decoding only), `-C si|customary`, `--no-meta`, `-v`.
- `--hide-ids` / `--hide-ids-salt <s>`: replace every identifier field by a short hash, drop raw payload fields. The
  manifest says so (`privacy.hide_ids`).
- Exits at end of input, exit code 0; 2 for a bad command line (message on stderr, nothing on stdout).

Output (stdout, UTF-8 JSON Lines, flushed per line):
```
{"helper":"rtl433","version":"25.12.0","protocol":1,"licence":"GPL-2.0-or-later","source":"...","format":"cu8","rate":250000,"centre":433920000,"protocols_built":384,"protocols_enabled":329,"hide_ids":false}
{"t":0.214176,"type":"rtl433","protocol":88,"model":"Toyota","rtl_type":"TPMS","id":"f0d5aee3","status":128,"pressure_PSI":34.250,"temperature_C":29.000,"mic":"CRC","mod":"FSK","freq1":433.928,"freq2":433.881,"rssi":1.092,"snr":28.342,"noise":-28.342,"text":"Toyota type=TPMS id=f0d5aee3 status=128 pressure_PSI=34.25 temperature_C=29 mic=CRC"}
{"t":0.262144,"type":"stats","samples":65536,"bytes":131072,"frames":1,...,"events":1,"protocols_enabled":335}
```
Event `rtl433`: `t` (seconds from the sample count), `protocol` (rtl_433 protocol number), then rtl_433's own fields for
that device (`model`, `id`, `channel`, `battery_ok`, `temperature_C`, `pressure_kPa`, `button`, `mic` ...), then the radio
fields `mod freq freq1 freq2 rssi snr noise`, then `text`. Field names `t`, `type`, `text` used by a decoder are
prefixed `rtl_`. A final `stats` line gives counts. Rolling-code values are shown as the opaque numbers the decoder
reads; nothing recovers keys or predicts codes.

## Not built
Itron ERT (SCM, SCM+, IDM, NetIDM; patent), LoRa / LoRaWAN, decoders that decrypt (IKEA Sparsnas, Govee H5059 / H5112 /
H5310, Apator Metra, ELV ESA); radio drivers, network outputs, capture/replay aids. Details in `UPSTREAM.md`;
`--list-protocols` marks them `-` and keeps the protocol numbers of upstream.

## Tests
`ctest` runs 36 cases. Signals come from `test/gen_signals.c` (written from the protocol descriptions of three
decoders: Silvercrest remote, Prologue temperature sensor, Toyota TPMS), so the decode of a *generated* signal is what
is pinned in `test/expected/`. `RT_UPDATE=1 ctest ...` rewrites the pinned files; review the diff.

## Running the tools on a recording
Recordings are not part of this repository (they may hold other people's transmissions). They are wideband, the decoders want 250 kS/s, so `tools/iqshift.c` (built as `rtl433-iqshift`; a test
tool, not part of the helper) moves one channel to 0 Hz, low-pass filters and decimates by an integer, and
`tools/run_real_recordings.sh` pipes that into the helper and into `tools/summarize_events.py`, which prints counts
only (no ids, no payload values, nothing written to disk):
```
tools/run_real_recordings.sh <file> <cs16|cu8> <file rate> <file centre Hz> <decim> <floor dBFS|native> <channel centre Hz>...
```
A weak B200 file never reaches rtl_433's minimum detection level (about 0.125 of full scale), so `iqshift --floor-dbfs -45`
sets a digital gain that puts the channel's median envelope (a noise-floor estimate) at -45 dBFS; `native` keeps the
recording's own level.
