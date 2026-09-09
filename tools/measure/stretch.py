#!/usr/bin/env python3
"""Does a phoneme's body stretch, and by what law?

Every breakpoint in the measured tables is an absolute frame number at the one
length that case happened to be, which is why the tables describe utterances
rather than phonemes. This asks whether a phoneme at one length can be
produced from the same phoneme at another, and the answer is yes.

A vowel before a voiceless consonant is shorter than the same vowel before a
voiced one -- /a/ runs 32 frames before /p/ and 37 before /b/ -- so the pair
corpus already holds vowels at two lengths and no new measurement is needed.
Comparing the two:

The voice-quality onsets do not stretch. Open quotient rises 18 27 36 45 54
and diplophonia falls 100 77 53 29 5 over five frames whatever the length.

The body does stretch, and it keeps its endpoints. /a/'s f2 glides 1200 to
1151 at both lengths, in 26 steps at the short one and 33 at the long, and the
engine's own segment arithmetic reproduces both from the same two numbers:
`v0 + int((v1 - v0) * min(den, 2 * i) / den)' with den twice the number of
steps.

So a phoneme is two endpoints and a length, and that is what makes the tables
about phonemes rather than about the utterances they were measured in.

    tools/measure/stretch.py
"""

import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import compose as K                                      # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))

# The five frames at the start that do not stretch.
ONSET = 5

VOWELS = "i I e E A a u U o c Y W O X H x".split()
WATCH = ("f1", "f2", "f3", "b1", "b2", "b3", "f4", "f5", "fnz", "oq", "di",
         "av", "ah", "af")


def ramp(v0, v1, n):
    """`n' frames of the engine's own straight line from v0 to v1."""
    if n <= 1:
        return [v0] * max(0, n)
    den = 2 * (n - 1)
    return [v0 + int((v1 - v0) * min(den, 2 * i) / float(den))
            for i in range(n)]


def body_end(seq):
    """Where the phoneme's own body stops and the run-in to the next begins.

    The body is the longest straight line the law describes from the end of
    the onset, and the run-in is whatever is left. That boundary cannot be
    assumed to be the closure: /a/ before /p/ glides its f2 over 26 frames and
    then runs in over 6, and before /b/ over 33 and then 4, the run-in
    belonging to the consonant rather than the vowel. Taking the whole stretch
    up to the closure as the body is what made a first version of this unable
    to describe even its own source.
    """
    n = len(seq)
    best = ONSET
    for k in range(ONSET, n):
        if seq[ONSET:k + 1] == ramp(seq[ONSET], seq[k], k - ONSET + 1):
            best = k
    return best


def main(argv):
    here = os.path.join(ROOT, "lang", "measured")
    cases = {}
    for f in ("enus-pairs.txt", "enus-pairs2.txt", "enus-holdout.txt"):
        p = os.path.join(here, f)
        if os.path.exists(p):
            cases.update(K.cases(p))

    # Group each vowel's carriers by how long it runs before the closure.
    byv = collections.defaultdict(lambda: collections.defaultdict(list))
    for name, r in cases.items():
        byv[r["v1"]][r["span"][0]].append((name, r))

    print("Predicting each vowel's body at one length from the same vowel at")
    print("another, by the ramp law. A body is frames %d to the closure."
          % ONSET)
    print()
    print("%-3s %8s %8s  %s" % ("V", "from", "to", "parameters reproduced"))
    total = good = 0
    for v in VOWELS:
        lens = sorted(byv[v])
        if len(lens) < 2:
            print("%-3s %8s %8s  only one length measured" % (v, lens, "-"))
            continue
        src = byv[v][lens[0]][0][1]
        dst = byv[v][lens[-1]][0][1]
        m, n = lens[0], lens[-1]
        ok, bad = [], []
        for p in WATCH:
            a = [f[p] for f in src["frames"][:m]]
            b = [f[p] for f in dst["frames"][:n]]
            ea, eb = body_end(a), body_end(b)
            if ea <= ONSET or eb <= ONSET:
                continue
            # Predict the long body from the short one's two endpoints, over
            # however many frames the long one gives it.
            got = b[:ONSET] + ramp(a[ONSET], a[ea], eb - ONSET + 1)
            total += 1
            if got == b[:eb + 1]:
                good += 1
                ok.append(p)
            else:
                bad.append("%s(%d->%d)" % (p, a[ea], b[eb]))
        print("%-3s %8d %8d  %s%s"
              % (v, m, n, " ".join(ok) if ok else "none",
                 ("   FAILED: " + " ".join(bad)) if bad else ""))
    print()
    if total:
        print("%d of %d parameters predicted exactly at the second length"
              % (good, total))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
