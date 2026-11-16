# trunkctl - trunking control channel decoder (FoxSDR GPL helper)

Decodes the **control channel** of two trunked radio systems from discriminator audio or channel I/Q:

- **Motorola SmartNet / SmartZone (Type II)**, 3600 baud: frame sync, de-interleave, parity correction, CRC, and the
  outbound signalling words: system ID, voice channel grants (group, private, analog and digital), voice updates,
  affiliation, status / message acknowledgements, call alerts, denials, patches, date and time, adjacent / alternate
  control channels, control channel frequency. Channel number -> frequency for the 800 MHz standard, rebanded and
  splinter plans, 900 MHz and a configurable 400 MHz (OBT) plan.
- **EDACS**, 9600 baud, standard / networked and Extended Addressing (and ESK): sync, BCH(40,28) error detection **and
  correction**, voice channel assignments (analog, digital, data, TDMA), LCN status messages, site ID, peer sites, control
  channel LCN, patch and kick listings, AFS split into agency / fleet / subfleet.

Receive only. No voice (no ProVoice, no codec), no decryption: encrypted calls are reported as such, and that is all.
It is a complete command-line program: you can pipe samples into it without FoxSDR. It is licensed GPL-3.0-only
(see `LICENSE`); `UPSTREAM.md` says where every part comes from.

## Input
`trunkctl --foxsdr-helper 1 --format <fmt> --rate <Hz> [options]` reads samples on stdin until end of file, prints JSON
Lines on stdout (a hello line first), human-readable log lines on stderr. `--foxsdr-helper-info` prints the manifest.

| `--format` | meaning |
|---|---|
| `s16`, `f32` | mono **FM discriminator audio** of one channel (positive = carrier above the channel centre), the signal an `rtl_fm`-style demodulator gives. Little-endian int16 or float |
| `cs16`, `cf32` | interleaved I/Q of one channel (already tuned and filtered to about 12.5 kHz), FM-demodulated inside |

Sample rate: any rate of at least 2 symbols per symbol period: **7200 Hz for SmartNet, 19200 Hz for EDACS**.
28800 Hz suits both (8 and 3 samples per symbol); 48000 Hz works. With `--protocol auto` (default) every protocol the
rate allows is decoded at once and events carry `"proto"`. Polarity is found from the sync word (`--polarity` forces it).

By hand, from a recording of discriminator audio:

```
trunkctl --foxsdr-helper 1 --format s16 --rate 48000 --bandplan 800_standard < control_channel.s16
```

Typical SmartNet start (generated test signal, see Tests):

```
{"helper":"trunkctl","version":"0.1.0","protocol":1,"licence":"GPL-3.0-only","source":"https://github.com/TrunkRecorder/trunk-recorder",...,"format":"s16","rate":48000,"protocols":["smartnet"]}
{"t":0.048813,"type":"status","proto":"smartnet","text":"smartnet: control channel locked"}
{"t":0.608812,"type":"sysinfo","proto":"smartnet","kind":"system","sys_id":"0x1234","channel":168,"freq_hz":855212500}
{"t":0.702146,"type":"grant","proto":"smartnet","call":"group","mode":"analog","tgid":6720,"rid":12345,"channel":288,"freq_hz":858212500,"encrypted":false,"emergency":false,"update":false}
```

## Options
| option | meaning |
|---|---|
| `--protocol auto\|smartnet\|edacs` | default auto |
| `--polarity auto\|normal\|inverted` | default auto |
| `--bandplan none\|800_standard\|800_reband\|800_splinter\|900\|obt` | SmartNet channel -> frequency. `none` (default): channels are reported without `freq_hz`. `obt` needs `--obt-base MHz --obt-high MHz --obt-spacing MHz --obt-offset N` (frequency of channel number N is the base) |
| `--variant auto\|standard\|ea` | EDACS addressing. Auto follows edacs-fm's rules (peer-site relay word = extended addressing; command byte 0xEE / 0xEF / 0xF3 = standard) and says so in a `status` line; nothing is decoded before it knows |
| `--esk` | EDACS: command byte XOR 0xA0 (not auto-detected) |
| `--afs A,F` | EDACS standard: agency and fleet bit lengths of the 11-bit AFS (default 4,4, subfleet = the rest) |
| `--bch-correct 0\|1\|2` | EDACS: bit errors corrected per 40-bit message, default 1. With 12 parity bits a random word is within distance 1 of a codeword about 1 % of the time and within distance 2 about 20 % of the time, so 2 can turn a destroyed message into a plausible wrong one (it is in the test suite: `ed_damaged_c2`) |
| `--lcn N=MHz` (repeatable), `--lcn-file FILE` | EDACS LCN -> frequency (file: `N MHz` or `N,MHz`, `#` comments) |
| `--update-interval S` | report the same call (talkgroup + channel) again only after S seconds (default 2; 0 = every repeat) |
| `--stats-interval S` | `stats` line every S seconds of signal (default 10; 0 = only at the end; a final line is always printed) |
| `--hide-ids` | radio IDs and talkgroups are replaced by per-run aliases `R1`, `T1` ... in order of first appearance (strings instead of numbers). Not reversible and not stable between runs. Agency / fleet / subfleet, raw words (`--raw`) and unknown-meaning addresses are hidden too. Site IDs, system IDs, channels and frequencies are not personal and stay |
| `--raw` | also print every valid SmartNet OSW (`osw`) and every EDACS frame (`frame`) |

Exit codes: 0 after end of file; 2 for a wrong command line (message on stderr, nothing on stdout, no dialog).

## Output (one JSON object per line; `t` = seconds of signal from the sample count)
- `status` (reserved by the protocol): `text` = "control channel locked" / "lost" (no valid frame for 1 s SmartNet, 3 s EDACS) and "EDACS variant detected".
- `grant` - a voice channel assignment, for trunk following:
  - SmartNet: `call` (`group` / `private`), `mode` (`analog` / `digital`, absent on one-OSW updates), `tgid`, `rid` (source
    radio, absent when unknown), `dst_rid` (private), `channel` (number), `freq_hz` (when the band plan is known),
    `encrypted`, `emergency` (flags in the low bits of the talkgroup word), `update` (true = a one-OSW voice update
    without source).
  - EDACS: `variant` (`ea` / `standard`), `call` = `group`, `mode` (`digital`, `data`, `tdma` for extended addressing;
    `analog`, `digital` for standard), `tgid` (extended addressing: 16-bit group; standard: 11-bit AFS), `agency`,
    `fleet`, `subfleet` (standard), `rid`, `lcn` (= `channel`), `freq_hz` (when `--lcn` is given), `update` (false).
- `sysinfo`: `kind` = `system` (system ID `sys_id` + control channel), `control_channel`, `alt_cc`, `adjacent_cc`,
  `site`, `adjacent_site`, `datetime`, `scan_marker` (SmartNet); `site`, `peer_site`, `control_channel` (EDACS).
- `affiliation` (SmartNet): `rid`, `tgid`.
- `msg`: everything else with a `name`: SmartNet `group_busy`, `emergency_busy`, `private_call_busy`, `dynamic_regroup`,
  `status_ack`, `message`, `message_ack`, `call_alert`, `private_call_ring`, `denied` (`reason`), `patch`, `multiselect`,
  `patch_cancel`, `emergency_ptt`, `roaming`, `amss`, `radio_check`, `deaffiliation`, failsoft / inhibit / selector
  names ...; EDACS `lcn_status` (`state`: downlink, reserved, convert_to_callee, caller_queued, system_busy,
  radio_id_denied: LCNs 26-31 are call states, not channels), `patch`, `kick`.
- `stats`: `frames`, `messages`, `unknown`, EDACS `bad` / `corrected`, SmartNet `gaps`, `lost`, `locked`.

A bridge ignores event types and fields it does not know. The full field list per type is in `trunkctl.helper.json`.

## What it does not do
No voice, no decryption, no data-channel decoding. SmartNet "international" and "shuffled" 800 MHz plans decode but
report no frequency (as upstream). Not proven on real signals: see `UPSTREAM.md` (UNVERIFIED section).

## Build and test
Visual Studio 2022, CMake, x64 Release, C++17. No other dependency.

```
cmake -S trunkctl -B <build> -G "Visual Studio 17 2022" -A x64
cmake --build <build> --config Release --parallel 4 --target trunkctl
ctest --test-dir <build> -C Release --output-on-failure
```

Tests (`test/`): `trunkgen` (test tool, not shipped) builds control-channel frames from the same constants the decoder
uses (SmartNet: checked against the upstream `rx_smartnet.cc` functions, see `UPSTREAM.md`; EDACS: edacs-fm's sync and
layouts with this helper's BCH encoder), modulates them as 2-FSK discriminator audio or FM I/Q at several sample rates,
polarities, timing offsets and noise levels; the CTest cases pipe the files in and compare the JSON Lines with pinned
files in `test/expected/`, check determinism (two runs equal), a file fed through a pipe in small writes, float and I/Q
input against the s16 result, damaged frames (corrected, dropped, never invented), loss of lock, empty and garbage
input, and wrong command lines. `TC_UPDATE=1 ctest ...` rewrites the pinned files after an intended change.
