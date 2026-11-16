# UPSTREAM.md: where the tetrapol helper comes from

## Project, commit, licence

| | |
| --- | --- |
| Project | tetrapol-kit, "Kit for analysing of TETRAPOL radio networks" (brmlab, Prague; GPL) |
| Repository taken | https://github.com/airphel/tetrapol-kit-2023 (default branch `master`) |
| Commit | `09def9dcbc4c3778ab4a8813b6a0c7c48ae40910`, 2023-01-05, "Update sysop_env.sh" (shallow clone, read locally) |
| Lineage (from the upstream README) | https://brmlab.cz/gitweb/?p=tetrapol-kit.git (origin), https://github.com/aeburriel/tetrapol-kit, https://github.com/jenda122/tetrapol-kit, https://github.com/jswo/tetrapol-kit, https://github.com/airphel/tetrapol-kit-2023; GitHub lists jswo, sysop42, airphel and jenda122 as contributors |
| Licence | **GPL-2.0 (version 2 of the licence)**. The repository's `COPYING` is the GPL version 2 text (copied here byte for byte as `LICENSE`); GitHub reports `GPL-2.0`; the README says only "For license see COPYING". There is **no per-file licence notice and no copyright line in any source file, and the words "or any later version" appear nowhere**, so the helper is treated as **GPL-2.0-only** (the conservative reading; the manifest and the hello line say `GPL-2.0-only`). The licence is stated by the upstream project, not by its files: its README (commit 09def9dcbc4c) says "For license see COPYING", and `COPYING` is the unmodified GPL version 2 text (read online at https://github.com/airphel/tetrapol-kit-2023 on 2026-11-16 and in the local clone at that commit). |

Why this repository: the aeburriel fork (last push 2016-03, the one the browser pass listed first) has no `cch.c`, `frame.c`
(the newer frame decoder and encoder), `link.c`, `terminal.c`, `msg_coding.c`, `tetrapol.c` or `tch.c` (compared file by file
with `lib/` of airphel/tetrapol-kit-2023); the 2023 repository contains them. Other
repositories found and **not** used: Parnoud/tetrapol-kit (GPL-3.0, a derivative; mixing it would change the licence),
adswill/OnAir (GPL-3.0; its tetrapol code was written from PAS 0001-2 and is a second implementation), aeburriel/tetrapol-rpcelp
and Parnoud/tetrapol-rpcelp (voice codec: not touched, see below).

How the repository was found. tetrapol-kit and the
two OSS pass reports; I searched again: WebSearch `site:github.com tetrapol-kit tetrapol_dump`, WebSearch
`site:github.com "tetrapol" decoder GPL signalling "tetrapol_dump"`, WebSearch `site:github.com tetrapol IQ recording sample cfile
OR wav OR bits demodulated test` (looking for a real recording: none on GitHub), then `gh api repos/<owner>/<repo>` (licence,
dates, parent) for aeburriel/tetrapol-kit, airphel/tetrapol-kit-2023, Parnoud/tetrapol-kit and brmlab/tetrapol-kit (404: the
origin is on brmlab's own gitweb), `gh api repos/airphel/tetrapol-kit-2023/contributors`, and `git clone --depth 1` of the two
candidates; the hits were opened and read (README, `COPYING`, every source file listed below, the file headers).

## Specification checked against

The air interface is published as PAS 0001-2 (Radio Air Interface) v3.0.0, 10 November 1999, TETRAPOL Forum, public at
https://criticalcommunications.airbus.com/sites/g/files/jlcbta221/files/2024-10/16080916424711244_Radio_Air_Interface_v300.pdf
(the PDF is not in this repository; copyright TETRAPOL Forum). Message layouts are PAS 0001-3-2 and the link and transport layers
PAS 0001-3-3, cited by section number in the upstream headers; I did not have those two documents, so the message decoding is
**verified only as far as upstream's own reading of them** (and against the generated signals, which are written from the same
reading).

Checked against the text of PAS 0001-2 with `test/tools/pas/check_constants.py` (the PDF is LZW-compressed; `pdf_text.py` extracts
its text without any library): the scrambling sequence `s(k) = s(k-1) + s(k-7)`, `S(SCR,k) = s(k+SCR)`, `S(0,k) = 0` (6.1.5.1);
the frame header `0 1 1 0 0 0 1 0` (6.1.5.2, "f0 .. f7"); the VHF interleaving formula and its worked example k(123) = 121
(6.1.3.1); `PRE_COD` and the differential precoding rule for UHF (6.1.4.2); both UHF interleaving tables (6.1.4.1 and 6.2.4.1),
all 304 numbers; the rate-1/2 convolutional code `C2j = b(j) + b(j-1) + b(j-2)`, `C2j+1 = b(j) + b(j-2)` with the cyclic
initialisation (6.1.2, 6.2.2); the CRC polynomials `1 + D + D^3` with one's complement for voice frames (6.1.1) and `1 + D^2 + D^5`
for data frames (6.2.1); GMSK with BT = 0.25 and a gross bit rate of 8 kbit/s (clauses 4 and 5). Every one of these agrees with
`upstream/frame.c`. Not checked (not in the document I had): the HDLC frame check sequence (PAS 0001-3-3 7.4.1.1, polynomial
x^16 + x^12 + x^5 + 1 per the upstream comment), the stuffing pattern table, the FN / ASB flag rules and the superframe layout.

## Files taken (all from `lib/` of the upstream commit, copied, then patched; 5257 lines of C and 1912 lines of headers)

`addr.c bch.c bit_utils.c cch.c data_frame.c frame.c hdlc_frame.c link.c log.c misc.c msg_coding.c pch.c phys_ch.c rch.c sdch.c
terminal.c tetrapol.c tp_timer.c tpdu.c tsdu.c` and, in `tetrapol/`: `addr.h bch.h bit_utils.h cch.h data_frame.h frame.h
hdlc_frame.h link.h log.h misc.h msg_coding.h pch.h phys_ch.h rch.h sdch.h system_config.h terminal.h tetrapol.h tetrapol_int.h
tp_timer.h tpdu.h tsdu.h`. They are in `upstream/` under the upstream names. Line endings were normalised to LF by git
(`.gitattributes`); otherwise commit `1392d0c` of this branch is byte for byte the upstream files, so
`git diff 1392d0c -- tetrapol/upstream` is the exact list of changes (it is also saved as
`patches/tetrapol-helper-vs-upstream.patch`: 15 files, 218 lines added, 181 removed).

**Not taken, on purpose**
* `tch.c`, `lsdu_vch.c`, `lsdu_cd.c` (+ headers): traffic channel, voice-channel signalling and circuit-data frames. No voice, no data content.
* `tsdu_print.c`, `frame_json.c`, `tsdu_json.c`: upstream's text and hex dumps of frames and message bytes, replaced by `helper_out.c`.
* `apps/tetrapol_dump.c` (replaced by `helper_main.c`), `apps/tetrapol_build.c` (a transmitter-side tool; its encoder is exercised only by the test, see below), `lib/test_*.c` (cmocka unit tests; cmocka is not installed; the behaviour they pin is covered by the CTest cases).
* `demod/` (GNU Radio flowgraphs and scripts, Python; needs gr-osmosdr). The helper's own demodulator, `src/gmsk.c`, is new C code that performs the same steps as `demod.py` (channel low-pass, `digital.gmsk_demod`); nothing of the Python is copied.
* `doc/voice.txt`, `doc/crypto.txt`, `doc/och-rfsi.txt` (not opened), and everything about the RP-CELP voice codec (tetrapol-rpcelp, a separate repository; not opened, not fetched). **No voice codec of any kind is in this folder.** No decryption, no key handling.

## New code (written for the helper, GPL-2.0-only like the rest)

| file | lines | what |
| --- | --- | --- |
| `src/helper_main.c` | ~440 | command line, pipe protocol 1 (hello, `--foxsdr-helper-info`), input loop, `SetErrorMode` first, JSON on a private copy of stdout, fd 1 pointed at stderr |
| `src/helper_out.c`, `src/tetrapol/helper_out.h` | ~1130, ~80 | JSON Lines writer (bounded, never overflows), events, message field emitters, `--hide-ids` hashing, counters |
| `src/gmsk.c`, `src/gmsk.h` | ~230, ~40 | GMSK demodulator: FIR low-pass, FM discriminator, carrier-offset removal, Gardner timing recovery with cubic interpolation, slicer, differential decode |
| `compat/endian.h`, `compat/unistd.h`, `compat/sys/time.h` | small | MSVC shims for headers upstream includes |
| `test/tools/tpgen.c` | ~1050 | test generator (below) |
| `test/run_case.cmake`, `test/CMakeLists.txt`, `CMakeLists.txt` | | CTest driver and build |
| `test/tools/pas/*.py` | | the PAS constant check |

## Changes to the upstream files (every one is marked `FoxSDR helper` in the source)

1. **Reporting.** `phys_ch.c` (scr event, sync found/lost, frame event, traffic event), `tetrapol.c` (the TSDU event), `pch.c` and `rch.c`
   (PCH / RCH), `cch.c` (the report is no longer behind `LOG_IF(INFO)`) call `helper_out.c` instead of `printf` / `LOGF`. Upstream
   printed raw frame and message bytes as hex; the helper never does.
2. **Traffic is not decoded.** `phys_ch.c`: the `tch_*` calls are gone (a frame is synchronised, counted and reported as "traffic
   frame, not decoded"); `link.c`: `UI_VCH` and `UI_CD` HDLC frames are reported as not decoded instead of handed to `lsdu_*`;
   `tetrapol.c`: messages whose codop carries user data (short data, datagram, ability management, information delivery, broadcast,
   extended status) are not passed to `tsdu_decode` at all.
3. **MSVC.** `tp_timer.c`, `tsdu.c`, `link.c`: variable-length arrays and `sizeof(T[n])` replaced by fixed buffers and `n * sizeof(T)`.
   `tsdu.h` / `tsdu.c`: `void *optionals[]` (a flexible array member inside a struct that is embedded in other structs; GNU extension)
   replaced by the macro `TSDU_OPTIONALS()` for the same memory, layout unchanged. `frame.h`: dummy member in three empty structs.
4. **glib removed.** `terminal.c`: the `GTree` of terminals is a linear array capped at 512 entries with least-recently-used eviction
   (each terminal owns about 30 kB of reassembly buffers; upstream's tree grew without limit).
5. **Robustness against damaged or hostile input** (found by running random valid-FCS messages through the helper, 32 000 messages,
   also under AddressSanitizer):
   * `msg_coding.c`, `tsdu.c`: `address_list_decode()` looped for ever and reallocated without bound when an address had an unsupported
     address type (the data pointer did not advance); it is now limited to the bytes available and to 8 addresses.
   * `tpdu.c`: the DU length byte and the segment-header length came off the air unchecked and were used for `memcpy` / `tsdu_decode`;
     both are checked against the HDLC payload.
   * `tetrapol.c`: the TSDU is decoded from a copy with 64 bytes of zero padding, because several decoders read a fixed-size field after
     checking only a minimum length.
6. **Upstream encoder bug (the helper never calls the encoder).** `frame.c`, `frame_encode1()`: `data << 4` spills four bits above bit 52
   into the first bits of the second block, which `frame_encode2()` ORs into the same 64-bit word; frames made by `frame_encoder_encode()`
   (and so by upstream's `tetrapol_build`) had wrong bits 52..55 of the coded frame in about half of all frames. Masked to 52 bits. Found
   by `tpgen xcheck` (396 of 35 840 bits differed before the fix, 0 after).

Not changed, observed (UNVERIFIED, no PAS 0001-3-3 at hand): `data_frame.c check_parity()` tests bit indices 3..66 of the 66-bit block
(`data[0..65]`), which includes the first ASB bit and skips the first payload bit (index 2); the helper's own generator keeps ASB at 0
so the check is satisfied either way. Upstream's `tetrapol_build.c write_frame()` emits each byte most significant bit first while the
decoder reads the frame least significant bit first; I did not run `tetrapol_build`.

## Dependencies and toolchain

None beyond the C runtime. Upstream needs glib2 (replaced, change 4), json-c and cmocka (only for the tools and unit tests that were not
taken). MSVC (Visual Studio 2022, x64, Release) + CMake 4.4; the library files build as C11 with warnings 4018 / 4200 / 4244 / 4267 / 4996
silenced (upstream style: implicit narrowing, zero-length union member `blob_[0]`).

## Tests and the generator (what is and is not proven)

`test/tools/tpgen.c` makes every signal. Its channel coding is an encoder written from the same constants as upstream (bit-level:
CRC-5 and CRC-3, the tail-biting rate-1/2 code, interleaver and scrambler tables, differential precoding, header, the HDLC FCS solved
over GF(2)), plus a GMSK modulator (Gaussian BT 0.25, h = 0.5, with noise, carrier offset, timing offset and clock error) and a damage
tool. It does **not** call the upstream decoder. `tpgen xcheck` compares its frames bit for bit with upstream's `frame_encoder_encode()`
(8 frames x 2 frame types x 2 bands x 7 scrambling constants, after change 6): 35 840 bits, 0 different. The constants were
separately checked against the PAS text (above). What is proven: decode of everything the generator can say, error correction and parity
recovery, resynchronisation, the demodulator at 16000, 32000, 44100 and 48000 Hz in CTest (and by hand at 96000 and 250000 Hz), +-1.8 kHz carrier offset, +-150 ppm clock error and
16 dB SNR, and that nothing blows up on garbage. What is **not** proven: any real over-the-air signal (no recording was available: none
on the development machine, none found on GitHub), uplink, VHF on a real network, the HDLC FCS and FN / ASB conventions beyond upstream's reading of PAS
0001-3-3, and the demodulator against real transmitter filtering and fading.

## Date of the changes

All changes to the upstream files listed in this file were made in November 2026 (the files were taken on 2026-11-12).
