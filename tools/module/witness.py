#!/usr/bin/env python3
"""Watching what a rule writes, rather than reading the rule.

The generated C is the rules, so a print put at a write and a build says
exactly what the engine chose, for whatever text is then spoken. That beats
reading the guards: a block runs from its label to the `goto' that ends it,
and collecting every test seen since the last label crosses the nested ifs
the generated C is full of, which had /t/'s 1750 guarded by a test naming
/k/ when its real guard is a feature of the item to the right.

Two rules learned the hard way and kept here so nothing has to learn them
again.

**The print must read the slot back**, not copy the expression. A third of
the formant writes take their value from a register and /l/'s are all
`LOW(r0)', so printing expressions collapses every context of /l/ into one
useless string.

**Choose what to instrument by what it writes, never by what it is called.**
Fifty-six of the sixty-five rules that write a formant slot have a name that
looks like a formant rule's, and the nine that do not include
`set_seg_default_acoustic_vals', which is the segment delimiter and where
the fourth and fifth formants get their defaults. Missing it cost a day.

The instrumented C is generated rather than tracked, so `git checkout` will
not undo it: strip it, and `make rules' rewrites it in any case.

    tools/module/witness.py <probe> [--tag enus] [--slots s274,s439]
                            [--objs es_cdur,es_ndur] [--strip]
                            [--say <text>...]

With neither `--slots' nor `--objs' it instruments nothing, on purpose.
`--say' speaks each text and prints the writes in the order they happened.
"""

import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
WRITE = re.compile(
    r'^(\s*)(?:STATE|GLOBAL)\((u?int\d+_t),(?: r\d+,)? (s\d+)\) = \((.+?)\);')
RULE = re.compile(r'^/\* (\w+), from ([\w.]+) \*/')
MARK = "WIT"


def rule_files(tag):
    d = os.path.join(ROOT, "lang", tag)
    return [os.path.join(d, f) for f in sorted(os.listdir(d))
            if re.match(r'delta_rules_c\d+_%s\.c$' % tag, f)]


def instrument(tag, keep):
    """A print at every write `keep(rule, obj, slot)' wants. Answers how many."""
    n = 0
    for path in rule_files(tag):
        lines = open(path).read().split("\n")
        out = []
        rule = obj = None
        touched = False
        for i, ln in enumerate(lines, 1):
            m = RULE.match(ln)
            if m:
                rule, obj = m.group(1), m.group(2)
            m = WRITE.match(ln)
            if m and rule is not None and keep(rule, obj, m.group(3)):
                n += 1
                touched = True
                out.append(ln)
                dest = ln.strip()
                dest = dest[:dest.index(" = (")]
                out.append('%sfprintf(stderr, "%s %s %d %s %%d\\n", (int)%s);'
                           % (m.group(1), MARK, rule, i, m.group(3), dest))
                continue
            out.append(ln)
        if touched:
            txt = "\n".join(out)
            if "#include <stdio.h>" not in txt:
                k = txt.index("\n", txt.index("#include"))
                txt = txt[:k] + "\n#include <stdio.h>" + txt[k:]
            open(path, "w").write(txt)
    return n


def strip(tag):
    """Take the prints back out, leaving the file as it was."""
    for path in rule_files(tag):
        txt = open(path).read()
        if MARK not in txt:
            continue
        keep = [l for l in txt.split("\n") if MARK not in l]
        txt = "\n".join(keep).replace("\n#include <stdio.h>", "", 1)
        open(path, "w").write(txt)


def speak(probe, text):
    """What the instrumented rules wrote while saying one thing, in order."""
    with tempfile.TemporaryDirectory() as w:
        c = os.path.join(w, "c.txt")
        with open(c, "w") as f:
            f.write(text + "\n")
        r = subprocess.run([probe, "@" + c, os.path.join(w, "c.wav"), "a"],
                           capture_output=True, text=True)
    out = []
    for line in r.stderr.split("\n"):
        if line.startswith(MARK + " "):
            f = line.split()
            if len(f) >= 5:
                out.append((f[1], int(f[2]), f[3], int(f[4])))
    return out


def opt(argv, name):
    return argv[argv.index(name) + 1] if name in argv else None


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    probe = argv[1]
    tag = opt(argv, "--tag") or "enus"
    if "--strip" in argv:
        strip(tag)
        print("witness: prints removed")
        return 0
    if "--say" in argv:
        for text in argv[argv.index("--say") + 1:]:
            print("== %s" % text)
            for rule, line, slot, val in speak(probe, "`[.1%s]" % text):
                print("   %-32s %-8s %d" % (rule, slot, val))
        return 0
    slots = set((opt(argv, "--slots") or "").split(",")) - {""}
    objs = set((opt(argv, "--objs") or "").split(",")) - {""}

    def keep(rule, obj, slot):
        if slots and slot not in slots:
            return False
        if objs and obj.split(".")[0] not in objs:
            return False
        return bool(slots or objs)

    n = instrument(tag, keep)
    print("witness: %d writes instrumented; build and speak with --say" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
