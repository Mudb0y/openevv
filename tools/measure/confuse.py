#!/usr/bin/env python3
"""Does a generated segment still sound like the phoneme it is meant to be?

Counting wrong parameter values says how far the tables are from the engine
and says nothing about what that costs. Two of the three faults Stas heard --
`banana' as vanana, `abandonment' as abandonwend -- were a per cent or two of
values in one segment, invisible against a total, and each turned one phoneme
into another. A measure that finds those without an ear has to ask a
different question: not how far a segment is from where it should be, but
whether it is now nearer to some other phoneme.

So this builds a centroid for every phoneme from the engine's own frames --
the mean of each parameter over every segment of that phoneme in the corpus,
scaled by that parameter's spread so a formant and an amplitude count alike
-- and then asks, of each generated segment, which centroid it lands nearest.
A segment that lands on another phoneme is a confusion and is named. It is
the same test a listener performs, done arithmetically, and it is cheap.

    tools/measure/confuse.py <probe> [--words N] [--tag enus]
"""

import collections
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "module"))
sys.path.insert(0, os.path.join(ROOT, "tools", "measure"))

import segments as S                                     # noqa: E402
import durations as D                                    # noqa: E402
import fromtable as F                                    # noqa: E402
import generate as G                                     # noqa: E402
import replay as R                                       # noqa: E402

# What a listener has to go on: where the formants are, how wide they are,
# and how the thing is being excited.
FEATURES = ("f1", "f2", "f3", "f4", "f5", "b1", "b2", "b3",
            "av", "af", "ah", "fnz", "tl", "ab")


def mean_over(frames, lo, hi):
    """One segment's parameters, averaged over the frames inside it."""
    a = max(0, lo // 5)
    b = min(len(frames), (hi + 4) // 5)
    if b <= a:
        return None
    out = []
    for name in FEATURES:
        j = R.NAMES.index(name)
        out.append(sum(frames[i][j] for i in range(a, b)) / float(b - a))
    return out


def spread(vectors):
    """Each feature's standard deviation, so the distances are comparable."""
    n = len(vectors)
    out = []
    for k in range(len(FEATURES)):
        m = sum(v[k] for v in vectors) / n
        s = math.sqrt(sum((v[k] - m) ** 2 for v in vectors) / n)
        out.append(s if s > 1e-6 else 1.0)
    return out


def nearest(vec, centroids, scale):
    best = None
    for name, c in centroids.items():
        d = sum(((vec[k] - c[k]) / scale[k]) ** 2
                for k in range(len(FEATURES)))
        if best is None or d < best[1]:
            best = (name, d)
    return best


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    probe = argv[1]

    def opt(name, dflt):
        return argv[argv.index(name) + 1] if name in argv else dflt

    tag = opt("--tag", "enus")
    limit = int(opt("--words", "300"))
    base, over, far = F.load(tag)
    durs = G.load_durations(tag)
    S.setup(probe)

    engine = collections.defaultdict(list)
    made = []
    words = S.annotations(tag)[:limit]
    dflt = G.defaults(probe, words[0][1])
    for word, body in words:
        pros = D.prosody(body)
        frames = R.frames_of(probe, body)
        runs = S.tap(probe, body)
        if not frames or len(runs) != len(pros) + 1:
            continue
        got = G.gaps_from_tables(pros, durs, base, over, dflt, far)
        if got is None:
            continue
        gaps, total = got
        # The generated frames, on the engine's own grid.
        built = []
        for i in range(len(frames)):
            t = i * 5
            if t >= total:
                break
            row = list(frames[i])
            for name, gs in gaps.items():
                v = F.value(gs, t)
                if v is not None:
                    row[R.NAMES.index(name)] = v
            built.append(row)
        at = 0
        for i, f in enumerate(pros):
            d = runs[i][1] - runs[i][0]
            a, b = at, at + d
            at += d
            v = mean_over(frames, a, b)
            w = mean_over(built, a, b)
            if v is None or w is None:
                continue
            engine[f["unit"]].append(v)
            made.append((f["unit"], word, w, v))

    centroids = {}
    for unit, rows in engine.items():
        if len(rows) < 3:
            continue
        centroids[unit] = [sum(r[k] for r in rows) / len(rows)
                           for k in range(len(FEATURES))]
    scale = spread([v for rows in engine.values() for v in rows])

    # The engine's own segments are the control, and they must be counted
    # the same way. A centroid is one mean over a whole segment, so two
    # phonemes that are genuinely close -- the two schwas, /r/ and /R/ --
    # land on each other whoever made them. Without the control the rate
    # says nothing; with it, the difference is what the tables cost.
    wrong = collections.Counter()
    example = {}
    n = ours = theirs = 0
    for unit, word, mine, engs in made:
        if unit not in centroids:
            continue
        n += 1
        a = nearest(mine, centroids, scale)[0]
        b = nearest(engs, centroids, scale)[0]
        if b != unit:
            theirs += 1
        if a != unit:
            ours += 1
            if b == unit:
                # Only where the engine's own lands right and ours does not
                # is a confusion the tables caused.
                wrong[(unit, a)] += 1
                example.setdefault((unit, a), word)
    print("%d segments. The engine's own land on another phoneme %d times "
          "(%.1f per cent), ours %d (%.1f)"
          % (n, theirs, 100.0 * theirs / n, ours, 100.0 * ours / n))
    print("%d are ours alone -- the engine's landed right and ours did not "
          "(%.2f per cent)" % (sum(wrong.values()),
                               100.0 * sum(wrong.values()) / n))
    for (unit, near), c in wrong.most_common(12):
        print("   %-3s heard as %-3s  %4d times, e.g. %s"
              % (unit, near, c, example[(unit, near)]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
