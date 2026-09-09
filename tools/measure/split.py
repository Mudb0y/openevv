#!/usr/bin/env python3
"""How much of a carrier is settled by one neighbour, frame by frame.

Reading a consonant's locus off its plateau is unreliable where the plateau is
short -- /p/ closes for a single frame, /k/ for two or three -- so the value
read is a point on a slope that has not finished moving. The question a front
end actually asks is not what the locus is but how much of the utterance it
can generate knowing only what is next to what: given the vowel before a
consonant and the consonant, is the run-in to it fixed?

That is answerable without picking any locus at all. The two Latin squares
hold, for every (consonant, vowel) pair, two carriers sharing that vowel and
that consonant and differing in the vowel at the far end. So align them at the
consonant's closure and walk outwards: the frame at which they first differ is
the reach of the far vowel. If the run-in agrees all the way back into the
vowel, everything up to the closure is a function of the near pair alone, and
a front end can generate it from a table of pairs.

    tools/measure/split.py <table> <table>
"""

import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import loci as L                                         # noqa: E402
import replay as R                                       # noqa: E402


def frames(path):
    """Every case's frames and its closure, from the table alone."""
    shared, phones = R.table(path)
    out = {}
    for name, phone in phones.items():
        if ":" not in name:
            continue
        cons, vv = name.split(":", 1)
        if len(vv) != 2:
            continue
        fr = R.build(shared, phone)
        span = L.hold(fr)
        if span is None:
            continue
        out[name] = (cons, vv[0], vv[1], fr, span)
    return out


def reach(a, b, at_a, at_b, back):
    """Frames of agreement outward from the closure, in one direction."""
    n = 0
    while True:
        i, j = at_a + (-n if back else n), at_b + (-n if back else n)
        if i < 0 or j < 0 or i >= len(a) or j >= len(b):
            return n, True
        if a[i] != b[j]:
            return n, False
        n += 1


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    one, two = frames(argv[1]), frames(argv[2])

    # Timing is what this can settle, and it settles it completely. The
    # values cannot be tested this way and it is worth saying why: the
    # transition into a consonant aims at an f1 the FOLLOWING vowel sets, so
    # two carriers sharing their first vowel and their consonant are supposed
    # to differ through the run-in. Comparing their frames therefore measures
    # the design and not the separability. What can be compared is when
    # things happen, and for a front end that is half the question.
    for label, key in (("the vowel before it", 1), ("the vowel after it", 2)):
        seen = collections.defaultdict(dict)
        for tbl, w in ((one, 0), (two, 1)):
            for name, rec in tbl.items():
                seen[(rec[0], rec[key])][w] = rec
        pairs = start = length = runout = 0
        offby = collections.Counter()
        for (cons, v), both in seen.items():
            if len(both) != 2:
                continue
            _, _, _, fa, sa = both[0]
            _, _, _, fb, sb = both[1]
            pairs += 1
            if sa[0] == sb[0]:
                start += 1
            la, lb = sa[1] - sa[0] + 1, sb[1] - sb[0] + 1
            if la == lb:
                length += 1
            else:
                offby[abs(la - lb)] += 1
            if len(fa) - sa[1] == len(fb) - sb[1]:
                runout += 1
        print("=== a consonant and %s, over %d pairs" % (label, pairs))
        print("  closure begins at the same frame:   %3d of %d"
              % (start, pairs))
        print("  closure lasts the same many frames: %3d of %d%s"
              % (length, pairs,
                 ("  (off by %s)" % ", ".join(
                     "%d in %d" % (v, c) for v, c in sorted(offby.items())))
                 if offby else ""))
        print("  same many frames follow it:         %3d of %d"
              % (runout, pairs))
        print()
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
