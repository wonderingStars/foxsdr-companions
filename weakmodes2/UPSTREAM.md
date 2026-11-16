# weakmodes2: upstream, what was taken, what was changed

Helper licence: **GPL-3.0-or-later** (`LICENSE`, the GPL v3 text). Every file taken keeps its upstream notices. The OnAir
part carries the extra attribution term of OnAir's section 7 notice (`ADDITIONAL-TERMS-OnAir.md`): the helper prints
"Based on OnAir by Kristijonas Adomaitis - https://on-air.dev" in `--version`, `--help` and the hello line.
(OnAir's source files have no per-file licence header; OnAir's REUSE.toml gives them "Copyright (C) 2026 Kristijonas Adomaitis and
OnAir contributors, GPL-3.0-or-later". The WSJT-X credit is in the file headers: "WSJT-X (K1JT et al., GPL-3)".)

## Sources

| Source | Where | Commit / version | Licence |
|---|---|---|---|
| **OnAir** (Kristijonas Adomaitis and contributors), `core/src/hfdig/` | https://github.com/adswill/OnAir | `e118396b25d5` (2026-11-12) | GPL-3.0-or-later (REUSE.toml default entry; `LICENSE`), plus the section 7 attribution term (`ADDITIONAL-TERMS.md`) |
| **WSJT-X** (K1JT, K9AN, G4WJS, IV3NWV, the WSJT Development Group), `lib/qra/q65/*.c` | official source tree, https://git.code.sf.net/p/wsjt/wsjtx (SourceForge) | `b4f9a431bcf6` (develop, 2025-02-04) | GPL-3.0 (COPYING in that tree; the qracodes files say "version 3 of the License, or (at your option) any later version") |

OnAir's `hfdig` is a C++ re-implementation, written from the WSJT-X Fortran and C sources, of the FT8-family message packing,
the (240,101) / (240,74) LDPC codes with belief propagation and OSD, FST4 / FST4W, and Q65. It is the "existing C++ port" that was sought
for; no Fortran toolchain was needed. The WSJT-X C files for Q65 are used unchanged for the spread-signal (fast-fading) decode.

Searches made for other ports: WebSearch `site:github.com FST4 FST4W decoder C++ port WSJT-X LDPC 240 101` found
only `mfsk-core` (Rust, jl1nie/mfsk-core, GPL-3.0-or-later; not C/C++) and Decodium (a Qt/C++ WSJT-X derivative; its FST4 code was not inspected);
`site:github.com Q65 decoder C port qracodes q65_decode GPL` found `mfsk-core` again and QR-code libraries (unrelated). OnAir was found by
reading a local clone of github.com/adswill/OnAir (its `THIRD_PARTY.md` names WSJT-X as the source;
`core/src/hfdig/hfdig_fst4.cpp` and `hfdig_q65.cpp` are the decoders). Not searched: SourceForge, Codeberg and GitLab mirrors of other ports.

## Files taken, verbatim unless listed under "changes"

`upstream/hfdig/` from OnAir `core/src/hfdig/` (about 3,900 lines): `hfdig_fst4.cpp` (370), `hfdig_q65.cpp` (397 after changes), `hfdig_q65_tables.cpp`,
`hfdig_wsjt.cpp` (738: resampler, spectrogram, tone fits, sync search, drift search, LDPC encode / belief propagation / OSD), `hfdig_wsjt_tables.cpp` (the LDPC tables of
WSJT-X lib/fst4/ldpc_240_*.f90), `hfdig_wsjt_int.h`, `hfdig_ftx_msg.cpp` (77-bit message packing and unpacking, after WSJT-X packjt77.f90), `hfdig_ftx_tables.cpp`,
`hfdig_ftx_int.h`, `hfdig_wspr.cpp` (the 50-bit message of FST4W; after the WSJT-X wsprd utilities).
`upstream/include/dect2/common/{bits,str,window}.h` from OnAir `core/include/dect2/common/`.
`upstream/wsjtx_q65/` from WSJT-X `lib/qra/q65/`, unchanged (about 2,900 lines): `q65.c` / `q65.h` (it includes `fadengauss.c` and `fadenlorentz.c`, which are
therefore present but not compiled on their own), `qracodes.c` / `.h`, `npfwht.c` / `.h`, `pdmath.c` / `.h`, `qra15_65_64_irr_e23.c` / `.h`.
Left out of the WSJT-X directory: `normrnd.*` (random numbers for its simulator), `q65test.c`, the Fortran files, the Visual Studio project files.

## Changes to upstream files (all marked "FoxSDR helper" in the source)

1. `hfdig_wsjt.cpp`, `hfdig_wsjt_int.h`: `resample8k()` takes the input rate (`rateIn`, default 8000) and `Slot` has a `rate` field, so 12 kHz audio is
   resampled to the decoder rates directly instead of being converted to 8 kHz first. `hfdig_fst4.cpp`, `hfdig_q65.cpp`: pass `s_.rate`.
2. `hfdig_q65.cpp`: the candidate loop has a second round that calls `q65FastFading()` (new file `src/wm2_q65ff.cpp`) on the candidates the plain
   decoder did not decode; members `ffMode_` and `lastFF_`; a decode from that pass is not subtracted from the audio.
3. Nothing else in the OnAir files was edited. Two things OnAir's files expect are provided by this helper instead: `compat/dect2/hfdig/hfdig_ftx.h` (only the
   `FtxDecode` record from OnAir's receiver header, which declares the whole HF receiver) and `compat/dect2/dsp/fftutil.h` (a self-contained radix-2 FFT with the
   same `Fft` interface; OnAir's sits on its own FFT kernels). `compat/wm2_msvc.h` supplies `__builtin_ctzll` and `__builtin_popcountll` for MSVC
   (force-included for the two files that use them).

## New code (GPL-3.0-or-later, written for this helper)

`src/wm2_main.cpp` (command line, slot clock, input loop, decode worker thread, JSON output), `src/wm2_q65ff.{h,cpp}` (the fast-fading decode, after WSJT-X
`q65.f90` q65_dec2 / q65_s1_to_s3 and `q65_decode.f90`), `compat/*`, `test/tools/wm2gen.cpp` (test tool), `test/tools/sweep.py`, `sweep_all.sh`, `test/run_case.cmake`.

## What the decoders are, and are not

Built: FST4 and FST4W at every period (15, 30, 60, 120, 300, 900, 1800 s), Q65 A .. E at 15, 30, 60, 120 and 300 s. FST4-60, FST4W-120 and Q65-60A first,
as asked; the other periods run the same code with other constants and were tested at 15, 30, 120 and 300 s (FST4, Q65) and 300 s (FST4W) on generated signals
(900 and 1800 s only by running the 30-minute WSJT-X sample, which decoded nothing at any period, so those two are UNVERIFIED).
Not built: the 8-symbol combination of WSJT-X's FST4 decoder, multi-pass subtraction for FST4, a-priori (AP) decoding and AP list decoding for FST4 and Q65,
Q65 averaging over several periods, the "Max drift" and "Deep search" settings, SuperFox, anything that talks to a network.
Not proven against WSJT-X side by side: the program was not run next to a WSJT-X build (none is available here); real-signal proof is WSJT-X's own sample recordings.

## Upstream notice files

`upstream/onair-notices/` holds `NOTICE.md`, `THIRD_PARTY.md` and `TRADEMARKS.md`, copied unchanged from OnAir at commit `e118396b25d5f1dc02b3098470bc289054897b9f`. They describe the whole OnAir project, not only the files taken here. The "credits data" that `NOTICE.md` mentions is not included.
