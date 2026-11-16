# UPSTREAM: trunkfmt

* **Project:** sdrtrunk, by Dennis Sheirer and contributors. <https://github.com/DSheirer/sdrtrunk>
* **Commit taken:** `a53f6e9d992dcd94adc73a2591c3b4690a66a6b1` (2026-11-12, "#2628 Remove ThumbDV code support. (#2629)"),
  shallow clone, read locally on 2026-11-12.
* **Licence:** GNU General Public License, version 3 or (at your option) any later version. Upstream `LICENSE` is the GPL-3.0
  text (copied here as `LICENSE`, byte for byte); the file headers of the ported sources read "either version 3 of the
  License, or (at your option) any later version". This helper is therefore GPL-3.0-or-later.
* **Nature of the work:** the upstream is Java and no Java toolchain is installed, so the parts named below were PORTED
  to C++17 by hand (a derivative work under the upstream licence). Upstream class and method names are kept in comments
  next to each ported part. Nothing was compiled from, or linked against, upstream code; no binary was fetched.
* **Mixing:** nothing in this folder comes from any project other than sdrtrunk. The test generator and the glue code
  (`main.cpp`, JSON output, resampler, tests) are new; they are under the same licence as the rest of the folder.

## Files read (upstream paths under `src/main/java/io/github/dsheirer/`) and where they went

| trunkfmt file | ported from |
| --- | --- |
| `src/framer.h`, `src/framer.cpp` | `bits/MessageFramer.java`, `bits/SyncPatternMatcher.java`, `bits/SyncPattern.java` (LJ1200, LJ1200_TRANSPONDER, TAIT_CCDI_GPS_MESSAGE, TAIT_SELCAL_MESSAGE) |
| `src/common.h` | the parts of `bits/BinaryMessage.java` / `CorrectedBinaryMessage.java` the decoders call: `getInt(int[])`, `getInt(start,end)`, `toHexString()` |
| `src/afsk1200.h`, `src/afsk1200.cpp` | `dsp/afsk/AFSK1200Decoder.java` (incl. the inner `Correlator`), `dsp/afsk/AFSKSampleBuffer.java`, `dsp/afsk/AFSKTimingErrorDetector.java`, `buffer/FloatAveragingBuffer.java`, `dsp/oscillator/ScalarRealOscillator.java` + `AbstractOscillator.java` (reference tones) |
| `src/lj1200.h`, `src/lj1200.cpp` | `module/decode/lj1200/LJ1200Decoder.java`, `LJ1200MessageProcessor.java`, `LJ1200Message.java`, `LJ1200TransponderMessage.java`, `LJ1200DecoderState.java` (the "only valid messages are used" rule), `FunctionAndReplyCode*.java`; `edac/CRCLJ.java` |
| `src/tait1200.h`, `src/tait1200.cpp` | `module/decode/tait/Tait1200Decoder.java`, `Tait1200ANIMessage.java`, `Tait1200ANIMessageProcessor.java`, `Tait1200GPSMessage.java`, `Tait1200GPSMessageProcessor.java`, `identifier/TaitIdentifier.java` (trim) |
| `src/fleetsync2.h`, `src/fleetsync2.cpp` | `module/decode/fleetsync2/Fleetsync2Decoder.java`, `Fleetsync2MessageProcessor.java`, `Fleetsync2DecoderState.java` (valid-only rule), `FleetsyncMessageType.java`, `message/Fleetsync2Message.java`, `AutomaticNumberIdentification.java`, `Status.java`, `LocationReport.java`, `identifier/FleetsyncIdentifier.java` (`formatted()`); `edac/CRCFleetsync.java`; `bits/SyncPattern.java` FLEETSYNC2 |
| `src/mpt1327.h`, `src/mpt1327.cpp` | `module/decode/mpt1327/MPT1327Decoder.java`, `MPT1327MessageProcessor.java`, `Sync.java`, `MPT1327Message.java` (all of it: block layout, `getMessage()`, SDM binary / ASCII / BCD / telex, the inner enums); block check by `CRCFleetsync` as upstream; `bits/SyncPattern.java` MPT1327_CONTROL / _TRAFFIC and the French variants |
| `src/ltrdsp.h`, `src/ltrdsp.cpp` | `dsp/fsk/LTRDecoder.java` (slope slicer, timing adjust, excessive-ones reset) |
| `src/ltr.h`, `src/ltr.cpp` | `module/decode/ltrstandard/LTRStandardDecoder.java`, `LTRStandardMessageProcessor.java`, `message/LTRMessage.java`, `Call.java`, `CallEnd.java`, `Idle.java`, `UnknownMessage.java`, `identifier/talkgroup/LTRTalkgroup.java` (`formatted()`); `module/decode/ltrnet/LTRNetDecoder.java`, `LTRNetMessageProcessor.java`, `message/LtrNetMessage.java`, `LtrNetMessageFactory.java`, `osw/*` (all 15 classes), `isw/*` (all 7 classes), `LtrNetMessageType.java`; `edac/CRCLTR.java`; `bits/SyncPattern.java` LTR_STANDARD_OSW / _ISW |
| `src/passport.h`, `src/passport.cpp` | `module/decode/passport/PassportDecoder.java`, `PassportMessageProcessor.java` (with `IdleMessageFinder`), `PassportMessage.java`, `PassportBand.java`, `identifier/PassportRadioId.java` and `PassportTalkgroup.java`; `edac/CRCPassport.java`; `message/MessageType.java` names |
| `src/dmr_connectplus.h`, `src/dmr_connectplus.cpp` | `module/decode/dmr/message/data/csbk/motorola/ConnectPlus*.java` (all 11 message classes), `csbk/CSBKMessage.java`, `csbk/CSBKMessageFactory.java` and `csbk/Opcode.java` (the Connect Plus rows), `message/type/Vendor.java`, `message/data/lc/shorty/ConnectPlusControlChannel.java`, `ConnectPlusTrafficChannel.java`, `ShortLCMessage.java`, `LCOpcode.java` (rows 9, 10), `channel/DMRLsn.java`, `module/decode/ip/mototrbo/xcmp/XCMPMessageType.java`; `edac/CRCDMR.java` (`CCITT_80_CHECKSUMS`, `correctCCITT80`, `crc8`) |

Not read or not used: every other file of sdrtrunk. No sdrtrunk test files exist for these decoders (`src/test` has none for
LJ1200, Tait or Connect Plus), so the tests use signals generated by `tools/gen_signals.cpp`.

## Every change from upstream, and why

1. **Language and structure.** Java classes became C++ structs/functions; `BinaryMessage`/`BitSet` became `BitMsg` in
   `common.h`; the listener/broadcaster plumbing became `std::function` callbacks. Bit order, field index lists and
   arithmetic follow the Java exactly (the first index of an `int[]` field list is the most significant bit).
2. **Sample-rate converter replaced.** Upstream resamples 8000 Hz audio to 7200 Hz (6 samples per symbol) with
   `RealResampler`, which wraps libresample4j (a separate library, not used). `Resampler` in `afsk1200.cpp` is new code:
   an exact rational polyphase windowed-sinc (Blackman) interpolator, cut-off 0.9 x half of the lower of the two rates. It also
   lets the helper accept other input rates (4000..192000 Hz). Consequence: the symbol decisions are the same algorithm but
   not bit-identical to upstream's output at 8000 Hz (different filter); the decoder's correlation and majority logic is
   tolerant of that, but bit-exactness against upstream was NOT tested (no Java to run it).
3. **Audio is pushed one sample at a time** instead of in buffers, so the timestamp of each message is exact and
   independent of how the pipe is chunked (tested: 777-byte chunks give identical output). `AFSK1200Decoder.Decoder.receive`
   loops over samples and keeps all state per sample, so this is behaviourally the same.
4. **Tait GPS longitude sign.** Upstream `Tait1200GPSMessage.getGPSLocation()` has `longitude = -1;` when the sign bits are 0,
   which discards the whole value. It is plainly a typo for `longitude *= -1;`; this port uses `*= -1`.
5. **Tait GPS time.** Upstream builds a `Calendar` from the current wall-clock year and the MONTH/day/time fields. A helper must
   be deterministic, so only the time of day (`gps_time`), the day-of-month field (`day`) and the raw 4-bit field at bits
   388..391 (`field_388_391`; upstream says "not sure if this field is the month field or the speed field", and indexes it both
   as month and as speed hundreds) are reported. The speed is computed as upstream does, so that same field is its hundreds
   digit.
6. **Tait GPS latitude seconds.** Upstream lists `LATITUDE_SECONDS_TENS = {340, 341, 342, 344}`, bit 344 also being the first bit of
   `LATITUDE_SECONDS_ONES = {344..347}`. It looks like a typo for 343 but cannot be checked without a recording, so it is KEPT as
   upstream has it (see UNVERIFIED below).
7. **Short LC vendor test.** Upstream `ShortLCMessage.getVendor()` treats bits 4..11 as a vendor id whenever bit 0 is set and the
   opcode is not 10. For Connect Plus Short LC opcode 9 (traffic channel info) those same bits are the high byte of the 12-bit
   NETWORK number (opcode 9 = 1001, bit 0 set), so a network such as 100 (0x064: "vendor" 6) makes upstream look the opcode up
   under the wrong vendor and not find it. This port decodes Short LC opcodes 9 and 10 whatever those bits contain. No other
   Short LC opcode is defined at 9 or 10 for any vendor in `LCOpcode`.
8. **Output.** Messages are printed as JSON Lines (upstream builds display strings and `DecodeEvent` objects). Fields upstream
   shows only in `toString()` are separate JSON fields. Identifiers for talkgroup alias lookup, `DMRLsn` frequency
   assignment (`ITimeslotFrequencyReceiver`), channel/GUI objects and logging are left out.
9. **Only sdrtrunk's *use* rules were kept:** an LJ1200 tower message is reported only if its CRC passed or was repaired
   (`LJ1200DecoderState.receive`); Tait and LJ transponder messages are always reported because upstream has no CRC for them.
   `--no-unchecked` suppresses those three types. Failed LJ1200 CRCs are only counted (`lj1200_failed_crc`).
10. **CSBK input.** The helper's `csbk-hex` mode takes the CSBK bytes as text; sdrtrunk's DMR framing, BPTC, slot-type and CACH
    processing that produce the 12 bytes are not part of this helper (the FoxSDR DMR plugin supplies them).

11. **LTR slicer low-pass filter.** `LTRDecoder` low-passes the 8 kHz audio with a Remez-designed FIR (`RemezFIRFilterDesigner`,
    pass band to 300 Hz, stop band from 500 Hz). That designer is not ported; `ltrdsp.cpp` uses a 145-tap Kaiser-window
    (beta 5.65) sinc at 400 Hz instead. The slicer's timing logic is a razor-thin margin (it skips 26 samples after each
    transition for a 26.67-sample bit) and depends on the filter's step response, so this is a real behavioural difference
    that was NOT compared with upstream. Measured on synthetic words (300 baud, rectangular NRZ, 8000 Hz): 300/300 outbound
    words decode, about 94 to 95 % of inbound (ISW) words; one inbound LTR-Net word type (`ISW_UNIQUE_ID`, channel 24) never decoded in
    the generated test signals, so that class is covered by code review only. Other filter lengths (47 to 145 taps) gave 81 to 95 % on the inbound words
    and the same 100 % outbound. Input at rates other than 8000 Hz goes through the `Resampler` first.
12. **LTR Net frequency pairing and ESN halves.** `LTRNetMessageProcessor` pairs the high and low frequency halves per LCN: kept
    (state per channel). Its ESN pairing never stores the other half, so the missing half always prints `xxxx`: kept.
13. **Passport frequencies.** Frequencies are 0 until `IdleMessageFinder` has seen the same idle word's text four times
    (the fourth is the reference); kept. Output carries `lcn_freq_hz`/`free_freq_hz` as upstream computes them.
14. **Sub-audible family only on request.** `ltr`, `ltrnet` and `passport` are off unless named in `--decoders`, because they need
    the sub-audible part of the discriminator output and read the same sync words (Passport and LTR outbound share the 9-bit
    sync, `0x158`), so running them on ordinary 1200 baud audio would only create noise.
15. **MPT-1327 / Fleetsync II / LTR / Passport time stamps.** Upstream messages carry `System.currentTimeMillis()`; here `t` is
    the input sample time. Fleetsync II GPS time is reported as the raw hour / minute / second, year-field, month and day
    values (upstream builds a `Calendar` from them with `calendar.add()`, relative to the current date).

## Observations about upstream behaviour (kept, not fixed)

* **MPT-1327 `getSiteID()`** returns `null` for every type except BCAST and ALH, and `getMessage()` appends it with
  `StringBuilder.append`, which prints the text `null` (visible in the `text` of ACKT, ACKQ, ACKX and the ALH variants other than
  ALH).
* **MPT-1327 short data messages**: the BCD decoder's last digit reads `getInt(320, 232)` (end before start), which is always 0.
  The HEAD ident2 field list differs from the address-codeword one, so the `ident2` of a HEAD differs from the generator's input
  by design of the field lists, not by a decoding error.
* **Fleetsync II** location reports: latitude is always north and longitude always west (TODO upstream); the year field list
  repeats bit 284 from the century field; the longitude fractional-minutes field overlaps the CRC bits of its block ("TODO: this
  overlaps with the CRC block address space"). The identity arithmetic `(fleet + 99) << 12` and `ident + 999` is upstream's.
* **Passport CRC repair** (`CRCPassport.correct`) flips bits 65..67 for a fixed list of residual values only.

* **DMR CSBK single-bit repair.** `CRCDMR.correctCCITT80` accepts a residual of 0 or 0xFFFF as "passed" but looks a one-bit
  error up as `residual == table[k]` only. A CSBK whose CRC field is the inverted form (which this port's generator produces,
  following the standard) has residual 0xFFFF XOR table[k] for a single bit error, which is not in the table, so such an error is
  reported `crc: error` rather than corrected. Whether real captures use the inverted or the plain form was not checked; the
  behaviour is upstream's and is pinned by the tests (`csbk_hex.jsonl`, the "one bit wrong" line).
* **Tait and LJ transponder sync only.** There is no length or CRC validation: any appearance of the 20-bit (Tait) or 16-bit (LJ
  transponder) sync pattern in a noisy symbol stream yields a message. Expect occasional false events on noise.

## Searches for existing implementations

The task named the upstream (sdrtrunk) directly and every decoder in this helper exists in it (listed in the table above), so
no "missing" or "blocked" claim is made and no code-host search was needed. Nothing was fetched except the sdrtrunk
repository clone (`git clone --depth 1`, source files only).

## UNVERIFIED

* No real recording of LoJack, Tait CCDI, Fleetsync II, MPT-1327, LTR, Passport or Connect Plus was available
  (the 1200 baud AFSK recordings found were Bell 202 packet radio, 1200/2200 Hz): all
  proof is on signals generated from upstream's own constants. The Tait field layout, the 20-bit syncs, the REVS fields and the
  Tait GPS digit encoding are upstream's own reading of the protocol (upstream marks them with TODOs) and may not match real
  radios; the Tait sync patterns in sdrtrunk are, per its comments, "a truncated portion of the NRZ-I encoded sync pattern".
* The helper was never compared with the Java decoder's output on the same audio.
* The LTR / LTR-Net / Passport slicer runs on a replacement low-pass filter (change 11); inbound (ISW) words decode less reliably
  than outbound ones on synthetic signals, and `ISW_UNIQUE_ID` never decoded in the test signals.
* The CRC tables of LTR and Passport are given only as tables upstream, so the generator uses the same tables: those two tests
  check framing, slicing and message handling, not the polynomials. LJ1200 (poly 0x6F63), Fleetsync II and MPT-1327 (CRC-15 poly
  0x6815, init 1, even parity) and DMR CSBK (CCITT, inverted, mask 0xA5A5) were generated by polynomial arithmetic and agree with
  the upstream tables.
