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
    """The consonant's own stretch, by the marker the engine itself gives.

    Aspiration sits at 34 through a vowel and at nought through a consonant,
    and that boundary is exact: over the twenty-six consonants between two
    /a/, the `ah == 0' span agrees frame for frame with the stretch at one f1
    for twenty of them, and the ones it does not agree about are the ones a
    plateau is the wrong question for. /h/ is aspiration, so it never
    suppresses it; /r/, /l/, /y/, /w/ and /R/ are sonorants with no closure,
    and a glide is a continuous transition rather than a target held. For
    those six this answers None rather than guessing, which is what an earlier
    version of this did -- it looked for the longest interior run at one f1
    and found stretches of the vowels instead, reporting an /f1/ of 272
    against 742 for the same pair and making it look as though the locus does
    not separate.
    """
    zero = [i for i, f in enumerate(frames) if f.get("ah", 34) == 0]
    if not zero:
        return None
    return (zero[0], zero[-1])


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
            out.setdefault("__noplateau__", set()).add(cons)
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
    skipped = (one.pop("__noplateau__", set())
               | two.pop("__noplateau__", set()))
    if skipped:
        print("no plateau to compare, so left out: %s"
              % " ".join(sorted(skipped)))
        print("(/h/ is aspiration; r l y w R are sonorants with no closure)")
        print()
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
    skipped = tbl.pop("__noplateau__", set())
    if skipped:
        print("no plateau: %s" % " ".join(sorted(skipped)))
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
