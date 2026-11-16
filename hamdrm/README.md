# hamdrm - HamDRM (digital SSTV) receive modem and file reassembly (GPL helper)

Receives **HamDRM** (the DRM-derived mode EasyPal and QSSTV use to send picture and other files over HF, also called digital
SSTV or DSSTV) from audio and rebuilds the files that were sent. It is the receive half of QSSTV's HamDRM engine without Qt,
without a sound card, without a user interface and without transmit. This program is based in whole or in part on **QSSTV**
(Johan Maes ON4QZ, https://github.com/ON4QZ/QSSTV): the modem is the RXAMADRM receiver of M. Bos PA0MBO (itself derived from
diorama by Dittrich and Schorr) and the file layer is QSSTV's `sourceDecoder`. Licence and every change: `UPSTREAM.md`, `COPYING`, `LICENSE`.

Why QSSTV and not Dream (GPL-2.0, the ancestor of QSSTV's transmit modem): everything HamDRM-specific is in QSSTV's code - the 2.3 and
2.5 kHz occupancy tables (receiver `K_min_K_max_list`, transmitter cell-mapping tables), the file layer with the EasyPal-compatible
Reed-Solomon `.rs1`-`.rs4` files, and a transmitter to test against. I did not open Dream's source to see how much of this it has (the research row
said "HamDRM support in Dream unverified"); QSSTV was the one that demonstrably has it, so it is the single upstream used.

## Run it by hand
```
hamdrm --foxsdr-helper 1 --format s16 --rate 12000 --out-dir C:\received < audio12k.s16
hamdrm --foxsdr-helper-info            (prints the manifest, hamdrm.helper.json)
```
* stdin: **mono audio at 12000 Hz** as raw little-endian 16-bit (`--format s16`) or 32-bit float (`--format f32`, scaled by 32767
  internally), until end of file. 12000 Hz is what QSSTV's receiver works at (its sound layer decimates the 48 kHz sound card by 4);
  the helper refuses any other `--rate`. The HamDRM audio sits in an ordinary SSB audio passband (about 350 Hz to 2.7 kHz), as sent by
  an SSB transmitter or heard from an SSB receiver set to the signal's suppressed-carrier frequency.
* `--out-dir DIR`: a finished file is written there (the folder must exist). Without it **nothing is written** (the `file` event still
  has name, size and sha256). The name is the sender's, reduced to a plain file name (no folders, no control or Windows-reserved
  characters or device names, at most 100 characters, an existing name gets `_1`, `_2` ...); the path the file was really written to is in the event.
* stdout: UTF-8 JSON Lines (protocol version 1). **No picture or file bytes ever go to stdout.** `t` is seconds of audio since the start
  (from the sample count; deterministic).
* Exit code 0 at end of input; 2 and a message on stderr for a bad option, `--format` or `--rate` (before any output).

## Events
First line, hello: `{"helper":"hamdrm","version":"1.0.0","protocol":1,"licence":"GPL-3.0-or-later","source":"https://github.com/ON4QZ/QSSTV","format":"s16","rate":12000,"upstream":"QSSTV 31c323c..."}`.
Empty input gives the hello line only.

| `type` | fields | meaning |
|---|---|---|
| `sync` | `state` = `time`, `frequency`, `frame` or `lost`; `mode` (A, B or E; not on `lost`); `snr_db` | stages of synchronisation: OFDM symbol timing and robustness mode found, carrier frequency and spectrum occupancy found, frame (FAC) aligned; `lost` = the receiver dropped back to searching |
| `mode` | `mode` e.g. `"B/2.3/Hi/16/Short"` (robustness mode / bandwidth kHz / protection Hi or Lo / QAM 4, 16 or 64 / interleaver), `callsign` (3 FAC blocks, or `null`), `snr_db` | the transmission parameters from the FAC; sent when they first appear or change |
| `header` | `transport_id`, `name` (file name from the MOT header), `size` (bytes, from the header), `mode`, `snr_db` | a file transfer has started (MOT header segment decoded) |
| `segment` | `transport_id`, `name`, `segment`, `received`, `total`, `last_seen`, `percent`, `snr_db` | one more data segment received (not repeated for a segment seen before); `total` is what is known so far |
| `file` | `transport_id`, `name` (as sent), `size`, `sha256`, `saved` (name written, or `null`), `path` (`null` if `--out-dir` is off), `fec` (`rs1`..`rs4` if Reed-Solomon recovered, else `null`), `segments`, `mode`, `callsign`, `note`, `snr_db` | a file is complete; `note` is `"hybrid"` (the picture is on an FTP server, QSSTV would fetch it: not done here), `"write failed"`, or `null` |
| `status` | `text` | a transport that is not a file for the user (`bsr.bin` repair requests) |
| `stats` | `seconds`, `locked`, `snr_db`, `packets`, `crc_failures`, `dropped`, `files` (`final:true` on the last one) | every 10 s of audio and at the end |

`snr_db` is QSSTV's smoothed FAC-based signal-to-noise estimate (WMER), as it reports it.
A transmission is normally sent several times; a second complete copy of a file already received is not reported again within one run
(QSSTV's `alreadyReceived` rule) - the same bytes arriving under a new transport id are a new file.

## What it does not do
No transmit, no sound card, no user interface, no image decoding or display (the file is written as received: JPEG, JPEG 2000, text...),
no FTP "hybrid" download, no repair requests (BSR / FIX), no log book. 48 kHz input is not accepted: decimate to 12 kHz first.
Full list in `UPSTREAM.md`.

## Build and test
Visual Studio 2022, CMake: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Release && ctest --test-dir build -C Release`.
The tests build their audio with `tools/txgen` (QSSTV's HamDRM transmit chain, ported the same way), so they prove the receiver against the same
upstream tables, not against signals from real transmitters. See UPSTREAM.md for what is proven and what is not.
