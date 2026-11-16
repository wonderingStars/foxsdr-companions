# atn - the ATN protocol stack above VDL Mode 2 (FoxSDR GPL helper)

`atn` decodes what is *inside* VDL Mode 2 AVLC frames: ISO 8208 (X.25), CLNP (including the ICAO LREF compressed
header), ES-IS, IDRP, COTP, the X.225 session / ACSE layers, and the ATN applications CM (context management),
CPDLC and ADS-C v2. It does **not** demodulate: it takes AVLC frames that some demodulator has already recovered, one
per line as hex, and prints JSON Lines.

It is built from [dumpvdl2](https://github.com/szpajder/dumpvdl2) 2.7.0 (GPL-3.0-or-later, Tomasz Lemiech) and a core
subset of libacars (MIT) and the asn1c runtime (BSD-2-Clause). See `UPSTREAM.md` for the commit, the files taken and
every change. Licence of this program: **GPL-3.0-or-later** (`LICENSE`).

Receive only. No network code, nothing stored or forwarded, no telemetry, no window or prompt. The output is what
dumpvdl2 itself would print for the same frame; nothing is added beyond the event wrapper described below.

## Run it by hand

```
atn.exe --foxsdr-helper 1 --format avlc-hex < frames.txt
atn.exe --format avlc-hex --fcs none --no-text < frames_without_fcs.txt
atn.exe --foxsdr-helper-info
```

## Input: `--format avlc-hex`

One AVLC frame per line, hex digits only (upper or lower case, no spaces, no `0x`), optionally preceded by a decimal
timestamp in seconds and one space: `123.4 aabbcc...`. Blank lines and lines starting with `#` are ignored; CR LF is fine.

The bytes are **the whole AVLC frame after bit de-stuffing, without flags or preamble**, in the order they are
received:

| octets | field |
|---|---|
| 4 | destination address (ICAO 24-bit address + type + A/G bit, 7 bits per octet, LSB = extension bit) |
| 4 | source address (same layout, with the command/response bit) |
| 1 | link control field (I / S / U frame) |
| n | information field (X.25 packet, or `ff ff 01` + ACARS, or XID parameters) |
| 2 | frame check sequence, low octet first (CRC-16/X-25 over everything before it) |

`--fcs check` (default): the 2 FCS octets are present and verified; a bad one gives an `error` event and the frame
is dropped. `--fcs none`: the frame ends after the information field (the demodulator already verified it); the helper
appends a correct FCS so that the upstream code, which checks it, runs unchanged. Frames of 11 octets or more
(including the FCS) are accepted, at most 4096 octets. The timestamp only drives the reassembly time-outs (X.25: 3 s,
COTP, CLNP lifetime) and is echoed as `t`; without it `t` is the last timestamp seen (0 at the start).

## Output: JSON Lines

The first line is the hello (`helper`, `version`, `protocol`, `licence`, `source`, `upstream`, `format`). Then, per frame:

* `{"t":..,"type":"frame","line":N,"octets":N,"layers":["avlc","x25","clnp","cotp","cpdlc"],"msg":{...}}` -
  `msg` is dumpvdl2's own JSON tree for the frame (`msg.avlc` holds `src`, `dst`, `cr`, `frame_type`, sequence numbers,
  and nested `x25` / `xid` / `clnp` / `cotp` / `esis` / `idrp` / `x225_spdu` / `x227_apdu` / `context_mgmt` / `cpdlc` /
  `adsc_v2` / `unknown_proto` objects). `layers` lists the keys of that nesting in order.
* `{"t":..,"type":"text","line":N,"text":"..."}` - the same frame as dumpvdl2's human-readable text (suppress with
  `--no-text`).
* `{"t":..,"type":"error","line":N,"error":"..."}` - bad hex, bad timestamp, bad FCS, frame too short or too long,
  line too long. At most 50 are printed, the rest are only counted.
* `{"t":..,"type":"stats","lines":..,"frames":..,"decoded":..,"bad_fcs":..,"too_short":..,"bad_input":..,"errors_suppressed":..}`
  once, at end of input.

`line` counts non-blank, non-comment input lines from 1. Exit status 0 at end of input; 2 for a bad command line.

Other options: `--decode-fragments` (dumpvdl2's option: also decode higher layers of incomplete fragments),
`--rate` / `--centre` (accepted and ignored: there are no samples), `--version`, `--help`.

## What is decoded, what is not

Decoded (all from dumpvdl2): AVLC address / S / U / I frames and XID (link establishment, GSIF, parameter negotiation);
ISO 8208 call set-up / clearing / reset / restart / diagnostic / RR / REJ / data with fast select, facilities, SNDCF
and SNDCF error reports; X.25 fragment reassembly; ISO 8473 CLNP data, error report and echo PDUs, with options and the ATN
security label, the segmentation part and CLNP reassembly, and the ICAO compressed (LREF) data PDUs; ES-IS ESH / ISH;
IDRP OPEN / UPDATE / ERROR / KEEPALIVE / CEASE / RIB refresh; ISO 8073 COTP (CR, CC, DR, DC, DT, ED, AK, EA, RJ, ER) with
reassembly; X.225 short-form session SPDUs; X.226/X.227 fully-encoded-data and ACSE (AARQ / AARE / RLRQ / RLRE / ABRT);
CM v1/v2 messages; CPDLC v1/v2 (ATN B1/B2) uplink, downlink and the protected-mode wrapper; ADS-C v2.

Not decoded here: ACARS inside AVLC (frames starting `ff ff 01` come out as `unknown_proto` - FoxSDR decodes ACARS
itself) and everything that rides on ACARS (ARINC 622, FANS-1/A ADS-C and CPDLC, MIAM, OHMA); the address databases
(aircraft registration, ground station names); the demodulator, FEC, and dumpvdl2's outputs and statistics.

## Build

Visual Studio 2022 (MSVC), CMake 3.16 or newer, x64, Release; Python 3 for the tests. Nothing else is needed.

```
cmake -S atn -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target atn --parallel 4
cd build && ctest -C Release
```

`tests/make_frames.py` and `tests/testgen.c` document how the test frames were made (see `tests/`).
