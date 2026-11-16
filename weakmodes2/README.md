# weakmodes2: FST4, FST4W and Q65 receiver (FoxSDR GPL helper)

A complete, standalone, receive-only command-line program under the GPL (GPL-3.0-or-later). It reads mono audio from standard input and prints one
JSON line per decoded transmission. FoxSDR starts it as a separate process; any program can pipe audio into it. No network code, no transmit code, no window.

Decodes (WSJT-X protocols): **FST4** and **FST4W** (T/R 15, 30, 60, 120, 300, 900, 1800 s; FST4W messages are the 50-bit WSPR type) and **Q65** (tone spacings A .. E,
T/R 15, 30, 60, 120, 300 s, including spread / scatter / EME signals). The decoders are OnAir's C++ re-implementations of WSJT-X's, plus WSJT-X's own
C codec for spread Q65 signals; see `UPSTREAM.md` for sources, commits, licences and every change. The attribution line "Based on OnAir by Kristijonas Adomaitis - https://on-air.dev" is required by OnAir's licence notice
(`ADDITIONAL-TERMS-OnAir.md`); the helper prints it in `--version`, `--help` and its hello line, and it must stay in any legal notice that shows
this helper's licence.

## Build

```
cmake -S weakmodes2 -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4 --target weakmodes2-helper wm2gen
ctest --test-dir build -C Release --output-on-failure
```
Standard C++20 and C11, no libraries beyond the compiler's.

## Run by hand

Audio: `--format s16` (little-endian 16-bit) or `f32`, mono, `--rate` 8000 .. 192000 (12000 recommended). A WAV file from WSJT-X has a 44-byte header to skip:
```
tail -c +45 210115_0058.wav | weakmodes2-helper.exe --foxsdr-helper 1 --format s16 --rate 12000 --decoders fst4:60
```
Real output (WSJT-X's own sample `FST4+FST4W/210115_0058.wav`; the callsigns are real stations; the hello line is shortened here):
```
{"helper":"weakmodes2","version":"0.1.0","protocol":1,"licence":"GPL-3.0-or-later","source":"https://github.com/adswill/OnAir",...,"format":"s16","rate":12000,"decoders":["FST4:60"]}
{"t":60.000,"type":"decode","mode":"FST4","sub":"60","period":60,"slot":0,"freq":1100.5,"snr":-9,"dt":0.3,"call":"N5TM","grid":"EL29","text":"CQ N5TM EL29"}
{"t":60.000,"type":"decode","mode":"FST4","sub":"60","period":60,"slot":0,"freq":1330.5,"snr":0,"dt":0.4,"call":"K9KFR","grid":"EN71","text":"CQ K9KFR EN71"}
{"t":60.009,"type":"stats","samples":720103,"slots_decoded":1,"decodes":2}
```

Options: `--decoders mode:period,...` (default `fst4:60,fst4w:120,q65:60A`; Q65 needs the sub-mode letter), `--fmin/--fmax` (200 .. 3000 Hz),
`--dt-min/--dt-max` (-2.5 .. 2.5 s), `--slot-offset s`, `--start-utc epoch`, `--foxsdr-helper-info`, `--version`, `--help`.

## Slot clock

The decoders work on whole T/R slots. There is no wall clock in the program: the first sample is the start of a slot of every decoder, unless `--slot-offset s` (seconds
already elapsed in the slot at the first sample) or `--start-utc epoch` (UTC time of the first sample; slots then follow the UTC clock of each period and the JSON gets `utc`, the
slot's start) says otherwise. A host that cannot start the stream on a slot edge passes the offset. A slot is decoded when its last sample has arrived (`t` = end of the slot,
seconds of audio since the start). At end of file a part-full slot is decoded if it holds the whole transmission (`"partial":true`); a shorter remainder gives a `status` line.

## Output (pipe protocol version 1, UTF-8 JSON Lines)

First line: the hello. Events: `decode` with `t`, `mode` (FST4, FST4W, Q65), `sub` (`60` for FST4 and FST4W, `60A` for Q65), `period`, `slot` (0 = the slot of the first sample),
`utc` (with `--start-utc`), `freq` (Hz, lowest tone), `snr` (dB in 2500 Hz, as WSJT-X), `dt` (s against the nominal start), `dbm` (FST4W), `call`, `grid`, `partial`, `text` (the message);
`status` (`text`); `stats` at the end. Decodes of a slot are ordered by frequency. The output depends only on the input bytes and the options.

## Sensitivity, measured

Generated signals in white noise, 10 slots per level, the 50 % point by interpolation (about +-1 dB):

| mode | measured 50 % SNR in 2500 Hz | WSJT-X user guide threshold |
|---|---|---|
| FST4-15 / 30 / 60 / 120 / 300 | -18.7 / -22.3 / -25.8 / -29.7 / -33.2 dB | -20.7 / -24.2 / -28.1 / -31.3 / -35.3 dB |
| FST4W-120 / 300 | -30.2 / -33.5 dB | -32.8 / -36.8 dB |
| Q65-15A / 30A / 60A / 120A / 300A | -21.4 / -24.5 / -27.8 / -30.7 / -35.2 dB | -22.2 / -24.8 / -27.6 / -30.8 / -33.8 dB |

FST4 and FST4W are about 2 dB less sensitive than WSJT-X (the 8-symbol combination and a-priori decoding are not built); Q65 matches on Gaussian noise. These numbers come from the
generator in this folder (`test/tools/wm2gen`), which uses OnAir's waveform code, not WSJT-X's. Real recordings (WSJT-X's sample files) that decode: FST4-60 (2 stations), Q65-60A EME (3),
Q65-60D EME (1), Q65-120D rain scatter (1), Q65-60B troposcatter (1), Q65-120E ionoscatter (1) and Q65-300A optical scatter (1; run by hand, not in the test suite: 7 MB). The other sample files
(Q65-30A x4, one 120E, two 60B, and the 30-minute FST4 / FST4W file) do not decode yet.

## Tests

`ctest` runs 45 cases through `test/run_case.cmake`: pinned JSON for generated clean and near-threshold signals, WSJT-X's real recordings (`test/data/README.md`), several signals / slots / decoders,
the UTC slot clock, a short recording, noise only (no false decodes), and the failure paths (bad format, rate, protocol, decoder list; empty input; garbage input as s16 and as f32). Pinned files are
rewritten with `MM_UPDATE=1 ctest ...` (review the diff). `test/tools/sweep.py` and `sweep_all.sh` make the sensitivity tables.
