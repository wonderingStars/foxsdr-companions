# UPSTREAM.md -- fldigimodes

## Project
- **fldigi** (Dave Freese W1HKJ and others), https://github.com/w1hkj/fldigi
- Commit taken: `61b97f4133c488063f3de1795c894d22d5032e8a`, "Version 4.1.23", dated 2022-06-23 (the head of `master` of the
  GitHub repository on 2026-11-12). Shallow clone, sparse checkout of `src/`,
  `COPYING`, `AUTHORS` (a full checkout fails on Windows: some upstream file names are not valid there). **Not
  checked:** whether the project's own repository (SourceForge) is newer than this GitHub head.
- Licence: **GNU GPL version 3** (`COPYING`, the full text, is `LICENSE` here). The GitHub page of the project
  (https://github.com/w1hkj/fldigi, read 2026-11-16) shows "GPL-3.0 license" and lists `COPYING`; fldigi's README says nothing
  about licences. The fldigi-written files carry the notice "either version 3 of the License, or (at your option) any later
  version", but this helper also contains files that carry no such notice, so the helper is declared **GPL-3.0-only**
  (the version that every part of the tree is distributed under) and not "or later":
  - `include/jalocha/pj_*.h` (Pawel Jalocha's Olivia/MFSK engine, December 2004) carry no licence text at all. fldigi
    distributes them inside its tree under its `COPYING` (GPL version 3); upstream states no file-level licence, no
    "or later" grant, and no separate licence for them. Neither the fldigi page nor the repository says more.
  - `include/dsp.h`, `mt63/dsp.cxx`, `mt63/mt63base.cxx` (Jalocha's MT63 engine, as modified by W1HKJ) carry the
    fldigi GPL-3.0-or-later notice with "Copyright (C) 1999-2004 Pawel Jalocha, SP9VRC".
- Every upstream copyright and licence header is intact.

## Files taken (all under `upstream/`, same relative paths as fldigi's `src/`)
Unchanged: `include/jalocha/pj_{cmpx,fft,fht,fifo,gray,lowpass3,mfsk,struc}.h`, `include/{complex,dominovar,dsp,fftfilt,
filters,gfft,interleave,mbuffer,mfskvaricode,misc,mt63base,thor,thorvaricode,viterbi}.h`, `filters/{filters,viterbi,
fftfilt}.cxx`, `mfsk/mfskvaricode.cxx`, `misc/misc.cxx`, `mt63/{dsp,mt63base}.cxx` with `mt63/{symbol,mt63intl}.dat`,
`rsid/rsid_defs.cxx`, `thor/{dominovar,thorvaricode}.cxx` (`dominovar.cxx` is `src/dominoex/dominovar.cxx`), `AUTHORS`.
Changed (diffs in `patches/`): `include/mfsk.h`, `mfsk/mfsk.cxx`, `mfsk/interleave.cxx`, `thor/thor.cxx`.
About 20,700 lines in all (3,392 of them the generated FFT header `gfft.h`).

## Changes to upstream files, and why
All marked `FOXSDR_HELPER` in the source. `patches/*.diff` are the exact diffs against the commit above.
1. `include/mfsk.h`: FLTK headers, the picture-viewer declarations and the `friend` list removed. (No window.)
2. `mfsk/mfsk.cxx`:
   - `#include "mfsk-pic.cxx"` removed (the picture windows and the picture transmitter; not taken); the constructor
     no longer creates the viewer windows, the destructor no longer hides them; `setpicture_link` and `txSPP` replaced by
     fldigi's default of 8 samples per pixel.
   - Picture **receive** keeps fldigi's state machine (the picture header in the text, the start delay, the pixel count)
     so that the audio of a picture is not decoded as text, but the pixels go nowhere; the helper prints one `status`
     event ("a WxH colour/grey picture is being sent; the pixels are skipped") through `fm_picture_skipped()`.
   - The transmitter (`tx_init`, `tx_process`, `sendsymbol` ... to the end of the file) is compiled only with
     `-DFOXSDR_TEST_TX`, which only the test tool sets; the helper has stubs (`tx_init`, `tx_process`, `send_*_image`).
     Inside the transmitter the XML-RPC characters-per-second test, the picture transmit state and `send_*_image` are
     reduced (they use the viewer windows).
   - Two variable-length arrays (`b[symbits]`, `symbols[symbits]`; `symbits` is at most 5) became fixed arrays of 32:
     MSVC has no VLAs.
3. `mfsk/interleave.cxx`: one variable-length array (`syms[size]`) became `syms[64]` (size is at most 32).
4. `thor/thor.cxx`:
   - `#include "thor-pic.cxx"` removed; its three static definitions (`imageheader`, `avatarheader`, `IMAGEspp`) kept in
     place; `activate_thor_image_item()` calls removed. Picture receive: as for MFSK (state machine kept, pixels
     dropped, one `status` event). The THOR secondary-text channel (`put_sec_char`) is dropped.
   - Transmitter compiled only with `-DFOXSDR_TEST_TX` (stubs otherwise); the picture and avatar transmitters are cut out
     of the test build too; two variable-length arrays (`black[symlen]`) became `std::vector`.

## Written for this helper (not fldigi code, same licence)
- `shim/`: fldigi's program environment in miniature. `modem.h` is a reduced `class modem` (the member names and meanings
  are fldigi's; its sound card, rig, waterfall and GUI parts are not taken). `fm_shim.h` and the one-line headers beside
  it (`config.h`, `fl_digi.h`, `configuration.h`, `status.h`, `FL/Fl.H` ...) give `progdefaults`, `progStatus`, `REQ(...)`,
  `put_rx_char`, `wf` ... a definition that does nothing, or hands the decoded character to the helper. The defaults in
  `fm_progdefaults` / `fm_progstatus` are fldigi's own (`configuration.h`, `status.cxx`): squelch on at 5.0, THOR soft
  symbols and soft bits on, THOR filter on, `slowcpu` on, MT63 8-bit on, short MT63 integration.
- `shim/fm_modes.h`: the `trx_mode` enumeration, copied from `src/include/globals.h` lines 48-263 (so that the
  unchanged `rsid_defs.cxx` compiles).
- `src/fm_runtime.cpp`: the definitions behind the shim; `modem::update_quality()` / `get_quality()` are copied from
  `src/trx/modem.cxx`.
- `src/helper_main.cpp`, `modes.cpp`, `factory.cpp`, `decoder.h`: command line, protocol, JSON, text coalescing.
- `src/mfsk_decoder.cpp`, `thor_decoder.cpp`: set the fldigi class up and read its state (SNR, metric).
- Ported from fldigi (the upstream function names are in the comments):
  - `src/olivia_decoder.cpp` (about 140 lines): the receive half of `olivia::restart`, `olivia::rx_process`,
    `olivia::unescape` (`src/olivia/olivia.cxx`) and `contestia::restart`, `rx_process`, `unescape`
    (`src/contestia/contestia.cxx`), driving Jalocha's `MFSK_Receiver` (unchanged). Olivia/Contestia use fldigi's own
    carrier formulas (including its integer truncation of the tone offset) and its squelch-to-sync-threshold rule.
    fldigi's `rx_flush` is not called at the end of the input (it only prints noise there).
  - `src/mt63_decoder.cpp` (about 110 lines): the receive half of `mt63::rx_init`, `mt63::rx_process`
    (`src/mt63/mt63.cxx`) driving `MT63rx`, `dspLevelMonitor` and `double_buff` (unchanged).
  - `src/rsid_detector.cpp` (about 600 lines): the receive side of `cRsId` (`src/rsid/rsid.cxx`, `src/include/rsid.h`):
    the tables `Squares` and `indices`, `Encode`, `CalculateBuckets`, `search`, `search_amp`, `HammingDistance`, `receive`
    and the frequency arithmetic of `apply`, with `rsid_defs.cxx` included unchanged exactly as `rsid.cxx` includes it.
    Not taken: `setup_mode`, the rest of `apply` (it switched fldigi's modem), the transmitter (`send`, `assigned`).
    The hit is reported instead of acted on; fldigi's per-user list of enabled RSID modes is not applied (every
    entry is reported, with `known:false` where fldigi has no modem for it).
  - `src/fm_mode_names.inc`: three columns (id, short name, long name) of `mode_info[]` in `src/globals/globals.cxx`,
    extracted by a script; one row, `MODE_PACKET`, whose id is commented out in fldigi's own enum, is dropped.
- `test/`: the test tool `sigtool` and its generator `gen_modes.cpp` (which contain the **transmit** halves of
  `olivia::tx_process`, `contestia::tx_process`, `mt63::tx_process` and `cRsId::send`, and call the `mfsk` and `thor`
  classes' own transmitters), `run_case.cmake`, `CMakeLists.txt`, `tools/sweep.py`.

## Outside libraries
- **libsamplerate** (used by fldigi's RSID code to bring the audio to 11025 Hz): not installed and not needed: a small
  streaming windowed-sinc resampler (Hann window, 16 zero crossings, cut-off 0.94 of the lower Nyquist) is in
  `rsid_detector.cpp`, class `Resampler`. fldigi's `sample_converter` default is "fastest sinc".
- **FLTK, pthread wrappers, libgen, unistd, the picture and rig code**: not taken; empty or no-op headers in `shim/`.
- **FFTW**: fldigi does not use it for these modems (its own `gfft.h` and Jalocha's FFT are in the tree).
- Searches for something "missing": none were needed.

## Left out (on purpose)
Every transmit path in the helper; pictures and avatars (binary payloads are not in protocol version 1); THOR secondary
text; fldigi's squelch/AFC/waterfall GUI controls; MT63 and Olivia start/stop tones (receive does not need them); RSID
transmit; all fldigi modes that are not Olivia, Contestia, MFSK, MT63, THOR (PSK, RTTY, DominoEX ... are not in this
port). `thor-pic.cxx`, `mfsk-pic.cxx`, `olivia.cxx`, `contestia.cxx`, `mt63.cxx`, `rsid.cxx` and their headers are
not copied: their receive logic is in the ported files above.
