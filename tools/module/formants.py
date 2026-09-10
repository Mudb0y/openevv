#!/usr/bin/env python3
"""What each place of articulation does to the formants, read out of the rules.

Every phoneme a language declares is "at" one of a handful of rules named for
a place of articulation -- `eng_lab_Fv', `eng_alv_Fv', `eng_lat_Fv' and so
on, which `tools/module/phonemes.py' prints beside each phoneme -- and those
rules are what compute its formant values. They are in the tree as C, `make
rules' having written them, so the values can be read rather than measured.

And they want reading rather than measuring, because they are not curves. Each
rule is a base locus and then a set of context-dependent overrides, every
value an integer immediate, chosen by a chain of tests: `eng_lat_Fv' has
fourteen. Sampling the output of that and fitting a continuous model to it is
what made /l/'s locus look unpredictable, the velar pinch look unidentifiable
and a run-out's level come out a hundred hertz wrong.

The slots hold no names -- a language declares its globals by kind and count,
so a slot's number is positional and the original's names are gone -- so the
mapping below is inferred from what the engine was measured doing, and three
consonants agree on it: `eng_alv_Fv' sets 1500 and 2550 where /t/ and /s/
between two /a/ measure f2 1500 and f3 2550; `eng_lab_Fv' sets 1000, 2200,
2250, 3300 and 3600 where /m/ measures f2 1000, f3 2200 rising to 2248, f4
3300 and f5 3600; `eng_lat_Fv' sets 800 and 3000 where /l/ measures f2 800
and f3 3000.

    tools/module/formants.py [<tag>]        default enus
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))

# Inferred from measurement, not from any name in the tree. `a' is the value
# the half before the consonant takes and `b' the half after: `eng_lab_Fv'
# sets 2200 and 2250, and /m/ between two /a/ runs its f3 from 2200 to 2248.
SLOT = {
    "s439": "f2a", "s440": "f2b",
    "s441": "f3a", "s442": "f3b",
    "s443": "f4a", "s444": "f4b",
    "s445": "f5a", "s446": "f5b",
}

# Minus two as an int16: the rules' own "leave this one alone". Where a target
# is not set there is nothing to ramp to at that end, so the trajectory runs
# from wherever the vowel already was -- which is why a sonorant looks like it
# has no extent of its own.
KEEP = 65534

VALUE = re.compile(
    r'(?:STATE|GLOBAL)\(int16_t,(?: r\d+,)? (s4\d\d)\) = \((\d+)\)')
LABEL = re.compile(r'^\s*(alt\d+_\d+|L\d+):')
RULE = re.compile(r'^/\* (\w+), from (\S+) \*/')
# `CALLW(test_string_s, FIELD(0), 2, 1, delta_sym_ref[6260])' -- the statement
# whose field to read, how many bytes to compare, and where they are.
# test_string_s walks the scan comparing each node's field against the string,
# and every call in these rules passes one byte, so a condition is "the
# neighbouring phoneme is X".
TEST = re.compile(r'CALLW?\((test_string_s), [^,]+, (\d+), (\d+), '
                  r'delta_sym_ref\[(\d+)\]\)')
OTHER = re.compile(r'CALLW?\((test\w+|starttest\w*), [^)]*?(\d+)\)')


def stores(tag):
    """Every store of the language's own bytes, by name."""
    path = os.path.join(ROOT, "lang", tag, "%s.consts" % tag)
    out = {}
    if not os.path.exists(path):
        return out
    name = None
    for line in open(path):
        if line.startswith("store "):
            name = line.split()[1]
            out[name] = []
        elif line.startswith("bytes ") and name:
            out[name] += [int(x, 16) for x in line.split()[1:]]
    return out


def phoneme_codes(tag):
    """Which phoneme each code is, from the language's own table."""
    import subprocess
    out = {}
    try:
        r = subprocess.run([sys.executable,
                            os.path.join(ROOT, "tools", "module",
                                         "phonemes.py"), tag],
                           capture_output=True, text=True)
    except Exception:
        return out
    for line in r.stdout.split("\n"):
        m = re.match(r'\s*\d+\s+(\S+)\s+numbers (\d+)', line)
        if m:
            out[int(m.group(2))] = m.group(1)
    return out


def symbols(tag):
    """Where each address the rules name falls: store and offset."""
    path = os.path.join(ROOT, "lang", tag, "rules", "symbols")
    out = {}
    if not os.path.exists(path):
        return out
    n = 0
    for line in open(path):
        if line.startswith("#") or not line.strip():
            continue
        f = line.split()
        if f[0] == "at" and len(f) >= 5:
            out[n] = (f[1], f[2], f[3], int(f[4]))
            n += 1
    return out


def rule_bodies(tag):
    """Every rule written as C, by name, as a list of lines."""
    out = {}
    d = os.path.join(ROOT, "lang", tag)
    for f in sorted(os.listdir(d)):
        if not re.match(r'delta_rules_c\d+_%s\.c$' % tag, f):
            continue
        name = None
        for line in open(os.path.join(d, f)):
            m = RULE.match(line)
            if m:
                name = m.group(1)
                out[name] = {"object": m.group(2), "file": f, "lines": []}
                continue
            if name is not None:
                out[name]["lines"].append(line)
    return out


def read_rule(lines):
    """The base locus and every override, by the block that sets it."""
    where = "base"
    order = ["base"]
    sets = {}
    guards = {}
    for line in lines:
        m = LABEL.match(line)
        if m:
            where = m.group(1)
            if where not in order:
                order.append(where)
            continue
        for m in TEST.finditer(line):
            guards.setdefault(where, []).append(
                ("string", int(m.group(2)), int(m.group(3)),
                 int(m.group(4))))
        if not TEST.search(line):
            for m in OTHER.finditer(line):
                guards.setdefault(where, []).append(
                    (m.group(1), 0, 0, -int(m.group(2)) - 1))
        m = VALUE.search(line)
        if m:
            slot, v = m.group(1), int(m.group(2))
            if slot in SLOT:
                sets.setdefault(where, []).append(
                    (SLOT[slot], "keep" if v == KEEP else v))
                if where not in order:
                    order.append(where)
    return order, sets, guards


def main(argv):
    tag = argv[1] if len(argv) > 1 else "enus"
    syms = symbols(tag)
    blobs = stores(tag)
    codes = phoneme_codes(tag)
    bodies = rule_bodies(tag)
    names = sorted(n for n in bodies if n.endswith("_Fv"))
    if not names:
        sys.stderr.write("formants: no _Fv rules in lang/%s\n" % tag)
        return 1

    print("# What each place of articulation does to the formants, read out of")
    print("# %s's own rules. Values are the rules' own integers; `keep' is"
          % tag)
    print("# their sentinel for leaving a target unset. `a' is the half before")
    print("# the consonant and `b' the half after. Written by")
    print("# tools/module/formants.py -- see its head for how the slots were")
    print("# identified, which was by measurement and not by any name.")
    total = 0
    for name in names:
        b = bodies[name]
        order, sets, guards = read_rule(b["lines"])
        n = sum(1 for k in order if sets.get(k))
        total += n
        print()
        print("place %s from %s in %s" % (name, b["object"], b["file"]))
        for k in order:
            if not sets.get(k):
                continue
            vals = "  ".join("%s=%s" % (s, v) for s, v in sets[k])
            print("  %-9s %s" % (k, vals))
            for kind, st, wide, num in guards.get(k, [])[:4]:
                if kind != "string":
                    print("      after %s %d" % (kind, -num - 1))
                    continue
                if num not in syms:
                    print("      when the scan matches symbol %d" % num)
                    continue
                _, symname, store, off = syms[num]
                blob = blobs.get(store, [])
                seq = blob[off:off + max(1, wide)]
                said = " ".join(codes.get(c, "?%d" % c) for c in seq)
                print("      when the next is %-10s (%s +%d, %s)"
                      % (said or "?", store, off, symname))
    print()
    print("# %d rules, %d blocks that set a formant value" % (len(names), total))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
