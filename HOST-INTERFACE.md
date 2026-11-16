# Host interface of the FoxSDR GPL helpers

This file describes how a program starts and talks to each helper. It is built from the helper manifests (`<folder>/<folder>.helper.json`), the READMEs and UPSTREAM.md of each folder, and the `--help` output of the built executables. Where a source does not say, this file says "not stated"; nothing here is a guess. If a row and a folder's README disagree, the README wins and this file is wrong.

The pipe protocol is `docs/PIPE-PROTOCOL.md` (version 1): `helper.exe --foxsdr-helper 1 --format <fmt> --rate <Hz> [options]`, samples on stdin until end of file, one JSON object per line on stdout (the hello line first, then events), log text on stderr, exit 0 at end of input. `helper.exe --foxsdr-helper-info` prints the manifest. A bridge must ignore event types and fields it does not know. `tools/conformance.py` checks these points for every helper.

Notes that apply to every row:

- **Folder / exe**: the folder in this repository and the executable file the manifest names (the CMake target name may differ). All are 64-bit Windows console programs that open no window.
- **Licence** is what the manifest and the folder's UPSTREAM.md say: `-only` means the code carries no "or later" grant, `-or-later` means it does.
- **Formats**: `s16` = mono little-endian 16-bit, `f32` = mono float (-1..1), `cs16` / `cf32` = interleaved I,Q little-endian, `cu8` / `cs8` = interleaved unsigned / signed 8-bit I,Q. Text and symbol formats are named in the row.
- **Proof level**: *real recording* = a recording of a real transmission decodes; *upstream samples* = the upstream project's own sample files decode (whether they are over-the-air is the upstream's statement); *generated only* = only signals made by a generator written from the same understanding of the standard. Real recordings are kept outside the repository and are never committed.

## 1. One row per helper

| Folder / exe | Upstream (project, commit, URL) | Licence | Input formats and sample rates | Arguments a caller must (or may) pass | `--hide-ids` | Attribution the host must show | Proof level |
|---|---|---|---|---|---|---|---|
| `multimon/` `multimon-helper.exe` (v1.6.2) | multimon-ng 1.6.2, commit b5f7d71c5e9b, https://github.com/EliasOenal/multimon-ng | GPL-2.0-or-later | audio: `s16`, `f32`; rate 22050 Hz only (other rates refused, no resampler) | `--rate 22050`; `-a <demod>` once per wanted demodulator (default: all 28 run; UFSK1200 prints junk lines on noise); `--start-time <unix s>` only if FLEX time stamps are wanted (default 0; without it FLEX_NEXT text carries 1970-01-01) | no | not stated | upstream samples (real POCSAG, GSC, FLEX_NEXT, UFSK1200 and X10 recordings from the multimon-ng repository, which has no separate licence statement for them) + generated; real I/Q traffic is never pinned |
| `hfmodes/` `hfmodes.exe` (v0.1.0) | OnAir, commit e118396b25d5, https://github.com/adswill/OnAir (+ Rivet, commit 41f81b236db2, https://github.com/IanWraith/Rivet) | GPL-3.0-or-later + OnAir additional terms (GPL section 7(b), `hfmodes/ADDITIONAL-TERMS.md`) | audio: `s16`, `f32`; rate 8000 to 192000 Hz; I/Q refused | `--rate`; `--modes a,b,...` (default: every decoder runs, about 1.3 x real time at 44.1 kHz); `--crowd36-baud N` optional | no | **must show** in the legal notices that show this helper's licence: `Based on OnAir by Kristijonas Adomaitis - https://on-air.dev` | generated only, except SITOR-B (one real off-air recording, local, optional test `real_sitorb_tfc`); OnAir itself validated its decoders on simulated signals only |
| `trunkfmt/` `trunkfmt.exe` (v0.1.0) | sdrtrunk, commit a53f6e9d992d, https://github.com/DSheirer/sdrtrunk | GPL-3.0-or-later | audio: `s16`, `f32` (FM-demodulated audio, mono), rate 4000 to 192000 Hz, 8000 recommended; text: `--format csbk-hex` (lines `csbk <24 hex digits>` or `slc <11 hex digits>`; no rate) | `--rate 8000`; `--decoders lj1200,tait,fleetsync2,mpt1327` (default: those four; LTR, LTR-Net and Passport only when named: `ltr`, `ltrnet`, `passport`); `--no-unchecked` drops lj1200_transponder, tait_gps and tait_ani, which have no CRC upstream (a host may want it as the default); `--invert`, `--mpt-french` optional | no | not stated | generated only (no real recording; never compared with the Java decoder) |
| `atn/` `atn.exe` (v1.0.0) | dumpvdl2 2.7.0, commit 686e87f2294e, https://github.com/szpajder/dumpvdl2 (+ libacars 2.2.1, MIT; asn1c runtime, BSD-2-Clause) | GPL-3.0-or-later | text: `--format avlc-hex`, one de-stuffed AVLC frame per line as hex (optional leading decimal timestamp); no audio or I/Q; `--rate` and `--centre` accepted and ignored | `--format avlc-hex`; `--fcs check` (default: frames end with the 2-octet FCS) or `--fcs none` (the host's demodulator already verified it); the host needs its own VDL2 demodulator and handles ACARS itself (ACARS comes out as `unknown_proto`) | no | not stated | generated only (frames assembled from the standards; nothing decoded from a real over-the-air frame) |
| `rtl433/` `rtl433-helper.exe` (v25.12.0) | rtl_433 25.12+0c85f7ad3566, https://github.com/merbanan/rtl_433 | GPL-2.0-or-later | iq: `cu8`, `cs8`, `cs16`, `cf32`; rate 50000 to 4000000 Hz (250000 native) | `--rate`; `--centre <Hz>` (default 433920000; reported only, not tuned); `-R <n>` enables one protocol, `-R -<n>` disables one (329 of 384 decoders are on by default) | yes: `--hide-ids` (identifiers become salted hashes `hNNNNNNNN`, raw payload fields dropped; `--hide-ids-salt <s>` fixes the salt, default random per run) | not stated | generated only in the committed tests; a one-off local cross-check against four real recordings from rtl_433_tests matched (stated in UPSTREAM.md, not reproducible from this repository) |
| `packet/` `packet-helper.exe` (v1.0.1) | Dire Wolf 1.8.2, commit eda1383f5fa9, https://github.com/wb2osz/direwolf | GPL-2.0-or-later | audio: `s16`, `f32`; rate 8000 to 192000 Hz (manifest default 48000; 9600 baud needs at least 19200) | `--rate`; `--baud 300|1200|9600` (default 1200); `--profile`, `--fix-bits 0..4`, `--sanity ax25|aprs|none` optional | no | not stated | real recording for AFSK 1200 / AX.25 only (WA8LMF TNC Test CD, 180 s, local, counts pinned); 300 baud, 9600 baud, FX.25 and IL2P generated only |
| `js8/` `js8-helper.exe` (v1.0.0) | JS8Call, commit a7ff1be0b389, https://github.com/js8call/js8call | GPL-3.0-only | audio: `s16`, `f32`; rate 12000 Hz only | `--rate 12000` (the host must resample); `--start-time <UTC unix seconds>` (periods follow the UTC clock; default 0 = the first sample is a period boundary); `--modes` (default ABCE; I only on request) | no | not stated | upstream samples (JS8Call's own A_* and E_* test WAVs, copied unchanged; whether they are over-the-air is not stated by upstream) + generated; Ultra speed untested on real signals |
| `tetrapol/` `tetrapol-helper.exe` (v0.1.0) | tetrapol-kit-2023 (airphel), commit 09def9dcbc4c, https://github.com/airphel/tetrapol-kit-2023 | GPL-2.0-only | iq: `cf32`, `cs16` (ONE channel at baseband, carrier within about 2 kHz of 0 Hz, rate 16000 to 500000 Hz, 48000 usual); `bits`: hard GMSK bits, one byte 0 or 1 per bit, rate 8000 | `--rate`; `--band uhf|vhf`, `--direction down|up`, `--channel cch|tch` (tch: frames only counted); `--scr N` optional (default: detected) | yes: `--hide-ids` (identities become salted hashes; `--hide-ids-seed N` fixes the salt) | not stated | generated only (no real recording) |
| `stdc/` `stdc-helper.exe` (v1.0.0) | inmarsatc (cropinghigh), commit cda1242e7998, https://github.com/cropinghigh/inmarsatc | GPL-3.0 (version 3; UPSTREAM.md: the source has no "or later" clause; the manifest and hello string is the bare `GPL-3.0`) | audio: `s16`, `f32` at 48000 Hz (carrier 500 to 4500 Hz); iq: `cs16`, `cf32` at 48000 Hz (shifted up by `--iq-shift`, default 2500); `sym`: one byte 0 or 1 per symbol at 1200 Hz. Other rates refused, no resampling | `--rate 48000` (`--rate 1200` for `sym`); `--frames` optional; message text of any kind is never output | yes: `--hide-ids` (run-local pseudonyms #1, #2 ...) | not stated | real recording for one case (sigidwiki Inmarsat-C TDM EGC, 49 s I/Q, local, optional test `real_recording`: Bulletin Board for AOR-E); only the packet types in that recording are proven on real data; the rest generated |
| `sondes/` `sondes.exe` (v1.0.0) | RS (rs1729), commit 89b1ea7f3371, https://github.com/rs1729/RS | GPL-3.0-only | audio: `s16`, `f32` (FM-demodulated); rate 38400 to 192000 Hz, 48000 recommended; I/Q refused | `--rate`; `--types lms6,mk2a,mrz,mts01,cf06,rs92` (default all but rs92); RS92 needs `--rs92-ephemeris <file>` or `--rs92-almanac <file>` | no | not stated | generated only (the Mk IIa case uses real recorded subframe bits modulated by the generator); RS92 unverified |
| `trunkctl/` `trunkctl.exe` (v0.1.0) | trunk-recorder 5.2.0 (SmartNet, with OP25 rx_smartnet), commit 1aac86c; edacs-fm (EDACS), commit d92422ea8d13; https://github.com/TrunkRecorder/trunk-recorder, https://github.com/lwvmobile/edacs-fm | GPL-3.0-only | audio: `s16`, `f32` (FM discriminator audio, positive = higher frequency); iq: `cs16`, `cf32` (one channel, FM-demodulated in the helper); rate at least 7200 Hz for SmartNet, at least 19200 Hz for EDACS, 28800 Hz suits both | `--rate`; `--protocol auto|smartnet|edacs`; SmartNet frequencies need `--bandplan` (and `--obt-*`), EDACS frequencies need `--lcn N=MHz` or `--lcn-file`; `--variant`, `--esk`, `--afs`, `--bch-correct` optional | yes: `--hide-ids` (per-run aliases R1, T1 ...) | not stated | generated only (UPSTREAM.md: UNVERIFIED, no real recording of either protocol) |
| `weakmodes/` `weakmodes-helper.exe` (v1.0.0) | WSJT-X, commit b4f9a431bcf6, https://git.code.sf.net/p/wsjt/wsjtx (MSK144, JT9); wsjtx-ft2libre, commit 356be92606a6, https://github.com/kosciej/wsjtx-ft2libre (FT2L) | GPL-3.0-or-later | audio: `s16`, `f32`; rate 12000 Hz exactly (other rates refused) | `--rate 12000`; `--mode ft2,msk144,jt9` (default all three); periods are counted from the first sample, so start the audio on a period boundary (3.75 s FT2L, 60 s JT9) or pass `--offset <s>` | no | not stated | upstream samples (WSJT-X's own real MSK144 and JT9 recordings) + generated; FT2L has never decoded a real FT2 signal |
| `weakmodes2/` `weakmodes2-helper.exe` (v0.1.0) | OnAir, commit e118396b25d5 (FST4, FST4W, Q65), https://github.com/adswill/OnAir; WSJT-X lib/qra/q65, commit b4f9a431bcf6 | GPL-3.0-or-later + OnAir additional terms (GPL section 7(b), `weakmodes2/ADDITIONAL-TERMS-OnAir.md`) | audio: `s16`, `f32`; rate 8000 to 192000 Hz, 12000 recommended | `--rate`; `--decoders mode:period,...` (default `fst4:60,fst4w:120,q65:60A`); `--start-utc <UTC unix s>` or `--slot-offset <s>` so slots follow the UTC clock | no | **must show**: `Based on OnAir by Kristijonas Adomaitis - https://on-air.dev` | upstream samples (WSJT-X's real FST4 and Q65 sample recordings decode in the suite; several other sample files do not decode yet) + generated |
| `fldigimodes/` `fldigimodes.exe` (v0.1.0) | fldigi 4.1.23, commit 61b97f4133c4, https://github.com/w1hkj/fldigi | GPL-3.0-only | audio: `s16`, `f32`; rate 8000 Hz (11025 Hz for mfsk11, mfsk22, thor5, thor11, thor22) | `--rate`; `--mode <mode>` is REQUIRED (olivia-T-B, contestia-T-B, mfsk*, mt63-*, thor*, rsid); `--freq <Hz>` (default 1500), `--squelch`, `--reverse`, `--rsid on|off` optional | no | not stated | generated only (signals from fldigi's own transmitter, white noise only; no real recording) |
| `hfdl/` `hfdl.exe` (v1.7.0) | dumphfdl 1.7.0, commit bb7234bdf12f, https://github.com/szpajder/dumphfdl (+ libacars 2.2.1 MIT, liquid-dsp MIT port, libfec LGPL) | GPL-3.0-or-later | iq: `cs16`, `cf32`; rate 10800 S/s or more (5400 and 7200 decode nothing and are refused) | `--rate`; `--channel-offset <Hz>` REQUIRED, once per channel (assigned HFDL frequency minus the I/Q centre; the carrier is 1440 Hz above it); `--centre <Hz>` (absolute frequencies), `--start-time <unix s>`, `--system-table <file>` optional | yes: `--hide-ids` (blanks ICAO address, registration, flight id, aircraft data) | not stated | real recording (127 s off-air, 21931 kHz, local: 50 PDUs on four channels, all CRC good; test `hfdl.real_recording`) + generated |
| `hamdrm/` `hamdrm.exe` (v1.0.0) | QSSTV (ON4QZ), commit 31c323ccb848, https://github.com/ON4QZ/QSSTV | GPL-3.0-or-later | audio: `s16`, `f32`; rate 12000 Hz only (the host decimates 48 kHz) | `--rate 12000`; `--out-dir <DIR>` to have files written (no file bytes on stdout) | no | **must show** a statement that the program is based in whole or in part on QSSTV (`hamdrm/COPYING`); the helper says so in its README, source and `--version`: `hamdrm 1.0.0, based in whole or in part on QSSTV (ON4QZ), commit 31c323ccb848bade1dbab0a96890073f7b9455ea` | generated only (tests use QSSTV's own transmit chain, so a shared misreading of the standard is not caught; no real recording) |
| `msumrcal/` `msumrcal.exe` (v1.0.0) | SatDump, commit 9fee0793c7d4, https://github.com/SatDump/SatDump (infrared calibrator) | GPL-3.0-only | text: `--format msumr-lines` (`T` telemetry and `P` pixel-row records from a Meteor LRPT decoder; byte offsets in README); no samples; `--rate` optional and not used | `--format msumr-lines`; `--cold-offset-k <K>` (the "M2-2 patch" cold offset, +40 K, is an option the host must pass); `--lines-per-tlm N`, `--emit coeffs|bt|both` optional | no | not stated | generated only (arithmetic checked from the same Planck constants; no real satellite pass; the channel wavenumbers look like a stand-in, so absolute brightness temperature may be off by a few kelvin) |
| `mdc/` `mdc-helper.exe` (v1.0.0) | mdc-encode-decode (Matthew Kaufman), commit 45fc9095fbde, https://github.com/atmatthewat/mdc-encode-decode; fsync-mdc1200-decode, commit 287356a99d44, https://github.com/russinnes/fsync-mdc1200-decode (Fleetsync I) | GPL-2.0-only | audio: `s16`, `f32`; rate 8000 to 96000 Hz, 48000 recommended | `--rate`; `--no-mdc`, `--no-fleetsync`, `--report-bad` optional; Fleetsync II is not decoded here (see trunkfmt) | yes: `--hide-ids` (same id, same token within one run) | not stated | generated only (nothing real: no MDC-1200 or Fleetsync I recording was available) |

Event types (every helper prints the hello line first):

| Folder | Event types (from the manifest) |
|---|---|
| `multimon` | `POCSAG512`, `POCSAG1200`, `POCSAG2400`, `GSC`, `FLEX`, `FLEX_NEXT`, `EAS`, `UFSK1200`, `CLIPFSK`, `FMSFSK`, `AFSK1200`, `AFSK2400`, `AFSK2400_2`, `AFSK2400_3`, `HAPN4800`, `FSK9600`, `DTMF`, `DUALTONE`, `ZVEI1`, `ZVEI2`, `ZVEI3`, `DZVEI`, `PZVEI`, `EEA`, `EIA`, `CCIR`, `MORSE_CW`, `X10` |
| `hfmodes` | `text`, `event`, `status`, `stats` |
| `trunkfmt` | `lj1200`, `lj1200_transponder`, `tait_gps`, `tait_ani`, `fleetsync`, `fleetsync_status`, `fleetsync_gps`, `ltr`, `ltrnet`, `passport`, `mpt1327`, `connectplus_csbk`, `connectplus_slc`, `csbk`, `parse_error`, `stats` |
| `atn` | `frame`, `text`, `error`, `stats` |
| `rtl433` | `rtl433`, `stats` |
| `packet` | `ax25`, `fx25`, `il2p`, `stats` |
| `js8` | `js8`, `stats` |
| `tetrapol` | `sync`, `scr`, `frame`, `message`, `paging`, `ack`, `traffic`, `stats`, `status` |
| `stdc` | `packet`, `frame`, `stats` |
| `sondes` | `sonde`, `status`, `stats` |
| `trunkctl` | `grant`, `sysinfo`, `affiliation`, `msg`, `osw`, `frame` |
| `weakmodes` | `text` |
| `weakmodes2` | `decode`, `status`, `stats` |
| `fldigimodes` | `text`, `rsid`, `status`, `stats` |
| `hfdl` | `hfdl`, `text`, `status`, `stats` |
| `hamdrm` | `sync`, `mode`, `header`, `segment`, `file`, `stats`, `status` |
| `msumrcal` | `cal`, `bt`, `stats`, `status` |
| `mdc` | `mdc1200`, `mdc1200_crc_fail`, `fleetsync1`, `stats` |

## 2. Event fields per helper

Copied from the `events` object of each manifest (the field names the manifest lists for the panel). The manifests do not mark which fields are always present; an event may omit fields that do not apply. Where a manifest lists one event type for many sub-protocols (for example `rtl433`), the list is the common fields and protocol-specific fields can also appear.

### `multimon`

- `POCSAG512`: `address`, `function`, `kind`, `message`
- `POCSAG1200`: `address`, `function`, `kind`, `message`
- `POCSAG2400`: `address`, `function`, `kind`, `message`
- `GSC`: `address`, `function`, `kind`, `message`, `state`
- `FLEX`: `time`, `baud`, `levels`, `fragment`, `phase`, `cycle`, `frame`, `capcode`, `capcodes`, `kind`, `message`
- `FLEX_NEXT`: `time`, `baud`, `levels`, `cycle`, `frame`, `phase`, `capcode`, `capcodes`, `address_type`, `kind`, `flags`, `message`
- `EAS`: `kind`, `message`, `originator`, `event`, `locations`, `duration`, `issued`, `sender`
- `UFSK1200`: `message`
- `CLIPFSK`: `message`
- `FMSFSK`: `message`
- `AFSK1200`: `kind`, `from`, `to`, `via`, `frame`, `pid`, `source`, `dest`, `path`, `info`, `info_lines`, `message`
- `AFSK2400`: `kind`, `from`, `to`, `via`, `frame`, `pid`, `source`, `dest`, `path`, `info`, `info_lines`, `message`
- `AFSK2400_2`: `kind`, `from`, `to`, `via`, `frame`, `pid`, `source`, `dest`, `path`, `info`, `info_lines`, `message`
- `AFSK2400_3`: `kind`, `from`, `to`, `via`, `frame`, `pid`, `source`, `dest`, `path`, `info`, `info_lines`, `message`
- `HAPN4800`: `kind`, `from`, `to`, `via`, `frame`, `pid`, `source`, `dest`, `path`, `info`, `info_lines`, `message`
- `FSK9600`: `kind`, `from`, `to`, `via`, `frame`, `pid`, `source`, `dest`, `path`, `info`, `info_lines`, `message`
- `DTMF`: `digit`
- `DUALTONE`: `message`
- `ZVEI1`: `digits`
- `ZVEI2`: `digits`
- `ZVEI3`: `digits`
- `DZVEI`: `digits`
- `PZVEI`: `digits`
- `EEA`: `digits`
- `EIA`: `digits`
- `CCIR`: `digits`
- `MORSE_CW`: `message`
- `X10`: `bits`, `house`, `unit`

### `hfmodes`

- `text`: `mode`, `text`
- `event`: `mode`, `text`
- `status`: `mode`, `state`, `text`, `centre_hz`, `shift_hz`, `baud`, `detail`, `snr_db`, `params`, `good`, `bad`
- `stats`: `mode`, `good`, `bad`, `samples`, `seconds`

### `trunkfmt`

- `lj1200`: `crc`, `function`, `function_code`, `address`, `reply_code`, `site`, `vrc`, `lrc`, `crc_field`, `msg`
- `lj1200_transponder`: `crc`, `crc_field`, `msg`
- `tait_gps`: `crc`, `from`, `to`, `lat`, `lon`, `gps_time`, `day`, `field_388_391`, `speed_kph`, `size`, `size2`, `msg`
- `tait_ani`: `crc`, `from`, `to`, `size`, `size2`, `msg`
- `fleetsync`: `crc`, `kind`, `from`, `emergency`, `lone_worker`, `paging`, `acknowledge`, `ani`, `msg`
- `fleetsync_status`: `crc`, `kind`, `from`, `to`, `status`, `emergency`, `lone_worker`, `paging`, `acknowledge`, `ani`, `msg`
- `fleetsync_gps`: `crc`, `kind`, `from`, `to`, `lat`, `lon`, `heading`, `speed`, `gps_time`, `gps_year_field`, `gps_month`, `gps_day`, `msg`
- `ltr`: `direction`, `message`, `crc`, `area`, `lcn`, `home`, `group`, `free`, `talkgroup`, `msg`
- `ltrnet`: `direction`, `message`, `crc`, `talkgroup`, `area`, `lcn`, `home`, `group`, `free`, `channels`, `frequency_hz`, `half`, `neighbor`, `rank`, `unique_id`, `site`, `esn`, `esn_high`, `esn_low`, `text`, `msg`
- `passport`: `message`, `dcc`, `site`, `lcn`, `free`, `type_number`, `group`, `radio_id`, `lcn_freq_hz`, `free_freq_hz`, `site_band`, `neighbor_band`, `neighbor_freq_hz`, `corrected`, `text`, `msg`
- `mpt1327`: `sync`, `message`, `crc`, `description`, `prefix`, `ident1`, `ident2`, `channel`, `from`, `to`, `system`, `text`, `msg`
- `connectplus_csbk`: `decoded`, `fid`, `vendor`, `csbko`, `name`, `last_block`, `protected`, `crc`, `source`, `target`, `radio`, `talkgroup`, `lsn_field`, `lcn`, `timeslot`, `lsn`, `neighbors`, `window`, `superframe`, `reserved_for`, `message_type`, `version`, `data_lcn`, `unk`, `msg`
- `connectplus_slc`: `crc`, `slc_opcode`, `decoded`, `name`, `network`, `site`, `msg`
- `csbk`: `decoded`, `fid`, `vendor`, `csbko`, `crc`, `msg`
- `parse_error`: `line`, `text`
- `stats`: `samples`, `symbols`, `lj1200`, `lj1200_failed_crc`, `lj1200_transponder`, `tait_ani`, `tait_gps`, `suppressed_unchecked`, `fleetsync2`, `fleetsync2_invalid`, `mpt1327`, `mpt1327_invalid`, `ltr`, `ltrnet`, `ltrnet_failed_crc`, `passport`, `passport_failed_crc`, `lines`, `decoded`, `not_decoded`, `errors`, `errors_not_printed`

### `atn`

- `frame`: `t`, `line`, `octets`, `layers`, `msg`
- `text`: `t`, `line`, `text`
- `error`: `t`, `line`, `error`
- `stats`: `lines`, `frames`, `decoded`, `bad_fcs`, `too_short`, `bad_input`

### `rtl433`

- `rtl433`: `protocol`, `model`, `subtype`, `id`, `channel`, `battery_ok`, `battery_V`, `temperature_C`, `temperature_F`, `humidity`, `pressure_kPa`, `pressure_PSI`, `pressure_hPa`, `wind_avg_km_h`, `wind_dir_deg`, `rain_mm`, `button`, `code`, `state`, `status`, `alarm`, `tamper`, `mic`, `mod`, `freq`, `freq1`, `freq2`, `rssi`, `snr`, `noise`, `text`
- `stats`: `samples`, `bytes`, `frames`, `frames_ook`, `frames_fsk`, `frames_events`, `events`, `protocols_enabled`

### `packet`

- `ax25`: `text`, `src`, `dst`, `via`, `heard`, `control`, `pid`, `frame`, `aprs`, `info_len`, `info_hex`, `info_text`, `fix`, `level`, `mark`, `space`, `subchan`, `slice`
- `fx25`: `text`, `src`, `dst`, `via`, `heard`, `control`, `pid`, `frame`, `aprs`, `info_len`, `info_hex`, `info_text`, `corrected`, `level`, `mark`, `space`, `subchan`, `slice`
- `il2p`: `text`, `src`, `dst`, `via`, `heard`, `control`, `pid`, `frame`, `aprs`, `info_len`, `info_hex`, `info_text`, `corrected`, `level`, `mark`, `space`, `subchan`, `slice`
- `stats`: `samples`, `frames`, `ax25`, `fx25`, `il2p`, `fx25_corrected`, `il2p_corrected`

### `js8`

- `js8`: `mode`, `speed`, `slot_t`, `utc`, `freq`, `dt`, `snr`, `quality`, `low_confidence`, `bits`, `first`, `last`, `data_flag`, `frame`, `frame_type`, `text`, `from`, `to`, `cmd`, `num`, `grid`, `cq`
- `stats`: `samples`, `slots`, `frames`, `frames_by_mode`

### `tetrapol`

- `sync`: `state`
- `scr`: `scr`
- `frame`: `frame_no`, `state`, `syndromes`, `bits_fixed`
- `message`: `log_ch`, `frame_no`, `tpdu`, `addr`, `prio`, `tsap_id`, `tsap_ref_swmi`, `tsap_ref_rt`, `msg`, `class`, `codop`, `len`, `text`, `cell_state`, `cell_id`, `bn_id`, `u_ch_scrambling`, `radio_param`, `band`, `channel_id`, `cell_config`, `country_code`, `system_id`, `loc_area`, `cell_bn`, `system_time`, `cell_access`, `superframe_cpt`, `ccr_config`, `adjacent_cells`, `cell_ids`, `adjacent_bn`, `activation_mode`, `group_id`, `coverage_id`, `d_ch_scrambling`, `key_reference`, `addr_tti`, `calling_adr`, `host_adr`, `cause`, `call_type`, `dch_low_layer`, `complete_reg`, `rt_status`, `rt_data_info`
- `paging`: `frame_no`, `activation_bitmap`, `addrs`
- `ack`: `frame_no`, `acks`, `nacks`
- `traffic`: `kind`, `text`, `syndromes`
- `stats`: `bits`, `frames_ok`, `frames_bad`, `frames_corrected`, `messages`, `messages_data_not_decoded`, `messages_unknown`, `traffic_frames_not_decoded`, `sync_found`, `sync_lost`, `lines_dropped`
- `status`: `text`

### `stdc`

- `packet`: `frame`, `descriptor`, `name`, `length`, `stage`, `reassembled`, `uncertain`, `ber`, `fields`
- `frame`: `frame`, `ber`, `reversed`, `midstream_reversed`, `uncertain`
- `stats`: `samples`, `symbols`, `frames`, `packets`, `packets_bad`, `carrier_hz`, `in_sync`, `no_sync_blocks`

### `sondes`

- `sonde`: `sonde`, `subtype`, `serial`, `frame`, `time`, `lat`, `lon`, `alt`, `vel_h`, `heading`, `vel_v`, `sats`, `temp`, `humidity`, `pressure`, `batt`, `gpstow`, `aux`, `freq`, `tx_frequency`, `ref_datetime`, `ref_position`
- `status`: `text`
- `stats`: `samples`, `frames`

### `trunkctl`

- `grant`: `proto`, `call`, `mode`, `tgid`, `rid`, `dst_rid`, `channel`, `lcn`, `freq_hz`, `encrypted`, `emergency`, `update`, `variant`, `agency`, `fleet`, `subfleet`
- `sysinfo`: `proto`, `kind`, `sys_id`, `site`, `channel`, `freq_hz`, `cc_channel`, `cc_freq_hz`, `lcn`, `control_lcn`, `datetime`, `weekday`, `variant`
- `affiliation`: `proto`, `rid`, `tgid`
- `msg`: `proto`, `name`, `rid`, `dst_rid`, `tgid`, `sub_tgid`, `from_tgid`, `to_tgid`, `reason`, `code`, `opcode`, `status`, `message`, `mode`, `site`, `state`, `lcn`, `variant`
- `osw`: `proto`, `addr`, `group`, `cmd`
- `frame`: `proto`, `a`, `b`, `ok_a`, `ok_b`, `errors`

### `weakmodes`

- `text`: `mode`, `freq`, `snr`, `dt`, `tstart`, `text`, `note`

### `weakmodes2`

- `decode`: `mode`, `sub`, `period`, `slot`, `utc`, `freq`, `snr`, `dt`, `dbm`, `call`, `grid`, `partial`, `text`
- `status`: `text`
- `stats`: `samples`, `slots_decoded`, `decodes`

### `fldigimodes`

- `text`: `text`, `mode`, `freq`, `offset`, `snr`, `sync`, `metric`
- `rsid`: `name`, `code`, `mode`, `known`, `freq`, `extended`, `eot`
- `status`: `text`
- `stats`: `samples`, `chars`, `text_events`, `rsid_hits`, `pictures_skipped`

### `hfdl`

- `hfdl`: `hfdl`, `text`
- `text`: `text`
- `status`: `text`
- `stats`: `samples`, `pdus`

### `hamdrm`

- `sync`: `state`, `mode`, `snr_db`
- `mode`: `mode`, `callsign`, `snr_db`
- `header`: `transport_id`, `name`, `size`, `mode`
- `segment`: `transport_id`, `name`, `segment`, `received`, `total`, `percent`, `snr_db`
- `file`: `transport_id`, `name`, `size`, `sha256`, `saved`, `path`, `fec`, `note`, `mode`, `callsign`, `snr_db`
- `stats`: `seconds`, `locked`, `snr_db`, `packets`, `crc_failures`, `dropped`, `files`
- `status`: `text`

### `msumrcal`

- `cal`: `k`, `ch`, `valid`, `reason`, `cold_view`, `hot_view`, `cold_k`, `hot_k`, `cold_rad`, `hot_rad`, `gain`, `offset`, `filled`, `outlier`
- `bt`: `y`, `ch`, `k`, `unit`, `bt`
- `stats`: `strips`, `strips_with_tlm`, `strips_with_temps`, `pixel_rows`, `pixels`, `pixels_invalid`, `bad_lines`
- `status`: `text`

### `mdc`

- `mdc1200`: `frames`, `op`, `op_hex`, `arg`, `arg_hex`, `unit`, `op2`, `op2_hex`, `arg2`, `arg2_hex`, `unit2`, `ecc_bits`, `crc_ok`
- `mdc1200_crc_fail`: `frame`, `crc_ok`
- `fleetsync1`: `cmd`, `subcmd`, `from_fleet`, `from_unit`, `to_fleet`, `to_unit`, `all_call`, `baud`, `raw`, `payload`, `crc_ok`
- `stats`: `samples`, `mdc1200`, `mdc1200_double`, `mdc1200_ecc_corrected`, `mdc1200_crc_fail`, `fleetsync1`, `fleetsync2_ignored`

## 3. Things the table cannot say in one cell

- **Sample rates are not converted by the helper** unless the row says so: multimon (22050 Hz), js8 and weakmodes (12000 Hz), hamdrm (12000 Hz) and stdc (48000 Hz) accept one rate and refuse others with exit code 2 and a message on stderr; the host must resample.
- **Helpers that need the host to supply an upstream stage**: `atn` needs AVLC frames from a VDL2 demodulator; `msumrcal` needs `T`/`P` lines from a Meteor LRPT decoder; `trunkfmt --format csbk-hex` needs CSBK or Short LC words from a DMR decoder.
- **Privacy options**: only the helpers marked `yes` in the `--hide-ids` column have one. `stdc` never outputs message text. The others have no such option and print the identifiers they decode.
- **The hello line** carries extra fields in several helpers (for example `format`, `rate`, `upstream`, `mode`): a bridge reads only `helper`, `version`, `protocol`, `licence`, `source` (and `attribution` where present) and ignores the rest.
- **Known limits**: FT2L in `weakmodes` has never decoded a real FT2 signal. The `mdc` helper reports operation codes and arguments as numbers only; it has no table of operation names.
