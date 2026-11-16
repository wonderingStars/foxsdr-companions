# sondes: upstream, licence and changes

## Upstream
- Project: **rs1729/RS** ("Wetterballon-Radiosonden 400-406 MHz"), author zilog80 and contributors.
- URL: https://github.com/rs1729/RS
- Commit taken: `89b1ea7f3371d99c630f6734c42014d88e667853` (2026-07-31, "DFM (dfm09mod): switch to T4 ..."), shallow clone read at
  read locally.
- Licence: **GNU GPL version 3**, from the repository's `LICENSE` file (identical to the FSF text, `gh api licenses/gpl-3.0`;
  GitHub reports `GPL-3.0`). The source files carry **no per-file licence headers and no "or later" wording**, so this helper
  declares `GPL-3.0-only` (the conservative reading). `LICENSE` in this folder is the upstream file.
- Nothing from any other project is mixed in. The only other code is the helper's own glue and test tool (`src/`, `test/`),
  which is released under the same GPL-3.0.

How the upstream was found. rs1729/RS was chosen and confirmed from
the repository itself (`gh api repos/rs1729/RS`: licence `gpl-3.0`, directories `lms6`, `mk2a`, `mrz`, `rs92`, `c34`, `cn`,
`weathex`, `dropsonde`, `demod/mod`), and by web searches on 2026-11-12: `site:github.com LMS6 radiosonde decoder Sippican 403 MHz
rs1729` and `site:github.com Sippican LMS6-1680 Mk2a radiosonde decoder`. Hits: rs1729/RS (LMS6-403 in `lms6/`, LMS6-1680 /
Mk IIa in `mk2a/`), projecthorus/radiosonde_auto_rx (a user of rs1729's decoders, its JSON wiki maps LMS6 to `lms6Xmod` and
LMS6-1680 to `mk2mod`), MrARM/lms6 (firmware research, no decoder). No other LMS6 decoder repository was found. The
auto_rx project was not read (not needed; its licence was not checked).

## Files taken (unchanged copies were committed first as `5bf6482`; `git diff 5bf6482 HEAD -- sondes/upstream` shows every change)
From `demod/mod/` (the "cross-correlation header sync" decoders, which also print the auto_rx JSON lines):
`demod_mod.c`, `demod_mod.h` (shared FM-audio bit demodulator), `bch_ecc_mod.c`, `bch_ecc_mod.h` (Reed-Solomon / BCH),
`version_jsn.h`, `lms6Xmod.c` (LMS6-403 and LMS-X), `mp3h1mod.c` (MRZ-N1 / MP3-H1), `mts01mod.c` (MTS01),
`cf06ht03mod.c` (CF-06-AH, HT03G / GTH3), `rs92mod.c` and `nav_gps_vel.c` (RS92). From `mk2a/`: `mk2a1680mod.c` (LMS6-1680 / Mk IIa).
About 13,800 lines of upstream C in all. Test data: `FOXSDR_SAMPLES_DIR/Mk2a_9600.bytes`, copied from `mk2a/Mk2a_9600.bytes` (real recorded
Mk IIa subframes, hex text).

## What the helper adds (own code, GPL-3.0)
- `src/helper_main.cpp`: the pipe protocol, one thread per decoder, the barrier that makes output order independent of thread timing.
- `src/sondes_glue.h`, `sondes_glue.cpp`, `sondes_stream.h`: see "Changes" below.
- `test/gen.cpp` (`sondes-gen`): test signal generator and checker; `test/*.cmake`: CTest cases.

## Changes to the upstream code (all of them)
The decoders are standalone programs with their own `main()`; the helper runs several in one process on the same audio.
1. **Compiled as C++20** (`/Zc:strictStrings-`), because MSVC has no C99 `float complex`. `sondes_glue.h`, force-included
   (`/FI`) before every upstream file, supplies a C-compatible complex type (`fcplx`/`dcplx` with C99's float/double mixing),
   `I`, `cexp creal cabs carg conj`, malloc/calloc/realloc without casts.
2. `float complex` -> `fcplx`, `double complex` -> `dcplx`, `#include <complex.h>` removed: `demod_mod.c`, `demod_mod.h`, `mk2a1680mod.c`.
3. **Input**: `read_wav_header()` (in `demod_mod.c` and `mk2a1680mod.c`) no longer parses a WAV file; it returns the helper's rate,
   16 bit, mono (`sondes_decoder_rate()`). `fread`/`fgetc`/`fclose`/`stdin` are macros (glue header) that read the helper's
   per-decoder sample queue; `printf`/`fprintf(stdout, ...)` are captured line by line and the decoders' own `--json` lines become events
   (the text lines are dropped). `fprintf(stderr, ...)` is unchanged.
4. `main` of each decoder is renamed by the compiler (`-Dmain=lms6_main` etc., `CMakeLists.txt`); `rs92mod.c`: `char *argv[]` -> `char **argv`
   so that the name mangles alike. `mk2a1680mod.c` carries its own copy of the demodulator, so its functions and types are renamed
   (`-Dread_wav_header=mk2a_read_wav_header ...`, listed in `CMakeLists.txt`).
5. Statics shared between threads made `thread_local`: `IQdc` and `ws_dec` in `demod_mod.c` and `mk2a1680mod.c` (IQ paths, unused by the
   audio helper). Every other file-scope variable in the used files is constant or per decoder.
6. C constructs C++ rejects: `mp3h1mod.c` (string literal followed by a macro without a space, lines 689-690), `mts01mod.c` (`char*` cast in a
   `strncpy`), `cf06ht03mod.c` (designated initialiser mixed with positional ones, line 96), `rs92mod.c` (`strncmp` cast),
   `nav_gps_vel.c` (variable-length arrays replaced by arrays of 12, `SONDES_NMAX`; `calc_DOPn()` returns -1 above 12 satellites; the
   five `matrix_invert(B, BBB / Binv)` calls copy through a 4x4 temporary because the arrays are no longer exactly 4 wide),
   `demod_mod.c` / `mk2a1680mod.c` (`f32read_cblock()`: variable-length array -> `_alloca`, IQ path only).
Nothing was removed from the decoding code, the sync correlation or the error correction.

Options the helper gives each decoder (see `helper_main.cpp`): `lms6 --json`; `mk2a --json --crc --lpFM`; `mrz --json --ptu --auto`;
`mts01 --json`; `cf06 --json --auto --ecc2`; `rs92 --json --crc --ecc [-e ephemeris | -a almanac]`. `--json` is the upstream option for the auto_rx
JSON line; for lms6 it also switches on Viterbi and Reed-Solomon.

## Not built, and why
- **c34/c50** (`c34/c34dft.c`, `c50dft.c`), **RD94 / RD41 dropsondes** (`dropsonde/`), **Weathex WxR-301** (`weathex/`), **MTS01 variants and
  CF-06 / HT03 `unk600bd`** (`cn/`): single-file older programs with no `demod/mod` version and no JSON output; not ported in the time allowed.
  Not a licence or patent reason; they are in the same repository and could be added the same way.
- **M10, M20, DFM, iMet, Meisei, RS41**: not covered here.
- **I/Q input**: the upstream's IQ modes (`--IQ`, `--iq0` ...) are not wired; the helper takes FM-demodulated audio only.
- **RS92 is built but UNVERIFIED**: it prints nothing until a GPS ephemeris (RINEX navigation file) or almanac is supplied, then it needs
  real RS92 audio; no RS92 recording was available and a synthetic RS92 frame needs pseudoranges consistent with the ephemeris, which the
  generator does not make. The decoder loads the repository's `brdc2910.15n` and almanac without complaint on non-RS92 audio, nothing more is claimed.
- No network code, no decryption. The helper reads a file only for `--rs92-ephemeris` / `--rs92-almanac`.
