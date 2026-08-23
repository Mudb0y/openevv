#!/usr/bin/env python3
"""A language's phonemes, from the three places they live.

A phoneme is in three places at once and none of them alone says what it is.

Its name is a value of the phone statement's first field, in
`lang/<tag>/<tag>.statements`, and that is the list the rules index by. Its
numbers are a `Phoneme` line in `lang/<tag>/<tag>.settings`: four bytes of name
and then eleven values, which is what a caller handing the engine phonemes
rather than text is read against. And what it sounds like is a rule --
`<x>_ph_<name>` -- which sets the source parameters and calls one locus rule
for the place it is articulated at, `<x>_<place>_Fv`, where the formant targets
are.

So this puts the three beside each other: every phoneme the language declares,
whether it has numbers, whether it has a rule, and which place that rule speaks
it at. What it is for is adding one, which means having somewhere to copy from
and knowing what a copy has to cover.

usage: lang-phonemes.py <tag>            every phoneme
       lang-phonemes.py <tag> <name>...  only the ones named
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def inventory(tag):
    """The names the phone statement declares, in order, which is the order
    the rules index them by."""
    p = os.path.join(ROOT, "lang", tag, "%s.statements" % tag)
    inside = False
    field = None
    out = []
    for line in open(p):
        if line.startswith("statement phone"):
            inside = True
            continue
        if line.startswith("statement ") and inside:
            break
        if not inside:
            continue
        if line.startswith("  field "):
            field = line.split()[1]
        elif line.startswith("    value") and field == "name":
            t = line[len("    value"):].rstrip("\n")
            out.append(t[1:] if t.startswith(" ") else t)
    return out


def declared(tag):
    """The numbers each phoneme is declared with in the settings: its name out
    of the first four bytes, and the eleven values after them."""
    p = os.path.join(ROOT, "lang", tag, "%s.settings" % tag)
    out = {}
    for line in open(p):
        m = re.match(r"Phoneme(\d+)=(.*)", line.strip())
        if not m:
            continue
        nums = [int(x) for x in m.group(2).split()]
        name = "".join(chr(c) for c in nums[:4] if c)
        out[name] = (int(m.group(1)), nums[4:])
    return out


def rules(tag):
    """Which rule speaks each phoneme, and the place it speaks it at.

    A rule for a phoneme is named for it, and the one call it makes to
    something ending in _Fv is the place: that rule holds the formant targets.
    """
    where = os.path.join(ROOT, "lang", tag, "rules")
    out = {}
    for f in sorted(os.listdir(where)):
        if not (f.endswith(".dr") or f.endswith(".up")) or f == "wrappers.up":
            continue
        name = None
        for raw in open(os.path.join(where, f)):
            line = raw.rstrip("\n")
            if line.startswith("rule "):
                name = line.split()[1]
                continue
            if name is None:
                continue
            m = re.match(r"\s+call (\S+_Fv)\b", line)
            if m:
                ph = re.match(r".*_ph_(.+?)(_dur)?$", name)
                if ph:
                    out.setdefault(ph.group(1), (name, set()))
                    out[ph.group(1)][1].add(m.group(1))
    return out


def show(tag, want):
    names = inventory(tag)
    nums = declared(tag)
    said = rules(tag)

    print("%s: %d phonemes declared in the statements, %d in the settings,"
          " %d with a rule" % (tag, len(names), len(nums), len(said)))
    for code, nm in enumerate(names):
        if want and nm not in want:
            continue
        if nm == "GAP":
            continue
        line = "%3d  %-5s" % (code, nm)
        if nm in nums:
            line += " numbers %-2d" % nums[nm][0]
        else:
            line += " no numbers"
        if nm in said:
            rule, places = said[nm]
            line += "  %-16s at %s" % (rule, ", ".join(sorted(places)))
        else:
            line += "  no rule of its own"
        print(line)
    missing = [nm for nm in nums if nm not in names]
    if missing:
        print("  and the settings declare %s, which the statements do not"
              % ", ".join(sorted(missing)))
    return True


def main(argv):
    if not argv:
        print(__doc__.strip())
        return 2
    return 0 if show(argv[0], set(argv[1:])) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
