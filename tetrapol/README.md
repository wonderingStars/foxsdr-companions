# tetrapol: TETRAPOL control-channel signalling helper (GPL-2.0-only)

A standalone command-line program that decodes the **signalling** of a TETRAPOL control channel and prints one
JSON line per decoded frame or message. It is built from the decoder of
[tetrapol-kit](https://github.com/airphel/tetrapol-kit-2023) (GPL-2.0), see `UPSTREAM.md`, and follows the FoxSDR
helper pipe protocol (version 1, `docs/PIPE-PROTOCOL.md`) but needs nothing from FoxSDR: pipe samples in, read lines out.

## What it does and does not do

Does: GMSK demodulation of one channel (or takes the demodulated bits), frame synchronisation, scrambling-constant
detection (SCR 0..127), de-scrambling, de-interleaving, channel decoding with error correction, CRC-5 / FCS checks,
reassembly of multi-block messages (with the XOR parity block), the link and transport layers, and the signalling
messages the upstream code decodes (PAS 0001-3-2): system information (cell, base-station, network, location area,
scrambling, radio parameters), neighbouring cells, registration, group activation / paging / idle / reject, call setup
and connect (channel assignment, channel numbers, scrambling), group lists and compositions, causes, and the paging
(PCH) and random-access acknowledgement (RCH) lists.

Does not, by design:

* **No voice.** Voice frames are never decoded; no codec is included or linked. On a traffic channel (`--channel tch`)
  every frame is reported as `traffic frame, not decoded`.
* **No data content.** Short data, datagrams, ability management, information delivery, broadcast and similar messages
  are reported by name and length only (`data message, content not decoded`); their bytes are never parsed or printed.
* **No decryption and no keys.** Authentication challenges and call keys are not printed; the key *reference* (type and
  index, so "clear call" or "ciphered") is.
* **No raw dumps.** Upstream printed every frame and message as hex; this helper prints decoded fields only.
* Terminal and group identities can be replaced by short hashes: `--hide-ids`.

## Run it by hand

```
tetrapol-helper --foxsdr-helper 1 --format cf32 --rate 48000 < channel.cf32
tetrapol-helper --foxsdr-helper 1 --format bits --rate 8000 --no-frames < demodulated.bits
tetrapol-helper --foxsdr-helper-info
```

Input (`--format`):

| format | what stdin carries | `--rate` |
| --- | --- | --- |
| `cf32` | interleaved float32 I,Q of **one** TETRAPOL channel at baseband: carrier within about ±2 kHz of 0 Hz, band-limited to roughly 10 kHz (the radio's channel filter), amplitude not critical | 16000 ... 500000 Hz (48000 is a good choice) |
| `cs16` | the same as little-endian int16 | the same |
| `bits` | hard-decided bits as GNU Radio `digital.gmsk_demod` writes them (the input of upstream `tetrapol_dump`), one byte (0 or 1) per bit, 8000 bit/s | 8000 |

Options: `--channel cch|tch` (default `cch`), `--band uhf|vhf` (default `uhf`: the two bands interleave differently),
`--direction down|up` (default `down`), `--scr N` (default: detected from the signal), `--hide-ids`,
`--hide-ids-seed N` (default: a random salt per run; a fixed seed makes runs reproducible), `--no-frames` (no line
for every good frame), `--report-bad-frames`, `-v` / `-vv` / `-vvv` (diagnostics on stderr; with `--hide-ids` only `-v`).

Exit status: 0 at end of input, 2 for a bad command line (message on stderr, nothing on stdout), 3 if stdout closes
or the decoder cannot start.

## Output

stdout is UTF-8 JSON Lines, flushed per line. The first line is the hello:

```
{"helper":"tetrapol","version":"0.1.0","protocol":1,"licence":"GPL-2.0-only","source":"https://github.com/airphel/tetrapol-kit-2023","upstream":"tetrapol-kit-2023 09def9d","format":"bits","rate":8000,"channel":"cch","band":"uhf","direction":"down","hide_ids":false}
```

then events `{"t":<seconds>,"type":"<event>",...}`. `t` is the bit count at the end of the frame divided by 8000
(the symbol clock), so it does not depend on how the input is cut into reads. Events:

| type | meaning |
| --- | --- |
| `sync` | frame synchronisation `found` / `lost` |
| `scr` | scrambling constant detected or changed |
| `frame` | one good control-channel frame: `frame_no` (0..199 in the superframe, `null` until the broadcast channel has been seen), `state`, `syndromes`, `bits_fixed` (bits the error correction repaired); `--report-bad-frames` adds the failed ones; `--no-frames` removes them |
| `message` | one signalling message: `log_ch` (BCH, SDCH ...), `frame_no`, `addr`, `prio`, `tsap_id`, `msg` (`D_SYSTEM_INFO` ...), `class` (system, registration, call, group, data, authentication, unknown), `codop`, `len`, then the decoded fields of that message |
| `paging` | the PCH: `activation_bitmap`, the paged addresses |
| `ack` | the RCH: acknowledged addresses and negative acknowledgements (`noise`, `collision`) |
| `traffic` | a traffic-channel frame (or a voice-channel / circuit-data HDLC frame): `traffic frame, not decoded` |
| `stats` | last line: bits, frames ok / bad / corrected, messages, data messages not decoded, traffic frames, sync found / lost |

`--hide-ids` replaces `addr`, `group_id(s)`, `host_adr`, `calling_adr`, `addr_tti`, `serial_nb` and the
paging / ack lists by `h:xxxxxxxx` (the same identity gives the same hash inside one run; broadcast constants such as
`0.7.0xfff` stay readable). The upstream library's own diagnostics (`-vv`) can contain addresses, so they are not
available together with `--hide-ids`.

The same helper is exercised without the radio by `ctest` (see below).

## Build and test

```
cmake -S tetrapol -B <build dir> -G "Visual Studio 17 2022" -A x64
cmake --build <build dir> --config Release --parallel 4
ctest --test-dir <build dir> -C Release --output-on-failure
```

No dependency beyond the C runtime (upstream's glib, json-c and cmocka are not needed). `TP_UPDATE=1 ctest ...`
rewrites the pinned expectations in `test/expected/` (review the diff).

`test/tools/tpgen.c` is the test signal generator (control channel, traffic channel, damage, GMSK modulator with noise,
carrier offset, timing offset and clock error); `tpgen xcheck` compares its channel coding with upstream's own encoder.
All test signals are generated; there is no real recording in this folder.
