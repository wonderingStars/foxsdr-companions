# UPSTREAM: rtl433 helper

| | |
|---|---|
| Project | rtl_433 (merbanan/rtl_433) |
| URL | https://github.com/merbanan/rtl_433 |
| Commit taken | `0c85f7ad3566c05cb39b0ff5f1cf6a1f200b8e6e` ("Add conf for key finder tags", 2026-11-08; shallow clone, release 25.12 plus later commits) |
| Licence | GPL-2.0-or-later. `COPYING` is the GPL version 2 text; every source file header says "either version 2 of the License, or (at your option) any later version" (checked: `src/rtl_433.c` and the 39 other headers that carry the sentence). `LICENSE` in this folder is upstream's `COPYING`, unchanged. |
| Helper licence | GPL-2.0-or-later (same). New files written for the helper carry the same notice. |
| Mixed code | None: only rtl_433 code. No second upstream project. |

## Files taken (all copied unchanged except line endings, see below)
- `src/`: abuf.c aes.c baseband.c bit_util.c bitbuffer.c compat_time.c data.c decoder_util.c fft.c fileformat.c list.c logger.c
  optparse.c pulse_data.c pulse_detect.c pulse_detect_fsk.c pulse_slicer.c r_flow.c r_util.c raw_output.c rfraw.c
  string_expand.c timer.c (23 files).
- `src/devices/`: 329 device decoder files, one per decoder (upstream has 342 in the build list; 13 are left out, see below).
- `include/`: the upstream headers the above need (39 files, no mongoose.h, http_server.h, output_*.h, sdr.h, ...).
- `LICENSE` = `COPYING`.
- Line endings: the clone was checked out with CRLF; all files were converted to LF (`.gitattributes` in this
  repository forces LF). Nothing else was touched in them.

## Files changed
- `src/r_api.c`: TRIMMED copy of upstream's (1443 -> 769 lines). Kept unchanged: protocol registration, `time_pos_str`,
  `run_ook_demods`, `run_fsk_demods`, the log handler, `event_occurred_handler`, `log_device_handler`,
  `data_acquired_handler` (unit conversion, protocol number, meta fields), `create_report_data`, `flush_report_data`.
  Removed: the mongoose manager, SDR device control (`set_center_freq`, `set_gain_str`, ...), CSV field discovery, all
  output constructors (json/csv/log/kv/mqtt/influx/syslog/http/trigger/null/rtltcp), signal-file dumpers (`add_dumper`,
  `add_sr_dumper`), data tags, the LoRa demodulator creation (`lora_fm_demod` stays NULL; `get_lora_params`,
  `run_lora_demods` return 0). Diff: `patches/r_api.c.diff`.
- `include/rtl_433_devices.h`: the 14 removed decoders are renamed `removed_<name>` in the DEVICES list (their
  placeholders live in `src/helper_stubs.c`). This keeps every other protocol number the same as in upstream rtl_433.
  Diff: `patches/rtl_433_devices.h.diff`.

## Files written for the helper (new, GPL-2.0-or-later)
- `src/helper_main.c` (replaces upstream `src/rtl_433.c`): command line, stdin reader, hello/stats lines. The input loop
  follows the "in files" test mode of upstream `main()` (format conversion CS8->CU8 and CF32->CS16 as upstream does).
- `src/output_foxsdr.c`, `include/output_foxsdr.h`: the one output handler (JSON line + `text`), `--hide-ids`.
- `src/helper_stubs.c`: no-op stand-ins for the parts not built, and the 14 `removed_*` placeholder decoders.
- `test/gen_signals.c`, `test/feed.c`, `test/run_case.cmake`, `test/CMakeLists.txt`, `test/expected/*.jsonl`: tests.

## Left out, and why
| What | Why |
|---|---|
| Itron ERT: `ert_scm`, `scmplus` (SCM+), `ert_idm`, `ert_netidm` (protocols 149, 154, 160, 161) | Patent US7830874 (shown active to 2029-02-09). Left out because of the patent. |
| LoRa: `src/lora.c`, `src/lora_fm.c`, `src/lorawan.c`, decoders `cotech_36_9231`, `lorawan`, `elsys_ers` (387-389) | LoRa chirp demodulation is on the do-not-build list. `lorawan.c` and the two device decoders need it. |
| Decoders that decrypt or recover a key: `ikea_sparsnas` (130, brute-forces its key), `govee_h5059` (300), `govee_h5310` (349), `govee_h5112` (350) (fixed 128-byte XOR key table), `apator_metra_erm30` (277), `apator_metra_eitn30` (296) (nibble-substitution "encryption"), `esa_energy` (117, salted XOR chain) | Brief rule: no decryption, no key recovery. A judgement call for the last three (Apator, ESA are obfuscations with no secret): to put one back, restore its file from upstream and its `DECL` line, delete its `REMOVED(...)` line. |
| Radio drivers: `src/sdr.c` (RTL-SDR, SoapySDR), `rtl_tcp` output and input | Input is standard input only. |
| Network: `src/mongoose.c`, `http_server.c`, `output_mqtt.c`, `output_influx.c`, `output_udp.c` (syslog), `output_rtltcp.c`, `output_trigger.c`, `data_tag.c` | No network code in a helper. |
| Replay / capture aids: `samp_grab.c` (writes I/Q segments), `write_sigrok.c`, `sigmf.c`, `microtar.c`, `pulse_analyzer.c` (prints suggested flex lines), `am_analyze.c`, VCD / pulse dumpers | FoxSDR is receive-only; nothing that helps replay or clone a device. |
| Config file reader, terminal output (`confparse.c`, `compat_paths.c`, `term_ctl.c`, `output_file.c`, `output_log.c`, `jsmn.c`, `r_version.c`) | Not needed; one fixed output. |
| `src/rtl_433.c` | Replaced by `helper_main.c`. |

Not removed on purpose: `flex.c` (`-X`, user-defined *decoders*: it only decodes), the decoders for rolling-code
remotes (`hcs200`, `hcs361`, `hcs362`, `somfy_rts`, KeeLoq-style car remotes ...): they show counters and hop codes as
opaque values; nothing computes, recovers or predicts a code. `silver_spring_mesh` (Itron-owned network, different
protocol, not ERT) is kept. Audit method: grepped all decoders for `decrypt|encrypt|aes|brute|key` (see the table above
for what it found); no decoder performs next-code prediction.

## Differences in behaviour from upstream rtl_433
- Output is JSON Lines of the FoxSDR pipe protocol, not rtl_433's JSON: `time` becomes `t` (seconds, from the sample
  count), added `type":"rtl433"`, a `text` summary; fields named `t`, `type`, `text`, `time` in a decoder are renamed
  `rtl_t`, `rtl_type`, `rtl_text`, `rtl_time` (only `type` occurs: the TPMS decoders). Doubles are printed `%.3f` as
  upstream's JSON printer does. Strings are fully JSON-escaped (upstream prints a string that starts with `{` verbatim).
- `protocol` (the rtl_433 protocol number) and the meta fields (`mod`, `freq`/`freq1`/`freq2`, `rssi`, `snr`, `noise`)
  are on by default (`--no-meta` turns the radio ones off).
- `mod` is rtl_433's own guess (`ASK` or `FSK`); on weak or noisy OOK it can say `FSK`. Unchanged upstream logic.
- Timestamps are the sample position, never wall clock; `localtime` is not used (TZ=UTC).
- Three file-name quirks: upstream devices with `.` or `-` in the file name are in the build list (`tfa_drop_30.3233.c`,
  `tfa_twin_plus_30.3049.c`, `inovalley-kw9015b.c`).

## Privacy: --hide-ids
Replaces the value of every identifier field by `h` + 8 hex digits: FNV-1a over a per-run salt and the value text,
finalised. Identifier fields are chosen by name (`foxsdr_is_id_key` in `output_foxsdr.c`): `id`, `ids`, `id0`..,
`uid`, `sn`, `esn`, `serial*`, `address`, `addr`, `housecode`, `node`, `mac`, `eui`, `imei`, `*_id`, `*ID`/`*Id`
(except `msg_id`, `cmd_id`, `sequence_id`, `button_id`, `PacketTypeID`, `ProtocolID`). Raw payload fields (`data`,
`payload`, `raw`, `bits`, `codes`, `rows`, `packet`, `frame`, `hex` ...) are dropped, since the id can be read out of
them. The `text` summary uses the hashed values. Limits: the salt is random per run (or `--hide-ids-salt`), so hashes
differ between runs; fields with other names (a `code` that is really a fixed remote serial) are NOT hidden.

## Tests and licence of the test recordings
`merbanan/rtl_433_tests` (the companion sample repository): GitHub licence API `license: null`; the repository root
(`.github .gitignore README.md bin false_positives_test makefile requirements.txt tests`) has no LICENSE file; the
README has no licence statement (only a "Contributing" section); the web search below found none either. So the
recordings are NOT committed. They were used once, locally, outside the repository, as a cross-check of the build
(downloaded as data files from raw.githubusercontent.com into a local folder):
Toyota TPMS `0d5aee3_g007_433.92M_250k.cu8`, Schrader TPMS `gfile001.cu8`, Honeywell door sensor
`honeywell-001.cu8`, Oregon WMR500 `g028_868.2M_1000k.cu8` (1 MS/s, centre 868.2 MHz): model, id, values and `@time`
equal the `.json` references shipped next to each file (e.g. Toyota: `t` 0.214176, id f0d5aee3, 34.25 PSI, 29 C).
The pinned tests use signals from `test/gen_signals.c` instead (see README).

Searches run: `gh api repos/merbanan/rtl_433_tests` (licence null), `gh api .../contents` (no LICENSE),
README grep for licence/copyright; WebSearch `site:github.com merbanan rtl_433_tests license recordings sample files
licence` (no licence statement found).
