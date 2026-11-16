# js8: JS8Call receive decoder as a FoxSDR GPL helper

A standalone command-line program that decodes **JS8** (JS8Call's weak-signal keyboard mode) from 12 kHz audio. It is
the receive side of JS8Call's C++ decoder (https://github.com/js8call/js8call, commit `a7ff1be`), ported to plain
C++ for MSVC. Licence **GPL-3.0-only** (`LICENSE`); provenance and every change in `UPSTREAM.md`.

It is receive only: no transmit code, no rig control, no network, no windows. It works without FoxSDR: anything that can
write raw audio to a pipe can use it.

## What it decodes

Speeds (JS8Call's names): **A Normal 15 s**, **B Fast 10 s**, **C Turbo 6 s**, **E Slow 30 s** (the default),
and **I Ultra 4 s** (only if asked for: `--modes ABCEI`; JS8Call ships it switched off and it is untested on real
signals). Each speed has its own period, so one audio stream can be decoded at several speeds at once. Per period it
does the Costas synchronisation search over the whole audio band (`--nfa`..`--nfb`), the LDPC(174,87) belief-propagation
decode with CRC-12 check, successive signal subtraction over up to three passes, and unpacks every frame with the
varicode, the 262144-word JSC dictionary and the callsign/grid/command packing.

Frame types: `heartbeat` (HB and the CQ variants), `compound`, `compound_directed`, `directed` (all 32 commands),
`data` (Huffman or JSC text), `unknown` (a CRC-valid frame that is none of those: raw 12 characters are still reported).
Multi-frame messages are not reassembled: one line per frame, with `first` and `last` flags.

## Running it by hand

```
js8-helper.exe --foxsdr-helper 1 --format s16 --rate 12000 [--modes ABCE] [--start-time <epoch s>] < audio.raw
js8-helper.exe --foxsdr-helper-info          (prints the manifest)
js8gen.exe --convert recording.wav audio.raw  (test tool: 12 kHz mono 16-bit WAV to raw s16)
```

* `--format s16` (little-endian 16-bit mono) or `f32` (float, +-1.0 is full scale); `--rate 12000` only.
* Periods are aligned to **UTC**: a Normal period starts at :00, :15, :30, :45. Give `--start-time` as the UTC of the
  first sample (seconds since 1970, fractions allowed); without it the first sample is taken to be a period boundary.
  A period is decoded when it is complete; a final partial period is decoded if it holds a whole frame.
* `--nfa/--nfb` audio frequency window (default 100..4910 Hz), `--nfqso` frequency tried first (1500),
  `--text-events` adds a reserved `text` event per frame (for the generic bridge).

Real output (the first test file, `FOXSDR_SAMPLES_DIR/A_1_4.wav` converted, cut to the first fields):

```
{"helper":"js8","version":"1.0.0","protocol":1,"licence":"GPL-3.0-only","source":"https://github.com/js8call/js8call","upstream":"JS8Call a7ff1be0b389d287fdc56e2ea0d06962aa68127d","format":"s16","rate":12000,"modes":"A"}
{"t":15.000,"type":"js8","mode":"A","speed":"Normal","slot_t":0.000,"utc":"1970-01-01T00:00:00Z","freq":1866.1,"dt":0.10,"snr":-28,"quality":0.77,"low_confidence":false,"bits":3,"first":true,"last":true,"data_flag":false,"frame":"2Y-wUW3FOjFp","frame_type":"heartbeat","text":"KN4ZXG: @HB HEARTBEAT FM16 ","from":"KN4ZXG","grid":"FM16","cq":"HB"}
{"t":15.000,"type":"js8","mode":"A","speed":"Normal","slot_t":0.000,"utc":"1970-01-01T00:00:00Z","freq":702.6,"dt":0.10,"snr":-22,"quality":0.80,... "frame_type":"directed","text":"K0OG: KN4CRD SNR +02 ","from":"K0OG","to":"KN4CRD","cmd":"SNR","num":"+02"}
...
{"t":15.000,"type":"stats","samples":180000,"slots":1,"frames":5,"frames_by_mode":{"A":5}}
```

## Output (protocol version 1)

First line the hello (`helper`, `version`, `protocol`, `licence`, `source`, `upstream`, `format`, `rate`, `modes`),
then, in order of the end of each period (ties: A, B, C, E, I), one `js8` event per decoded frame, and one `stats`
line at end of input. `{"type":"js8", ...}` fields:

| field | meaning |
|---|---|
| `t` | seconds since the first sample, at the end of the decoded period |
| `mode`, `speed` | `A`/`Normal`, `B`/`Fast`, `C`/`Turbo`, `E`/`Slow`, `I`/`Ultra` |
| `slot_t`, `utc` | start of the period: seconds since the first sample, and UTC (ISO 8601; relative to 1970 unless `--start-time` is given) |
| `freq` | audio frequency of the lowest tone, Hz |
| `dt` | time offset of the signal against the nominal start, seconds (JS8Call's `xdt - ASTART`) |
| `snr` | JS8Call's own SNR figure in dB (see below) |
| `quality` | 1 - (hard errors in the LDPC codeword / 60); `low_confidence` is true under 0.17, JS8Call's threshold for showing a frame in brackets |
| `bits`, `first`, `last`, `data_flag` | the three frame bits (1 first frame of a message, 2 last frame, 4 data frame) |
| `frame` | the 12 characters of the 6-bit alphabet that were received |
| `frame_type`, `text` | `heartbeat`, `compound`, `compound_directed`, `directed`, `data`, `unknown`, and JS8Call's display text for the frame |
| `from`, `to`, `cmd`, `num`, `grid`, `cq` | parsed parts where the frame has them (`cmd` without its leading space; `num` as JS8Call formats it, e.g. `-05`; `cq` is `HB`, `CQ`, `CQ DX` ...) |

The helper reads stdin until end of file and exits 0; it is deterministic for a given input (the same bytes in any read
size give the same lines). Bad command lines exit 2 with a message on stderr and print nothing on stdout.

## Decode threshold (measured here, generated signals)

`tools/sweep.sh` makes 12 noisy files per level (white Gaussian noise, one heartbeat at different audio frequencies,
nominal SNR in a 2500 Hz bandwidth) and counts correct decodes of the heartbeat. Full table:
`test/threshold_sweep_2026-11-12.txt`.

| speed | all 12 decoded down to | about half decoded at | last level with any decode |
|---|---|---|---|
| A Normal | -17 dB | -19 dB (7/12) | **-20 dB** (3/12) |
| B Fast | -15 dB | -16 dB (10/12), -17 dB 3/12 | **-18 dB** (1/12) |
| C Turbo | -13 dB | -14 dB (5/12) | **-15 dB** (1/12) |
| E Slow | -20 dB | -22 dB (7/12) | **-23 dB** (1/12) |
| I Ultra | -9 dB (-10 dB 11/12) | -11 dB (8/12) | -13 dB (1/12) |

So for Normal speed the decoder still decodes at **-20 dB** (SNR in 2500 Hz), reliably down to -17 dB. Caveat:
the `snr` field is JS8Call's own scale and reads lower than the true SNR of these generated signals by a speed-dependent
amount: at 50% decode Normal reports about -26, Fast -27, Turbo -30, Slow -28, Ultra -29 (JS8Call's documented receive
thresholds in its source are -24, -22, -20, -28 and -18 on that scale). Frequency drift, fading and real receivers
are not in these numbers.

## Tests

```
cmake -S js8 -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

31 cases; each pipes a file into the helper and compares its JSON with a pinned file in `test/expected/` (after
`JS8_UPDATE=1` rewrites them, review `git diff`): JS8Call's sample recordings, generated clean and noisy signals of every
frame kind and speed, float input, a 1001-byte-piece pipe, a one-minute multi-period stream (file and pipe), UTC alignment, the frequency window, text events, empty input,
1.4 MB of random bytes, six bad command lines, and the manifest. `js8gen` (the test tool) makes the generated files with
JS8Call's own encoder; it is not part of the helper.

## What is left out

Transmit (the helper has none; `js8gen` only writes files), rig control, UDP/TCP APIs, APRS-IS, PSKReporter, spot upload,
multi-frame message reassembly and the buffered-command checksums, sample rates other than 12 kHz, I/Q input, WAV input
(convert with `js8gen --convert`), and the extra decoder stages of JS8Call-improved (soft combining, interference
cancellation, whitening, decoder-aided demodulation) which find more frames in weak or crowded periods.
