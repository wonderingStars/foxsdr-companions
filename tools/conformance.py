#!/usr/bin/env python3
"""Protocol conformance check for one FoxSDR GPL helper executable (pipe protocol version 1, docs/PIPE-PROTOCOL.md).

Run it with isolated mode:   python -I tools/conformance.py --exe <helper.exe> --manifest <name>.helper.json
                                                           [--dumpbin <path to dumpbin.exe>]

What it checks (every failure is printed as "FAIL <check>: <reason>"; the exit code is 0 only if nothing failed):
  manifest   the manifest file parses and has the fields PIPE-PROTOCOL.md requires; `--foxsdr-helper-info` prints ONE valid
             JSON object, exits 0, uses LF only, and equals the manifest file.
  runs       for empty input and for one second of silence in EACH input format the manifest lists (text formats get
             empty lines), the helper is started as the protocol says and: the first stdout line is the hello line
             (helper, version, protocol 1, licence, source), every stdout line is a JSON object that ends in LF alone
             (no CR), every event line has a "type", the exit code is 0, the process is gone within 2 seconds of the
             end of input, and no window of the process is seen while it runs.
  format     a wrong --format gives a non-zero exit and a message on stderr (and no window).
  attribution  if the manifest carries an "attribution" (OnAir-derived helpers), that exact text is in --version,
             --help and the hello line.
  imports    `dumpbin /imports` of the exe names none of ws2_32, wsock32, wininet, winhttp, urlmon (a helper that does
             is a finding to report; the check does not hide it).
Windows only (the helpers are Windows executables). Standard library only. Nothing is written to disk.
"""
import argparse
import ctypes
import glob
import json
import os
import re
import subprocess
import sys
import threading
import time

BAD_DLLS = ("ws2_32", "wsock32", "wininet", "winhttp", "urlmon")
BYTES_PER_SAMPLE = {"s16": 2, "f32": 4, "cs16": 4, "cf32": 8, "cu8": 2, "cs8": 2, "sym": 1, "bits": 1}
TEXT_FORMATS = ("avlc-hex", "msumr-lines", "csbk-hex")
# fixed rate of symbol formats that the manifest's audio rate does not describe
SYMBOL_RATE = {"sym": 1200, "bits": 8000}
# arguments a helper needs on top of --foxsdr-helper/--format/--rate to run at all (taken from its README / manifest)
EXTRA_ARGS = {
    "multimon": ["-a", "POCSAG1200"],
    "hfdl": ["--channel-offset", "0"],
    "fldigimodes": ["--mode", "mfsk16"],
}
CREATE_NO_WINDOW = 0x08000000
EXIT_WITHIN = 2.0

failures = []
warnings = []


def fail(check, reason):
    failures.append((check, reason))
    print("FAIL %s: %s" % (check, reason))


def warn(check, reason):
    warnings.append((check, reason))
    print("WARN %s: %s" % (check, reason))


def ok(check, detail=""):
    print("ok   %s%s" % (check, (" - " + detail) if detail else ""))


# ---------------------------------------------------------------------------------------------- process running
def _visible_windows_of(pid):
    """True if the process owns a visible top-level window right now (Windows only)."""
    if os.name != "nt":
        return False
    user32 = ctypes.windll.user32
    found = []
    proto = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    def cb(hwnd, _lparam):
        p = ctypes.c_ulong(0)
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(proto(cb), 0)
    return bool(found)


def run(exe, args, data=b"", timeout=60.0):
    """Start the helper, feed `data` on stdin, close it, and collect everything.
    Returns dict(rc, out, err, seconds_after_eof, window)."""
    flags = CREATE_NO_WINDOW if os.name == "nt" else 0
    p = subprocess.Popen([exe] + args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         creationflags=flags)
    res = {"out": b"", "err": b"", "t_eof": None, "window": False}

    def reader(stream, key):
        res[key] = stream.read()

    def writer():
        try:
            for i in range(0, len(data), 65536):
                p.stdin.write(data[i:i + 65536])
            p.stdin.flush()
        except OSError:
            pass  # the helper closed its end early: judged by its exit code and output
        try:
            p.stdin.close()
        except OSError:
            pass
        res["t_eof"] = time.monotonic()

    threads = [threading.Thread(target=reader, args=(p.stdout, "out"), daemon=True),
               threading.Thread(target=reader, args=(p.stderr, "err"), daemon=True),
               threading.Thread(target=writer, daemon=True)]
    for t in threads:
        t.start()
    deadline = time.monotonic() + timeout
    while p.poll() is None and time.monotonic() < deadline:
        if _visible_windows_of(p.pid):
            res["window"] = True
        time.sleep(0.01)
    t_exit = time.monotonic()
    if p.poll() is None:
        p.kill()  # only the process started here
        p.wait()
        res["timeout"] = True
    for t in threads:
        t.join(5)
    res["rc"] = p.returncode
    res["after_eof"] = None if res["t_eof"] is None else max(0.0, t_exit - res["t_eof"])
    return res


# ---------------------------------------------------------------------------------------------------- manifest
def manifest_formats(m):
    f = m.get("formats", m.get("format"))
    if f is None:
        return []
    return [f] if isinstance(f, str) else list(f)


def manifest_rate(m, fmt):
    """The rate to pass for `fmt`, or None for text formats."""
    if fmt in TEXT_FORMATS:
        return None
    if fmt in SYMBOL_RATE:
        return SYMBOL_RATE[fmt]
    r = m.get("rate")
    if isinstance(r, bool):
        return None
    if isinstance(r, (int, float)):
        return int(r)
    if isinstance(r, dict):
        for k in ("default", "recommended", "preferred", "native", "min"):
            if isinstance(r.get(k), (int, float)):
                return int(r[k])
    return None


def check_manifest(m, path):
    need = ("name", "version", "licence", "source", "executable", "input", "rate", "events")
    for k in need:
        if k not in m:
            fail("manifest.field", "%s lacks \"%s\"" % (os.path.basename(path), k))
    if "format" not in m and "formats" not in m:
        fail("manifest.field", "%s lacks \"format\" (or \"formats\")" % os.path.basename(path))
    if m.get("input") not in ("audio", "iq"):
        warn("manifest.input", "input is %r, PIPE-PROTOCOL.md names audio or iq (text-line helpers are an extension)" % m.get("input"))
    if not isinstance(m.get("events"), (dict, list)) or not m.get("events"):
        fail("manifest.events", "events must list the event types with their fields")


def lines_ok(check, out, errors):
    """Every line: UTF-8, LF only, a JSON object. Returns the parsed objects."""
    objs = []
    if not out:
        return objs
    if not out.endswith(b"\n"):
        errors.append("output does not end with LF")
    if b"\r" in out:
        errors.append("output contains CR (lines must end in LF alone)")
    try:
        text = out.decode("utf-8")
    except UnicodeDecodeError as e:
        errors.append("stdout is not UTF-8: %s" % e)
        return objs
    for n, line in enumerate(text.split("\n")[:-1] if text.endswith("\n") else text.split("\n"), 1):
        try:
            o = json.loads(line)
        except ValueError as e:
            errors.append("line %d is not JSON (%s): %.80r" % (n, e, line))
            continue
        if not isinstance(o, dict):
            errors.append("line %d is not a JSON object" % n)
            continue
        objs.append(o)
    return objs


def check_run(label, m, exe, args, data):
    r = run(exe, args, data)
    errs = []
    if r.get("timeout"):
        errs.append("did not exit within 60 s")
    if r["rc"] != 0:
        errs.append("exit code %s (stderr: %.200r)" % (r["rc"], r["err"].decode("utf-8", "replace").strip()))
    objs = lines_ok(label, r["out"], errs)
    if not objs:
        errs.append("no stdout line at all (the hello line is required)")
    else:
        h = objs[0]
        for k in ("helper", "version", "protocol", "licence", "source"):
            if k not in h:
                errs.append("hello line lacks \"%s\"" % k)
        if h.get("protocol") != 1:
            errs.append("hello protocol is %r, not 1" % (h.get("protocol"),))
        if h.get("helper") != m.get("name"):
            errs.append("hello helper %r differs from manifest name %r" % (h.get("helper"), m.get("name")))
        if "type" in h and h.get("type") != "hello":
            errs.append("first line is an event (%r), not the hello line" % h.get("type"))
        for n, o in enumerate(objs[1:], 2):
            if not isinstance(o.get("type"), str):
                errs.append("line %d has no \"type\": %.100r" % (n, json.dumps(o)))
                break
    if r["after_eof"] is not None and r["after_eof"] > EXIT_WITHIN:
        errs.append("exited %.2f s after end of input (limit %.0f s)" % (r["after_eof"], EXIT_WITHIN))
    if r["window"]:
        errs.append("a visible window of the process was seen")
    if errs:
        for e in errs:
            fail(label, e)
    else:
        ok(label, "rc 0, %d line(s), %.2f s after end of input" % (len(objs), r["after_eof"] or 0))
    return objs, r


# ----------------------------------------------------------------------------------------------- dumpbin / imports
def find_dumpbin(given):
    if given and os.path.isfile(given):
        return given
    from_path = None
    for d in os.environ.get("PATH", "").split(os.pathsep):
        c = os.path.join(d, "dumpbin.exe")
        if os.path.isfile(c):
            from_path = c
            break
    if from_path:
        return from_path
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if os.path.isfile(vswhere):
        try:
            out = subprocess.run([vswhere, "-latest", "-products", "*", "-property", "installationPath"],
                                 capture_output=True, text=True, timeout=30).stdout.strip()
        except (OSError, subprocess.SubprocessError):
            out = ""
        if out:
            hits = glob.glob(os.path.join(out, "VC", "Tools", "MSVC", "*", "bin", "Hostx64", "x64", "dumpbin.exe"))
            if hits:
                return sorted(hits)[-1]
    return None


def check_imports(exe, dumpbin):
    db = find_dumpbin(dumpbin)
    if not db:
        fail("imports", "dumpbin.exe not found (give --dumpbin, or put it on PATH)")
        return
    p = subprocess.run([db, "/nologo", "/imports", exe], capture_output=True, text=True, timeout=120)
    if p.returncode != 0:
        fail("imports", "dumpbin failed (%d): %.200s" % (p.returncode, p.stderr or p.stdout))
        return
    dlls = sorted({m.group(1).lower() for m in re.finditer(r"^\s{4}(\S+\.dll)\s*$", p.stdout, re.M | re.I)})
    bad = [d for d in dlls if os.path.splitext(d)[0] in BAD_DLLS]
    if bad:
        fail("imports", "imports a network library: %s (all imports: %s)" % (", ".join(bad), ", ".join(dlls)))
    else:
        ok("imports", "no network library; imports: " + ", ".join(dlls))


# ------------------------------------------------------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--exe", required=True)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--dumpbin")
    a = ap.parse_args()
    exe = os.path.abspath(a.exe)
    if not os.path.isfile(exe):
        fail("exe", "%s does not exist" % exe)
        return 1
    try:
        with open(a.manifest, "rb") as f:
            raw = f.read()
        manifest = json.loads(raw.decode("utf-8"))
    except (OSError, ValueError) as e:
        fail("manifest.parse", "%s: %s" % (a.manifest, e))
        return 1
    print("conformance: %s (%s)" % (manifest.get("name"), os.path.basename(exe)))
    check_manifest(manifest, a.manifest)
    if manifest.get("executable") != os.path.basename(exe):
        fail("manifest.executable", "manifest says %r, the file is %r" % (manifest.get("executable"), os.path.basename(exe)))

    # --foxsdr-helper-info
    r = run(exe, ["--foxsdr-helper-info"])
    errs = []
    if r["rc"] != 0:
        errs.append("exit code %s" % r["rc"])
    objs = lines_ok("info", r["out"], errs)
    body = r["out"].decode("utf-8", "replace")
    try:
        info = json.loads(body)  # the whole output must be ONE JSON document (it may span lines)
        if info != manifest:
            errs.append("output differs from %s" % os.path.basename(a.manifest))
    except ValueError as e:
        info = None
        errs.append("stdout is not one JSON document: %s" % e)
    # a pretty-printed manifest spans several lines, so per-line JSON is not required for the info output; LF alone is
    # (Windows text-mode stdout turns LF into CRLF: that was a slip in five helpers and is now a failure)
    errs = [e for e in errs if not e.startswith("line ")]
    if r["window"]:
        errs.append("a window was seen")
    if errs:
        for e in errs:
            fail("info", e)
    else:
        ok("info", "one JSON manifest, equal to the manifest file")

    base = ["--foxsdr-helper", "1"]
    extra = EXTRA_ARGS.get(manifest.get("name"), [])
    formats = manifest_formats(manifest)
    modes = [f for f in formats]
    if manifest.get("name") == "trunkfmt":
        modes.append("csbk-hex")  # a second, text input mode named in the manifest "modes"
    if not modes:
        fail("runs", "the manifest lists no input format")
    for fmt in modes:
        rate = manifest_rate(manifest, fmt)
        args = base + ["--format", fmt] + (["--rate", str(rate)] if rate else []) + extra
        check_run("empty[%s]" % fmt, manifest, exe, args, b"")
        if fmt in TEXT_FORMATS:
            data = b"\n" * 100
        else:
            bps = BYTES_PER_SAMPLE.get(fmt)
            if bps is None:
                fail("silence[%s]" % fmt, "no byte size known for this format")
                continue
            fill = b"\x7f" if fmt == "cu8" else b"\x00"
            data = fill * (bps * (rate or 0))
        check_run("silence[%s]" % fmt, manifest, exe, args, data)

    # wrong --format
    r = run(exe, base + ["--format", "no-such-format", "--rate", "48000"] + extra)
    errs = []
    if r["rc"] == 0:
        errs.append("a wrong --format exited 0")
    if not r["err"].strip():
        errs.append("a wrong --format printed nothing on stderr")
    if r["window"]:
        errs.append("a window was seen")
    if r.get("timeout"):
        errs.append("did not exit")
    if errs:
        for e in errs:
            fail("wrong-format", e)
    else:
        ok("wrong-format", "rc %s, stderr: %.80s" % (r["rc"], r["err"].decode("utf-8", "replace").strip().splitlines()[0]))

    # attribution (OnAir additional terms)
    att = manifest.get("attribution")
    if att:
        for label, args in (("--version", ["--version"]), ("--help", ["--help"])):
            r = run(exe, args)
            if att.encode("utf-8") not in r["out"] + r["err"]:
                fail("attribution", "%r is not in the %s output" % (att, label))
            else:
                ok("attribution", "present in %s output" % label)
        r = run(exe, base + ["--format", formats[0], "--rate", str(manifest_rate(manifest, formats[0]) or 12000)] + extra)
        hello = r["out"].decode("utf-8", "replace").split("\n")[0]
        try:
            hobj = json.loads(hello)
        except ValueError:
            hobj = {}
        if att in str(hobj.get("attribution", "")):
            ok("attribution", "present in the hello line")
        else:
            fail("attribution", "not in the hello line")
    else:
        # a helper that names OnAir as upstream must carry the attribution in its manifest
        if "adswill/OnAir" in str(manifest.get("source", "")) + str(manifest.get("upstream", "")):
            fail("attribution", "upstream is OnAir but the manifest has no \"attribution\"")

    check_imports(exe, a.dumpbin)

    print("conformance %s: %d failure(s), %d warning(s)" % (manifest.get("name"), len(failures), len(warnings)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
