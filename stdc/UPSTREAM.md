# UPSTREAM: where the stdc helper comes from

| | |
|---|---|
| Project | `inmarsatc`, "C++ library with functions to receive Inmarsat-C signals" |
| URL | https://github.com/cropinghigh/inmarsatc |
| Commit taken | `cda1242e79981d71cd8608e971c8dbc691942b10` (2023-07-10, "memory/initialization issues fix"), shallow clone, read locally |
| Licence | GNU GPL version 3. Confirmed from the repository's `LICENSE` file (the GPL-3.0 text, copied here unchanged as `LICENSE`) and its README ("Original code license: GNU General Public License 3, microp11 2017"). The source files carry no per-file notice and no "or later" clause, so the helper is licensed as **GPL-3.0 (version 3)**; nothing here says "or any later version". |
| Lineage (from the upstream README) | Mostly based on Scytale-C (https://bitbucket.org/scytalec/scytalec/, Copyright microp11 2017, GPL-3), itself based on Tekmanoid's Java application; the Viterbi decoder is after Phil Karn KA9Q (Copyright 1995, quoted in `inmarsatc_decoder.h`). Those notices are kept in the comments of the copied files. |
| Also read (not copied) | https://github.com/cropinghigh/stdcdec commit `4b4ef4a1a6f1` (GPL-3): only to see how upstream feeds its demodulator (48 kHz mono audio, each sample handed over as the complex number (v, v)), which `TOLERANCE` it passes the decoder (9), and that its parser program prints only packets whose CRC is valid. No code was taken from it. |
| Other upstream code mixed in | None. |

## Files taken

Copied as they were into `upstream/` (the pristine reference, line endings normalised to LF) and into `src/` (the working copy):
`inmarsatc_demodulator.h/.cpp` (184 + 432 lines), `inmarsatc_decoder.h/.cpp` (373 + 472), `inmarsatc_parser.h/.cpp` (146 + 1189).
2796 lines in all. Not taken: upstream's `CMakeLists.txt` (shared libraries; replaced by one static library), `README.md` (kept as `upstream/README.md`).
New files (written for the helper, GPL-3): `src/stdc_main.cpp`, `src/stdc_privacy.h`, `test/*`, `CMakeLists.txt`.

`patches/inmarsatc-foxsdr.diff` is the complete `diff -u` of `upstream/` against `src/`. Every change in it is marked `FOXSDR:` in the source.

## Changes to the upstream code, and why

Memory safety and determinism (the helper must give the same output for the same input, and must survive damaged frames):
1. **Export macros removed** (`INMARSATC_EXPORT` is empty): the three libraries are one static library. `ulong`/`long` in the Viterbi `State` became `uint64_t`/`int64_t` (they are 64-bit on upstream's Linux build, 32-bit under MSVC); `M_PI` defined for MSVC.
2. **Uninitialised memory zeroed**: FIR taps and history (`new double[]()`), the Gardner/CMA/demodulator members, the demodulator `symbolBuffer`, the UW finder's symbol register, the decoder's output frame, the Viterbi result. Upstream read these before writing them.
3. **FIR constructor copied `len-1` taps** into the `len`-tap array (the last tap, and for the 2-tap loop filters the second tap, was undefined). All taps are copied now. `FIR::filter()` computed a whole variable-length array to return its last element; it computes that element directly (same arithmetic).
4. **Off-by-one copies that left one element uninitialised**: the 5000-symbol result of the demodulator (first symbol of each chunk), the frame returned by the UW finder for an "uncertain" frame (last symbol), and the 640-byte decoded frame (last byte). All copy the full length.
5. **Overlapping `std::copy` used as a shift** (demodulator symbol buffer, UW finder register) replaced by `memmove`.
6. **Wall-clock stamp** `system_clock::now()` in the descrambler removed (different clock type under MSVC; never used; not deterministic).
7. **Parser bounds**: `decoder_result.decodedFrame` is 640 bytes plus 1024 bytes of zero padding, because the parser reads fixed offsets and CRC-checked lengths past byte 640 on damaged frames; a packet that does not fit in the frame, or is shorter than 3 bytes, is never CRC-valid (upstream read the CRC from before the frame for length 1); station lists stop at the end of their packet (255 announced stations read 1500 bytes); the BD handler clamps the first part to the inner packet length (heap overflow upstream for inner length 0); the variable-length array in the EGC address copy is a fixed array.
8. **CRC "zero" exception**: upstream accepted any packet whose CRC field was `00 00` (a workaround for packets reassembled from BD/BE, whose CRC bytes are never received). It now applies only to the packet reassembled from a BD/BE chain (`allowZeroCrc`, reported as `"reassembled":true`). Without this, zero-filled tails accept garbage packets. A BD/BE inside a reassembled packet is not followed (it could recurse for ever).
9. **BE reassembly advanced the frame position by the inner packet's length** (`ret` had been replaced before `*pos += ret.packetLength`), so packets after a completed multiframe chain were read from the wrong place. It advances by the BE packet's own length.

Bugs in what the parser reports (found by reading it against its own neighbouring code; **not verified against the Inmarsat specification, which is not available here**):

10. `getLesName`: case labels written with a leading zero (`001`, `044`, `012`, `021` ...) are octal in C++ (`044` is 36), so the NCS (id 44) and several LES were never named. Now decimal. (The real recording now shows `"144, NCS"` for the NCS of AOR-E.)
11. `decode_7D`: the `channelType` switch had no `break`, so every channel type was named "Reserved". Breaks added.
12. `decode_81`: the third byte of the terminal id was read from `[pos+3]` (the middle byte again) instead of `[pos+4]`; the downlink channel added `15305` where every other packet adds `1530.5`.
13. `decode_81`, `decode_92`, `decode_AB`: a single `ostringstream` was reused with `os.clear()` (which does not empty it), so later hex fields repeated earlier ones. Each field is now just its own bytes.
14. `getStations`: the downlink channel used byte `j+4` for both halves; it uses `j+4`, `j+5`.

Format of what is reported: upstream's `packetVars` was a `map<string,string>` of text. It is an ordered list of ready-made JSON values here (numbers, booleans, arrays and objects instead of text blobs: `services`, `status`, `tdmSlots`, `stations`). Field names are upstream's.

Privacy changes (broadcast and signalling only; for a message to one terminal report type, length and addressing, never the text):

15. `decode_A3` (individual poll) and `decode_A8` (confirmation) no longer extract `shortMessage` (nor A3's `unknown1Hex`, which can contain message bytes); only `shortMessageChars` / `shortMessageLength` (sizes) remain.
16. `decode_AA` (message) no longer copies its data bytes into the payload; `payloadBytes` is reported.
17. `decode_B1` / `decode_B2` (EGC) and `decode_BE` no longer copy the broadcast text into the payload; EGC text is decoded by FoxSDR's own EGC plugin. Header fields and `payloadBytes` are reported. (B1 and B2 shared 40 identical lines; one function now.)
18. A second, independent barrier in `src/stdc_privacy.h`: only keys listed there for a descriptor are ever printed; terminal identifiers (`mesId`, the 3-byte id in a login acknowledgement) become run-local pseudonyms with `--hide-ids`.

Not done: decryption (nothing here decrypts anything).

## Written for the helper (not upstream code)

`src/stdc_main.cpp`: the pipe protocol (hello line, events, `--foxsdr-helper-info`), formats s16 / f32 (audio), cs16 / cf32 (I/Q shifted up by `--iq-shift`, real part used as audio) and sym (demodulated symbols), fixed 4800-sample blocks (the demodulator normalises by the mean magnitude of each call, so the block size is part of the result), a `stats` line every 30 s of input and at the end. `t` is the input time at which a 5000-symbol chunk finished (steps of 4.17 s), not the time of the frame.

## Searches

Whether anything implements the five descriptors upstream marks "not implemented yet" (0x91 Distress Alert Ack, 0x9A Enhanced Data Report Ack, 0xA0 Distress Test Request, 0xAC Request Status, 0xAD Test Result), run 2026-11-12:
- `site:github.com Inmarsat-C "Distress Alert Ack" OR "Enhanced Data Report" OR "Request Status" packet decoder 0x91 0x9A`: no hit names these; leads: InmarScope (open source, AERO and STD-C), the SDRangel `demodinmarsat` plugin.
- `site:github.com scytalec Inmarsat-C decoder packet descriptor 0xAC 0xAD "Test Result"`: Scytale-C is on Bitbucket (not GitHub); cropinghigh/inmarsatc is the C++ port.
- Opened: https://github.com/f4exb/sdrangel/blob/master/plugins/channelrx/demodinmarsat/readme.md : lists a Packets table and a Type column but names none of these packet types.

**UNVERIFIED**: the SDRangel plugin source, InmarScope's source and Scytale-C's Bitbucket source were not opened, so whether any of them decodes the five is not established. Not followed up.
