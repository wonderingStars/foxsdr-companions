#!/usr/bin/env python3
"""Find the lowest SNR at which each mode still decodes the test message, using sigtool (fldigi's own transmitter)
and the helper. Run by hand; the result goes in README.md and the threshold cases in test/CMakeLists.txt.

  python -I sweep.py <sigtool.exe> <fldigimodes.exe> <workdir> [--seeds 1,2,3] [--start 6] [--floor -30] [mode ...]

"Decodes" means the sent message appears unchanged in the joined text events. SNR is signal power over noise power in
a 2500 Hz bandwidth (sigtool gen --snr). A mode's threshold is the lowest whole dB at which every seed decodes while
every higher dB also did (the scan stops at the first dB where a seed fails)."""
import concurrent.futures
import os
import subprocess
import sys

MESSAGE = "CQ DE FOXSDR TEST 123"
DEFAULT_MODES = [
    "olivia-4-250", "olivia-8-500", "olivia-16-500", "olivia-32-1000", "olivia-64-2000",
    "contestia-4-250", "contestia-8-500", "contestia-16-1000", "contestia-32-1000",
    "mfsk4", "mfsk8", "mfsk11", "mfsk16", "mfsk22", "mfsk31", "mfsk32", "mfsk64", "mfsk128",
    "mt63-500s", "mt63-500l", "mt63-1000s", "mt63-1000l", "mt63-2000s", "mt63-2000l",
    "thormicro", "thor4", "thor5", "thor8", "thor11", "thor16", "thor22", "thor25x4", "thor50x1", "thor50x2", "thor100",
]


def rate_of(mode):
    return 11025 if mode in ("mfsk11", "mfsk22", "thor5", "thor11", "thor22") else 8000


def decodes(sigtool, helper, work, mode, snr, seed):
    tag = "%s_%s_%d" % (mode, str(snr).replace("-", "m"), seed)
    wav = os.path.join(work, tag + ".wav")
    raw = os.path.join(work, tag + ".raw")
    out = os.path.join(work, tag + ".jsonl")
    tail = []
    if mode.startswith(("mt63", "thor")):
        tail = ["--tail", "8"]
    r = subprocess.run([sigtool, "gen", mode, wav, "--text", MESSAGE, "--snr", str(snr), "--seed", str(seed)] + tail,
                       capture_output=True)
    if r.returncode:
        return False
    subprocess.run([sigtool, "wav2raw", wav, raw], capture_output=True)
    with open(raw, "rb") as fin, open(out, "wb") as fout:
        subprocess.run([helper, "--foxsdr-helper", "1", "--format", "s16", "--rate", str(rate_of(mode)), "--mode", mode],
                       stdin=fin, stdout=fout, stderr=subprocess.DEVNULL)
    t = subprocess.run([sigtool, "jtext", out], capture_output=True).stdout.decode("latin-1")
    for f in (wav, raw, out):
        try:
            os.remove(f)
        except OSError:
            pass
    return MESSAGE in t


def threshold(args):
    sigtool, helper, work, mode, seeds, start, floor = args
    last_ok = None
    snr = start
    while snr >= floor:
        if all(decodes(sigtool, helper, work, mode, snr, s) for s in seeds):
            last_ok = snr
            snr -= 1
        else:
            break
    return mode, last_ok


def main():
    a = sys.argv[1:]
    if len(a) < 3:
        print(__doc__)
        return 2
    sigtool, helper, work = a[0], a[1], a[2]
    seeds, start, floor, modes = [1, 2, 3], 6, -30, []
    i = 3
    while i < len(a):
        if a[i] == "--seeds":
            seeds = [int(x) for x in a[i + 1].split(",")]
            i += 2
        elif a[i] == "--start":
            start = int(a[i + 1])
            i += 2
        elif a[i] == "--floor":
            floor = int(a[i + 1])
            i += 2
        else:
            modes.append(a[i])
            i += 1
    modes = modes or DEFAULT_MODES
    os.makedirs(work, exist_ok=True)
    jobs = [(sigtool, helper, work, m, seeds, start, floor) for m in modes]
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as ex:
        for mode, t in ex.map(threshold, jobs):
            print("%-18s %s" % (mode, ("lowest SNR that decodes: %d dB" % t) if t is not None else "does not decode at +%d dB" % start),
                  flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
