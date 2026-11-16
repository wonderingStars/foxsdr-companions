# Pipe protocol, version 1

Every program in this repository is a complete, standalone command-line program. It reads raw samples (or text lines)
on standard input and writes JSON Lines on standard output. It links nothing from any host program, and any program can
start it as a separate process and pipe data into it. FoxSDR does exactly that, and so can anything else.

## Starting a helper

`helper.exe --foxsdr-helper 1 --format <fmt> --rate <Hz> [--centre <Hz>] [helper options]`

- `<fmt>`: `s16` (mono audio, little-endian 16-bit), `f32` (mono audio, float), `cs16` or `cf32` (interleaved I/Q).
  Some helpers also accept other formats; each helper's README and `HOST-INTERFACE.md` list what it accepts.
- stdin: raw samples in that format and nothing else, until end of file. End of file means stop and exit 0.
- stdout: UTF-8 JSON Lines, one object per line, flushed per line. The first line is the hello:
  `{"helper":"<name>","version":"<x.y.z>","protocol":1,"licence":"GPL-3.0-or-later","source":"<url>"}`
  then events: `{"t":<seconds since start, from the sample count>,"type":"<event>", ...fields}`.
  Reserved types: `text` (field `text`), `status` (field `text`), `stats` (free fields). A helper may define more;
  a reader ignores what it does not know. Binary payloads (pictures, audio) are not part of version 1.
- stderr: human-readable log lines; never parsed.
- Nothing flows from the helper back to the program that started it: no tuning requests, no calls.
- `helper.exe --foxsdr-helper-info` prints the manifest JSON and exits 0.

## Manifest

`<name>.helper.json` holds: name, version, licence, source URL, executable name, input (`audio` or `iq`), format,
rate (fixed, or minimum and maximum), and the list of event types with their field names.

## Rules every helper follows

Receive only. No decryption. No network access. No telemetry. No window, no dialog, no console. Exits when stdin
closes. Deterministic on a given input file, so a test can pin its output. The upstream code is changed as little as
possible and every change is listed in the folder's `UPSTREAM.md`.

## Conformance

`tools/conformance.py` checks these points for every helper: the manifest, the hello line, line endings, the exit
code and exit time at end of input, that no window is opened, that a wrong `--format` is refused, and that the
executable imports no network library.
