#!/usr/bin/env python3
"""Tests of the atn helper. GPL-3.0-or-later, new code written for the FoxSDR atn helper.

usage: python -I run_tests.py <path to atn.exe> [--update]

--update rewrites tests/expected.jsonl from the current output (review the diff before committing it).
Every check prints one line; the exit status is non-zero if any check fails. Nothing here touches the network.
"""
import json, os, random, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = sys.argv[1]
FAILS = []

def run(args, stdin=b'', timeout=120):
    p = subprocess.run([EXE] + args, input=stdin, capture_output=True, timeout=timeout)
    return p.returncode, p.stdout, p.stderr

def check(name, cond, detail=''):
    print(('PASS ' if cond else 'FAIL ') + name + ((' -- ' + str(detail)) if (detail and not cond) else ''))
    if not cond:
        FAILS.append(name)

def lines_json(out):
    res = []
    for ln in out.decode('utf-8').split('\n'):
        if ln:
            res.append(json.loads(ln))      # raises (=> test crash, reported) if a line is not valid JSON
    return res

HELPER = ['--foxsdr-helper', '1', '--format', 'avlc-hex']
frames_text = open(os.path.join(HERE, 'frames.txt'), 'rb').read().replace(bytes([13]), b'')

# ---- manifest ------------------------------------------------------------------------------------------------------
rc, out, err = run(['--foxsdr-helper-info'])
m = json.loads(out)
check('info: exit 0 and manifest names the helper', rc == 0 and m['name'] == 'atn' and m['protocol'] == 1)
manifest_file = json.load(open(os.path.join(HERE, '..', 'atn.helper.json')))
check('info: program manifest equals atn.helper.json', m == manifest_file, (m, manifest_file))

# ---- empty input -----------------------------------------------------------------------------------------------------
rc, out, err = run(HELPER, b'')
ev = lines_json(out)
check('empty input: exit 0, hello then stats only', rc == 0 and len(ev) == 2 and ev[0]['helper'] == 'atn'
      and ev[0]['protocol'] == 1 and ev[1]['type'] == 'stats' and ev[1]['frames'] == 0, (rc, out))
check('hello line carries licence and source', 'GPL-3.0' in ev[0]['licence'] and ev[0]['source'].startswith('https://'))

# ---- wrong format / bad options -------------------------------------------------------------------------------------
for fmt in ('s16', 'cf32', 'avlc', ''):
    rc, out, err = run(['--foxsdr-helper', '1', '--format', fmt, '--rate', '48000'], b'00')
    check('wrong --format %r: non-zero exit, message on stderr, nothing on stdout' % fmt,
          rc != 0 and out == b'' and b'avlc-hex' in err, (rc, out, err))
rc, out, err = run(['--foxsdr-helper', '2', '--format', 'avlc-hex'])
check('unsupported protocol version refused', rc != 0 and out == b'')
rc, out, err = run(['--bogus'])
check('unknown option refused', rc != 0 and out == b'')
rc, out, err = run([])
check('no --format refused', rc != 0 and out == b'')

# ---- the real test: layers, text, pinned output --------------------------------------------------------------------------
meta = []
for ln in frames_text.decode().split('\n'):
    if ln.startswith('# '):
        name, layers, sub = [x.strip() for x in ln[2:].split('|')]
        meta.append((name, layers, sub))
rc, out, err = run(HELPER, frames_text)
ev = lines_json(out)
check('frames: exit 0, nothing on stderr', rc == 0 and err == b'', (rc, err))
frames = {e['line']: e for e in ev if e.get('type') == 'frame'}
texts = {e['line']: e for e in ev if e.get('type') == 'text'}
check('frames: every input line decoded (%d)' % len(meta), len(frames) == len(meta) and len(texts) == len(meta),
      (len(frames), len(texts), len(meta)))
for i, (name, layers, sub) in enumerate(meta, 1):
    f, t = frames.get(i), texts.get(i)
    ok = f is not None and ','.join(f['layers']) == layers and t is not None and sub in t['text']
    check('layers+text %-34s %s' % (name, layers), ok, (f and f['layers'], t and t['text'][-200:]))

stats = [e for e in ev if e.get('type') == 'stats'][0]
check('stats event counts', stats['frames'] == len(meta) and stats['decoded'] == len(meta) and stats['bad_fcs'] == 0, stats)

# field-level checks that do not depend on the pinned file
def msg(i):
    return frames[i]['msg']
idx = {m[0]: i + 1 for i, m in enumerate(meta)}
i = idx['cpdlc_downlink_wilco']
check('AVLC json: sseq/rseq are the right way round (sent 5, received 2)',
      msg(i)['avlc']['sseq'] == 5 and msg(i)['avlc']['rseq'] == 2, msg(i)['avlc'])
check('AVLC json: addresses and direction', msg(i)['avlc']['src']['addr'] == '4CA2B1'
      and msg(i)['avlc']['src']['type'] == 'Aircraft' and msg(i)['avlc']['dst']['addr'] == '2B1A93')
cp = msg(i)['avlc']['x25']['clnp']['cotp']['cpdlc']
check('CPDLC json: payload present', isinstance(cp, dict) and len(json.dumps(cp)) > 50, cp)
check('CPDLC downlink text shows message id, reference and element',
      all(s in texts[i]['text'] for s in ('Msg ID: 5', 'Msg Ref: 4', 'WILCO', '2026-11-12 09:41:07')))
j = idx['cm_logon_request']
check('CM logon text shows flight id and TSAP', 'Flight ID: TEST123' in texts[j]['text'])
k = idx['x25_fragment_2_of_2_reassembled']
check('X.25 fragments are reassembled (status complete) and decoded',
      msg(k)['avlc']['x25']['reasm_status'] == 'complete' and 'cpdlc' in msg(k)['avlc']['x25']['clnp']['cotp'])

expected_path = os.path.join(HERE, 'expected.jsonl')
if '--update' in sys.argv:
    open(expected_path, 'wb').write(out)
    print('expected.jsonl rewritten (%d lines)' % len(out.split(b'\n')))
else:
    exp = open(expected_path, 'rb').read()
    check('pinned output: byte-identical to expected.jsonl', out == exp,
          'first difference near byte %d' % next((n for n, (a, b) in enumerate(zip(out, exp)) if a != b), min(len(out), len(exp))))
rc2, out2, _ = run(HELPER, frames_text)
check('deterministic: second run is byte-identical', out2 == out)

# ---- options ----------------------------------------------------------------------------------------------------------------
rc, out_nt, err = run(HELPER + ['--no-text'], frames_text)
check('--no-text: no text events, frame events unchanged',
      not any(e.get('type') == 'text' for e in lines_json(out_nt))
      and [e for e in lines_json(out_nt) if e.get('type') == 'frame'] == [e for e in ev if e.get('type') == 'frame'])

# frames without FCS (as a demodulator that already verified it would pass them)
nofcs = []
for ln in frames_text.decode().split('\n'):
    if ln and not ln.startswith('#'):
        t, h = ln.split()
        nofcs.append('%s %s' % (t, h[:-4]))
rc, out_nf, err = run(HELPER + ['--fcs', 'none', '--no-text'], ('\n'.join(nofcs) + '\n').encode())
check('--fcs none: same decoded frames as with the FCS present',
      [e for e in lines_json(out_nf) if e.get('type') == 'frame'] ==
      [e for e in ev if e.get('type') == 'frame'])

# ---- input errors -------------------------------------------------------------------------------------------------------------
good = frames_text.decode().split('\n')[1]
bad_fcs = good[:-1] + ('0' if good[-1] != '0' else '1')
cases = [
    ('bad FCS', bad_fcs, 'bad FCS'),
    ('odd number of hex digits', '100.0 abc', 'odd number'),
    ('not hex', 'zz' * 20, 'not hex'),
    ('too short', 'aabbcc', 'too short'),
    ('bad timestamp', '1x.0 ' + good.split()[1], 'timestamp'),
    ('too many fields', '1.0 2 ' + good.split()[1], 'too many'),
]
for name, line, sub in cases:
    rc, out, err = run(HELPER, (line + '\n').encode())
    ev2 = lines_json(out)
    errs = [e for e in ev2 if e.get('type') == 'error']
    check('input error: %s' % name, rc == 0 and len(errs) == 1 and sub in errs[0]['error'] and not any(
        e.get('type') == 'frame' for e in ev2), (rc, ev2))
rc, out, err = run(HELPER, b'x' * 20000 + b'\n' + good.encode() + b'\n')
ev2 = lines_json(out)
check('input error: over-long line is reported once, the next line is still decoded',
      rc == 0 and sum(1 for e in ev2 if e.get('type') == 'error') == 1 and sum(1 for e in ev2 if e.get('type') == 'frame') == 1, ev2[:4])
rc, out, err = run(HELPER, b'# a comment\n\n   \n' + good.encode() + b'\r\n')
check('comments, blank lines and CRLF are accepted', rc == 0 and sum(1 for e in lines_json(out) if e.get('type') == 'frame') == 1)

# ---- garbage and flooding -----------------------------------------------------------------------------------------------------
rng = random.Random(20261112)
junk = bytes(rng.randrange(256) for _ in range(200000))
rc, out, err = run(HELPER, junk)
ev2 = lines_json(out)
check('garbage binary input: exit 0, valid JSON lines only, output bounded',
      rc == 0 and len(ev2) <= 2 + 50 and ev2[-1]['type'] == 'stats', (rc, len(ev2)))
flood = ('zz\n' * 100000).encode()
rc, out, err = run(HELPER, flood)
ev2 = lines_json(out)
check('100000 bad lines: error events capped, rest only counted',
      rc == 0 and len(ev2) <= 2 + 50 and ev2[-1]['bad_input'] == 100000 and ev2[-1]['errors_suppressed'] == 100000 - 50, ev2[-1])

# ---- fuzz: valid FCS (via --fcs none) over mutated real frames, so the upper layers see hostile data ---------------------------------
seeds = [bytes.fromhex(l.split()[1])[:-2] for l in frames_text.decode().split('\n') if l and not l.startswith('#')]
fl = []
for n in range(6000):
    b = bytearray(rng.choice(seeds))
    for _ in range(rng.randrange(1, 6)):
        r = rng.random()
        if r < 0.5 and len(b) > 9:
            b[rng.randrange(9, len(b))] = rng.randrange(256)       # leave the AVLC addresses/control mostly intact
        elif r < 0.7 and len(b) > 12:
            del b[rng.randrange(9, len(b)):]                        # truncate
        elif r < 0.9:
            b.extend(bytes(rng.randrange(256) for _ in range(rng.randrange(1, 40))))
        else:
            b[rng.randrange(len(b))] = rng.randrange(256)
    fl.append('%d.%d %s' % (n // 10, n % 10, bytes(b).hex()))
rc, out, err = run(HELPER + ['--fcs', 'none'], ('\n'.join(fl) + '\n').encode(), timeout=300)
ev2 = lines_json(out)
fr = sum(1 for e in ev2 if e.get('type') == 'frame')
check('fuzz: 6000 mutated frames, exit 0, every output line valid JSON (%d decoded)' % fr,
      rc == 0 and ev2[-1]['type'] == 'stats' and ev2[-1]['lines'] == 6000 and fr > 3000, (rc, err[-300:], ev2[-1]))
check('fuzz: nothing on stderr', err == b'', err[-300:])

print('\n%d check(s) failed' % len(FAILS) if FAILS else '\nall checks passed')
sys.exit(1 if FAILS else 0)
