"""recording_counts.py -- run one channel of an I/Q recording through fmdemod and the multimon helper, print COUNTS ONLY.

    MM_BUILD=<build dir> python -I recording_counts.py <capture.cs16|.cu8> <rate Hz> <offset Hz> <DEMOD[,DEMOD...]> [fmdemod options]
    env HARGS="-b 0"   extra helper options        env TBINS=1   also print events per 10 s of capture

Prints: events per type and kind, distinct addresses and function counts (numbers only), FLEX baud / levels, distinct
FLEX frames, and from the helper's -v 3 diagnostics (FLEX only): frames whose frame-info word passed BCH, BCH-corrected
words, uncorrectable words, bad checksums. POCSAG has no such per-batch figure (upstream's POCSAG BCH counters run at
every bit offset while hunting for sync, so they say nothing about traffic): for POCSAG use -b 0 / 1 / 2 in HARGS and
compare the event counts, as `-b 0` keeps only words that are clean without any correction. The JSON events and the diagnostic text are consumed by counters: nothing a
decoder printed (message text, addresses, capcodes) is stored, printed or written to disk. Part of the FoxSDR
"multimon" GPL helper test tools. GPL-2.0-or-later.
"""
import collections, json, os, re, subprocess, sys, threading

B = os.environ.get('MM_BUILD')
if not B:
    sys.exit('set MM_BUILD to the CMake build directory (the one holding Release/ and test/Release/)')
f, rate, off, modes = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4].split(',')
extra = sys.argv[5:]

p1 = subprocess.Popen([B + '/test/Release/fmdemod.exe', f, rate, off] + extra, stdout=subprocess.PIPE)
args = [B + '/Release/multimon-helper.exe', '--foxsdr-helper', '1', '-v', '3']
for m in modes:
    args += ['-a', m]
args += os.environ.get('HARGS', '').split()
p2 = subprocess.Popen(args, stdin=p1.stdout, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
p1.stdout.close()

ev = collections.Counter()
frames = collections.defaultdict(set)
params = collections.Counter()
tbins = collections.defaultdict(collections.Counter)
addrs = collections.defaultdict(set)
funcs = collections.defaultdict(collections.Counter)
err = collections.Counter()
stats = collections.defaultdict(lambda: collections.Counter())

def rd_out():
    for line in p2.stdout:
        try:
            o = json.loads(line)
        except Exception:
            ev['badjson'] += 1
            continue
        t = o.get('type')
        if not t:
            continue
        ev[t] += 1
        if 'address' in o: addrs[t].add(o['address'])
        if 'function' in o: funcs[t][o['function']] += 1
        tbins[t][int(o.get('t', 0) // 10) * 10] += 1
        k = o.get('kind')
        if k:
            ev[t + ':' + str(k)] += 1
        if 'baud' in o and 'levels' in o:
            params['%s %sbd/%slev' % (t, o['baud'], o['levels'])] += 1
        if 'cycle' in o and 'frame' in o:
            frames[t].add((o['cycle'], o['frame']))

cur = None
def rd_err():
    global cur
    for raw in p2.stderr:
        l = raw.decode('utf-8', 'replace')
        m = re.match(r'===(\w+) stats===', l)
        if m:
            cur = m.group(1)
            continue
        m = re.match(r'(Words BCH checked|Corrected errors|Corrected 1bit errors|Corrected 2bit errors|'
                     r'Invalid word or >2 bits errors): (\d+)', l)
        if m and cur:
            stats[cur][m.group(1)] += int(m.group(2))
            continue
        for pat, key in (
            (r'^(FLEX(?:_NEXT)?): Phase . Fixed (\d) errors', 'fixed'),
            (r'^(FLEX(?:_NEXT)?): Phase . Data corruption', 'uncorrectable word'),
            (r'^(FLEX(?:_NEXT)?): FrameInfoWord', 'FIW ok'),
            (r'^(FLEX(?:_NEXT)?): Unable to decode FIW', 'FIW bad'),
            (r'^(FLEX(?:_NEXT)?): Bad Checksum', 'bad checksum'),
            (r'^(FLEX(?:_NEXT)?): SyncInfoWord', 'sync'),
            (r'^(FLEX(?:_NEXT)?): BlockInfoWord', 'BIW'),
            (r'^(FLEX(?:_NEXT)?): Garbled', 'garbled block'),
            (r'^(FLEX(?:_NEXT)?): Alpha K checksum FAIL', 'K fail'),
            (r'^(FLEX(?:_NEXT)?): Alpha signature FAIL', 'sig fail'),
        ):
            m = re.match(pat, l)
            if m:
                k = key + (' %s-bit' % m.group(2) if key == 'fixed' else '')
                err[m.group(1) + ' ' + k] += 1
                break

t1 = threading.Thread(target=rd_out); t2 = threading.Thread(target=rd_err)
t1.start(); t2.start(); t1.join(); t2.join(); p2.wait(); p1.wait()
print('events:', ' '.join('%s=%d' % kv for kv in sorted(ev.items())) or 'none')
if os.environ.get('TBINS'):
    for k, v in sorted(tbins.items()):
        print('t10s', k, ' '.join('%d:%d' % kv for kv in sorted(v.items())))
for k in sorted(addrs):
    print('distinct addresses', k, len(addrs[k]), 'function counts', dict(funcs[k]))
if params:
    print('params:', ' '.join('%s=%d' % kv for kv in sorted(params.items())))
if frames:
    print('distinct frames:', ' '.join('%s=%d' % (k, len(v)) for k, v in sorted(frames.items())))
if err:
    print('diag:', ' '.join('[%s]=%d' % kv for kv in sorted(err.items())))
print('rc', p2.returncode)
