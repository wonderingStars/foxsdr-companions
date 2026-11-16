# weakmodes: FT2L, MSK144 and JT9 receive-only decoder (FoxSDR GPL helper)

A complete stand-alone command-line program: mono audio at 12000 Hz on standard input, one JSON line per decoded
message on standard output. It follows the FoxSDR helper pipe protocol version 1 (see `docs/PIPE-PROTOCOL.md` in this
repository) but does not need FoxSDR: any program can pipe audio into it.

Licence: GPL-3.0-or-later (`LICENSE`). The decoders are C++ ports of WSJT-X (Joe Taylor K1JT and the WSJT Development
Group) and of the FT2L code in wsjtx-ft2libre. Sources, commits and every change: `UPSTREAM.md`.
Receive only: there is no transmit code, no network code (no PSKReporter), no rig control.

## What it decodes

| mode | what | period | notes |
|---|---|---|---|
| `FT2L` | "FT2" as implemented by wsjtx-ft2libre: 4-GFSK, 41.667 baud, 103 symbols, LDPC(174,91), 77-bit messages, 3.75 s periods | 3.75 s | **Reverse-engineered format. It may not match other FT2 software, and has not been tested against real FT2 traffic.** |
| `MSK144` | meteor-scatter MSK, 2000 baud, 72 ms frames, LDPC(128,90), 77-bit messages | 15 s | short-ping decoder and frame averaging, as WSJT-X's real-time decoder |
| `JT9` | 9-FSK, convolutional code K=32 r=1/2 with the Fano sequential decoder, 72-bit messages | 60 s | the 1-minute sub-mode (JT9A) only |

FT2L and MSK144 messages use the WSJT-X 77-bit format (standard calls and grids, reports, RRR / RR73 / 73, free text,
ARRL Field Day, RTTY contest, non-standard calls, EU VHF); JT9 uses the older 72-bit format.

## Run it by hand

```
weakmodes-helper --foxsdr-helper 1 --format s16 --rate 12000 [--mode ft2,msk144,jt9] < audio.raw
weakmodes-helper --foxsdr-helper-info
```

`audio.raw` is raw 16-bit little-endian mono samples at 12000 Hz (a WAV file with its 44-byte header cut off), or
float samples with `--format f32` (full scale +-1.0). `--format` other than `s16`/`f32` or `--rate` other than 12000
makes the program print a message on stderr and exit with status 2.

Options: `--mode` comma list of `ft2`, `msk144`, `jt9` (default all three); `--fmin`, `--fmax` audio search range in Hz
(default 200..4000; MSK144 uses WSJT-X's own 1500 Hz centre and +-100 Hz unless you change them: then the centre is the
middle of your range); `--depth 1..3` (default 3: deepest search; FT2L depth 2 has no OSD, depth 1 one pass without
subtraction); `--offset s` seconds from the first sample to the first period boundary (default 0); `--chunk n`
samples per read (default 4096; the output does not depend on it except the emission time `t`).

**Timing.** Periods are counted from the first sample, plus `--offset`. FT2L (3.75 s) and JT9 (60 s) decode a period
when it is complete, so the audio must start on a period boundary of the radio's UTC clock, or you must give the
offset. The `dt` field is the offset of the signal against that boundary, as WSJT-X shows it. MSK144 restarts its
15 s sequence in the same way.

**CPU.** MSK144 and JT9 are light. FT2L decodes every 3.75 s and its deepest search (`--depth 3`, OSD) took about
8 s for 30 s of busy noisy audio on a loaded PC; `--depth 2` (belief propagation only) about 2 s, `--depth 1` about 1 s.

## Output

First line, the hello: `{"helper":"weakmodes","version":"1.0.0","protocol":1,"licence":"GPL-3.0-or-later","source":...,
"upstream":...,"format":"s16","rate":12000,"modes":["FT2L","MSK144","JT9"],"note":...}`. Then one line per message:

```
{"t":7.500,"type":"text","mode":"FT2L","freq":1499.9,"snr":-16,"dt":0.10,"tstart":4.350,"text":"CQ K1ABC FN42"}
```

`t` is the sample clock (seconds of audio read) when the line was written; `tstart` the sample-clock time of the
transmission or ping; `freq` the audio frequency in Hz; `snr` in dB in a 2500 Hz bandwidth as each WSJT-X decoder
estimates it (the FT2L estimate is the upstream formula and reads several dB low on the test signals); `dt` the time
offset in seconds as WSJT-X prints it; `text` the message; `note` (MSK144) which decoder got it. The event type is
`text`, so a generic bridge can show the lines without knowing the fields. Nothing is printed for a period with no
decode. stderr carries human-readable log lines; end of input ends the program with status 0.

## Build and test

```
cmake -S weakmodes -B <build dir> -G "Visual Studio 17 2022" -A x64
cmake --build <build dir> --config Release --parallel 4
ctest --test-dir <build dir> -C Release --output-on-failure
```

The tests use signal generators ported from the same upstream (`ft2l_gen`, `msk144_gen`, `jt9_gen`: test tools that
only write WAV files), the WSJT-X project's own real sample recordings for MSK144 and JT9, and check the pinned JSON
lines, the failure paths (empty input, garbage, wrong `--format` / `--rate`) and independence from the read size.
`FOXSDR_SAMPLES_DIR/` holds the real recordings (GPL-3.0, from the WSJT-X sample set). The measured decode thresholds are in
`UPSTREAM.md`.

## What is left out

See `UPSTREAM.md`: no MSK40, no JT9 sub-modes B..H, no JT65, no FT8/FT4 (FoxSDR has its own), no SuperFox, no a-priori
decoding that needs a station call, no transmit, no network.
