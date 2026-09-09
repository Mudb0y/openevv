#!/usr/bin/env python3
"""What each vowel does to each consonant, read out of the measured tables.

Across a consonant between two vowels, f1 holds one value and f2 and f3 ramp
straight from one value to another. This finds that stretch in every measured
carrier and reports the value at each end of it, so the question "what does an
/i/ before an /m/ do to it" has a number rather than an impression.

The stretch is the longest run of frames in the interior at one f1, which is
what a consonant is in this engine whatever its manner: /m/ between two /a/
holds f1 at 300, /t/ and /s/ hold it at 300 too, and what tells those apart is
the frication and the voicing rather than the formants.

Given two tables measured as different Latin squares, `--compare' holds the
one against the other. Every (consonant, vowel) pair appears in both, with a
different vowel at the far end, so agreement between them is the test of
whether a value belongs to the neighbour that sets it or to the pair as a
whole. Disagreement is coarticulation reaching further than one phoneme.

    tools/measure/loci.py <table> [<table> --compare]
"""

import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import replay as R                                       # noqa: E402

# What is worth reporting: the formants and bandwidths that shape a consonant,
# the voicing and the two noise sources that say what manner it is.
WATCH = ("f1", "f2", "f3", "b1", "b2", "b3", "f4", "f5", "fnz", "av", "af",
         "ah")


def rebuild(shared, phone):
    """Every frame of one case, from the table alone -- no engine needed."""
    return R.build(shared, phone)


def hold(frames):
    """The consonant's own stretch: longest interior run at one f1."""
    n = len(frames)
    if n < 5:
        return None
    f1 = [f["f1"] for f in frames]
    best = None
    i = 1
    while i < n - 1:
        j = i
        while j + 1 < n - 1 and f1[j + 1] == f1[i]:
            j += 1
        if f1[i] != f1[0] and f1[i] != f1[n - 1]:
            if best is None or (j - i) > (best[1] - best[0]):
                best = (i, j)
        i = j + 1
    return best


def read(path):
    """Each case's name, its two vowels, and the loci at each end."""
    shared, phones = R.table(path)
    out = {}
    for name, phone in phones.items():
        if ":" not in name:
            continue
        cons, vv = name.split(":", 1)
        if len(vv) != 2:
            continue
        frames = rebuild(shared, phone)
        span = hold(frames)
        if span is None:
            continue
        a, b = span
        out[name] = {
            "cons": cons, "before": vv[0], "after": vv[1],
            "at": a, "len": b - a + 1,
            "in": {p: frames[a][p] for p in WATCH if p in frames[a]},
            "out": {p: frames[b][p] for p in WATCH if p in frames[b]},
        }
    return out


def compare(one, two):
    """Hold each square's loci against the other's for the same pair."""
    # A vowel before a consonant sets the `in' locus; after it, the `out'.
    agree = collections.Counter()
    differ = collections.Counter()
    worst = collections.defaultdict(list)
    for side, key in (("in", "before"), ("out", "after")):
        seen = collections.defaultdict(dict)
        for tbl, which in ((one, 0), (two, 1)):
            for name, rec in tbl.items():
                seen[(rec["cons"], rec[key], side)][which] = rec
        for pair, both in seen.items():
            if len(both) != 2:
                continue
            cons, v, _ = pair
            for p in WATCH:
                a = both[0][side].get(p)
                b = both[1][side].get(p)
                if a is None or b is None:
                    continue
                if a == b:
                    agree[p] += 1
                else:
                    differ[p] += 1
                    worst[p].append((abs(a - b), cons, v, side, a, b))
    print("%-4s %7s %7s %6s  %s" % ("par", "agree", "differ", "worst",
                                    "where the worst is"))
    for p in WATCH:
        tot = agree[p] + differ[p]
        if not tot:
            continue
        w = max(worst[p]) if worst[p] else None
        where = ""
        if w:
            where = "%s %s %s: %d against %d" % (w[1], w[3], w[2], w[4], w[5])
        print("%-4s %7d %7d %6s  %s"
              % (p, agree[p], differ[p], w[0] if w else "-", where))
    ta, td = sum(agree.values()), sum(differ.values())
    print()
    print("%d of %d values agree between the two squares, %.1f per cent"
          % (ta, ta + td, 100.0 * ta / max(1, ta + td)))


def main(argv):
    args = [a for a in argv[1:] if a != "--compare"]
    if not args:
        sys.stderr.write(__doc__)
        return 2
    if "--compare" in argv and len(args) == 2:
        compare(read(args[0]), read(args[1]))
        return 0
    tbl = read(args[0])
    print("%-9s %4s %4s  %s" % ("case", "at", "len",
          " ".join("%s" % p for p in ("f1", "f2", "f3", "av", "af"))))
    for name in sorted(tbl):
        r = tbl[name]
        print("%-9s %4d %4d  %s" % (
            name, r["at"], r["len"],
            " ".join("%d/%d" % (r["in"].get(p, 0), r["out"].get(p, 0))
                     for p in ("f1", "f2", "f3", "av", "af"))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
