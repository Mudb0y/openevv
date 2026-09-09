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


def show(probe, phoneme):
    n, moving, still = tracks(probe, "`[.1%s]" % phoneme)
    print("phoneme %s frames=%d" % (phoneme, n))
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
    for p in argv[2:]:
        show(argv[1], p)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
