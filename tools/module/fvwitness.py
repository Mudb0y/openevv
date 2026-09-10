#!/usr/bin/env python3
"""Which block of a formant rule actually fires, watched rather than read.

`tools/module/formants.py' reads the nine place-of-articulation rules and says
what values each block sets and what its guards test. The values are checked
against measurement and hold; the guards are read, and reading them wrongly is
easy -- a block runs from its label to the `goto' that ends it, and collecting
every test seen since the last label crosses the nested ifs the generated C is
full of, which had /t/'s 1750 guarded by a test naming /k/ when its real guard
is a feature of the item to the right.

So this does not read them. It puts a print at every value write in every one
of those rules, builds, speaks a corpus, and writes down which blocks fired
for each case. What comes out is a table of context to values that the engine
witnessed rather than one inferred from its source, and it needs no
understanding of the scan at all.

The instrumented C is generated rather than tracked, so it is stripped back
afterwards and `make rules' will rewrite it in any case.

    tools/module/fvwitness.py <probe> [<tag>]        default enus
"""

import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
SLOTS = ("s439", "s440", "s441", "s442", "s443", "s444", "s445", "s446")
WRITE = re.compile(
    r'^(\s*)(?:STATE|GLOBAL)\(int16_t,(?: r\d+,)? (s4\d\d)\) = \((.+?)\);')
RULE = re.compile(r'^/\* (\w+), from')
MARK = "FVW"


def rule_files(tag):
    d = os.path.join(ROOT, "lang", tag)
    return [os.path.join(d, f) for f in sorted(os.listdir(d))
            if re.match(r'delta_rules_c\d+_%s\.c$' % tag, f)]


def instrument(tag):
    """A print at every formant write in every _Fv rule. Answers the count."""
    n = 0
    for path in rule_files(tag):
        lines = open(path).read().split("\n")
        out = []
        rule = None
        touched = False
        for i, ln in enumerate(lines, 1):
            m = RULE.match(ln)
            if m:
                rule = m.group(1) if m.group(1).endswith("_Fv") else None
            if rule:
                m = WRITE.match(ln)
                if m and m.group(2) in SLOTS:
                    n += 1
                    touched = True
                    out.append(ln)
                    # The value the slot actually took, read back out of the
                    # slot after the write rather than copied from the
                    # expression: a third of the writes take their value from
                    # a register -- /l/'s are all `LOW(r0)' -- and printing
                    # the expression says nothing about what the engine chose.
                    dest = ln.strip()
                    dest = dest[:dest.index(" = (")]
                    out.append('%sfprintf(stderr, "%s %s %d %s %%d\\n", '
                               '(int)%s);'
                               % (m.group(1), MARK, rule, i, m.group(2), dest))
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


def witness(probe, text):
    """Which blocks fired, in order, for one utterance."""
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
                out.append((f[1], int(f[2]), f[3], f[4]))
    return out


VOWELS = "i I e E A a u U o c Y W O X H x".split()
CONSONANTS = "b p d t F k g D T v f z s Z S J C h m n G r l y w R".split()


def corpus():
    """Every consonant between every pair of adjacent vowels, and at a word's
    edges: the same shapes the measured tables use, so the two can be set
    against each other."""
    out = []
    for c in CONSONANTS:
        for k, v1 in enumerate(VOWELS):
            v2 = VOWELS[(k + 1) % len(VOWELS)]
            out.append(("%s:%s%s" % (c, v1, v2), "`[.1%s%s%s]" % (v1, c, v2)))
        for v in VOWELS:
            out.append(("%s:.%s" % (c, v), "`[.1%s%s]" % (c, v)))
            out.append(("%s:%s." % (c, v), "`[.1%s%s]" % (v, c)))
    return out


def run(probe, tag):
    """Speak the corpus and write down which blocks the engine chose."""
    print("# Which block of a formant rule the engine actually chooses, for")
    print("# every consonant in every vowel context. Witnessed, not read: a")
    print("# print at each value write, then %s said each case. So this owes"
          % tag)
    print("# nothing to understanding the scan, and it is what a composer")
    print("# wants -- context to values, as the engine chose them.")
    print("#")
    print("# Written by tools/module/fvwitness.py. `.' is a word edge.")
    seen = {}
    for name, text in corpus():
        w = witness(probe, text)
        if not w:
            continue
        # The last value each slot was given is the one that stands.
        final = {}
        blocks = []
        for rule, line, slot, val in w:
            final[slot] = (val, line)
            if line not in blocks:
                blocks.append(line)
        key = tuple(sorted(final.items()))
        seen.setdefault(key, []).append(name)
        print()
        print("case %s" % name)
        print("  chose %s" % " ".join(str(b) for b in blocks))
        print("  gives %s" % "  ".join(
            "%s=%s" % (k, v[0]) for k, v in sorted(final.items())))
    print()
    print("# %d cases, %d distinct outcomes" % (
        sum(len(v) for v in seen.values()), len(seen)))
    return 0


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    probe = argv[1]
    tag = argv[2] if len(argv) > 2 else "enus"
    if "--strip" in argv:
        strip(tag)
        print("fvwitness: prints removed", file=sys.stderr)
        return 0
    if "--run" in argv:
        return run(probe, tag)
    n = instrument(tag)
    print("fvwitness: %d writes instrumented; build and rerun with --run" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
