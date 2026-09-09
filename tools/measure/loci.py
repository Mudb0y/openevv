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


def span_where(frames, test, middle=False):
    """The longest run of frames the test holds over, or None.

    A run, not the outer bounds of every match. Aspiration goes to nought
    over exactly one stretch so the difference never showed, but voicing goes
    to nought twice -- once for /h/ and again for the utterance's own release
    at the end -- and taking the first and last match merged the two. In
    `Aha' that made /h/'s anchor run from frame 44 to 107 of 108, so the
    composition held the voice at nought from the consonant to the end of the
    word and never recovered. The third bandwidth has the same shape: /R/
    picked up a single frame at the very end instead of its own thirty.

    A run touching either edge is the utterance's own onset or release rather
    than a consonant, so only interior runs count and a marker that finds
    none answers None -- which lets the next marker be tried. Falling back to
    an edge run instead is what left /R/ anchored on frame 94 of 95: its
    aspiration touches nought on the last frame alone, that was taken as the
    consonant, and the third bandwidth -- which moves 150 to 250 and back over
    frames 25 to 55, exactly the /R/ -- was never looked at.
    """
    runs = []
    i, n = 0, len(frames)
    while i < n:
        if test(frames[i]):
            j = i
            while j + 1 < n and test(frames[j + 1]):
                j += 1
            runs.append((i, j))
            i = j + 1
        else:
            i += 1
    inner = [r for r in runs if r[0] > 0 and r[1] < n - 1]
    if middle:
        # /W/ is the one vowel that moves the third bandwidth itself, 150 to
        # 500 and back, so in a carrier like `WlX' it cannot be told from the
        # consonant's own movement by value. It can be told by place: a
        # consonant in a carrier is in the middle, and a vowel's own movement
        # is not. Without this /l/ between /W/ and /X/ took the vowel's
        # stretch and put a hundred and forty milliseconds of silence in.
        mid = [r for r in inner
               if n // 4 <= (r[0] + r[1]) // 2 <= (3 * n) // 4]
        inner = mid or inner
    if not inner:
        return None
    return max(inner, key=lambda r: r[1] - r[0])


def hold(frames):
    """The consonant's own stretch, by whichever marker the engine gives.

    Three markers, tried in turn, because no one of them covers every manner.

    **Aspiration going to nought** is the obstruents and the nasals, and it is
    exact: over the twenty-six consonants between two /a/ the `ah == 0' span
    agrees frame for frame with the stretch held at one f1 for twenty of them.

    **Voicing going to nought** is /h/, which is aspiration itself and so
    never suppresses it -- it lifts `ah' from 34 to 37 instead -- but does
    stop the voice.

    **The third bandwidth leaving the vowel's own value** is the five
    sonorants, /r/, /l/, /y/, /w/ and /R/. They have no amplitude marker at
    all: voicing stays up, frication stays at nought, aspiration stays at its
    baseline, and only the formants move. But every one of them takes `b3'
    away from the 150 a vowel holds -- to 250, 400, 500, 550 and 250 -- with
    the extremum squarely in the interior, so the departure is the anchor.

    An earlier version of this had only the first marker and answered None for
    the other six, which was honest but cost 96 of the 416 carriers at 175
    words a minute and 124 at 700. The one thing it must not do is guess: a
    version before that took the longest interior run at one f1, silently
    found stretches of the *vowels*, and reported an f1 of 272 against 742 for
    the same pair.
    """
    if not frames:
        return None
    span = span_where(frames, lambda f: f.get("ah", 34) == 0)
    if span is not None:
        return span
    span = span_where(frames, lambda f: f.get("av", 50) == 0)
    if span is not None:
        return span
    base = frames[0].get("b3")
    if base is None:
        return None
    return span_where(frames, lambda f: f.get("b3") != base, middle=True)


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
