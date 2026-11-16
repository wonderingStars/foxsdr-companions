# UPSTREAM.md - where trunkctl comes from

trunkctl decodes two trunking control channel formats. Each decoder is ported from exactly ONE upstream project;
no code of the other upstream is inside it (the two never share a function). Licence of the whole helper:
**GPL-3.0-only** (see `LICENSE`, the GPL v3 text from `gh api licenses/gpl-3.0`).

| Protocol | Upstream (the one taken) | Commit | Licence (file read) |
|---|---|---|---|
| Motorola SmartNet / SmartZone Type II | trunk-recorder, https://github.com/TrunkRecorder/trunk-recorder (version 5.2.0). It carries OP25's `rx_smartnet.cc` in `lib/op25_repeater/lib/` for the framing and has its own C++ `smartnet_parser.cc` for the signalling-word meanings | `1aac86c4885949ffa18c013c5936836c98cd90ce` (2026-09-26) | `LICENSE` = GNU GPL version 3. `rx_smartnet.cc` header: "GNU General Public License ... version 3, or (at your option) any later version" (OP25, copyright 2020 Graham J. Norbury, "inspiration and code fragments from mottrunk.txt and gr-smartnet"). `smartnet_parser.cc` has no file header: the repository licence, GPL-3.0, applies |
| EDACS (standard, networked, extended addressing, ESK) | edacs-fm by lwvmobile, https://github.com/lwvmobile/edacs-fm (archived) | `d92422ea8d13619150b23da3e3e4690ec349b868` (2022-12-27) | `LICENSE` = GNU GPL version 3. `edacs-fm.c` has no licence sentence in its header; it credits "portions originally from" sp5wwp/ledacs (licence checked: Unlicense, `gh api repos/sp5wwp/ledacs --jq .license`) and Robert Morelos-Zaragoza's BCH program (see below: NOT taken) |

Strictest licence among the two: GPL-3.0 (the `rx_smartnet.cc` "or later" grant is not needed; the other files have
no grant of "or later"). Both are GPL-3.0 and compatible with each other, so the helper is GPL-3.0-only.

## Choice of upstream, and the search that led to it
`reconcile-licences.md` item 1 said SmartNet exists only in OP25 / trunk-recorder (GPL) and EDACS only in edacs-fm
(GPL). Searches run on 2026-11-12 (WebSearch; hits opened or cloned where they matter):
- `site:github.com smartnet motorola type II control channel decoder 3600 baud OSW` -> gr-smartnet (boatbod fork of
  lokkju/gr-smartnet), robotastic/smartnet-recorder (inactive, "check out Trunk Recorder"), GopherTrunk.
- `site:github.com gr-smartnet` -> bistromath/gr-smartnet (cloned, `COPYING` = GPL-3.0, commit 5d45380, 2012; it is
  the origin of the OP25 code; not used: older, GNU Radio 3.6 blocks), boatbod/gr-smartnet, robotastic/smartnet-scanner.
- `site:gitlab.com OR site:codeberg.org OR site:sourceforge.net smartnet type II trunking decoder OSW` -> only
  SourceForge "opentrunker" (Linux control channel decoder, no source opened: not GPL-3 confirmed, not needed),
  nothing on GitLab / Codeberg.
- `site:github.com EDACS control channel decoder 9600 baud BCH` and `site:github.com edacs "extended addressing"
  decoder trunk 9600 control channel` -> lwvmobile/edacs-fm (taken), lwvmobile/ledacs-esk and sp5wwp/ledacs (older
  ESK-only trackers, edacs-fm's ancestors), lb5je/dsd and szechyjs/dsd (voice decoders; no control channel BCH).
- Not usable: GopherTrunk (Apache-2.0 label, but its SmartNet and EDACS files say "ported from OP25 / trunk-recorder
  / edacs-fm", so they are GPL-derived; see reconcile-licences.md).

Why trunk-recorder and not OP25 itself or sdrtrunk: trunk-recorder is C++ and has BOTH halves in C++ (framer from
OP25 and the OSW parser, which in OP25 is Python `tk_smartnet.py`); a C or C++ port was wanted. The OSW semantics are
those of OP25's Python parser as ported by trunk-recorder (its comments quote "line 850 / 861 ... from Python
implementation"). Why edacs-fm: the only maintained open EDACS control-channel decoder in C.

## Files taken (read and ported; no upstream file is copied byte for byte)
| Upstream file | What was taken | Where it is now |
|---|---|---|
| `rx_smartnet.cc` (244 lines) `rx_sym`, `deinterleave`, `error_correction`, `crc_check`, `cbuf_insert` | the whole framing / FEC / CRC chain, line for line | `src/smartnet.cpp` `SmartnetFramer` (about 110 lines) |
| `rx_smartnet.h`, `frame_sync_magics.h` | constants: 84-symbol frame, 76 payload, 27 data + 10 CRC bits, ID/command XOR masks, sync 0xAC | top of `src/smartnet.cpp` |
| `smartnet_parser.cc` (1108 lines) `process_osws`, `get_freq`, `is_chan`, `get_bandplan_details`, `is_patch_group`, `is_multiselect_group`, the individual extended-function table | the OSW decision tree and the 800 / 900 / OBT band plans | `src/smartnet.cpp` `SmartnetParser`, `BandPlan` (about 600 lines) |
| `edacs-fm.c` (1609 lines): `SYNC_FRAME`, the frame layout (48-bit sync + six 40-bit messages), field positions for extended addressing and standard / networked, command bytes 0xEE 0xEF 0xFD 0xF3, ESK mask 0xA0, AFS split, LCN status names | protocol constants and layouts only (read, not copied) | `src/edacs.cpp` (310 lines, own structure) |
| `bch3.h` | **nothing**. Its header says the program is "free for non-commercial purposes" and needs the author's written permission for commercial use, which is not GPL-compatible. Only the code parameters edacs-fm documents (GF(2^6), t = 2, length 40, dimension 28, systematic, message first then 12 parity bits) were used | `src/bch4028.cpp` is written from textbook BCH theory (Lin and Costello) |

## Changes made to the upstream logic, and why
1. No GNU Radio, Boost.Log, nlohmann/json or `System` / `TrunkMessage` objects: those are unavailable here and
   not needed. Output is written by `src/emitter.cpp`. The frame / FEC / CRC code is otherwise unchanged.
2. **Queue handling** (`SmartnetParser::process`). Upstream pops from an empty `std::deque` in several branches
   (undefined behaviour) and relies on a "queue reset" OSW that `rx_smartnet.cc` never sends. Here the queue is
   peeked (never popped blindly); where OSWs are missing, the decoder puts a gap marker in the queue, and a
   multi-OSW message that would need to read across a gap is dropped (counted `unknown`) instead of being
   mis-assembled. The idle-OSW re-ordering rules (put back osw0 / osw2) are kept, written as "delete the idle from the
   middle of the queue".
3. **Timers on the sample clock** (`sync_timer` is wall clock upstream) so that the output is deterministic.
4. **Control channel of the two-OSW system-id message with `(osw1.addr & 0xfc00) == 0x2800`**: upstream reads the
   frequency of `osw0`, which belongs to the NEXT message; the channel is in the low ten bits of `osw1` (the
   three-OSW form checks exactly this equality). Here the channel comes from `osw1`. (Judgement call: UNVERIFIED on air.)
5. `is_chan` on a rebanded 800 MHz plan: upstream's `else if (chan <= 0x2cf)` also accepts channels 0x230-0x2cf,
   which have no frequency on that plan; here they are not channels.
6. Repeats: the control channel repeats voice grants and updates. `--update-interval` (default 2 s) reports a call
   (talkgroup + channel) at most once per interval; upstream reports every one to its tracker.
7. The OSW fields upstream only logs (private calls, interconnect, the ext-function table) are reported as events
   (`grant` with `call: private`, `msg` with a `name`). The "interconnect call" branch upstream is unreachable (same
   condition as the private call branch before it) and is not ported.
8. **Added, not from upstream:** the symbol slicer (`src/slicer.cpp`: integrate-and-dump at four timing phases, tracked
   high / low levels, both polarities decoded in parallel) because neither upstream has a timing recovery that fits an
   arbitrary sample rate (edacs-fm averages three samples at a fixed 28800 Hz; trunk-recorder uses GNU Radio blocks);
   BCH **error correction** for EDACS (edacs-fm only checks); best-of-three copies of each EDACS message; quality
   arbitration between timing phases; the lock / lost status lines; I/Q input (quadrature discriminator); `--hide-ids`.

## Not ported (and why)
- trunk-recorder state: talkgroup / voice-frequency tables, patch tracking and expiry, adjacent-site tables, JSON
  status for the web page, recorder control, talkgroup CSV (a follower in FoxSDR keeps its own state from the events).
- SmartNet "international" and "shuffled" 800 MHz plans: upstream returns no frequency for them either (channels still
  decode, `freq_hz` is absent). 900 MHz and 400 MHz (OBT) are ported.
- edacs-fm: ncurses screen, rtl_fm / UDP tuner control, CSV site and group files, call logging, the "ESK on/off
  detection" heuristic (its own comment: "experimenting with values"; use `--esk`), the Python `PyEDACS` hooks.
- Voice (ProVoice, analog audio), data channels, encryption keys: not offered.

## Cross-checks run against upstream code
- `rx_smartnet.cc` `deinterleave` + `error_correction` + `crc_check` (the upstream functions, extracted verbatim into a
  scratch program outside this repository) decode frames made by `trunkgen`'s encoder: 20000 of 20000 clean frames and
  20000 of 20000 frames with one flipped data bit give the right address, group flag and command. So the generator's
  SmartNet constants agree with upstream.
- EDACS: the BCH(40,28) generator polynomial computed here is the textbook BCH(63,51) one (octal 12471, 0x1539), unit
  tested. A run of edacs-fm's `bch3.h` against this encoder was attempted and refused by the permission layer of this
  session (it would have executed a non-commercial-licence upstream file); it was NOT done. **UNVERIFIED:** that
  edacs-fm's bit order for the parity field equals this code's. It was derived by reading `BCH()` in `bch3.h`
  (message bit i = coefficient of x^i, parity in the 12 low bits, transmitted MSB first).

## UNVERIFIED (no real recording of either protocol was available)
Everything above is proven on generated signals only. Real-world open points: whether copies two and three of each
EDACS message are plain repeats (edacs-fm uses only copies one and four); the meaning of the "mode" bit for OBT
grants (`osw2.grp`: analog or digital) and of the adjacent / alternate control channel flag; the EDACS standard
status nibble and 5-bit LCN overlap in the extended-addressing layout (edacs-fm's own field table is used as is);
discriminator polarity and level on real hardware (handled by auto polarity).
