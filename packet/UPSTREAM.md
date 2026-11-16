# packet: where it comes from

| | |
|---|---|
| Upstream project | Dire Wolf, "software TNC for packet radio" by John Langner, WB2OSZ |
| URL | https://github.com/wb2osz/direwolf |
| Commit taken | `eda1383f5fa9d8ba3cb27f99db1d2c79494404c9` (2026-05-19, "Bump patch version to 2 in CMakeLists.txt"; version 1.8.2 development) |
| Fetched | 2026-11-12, `git clone --depth 1`, source files only. No prebuilt binary, installer or script was fetched or run. |
| Licence | GNU General Public License, version 2 **or (at your option) any later version** (SPDX `GPL-2.0-or-later`). Evidence: the `LICENSE` file is the GPL version 2 text; every `.c` file that carries a notice says "either version 2 of the License, or (at your option) any later version"; upstream `debian/copyright` lists `Files: * License: GPL-2+`. `LICENSE` in this folder is upstream's file unchanged. |
| Third-party licences inside | `fx25_init.c`, `fx25_encode.c`, `fx25_extract.c` contain Phil Karn KA9Q's Reed-Solomon code ("May be used under the terms of the GNU General Public License"); kept with its notice. `upstream/misc/*.c` are BSD-licensed string functions that upstream dual-licenses "BSD or GPL-2+" (`debian/copyright`); their notices are intact. |
| Mixed projects | None. Only Dire Wolf code is in this helper. |

## What the helper is

Dire Wolf's receive path with a new front end and back end. Upstream's test fixture `atest.c` already does exactly
"WAV file in, decoded frames out" through the real demodulators (`multi_modem_init`, `multi_modem_process_sample`,
`dlq_rec_frame`). `src/packet_main.c` is that program with the WAV reader replaced by raw samples on standard input
and `dlq_rec_frame`'s printed report replaced by one JSON line per frame. The setup code that picks the modem for a bit rate
is atest.c's (`baud < 600` is 1600/1800 Hz AFSK, `< 1800` is 1200/2200 Hz AFSK, otherwise the G3RUH scrambled baseband modem).

## Files taken, byte for byte (nothing under `upstream/` was edited)

Receive chain, linked into `packet-helper.exe` (24 `.c`, 29 `.h`, about 13,900 lines of C):
`ais.c demod.c demod_afsk.c demod_psk.c demod_9600.c dsp.c fx25_extract.c fx25_encode.c fx25_init.c fx25_rec.c hdlc_rec.c
hdlc_rec2.c il2p_codec.c il2p_scramble.c il2p_rec.c il2p_payload.c il2p_init.c il2p_header.c multi_modem.c rrbb.c fcs_calc.c
ax25_pad.c ax25_pad2.c dtime_now.c` and the headers
`ais.h audio.h ax25_pad.h ax25_pad2.h demod.h demod_9600.h demod_afsk.h demod_psk.h direwolf.h dlq.h dsp.h dtime_now.h fcs_calc.h
fsk_demod_state.h fsk_filters.h fsk_gen_filter.h fx25.h gpio_common.h grm_sym.h hdlc_rec.h hdlc_rec2.h il2p.h latlong.h
multi_modem.h ptt.h rpack.h rrbb.h textcolor.h tune.h version.h`.
`demod_psk.c` and `ais.c` are linked only because `demod.c` / `hdlc_rec2.c` call them; the helper never selects the
PSK, AIS or EAS modems (`--baud` accepts 300, 1200, 9600 only).
`external/misc/` (copied to `upstream/misc/`): `strsep.c strtok_r.c strcasestr.c strlcpy.c strlcat.c`, the string functions
upstream supplies on Windows.

Test tool only, never in the helper (7 `.c`, 4 `.h`): `gen_packets.c gen_tone.c hdlc_send.c fx25_send.c il2p_send.c dtmf.c
morse.c` and `gen_tone.h hdlc_send.h dtmf.h morse.h`. This is Dire Wolf's signal generator; it writes a WAV file and cannot
transmit (the transmit code that drives a radio, `xmit.c`, `ptt.c`, `audio*.c`, is not here).

Not taken (the helper has none of): every transmit and audio-device module (`xmit.c audio.c audio_win.c audio_portaudio.c
ptt.c cm108.c gpio_common.c tq.c`), KISS and AGW network servers and clients (`kiss*.c server.c agwlib.c nettnc.c dwsock.c`),
IGate and APRS-IS (`igate.c`), digipeater (`digipeater.c cdigipeater.c`), beaconing (`beacon.c`), GPS (`dwgps*.c`),
text-to-speech and APRStt (`tt_*.c aprs_tt.c ttcalc.c`), the APRS packet decoder and encoder (`decode_aprs.c encode_aprs.c
telemetry.c symbols.c deviceid.c latlong.c` - FoxSDR has its own), the connected-mode link layer (`ax25_link.c`), the
configuration file reader (`config.c`), `dlq.c` (the queue to the link layer), `dedupe.c`, DTMF decode, logging, `mheard.c`,
and the bundled `regex`, `geotranz` and `hidapi` libraries. There is no network code and no thread in the helper.

## What is new in this folder (GPL-2.0-or-later, same as Dire Wolf)

| File | Purpose |
|---|---|
| `src/packet_main.c` | Protocol version 1 front end, atest.c's modem set-up, the sample loop, `dlq_rec_frame` (JSON), `audio_get` / `ptt_set` / `get_input` stubs |
| `src/dw_console.c` | Replaces upstream `textcolor.c`: `dw_printf` goes to stderr only with `--verbose` (otherwise nowhere), no colours, no console calls; can capture a call's text to a buffer (used for `ax25_safe_print`) |
| `compat/dw_compat.h` | Forced include for MSVC: defines `__attribute__(x)` away, `__restrict__`, `strcasecmp`, `localtime_r`, `__builtin_popcount(ll)` |
| `compat/regex.h`, `compat/unistd.h`, `compat/getopt.h`, `compat/getopt_win.c` | Empty `regex.h` (upstream `ax25_pad.c` includes it but calls nothing), `unistd.h` stand-in, a small `getopt_long` (generator only) |
| `tools/gen_damage_shim.c`, `tools/rawtool.c` | Test tools: damage shim for the generator, signal-file utilities |
| `test/` | CTest cases, driver and pinned expectations |

## Changes to upstream behaviour (none to upstream files)

1. **No edits to any upstream file.** `cmp` against the clone is clean for every file in `upstream/` (the repository's `.gitattributes` stores text as LF, which is also what upstream's own repository holds).
2. Build-time substitutions instead of edits: `textcolor.c` is replaced by `src/dw_console.c`; `regex.h` is an empty file;
   GCC attributes are defined away; `__WIN32__` is defined on the compiler command line (upstream expects its MinGW compiler to
   define it); `MAJOR_VERSION`, `MINOR_VERSION`, `PATCH_VERSION` come from the command line as in upstream's CMake.
3. Floating point: upstream builds with GCC `-ffast-math`; this build uses MSVC's default `/fp:precise`. The decoder is
   deterministic for a given binary and input (tested: two runs byte-identical). A bit-for-bit match with an upstream GCC build was
   not checked.
4. Frames are reported when `multi_modem.c` releases them, a few bit times after the last bit. At end of input the helper feeds
   0.25 s of silence so a frame still pending is released (il2p_rec.c says: "be sure to send an extra bit to flush it out").
5. Defaults differ from `atest.c` in one place: the helper's default frame check is `--sanity ax25` (a frame must look like AX.25),
   atest's is APRS. `--sanity aprs` restores Dire Wolf's default.
6. Dire Wolf's own `exit()` calls on a bad configuration (for example an unsupported demodulator profile letter) would end the
   process; the helper validates `--profile`, `--baud`, `--rate` and their combinations first and refuses with exit code 2 and a
   message on stderr before it prints the hello line.

## Searches made

Nothing was called missing. The source was available in full from the upstream repository named in the task; no WebSearch was
needed.
