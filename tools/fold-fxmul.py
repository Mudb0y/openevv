"""Collapse the inlined staged multiply in KlattSynth into one pseudo-line.

fxmul_scaled expands to about ninety instructions every time MSVC inlines it,
and it is already transcribed and verified, so folding each expansion back to a
single call makes the surrounding logic readable.
"""

import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'analysis/clsyn/functions/KlattSynth.asm')
OUT = os.path.join(ROOT, 'analysis/clsyn/KlattSynth.folded.asm')

LADDER = ('$0x10000,', '$0x100000,', '$0x1000000,', '$0x10000000,',
          '$0xffff0000,', '$0xfff00000,', '$0xff000000,', '$0xf0000000,')

lines = open(SRC).read().splitlines()

parsed = []
for l in lines:
    m = re.match(r'^\s*([0-9a-f]+):\s+(.*)$', l)
    parsed.append((int(m.group(1), 16), m.group(2).strip(), l) if m else None)

idx = [i for i, p in enumerate(parsed) if p]
text = [parsed[i][1] for i in idx]
addr = [parsed[i][0] for i in idx]

# Every inlined expansion carries the eight ladder constants exactly once, in
# order, so a new cluster starts each time the sequence returns to 0x10000.
hits = [n for n, t in enumerate(text) if any(c in t for c in LADDER)]
clusters = []
for n in hits:
    if clusters and '$0x10000,' not in text[n] and len(clusters[-1]) < 8:
        clusters[-1].append(n)
    else:
        clusters.append([n])

regions = []
for c in clusters:
    lo, hi = c[0], c[-1]

    # Back up over the guard that splits positive from negative.
    s = lo
    while s > 0:
        t = text[s - 1]
        if (t.startswith(('cmpl', 'testl', 'movswl', 'movl', 'jle', 'jge',
                          'jbe', 'jmp', 'negl', 'sarl', 'imull'))
                and hi - s < 200):
            s -= 1
            if t.startswith('jle') and 'testl' in text[s - 1]:
                s -= 2
                break
            continue
        break

    # Forward through the convergence copies.
    e = hi
    while e + 1 < len(text):
        t = text[e + 1]
        if re.match(r'movl\s+-0x[0-9a-f]+\(%ebp\),\s+%eax$', t) or \
           re.match(r'movl\s+%eax,\s+-0x[0-9a-f]+\(%ebp\)$', t) or \
           t.startswith(('imull', 'sarl', 'negl', 'movswl', 'jmp')):
            e += 1
            continue
        break
    regions.append((s, e))

# Recover the operands: x is what the ladder compares, coef the other factor.
def operands(s, e):
    x = coef = None
    for m in range(s, min(s + 25, e + 1)):
        t = text[m]
        cm = re.match(r'cmpl\s+\$0x10000,\s+(.*?)(?:\s+#.*)?$', t)
        if cm and x is None:
            x = cm.group(1)
        im = re.match(r'imull\s+([^,]+),', t)
        if im and coef is None:
            coef = im.group(1)
    if x is None:
        for m in range(s, min(s + 25, e + 1)):
            cm = re.match(r'cmpl\s+\$0x10000,\s+(.*?)(?:\s+#.*)?$', text[m])
            if cm:
                x = cm.group(1)
                break
    return x, coef


starts = {s: (s, e) for s, e in regions}
out = []
skip_to = -1
for n in range(len(text)):
    if n <= skip_to:
        continue
    if n in starts:
        s, e = starts[n]
        x, coef = operands(s, e)
        dst = None
        for m in range(e, s, -1):
            dm = re.match(r'movl\s+\S+,\s+(-0x[0-9a-f]+\(%ebp\))$', text[m])
            if dm:
                dst = dm.group(1)
                break
        out.append('   %5x:  ;;;; FXMUL  x=%-24s coef=%-22s -> %-16s [%d insns]'
                   % (addr[s], x, coef, dst, e - s + 1))
        skip_to = e
        continue
    out.append(parsed[idx[n]][2])

open(OUT, 'w').write('\n'.join(out) + '\n')
print('collapsed %d staged multiplies' % len(regions))
print('lines: %d -> %d' % (len(text), len(out)))
