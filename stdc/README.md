# stdc: Inmarsat-C (STD-C) signalling helper

A standalone command-line decoder for the Inmarsat-C TDM channel (BPSK, 1200 symbols per second). It runs the whole
receive chain (carrier and clock recovery, unique-word frame sync, de-permutation, de-interleaving, Viterbi,
descrambling, frame and packet parsing) of [cropinghigh/inmarsatc](https://github.com/cropinghigh/inmarsatc) and
prints **one JSON line per broadcast or signalling packet**. Licence: GPL-3.0 (see `LICENSE`, `UPSTREAM.md`).
It receives only. It decrypts nothing. It has no network code.

## What it reports (and what it does not)

Packet descriptors decoded, with the names the parser gives them:

| Descriptor | Name | Fields |
|---|---|---|
| 0x7D | Bulletin Board | network version, sat/LES (NCS), signalling channel, channel type, status flags, services, random interval, frame time |
| 0x6C | Signalling Channel | services, uplink MHz, 28 TDM slot values |
| 0x08 | Acknowledgement Request | sat/LES, logical channel, uplink MHz |
| 0x27 | Logical Channel Clear | terminal id, sat/LES, logical channel |
| 0x2A | Inbound Message Ack | terminal id, sat/LES, logical channel |
| 0x81 | Announcement | terminal id, sat/LES, logical channel, downlink MHz, presentation |
| 0x83 | Logical Channel Assignment | terminal id, sat/LES, status, logical channel, frame length, duration, downlink and uplink MHz, frame offset |
| 0x92 | Login Ack | length, downlink MHz, 3-byte id, LES list (stations with services and downlink) |
| 0xAB | LES List | LES list length, station count, stations with services and downlink MHz |
| 0xA3 | Individual Poll | terminal id, sat/LES, **size** of the short message |
| 0xA8 | Confirmation | terminal id, sat/LES, **size** of the short message |
| 0xAA | Message | sat/LES, logical channel, packet number, **size** of the data |
| 0xB1, 0xB2 | EGC double header 1, 2 | message type and service name, priority, repetition, message id, packet number, presentation, address, **size** of the text |
| 0xBD, 0xBE | Multiframe Packet Start / Continue | inner descriptor and length; when a chain completes, the reassembled packet is printed (with `"reassembled":true`) |
| 0x91, 0x9A, 0xA0, 0xAC, 0xAD | Distress Alert Ack, Enhanced Data Report Ack, Distress Test Request, Request Status, Test Result | name and length only (upstream has no decoder for them) |
| anything else | Unknown | descriptor and length |

Only packets whose CRC is valid are printed; the number that failed is in the `stats` line (`packets_bad`).

**Left out on purpose:** message text of any kind (short messages of 0xA3 / 0xA8, data of 0xAA), and the text of EGC
broadcasts (0xB1 / 0xB2 / 0xBE; FoxSDR's own EGC plugin decodes those). For packets addressed to one terminal only the type,
length and addressing fields are printed. `--hide-ids` replaces terminal identifiers by run-local pseudonyms (`"#1"`, `"#2"` ...).

## Running it by hand

Input is **mono audio at 48000 Hz** (the upstream demodulator's form: USB audio with the signal carrier somewhere between
500 and 4500 Hz; it searches that range, starting at 2600 Hz) or I/Q at 48000 Hz:

```
stdc-helper.exe --foxsdr-helper 1 --format s16  --rate 48000 [options] < audio.s16      (also f32)
stdc-helper.exe --foxsdr-helper 1 --format cs16 --rate 48000 [options] < iq.cs16        (also cf32)
stdc-helper.exe --foxsdr-helper 1 --format sym  --rate 1200  [options] < symbols.bin    (one byte, 0 or 1, per symbol)
stdc-helper.exe --foxsdr-helper-info                                                    (the manifest)
```

I/Q is shifted up by `--iq-shift` Hz (default 2500) and its real part used as audio, so centre the I/Q on the
signal (within about a kilohertz). Options: `--hide-ids`, `--frames` (one `frame` line per decoded frame), `--lo-freq`,
`--hi-freq`, `--cent-freq` (demodulator search range and start), `--tolerance N` (unique-word errors allowed, default 9).
Other sample rates are refused (exit 2): the helper does not resample.

Real output, from the sigidwiki "Inmarsat-C TDM EGC" recording (I/Q, 48 kS/s, 49 s) with `--hide-ids`:

```
{"helper":"stdc","version":"1.0.0","protocol":1,"licence":"GPL-3.0","source":"https://github.com/cropinghigh/inmarsatc","upstream":"inmarsatc cda1242 (2023-07-10)","format":"cs16","rate":48000,"hide_ids":true}
{"t":16.700,"type":"packet","frame":5987,"descriptor":"0x7D","name":"Bulletin Board","length":14,"stage":"complete","reassembled":false,"uncertain":false,"ber":0,"networkVersion":109,"sat":1,"satName":"Atlantic Ocean Region East (AOR-E)","lesId":44,"lesName":"144, NCS",...
{"t":16.700,"type":"packet","frame":5987,"descriptor":"0xA3","name":"Individual Poll","length":14,"stage":"partial","reassembled":false,"uncertain":false,"ber":0,"mesId":"#1","sat":1,"satName":"Atlantic Ocean Region East (AOR-E)",...
```

Events: `packet` (above), `frame` (with `--frames`), `stats` (every 30 s of input and once at the end with `"final":true`).
`t` is seconds of input at the end of the 5000-symbol chunk in which the frame completed (steps of 4.17 s); `frame`
is the network frame number (8.64 s per frame).

## Building and testing

Visual Studio 2022, CMake: `cmake -S stdc -B build -A x64 && cmake --build build --config Release && ctest --test-dir build -C Release`.
The tests use `stdc-gen`, an Inmarsat-C encoder written from the constants the upstream decoder holds (see `test/stdc_gen.cpp`).
