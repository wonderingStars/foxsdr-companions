# Sensitivity sweep of the weakmodes2 helper on generated signals (a test tool, not part of the helper).
# For each SNR (dB in 2500 Hz) it makes N slots of white noise with one transmission each (wm2gen, seeded), pipes them through the
# helper and counts the slots whose message was decoded exactly. Prints a table and the lowest SNR with at least 50 % / 90 % decodes.
#   python -I sweep.py <wm2gen.exe> <weakmodes2-helper.exe> <mode> <period> <sub|-> "<message>" <snr_from> <snr_to> <step> [slots] [workdir]
import json, os, subprocess, sys

gen, helper, mode, period, sub, msg, a, b, step = sys.argv[1:10]
slots = int(sys.argv[10]) if len(sys.argv) > 10 else 10
work = sys.argv[11] if len(sys.argv) > 11 else os.environ.get("TEMP", ".")
os.makedirs(work, exist_ok=True)
a, b, step = float(a), float(b), float(step)
dec = {"fst4": "fst4", "fst4w": "fst4w", "q65": "q65"}[mode] + ":" + period + (sub if mode == "q65" else "")
snr = a
rows = []
while (snr >= b) if step < 0 else (snr <= b):
    raw = os.path.join(work, "sweep_%d_%s_%s_%s.raw" % (os.getpid(), mode, period, snr))
    subprocess.run([gen, "--mode", mode, "--period", period, "--sub", sub if sub != "-" else "A", "--sig", "%s@1500@%s@0.2" % (msg, snr),
                    "--slots", str(slots), "--seed", "7", "--out", raw], check=True)
    with open(raw, "rb") as f:
        p = subprocess.run([helper, "--foxsdr-helper", "1", "--format", "s16", "--rate", "12000", "--decoders", dec], stdin=f,
                           capture_output=True, text=True, check=True)
    os.remove(raw)
    ok = 0
    snrs = []
    seen = set()
    for line in p.stdout.splitlines():
        j = json.loads(line)
        if j.get("type") == "decode" and j["text"] == msg and j["slot"] not in seen and abs(j["freq"] - 1500) < 3:
            ok += 1
            seen.add(j["slot"])
            snrs.append(j["snr"])
    rows.append((snr, ok))
    print("%s %-6s %-5s snr %6.1f dB: %2d/%d decoded%s" % (mode, period, sub, snr, ok, slots, ("  (reported snr median %d)" % sorted(snrs)[len(snrs) // 2]) if snrs else ""), flush=True)
    snr += step
for frac, name in ((0.5, "50 %"), (0.9, "90 %")):
    good = [s for s, k in rows if k >= frac * slots]
    print("lowest SNR with >= %s decoded: %s" % (name, min(good) if good else "none in range"))
