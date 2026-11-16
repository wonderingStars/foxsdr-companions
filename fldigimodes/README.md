# fldigimodes -- fldigi's Olivia, Contestia, MFSK, MT63 and THOR receivers, and RSID, as a FoxSDR GPL helper

A complete command-line program under the **GNU GPL v3 or later** (see `LICENSE`, `UPSTREAM.md`). It reads audio on standard
input and prints JSON Lines on standard output (FoxSDR helper pipe protocol version 1, `docs/PIPE-PROTOCOL.md`). It decodes with
the modem code of **fldigi 4.1.23** (commit `61b97f4133c4`, https://github.com/w1hkj/fldigi). **Receive only**: the
transmit code is not in the program (it is compiled into the test tool only, to make test audio).

## Modes (`--mode`)
| family | modes | notes |
|---|---|---|
| Olivia | `olivia-<tones>-<bw>`: tones 2, 4, 8, 16, 32, 64, 128, 256; bw 125, 250, 500, 1000, 2000 | also `--mode olivia --tones 8 --bandwidth 500`; the same range fldigi's configuration allows (index 0..7 = 2..256 tones) |
| Contestia | `contestia-<tones>-<bw>`, same values | |
| MFSK | `mfsk4 mfsk8 mfsk11 mfsk16 mfsk22 mfsk31 mfsk32 mfsk64 mfsk128 mfsk64l mfsk128l` | MFSK16 uses the interleaver and varicode of fldigi's `mfsk` class |
| MT63 | `mt63-500s mt63-500l mt63-1000s mt63-1000l mt63-2000s mt63-2000l` | s = short (32), l = long (64) interleave; `--mt63-long-integral` |
| THOR | `thormicro thor4 thor5 thor8 thor11 thor16 thor22 thor25x4 thor50x1 thor50x2 thor100` | |
| RSID | `--mode rsid` (only the detector) or alongside any mode (`--rsid on`, the default) | |

**Audio rate: 8000 Hz**, mono, `--format s16` (little-endian 16-bit) or `f32`. Five modes run at **11025 Hz** (`mfsk11`,
`mfsk22`, `thor5`, `thor11`, `thor22`): give `--rate 11025` for those; any other rate is refused with a message. The RSID
detector takes either rate (it resamples to 11025 Hz internally).

## Running it by hand
```
fldigimodes.exe --foxsdr-helper 1 --format s16 --rate 8000 --mode olivia-8-500 --freq 1500 < audio.raw
```
`audio.raw` is raw 16-bit mono samples (strip the WAV header: `sigtool wav2raw in.wav out.raw`). Options:
`--freq <Hz>` audio centre of the signal (default 1500; Olivia/Contestia search ±8 tone spacings round it, MFSK/THOR track
it with AFC), `--squelch <0..100>|off` (fldigi's slider; default 5, on, as fldigi starts), `--reverse` (reversed sideband),
`--sync-margin`, `--sync-integ` (Olivia/Contestia, defaults 8 and 4), `--rsid on|off`, `--rsid-search wide|narrow`
(default wide: the whole band; narrow is ±200 Hz round `--freq`, fldigi's default), `--foxsdr-helper-info` (the manifest).
Real output (an Olivia 8/500 test signal made by fldigi's own transmitter; the lines are cut at 150 characters here):
```
{"helper":"fldigimodes","version":"0.1.0","protocol":1,"licence":"GPL-3.0-only","source":"https://github.com/w1hkj/fldigi", ...
{"t":15.296,"type":"text","text":"CQ C","mode":"olivia-8-500","freq":1500.0,"offset":0.00,"sync":9.36,"metric":32}
{"t":17.344,"type":"text","text":"Q de F","mode":"olivia-8-500","freq":1500.0,"offset":0.00,"sync":9.33,"metric":32}
```

## Events
- hello (first line): `helper version protocol licence source upstream format rate mode freq rsid`.
- `text`: `text` (UTF-8; bytes above 127 are read as ISO-8859-1; control codes other than BS, TAB, LF, CR are dropped),
  `mode`, `freq` (Hz, after AFC), `offset` (Hz; Olivia/Contestia: the sync search's tuning offset, MFSK/THOR: AFC shift,
  MT63: the modem's total offset), `sync` (Olivia/Contestia: Jalocha's sync S/N, linear; MT63: the FEC S/N), `snr` (dB;
  MFSK, THOR: fldigi's own "s/n" estimate; MT63: 10 log10 of the FEC S/N), `metric` (fldigi's 0..100 metric). A field a
  modem does not provide is absent. Text is collected until a line feed, 80 characters, or 2 seconds of audio, so one event is
  a phrase, not a character. `t` is seconds of audio read so far (from the sample count).
- `rsid`: `name` (fldigi's RSID name, e.g. `MFSK16`, `OLIVIA_8_500`), `code`, `mode` (fldigi's short mode name), `known`
  (false: fldigi has no modem for that ID), `freq` (Hz), `extended` (found after an ESCAPE code, in the second table), `eot`.
- `status` (`text`): a picture is being sent (MFSK, THOR); its pixels are skipped. `stats` at the end: `samples chars
  text_events rsid_hits pictures_skipped`.
Output is the same whatever the size of the reads: samples are processed in fixed blocks of 512.

## RSID names table
`sigtool rsid-tables` lists it (pinned in `test/expected/rsid_tables.txt`): **145 entries in the first table, 57 in the second,
202 names in all**; 151 of them map to a mode fldigi has, 51 are IDs fldigi lists but has no modem for.

## How low can the signal go (measured on the development machine, test signals only)
The test signal is made by fldigi's own transmitter for the same mode, with seeded white Gaussian noise added over the whole
audio band (the mix is scaled to fit 16 bit without clipping, which keeps the ratio). **SNR = signal power / noise power in a 2500 Hz
bandwidth.** "Decodes" = the message `CQ DE FOXSDR TEST 123` comes out unchanged in the text; the figure is the lowest whole dB at
which seeds 1, 2 and 3 all decoded it, scanning down from +6 dB in 1 dB steps until the first failure (`test/tools/sweep.py`).
At that level seeds 4 to 8 were also tried: 24 of the 35 modes decoded all five, the others 2 to 4 of five (2 of 5: mfsk128, thor4;
3 of 5: mfsk31, mfsk32, mt63-1000s, thor5), so read each figure as accurate to about 1 to 2 dB. Default settings (squelch on at 5, RSID on).

| mode | lowest SNR (dB) | mode | lowest SNR (dB) | mode | lowest SNR (dB) |
|---|---|---|---|---|---|
| olivia-4-250 | -12 | mfsk4 | -15 | thormicro | -20 |
| olivia-8-500 | -11 | mfsk8 | -12 | thor4 | -18 |
| olivia-16-500 | -13 | mfsk11 | -14 | thor5 | -16 |
| olivia-32-1000 | -13 | mfsk16 | -12 | thor8 | -14 |
| olivia-64-2000 | -12 | mfsk22 | -7 | thor11 | -12 |
| contestia-4-250 | -11 | mfsk31 | -10 | thor16 | -11 |
| contestia-8-500 | -10 | mfsk32 | -9 | thor22 | -10 |
| contestia-16-1000 | -9 | mfsk64 | -6 | thor25x4 | -9 |
| contestia-32-1000 | -12 | mfsk128 | -4 | thor50x1 | -6 |
| mt63-500s | -9 | mt63-1000s | -6 | thor50x2 | -6 |
| mt63-500l | -9 | mt63-1000l | -6 | thor100 | -3 |
| mt63-2000s | -2 | mt63-2000l | -3 |  |  |

RSID: detected at -6 dB in the test (`rsid_noisy`); its threshold was not swept.
These are results for fldigi's modems against fldigi's own transmitter on a clean additive-noise channel: no fading, no
frequency drift, no interference. Real band conditions will be worse.

## Limits
- Real recordings: none were available here (no Olivia, Contestia, MFSK, MT63 or THOR recording);
  everything above is proven on generated signals only.
- At the end of the input, Olivia, Contestia and MT63 hold back the text still in their pipeline (MT63 up to its interleave length,
  several seconds; Olivia a few FEC blocks): keep feeding silence after the last symbol, as a live radio does.
- Before a signal is locked, the first characters can be noise characters (fldigi does the same); the squelch (`--squelch`)
  trades that against sensitivity.
- Not decoded: pictures (MFSK, THOR), THOR secondary text, anything outside the listed modes (PSK, RTTY, DominoEX ...).

## Build and test
```
cmake -S fldigimodes -B <build> -G "Visual Studio 17 2022" -A x64
cmake --build <build> --config Release --parallel 4
ctest --test-dir <build> -C Release --output-on-failure
```
Windows, MSVC 2022, no outside libraries. The test tool `sigtool` (built with the tests) makes WAV test audio with fldigi's
transmitters. `MM_UPDATE=1 ctest ...` rewrites the pinned expectations (review the diff).
