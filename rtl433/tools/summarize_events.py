"""Reads the rtl433 helper's JSON Lines on standard input and prints COUNTS ONLY: packages the pulse detector found,
packages a decoder claimed, decodes per model name. No identifier, payload or measured value is printed or kept.

    rtl433-helper.exe ... < narrow.cs16 | python -I tools/summarize_events.py [label]

Output (one block per run): `label: packages=<ook+fsk> (ook=.. fsk=..) claimed=.. unclaimed=.. decodes=..` and then
`  <model name>: <count>` per model, most frequent first. Licence GPL-2.0-or-later.
"""
import collections
import json
import sys


def main():
    label = sys.argv[1] if len(sys.argv) > 1 else "run"
    models = collections.Counter()
    stats = None
    bad = 0
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            o = json.loads(line)
        except ValueError:
            bad += 1
            continue
        t = o.get("type")
        if t == "rtl433":
            models[str(o.get("model", "?"))] += 1
        elif t == "stats":
            stats = o
    if stats is None:
        print("%s: NO stats line (helper failed?) bad_lines=%d" % (label, bad))
        return 1
    ook, fsk, ev = stats.get("frames_ook", 0), stats.get("frames_fsk", 0), stats.get("frames_events", 0)
    print("%s: packages=%d (ook=%d fsk=%d) claimed=%d unclaimed=%d decodes=%d%s"
          % (label, ook + fsk, ook, fsk, ev, ook + fsk - ev, sum(models.values()),
             " bad_lines=%d" % bad if bad else ""))
    for m, c in models.most_common():
        print("  %s: %d" % (m, c))
    return 0


if __name__ == "__main__":
    sys.exit(main())
