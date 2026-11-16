# trunkfmt

A FoxSDR GPL helper (pipe protocol version 1, see `../docs/PIPE-PROTOCOL.md`) that decodes, from FM-demodulated audio:

* **LoJack LJ1200** - tower messages (function, 28-bit transponder address, reply code, CRC-16 checked and
  single-bit corrected) and transponder-test messages (no CRC upstream).
* **Tait 1200** - CCDI GPS position messages and ANI (selcall) from/to identities.
* **Fleetsync II** - ANI, acknowledge, status (with fleet extension) and GPS location reports, per-block CRC-15.
* **MPT-1327** - control and traffic channel codewords (normal and French sync), message types, idents, short data
  messages (binary, ASCII, BCD, telex).
* **LTR**, **LTR-Net** and **Passport** - 300 baud sub-audible signalling words (outbound and inbound); only on request
  with `--decoders ltr,ltrnet,passport`.

and, from text lines holding already-extracted DMR control signalling, in a second mode:

* **Motorola Connect Plus** CSBKs (11 opcodes) and the two Connect Plus Short LC messages.

It is a C++ port of the Java decoders in [sdrtrunk](https://github.com/DSheirer/sdrtrunk) (GPL-3.0-or-later). It is a
complete program on its own: it needs nothing from FoxSDR, and works with any program that can pipe samples into it.
Licence: GPL-3.0-or-later, see `LICENSE`. What was taken and changed: `UPSTREAM.md`.

## Build (Windows, Visual Studio 2022, CMake)

```
cmake -S trunkfmt -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

`trunkfmt_gen.exe` (test signal generator, built with it) writes the test signals; `ctest` runs it.

## Run by hand

Audio mode - mono FM-demodulated audio (the discriminator output, not the radio's de-emphasised speaker audio if you can
avoid it), signed 16-bit little-endian (`s16`) or 32-bit float (`f32`), 8000 Hz recommended (4000..192000 accepted):

```
trunkfmt --foxsdr-helper 1 --format s16 --rate 8000 < recording.raw
trunkfmt --foxsdr-helper 1 --format s16 --rate 8000 --decoders lj1200 --no-unchecked < recording.raw
```

Options: `--decoders lj1200,tait,fleetsync2,mpt1327,ltr,ltrnet,passport` (default: the first four; the sub-audible
family `ltr`, `ltrnet`, `passport` shares the audio with the others but needs the discriminator signal with its
sub-audible part, so it is off unless named; `ltr` and `ltrnet` read the same words differently, name one),
`--mpt-french` (French MPT-1327 sync words), `--invert` (swap mark and space, upstream `Output.INVERTED`),
`--no-unchecked` (drop the event types that upstream cannot CRC-check: `lj1200_transponder`, `tait_gps`, `tait_ani`).

Connect Plus mode - text on stdin, one message per line:

```
trunkfmt --foxsdr-helper 1 --format csbk-hex < csbks.txt
csbk 83 06 12 D6 87 00 10 E1 28 42 70 7E        # 12 bytes: CSBK with its 16-bit CRC (24 hex digits)
csbk 8306 12D6 8700 10E1 2842                    # 10 bytes: no CRC field (20 hex digits), crc reported "none"
slc A0640500023                                  # Short LC: 36 bits + CRC-8 (11 hex digits), or 9 digits without CRC
```

Spaces, `:`, `-` and `_` inside the hex are ignored, `#` starts a comment, blank lines are skipped, a line without a
keyword is a `csbk`. At most 20 `parse_error` lines are printed per run (the final `stats` line counts all of them).

Output is JSON Lines on stdout, flushed per line; the first line is the hello line, the last is a `stats` line.
`--foxsdr-helper-info` prints the manifest (`trunkfmt.helper.json`).

## Events

| type | when | fields |
| --- | --- | --- |
| `lj1200` | tower message with a passing or single-bit-corrected CRC | `crc` (`pass`/`corrected`), `function`, `function_code`, `address` (7 hex digits), `reply_code`, `site` (only for function `1Y-SITE ID`), `vrc`, `lrc`, `crc_field`, `msg` (20 hex digits, 80 bits) |
| `lj1200_transponder` | sync 0x2AD5 seen (**no CRC exists upstream**) | `crc`:`unchecked`, `crc_field`, `msg` |
| `fleetsync` | Fleetsync II ANI / acknowledge / page / unknown, first block valid | `crc` (one character per block: `*` pass, `C` corrected), `kind`, `from` (`FFF:IIII`), flags `emergency`, `lone_worker`, `paging`, `acknowledge`, `ani`, `msg` |
| `fleetsync_status` | Fleetsync II status | as `fleetsync` plus `to`, `status` (0..127) |
| `fleetsync_gps` | Fleetsync II location report (8 blocks) | as `fleetsync` plus `to`, `lat`, `lon` (upstream always assumes north / west), `heading`, `speed`, `gps_time`, `gps_year_field`, `gps_month`, `gps_day` |
| `mpt1327` | MPT-1327 codeword with a valid first block | `sync` (`control`/`traffic`), `message` (`GTC`, `ALH`, `ACK`, `HEAD_PLUS2` ...), `description`, `crc` (`[*C---]`: one character per block checked), `prefix`, `ident1`, `ident2`, `channel`, `from`, `to`, `system`, `text` (upstream's decoded line, incl. short data messages), `msg` |
| `ltr` | LTR Standard word with a valid CRC (needs `--decoders ltr`) | `direction` (`osw`/`isw`), `message` (`CALL`, `CALL_END`, `IDLE`, `UNKNOWN`), `crc` (`pass`/`pass-inverted`), `area`, `lcn`, `home`, `group`, `free`, `talkgroup` (`HH-GGG`), `msg` |
| `ltrnet` | LTR-Net word with a valid CRC (needs `--decoders ltrnet`) | `direction`, `message` (`OSW_CALL_START`, `OSW_SYSTEM_IDLE`, `OSW_SITE_ID`, `OSW_TRANSMIT_FREQUENCY_HIGH`, `ISW_REQUEST_ACCESS` ...), `crc`, type specific fields (`talkgroup`, `area`, `lcn`, `home`, `group`, `free`, `channels`, `frequency_hz` once both halves have been seen, `neighbor`, `rank`, `unique_id`, `site`, `esn`), `text`, `msg` |
| `passport` | Passport word with a valid (or repaired) CRC (needs `--decoders passport`) | `message` (`SY_IDLE`, `CA_STRT`, `CA_ENDD`, `CA_PAGE`, `ID_RDIO`, `ID_TGAS`, `DA_STRT`, `RA_REGI`, `UN_KNWN`), `dcc`, `site`, `lcn`, `free`, `type_number`, `group` / `radio_id`, `lcn_freq_hz`, `free_freq_hz` (0 until four identical idle words fixed the site band), `site_band`, `neighbor_band`, `neighbor_freq_hz`, `corrected`, `text`, `msg` |
| `tait_gps` | CCDI GPS sync seen (**no CRC upstream**) | `from`, `to`, `lat`, `lon`, `gps_time` (HH:MM:SS), `day`, `field_388_391`, `speed_kph`, `size`, `size2`, `msg` |
| `tait_ani` | selcall sync seen (**no CRC upstream**) | `from`, `to`, `size`, `size2`, `msg` |
| `connectplus_csbk` | CSBK with FID 6 and a known opcode | `decoded`:true, `fid`, `vendor`, `csbko`, `name`, `last_block`, `protected`, `crc` (`ok`/`corrected`/`error`/`none`), opcode fields (below), `msg` |
| `connectplus_slc` | Short LC | `crc`, `slc_opcode`, `decoded`, `name`, `network`, `site`, `msg` |
| `csbk` | any other CSBK | `decoded`:false, `fid`, `vendor`, `csbko`, `crc`, `msg` |
| `parse_error` | an unparseable text line | `line`, `text` |
| `stats` | end of input | counters |

`t` is seconds since the start of the input, from the input sample count, at the sample where the message completed
(three decimals). Text mode has no samples: `t` is 0 and `line` is the input line number.

Connect Plus opcode fields (FID 6, `csbko` value): 1 neighbour report (`neighbors` list of site numbers, `unk`);
3 voice channel user (`source`, `talkgroup`, `lsn_field`, `lcn`, `timeslot`, `lsn`, `unk`); 6 data channel grant
(`target`, `lsn_field`, `lcn`, `timeslot`, `lsn`, `unk`); 10 OTA announcement (`message_type`, `message_type_name`,
`version`, `lsn_field`, `data_lcn`, `data_timeslot`, `data_lsn`, `unk`); 12 terminate channel grant (`target`, `unk1`..`unk5`);
16 CSBKO 16 (`source`, `target`, `unk`); 17 registration request and 18 registration response (`source`, `target`, `unk`);
24 talkgroup affiliation (`radio`, `talkgroup`, `unk`); 28 enhanced data revert window announcement (`window`,
`superframe`, `reserved_for` when non-zero); 29 enhanced data revert window grant (`target`, `superframe`, `window`).
Fields called `unk` are fields sdrtrunk itself does not understand.

## What is not here

No voice (sdrtrunk uses the separate JMBE codec library for that and half-rate voice is patent-limited, so it is not
built). Not ported from sdrtrunk: the standard DMR CSBK / link-control layer (the FoxSDR DMR plugin has it), the
Capacity Plus / Capacity Max / Hytera CSBKs, the DMR framing and BPTC decoding that produce the CSBK bytes, and
everything that is sdrtrunk's user interface, channel management, audio, recording or talkgroup alias database.
