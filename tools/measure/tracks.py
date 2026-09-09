#!/usr/bin/env python3
"""Fit a phoneme's parameter tracks to breakpoints, exactly.

The synthesiser is told sixty-two numbers a frame and it is told them as
piecewise-linear tracks: a parameter is given targets and walks a straight
line between them. So the faithful way to write a phoneme down is the way the
engine already thinks of it -- per parameter, the frames at which it turns and
the value it turns at -- and not as a fixed number of targets, which cannot
express what /aU/ actually does.

Between two breakpoints the value is

    v0 + int((v1 - v0) * min(den, 2 * (i - i0)) / den)

with `den' the segment's length in half-frames and the conversion truncating
towards nought, which is what C does. `den' is twice the segment's length in
frames for almost every segment; where it is not, the fit says so and the
table carries it.

    tools/measure/tracks.py <probe> <phoneme>...
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import replay as R                                       # noqa: E402

# f0 is the intonation and belongs to the utterance, not the phoneme.
SKIP = ("step", "f0")

HEAD = [
    "# Measured, not read. Each case is spoken as a pronunciation annotation",
    "# and what the synthesiser was told is written down frame by frame, then",
    "# fitted to the breakpoints that reproduce it exactly:",
    "# tools/measure/tracks.py does the fitting, tools/measure/replay.py holds",
    "# this table against the engine again.",
    "#",
    "# A parameter is a piecewise-linear track, which is how the synthesiser is",
    "# told it. Between two breakpoints,",
    "#",
    "#     v0 + int((v1 - v0) * min(den, 2 * (i - i0)) / den)",
    "#",
    "# with den the segment's length in half-frames and the conversion",
    "# truncating towards nought, which is what C does. A `/den' after a",
    "# breakpoint says the segment does not take the whole way to the next",
    "# one -- so `42:700/20' reaches 700 ten frames later and holds it, which",
    "# is how a formant settles before the sound ends.",
    "#",
    "# Absolute frame numbers, five milliseconds a frame, at the one duration",
    "# each case was measured at. What a shorter or longer one does is not",
    "# known yet.",
]

BLURB = {
    "pairs2": [
        "# The same 416 pairs, each against a different partner.",
        "#",
        "# Carrier k is vowel k, the consonant, vowel k+3, where the first",
        "# square used k+1. So every (consonant, vowel) pair is measured a",
        "# second time with a different neighbour on the other side, which is",
        "# what tests separability: if the value a vowel sets does not depend",
        "# on the vowel at the far end, the two squares must agree.",
        "#",
    ] + HEAD,
    "pairs": [
        "# English's consonants against every vowel, both sides.",
        "#",
        "# One Latin square a consonant: carrier k is vowel k, the consonant,",
        "# vowel k+1. Every vowel therefore appears once before the consonant",
        "# and once after it, which is enough because the locus separates --",
        "# across a consonant f1 holds one value the FOLLOWING vowel sets while",
        "# f2 and f3 ramp from a value the PRECEDING vowel sets to one the",
        "# following vowel sets. See docs/authoring.md. 416 carriers rather",
        "# than the 6,656 of the full cross product.",
        "#",
    ] + HEAD,
    "vowels": ["# English's vowels, as the engine says them.", "#"] + HEAD,
    "consonants": [
        "# English's consonants, as the engine says them between two /a/.",
        "#",
        "# A consonant cannot be measured on its own: asked for one, the engine",
        "# spells the letter out and gives four hundred frames of it. So each",
        "# is measured in a carrier, and what is written down is the whole",
        "# carrier rather than the consonant, because the consonant's own",
        "# values cannot be separated from it -- see docs/authoring.md. /m/",
        "# between two /a/ holds f2 flat at 1000; between two /i/ it runs 1300",
        "# to 1400 and between two /u/ 850 to 1000, and its f1 is 300 after /a/",
        "# against 250 after either. The locus is coarticulated, so these",
        "# tables are twenty-six utterances and not twenty-six consonants.",
        "#",
    ] + HEAD,
}


def seg_ok(seq, i0, i1, den):
    v0, v1 = seq[i0], seq[i1]
    d = v1 - v0
    for i in range(i0, i1 + 1):
        t = min(den, 2 * (i - i0))
        if seq[i] != v0 + int(d * t / float(den)):
            return False
    return True


def seg_den(seq, i0, i1):
    """The denominator that reproduces this segment, or None."""
    span = 2 * (i1 - i0)
    for den in [span] + [span - 1, span + 1] + list(range(1, 4 * span + 2)):
        if den > 0 and seg_ok(seq, i0, i1, den):
            return den
    return None


def fit(seq):
    """Fewest breakpoints that reproduce the sequence exactly."""
    n = len(seq)
    if n == 0:
        return []
    # Greedy from the left, taking the longest segment that still fits. Greedy
    # is not provably minimal but it is exact, which is what matters, and on
    # these tracks it finds the turns a person would draw.
    out = []
    i0 = 0
    while i0 < n - 1:
        best = None
        for i1 in range(n - 1, i0, -1):
            den = seg_den(seq, i0, i1)
            if den is not None:
                best = (i1, den)
                break
        if best is None:
            i0 += 1
            out.append((i0, seq[i0], 0))
            continue
        i1, den = best
        out.append((i0, seq[i0], den if den != 2 * (i1 - i0) else 0))
        i0 = i1
    out.append((n - 1, seq[n - 1], 0))
    return out


def tracks(probe, text):
    """Every parameter that moves, as breakpoints, plus the ones that do not."""
    idx = {n: i for i, n in enumerate(R.NAMES)}
    got = R.frames_of(probe, text)
    live = [r for r in got
            if any(r[idx[s]] >= 20 for s in ("av", "af", "ah"))]
    moving, still = {}, {}
    for name in R.NAMES:
        if name in SKIP:
            continue
        seq = [r[idx[name]] for r in live]
        if not seq:
            continue
        if all(v == seq[0] for v in seq):
            still[name] = seq[0]
        else:
            moving[name] = fit(seq)
    return len(live), moving, still


# English's vowels, each said on its own, and its consonants, each said
# between two /a/ -- a consonant cannot be said on its own at all, since the
# engine spells the letter out instead and gives four hundred frames of it.
VOWELS = "i I e E A a u U o c Y W O X H x".split()
CONSONANTS = ("b p d t F k g D T v f z s Z S J C h m n G r l y w R").split()


def corpus(which):
    """The name and the text of each case."""
    if which == "vowels":
        return [(v, "`[.1%s]" % v) for v in VOWELS]
    if which == "consonants":
        return [(c, "`[.1a%sa]" % c) for c in CONSONANTS]
    # One Latin square a consonant: every vowel once before it and once
    # after. That is what separability buys -- `aCi' reports /a/-before-C
    # and C-before-/i/ in the same utterance -- so 416 carriers cover
    # every consonant against every vowel on both sides rather than the
    # 6,656 the full cross product would want.
    step = 1 if which == "pairs" else 3
    out = []
    for c in CONSONANTS:
        for k, v1 in enumerate(VOWELS):
            v2 = VOWELS[(k + step) % len(VOWELS)]
            out.append(("%s:%s%s" % (c, v1, v2),
                        "`[.1%s%s%s]" % (v1, c, v2)))
    return out


def emit(probe, which, path, blurb):
    """Fit every case in the corpus and write the table."""
    cases = corpus(which)
    fitted = [(name, text) + tracks(probe, text) for name, text in cases]

    # What every case holds at the same value, said once.
    common = {}
    for name, val in fitted[0][4].items():
        if all(f[4].get(name) == val for f in fitted):
            common[name] = val

    out = list(blurb)
    out += ["#", "# What every case holds still, said once:", "#"]
    line = "shared"
    for name in sorted(common):
        if len(line) > 66:
            out.append(line)
            line = "shared"
        line += " %s=%d" % (name, common[name])
    out.append(line)
    out.append("#")
    for name, text, n, moving, still in fitted:
        out.append("")
        out.append("phone %s text=%s frames=%d" % (name, text, n))
        for k in sorted(still):
            if k not in common:
                out.append("  still %-4s %d" % (k, still[k]))
        for k in sorted(moving):
            pts = " ".join("%d:%d%s" % (i, v, "/%d" % d if d else "")
                           for i, v, d in moving[k])
            out.append("  track %-4s %s" % (k, pts))
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")
    return len(fitted), len(common)


def show(probe, text):
    n, moving, still = tracks(probe, text)
    print("phone ? text=%s frames=%d" % (text, n))
    for name in sorted(still):
        print("  still %-4s %d" % (name, still[name]))
    for name in sorted(moving):
        pts = " ".join("%d:%d%s" % (i, v, "/%d" % d if d else "")
                       for i, v, d in moving[name])
        print("  track %-4s %s" % (name, pts))


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    if argv[2] in ("vowels", "consonants", "pairs", "pairs2"):
        which = argv[2]
        path = os.path.join(os.path.dirname(os.path.dirname(
            os.path.dirname(os.path.abspath(__file__)))),
            "lang", "measured", "enus-%s.txt" % which)
        blurb = BLURB[which]
        cases, common = emit(argv[1], which, path, blurb)
        print("%s: %d cases, %d shared stills, written to %s"
              % (which, cases, common, os.path.relpath(path)))
        return 0
    for t in argv[2:]:
        show(argv[1], t)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
