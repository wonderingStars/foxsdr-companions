# Extracts the text of an LZW-compressed PDF 1.1 (the format of the PAS 0001-2 download) without any PDF library:
#   python -I pdf_text.py <in.pdf> <out.txt>
# Good enough to read numbers and formulas; not a general PDF reader.
import re, sys


def lzw(data, early=1):
    out = bytearray()
    width = 9
    table = [bytes([i]) for i in range(256)] + [b'', b'']
    prev = None
    acc = 0
    accb = 0
    for byte in data:
        acc = (acc << 8) | byte
        accb += 8
        while accb >= width:
            code = (acc >> (accb - width)) & ((1 << width) - 1)
            accb -= width
            acc &= (1 << accb) - 1
            if code == 256:
                table = table[:258]
                width = 9
                prev = None
                continue
            if code == 257:
                return bytes(out)
            if prev is None:
                entry = table[code]
            else:
                if code < len(table):
                    entry = table[code]
                else:
                    entry = prev + prev[:1]
                table.append(prev + entry[:1])
            out += entry
            prev = entry
            n = len(table) + early
            width = 9 if n < 512 else 10 if n < 1024 else 11 if n < 2048 else 12
    return bytes(out)


d = open(sys.argv[1], 'rb').read()
pages = []
for m in re.finditer(rb'stream\r?\n', d):
    s = m.end()
    e = d.find(b'endstream', s)
    try:
        dec = lzw(d[s:e])
    except Exception:
        continue
    if b'Tj' in dec or b'TJ' in dec:
        parts = []
        for t in re.finditer(rb'\[(.*?)\]\s*TJ|\(((?:\\.|[^\\)])*)\)\s*Tj|(T\*|Td|TD|ET)', dec, re.S):
            if t.group(1) is not None:
                line = b''
                for a, b in re.findall(rb'\(((?:\\.|[^\\)])*)\)|(-?\d+\.?\d*)', t.group(1)):
                    if a:
                        line += a
                    elif b and float(b) < -200:
                        line += b' '
                parts.append(line)
            elif t.group(2) is not None:
                parts.append(t.group(2))
            else:
                parts.append(b'\n')
        pages.append(b''.join(parts))
out = b'\n\n=====PAGE=====\n'.join(pages)
out = re.sub(rb'\\(\d{3})', lambda m: bytes([int(m.group(1), 8) & 255]), out)
out = out.replace(b'\\(', b'(').replace(b'\\)', b')').replace(b'\\\\', b'\\')
open(sys.argv[2], 'wb').write(out)
print(len(pages), len(out))
