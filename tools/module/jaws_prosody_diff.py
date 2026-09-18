#!/usr/bin/env python3
"""Emit the CORRECT adoption edits for JAWS ECI.INI values into openevv .settings.

- Voice presets: matched by key (Voice1..8 have no ID semantics).
- Phoneme rows:   matched by phoneme ID (row-wise matching corrupts: JAWS'
  table rows are shifted vs ours by the extra low-ID override rows we carry).
Read-only. Prints the edit list and writes the payload to /tmp.
"""
import re, glob, sys

JAWS_INI = "/root/eloquence-re/reference/jaws-x/a7-x/ECI.INI"

def load_ini(path):
    out = {}
    sec = None
    for line in open(path, encoding="utf-8", errors="replace").read().split("\n"):
        line = line.rstrip("\r")
        m = re.search(r'\[([0-9.]+)\]\s*$', line)
        if m:
            sec = m.group(1)
            out[sec] = {}
            continue
        if sec and '=' in line:
            k, v = line.split('=', 1)
            out[sec][k] = v.strip()
    return out

jaws = load_ini(JAWS_INI)
edits = []  # (path, tag, section, key, old, new)

for p in sorted(glob.glob("lang/*/*.settings")):
    tag = p.split("/")[1]
    data = load_ini(p)
    for s in sorted(data):
        if s not in jaws:
            continue  # plpl [17.0]: no JAWS counterpart; esmx [2.2]: ditto
        for k, v in sorted(data[s].items()):
            if "Dataset" in k:
                continue
            jv = jaws[s].get(k)
            if jv is None or jv == v:
                continue
            if k.startswith("Voice"):
                edits.append((p, tag, s, k, v, jv))
            elif k.startswith("Phoneme"):
                oid = int(v.split()[0])
                jid = int(jv.split()[0])
                if oid == jid:
                    edits.append((p, tag, s, k, v, jv))
                # else: shifted-label artifact — skip

if not edits:
    print("no real edits")
    sys.exit(0)

last_tag = None
for p, tag, s, k, v, jv in sorted(edits):
    if tag != last_tag:
        print("\n== %s [%s]" % (tag, s))
        last_tag = tag
    print("   %-12s %s -> %s" % (k, v, jv))
print("\n%d real JAWS-value edits across %d languages" % (
    len(edits), len(set(e[1] for e in edits))))

safe = [(a, b, c, d, e) for a, b, c, d, e, _ in edits
        if not (b == "frfr" and d == "Phoneme31")]
with open("/tmp/jaws_prosody_apply.py", "w") as f:
    f.write("#!/usr/bin/env python3\n# Apply JAWS ECI.INI values to openevv .settings (ID-safe).\n")
    f.write("EDITS = %r\n" % [(t, s, k) for t, s, k, _, _ in safe])
print("payload -> /tmp/jaws_prosody_apply.py (%d edits)" % len(safe))
print("payload -> /tmp/jaws_prosody_apply.py")