# hfmodes: HF telegraphy decoders for FoxSDR (GPL helper)

A standalone command-line program. It reads mono audio on standard input and prints JSON Lines on standard output
(pipe protocol version 1, see `docs/PIPE-PROTOCOL.md` in this repository). FoxSDR can start it as a separate process; any
other program can pipe audio into it too.

Licence: GPL-3.0-or-later, with the additional attribution term of OnAir (`ADDITIONAL-TERMS.md`, GPL section 7(b)).
The decoders come from OnAir (https://github.com/adswill/OnAir) and from Rivet (https://github.com/IanWraith/Rivet);
`UPSTREAM.md` has the commits, the files and every change. `--version` and `--help` print the attribution line
the OnAir terms require: *Based on OnAir by Kristijonas Adomaitis - https://on-air.dev*

## Run it by hand

```
hfmodes --foxsdr-helper 1 --format s16 --rate 44100 [--modes SITOR-B,CROWD36] < audio.raw
hfmodes --foxsdr-helper-info      # the manifest
hfmodes --list-modes
```

- `--format s16` (little-endian 16-bit) or `f32` (little-endian float), mono. `--rate` 8000 to 192000 Hz. I/Q formats are refused.
- The audio is what a receiver in upper sideband gives: the tones are anywhere between about 300 and 3500 Hz. There is
  no centre frequency (`--centre` is accepted and ignored).
- `--modes` is a comma list (see `--list-modes`; `SITOR` means both SITOR modes). With no `--modes` every decoder
  runs at once: that is about 1.3 times real time on the development machine at 44.1 kHz (Piccolo is the slowest), and
  decoders that do not match the signal still print `found` status lines now and then. A bridge should pass the
  modes it wants.
- `--crowd36-baud N` (default 40) is Rivet's CROWD36 symbol rate.
- Input ends at end of file; the helper then prints `stats` lines and exits 0. Empty input: the hello line, one `stats` line, exit 0.
  A wrong `--format`, `--rate` or `--modes` value: a message on stderr, exit 2, nothing on stdout.
- Deterministic: the same bytes in give the same lines out (the processing step is 1024 input samples, whatever the read sizes).

Example, a real off-air recording (sigidwiki "SITOR-B tfc", converted to 44.1 kHz mono s16 by ffmpeg, kept out of the repository):

```
{"helper":"hfmodes","version":"0.1.0","protocol":1,"licence":"GPL-3.0-or-later","source":"https://github.com/adswill/OnAir","attribution":"Based on OnAir by Kristijonas Adomaitis - https://on-air.dev"}
{"t":0.348,"type":"status","mode":"SITOR-B","state":"found","centre_hz":2203.1,"shift_hz":161.1,"snr_db":58.5,"detail":"normal polarity"}
{"t":5.039,"type":"text","mode":"SITOR-B","text":"QQWWAAAAAA"}
{"t":6.432,"type":"text","mode":"SITOR-B","text":" ZC"}
{"t":6.896,"type":"text","mode":"SITOR-B","text":"ZC\r"}
{"t":7.546,"type":"text","mode":"SITOR-B","text":" CQ"}
{"t":8.104,"type":"text","mode":"SITOR-B","text":" DE "}
{"t":8.661,"type":"text","mode":"SITOR-B","text":"OST "}
{"t":9.218,"type":"text","mode":"SITOR-B","text":"QTC "}
{"t":9.799,"type":"text","mode":"SITOR-B","text":"LIST"}
```

## Output

Every line is a JSON object. `t` is seconds from the start, counted from the samples read.

| type | fields |
|---|---|
| hello (first line) | `helper`, `version`, `protocol`, `licence`, `source`, `attribution` |
| `text` | `mode`, `text`: decoded characters (a chunk: up to a newline, 80 characters or half a second of stream time). Bytes that are not UTF-8 appear as U+0080 to U+00FF |
| `event` | `mode`, `text`: one line per frame, message or state change, as the upstream decoder words it |
| `status` | `mode`, `state` (`found` = signal measured, `reading` = locked and reading), then per family: FSK modes `centre_hz`, `shift_hz`, `baud`, `detail`; CIS modes `text` (one line), `snr_db`, `params` (object of strings); CROWD36 `text`, `detail`; SITOR `centre_hz`, `shift_hz`, `baud` (when measured), `snr_db`, `detail` (tone polarity). Sent when something changes |
| `stats` | `mode`, `good`, `bad` (characters, frames or symbols read, and failed checks), at the end; a last line with `samples` and `seconds` |

## What each mode does: IDENTIFIES or DECODES

The upstream projects say plainly how far they go; this helper claims no more.

| Mode (`--modes` name) | Native rate | Result | What it reads |
|---|---|---|---|
| ARQ-E (`ARQ-E`) | 24 kHz | DECODES text | 7-bit words (odd number of marks), the 4- or 8-word inversion cycle, speed, shift and alignment found automatically, ITA2-P text. Control characters are counted, not named: OnAir's note says "control characters counted, not named (their codes are not published)" |
| ARQ-N (`ARQ-N`) | 24 kHz | DECODES text | as ARQ-E without the inversion cycle |
| AUTOSPEC, SPREAD 11/21/51 | 24 kHz | DECODES text | 10-bit Bauer code, single-error correction, speed found automatically; SPREAD depth detected |
| G-TOR (`G-TOR`) | 24 kHz | DECODES text / hex | 100, 200, 300 Bd frames, Golay and data forms, CRC; ASCII frames as text, Huffman frames as hex |
| EFR (`EFR`) | 24 kHz | DECODES telegrams | DCF49 / DCF39 200 Bd, DIN 19244 frames with checksum; the payload (Semagyr-TOP, Versacom) stays hex |
| Piccolo Mk VI (`PICCOLO-MK6`) | 24 kHz | DECODES text | 6-tone pairs to ITA2 |
| Coquelet-8 (`COQUELET-8`) | 24 kHz | DECODES text | 8 tones 773 to 960 Hz, 37.5 / 50 / 75 ms, pairs to ITA2 (table from Wavecom via OnAir) |
| Coquelet-13 (`COQUELET-13`) | 24 kHz | DECODES text | added in this helper: 13 tones 812 to 1172 Hz every 30 Hz, 75 ms, idle tone 1052 Hz; group I tones carry ITA-2 bits 3 to 5 and group II tones bits 1 and 2 (Crypto Museum's table). Coquelet-80: not available |
| CIS-36-50 BEE, Rivet's decoder (`CIS-36-50`) | 8 kHz | DECODES the framing, characters as hex | **from Rivet** (`CIS3650.java`). Only 50 Bd with a 200 Hz shift (the 1 bit is the lower tone), alternating sync, 44-bit start word with 21 ones (shown as hex), 70-bit session key twice, 7-bit 3-of-7 characters as `0x..`, end of message `1110111` three times, per-message character and error counts. The traffic is encrypted and nothing is decrypted |
| CIS-36-50 / CIS-50-50 BEE, OnAir's decoder (`CIS-36-50-ONAIR`) | 12 kHz | DECODES the framing, characters as hex | **from OnAir**, a separate implementation: 2-FSK at 36 / 50 / 75 / 100 / 150 Bd with shift and centre found from the spectrum, either polarity, follow-on messages. The start word itself is not published, only its weight is checked |
| CIS-14 AMOR (`CIS-14`) | 12 kHz | frames shown RAW | 96 Bd, 14-bit frames aligned from their period; fields in hex. The parity rule and the state codes are not published, so they are not checked |
| CIS-12 MS5 (`CIS-12`) | 12 kHz | physical layer only | pilot, 12 carriers, symbol clock, PSK order, phase steps (shown as digits). No frame format is published |
| CIS-36 MFSK-36 (`CIS-36`) | 12 kHz | IDENTIFIES only | tone count, 40 Hz spacing, 10 / 20 / 40 Bd. No frame format is published |
| CROWD36 (`CROWD36`) | 8 kHz | DECODES characters (Rivet's reading) | 34 tones 1410 to 2730 Hz in 40 Hz steps at 40 Bd; sync tone hunt, symbol timing, tone to character with the letters and figures shifts, Rivet's tables. See the limits below |
| CIS OFDM HDR CIS-45 / 60 / 93 (`CIS-OFDM-HDR`) | 12 kHz | IDENTIFIES only | OFDM symbol timing, carrier count, spacing, PSK order |
| CIS-128 (`CIS-128`) | 12 kHz | IDENTIFIES only | about 128 OFDM carriers at about 21 Bd |
| CIS-3000 (`CIS-3000`) | 12 kHz | IDENTIFIES only | one 8-PSK carrier at 3000 Bd. See the limits below |
| SITOR-B / FEC (`SITOR-B`) | 24 kHz | DECODES text | CCIR 476, 100 Bd, 170 Hz, either tone polarity, the 4-character time diversity, FSK pair found anywhere in the passband; no ZCZC framing |
| SITOR-A / AMTOR ARQ (`SITOR-A`) | 24 kHz | DECODES text | monitor of both stations, repeats dropped, selective call, control signals as events |

Not in this helper, and why:

- **FEC-A.** OnAir has no decoder of the on-air mode called FEC-A. The only "FEC-A" in OnAir is in
  `core/src/hfdig/hfdig_sitor.cpp`: a word-level class (no demodulator behind it) whose "FEC-A" setting accepts a word
  only when the same word comes again some words later. That is the time diversity SITOR-B already uses on the air and
  the SITOR-B decoder above does it. Wavecom describes its FEC-A as a different mode (a convolutional code with
  interleaving, 14-bit code words); neither OnAir nor Rivet has it, so it is not decoded. (Wavecom's text was read
  only through search-result excerpts; its manual pages were not retrievable.)
- **Twinplex ARQ.** OnAir has nothing for it. A description exists (sigidwiki "TWINPLEX", hoka.it, the 1952
  "Electrical Communication" article) but the sources disagree on whether it is an ARQ system, and no decoder source
  was found on GitHub or elsewhere.
- **PACTOR-I to IV.** OnAir has them (`core/src/pactor`, `core/src/pactorhf`) but they are tied to OnAir's engine
  (its audio, tuning and listening classes) and were not ported. OnAir's own note: PACTOR-II, -III and -IV
  "channel coding, framing and compression are not published, and OnAir does not reverse engineer proprietary
  protocols"; those modes measure carriers and phase steps and produce no text.
- **Coquelet-80:** still blocked. Neither OnAir nor Rivet has it and no description was found (searches in UPSTREAM.md). Coquelet-13 is NOT blocked: Crypto Museum publishes its tones and bit table (in a figure), which is what the decoder uses.
- Piccolo Mk XII, CIS-81 and the other CIS table entries: no tone table or frame format is published.

## Which upstream each decoder comes from

One upstream per decoder, never mixed inside a decoder: OnAir for everything except two. **Rivet**: CROWD36 and CIS-36-50 (`CIS-36-50`).
Coquelet-13 is OnAir's tone-pair decoder with a table added from Crypto Museum (README / UPSTREAM.md); it uses no Rivet code.

## What has been tried on real signals

Per mode: **OnAir says the decoders it wrote were tried on SIMULATED signals only** (its docs: "every decoder here has been validated on
simulated signals only; no over-the-air recording was available"; the CIS page: "Every check ran on the generators ... No real recording of any of
these signals has been through the decoders"). That covers ARQ-E, ARQ-N, AUTOSPEC, SPREAD, G-TOR, EFR, Piccolo, Coquelet-8, `CIS-36-50-ONAIR`, CIS-14, CIS-12,
CIS-36, OFDM HDR, CIS-128, CIS-3000 and SITOR-A. The two Rivet decoders (CROWD36, `CIS-36-50`) come from a project whose author says (in the code) the CIS 36-50 format came from information
received, and OnAir's docs credit Rivet for "the tone polarity of real recordings" of BEE; nothing here proves that, and this helper tried them on generated signals only. Coquelet-13 is new
code on a published table, generated signals only. **SITOR-B** is the one mode run on a real off-air recording here (sigidwiki; it read `ZCZC` ... `QTC LIST`).

## ARQ-E control signals: UNVERIFIED, not implemented

The ARQ-E control signals RQ, alpha and beta are not decoded or named by either upstream. The values 1110000 / 1000110 / 1001001 that a Wikipedia
article (citing Wiesner 1975) gives for them are UNVERIFIED against ITU-T F.342-2 and are not in this helper; OnAir counts the control words and leaves them unnamed.

## Limits (read before you rely on it)

- **OnAir says its decoders in this list were validated on simulated signals only** ("every decoder here has been
  validated on simulated signals only; no over-the-air recording was available"). The same holds here, with one exception:
  SITOR-B was run on a real off-air recording and read it (`ZCZC`, `CQ DE OST QTC LIST ... IN FEC MODE`).
- **CROWD36** follows Rivet, whose character tables are empirical and unexplained; there is no ground truth in this
  repository. It reads 1.28 s behind the audio (Rivet's buffer design), so its `t` values lag. The text includes the
  sync tones: they are tone 24 (`I`) alternating with a lower tone, which prints as `I` and a line break.
  Rivet's CROWD36 and OnAir's "CIS-36 (MFSK-36)" may or may not be the same signal; this helper runs both.
- **CIS-3000** is identified when complex baseband is fed to the decoder directly (unit test), but a generated CIS-3000 signal sent
  through the real-audio path of this helper was NOT identified (the decoder reported "no CIS-3000 match"). The cause
  was not found: unverified whether real audio of the mode would be identified.
- With every mode on at once the decoders see signals that are not theirs and may print `found` states; the status line
  is a measurement, the `text` events are what to trust.
- No decryption, no key recovery, no network, no telemetry.

## Build and test

```
cmake -S hfmodes -B <build> -G "Visual Studio 17 2022" -A x64
cmake --build <build> --config Release --target hfmodes --parallel 4
ctest --test-dir <build> -C Release        # builds the generator and the unit test too
```

The test signals are generated by `hfmodes_gen` (OnAir's own generators plus a CROWD36 and a SITOR-B generator);
see UPSTREAM.md. `real_sitorb_tfc` needs the sigidwiki recording and ffmpeg and is skipped without them.
