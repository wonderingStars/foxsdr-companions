# mdc: where the code comes from

Licence of this helper: **GPL-2.0-only** (the text is in `LICENSE`). Both decoder libraries carry, in every file, the
sentence "redistribute it and/or modify it under the terms of version 2 of the GNU General Public License as published by the
Free Software Foundation" with no "or any later version", so the helper stays GPL version 2 only. All upstream copyright and
licence headers are kept as they were.

## Chosen upstream for MDC-1200

**atmatthewat/mdc-encode-decode** by Matthew Kaufman, `https://github.com/atmatthewat/mdc-encode-decode`, commit
`45fc9095fbde8bf6a6988c7dc0de64a4ec0ffe12` (2016-07-27).

Licence checked from the repository itself: the README says "GPL license applies" (and offers commercial terms for closed
source products); every source file header says GPL **version 2** only. There is no separate licence file in that repository, so
`LICENSE` here is the GPL-2.0 text from the GitHub licences API (`gh api licenses/gpl-2.0`).

### Why this one
It is the original, and the only candidate with everything needed:
- full decode chain: sync detection on 5 parallel bit-phase decoders, 4-point demodulation, de-interleave, convolutional-code
  error correction, CRC-16 check, single and double (two block) packets;
- an encoder in the same library, which is used here only as a TEST TOOL to make test audio (`test/tools/mdc_gen.c`), and its
  own round-trip self test (`mdc_test.c`), which runs as the CTest case `upstream_selftest`;
- plain C, no libraries.

Candidates looked at and not chosen (licences confirmed from the repositories, 2026-11-12):
- `russinnes/fsync-mdc1200-decode` (commit `287356a99d4493b45b3fd447b276150262e9c871`, `COPYING` = GPL-2.0 text). Its
  `mdc_decode.c`, `mdc_decode.h` and `mdc_common.c` are byte-identical to the Kaufman files above (checked with `cmp`), so it adds
  nothing for MDC-1200; it IS the source of the Fleetsync I decoder (next section). Its own files `demod.c` (PulseAudio, UDP port
  9101, Linux only) and `build.sh` carry no licence header and are not used.
- `sdrtrunk` (DSheirer, GPL-3.0-or-later): `module/decode/mdc1200/*.java`. Its MDC decoder has no CRC or error-correction
  (`MDCMessage.isValid()` has `//TODO: add CRC and/or convolution decoding/repair` and returns true) and names only emergency
  and ANI, so it is a weaker decoder than Kaufman's. Not used. (The `trunkfmt` folder takes other parts of sdrtrunk under GPL-3.0.)
- `gatekeep/MDCTool` (C#, Windows Forms; README: "licenced under the GPL v2"). It is a port of Kaufman's decoder plus a GUI.
  Used only for the operation NAMES (see below).
- `MattCheramie/GopherTrunk` `docs/mdc1200.md` (clean-room Go implementation, Apache-2.0 per the search result). Not read for
  code; the page holds no opcode table. Not used.

## Fleetsync I

The Fleetsync decoder is `fsync_decode.c` (+ `fsync_common.c`, `fsync_decode.h`, `fsync_types.h`) from
**russinnes/fsync-mdc1200-decode**, commit `287356a99d4493b45b3fd447b276150262e9c871`, which carries Matthew Kaufman's headers
("fsync Encoder/Decoder Library", GPL version 2 only). Same author, same licence as the MDC-1200 files: compatible, one
helper under GPL-2.0-only. This upstream decoder handles Fleetsync I and Fleetsync II in one routine; the helper reports only
Fleetsync I (`is_fsync2 == 0`) and counts Fleetsync II messages as `fleetsync2_ignored` in the stats line, because `trunkfmt`
in this repository decodes Fleetsync II.

## Operation names

Kaufman's library returns only numbers (operation code, argument, unit ID), and so does this helper: it prints `op`, `arg`
and the other fields as numbers and never as names. A table of operation names was considered and left out: the only
source found for one is a project whose files carry licence statements that disagree with each other, so no name table is
shipped. A table of names can be added later from a source whose licence is clear.

## Files taken (all in `upstream/`, unchanged except as listed under "Changes")

| file | from | lines |
|---|---|---|
| mdc_decode.c, mdc_decode.h, mdc_common.c, mdc_types.h | mdc-encode-decode | 545 / 203 / 82 / 59 as cloned |
| mdc_encode.c, mdc_encode.h | mdc-encode-decode | 582 / 150 (TEST TOOL only, not in the helper) |
| mdc_test.c | mdc-encode-decode | 361 (upstream self test, CTest `upstream_selftest`) |
| fsync_decode.c, fsync_decode.h, fsync_common.c, fsync_types.h | fsync-mdc1200-decode | 582 / 141 / 143 / 57 |

The sha256 sums of the files as cloned (CRLF line endings) are in `upstream-sha256.txt`. About 1,900 lines of upstream C (the decoders and their headers) are in the helper itself; the encoder and
self test (about 1,100 lines) only in the test tools.

`src/main.c` (command line, JSON output, id hiding, Fleetsync flush), `CMakeLists.txt`, the manifest
and everything under `test/` are written for this helper.

## Changes made to the upstream files (also marked `FOXSDR-HELPER` in the code)

1. **Line endings** CRLF to LF in every file (first commit on the branch is the upstream text with only this change).
2. `mdc_decode.c`: `malloc` to `calloc` for the decoder object. Upstream left the `nlevel[]` sample history and other fields
   uninitialised, so the first packet of a run could depend on stack garbage; the helper must be deterministic.
3. `mdc_decode.c/.h`: report the number of bits the convolutional-code correction repaired in the block just decoded
   (`decoder->ecc_fixed`, compares the 14 block bytes before and after `_gofix`), and a new optional callback
   `mdc_decoder_badcrc_callback_t` called when a block fails its CRC (frame 1 or 2). Decoding logic itself is untouched.
4. `fsync_decode.c`: `malloc` to `calloc` (same reason: `lastvalue`, accumulators and the sync words were uninitialised).
5. `fsync_types.h`: sample type from `unsigned char` (U8) to `float` (`FSYNC_SAMPLE_FORMAT_FLOAT`), so that `src/main.c` can scale
   the input by the sample rate (see README, "Accepted rates"). The decoder code already had the float branch.

Known upstream defects left alone (listed so nobody trips on them): in `fsync_decode.c` `_procbits` the Fleetsync II repair
test reads `ec4 != -4` where `-1` is plainly meant (Fleetsync II only; not reached by the helper's output); `_dispatch` only runs when
the next 64-bit word fails (handled in `src/main.c`, see README); the zero-crossing counter ignores any count other than 2, 3, 4.

## Searches run
- Web search `site:github.com "mdc1200"`: only a mirror of OneOfEleven/uv-k5-firmware-custom `mdc1200.c` (radio firmware, a
  decoder/encoder derived from the same Kaufman code; licence not checked, not used).
- Web search `site:github.com "mdc-1200" decoder`: found `gatekeep/MDCTool`, `atmatthewat/mdc-encode-decode`,
  `russinnes/fsync-mdc1200-decode` (via a Gentoo ebuild, which says GPL-2+; the repository itself has GPL-2 `COPYING` and
  Kaufman's "version 2" headers, so GPL-2.0-only is used), `MattCheramie/GopherTrunk`.
- Web search `site:github.com fleetsync decoder MDC1200 license`: the same set.
- Opened and cloned for reading (a local clone): atmatthewat/mdc-encode-decode, russinnes/fsync-mdc1200-decode,
  gatekeep/MDCTool; read the existing sdrtrunk clone; fetched the GopherTrunk doc page. Nothing was fetched from outside GitHub
  except that one raw page, and no binary or script was fetched or run.
- Not searched on gitlab.com, codeberg.org or sourceforge.net (the three GitHub searches already found the original library
  and its derivatives; no operation names exist in any of them except the one project described above).
