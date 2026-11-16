# FoxSDR companion applications: source

This repository (https://github.com/wonderingStars/foxsdr-companions) holds the source of the companion applications that FoxSDR can download and start. Each companion is a
separate open-source command-line decoder program. It reads raw samples on standard input and writes JSON Lines on
standard output; it is a complete program on its own and links nothing from FoxSDR. FoxSDR, or any other program, can
start one as a separate process and show what it prints.

Each folder is its own program under its own licence, matching the project it is built from. The licences differ between
folders. Every folder has `UPSTREAM.md` naming the project it is built from, the commit, the licence and the changes made,
and the licence text and notices of that project. The pipe protocol is in `docs/PIPE-PROTOCOL.md`; `HOST-INTERFACE.md` is
the table of arguments, formats, events, attribution and proof level per program.

The history of this repository starts at this import: the earlier, private development history is not part of it. More companions will be added later.

## The programs

Proof level: **real** = a recording of a real transmission decodes; **upstream samples** = the upstream project's own
sample files decode; **generated** = only signals made by a generator. Details and exact arguments: `HOST-INTERFACE.md`.

| Folder | Decodes | Upstream | Licence | Proof level |
|---|---|---|---|---|
| `multimon/` | POCSAG, FLEX, GSC, EAS, AFSK/AX.25, DTMF, ZVEI and 20 more audio decoders | multimon-ng | GPL-2.0-or-later | upstream samples |
| `hfmodes/` | HF telegraphy: ARQ-E/N, G-TOR, SITOR, CIS, CROWD36 and more | OnAir | GPL-3.0-or-later + OnAir attribution term | generated (SITOR-B real) |
| `trunkfmt/` | LoJack, Tait 1200, Fleetsync II, MPT-1327, LTR, Passport, Connect Plus | sdrtrunk | GPL-3.0-or-later | generated |
| `atn/` | ATN payloads (CPDLC, ADS-C, ...) in VDL2 AVLC frames | dumpvdl2 | GPL-3.0-or-later | generated |
| `rtl433/` | 384 short-range device protocols (433/868/915 MHz) | rtl_433 | GPL-2.0-or-later | generated |
| `packet/` | AX.25, FX.25, IL2P (300, 1200, 9600 baud) | Dire Wolf | GPL-2.0-or-later | real (AFSK 1200) |
| `js8/` | JS8Call | JS8Call | GPL-3.0-only | upstream samples |
| `tetrapol/` | TETRAPOL control channel | tetrapol-kit | GPL-2.0-only | generated |
| `stdc/` | Inmarsat-C TDM channel | inmarsatc | GPL-3.0 (version 3) | real (one recording) |
| `sondes/` | radiosondes (LMS6, Mk IIa, MRZ, MTS01, CF-06) | rs1729/RS | GPL-3.0-only | generated |
| `trunkctl/` | SmartNet and EDACS control channels | trunk-recorder, edacs-fm | GPL-3.0-only | generated |
| `weakmodes/` | MSK144, JT9, FT2L | WSJT-X, wsjtx-ft2libre | GPL-3.0-or-later | upstream samples (FT2L never real) |
| `weakmodes2/` | FST4, FST4W, Q65 | OnAir, WSJT-X | GPL-3.0-or-later + OnAir attribution term | upstream samples |
| `fldigimodes/` | Olivia, Contestia, MFSK, MT63, THOR, RSID | fldigi | GPL-3.0-only | generated |
| `hfdl/` | HFDL (aircraft HF data link) | dumphfdl | GPL-3.0-or-later | real |
| `hamdrm/` | HamDRM digital SSTV | QSSTV | GPL-3.0-or-later | generated |
| `msumrcal/` | Meteor-M MSU-MR infrared calibration (text in, text out) | SatDump | GPL-3.0-only | generated |
| `mdc/` | MDC-1200 and Fleetsync I | mdc-encode-decode, fsync-mdc1200-decode | GPL-2.0-only | generated |

## Build

Windows, Visual Studio 2022 (x64, MSVC), CMake 3.20 or later, and Python 3 (only for the conformance tests). Run the
commands from a Visual Studio x64 developer command prompt (or run `vcvars64.bat` first):

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --parallel 4 --timeout 300
```

`-DFOXSDR_HELPER_<NAME>=OFF` leaves one program out (`MULTIMON`, `HFMODES`, `TRUNKFMT`, `ATN`, `RTL433`, `PACKET`, `JS8`,
`TETRAPOL`, `STDC`, `SONDES`, `TRUNKCTL`, `WEAKMODES`, `WEAKMODES2`, `FLDIGIMODES`, `HFDL`, `HAMDRM`,
`MSUMRCAL`, `MDC`). Each program also builds alone: `cmake -S <folder> -B <build> ...`. A few tests use real recordings that
are not part of this repository; they look for them under the folder given as `-DFOXSDR_SAMPLES_DIR=<folder>` and are
skipped when it is not set. The upstream projects' own sample recordings are not included, because they carry no stated licence. Tests that use them are left out unless the sample is found under `<folder>/` in that directory, and the pinned outputs for them are not shipped (run the tests once with the folder's update variable, for example `MM_UPDATE=1`, to create them). Samples: multimon-ng `test/samples/` (https://github.com/EliasOenal/multimon-ng; the FLAC files converted to raw 22050 Hz mono s16, file names as in `multimon/test/CMakeLists.txt`), JS8Call `media/tests/` (https://github.com/js8call/js8call), WSJT-X `samples/` (https://git.code.sf.net/p/wsjt/wsjtx; WAV converted to raw s16 by removing the 44-byte header), rs1729/RS `mk2a/Mk2a_9600.bytes` (https://github.com/rs1729/RS), and the recordings named in the folder READMEs for `hfdl`, `hfmodes`, `packet` and `stdc`.

`ctest` runs every program's own tests plus `conformance_<folder>`, which runs `tools/conformance.py` (by hand:
`python -I tools/conformance.py --exe <program.exe> --manifest <folder>/<folder>.helper.json`).

## Licences

Each folder carries its own licence file and notices, matching its upstream project: GPL-2.0-only, GPL-2.0-or-later,
GPL-3.0-only or GPL-3.0-or-later, plus the additional attribution terms of OnAir in `hfmodes` and `weakmodes2`. This
repository is an aggregate of separate programs: the top-level files (this README, `CMakeLists.txt`,
`HOST-INTERFACE.md`, `tools/`, `docs/`) only build and describe them and contain no upstream code. A folder's licence
applies to that folder alone; code is not moved between folders. `NOTICE.md` lists, per folder, the licence and the
upstream project.

## Problems

Report problems with these programs as issues at https://github.com/wonderingStars/foxsdr-companions/issues.
