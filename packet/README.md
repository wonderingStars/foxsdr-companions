# packet - AX.25, FX.25 and IL2P receiver (FoxSDR GPL helper)

A standalone command-line program that reads mono audio on standard input and prints one JSON line per decoded packet
frame. It is Dire Wolf's receive chain (https://github.com/wb2osz/direwolf, version 1.8.2 development, commit `eda1383f5fa9`)
behind the FoxSDR helper pipe protocol version 1 (`docs/PIPE-PROTOCOL.md`). It works without FoxSDR: any program can pipe audio in.

Licence: GNU GPL version 2 or (at your option) any later version, as Dire Wolf. See `LICENSE`, `UPSTREAM.md`.

## What it decodes

| Modem (`--baud`) | Signal | Tones / line code |
|---|---|---|
| `1200` (default) | AFSK, Bell 202 | 1200 / 2200 Hz |
| `300` | AFSK, HF SSB packet | 1600 / 1800 Hz |
| `9600` | G3RUH / K9NG scrambled baseband FSK | (needs the discriminator audio, DC-coupled) |

On every modem, three frame formats are decoded at the same time, and each is reported with its own `type`:
`ax25` (plain HDLC AX.25 frames), `fx25` (AX.25 inside an FX.25 Reed-Solomon codeblock; 16, 32 or 64 check bytes) and
`il2p` (Improved Layer 2 Protocol: scrambled, Reed-Solomon coded, either polarity, max-FEC or automatic parity). When
FX.25 or IL2P repaired the frame, `corrected` says how many bytes (symbols) were wrong.

**Sample rates.** Audio is mono at any rate from 8000 to 192000 Hz (Dire Wolf's own limits: `MIN_SAMPLES_PER_SEC`,
`MAX_SAMPLES_PER_SEC`). Tested in this build, generated signals: 300 and 1200 baud at 8000, 11025, 22050, 44100, 48000 and
192000 Hz; 9600 baud at 19200, 44100, 48000 and 192000 Hz. 9600 baud needs at least 19200 Hz (two samples per bit; at 16000 Hz
nothing decodes, so the helper refuses it). The default in the manifest is 48000.

**Not exposed.** Dire Wolf's 2400 / 4800 baud PSK modems, AIS and EAS demodulators are linked (the code calls them) but are not
selectable. Everything that transmits or talks to a network is absent: see `UPSTREAM.md`.

## Running it by hand

```
packet-helper.exe --foxsdr-helper 1 --format s16 --rate 44100 [--baud 300|1200|9600] [options] < audio.raw
packet-helper.exe --foxsdr-helper-info          (prints packet.helper.json)
```

`audio.raw` is raw samples without a header: `--format s16` is little-endian signed 16-bit, `--format f32` is little-endian
32-bit float in -1..1. To turn a mono 16-bit WAV file into one, build the test tools (below) and run
`rawtool wav2raw in.wav out.raw`, or let any converter write headerless `s16le`. With Dire Wolf's own generator:

```
gen_packets -B 1200 -X 16 -r 44100 -o fx.wav messages.txt      (messages in TNC2 monitor format, one per line)
rawtool wav2raw fx.wav fx.raw
packet-helper.exe --foxsdr-helper 1 --format s16 --rate 44100 < fx.raw
```

Options (all optional): `--baud N` (above); `--profile P` AFSK demodulator letters from Dire Wolf (`A`, `B`, `D`, `E`, with
`+` or `-` at the end; default `A+`; several letters run several demodulators side by side); `--fix-bits N` (0..4: try to repair
plain AX.25 frames whose CRC fails by flipping bits, as Dire Wolf's `-F`; default 0; FX.25 and IL2P have their own FEC);
`--sanity ax25|aprs|none` (what a plain AX.25 frame must look like; default `ax25`; Dire Wolf's own default is `aprs`);
`--verbose` (Dire Wolf's diagnostic text on stderr); `--centre HZ` (accepted, ignored). A bad option, `--format`, `--rate` or
`--baud` makes the program exit with code 2 and a message on stderr before it prints anything on stdout.

Exit code 0 at end of input, also for empty input (you get the hello line and the stats line).

## Output (stdout, one JSON object per line)

```
{"helper":"packet","version":"1.0.1","protocol":1,"licence":"GPL-2.0-or-later","source":"https://github.com/wb2osz/direwolf",
 "upstream":"Dire Wolf 1.8.2 (eda1383f5fa9...)","format":"s16","rate":44100,"baud":1200,"modem":"afsk","profile":"A+","sanity":"ax25","fix_bits":0}
{"t":1.0991,"type":"il2p","text":"W1ABC-9>APRS,WIDE1-1,WIDE2-1:!4237.14N/07120.83W>Dire Wolf test ...<0x0a>","src":"W1ABC-9",
 "dst":"APRS","via":["WIDE1-1","WIDE2-1"],"heard":"W1ABC-9","control":3,"pid":240,"frame":"UI cc=11, p/f=0","aprs":true,
 "info_len":63,"info_hex":"21343233...","info_text":"!4237.14N/07120.83W>Dire Wolf ...","corrected":0,"level":50,"mark":14,"space":14,"subchan":0,"slice":0}
{"t":2.6761,"type":"stats","samples":118017,"frames":2,"ax25":0,"fx25":0,"il2p":2,"fx25_corrected":0,"il2p_corrected":0}
```

| Event `type` | Fields |
|---|---|
| `ax25`, `fx25`, `il2p` | `t` seconds from the start, from the sample count (the time the frame was released, a few bit times after its end); `text` the monitor line Dire Wolf prints (reserved field): `SRC>DST,VIA*:` then the information field with control characters shown as `<0x..>`; `src`, `dst`, `via` (list; a trailing `*` marks the last digipeater that repeated it), `heard` (station heard: the last used digipeater, else the source); `control` byte (decimal); `pid` (decimal, only for I and UI frames); `frame` Dire Wolf's frame-type description; `aprs` (true for UI frames with PID 0xF0); `info_len`, `info_hex` (information field, lower-case hex), `info_text` (same bytes, printable ASCII, anything else as `.`); `corrected` (`fx25` and `il2p` only: bytes or symbols the Reed-Solomon code repaired); `fix` (`ax25` only, and only with `--fix-bits` > 0: which bit-flip repair was used, `NONE` for none); `level` (audio level, percent), `mark`, `space` (the two tone levels for AFSK; the positive and negative signal peaks for 9600 baud), `subchan`, `slice` (which of the parallel demodulators won). Addresses and `text` are omitted for frames that are not AX.25. |
| `stats` | printed once at end of input: `samples`, `frames`, `ax25`, `fx25`, `il2p` (counts), `fx25_corrected`, `il2p_corrected` (totals) |

`text` and strings are valid UTF-8 JSON: bytes that are not valid UTF-8 are written as `\u00XX`; use `info_hex` for the exact bytes.
A frame heard in both plain and FX.25 / IL2P form is reported once (Dire Wolf keeps the best copy). Output is deterministic.

## Build and test

Visual Studio 2022, x64, CMake (nothing else installed):

```
cmake -S packet -B build/packet -G "Visual Studio 17 2022" -A x64
cmake --build build/packet --config Release --parallel 4
ctest --test-dir build/packet -C Release --output-on-failure
```

`packet-helper.exe` links only KERNEL32 (the C runtime is static). The build also makes three test tools that are not part
of the helper: `gen_packets` (Dire Wolf's signal generator: it writes a WAV file, it cannot transmit), `gen_damaged` (the same
generator with a shim that flips bits in a chosen number of FX.25 / IL2P bytes) and `rawtool` (WAV to raw, noise, small
writes). `-DPACKET_TEST_TOOLS=OFF` leaves them out (the tests then need them, so run the tests with them on).
Two cases decode a real off-air recording (WA8LMF TNC Test CD v2, track 1, first 180 s, left channel) from
the samples folder (`-DFOXSDR_SAMPLES_DIR`) through `ffmpeg`; they are skipped (CTest "Not Run", needs CMake 3.29 or later for the skip) when either is
missing, and only frame counts are pinned. The other tests make every signal at run time with those tools (deterministic) and compare the helper's JSON with the files in
`test/expected/` byte for byte; `PK_UPDATE=1 ctest ...` rewrites those files after an intended change, then review the diff.

## Proven and not proven

Proven on generated signals (Dire Wolf's own modulator): every modem, the rates above, FX.25 with 16 / 32 / 64 check bytes and
IL2P (both polarities, max and automatic FEC), damaged FX.25 and IL2P frames at exactly the correction limit and one past it,
a stream with all three kinds, noisy AX.25 with and without `--fix-bits`, float input, input in 1-byte pipe writes, empty,
silent, random and noise input.

Proven on one real off-air recording (the two `real_wa8lmf_t1_*` cases above: WA8LMF TNC Test CD v2, track 1, 144.39 MHz APRS,
first 180 s, left channel, 1200 baud AFSK at 44100 Hz): the default demodulator decodes 139 frames and profile `E+` decodes 141,
all of them AX.25 (no FX.25, no IL2P on that recording). Only these counts are pinned, never any text from the recording, and
no comparison with a run of Dire Wolf itself is recorded. The recording is kept outside the repository, so the cases
are skipped when it or `ffmpeg` is missing.

Not tried: real off-air recordings at 300 baud or 9600 baud, real FX.25 or IL2P traffic, other implementations' FX.25 / IL2P
transmitters, and receiving from a live SDR.
