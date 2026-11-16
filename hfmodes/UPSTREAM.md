# UPSTREAM.md: where hfmodes comes from

Helper licence: **GPL-3.0-or-later**, plus the additional attribution term of OnAir (GPL section 7(b),
`ADDITIONAL-TERMS.md`, kept as it is). `LICENSE` is OnAir's copy of the GPL-3.0 text.

## 1. OnAir (the main upstream)

- Project: OnAir, Kristijonas Adomaitis and OnAir contributors. https://github.com/adswill/OnAir
- Commit taken: `e118396b25d5f1dc02b3098470bc289054897b9f` (2026-11-12), shallow clone, read locally.
- Licence: GPL-3.0-or-later (LICENSE file read: GPL version 3 text; `REUSE.toml` says "Copyright (C) 2026 Kristijonas Adomaitis and
  OnAir contributors", GPL-3.0-or-later; NOTICE.md and `ADDITIONAL-TERMS.md` add the attribution term). The terms ask that anyone conveying
  OnAir code keep ADDITIONAL-TERMS.md and the notices in the files, and show "Based on OnAir by Kristijonas Adomaitis - https://on-air.dev"
  in the program's legal notices; the helper prints it in `--version` and `--help` (a test checks that) and in the hello line.
- Language: C++ (C++20, CMake). Nothing needed porting. The taken files carry no per-file licence header (OnAir keeps that in REUSE.toml); none was
  added, so the files are byte for byte upstream except for the changes in section 4. `third_party/onair/NOTICE.md` is OnAir's NOTICE.md.
- What OnAir calls what: `core/src/hfutil/hfu_fsk.cpp` is the HF utility FSK / MFSK family; `core/src/cishf` the CIS modes; the marine front end and
  NAVTEX word decoder under `core/src/marine` plus `core/src/sitor/sitor_arq.cpp` are SITOR-B and SITOR-A. The class `SitorLiveReceiver`
  (`core/src/sitor/sitor_live.cpp`) is the flow `SitorChannel` in `src/main.cpp` re-creates without OnAir's radio front end.

### Files taken (verbatim, unless section 4 lists a change)

| Upstream path | Lines |
|---|---|
| `core/src/hfutil/hfu_fsk.cpp` (changed, section 4) | 976 -> 1001 |
| `core/include/dect2/hfutil/hfu_fsk.h` (changed) | 59 |
| `core/src/cishf/cishf_dsp.cpp`, `cishf_gen.cpp` (generators, used by the tests) | 1019, 230 |
| `core/include/dect2/cishf/cishf_dsp.h` | 98 |
| `core/src/marine/marine_dsp.cpp`, `marine_navtex.cpp` | 369, 355 |
| `core/include/dect2/marine/marine_dsp.h`, `marine_navtex.h`, `marine_tel.h` | 150, 83, 123 |
| `core/src/sitor/sitor_arq.cpp`, `core/include/dect2/sitor/sitor_arq.h` | 319, 102 |
| `core/include/dect2/common/ring.h`, `mathconst.h`, `window.h`, `bits.h` | 194, 17, 24, 63 |
| `core/include/dect2/dsp/dsp_compat.h`, `fftutil.h` | 25, 30 |
| `LICENSE`, `ADDITIONAL-TERMS.md`, `NOTICE.md` | licence texts |

About 4,100 lines of OnAir code are compiled in. The git history shows the verbatim import (`f41991d`) before any change.

### Left in OnAir and not taken

- `core/src/hfdig/hfdig_sitor.cpp` (the word-level "FEC-A / FEC-B" class): see FEC-A in README.md.
- `core/src/pactor`, `core/src/pactorhf` (PACTOR-I, II, III, IV): tied to OnAir's engine classes (`engine/mode_tuning.h`, `audio/hf_demod.h`,
  `audio/ham_listen.h`, `marine_dsp` front end, mode synthesis); a port needs the audio and tuning layers replaced. Not done here.
- `core/src/hfutil`: 3G-ALE, STANAG 4285, MIL-STD-188-110, STANAG 4481, Link 11 (`hfu_ale3g*.cpp`, `hfu_serial.cpp`, `hfu_4481.cpp`,
  `hfu_link11.cpp`, `hfu_hdr.cpp`): not part of this port.
- `core/src/shortwave`, `milhf`, `hfdig` (RTTY, PSK, Olivia ...), `ale`: not asked for.

## 2. Rivet (CROWD36 and CIS 36-50)

- Project: Rivet, Ian Wraith. https://github.com/IanWraith/Rivet. Commit read: `41f81b236db2fb6548bc43486b48fc9eefe2a837` (2014-01-01).
- Licence: **GPL-3.0-or-later per the header of every source file read** ("either version 3 of the License, or (at your option) any later
  version"; "Rivet Copyright (C) 2011 Ian Wraith"). The repository has NO LICENSE file; its README.txt carries no licence text.
- Language: Java (Swing). Ported to C++: `src/crowd36.cpp` and `crowd36.h`, a derivative work under the same licence. Ported from
  `src/org/e2k/CROWD36.java` (332 lines): the 34-entry tone table `TONES[]`, `C36A[]` / `F36A[]` character tables, `getTone`, `getChar`, `syncToneHunt`
  (four symbols, high-low-high-low, 100 Hz apart, correction to sync tone 24), the symbol timing from the lowest energy value, the state machine;
  from `MFSK.java` `doCR36_8000FFT` (200 samples, Blackman over 512 points zero-padded, FFT peak); from `FFT.java` `getSpectrum`, `getFFTFreq`,
  `findHighBin`, `getPercentageOfTotal`, `windowBlackman`; from `CircularDataBuffer.java` the 10240-sample delay buffer, `getABSAverage`, `returnLowestBin`.
  Upstream names are in comments next to each part. About 250 lines of C++ against about 450 lines of Java read.
- Compatibility: OnAir and Rivet are both GPL-3.0-or-later, so one combined program under GPL-3.0-or-later is allowed. `crowd36.cpp` is the only
  file with Rivet code; its header says so. OnAir's section 7(b) term applies to the OnAir files and is kept for them.
- Changes from Rivet (all in `src/crowd36.cpp`):
  1. Streaming input (any audio rate resampled to 8 kHz) instead of a WAV file with a GUI; no dialogs, no Swing.
  2. The sync hunt runs every fourth sample, not every sample (the timing step that follows does not depend on it).
  3. A tone outside the table (below tone 0 or above tone 33 by more than 40 Hz) prints `?` and counts as bad; Rivet clamps it to tone 0 or 33.
  4. A symbol whose spectrum peak is under 5 % of the summed spectrum (Rivet's own "just noise" test in the hunt) is treated as no signal:
     nothing is printed, and after 40 such symbols (one second) the decoder goes back to hunting. Rivet keeps printing noise.
  5. `cr` prints a newline; Rivet only sets its line counter to 50.
  6. Samples are rounded to 16-bit integers first, as Rivet reads them from WAV.
- Not taken: Rivet's other decoders (XPA, XPA2, FSK200/500, GW, CCIR493, RTTY ...).

### Rivet CIS 36-50 (`src/cis3650.cpp`, `cis3650.h`), mode `CIS-36-50`

- Ported from `src/org/e2k/CIS3650.java` (455 lines) with `FSK.java` (`do80FFT`, `do80FFTBinRequest`, `getPercentageDifference`, `kalmanFilter`, `checkITA3Char`, `ITA3VALS`),
  `FFT.java` (`getSpectrum`, `getFFTFreq`, `findHighBin`, `getPercentageOfTotal`, `windowBlackman`) and `CircularDataBuffer.java` (the 10240-sample delay buffer). The state machine,
  the 50 Bd alternating-sync hunt (two symbols 200 Hz +-10 apart, peak at least 10 %), the early/late Kalman symbol timing (0.99 / 0.009, divisor 2), the 7-bit buffers, the
  184-bit start buffer test (21 ones in 44 bits, two keys at most one bit apart, 8 valid characters), the end-of-message word 0x1DFBF7 and the 5000-character limit are Rivet's.
  About 230 lines of C++.
- Changes: streaming 8 kHz input (resampled from any rate) with no GUI; the 80-point real FFT is computed as a direct DFT of the 39 bins (80 is not a power of two); events instead
  of Rivet's status label and text lines (the "Message Start", "Sync 0x..", "Session Key" lines become one `event`; characters stay `0x..` hex, errors `[0x..]`, `<EOM>`);
  an empty spectrum leaves the Kalman filter alone (Rivet divides by zero and the filter stays NaN); the debug (raw bits) mode is not ported.
- Limits of Rivet's decoder, kept: 50 Bd at 200 Hz only (it checks the sample rate is 8 kHz and the tone gap is 200 Hz); no 36 Bd idle; it reads 1.28 s behind the audio.
- This is the decoder named `CIS-36-50`. OnAir's separate decoder stays available as `CIS-36-50-ONAIR`; the two share no code and are not mixed.

## Which upstream each decoder comes from

| Mode name | Upstream |
|---|---|
| ARQ-E, ARQ-N, AUTOSPEC, SPREAD, G-TOR, EFR, PICCOLO-MK6, COQUELET-8 | OnAir `hfu_fsk` |
| COQUELET-13 | OnAir `hfu_fsk` plus a table typed from Crypto Museum (section 3); no Rivet or python-codext code |
| CIS-36-50-ONAIR, CIS-14, CIS-12, CIS-36, CIS-OFDM-HDR, CIS-128, CIS-3000 | OnAir `cishf` |
| SITOR-B, SITOR-A | OnAir `marine` / `sitor_arq` |
| CIS-36-50, CROWD36 | Rivet |

## ARQ-E control signals

RQ / alpha / beta (reported as 1110000 / 1000110 / 1001001 from a Wikipedia article citing Wiesner 1975) are UNVERIFIED against ITU-T F.342-2. They are not used anywhere in this helper.

## 3. Facts used from other places (no code taken)

- Coquelet 13 tone table: Crypto Museum, https://www.cryptomuseum.com/ref/coquelet/index.htm and the figure
  `svg/coquelet13_bits.svg` on that site (read on 2026-11-12 in the browser pane: tones 1 to 8 at 812 to 1022 Hz carry ITA-2 bits 3 4 5 as 111, 011, 001, 100,
  101, 110, 010, 000; tones 9 to 12 at 1082 to 1172 Hz carry bits 1 2 as 11, 10, 01, 00; tone 0 at 1052 Hz idle; 75 ms per tone). The page text gives
  frequencies and "each tone lasts 75 ms"; the bit table is only in the figure.
- python-codext (dhondta/python-codext, GPL-3.0, commit `ee726c2cbe0ae627372bc027fde1de6e0de35cca`), `src/codext/frequency/coquelet.py`: read as a cross-check
  only. Its tables for Coquelet 13 equal the figure; its printed example for 'TEST' (13: `872 1172 1022 1112 902 1112 872 1172`; 8:
  `826 960 960 907 853 907 826 960`) is what `tests/unit.cpp` compares the generators with, for Coquelet 13 (independent table typed from the figure) and
  for OnAir's Coquelet-8. No python-codext code or text is in this helper. Wavecom's Coquelet 8 page gives the 8 tones 773 to 960 Hz and the durations; its bit columns
  could not be read.
- The Wavecom FEC-A description (README.md) comes from search-result excerpts of the W74PC and W61PC manuals (manualslib / manualsdir); the manual pages themselves were
  not retrievable (403 / 404).

## 4. Changes to OnAir files

1. `hfu_fsk.h`: `FskKind` gets `Coquelet13` (one line, with a comment).
2. `hfu_fsk.cpp` (+45 lines, -11): Coquelet 13 in the generator (13 tones from 812 Hz every 30 Hz, tone index 8 as the single idle tone, pairs of a group I tone
   and a group II tone 9..12), in `nTones()` / `toneSpacing()` / `mfskCandidates()`, in `findMfsk()` (a single strong tone is the idle tone, so the base is 8 tones lower),
   `decodePairs()` (idle tone skipped; a character is a group I tone then a group II tone; no pair-phase search) and `pump()`; the tone-energy arrays are 16 wide
   instead of 12. The Coquelet 8 and Piccolo paths are unchanged. The bit tables `kCoq1` / `kCoq2` are OnAir's and agree with the Crypto Museum figure.
3. Everything else is unchanged. `git diff f41991d HEAD -- hfmodes/third_party` shows exactly this.

## 5. Written for this helper (GPL-3.0-or-later)

- `src/main.cpp`: protocol, argument parsing, JSON Lines, per-mode channel glue (status, text coalescing), the SITOR flow without OnAir's radio front end (the FSK
  pair search is widened from +-600 Hz around the dial to +-3 kHz because audio input has no dial).
- `src/frontend.*`: resampler (windowed sinc), Hilbert analytic signal, 24 kHz to 12 kHz half-band (this last copied from OnAir's `core/src/cishf/cishf_rx.cpp`, class `Half`).
- `src/dsp_compat.cpp`: a plain radix-2 `fftSplit`, replacing OnAir's 430-line SIMD file; `src/msvc_compat.h`: `__builtin_popcount` for MSVC.
- `tests/`: generator, unit tests, CTest scripts, pinned expectations.

OnAir libraries not installed were not needed: no fftw, no Qt, no libsndfile.

## 6. Searches run

- WebSearch: `"FEC-A" HF teleprinter mode 100 baud 170 Hz CCIR 476 Wavecom decoder` (Wavecom's FEC-A is its own mode); `Wavecom decoder "FEC-A" "ARQ-1A" alphabet convolutional "14 bits" codeword`.
- WebSearch: `site:github.com "Twinplex" decoder HF` (nothing); `Twinplex ARQ 7-unit HF teleprinter system description Twinplex decoder source code` (descriptions: sigidwiki TWINPLEX, hoka.it twinplex page, "Electrical Communication Twinplex March 1952" PDF, cartoonman.github.io/Shoc, no code); `site:github.com Twinplex ARQ teleprinter` (nothing relevant).
- WebSearch: `Coquelet 13 Coquelet 8 tone table frequencies Hz sigidwiki` (nothing useful); then the Crypto Museum and Wavecom pages and the SVG were opened directly.
- An earlier browser pass searched: `"CROWD36"` (Rivet), `"CIS-36"` decoder mfsk, `"Coquelet"` mfsk decoder, `"FEC-A"` hf decoder, `"ARQ-E"` queries.
- Coquelet 80: no description or decoder found in any search; FEC-A (Wavecom's convolutional mode): no decoder or polynomial found; Coquelet 13: found (Crypto Museum, python-codext).
- Not found by any search: a decoder for Twinplex; Wavecom's FEC-A polynomials; the ARQ-E control signal (RQ, alpha, beta) bit patterns (ITU F.342 Annex I).

## 7. Proof

`ctest --test-dir <build> -C Release`: 44 tests (unit, 19 generated-signal pipe tests with pinned JSON Lines and decoded-text checks, all decoders at once,
failure paths, the manifest, a real SITOR-B recording). Break-it checks done: the text output of the FSK family disabled and the Coquelet group II table
swapped (4 tests RED: `unit`, `run_arqe`, `run_efr`, `run_coquelet13`); the CROWD36 letter table and the CCIR 476 space code changed (`unit`, `run_sitorb`, `run_sitora`
RED); each restored with `git checkout` and the suite green again. Note: `run_crowd36` stayed green with the CROWD36 table swapped because generator and decoder share the
table; the unit test (`toneForChar('Q') == 0` against Rivet's table) is what catches it.

The Rivet CIS 36-50 port: the start-word check changed from 21 to 20 ones turned `run_cis3650` RED; restored, suite green (44 of 44).

## Upstream notice files

`third_party/onair/THIRD_PARTY.md` and `third_party/onair/TRADEMARKS.md` are copied unchanged from OnAir at commit `e118396b25d5f1dc02b3098470bc289054897b9f`, next to the `NOTICE.md` that refers to them. They describe the whole OnAir project, not only the files taken here. The "credits data" that `NOTICE.md` mentions (a file the OnAir program ships with) is not included, because no OnAir credits screen is part of this program.
