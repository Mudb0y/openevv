#!/usr/bin/env python3
"""How long a segment lasts, as a base times factors rather than a table.

`lang/enus/enus.durations` is 94,089 contexts holding 26,440 distinct
lengths, which is most of what the three data files weigh. It is that big
because a duration is a number of milliseconds and every context gets its
own, and it is the one of the three that a model could replace outright:
a base for the phoneme at its stress, multiplied by a factor for each of the
things the key measurement found mattered -- where the syllable is, what its
coda is, whether the next syllable has an onset, and the rest.

So this fits one. Coordinate descent in logs, which is the same thing as
iterative proportional fitting: start with the base as the mean of each
phoneme's lengths, then take each factor in turn, set every level of it to
the mean residual at that level, and go round again. It converges in a
handful of passes and needs no linear algebra.

What comes out is the residual, and the residual is the answer: a model that
leaves every length within a few milliseconds replaces the table, and one
that does not says the table is carrying something a product of factors
cannot.

    tools/module/durmodel.py [--load <path>] [--words N]
"""

import collections
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools", "measure"))

import durations as D                                    # noqa: E402
import generate as G                                     # noqa: E402

# The base is the phoneme and its stress; everything else is a factor on it.
BASE = ("unit", "stress")
FACTORS = ("onset", "coda", "syl", "nsyl", "fromend", "prev", "next",
           "nexton", "left", "right")


def rows(tag):
    """Every context in the duration table, with its length."""
    t = G.load_durations(tag)
    out = []
    for k, v in t.items():
        f = dict(zip(D.KEY, k))
        out.append((f, v[0]))
    return out


def fit(data, passes=12):
    """Base and factors, by coordinate descent in logs."""
    base = {}
    level = {name: {} for name in FACTORS}
    for f, dur in data:
        base.setdefault(tuple(f[x] for x in BASE), []).append(math.log(dur))
    base = {k: sum(v) / len(v) for k, v in base.items()}
    for _ in range(passes):
        for name in FACTORS:
            got = collections.defaultdict(list)
            for f, dur in data:
                pred = base[tuple(f[x] for x in BASE)]
                for other in FACTORS:
                    if other != name:
                        pred += level[other].get(f[other], 0.0)
                got[f[name]].append(math.log(dur) - pred)
            level[name] = {k: sum(v) / len(v) for k, v in got.items()}
        got = collections.defaultdict(list)
        for f, dur in data:
            pred = sum(level[n].get(f[n], 0.0) for n in FACTORS)
            got[tuple(f[x] for x in BASE)].append(math.log(dur) - pred)
        base = {k: sum(v) / len(v) for k, v in got.items()}
    return base, level


def predict(base, level, f):
    p = base.get(tuple(f[x] for x in BASE))
    if p is None:
        return None
    for name in FACTORS:
        p += level[name].get(f[name], 0.0)
    return int(round(math.exp(p)))


def main(argv):
    tag = "enus"
    data = rows(tag)
    sys.stderr.write("durmodel: %d contexts\n" % len(data))
    base, level = fit(data)
    err = []
    for f, dur in data:
        got = predict(base, level, f)
        if got is not None:
            err.append(abs(got - dur))
    err.sort()
    n = len(err)
    within = lambda k: 100 * sum(1 for e in err if e <= k) // n
    print("%d lengths modelled by %d bases and %d factor levels"
          % (n, len(base), sum(len(v) for v in level.values())))
    print("   median error %d ms, mean %.1f, worst %d"
          % (err[n // 2], sum(err) / float(n), err[-1]))
    print("   within 2 ms %d%%, 5 ms %d%%, 10 ms %d%%, 20 ms %d%%"
          % (within(2), within(5), within(10), within(20)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
