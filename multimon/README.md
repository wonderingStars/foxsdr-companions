# multimon helper

multimon-ng 1.6.2's decoders as a standalone command-line program that follows the FoxSDR helper pipe protocol
(version 1, see `../docs/PIPE-PROTOCOL.md`). Licence GPL-2.0-or-later (`LICENSE`); what it is built from and every change:
`UPSTREAM.md`. It needs nothing from FoxSDR: any program can pipe audio into it.

## What it decodes
POCSAG 512 / 1200 / 2400 (numeric, alpha, and Skyper text with `-f skyper`), FLEX, FLEX_NEXT, GSC (Golay Sequential Code:
tone, numeric, alpha, voice start / end), EAS / SAME, UFSK1200, CLIPFSK, FMSFSK, AFSK1200 / 2400 (AX.25, APRS with `-A`),
HAPN4800, FSK9600, DTMF, DUALTONE, ZVEI1 / 2 / 3, DZVEI, PZVEI, EEA, EIA, CCIR, MORSE_CW, X10.
Input: **mono audio, 22050 Hz only**, `s16` (little-endian 16-bit) or `f32` (float, -1..1). No resampler: give it 22050 Hz.
Receive only, no network code, no telemetry, no decryption. It never opens a window or a dialog.

## Build (Visual Studio 2022, x64, Release)
```
cmake -S multimon -B build/multimon -G "Visual Studio 17 2022" -A x64
cmake --build build/multimon --config Release --parallel 4
ctest --test-dir build/multimon -C Release --output-on-failure
```
The program is `build/multimon/Release/multimon-helper.exe`. (`gen-ng` and `sigtool`, built too, are test tools.)

## Run by hand
```
multimon-helper.exe --foxsdr-helper 1 --format s16 --rate 22050 -a POCSAG1200 < pocsag1200.raw
```
`--foxsdr-helper-info` prints the manifest (`multimon.helper.json`). With no `-a` every demodulator runs, as in
multimon-ng; on noise `UFSK1200` then prints junk lines, so a host should pass `-a` for the rows it wants.
Convert other audio first, e.g. `ffmpeg -i in.wav -f s16le -ac 1 -ar 22050 out.raw`.

Output: a hello line, then one JSON object per decoded message, flushed per line.
```
{"helper":"multimon","version":"1.6.2","protocol":1,"licence":"GPL-2.0-or-later","source":"https://github.com/EliasOenal/multimon-ng","upstream":"multimon-ng 1.6.2 b5f7d71c5e9b","format":"s16","rate":22050,"demods":["POCSAG1200"]}
{"t":2.229,"type":"POCSAG1200","address":273040,"function":3,"kind":"alpha","message":"+++TIME=0008300324+++TIME=0008300324<NUL>","text":"POCSAG1200: Address:  273040  Function: 3  Alpha:   +++TIME=0008300324+++TIME=0008300324<NUL>"}
```
`t` = seconds of audio read when the message completed (resolution = read size, 0.37 s; `--chunk 256` for finer).
`type` = the demodulator name. `text` = the line exactly as multimon-ng prints it (several lines joined by `\n` for an
AX.25 frame). Control characters in messages appear as `<NUL>`, `<LF>` ... exactly as upstream prints them.

## Events and fields
| type | fields (besides `t`, `type`, `text`) |
|---|---|
| `POCSAG512` `POCSAG1200` `POCSAG2400` | `address` (number or null), `function` (0-3 or null), `kind` (`numeric` `alpha` `skyper` `none`), `message` (absent when `none`). A continuation block has `address` and `function` null |
| `GSC` | `address` (string, as printed incl. check digit), `function`, `kind` (`tone` `numeric` `alpha` `voice`), `message` (numeric, alpha), `state` (`start` `end`, voice) |
| `FLEX` | `baud`, `levels`, `fragment`, `phase`, `cycle`, `frame`, `capcode`, `capcodes` (array: group calls list all), `kind` (`ALN` `NUM` `TON` `UNK`), `message`; `time` only with `--start-time` |
| `FLEX_NEXT` | `baud`, `levels`, `cycle`, `frame`, `phase`, `capcode`, `capcodes`, `address_type`, `kind` (`ALN` `NUM` `SNUM` `NNUM` `HEX` `SMSG` `INS` `FLEXTIME` `SEC:...`), `flags` (fragment, K checksum, signature ...), `message`; `time` only with `--start-time` |
| `EAS` | `kind` (`header` `eom`), `message`, and for headers `originator`, `event`, `locations` (array), `duration`, `issued`, `sender` |
| `AFSK1200` `AFSK2400` `AFSK2400_2` `AFSK2400_3` `HAPN4800` `FSK9600` | `kind` `ax25`: `from`, `to`, `via`, `frame`, `pid`, `info_lines` (array); `kind` `aprs` (with `-A`): `source`, `dest`, `path`, `info` |
| `DTMF` | `digit` |
| `ZVEI1` `ZVEI2` `ZVEI3` `DZVEI` `PZVEI` `EEA` `EIA` `CCIR` | `digits` |
| `X10` | `bits`, `house`, `unit` |
| `UFSK1200` `CLIPFSK` `FMSFSK` `DUALTONE` `MORSE_CW` | `message` (MORSE_CW: one event per word) |

## Options
`--foxsdr-helper 1` (required), `--format s16|f32`, `--rate 22050`, `--centre` (ignored), `-a NAME` / `--demod NAME`
(repeat), `-s NAME`, `-c`, POCSAG `-f numeric|alpha|skyper|auto`, `-b 0..2`, `-p`, `-e`, `-u`, `-P auto|normal|inverted`,
`-C US|FR|DE|DK|SE|SI`, CW `-o -d -g -x -y`, `-A`, `-j`, `-v level` (diagnostics on stderr), `--chunk N`,
`--start-time unix-seconds`. Wrong `--format` / `--rate` / protocol / demodulator: message on stderr, exit code 2, nothing
on stdout. Exit 0 at end of input.

## Determinism and time
The same input gives the same output. FLEX and FLEX_NEXT print a time of day; upstream takes it from the wall clock.
Here it comes from a clock that follows the sample count: without `--start-time` FLEX prints no time stamp and the
FLEX_NEXT line contains `1970-01-01 00:00:xx+00:00` inside `text` (the JSON field `time` is left out then); with
`--start-time N` the clock starts at N (UTC) and both decoders print and report it. Those stamps move with the read
chunk size; nothing else does.

## Checking against I/Q recordings
`test/tools/fmdemod.exe <capture.cs16|capture.cu8> <rate Hz> <offset Hz> [--format cs16|cu8] [--seconds S]` FM-demodulates
one channel of an interleaved I/Q capture (cs16 = 16-bit, cu8 = RTL-SDR unsigned 8-bit; any sample rate of 200 kHz or
more) to 22050 Hz mono s16 on stdout, ready to pipe into the helper. `test/tools/recording_counts.py` runs that pipe and
prints counts only (see its header). `sigtool fmmod` FM-modulates audio the other way round; it was used to prove the chain
(a generated POCSAG page decoded from a synthetic 1 MS/s cs16 and 1.024 MS/s cu8 file at the right offset and not 12.5 kHz
away). Not part of the CTest cases: real traffic is never pinned.

