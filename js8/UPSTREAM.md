# js8 helper: upstream, provenance and changes

## What was taken

| | |
|---|---|
| Project | JS8Call (original repository) |
| URL | https://github.com/js8call/js8call |
| Commit | `a7ff1be0b389d287fdc56e2ea0d06962aa68127d` (Fri 2025-12-05, "Update README links and version information"), shallow clone, read locally |
| Licence | GPL-3.0. `COPYING` in the repository is the GPL version 3 text (read); GitHub's metadata says GPL-3.0. `varicode.cpp` says "version 3 ... or (at your option) any later version"; `JS8.cpp`, `JS8.hpp`, `commons.h`, `decodedtext.cpp` state no licence of their own. So this helper as a whole is **GPL-3.0-only** (`LICENSE` is the full GPL-3.0 text from `gh api licenses/gpl-3.0`). |
| Copyright holders named in the files | (C) 2018 Jordan Sherer, KN4CRD (varicode, jsc, decodedtext, Detector, Modulator ...); (C) 2025 Allan Bazinet, W6BAZ (JS8.cpp, the C++ decoder). The upstream headers are kept in the copies. |
| Receive side only | taken: the decoder and the message unpacking. NOT taken: any transmit path of the program (the sound-card modulator `Modulator.cpp`, the Qt main window), rig control, UDP/TCP APIs, APRS-IS, PSKReporter, spotting. |

Pristine copies of the files that were ported are in `upstream/` (line endings normalised to LF by git; otherwise as in the
commit): `JS8.cpp`, `JS8.hpp`, `commons.h`, `JS8Submode.cpp/.hpp`, `varicode.cpp/.h`, `jsc.cpp/.h`, `decodedtext.cpp/.h`.
They are for reading and diffing and are **not built**.

## Which upstream, and why (the first step)

Candidates, licences confirmed from the LICENSE files:

| Candidate | Licence | Decoder | Verdict |
|---|---|---|---|
| js8call/js8call (original) | GPL-3.0 (COPYING read) | C++17/20, no Fortran left: one file `JS8.cpp` (2800 lines) holds the whole decoder, BP/LDPC and the encoder. Needs FFTW, Boost (crc, multi_index, ccmath), Eigen, Qt (only the thread wrapper and QString in the message layer). All five speeds. | **Chosen.** Already C++, one self-contained decoder file, every dependency is small enough to replace. |
| JS8Call-improved/JS8Call-improved (commit bad77ff) | GPL-3.0 (LICENSE read) | the same design plus soft combining, successive-interference cancellation, whitening, decoder-aided demodulation, LDPC feedback in `JS8_Mode/*.h` (185 KB `JS8.cpp` plus about 150 KB of headers), Boost, Eigen, Qt. | More sensitive, but about three times the code to port and verify; not needed for a first helper. A later lane can add it. |
| liamsto/js8rs (commit 8c2264d) | GPL-3.0-only (LICENSE and Cargo.toml read) | Rust port of JS8Call-improved ("a derivative work", README). No Boost or Qt, but Rust (generics over const parameters, `rustfft`, `phf` tables) has to be translated to C++ and it carries all of the "improved" machinery. | Not chosen; useful as a cross-check: its test list gives the frames the improved decoder finds in JS8Call's sample files (compared below). |
| rtmrtmrtmrtm/fate (commit 7ed9e57) | MIT (LICENSE read) | C++ receiver, Normal speed only, needs fftw3, sndfile, portaudio; its README lists compound call-signs and speeds other than Normal as missing. Its source says "Many ideas and protocol details borrowed from ... WSJT-X". | Not used: Normal only, no compound frames, and its provenance relative to JS8Call's GPL tables was not checked (UNVERIFIED). Possibly a lead for a non-GPL Normal-speed decoder; I did not open `arrays.h`, `libldpc.cc` or `words.txt`. |
| dougsko/js8d | contradictory | not opened | not used, as instructed |
| others seen in `gh search repos js8` | randal007/x6100-js8 (LGPL-2.1, C, firmware for a radio), NR5P/js8-firefly (GPL-3.0, an iOS/Mac app), W5DMH/microjs8 (GPL-3.0, Python), jketterl/js8py (GPL-3.0, Python parser of the `js8` CLI's output), freerainboxbox/JS8File | file listings only | not examined further; none is a standalone C or C++ decoder that is simpler than the original |

Searches run (2026-11-12): web search `site:github.com "JS8" decoder C++ OR C "JS8Call" fork decoder library`;
`site:github.com js8 decoder standalone command line "js8" "costas" ldpc jsc`;
`site:github.com js8 decoder library rust OR go OR python "JS8" demodulator LDPC "JS8Call" port`;
`site:gitlab.com OR site:codeberg.org OR site:sourceforge.net js8call decoder source` (nothing on GitLab or Codeberg);
`gh search repos js8 --limit 30`; `gh api repos/<r>` for js8call/js8call, JS8Call-improved/JS8Call-improved, liamsto/js8rs,
rtmrtmrtmrtm/fate, randal007/x6100-js8; the READMEs, LICENSE files and decoder file headers of the first four were opened.
(The web search tool in this session is not a Google browser; the `site:` queries were run through it.)

## Files in the helper folder and where each comes from

| File | Origin |
|---|---|
| `src/js8_decoder.cpp` | `JS8.cpp`, ported (changes below). About 2650 lines; 425 upstream lines removed or replaced, 277 added, the rest unchanged. Upstream function and variable names are kept (`js8dec`, `syncjs8`, `syncjs8d`, `baselinejs8`, `js8_downsample`, `bpdecode174`, `subtractjs8`, `genjs8refsig`, `JS8::encode`, `DecodeMode<Mode>`). |
| `src/js8_core.h` | replaces `JS8.hpp` and `commons.h`: constants, Costas arrays and the submode numbers copied; the Qt `Decoder` class and the event variant dropped. |
| `src/js8_varicode.cpp/.h` | port of `varicode.cpp`, `jsc.cpp`, `decodedtext.cpp` from Qt types to the C++ standard library (about 1050 lines). Receive side: `unpack72bits`, `unpackAlphaNumeric50`, `unpackCallsign` (group table), `unpackGrid`, `unpackCmd`, `huffDecode`, `JSC::decompress`, `unpackCompoundFrame`, `unpackDataMessage`, `unpackFastDataMessage`, and `DecodedText`'s order of unpack strategies and message texts. Transmit side, used only by the test tool: the matching pack functions and `JSC::compress`. |
| `src/jsc_map.cpp` | `jsc_map.cpp` of the commit, the 262144-word JSC dictionary, **unchanged** (identical to upstream apart from line endings). |
| `src/jsc.h` | upstream `jsc.h` cut down to the table declaration `jsc_map.cpp` needs. `jsc_list.cpp` (the second 6.5 MB table, sorted for prefix lookup) is not used: the test tool derives the same longest-prefix lookup from `jsc_map.cpp` at run time. |
| `src/fftw_shim.cpp/.h` | written for this helper: the five FFTW calls `JS8.cpp` makes, on a mixed-radix FFT (any length; E mode needs a factor 47). |
| `src/helper_main.cpp` | written for this helper: command line, protocol, scheduling of the periods, JSON. |
| `tools/js8gen.cpp` | written for this helper: the TEST TOOL. Uses `JS8::encode` (upstream), the pack functions, and the continuous-phase 8-FSK modulation of `Modulator.cpp` (tone spacing 12000/NSPS Hz, NSPS samples per symbol at 12 kHz), minus the sound card plumbing; writes raw or WAV files. Not part of the helper, not installed with it. |
| `FOXSDR_SAMPLES_DIR/*.wav` | JS8Call's `media/tests` sample files, unchanged (see `the top-level README`). |

## Changes to the upstream code (each marked `FOXSDR:` in `src/js8_decoder.cpp` where it is in the decoder)

1. **FFTW removed**: `fftw3.h` and the plan calls are served by `fftw_shim.h`, a drop-in for exactly those calls. Results differ from FFTW in rounding only. The FFTW mutex locks are gone (single thread).
2. **Boost removed**: `boost::augmented_crc<12, 0xc06>` written out as a bitwise register (verified indirectly: it decodes JS8Call's own recordings, whose frames carry that CRC); `boost::math::ccmath::round` replaced by a one-line constexpr round; the `boost::multi_index_container` of sync candidates replaced by `SyncIndex` (the three indices became: ascending rank for the 40th-percentile normalisation, strongest-first extraction, removal of every entry within ±AZ Hz of a picked candidate; ties are ordered by frequency, NaN sorts last).
3. **Eigen removed**: the degree-5 baseline polynomial was a `colPivHouseholderQr` solve of a square 6x6 Vandermonde system; now Gaussian elimination with partial pivoting in double, after scaling the columns by powers of the largest x (x runs to ~640, x^5 to ~1e14; scaling does not change the solution, it keeps the conditioning sane). The Estrin evaluation is a plain loop over the same three coefficient pairs.
4. **Qt removed**: the `Worker`/`Decoder` thread classes, the semaphore, the event variant (`DecodeStarted`, `SyncStart`, `SyncState`, `DecodeFinished`) and the sync-statistics events are gone; `JS8::decode()` runs one period for one speed synchronously and calls back for each `Decoded` event. The 60-second ring buffer of 16-bit samples (`dec_data.d2`, with start and size) is replaced by passing the period's samples directly.
5. **Message layer**: `QString`/`QMap`/`QVector<bool>` became `std::string`/tables/`std::vector<bool>`. Differences of substance: `QMap::key()` ordering is reproduced by explicit tables (first key in ascending order holding the value); `JSC::decompress` stops if a code needs `base[8]` (upstream would read past its array); JSC words (Latin-1) are converted to UTF-8 on output; the `huffDecode` loop keeps its quirk of scanning later keys against the advanced bit string.
6. **Not ported** (transmit side or application level): `buildMessageFrames`, the typed-text regular expressions (`heartbeat_re`, `compound_re`, `directed_re`), `isValidCallsign`, the CRC-16/CRC-32 checksums of buffered commands, reassembly of multi-frame messages, the heartbeat/spot/APRS logic of the main window.
7. The decoder's frame output: upstream emits an event on a new frame and again when a later pass finds the same frame with a better SNR; the helper prints one line per distinct frame per period (the best-SNR event).

## What this helper decodes, and what it does not

* Speeds: **A Normal (15 s), B Fast (10 s), C Turbo (6 s), E Slow (30 s)** by default; **I Ultra (4 s)** only with `--modes ...I` (the decoder code exists upstream but the JS8Call build has it switched off, `JS8_ENABLE_JS8I 0`; it decodes generated signals here and is untested on real ones).
* Frame types reported: heartbeat (HB and the CQ variants), compound, compound directed, directed (all 32 commands), data (legacy Normal-speed Huffman/JSC frames and the Fast-data JSC frames of the other speeds), unknown.
* Input is 12 kHz audio only. Periods are aligned to UTC multiples of their length: the bridge gives `--start-time`; without it the first sample is taken as a boundary.
* Not decoded by this upstream version: whatever only the JS8Call-improved decoder finds. On JS8Call's sample files: `A_2_3.wav` expected 3 frames by js8rs's test list (improved decoder), this helper finds 2; `A_2_6.wav` expected 6, this helper 4; `A_1_4`, `A_2_9`, `E_1_1`, `A_2_1` (none) match. The frames this helper decodes are all in js8rs's lists, with the same 12-character frames (compared one by one).
* No transmit, no rig control, no network, no decryption (JS8 has none).

## Verification and what is UNVERIFIED

* Verified: builds with MSVC (VS 2022 BuildTools, x64 Release, `/std:c++20`); 31 CTest cases pass (README); RED with the decoder gutted, GREEN restored.
* The real JS8Call program was not run here (it needs Qt and the rest), so "same output as upstream" is shown only through JS8Call's own sample files and the frame lists js8rs publishes for them, not by diffing against a JS8Call run.
* Over-the-air behaviour beyond those sample files (frequency drift, band conditions), the SNR scale of the fast speeds against real stations, and the Ultra speed on real signals are UNVERIFIED.
* Patents: none of the excluded list (AMBE+2, LoRa chirp, DVB-S2 short frames, Itron ERT) is involved. Whether JS8 itself is encumbered was not searched.
* `FOXSDR_SAMPLES_DIR/*.wav`: JS8Call's repository (GPL-3.0) is the only licence statement seen for them.

## Build

```
cmake -S js8 -B <build dir> -G "Visual Studio 17 2022" -A x64
cmake --build <build dir> --config Release --parallel 4
ctest --test-dir <build dir> -C Release --output-on-failure
```
Targets: `js8-helper` (the helper), `js8gen` (test tool), `js8core` (library). `/bigobj` is needed for `jsc_map.cpp`.
