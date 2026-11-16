# mdc: MDC-1200 and Fleetsync I decoder (FoxSDR GPL helper)

A standalone command-line program. It reads mono audio from an analogue FM voice channel on standard input and prints one JSON line
per decoded packet. It is receive only, makes no network connection, opens no window and needs nothing from FoxSDR: any program can
pipe audio into it. FoxSDR starts it as a separate process (pipe protocol version 1, `docs/PIPE-PROTOCOL.md` in this repository).

Licence: GPL-2.0-only (`LICENSE`). Built from Matthew Kaufman's MDC and Fleetsync decoder libraries; see `UPSTREAM.md` for the
project, commits, files taken and every change.

## What it decodes
- **MDC-1200** (Motorola, 1200 baud MSK on the voice channel, tones 1200 / 1800 Hz): preamble and sync detection on five parallel
  bit-phase decoders, 4-point demodulation, de-interleave of the 112-bit block, convolutional-code error correction, CRC-16
  check, single packets and double packets (op 0x35 / 0x55 followed by a second block). Each packet gives operation code,
  argument, unit ID, how many bits error correction repaired, and the CRC result.
- **Fleetsync I** (Kenwood, 1200 baud): sync 0xAAAA23EB, 64-bit blocks with CRC-15 and parity, fleet and unit IDs of sender and
  target, command and sub-command, the all-call flag, the raw message bytes and the text payload of data messages (command 0x42).

Operation codes and arguments are printed as numbers (`op`, `arg`, and the same in hex); the helper has no table of operation names.

## Left out, and why
- **Fleetsync II**: the same upstream routine decodes it, but `trunkfmt` in this repository decodes it; this
  helper counts those messages as `fleetsync2_ignored` in the stats line and prints nothing for them.
- **An encoder in the helper**: none. Upstream's encoder is only in the test tools (`mdc-gen`, `mdc-upstream-selftest`), not shipped.
- No decryption exists in MDC-1200 or Fleetsync I, so none is attempted. No MDC-1200 data-message text (the upstream library does
  not define any), no Fleetsync I GPS decode (the payload is reported as raw text and bytes).

## Run it by hand
```
mdc-helper.exe --foxsdr-helper 1 --format s16 --rate 48000 [--hide-ids] [--no-mdc] [--no-fleetsync] [--report-bad] < audio.raw
mdc-helper.exe --foxsdr-helper-info          (prints the manifest, mdc.helper.json)
```
`audio.raw` is raw mono samples, 16-bit little-endian (`--format s16`) or 32-bit float (`--format f32`, full scale +-1.0), no header.
A WAV file works after stripping the 44-byte header, for example `tail -c +45 in.wav | mdc-helper.exe ...` for a plain PCM WAV.
End of input ends the run (exit 0). `--centre <Hz>` is accepted and ignored. A wrong `--format` (anything but s16 or f32),
`--rate` outside 8000 to 96000, an unknown argument or no decoder selected gives exit code 2 and a message on stderr.

Real output of this build (a generated signal, see "What has been tried on real signals"):
```
{"helper":"mdc","version":"1.0.0","protocol":1,"licence":"GPL-2.0-only","source":"https://github.com/atmatthewat/mdc-encode-decode","upstream":"mdc-encode-decode 45fc9095fbde (MDC-1200); fsync-mdc1200-decode 287356a99d4 (Fleetsync I)","format":"s16","rate":48000,"decoders":["mdc1200","fleetsync1"]}
{"t":0.3734,"type":"mdc1200","frames":1,"op":1,"op_hex":"01","arg":128,"arg_hex":"80","unit":"1234","ecc_bits":0,"crc_ok":true}
{"t":0.9405,"type":"mdc1200","frames":2,"op":53,"op_hex":"35","arg":137,"arg_hex":"89","unit":"4321","op2":131,"op2_hex":"83","arg2":13,"arg2_hex":"0D","unit2":"1234","ecc_bits":0,"crc_ok":true}
{"t":1.2400,"type":"stats","samples":59522,"mdc1200":2,"mdc1200_double":1,"mdc1200_ecc_corrected":0,"mdc1200_crc_fail":0,"fleetsync1":0,"fleetsync2_ignored":0}
```

## Output (JSON Lines, UTF-8, flushed per line)
`t` is seconds from the sample count at the time the packet was complete (Fleetsync: about 20 ms after the message ended).
- `mdc1200`: `frames` (1 or 2), `op`, `op_hex`, `arg`, `arg_hex`, `unit` (4 hex digits as
  a string), for double packets also `op2`/`op2_hex`, `arg2`..., `unit2` (the second block: op, argument and unit ID taken
  from its four bytes), `ecc_bits`, `crc_ok` (always true: a block that fails the CRC is never reported as a packet).
- `mdc1200_crc_fail` (only with `--report-bad`): a block failed its CRC after error correction. Several bit-phase decoders try the same
  block, so failures within 150 ms of a good packet are dropped and the rest are merged into one event. Noise can also cause these.
- `fleetsync1`: `cmd`, `subcmd`, `from_fleet`, `from_unit`, `to_fleet`, `to_unit` (-1 = none or all), `all_call`, `baud`, `raw` (hex of
  the message bytes), `payload` (data messages only, printable ASCII, others as \u00XX), `crc_ok` (always true).
- `stats` (last line): sample count and the counters. The hello line is first, as the protocol says.

`--hide-ids` replaces unit IDs by `u1`, `u2`, ... and fleet IDs by `f1`, `f2`, ... in order of first appearance: the same ID is always
the same token within one run, so counts and conversations stay readable. The raw hex, the Fleetsync payload text and `op2`/`arg2`
are not changed (the `raw` field of a Fleetsync message still contains the IDs in coded form; do not rely on `--hide-ids` for
anonymising `raw`: leave it out of anything you publish).

## Accepted rates
`--rate` 8000 to 96000 Hz (any whole number), `--format s16` or `f32`, mono. Recommended: **48000** (also 12000, 16000, 24000, 32000,
44100, 96000). Measured on generated clean signals, 20 slightly different positions of the packet relative to the decoder's bit clock,
packet alone in the file (the sweep script was not kept; the numbers come from this build):

| rate Hz | single 20/00/0042 | single 01/80/1234 | double 35.. call alert | double 55.. call alert ack |
|---|---|---|---|---|
| 8000 | 14 of 20 | 20 | 20 | 14 of 20 |
| 11025 | 20 | 20 | **0 of 20** | 20 |
| 12000 | 20 | 20 | 20 | 20 |
| 16000 | 20 | 20 | 20 | 20 |
| 22050 | 20 | 20 | 18 of 20 | 20 |
| 24000 to 96000 (24000, 32000, 44100, 48000, 96000) | 20 | 20 | 20 | 20 |

So the upstream decoder is not equally good at every rate and for every packet: at 8000 and 11025 Hz some clean packets are lost, and a
different double-packet content (op2 0x80, arg2 0x15) failed at half the positions at 16000 Hz in an earlier sweep (packets in the
pinned tests were chosen from what decodes; that is stated here so it is not a surprise). Use 48000 or another rate of 24000 and up.
Fleetsync I decoded all 20 of 20 positions at 8000, 16000, 24000, 48000 and 96000 for a one-block and a four-block message.

**Level.** The Fleetsync decoder has a fixed edge-detector threshold on the change between neighbouring samples; upstream is
written for 8000 Hz 8-bit input. This helper scales the Fleetsync input by rate/8000 so that every rate has upstream's 8 kHz
sensitivity. Measured on the generated signal: a Fleetsync tone below about 2 % of full scale is not decoded (0 of 2 at 1 % and 2 %
amplitude), 3 % and above is, at both 8000 and 48000 Hz. Give it normal audio levels. The MDC-1200 decoder compares signal levels
rather than testing a threshold; its low-level behaviour was not measured.

**Noise.** Generated MDC-1200 with white noise of 5 %, 12 % and 20 % of full scale rms (the signal is 68 % of full scale at the peak)
at 48000 Hz still decodes all six packets, with 1 to 3 packets needing error correction. Fleetsync I is much weaker: it decoded at
2 % noise, and in a small test of three seeds at 5 % and 8 % it failed in two of three (not pinned).

## What has been tried on real signals
**Nothing.** No real recording of MDC-1200 or Fleetsync I was available. Everything
that is proven is proven on **generated** audio:
- MDC-1200: made by upstream's own encoder (`mdc_encode.c`), so decoder and encoder are by the same author and share any shared
  misunderstanding of the protocol. The upstream round-trip self test passes (`upstream_selftest`). Whether a real Motorola radio's
  audio, with its filters, deviation limiting, de-emphasis and a real squelch tail, decodes at the same rate is **unverified**.
- Fleetsync I: there is no Fleetsync encoder upstream. `test/tools/mdc_gen.c` generates it **from the decoder's own source** (sync
  word, 64-bit blocks, the CRC routine of `fsync_common.c`), as exact 1200 / 1800 Hz continuous-phase FSK. A test made from the
  decoder cannot find a misreading of the real protocol; it proves the decoder's chain, CRC check and the helper's output only.
- Operation codes are printed as numbers; no name table is shipped.

## Tests (CTest, 41 cases)
```
cmake -S mdc -B <build dir> -G "Visual Studio 17 2022" -A x64
cmake --build <build dir> --config Release --parallel 4
ctest --test-dir <build dir> -C Release --parallel 4 --output-on-failure
```
- `upstream_selftest`: Kaufman's own encode-to-decode test (single, double, polled and callback), unchanged.
- `mdc_ops_*`: generated packets (a range of operation codes, plus an unknown code 0x7E, and four double packets) at 48000 Hz
  (22 packets), and a six-packet set at 8000, 12000, 16000, 24000 and 96000 Hz, as `f32`, and fed through a pipe in 777-byte
  writes; pinned JSON lines (`test/expected/`). I checked the 22 pinned packets against what was sent, field by field.
- `mdc_noise_05/12/20` (noisy, fixed seeds), `mdc_ecc_flip1/2` (1 and 2 bits of the coded block flipped: repaired, `ecc_bits`),
  `mdc_crc_fail` and `mdc_crc_fail_quiet` (a wrecked block: no packet, a `mdc1200_crc_fail` event only with `--report-bad`, a count
  in stats either way), `mdc_hide_ids`, `mdc_only`.
- `fs1_*`: Fleetsync I at 8000, 16000, 48000 Hz, `f32`, noisy, `--hide-ids`, `--no-mdc`, a wrong-CRC message that must not be reported;
  `mixed` (both protocols in one file).
- Failure paths: `empty_input` (hello and stats, exit 0), `garbage_noise` and `garbage_noise_8k` (8 seconds of loud noise: exit 0, at
  most 8 lines out), `truncated_mdc`, `truncated_fs1`, `truncated_fs1_late` (input cut inside a packet), `odd_byte` (a stray byte at
  the end: reported on stderr, ignored), wrong `--format` (cs16, junk), wrong `--rate` (low, high), unknown argument, no decoder
  selected (all exit 2 with a message and nothing on stdout, no dialog), `manifest_info` (`--foxsdr-helper-info` equals
  `mdc.helper.json`), `wav_writer` (the test tool's WAV output).
- Two runs on the same input are byte-identical (`twice` cases).
- Break-it checks, done by hand on 2026-11-12 and restored: removing the de-interleave in `mdc_decode.c` (`k = lbc`) turned 19 of 41
  cases RED; removing the MDC CRC comparison (`if(1)`) turned 8 RED (`mdc_crc_fail`, `mdc_crc_fail_quiet`, `mdc_noise_05`, `mdc_noise_20`, `mdc_ops_8k`,
  `mdc_ops_16k`, `mixed`, and Kaufman's own `upstream_selftest`); removing the Fleetsync CRC comparison turned 9 RED (all the `fs1_*` cases, `mixed`,
  `truncated_fs1_late`).

Test tools (not shipped, built by the same CMake): `mdc-gen` writes raw or WAV test audio (`--raw` / `--wav`, `--f32`, `--noise`,
`--seed`, `--truncate`; items listed at the top of `test/tools/mdc_gen.c`), `mdc-upstream-selftest` is upstream's test program.

## Notes for a bridge
- Start: `mdc-helper.exe --foxsdr-helper 1 --format s16 --rate 48000` (audio tap, mono). Feed raw samples; closing stdin ends the run.
- Event types: `mdc1200`, `mdc1200_crc_fail` (only with `--report-bad`), `fleetsync1`, `stats` (last line), the hello line first.
  Nothing else. All field names are listed in `mdc.helper.json`.
- Decoding happens as audio arrives; there is no look-ahead except Fleetsync, which reports a message when its audio ends (the
  helper injects a short burst of pseudo-random tones into the Fleetsync decoder after 20 ms of silence so a finished message is
  delivered even when the audio goes digitally silent; this burst does not advance the `t` clock).
- Keep the audio level normal (see "Level") and prefer 48000 Hz.
