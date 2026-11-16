# Upstream: hamdrm

* Project: **QSSTV** by Johan Maes ON4QZ - https://github.com/ON4QZ/QSSTV ("receiving and transmitting of SSTV and HAMDRM (DSSTV)").
  Found with `site:github.com QSSTV HamDRM digital SSTV source GPL` (hits: ON4QZ/QSSTV, forks xdsopl/qsstv, KD9QZO/qsstv, tomtastic/QSSTV); the
  GitHub licence API reports `GPL-3.0` for ON4QZ/QSSTV and its `LICENSE` is the GPL version 3 text (copied here as `LICENSE`).
* Commit taken: `31c323ccb848bade1dbab0a96890073f7b9455ea` (2026-11-01, "Merge pull request #79 from SpudGunMan/main"); read from a local clone.
* Licence of the files taken (checked in each header): the HamDRM receiver (`drmrx/`) is **RXAMADRM by M. Bos PA0MBO** ("GNU General Public Licence", no
  version stated) with parts by Torsten Schorr (GPL v2 **or later**) taken from *diorama 1.1.1* (A. Dittrich and T. Schorr, GPL); `sourcedecoder.cpp`, `filters`
  are by Johan Maes (GPL v2 **or later**); `rs.cpp` is Phil Karn KA9Q's Reed-Solomon codec ("may be used under the terms of the GNU public license").
  None is "GPL-2.0 only", so the helper is **GPL-3.0-or-later** (matching QSSTV's `LICENSE`). QSSTV's `COPYING` asks that a program using
  part or all of the code says it is based in whole or in part on QSSTV: it says so in `README.md`, in `main.cpp` and in `hamdrm --version`.
  `COPYING` (QSSTV's, unchanged) is kept next to `LICENSE`.
* **Single upstream, and why QSSTV and not Dream** (GPL-2.0, ancestor of QSSTV's transmit modem): QSSTV has everything HamDRM-specific in code - the 2.3 / 2.5 kHz
  occupancy tables (`K_min_K_max_list` in `drmrx/drm.cpp`), robustness mode E, the receive chain written for it (RXAMADRM), the MOT file layer with the
  EasyPal-compatible Reed-Solomon `.rs1-.rs4` files, and a transmitter to test against. I did not open Dream's source to compare (the research row said
  "HamDRM support in Dream unverified"), so "Dream cannot do it" is **not** claimed. Mixing two upstreams was avoided.

## Taken (verbatim copies in `upstream/qsstv/`, working copies in `src/`; line counts are the diff against the verbatim copy)
Receive chain, `src/drmrx/` -> `src/`: `bits2bytes`, `channeldecode` (unchanged), `crc16_bytewise` (2), `crc8_c` (2), `deinterleaver`, `demodulator.cpp/.h` (15 + 8),
`drm.cpp/.h` (4 + 11), `drmdefs.h`, `drmproto.h` (5), `filter1`, `filter1c`, `getfoffsint`, `getmode`, `getofdm`, `getofdmsync`, `getsymbolidx`, `mkfacmap`, `mkmap.h`,
`mkmscmap`, `msd_hard.h`, `msd_hard_sdc.h`, `msdhardfac`, `msdhardmsc`, `resamplefilter.h`, `structtemplates.h`, `viterbi_decode.cpp/.h`;
`src/utils/rs.cpp/.h` (6). The 153 Hilbert-filter coefficients of `src/dsp/filterparam.cpp` (`src/drm_hilbert_coef.inc`, unchanged numbers).
Rewritten from QSSTV code without Qt (same logic and function names in comments): `sourcedecoder.h/.cpp` (<- `drmrx/sourcedecoder.*`), `drmrx.h/.cpp` (<- `drmrx/drmrx.*`),
`rs_file.h/.cpp` (<- `utils/reedsolomoncoder.*`), `drm_hilbert.h` (<- `dsp/filters.cpp`, `dsp/filter.cpp`: `drmHilbertFilter`, `filter::processIQ`).
About 9800 lines in `src/` in total; roughly 5000 are the unchanged receive chain.
Test tool: `tools/txgen/` = QSSTV's HamDRM **transmit** chain (`drmtx/`, Dream-derived), ported the same way, with its own `CHANGES.md` and `README.md`; verbatim copies in `upstream/qsstv-tx/`.

## Written for the helper (NOT from QSSTV; GPL-3.0-or-later)
`main.cpp` (protocol, events), `fftw3.h` (a small stand-in for the FFTW calls QSSTV makes: FFTW is not installed and is itself GPL-2+; only the public API and
data layouts are followed, any length, tested against a direct DFT in `test/fft_shim_test.cpp`), `lu_solve.h` (see below), `sha256.h`, and the stubs `appglobal.h`,
`appdefs.h`, `supportfunctions.h`, `configparams.h`, `test/*`.

## Deliberately NOT taken (licence)
* `nrutil.c`, `ludcmp.c`, `lubksb.c` (in QSSTV's `drmrx/`): routines from the book *Numerical Recipes in C*, carrying no licence notice in QSSTV; that code is not GPL.
  Replaced by `src/lu_solve.h`, a fresh textbook LU decomposition with partial pivoting and substitution with the same 1-based call signatures
  (tested in `test/unit_test.cpp`; unlike the original a singular matrix does not call `exit()`). Numerical results can differ from the original in the last float bits.
* `newfft.cpp` (MiXViews, Regents of the University of California, "permission ... for research and/or educational purposes"): not GPL compatible. It only fed `psdmean.cpp` / `psdcmean.cpp`,
  which compute a power-spectrum display (`psd`, `cpsd`) that nothing in the decoder reads; all three are dropped together with the two calls in `demodulator::demodulate()`.
  (Removing them also removes upstream's copy loop that could write past `tempbuf[30000]`.)

## Changes to the taken code, and why
1. `drm.h`, `drm.cpp`: `QString drmCallsign` -> `std::string`, `QList<ushort> drmBlockList` -> `std::vector<unsigned short>`; Qt includes removed; `drm.h` includes `appglobal.h` for `uint`.
2. `demodulator.cpp`: `#include <QDebug>` and `nrutil.h` -> `lu_solve.h`; local `ludcmp`/`lubksb` prototypes removed; the psd block removed (above). `demodulator.h`: the four `isXxx()` accessors are `const`.
3. `drmproto.h`: declarations of `ivector/fvector/matrix/psdmean/psdcmean` removed (now in `lu_solve.h` or gone).
4. `rs.cpp`: four local arrays declared with the run-time size `[NN-KK+1]` (a GCC variable-length-array extension) are sized `[NN+1]` (MSVC has no VLAs); same values.
5. `crc8_c.cpp`, `crc16_bytewise.cpp`: array parameters `double x[]` written as `double *x` - MSVC mangles the two differently and the link failed; no code change.
6. All `addToLog(...)` debug calls compile to nothing (`appglobal.h` macro); Qt events to the GUI (`displayDRMStatEvent`, ...) in `drmRx::run` are gone; `smoothed SNR` (`avgSNR`) is kept.
7. `sourcedecoder` (rewrite): Qt types -> `std::vector`/`std::string`; **bounds checks added on every field read** from the received packet; packet length above `PACKETBUFFERLEN` is dropped
   (upstream only logs it and overruns the buffer); a file larger than 16 MiB or needing more than 32 MiB of segments is not assembled; `segmentNumber` is reset per packet; the padding indicator is bounded;
   a finished transport block no longer keeps accepting and clearing segments; GUI hand-offs replaced by the `RxEvent` callback; file names are reduced to a plain, safe name before writing (`safeName`).
8. `rs_file` (port of `reedSolomonCoder`): the erasure-position table is allocated large enough (upstream allocates `segmentLength*(totalSegments+1)` but reads `255*bep_size` entries);
   the "surplus" byte is read unsigned (upstream reads a `char`, wrong for values over 127); the extension stored inside the coded data is limited to letters and digits; sizes are validated.
9. `bsr.bin` transfers (repair requests meant for a transmitter) are reported as `status` and not saved; "hybrid" names (`.de`, `de_` prefix: the picture is on an FTP server) are reported as `file` with `note:"hybrid"` and not fetched.

## Left out (and why)
Transmit (except the test generator), sound card and 48 kHz decimation (the helper takes 12 kHz audio; QSSTV's own 4:1 decimator `downsamplefilter` is not ported - the generator uses its coefficient table to
make the test audio), user interface, image decoding / display (JPEG, JPEG 2000 via openjpeg: the file is saved as received), hybrid FTP download, BSR/FIX repair requests, log book, rig control, the spectrum/waterfall display.
No network code anywhere.

## Proven and not proven
Proven (tests, all run locally): the receiver decodes the generator's output in robustness modes A, B and E, both bandwidths, QAM 4/16/64, both protection levels and both interleavers, a
multi-segment binary, a Reed-Solomon `.rs2` file, a signal with added noise and one with a 3 s drop-out (second transmission recovers it); a cut signal gives segments but no file; noise and garbage give
no file and no crash; the file-name sanitiser; output is byte-identical on a second run. Address sanitiser build: clean on decode, noise, drop-out, cut, garbage.
**Not proven: any signal from a real transmitter or from real QSSTV/EasyPal.** The generator is QSSTV's own transmit chain, so a shared misreading of the HamDRM standard would not be caught; real-air behaviour
(fading, drift, frequency offset, sound-card sample-rate error) is untested. No HamDRM recording was available (its only SSTV sample is analog Martin M1), so none was used.

## Build recipe
`cmake -S . -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Release && ctest --test-dir build -C Release`. MSVC 2022, C++17, no third-party library (`-DHAMDRM_ASAN=ON` builds the helper with the MSVC address sanitiser).
