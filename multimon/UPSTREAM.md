# UPSTREAM: where the multimon helper comes from

| | |
|---|---|
| Project | multimon-ng (successor of Thomas Sailer's multimon) |
| URL | https://github.com/EliasOenal/multimon-ng |
| Commit taken | `b5f7d71c5e9b5a76093ba55c515968dc40f92804` (2026-10-18, "Fix FLEX_NEXT group-call capcode buffer overflow (#273) (#274)"), version string 1.6.2 |
| How fetched | `git clone --depth 1`, source files only; no binary, installer or script was fetched or run |
| Licence | GNU GPL version 2 or (at your option) any later version. `LICENSE` is upstream's `COPYING` (GPL-2.0 text) unchanged. Every demodulator header says "either version 2 of the License, or (at your option) any later version" |
| Licence of this helper | GPL-2.0-or-later (`GPL-2.0-or-later` in the hello line and the manifest) |

Other licences inside the upstream files (all compatible with the GPL; every header is kept intact):
public domain: `bch.c`, `bch.h`, `gen_flex.c`, `gen_pocsag.c`, `demod_morse.c`, `demod_x10.c`.
MIT: `cJSON.c`, `cJSON.h` (Dave Gamble). LGPL-2.1-or-later: `win32_getopt.c` / `.h` (only linked into the test tool
`gen-ng`; the helper itself includes only the header through `msvc_support.h`). `costabf.c`, `costabi.c` and
`gen_scope.c` carry no header of their own and fall under the repository's GPL-2.0 `COPYING`.
No code from any second project is mixed in.

## Files taken (verbatim, in `upstream/`; 23 660 lines)
Helper program: `multimon.h msvc_support.h filter.h filter-i386.h bch.c bch.h cJSON.c cJSON.h costabf.c costabi.c
pocsag.c selcall.c hdlc.c uart.c clip.c fms.c demod_poc5.c demod_poc12.c demod_poc24.c demod_flex.c
demod_flex_next.c demod_gsc.c demod_eas.c demod_ufsk12.c demod_clipfsk.c demod_fmsfsk.c demod_afsk12.c
demod_afsk24.c demod_afsk24_2.c demod_afsk24_3.c demod_hapn48.c demod_fsk96.c demod_dtmf.c demod_dualtone.c
demod_zvei1.c demod_zvei2.c demod_zvei3.c demod_dzvei.c demod_pzvei.c demod_eea.c demod_eia.c demod_ccir.c
demod_morse.c demod_x10.c`.
Test tool only (`gen-ng`, not part of the helper): `gen.c gen.h gen_clipfsk.c gen_dtmf.c gen_flex.c gen_hdlc.c
gen_pocsag.c gen_scope.c gen_sin.c gen_uart.c gen_zvei.c win32_getopt.c win32_getopt.h`.
Test data: `FOXSDR_SAMPLES_DIR/` (not included in this repository): upstream's sample recordings, converted to raw 16-bit where they were FLAC.

Not taken, on purpose: `unixinput.c` (replaced, see below), `win32_soundin.c`, `macos_audio.m`, `xdisplay.c`,
`demod_display.c`, `demod_sdl_scope.c` (audio devices, X11 and SDL scope windows), `demod_dumpcsv.c` (a debug dump),
`bch_stub.c`, `mkcostab.c`, `unsupported/`, `demo/`, upstream's CMake files and test scripts.
Demodulators carried: POCSAG512 POCSAG1200 POCSAG2400, FLEX, FLEX_NEXT, GSC, EAS, UFSK1200, CLIPFSK, FMSFSK,
AFSK1200, AFSK2400, AFSK2400_2, AFSK2400_3, HAPN4800, FSK9600, DTMF, DUALTONE, ZVEI1, ZVEI2, ZVEI3, DZVEI, PZVEI,
EEA, EIA, CCIR, MORSE_CW, X10 (28). Upstream's list also has DUMPCSV, SCOPE and SDL_SCOPE: not carried.

## Changes to upstream files (1)
`upstream/demod_x10.c`: `printbits()` and `x10_report()` wrote the decoded frame to **stderr** with `fprintf(stderr, ...)`,
`fputc` and `fputs`; changed to `verbprintf(0, ...)` so the frame is a normal output line (the helper turns it into an
event). Nine lines; the exact diff is `patches/demod_x10.patch`. The debug prints elsewhere in that file stay on stderr.
Every other file in `upstream/` is byte-identical to the commit above (checked with `diff --strip-trailing-cr`).

## Code written for the helper (2 307 lines, GPL-2.0-or-later)
- `src/helper_main.c`: replaces upstream's `unixinput.c` (kept in its header: derived from it). Takes from it the demodulator
  table, the option letters (`-a -s -c -v -A -j -e -u -p -P -f -b -C -o -d -g -x -y`), the read / overlap / `process_buffer`
  loop, and the two stubs `addJsonTimestamp`, `printJson`. Removed: audio devices, sox / fork reader, file arguments, the console
  banner, `--timestamp`, `--iso8601`, `--label`, `--json`, `-t`, `-n`, `-m`, `-r`, `-i`. Added: the FoxSDR protocol options, `s16`
  / `f32` input, a private copy of stdout for the JSON stream (fd 1 itself is pointed at stderr so a stray `printf` cannot
  break it), `SetErrorMode`, binary stdin, `TZ=UTC0`, the virtual clock, grouping of printed lines into events, `--chunk`.
  `json_mode` stays 0 and `verbprintf` levels above 0 go to stderr (only with `-v`).
- `src/helper_events.c/.h`: parses the lines the demodulators print into JSON fields; JSON string escaping with UTF-8 checking.
- `src/helper_compat.h`: force-included (`/FI`) into `demod_flex.c` and `demod_flex_next.c` only. (1) `time(NULL)` becomes a clock
  derived from the sample count, so those decoders' time stamps do not depend on the wall clock or the PC's zone; (2)
  `__builtin_popcount` for MSVC (`demod_flex_next.c` calls it unconditionally; upstream's own build uses GCC / MinGW).
- `CMakeLists.txt`: MSVC build. `demod_flex_next.c` is compiled with `/std:clatest` because it uses `typeof()`; the
  other files build in plain C11. Defines: `ONLY_RAW DUMMY_AUDIO NO_X11 NO_SDL3 CHARSET_UTF8 MAX_VERBOSE_LEVEL=3 _USE_MATH_DEFINES`.
- `test/`: `run_case.cmake`, `CMakeLists.txt`, `tools/sigtool.c` (signal and file utility incl. SAME and CW generators),
  `tools/compat/getopt.h` (one-line shim so upstream's `gen.c` builds with MSVC).

## Libraries not used
None needed: multimon-ng's input path uses only the C runtime. (sox is what upstream uses to read FLAC / WAV; the
helper reads raw samples only. `ffmpeg` was used once, outside the build, to turn the sample FLAC files into raw.)

## Searches recorded
Question: does multimon-ng, or a GitHub project built on it, decode the Skyper "rubric" (the service / category number in
Skyper pages) in addition to the ROT-1 text?
- `grep -rni rubric` over the multimon-ng commit: no match anywhere (source, README, man page).
- WebSearch `site:github.com skyper pocsag rubric decoder`: hits were multimon-ng itself (Skyper = ROT-1 text, `-f skyper`),
  the old multimon 1.0 `print_msg_skyper`, PDW, F4JTV/pager_decoder, 02bwilson/POCSAG-Decoder; none opened showed rubric handling
  (only the search-result snippets were read, the pages were not opened).
- WebSearch `site:github.com multimon-ng skyper rubric`: nothing relevant.
Result: this helper decodes Skyper messages as upstream does (POCSAG with `-f skyper`, text one character lower, kind `skyper`).
A rubric field is **not** produced; the rubric is carried inside the Skyper message payload and would need a separate parser
(not in this helper). Not exhaustively searched beyond the two queries above.
