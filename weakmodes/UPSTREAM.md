# weakmodes: upstream sources, licences, changes

The helper is a derivative work of GPL-3.0 code and is itself **GPL-3.0-or-later** (`LICENSE` is the GPL-3.0 text from
`gh api licenses/gpl-3.0`). No code from FoxSDR is in it and none of it goes into FoxSDR. It is a C++ port: the Fortran
(and a little C++) sources below were read in shallow local clones and re-written in C++17 with
the upstream file and routine names kept in the comments beside each ported part. Nothing was fetched except source
files (`git clone --depth 1`); no binary, installer or script from the internet was run.

## Sources and licences

| mode | source taken | commit | licence (file read) |
|---|---|---|---|
| FT2L | kosciej/wsjtx-ft2libre, `wsjtx/lib/ft2l_decode.f90`, `lib/ft2l/*`, shared routines | `356be92606a6020497740cece60f32c6ad607cff` (2026-02-26) | GPL-3.0 (`COPYING`; GitHub: GPL-3.0). A WSJT-X derivative. |
| MSK144 | WSJT-X, `lib/` (SourceForge git `https://git.code.sf.net/p/wsjt/wsjtx`) | `b4f9a431bcf6449df8f37b56de79d48b665b044c` (2025-02-04) | GPL-3.0 (`COPYING`) |
| JT9 (JT9A) | WSJT-X, `lib/` (same clone) | `b4f9a43` | GPL-3.0 (`COPYING`) |
| 77-bit message unpack (FT2L, MSK144) | WSJT-X `lib/77bit/packjt77.f90` | `b4f9a43` | GPL-3.0 |
| LDPC(174,91) encode / BP / OSD, CRC-14 (FT2L) | `lib/ft8/*174_91*.f90`, `get_crc14.f90`, `platanh.f90` from wsjtx-ft2libre; byte-identical to the WSJT-X files at `b4f9a43` (checked with `cmp`) | both | GPL-3.0 |

Only GPL-3.0 code is in the helper, so there is no licence mixing. The test recordings under `FOXSDR_SAMPLES_DIR/` are WSJT-X's
own sample files (GPL-3.0) with the WAV header removed.

### The search for existing C or C++ ports (done 2026-11-12, before porting)
Method: WebSearch with `site:github.com`, then the GitHub API for the licence file of every hit, then the repository
trees. Queries: `site:github.com MSK144 decoder C++ port wsjtx "msk144" GPL`; `site:github.com JT9 decoder C port "jt9"
fano standalone`; `site:github.com ft2 decoder "FT2" wsjtx ft2libre OR decodium OR "ft2l"`; `site:github.com mfsk-core
rust crate JT9 MSK144 FT8 FT4 WSJT-X port licence`; `site:github.com "msk144" C++ OR C decoder "ldpc_128_90" OR
"msk144decodeframe" -fortran`; `site:github.com "jt9" "fano" decoder C++ port wsjtx "sync9" OR "decode9w" OR
"jt9_decode"`; plus the earlier pass in an earlier search note.

| candidate | language | licence (checked) | verdict |
|---|---|---|---|
| iu8lmc/Decodium-4.0-Core-Shannon (commit `58f6867f`, 2026-11-06) | C++ / Qt | GPL-3.0 (`COPYING`, GitHub GPL-3.0) | The origin of FT2. Has C++ decoders `Detector/FtxFt2Stage7.cpp` (199 kB), `MSK144DecodeWorker.cpp` (105 kB), `JT9NarrowDecoder.cpp`, `FanoSequentialDecoder.cpp`. They are tied to Qt types (`QByteArray`, `commons.h`), AP caches and the program's threads, so they were **not used as the source**; the FT2 file was opened only to compare constants (below). |
| jl1nie/mfsk-core | Rust crate | GPL-3.0-or-later (GitHub `LICENSE`, README) | Ports JT9 and (feature `msk144`) MSK144 from the same Fortran, in Rust. Not C or C++; no advantage over the Fortran source, not used. |
| MSHV (SourceForge `mshv`) | C++ / Qt | stated GPL-3.0 on the project page; the licence file was not opened | A C++ rewrite of the WSJT modes. Not used (licence file unread, Qt-bound). |
| HegrJan/HamDigiSharp | C# | GPL-3.0 | From scratch, not a port; not used. |
| alexander-sholohov/msk144decoder, msk144gensim | C++ wrappers | `LICENSE`: "(GPLv3 OR MIT) - MIT for individual files; GPLv3 for produced binaries" (msk144decoder); no licence file (msk144gensim) | They call the WSJT-X Fortran; nothing to port. |
| RaspSDR/wsjtx, FxxMorgan/wsjtx-ft2h | Fortran/C++ forks | GPL-3.0 | Copies of WSJT-X; not needed. |
| kosciej/wsjtx-ft2libre | Fortran | GPL-3.0 | **The only open FT2 decoder other than Decodium's own**; used for FT2L. |

No C or C++ port of the MSK144 receiver or of the JT9 decoder was found that is not a wrapper or bound to Qt, so MSK144
and JT9 are ported from the official WSJT-X Fortran. FT2L is ported from wsjtx-ft2libre.

### FT2L is reverse-engineered. How far it matches the real FT2 (evidence, not proof)
wsjtx-ft2libre's author wrote FT2L from recordings of the closed program (their README says the published `ft2.it` text,
8-GFSK, was wrong). The Decodium source that is public now (`Detector/FtxFt2Stage7.cpp`, `FtxBitmetrics.cpp`) agrees
with FT2L on: NMAX 45000, ND 87, NS 16, NSPS 288, NDOWN 9 (1333.33 samples/s), Nh1 576; the four Costas blocks at
symbols 0-3, 33-36, 66-69, 99-102 with the same tone orders (0,1,3,2 / 1,0,2,3 / 2,3,1,0 / 3,2,0,1); the 77-element
scrambling vector `rvec` and the five CQ a-priori patterns, bit for bit; 1333.33 for the ibest to time conversion. Not
compared: the GFSK pulse and the tone spacing of the transmit waveform (the Decodium source tree has no modulator file
that I found), the real LDPC tables (FT2L uses the FT8 (174,91) code; Decodium's decoder loads `ldpc_174_91`), and any real FT2
recording. **No real FT2 signal was ever decoded by this helper.** The manifest, `README.md`, the hello line and every
FT2L line say it is reverse-engineered and may not match other FT2 software.

## Files taken and lines ported

### FT2L (from wsjtx-ft2libre `wsjtx/lib/`; line counts of the Fortran read)
`ft2l_decode.f90` 460 -> `src/ft2.cpp` `decode_period`; `ft2l/getcandidates2l.f90` 83, `ft2l/ft2l_baseline.f90` 49,
`ft2l/ft2l_downsample.f90` 48, `ft2l/sync2ld.f90` 96, `ft2l/get_ft2l_bitmetrics.f90` 161, `ft2l/subtractft2l.f90` 66,
`ft2l/ft2l_params.f90` 16 -> `src/ft2.cpp`; `ft2l/genft2l.f90` 90, `ft2l/gen_ft2lwave.f90` 69, `ft2/gfsk_pulse.f90` 6 ->
`src/ft2_wave.cpp`; `ft2l/ft2lsim.f90` 125 -> `tools/ft2l_gen.cpp` (test tool); `nuttal_window.f90` 15, `pctile.f90` 21,
`shell.f90` 27, `polyfit.f90` 72, `twkfreq1.f90` (the first-term use), `normalizebmet` (in `ft8/ft8b.f90`) -> `src/ft2.cpp`.
`ft8/decode174_91.f90` 155, `ft8/osd174_91.f90` 409 (+ `mrbencode91`, `nextpat91`, `boxit91`, `fetchit91`),
`ft8/encode174_91.f90` 58, `ft8/encode174_91_nocrc.f90` 47, `ft8/get_crc14.f90` 25, `platanh.f90` 24 ->
`src/ldpc174_91.cpp`; tables `ft8/ldpc_174_91_c_parity.f90` 270 and `ldpc_174_91_c_generator.f90` 86 -> `src/ldpc174_91_tables.h`
(converted by a throwaway script, no hand typing).
Result: `src/ft2.cpp` 642 lines, `src/ft2_wave.cpp` 76, `src/ldpc174_91.cpp` 528.

### 77-bit messages (from WSJT-X `lib/77bit/packjt77.f90`)
Receive side: `unpack77`, `unpack28`, `unpacktext77` (with `mp_short_div`), `to_grid4`, `to_grid6`, `to_grid`, `hash10`,
`hash12`, `hash22`, `ihashcall`, `save_hash_call` -> `src/unpack77.cpp` (489 lines). Test packer (`pack28`, `pack77_1`,
`packtext77`) -> `tools/pack77.h` (test tool).

### Program shell, not from upstream
`src/main.cpp` (pipe protocol), `src/wm_fft.cpp` (radix-2, mixed-radix and Bluestein FFT; WSJT-X uses FFTW),
`src/wm_common.h`, `tools/wmtool.cpp`, `tools/wm_wav.h`, the CMake files and the test driver
`test/run_case.cmake` (adapted from the multimon helper's).

### MSK144 and JT9
See the two appendices at the end of this file (the notes the porting passes wrote).

## Changes against upstream (FT2L, LDPC, unpack, FFT)
1. Single precision became double precision nearly everywhere (LDPC messages, LLRs and thresholds stay `float`); results
   can differ from a Fortran build in borderline cases. Nothing here was compared against a WSJT-X build (none installed).
2. FT2L: no my-call / his-call, so only the CQ a-priori pass remains (`ipass` 6); no contest a-priori modes; no early
   (partial period) decode passes; no timers; the QSO-frequency (`nfqso`) candidate priority is off.
3. `polyfit` (baseline) solves the normal equations by Gaussian elimination with pivoting instead of Cramer's rule with
   `determ`; `pctile` uses `std::sort` (same element); `indexx` in the OSD is a stable sort, so equal reliabilities are
   ordered by position (indexx's order for ties is not specified).
4. The OSD matrices are packed into 64-bit words and the second pre-processing rule works on packed codewords with per-byte
   distance tables (the code is linear, so the codeword of a candidate pair is the pattern's codeword xor two rows). It was
   checked against the straightforward byte-by-byte port on 400 random received words (a quarter with a 29-bit a-priori
   mask): the same message, nharderror and dmin in every case. About 4 times faster; at depth 3 FT2L needs about 8 s for
   30 s of busy noisy audio on this loaded PC (depth 2: about 2 s). The Fortran `cycle` in that rule ends the current i2
   step, not the chain walk; the first version of the port got this wrong and was fixed before the tests were pinned.
5. FT2L adds a "low confidence" note (JSON field `note`) to decodes whose WSJT-X quality figure `1-(nharderror+dmin)/60`
   is below 0.17, the figure for which WSJT-X's window prints a "?". The tests show one such false decode.
6. The stream wrapper decodes a period when its 45000 samples have arrived (periods counted from the first sample plus
   `--offset`); a last partial period of at least 3.1 s is zero padded at end of input.
7. `unpack77` keeps its hash tables for the whole run (as WSJT-X does); `my-call`/`dx-call` substitution and the recent
   calls list are left out. `unpack77_reset()` exists for the tests.
8. `wm_fft.cpp` replaces FFTW; `tools/fft_test.cpp` checks it against a direct DFT for 26 lengths and both signs.

## Left out
MSK40 short messages, MSK144 equaliser training and the OSD that `msk144decodeframe` never calls; JT9 sub-modes B..H, the
quick pass around a receive frequency, JT65; FT8 and FT4 (FoxSDR has its own closed decoders); SuperFox verification (not
in scope); the AP decoding passes that need a station call; every transmit path (`gen*` code is only in the test tools);
PSKReporter and any network code; rig control; Watterson fading in the generators; hashed-call lookups from a
call-sign list; the GUI and file/UDP outputs. Real FT2 traffic was never tested.

## Measured decode thresholds (generated signals, noise sigma 1 per sample at 12000 Hz; "SNR" in 2500 Hz as each upstream simulator defines it)
| mode | message | result |
|---|---|---|
| FT2L (20 seeds per step, one period, 1500 Hz, DT 0.1, final code) | `CQ K1ABC FN42` | -11, -12, -13 dB 20/20 (**lowest SNR decoding every seed: -13 dB**), -14 dB 12/20, -15 dB 4/20, -16 dB 0/20, -17 dB 0/20 |
| MSK144 ping (20 seeds) | `K1ABC W9XYZ EN37` | +6 .. -2 dB 20/20, -4 dB 12/20, -6 dB 1/20, -8 dB 0/20 (SNR at the peak of the ping) |
| MSK144 continuous carrier (10 seeds) | same | -8 dB 10/10, -10 dB 2/10 |
| JT9A (20 seeds, jt9sim nominal SNR, about 0.8 dB worse than true 2500 Hz SNR) | `CQ K1ABC FN42` | -24 and -25 dB 20/20, -26 dB 17/20, -27 dB 9/20, -28 dB 1/20, -29 dB 0/20 |
The printed `snr` of FT2L follows the upstream formula `10 log10(sync-1) - 14.8` and reads about 4 to 5 dB below the true value on
these signals; it is not meaningful for strong signals.

## Real recordings
MSK144: WSJT-X's two sample files decode to `K1JT WA4CQG EM72`, `CQ W4IMD EM84`, `CQ KD9VV EN71`. JT9: WSJT-X's
`130418_1742.wav` decodes to seven messages (`CQ GM7GAX IO75`, `TF3G N7MQ CN84`, `K1JT KF4RWA 73`, `CQ M0WAY IO82`,
`K1JT N5KDV EM41`, `G7CNF N4HFA EL89`, `JA1KAU PD0JAC -23`). Not compared with a WSJT-X run of the same files.
FT2L: none available.

---

# Appendix A: MSK144 (notes written by the MSK144 porting pass)



Source: WSJT-X, `https://sourceforge.net/p/wsjt/wsjtx` (shallow clone, read locally), commit
`b4f9a431bcf6449df8f37b56de79d48b665b044c` (2025-02-04), licence GPL-3.0 (COPYING in the clone). No C or C++ port of
the MSK144 receiver exists on GitHub that is not a wrapper around the Fortran (searches: `site:github.com MSK144
decoder C++ port wsjtx`; hits were Decodium (a WSJT-X fork, same Fortran), RaspSDR/wsjtx (fork), alexander-sholohov
msk144decoder / msk144gensim (wrappers that call the WSJT-X Fortran; LICENSE "GPLv3 OR MIT" for the wrapper files),
MSHV (SourceForge, GPLv3, C++ with Qt; not opened), HamDigiSharp (C#). The official Fortran was therefore ported.

### Files read and ported (upstream lines in lib/)
decode_msk144.f90 56, mskrtd.f90 261, analytic.f90 75, msk144spd.f90 196, msk144sync.f90 101,
msk144_freq_search.f90 50, msk144decodeframe.f90 112, bpdecode128_90.f90 117, platanh.f90 24, tweak1.f90 23,
ft8/chkcrc13a.f90 24, crc13.cpp 31, ldpc_128_90_reordered_parity.f90 176 (tables), hspec.f90 (the `bmsk144` branch
only, 20 lines, for the call pattern). Generator side (test tool only): ldpc_128_90_generator.f90 41, encode_128_90.f90
58, genmsk_128_90.f90 120, msk144sim.f90 106, makepings.f90 25.
Result: `src/msk144.cpp` (657 lines C++), `src/msk144_tables.h` (generated by a throwaway Python script from the two
Fortran `data` files, no hand typing), `tools/msk144_gen.cpp`. Upstream routine names are in the comments.

### Changes against upstream (all deliberate)
- Fortran single precision became double precision. Decisions at the edge of the thresholds can differ from WSJT-X's.
- `mskrtd` is driven by the `decode_msk144.f90` block loop: blocks of 7168 samples every 3584 (0.2987 s), restarting
  at every 15 s period boundary (the WSJT-X T/R default; periods count from the first sample, `--offset` moves the
  first boundary). Blocks never cross a period end (49 blocks per period, as in file mode of WSJT-X).
  `hspec.f90`'s guard (skip the block when either half block is all zeros) and mskrtd's `rms<1` exit are kept.
- Defaults: Rx frequency 1500 Hz and tolerance 100 Hz (WSJT-X defaults `nrxfreq`, `ntol`). If `--fmin/--fmax` differ
  from the program defaults 200/4000, the centre is (fmin+fmax)/2 and the tolerance (fmax-fmin)/2.
- `--depth` is WSJT-X's `ndepth` for mskrtd: 1 short-ping decoder only, 2 plus two averaging patterns, 3 plus all four.
- SNR: mskrtd's estimate (`10 log10(pmax/pnoise-1)` clipped to -8..24) with the same slowly-rising/quickly-falling
  noise state. A non-positive argument of the log (NaN in Fortran) gives -8. Duplicate check: same text is only
  repeated when its SNR is higher, state reset at each period.
- Output fields: `dt` and `tstart` are WSJT-X's T column (time of the ping centre, seconds into the 15 s period) and
  the same on the stream's sample clock; `freq` is the estimated carrier (`fest`, Hz, not rounded).
  `note` says which decoder got it ("short-ping decoder" = msk144spd, "frame-averaging decoder").
- msk144spd: window bin indices are clamped to 2..863 (Fortran would run out of bounds for a very large tolerance).
- Hash/recent-call lists of packjt77 (used by `unpack77`) are the parent's `unpack77.cpp`; see its notes.
- Left out: MSK40 short messages (`msk40spd`, `msk40sync`, `msk40decodeframe`, `bshmsg`, `bswl`, `update_msk40_hasharray`),
  the equaliser training (`msk144signalquality`, `pcoeffs`, `btrain`), the OSD decoder `osd128_90.f90` (not called by
  `msk144decodeframe`), the OpenMP threads of `msk144sync` (one thread: same result), and all file/UDP/GUI output.

### Proof
- Independent check against WSJT-X itself: the two real off-air recordings that ship in WSJT-X `samples/MSK144`
  decode to `K1JT WA4CQG EM72` (120500) and `CQ W4IMD EM84`, `CQ KD9VV EN71` (120800). This verifies the CRC-13, the
  LDPC tables, the sync words, the frame structure and the unpack together; the generator alone could not (it shares
  the same CRC and tables). Pinned in `test/expected/msk144_real_*.jsonl`. I did not compare against a WSJT-X run of the
  same files (no WSJT-X binary here); the texts are the plausible operating messages.
- Generated signals (`msk144_gen`, a port of msk144sim: peak amplitude sqrt(2)*10^(snr/20), noise sigma sqrt(6000/2500),
  so SNR is in 2500 Hz at the PEAK of the ping envelope `2.718 t exp(-t)`, width 0.12 s; scaled by 30 into 16 bits).
- Thresholds, message `K1ABC W9XYZ EN37`, one ping 3 s into a 15 s file, seeds 1..20 (`decoded/20`, wrong-text 0
  everywhere): +6 20, +3 20, 0 20, -2 20, **-4 12**, -6 1, -8 0 dB. So the lowest SNR with every one of 20 seeds
  decoding is **-2 dB**, 60 % at -4 dB, the limit about -6 dB (not compared with a WSJT-X run).
- Continuous carrier (3 s, seeds 1..10): -2 10, -4 10, -6 10, -8 10, **-10 2** of 10; the -8 dB case is decoded by the
  frame-averaging path (pinned as `msk144_gen_cw_weak`; with `--depth 1` it is not decoded: `..._depth1`).
- False decodes: 30 minutes of pure noise (3 x 600 s, seeds 11, 12, 13): 0 lines. 60 s of noise is in the test suite.
- Break-it: `bpdecode128_90` call replaced by `nharderror=-1`: 15 of 17 MSK144 tests RED (the two "nothing decoded"
  cases stay green, as they should); restored, file touched, rebuilt: 17/17 green.
- Decode speed: a 15 s file takes about 0.8 s in Release on the development machine (real time is not an issue).
- Block-size independence: `msk144_real_2_chunk` (reads of 100 samples), `..._pipe` (777-byte writes) and `..._f32`.

### Test command
`ctest -C Release -R msk144` : `100% tests passed, 0 tests failed out of 17`.

---

# Appendix B: JT9 (notes written by the JT9 porting pass)



Source: WSJT-X official git (https://git.code.sf.net/p/wsjt/wsjtx), commit b4f9a431bcf6449df8f37b56de79d48b665b044c
(2025-02-04), licence GPL-3.0 (COPYING read). The same Fortran files are inside kosciej/wsjtx-ft2libre (GPL-3.0) and were
not used. No C or C++ port of the JT9 decoder was found (searches: see the report); MSHV (GPL-3.0, SourceForge) is a C++
re-write whose licence file was not opened, so the Fortran source was ported directly.

### Files taken (upstream lib/, line counts) and where they went
| upstream | lines | here |
|---|---|---|
| jt9_decode.f90 (decode: candidate loop) | 172 | src/jt9.cpp `decode_period` |
| symspec.f90 (half-symbol spectra only) | 126 | src/jt9.cpp `symspec` |
| sync9.f90 | 96 | src/jt9.cpp `sync9` |
| pctile.f90, shell.f90 | 21+27 | src/jt9.cpp `pctile` (std::sort, same element) |
| softsym.f90, downsam9.f90, peakdt9.f90, getlags.f90 | 54+88+54+27 | src/jt9.cpp `softsym`, `downsam9`, `peakdt9` (nsps8=864 branch only) |
| afc9.f90 (+ shft), fchisq.f90, twkfreq.f90 | 81+44+30 | src/jt9.cpp `afc9`, `shft`, `fchisq`, `twkfreq` |
| symspec2.f90, chkss2.f90 | 88+20 | src/jt9.cpp `symspec2` |
| jt9fano.f90, fano232.f90, conv232.f90 | 92+138+38 | src/jt9.cpp `jt9fano`, `fano232`; polynomials and parity in src/jt9_pack.cpp |
| interleave9.f90, jt9sync.f90 | 39+18 | src/jt9_pack.cpp |
| packjt.f90 (unpackmsg, unpackcall, unpackgrid, unpacktext, packmsg, packcall, packgrid, packtext, getpfx2, grid2k, nchar) | ~600 of 1033 | src/jt9_pack.cpp |
| pfx.f90 (339 prefixes, 12 suffixes), jt9fano.f90 metric table xx0 | 50 | src/jt9_tables.h (generated by a throwaway script) |
| gen9.f90, encode232.f90, entail.f90, graycode.f90, fmtmsg.f90, grid2deg.f90, deg2grid.f90 | 65+33+30+9+21+39+30 | src/jt9_pack.cpp |
| jt9sim.f90 (signal and noise scaling, phase-continuous 9-FSK) | 175 | tools/jt9_gen.cpp (TEST TOOL) |

Ported C++ lines: src/jt9.cpp about 660, src/jt9_pack.cpp about 580, src/jt9_pack.h 41, src/jt9_tables.h 100, tools/jt9_gen.cpp 124.

### What it does
JT9 sub-mode A only (one-minute T/R period, NSPS 6912, 85 symbols, 9 tones 1.736 Hz apart, 16 sync symbols on tone 0,
69 data symbols x 3 bits, Gray coded, K=32 r=1/2 convolutional code, 72-bit message, no CRC). Periods are counted from
the first sample (plus `--offset`); a period is decoded when its 720000 samples are in. At end of input a last period
holding at least 45 s is zero padded and decoded. Output per decode: freq (tone 0, as WSJT-X shows it), snr (WSJT-X's
estimate), dt (WSJT-X convention: seconds against 1.0 s into the period), tstart = period start + 1.0 + dt.

The decode path follows WSJT-X `jt9` reading a file: `symspec` (16384-point FFT at half-symbol steps, no window) ->
`sync9` -> candidate loop (`jt9_decode`) -> `softsym` (downsam9, peakdt9, afc9, twkfreq, symspec2) -> `jt9fano`.

### Changes against upstream (each one deliberate)
1. All of it is C++17 with doubles where the Fortran uses REAL*4; FFTs come from wm::fft (any length, Bluestein for the
   653184-point downsam9 FFT). Results can differ from WSJT-X in borderline cases; nothing here was compared against
   a WSJT-X binary (none was available), only against the signals of the generator.
2. Only the nqd = 0 pass of `jt9_decode` runs over the whole band (fmin..fmax). The nqd = 1 "quick" pass around the
   receive frequency (nfqso, looser thresholds) is not ported: a bridge has no receive-frequency concept.
3. ADDED pass 2 (depth 3 only): Fano limit 100000, ccflim 2.2, red2lim 1.2, sync >= 0.8, schk >= 1.2, run after pass 1 on
   bins that are not done. Chosen from the sweep below (a looser 2.0/0.8/0.5/1.0 set gained at most one decode in 20 and
   cost 3 s per period on noise). A message found again within 20 Hz of an earlier decode is dropped.
4. `afc9`: iteration cap of 2000 on the two "repeat until it changes / keeps falling" loops (the Fortran has none and
   would hang on flat data); a non-finite result abandons the candidate. `fchisq` has no cross-call cache of the
   twkfreq result (the Fortran `save`d `a1,a2,a3` could hand a stale `c4` to a new candidate when `a` is exactly
   0,0,0; this only removes that case).
5. dt: WSJT-X prints the coarse `peakdt9` value; afc9 also finds a time shift a(3) (samples of the 27.78 Hz series,
   0.036 s) which upstream computes and discards. It is added to dt here. Even so, dt of a noisy weak signal is only good
   to about +-0.2 s (upstream's own coarse resolution is 0.036 s but the peak is ambiguous when the candidate bin is
   0.2-0.4 Hz off the tone).
6. Data length given to the big FFT: 182 half symbols = 628992 samples (all the signal; what a live WSJT-X has when
   ss(181) is complete). `jt9` reading a file passes npts8 = 74736 (597888 samples), which cuts the last 0.14 s.
7. unpackmsg: `getpfx2` code 449 (the user's own "add-on prefix" setting in WSJT-X) is not available; such a call is left
   as is. Hashed calls do not exist in JT9 (72-bit format), so there is no "<...>" case.
8. packcall: a call token longer than 6 characters is sent as text (the Fortran silently cuts it); the generator
   refuses messages with "/" calls (JT65v2 add-on prefix/suffix packing: getpfx1, packpfx are not ported). The decoder
   side does unpack those message types (iv2 1..7, k of 1..900); that part is UNVERIFIED (no generator, no reference).
9. A message containing "000AAA " is dropped (as in jt9fano).
10. Not ported: sub-modes B..H (decode9w, sync9w, softsym9w, 5/10/30-minute periods, JT9+JT65 dual mode), the average
    spectrum and waterfall code in symspec (syellow, flat1, smo), the low-sidelobe window (jt9 file mode runs without it),
    the "again" (double click) decode, any timer/statistics code.

### Tests (CTest, test/jt9.cmake) and the generator
`tools/jt9_gen.cpp` writes a 60 s 12 kHz WAV from jt9sim.f90's recipe: noise sigma 1 per sample times 10^(25/20),
signal amplitude 10^(snr/20) (so snr is "jt9sim nominal"; jt9sim's own comment says 2500 Hz bandwidth but with sigma 1 at
12 kHz sampling the true 2500 Hz SNR is 0.8 dB higher than the nominal value). Noise is wm::Rng, not gran.c.
Cases: jt9_clean (-5 dB), jt9_noisy (-24 dB), jt9_freetext, jt9_report (RR73), jt9_two (two stations, CQ DX and R-12),
jt9_noise and jt9_noise2 (noise only: no output), jt9_offset / jt9_nooffset (7.25 s lead-in, with and without
`--offset`), jt9_partial (52 s file), jt9_short (30 s file: nothing), jt9_range_out (--fmin/--fmax excludes the signal),
jt9_chunk (100-sample reads), jt9_f32, jt9_twice. Pinned JSON in test/expected/jt9_*.jsonl (the hello line is part of
it: if main.cpp's hello changes run `WM_UPDATE=1 ctest ...` and review the diff).

Break-it check: `if (ncycles < ...)` in `jt9fano` changed to `if (false && ncycles < ...)`: 10 of 16 jt9 tests failed (every
case that expects a decode); restored (file touched), 16/16 pass.

### Threshold measured (test/jt9_sweep.sh, final code, message "CQ K1ABC FN42" at 1500 Hz, seeds 1..20)
| jt9sim nominal SNR (dB) | decoded | false decodes |
|---|---|---|
| -24 | 20/20 | 0 |
| -25 | 20/20 | 0 |
| -26 | 17/20 | 0 |
| -27 | 9/20 | 0 |
| -28 | 1/20 | 0 |
| -29 | 0/20 | 0 |

50% point is about -27 dB nominal (about -26.2 dB in true 2500 Hz bandwidth), lowest SNR with any decode -28. Before pass 2
(one pass only) the same sweep gave 20, 20, 16, 7, 0, 0. WSJT-X quotes -27 dB for JT9A. Noise only: 0 decodes in 40
one-minute files (seeds 201..240) and 0 in the 140 sweep files that did not decode. JT9 has no CRC, so a false decode
is possible in principle; none was seen. SNR estimates (`snr`) follow upstream's formula and read about 2 dB low for a
-20 dB signal; for strong signals (>= -10 dB) the estimate saturates and is not meaningful.
Decode time: about 0.5 s per minute of audio on noise, 0.3-1 s with a signal.
