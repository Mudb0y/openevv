#!/usr/bin/env python3
"""Generate a carrier the tables have never seen, and see how close it is.

Every measured table reproduces the engine exactly, which proves the
measurement and not the format. What proves the format is generating an
utterance nobody measured: `lang/measured/enus-holdout.txt' is 416 carriers at
a vowel offset neither training square uses, so a composer built from
`enus-pairs.txt' and `enus-pairs2.txt' has seen no frame of it.

The model being tested is the one the measurements argued for. For a carrier
V1-C-V2:

Timing comes from the left pair. When the closure starts and how long it lasts
are functions of the preceding vowel and the consonant, measured over 320
pairs at 320 of 320, so both are taken from a training carrier sharing V1
and C.

A parameter that holds one value through the closure is taken from the right:
the following vowel sets it, f1 agreeing between squares on 632 of 640.

A parameter that ramps across the closure runs from the left carrier's onset
to the right carrier's offset, since one end is set by each neighbour.

And the run-in is a fixed shape scaled to its target. /a/ into /m/ runs 705
then 330 when the closure is at 300, and 700 then 284 when it is at 250: both
one tenth and fourteen fifteenths of the way from 750 to wherever it is going.
So the run-in is reproduced as fractions of the left carrier's own run-in,
stretched to the target the right carrier asks for, and the run-out likewise.

    tools/measure/compose.py <train> <train> <holdout> [--true-length]
"""

import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import loci as L                                         # noqa: E402
import replay as R                                       # noqa: E402

SKIP = ("step", "f0")

# Voicing is not a phoneme's, it is the utterance's: it declines in a
# staircase from the first vowel's plateau, holds the consonant's own value
# through the closure, resumes declining and lets go at the end. Splicing two
# carriers cannot produce that, their tails being different lengths, so every
# step after the join lands in the wrong place -- which is most of what is
# still wrong here.
#
# A staircase of one step every nine frames, with the remainder given to the
# first step, was fitted to two carriers and tried: it made the answer worse,
# 8,085 frames wrong against the splice's 3,892 and no carrier exact at all.
# So the nine does not generalise and the rule is not yet found. Splicing is
# what is left in.


def cases(path):
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
        out[name] = {"cons": cons, "v1": vv[0], "v2": vv[1],
                     "frames": fr, "span": span}
    return out


def between(s1, t1, s2, t2, want):
    """Two measured trajectories aimed at two targets, aimed at a third.

    Rescaling one trajectory to a new target needs to know where the vowel
    ends and the run-in begins, and getting that wrong distorts the vowel's
    own glide -- which is what a first version of this did, rescaling the
    whole prefix and coming out 15 per cent wrong on /p/. There is no need to
    know. Each side of a carrier has two measured neighbours, one from each
    training square, aimed at two different targets; and a run-in is linear in
    its target, /a/ into /m/ reaching one tenth and fourteen fifteenths of the
    way whether that way ends at 300 or at 250. So the answer is the straight
    line through the two measurements, evaluated at the target wanted.

    Where the two agree the answer is that value, which is how the vowel's own
    portion comes through untouched without having to be found.
    """
    if t1 == t2:
        return list(s1)
    w = (want - t1) / float(t2 - t1)
    return [a + int((b - a) * w) for a, b in zip(s1, s2)]


def compose(aa_pair, bb_pair, n_out=None):
    """The carrier with the left pair's first vowel and the right pair's second.

    Each argument is the two training carriers that share the near pair, one
    from each square. `n_out' overrides the predicted length, which is for
    telling one kind of error from another.
    """
    a, a2 = aa_pair
    b, b2 = bb_pair
    fa, fb = a["frames"], b["frames"]
    aa, ba = a["span"]
    ab, bb = b["span"]
    if n_out is None:
        n_out = aa + (ba - aa + 1) + (len(fb) - bb - 1)
    names = [k for k in fa[0] if k not in SKIP]
    out = [dict() for _ in range(n_out)]
    for p in names:
        va = [f[p] for f in fa]
        vb = [f[p] for f in fb]
        va2 = [f[p] for f in a2["frames"]] if a2 is not None else None
        vb2 = [f[p] for f in b2["frames"]] if b2 is not None else None
        # Does it hold one value through the closure, or ramp across it?
        flat_a = va[aa] == va[ba]
        flat_b = vb[ab] == vb[bb]
        onset = va[aa]
        offset = vb[bb]
        if flat_a and flat_b:
            # The following vowel sets it, so the right carrier has it.
            hold_vals = [offset] * (ba - aa + 1)
        else:
            # One end is set by each neighbour, and the line between them is
            # what the engine walks.
            k = ba - aa
            hold_vals = [onset + (int((offset - onset) * i / float(k))
                                  if k else 0) for i in range(k + 1)]
        # The run-in is the left carrier's shape aimed at our target. The
        # run-out is the right carrier's own and needs no scaling: it already
        # leaves our closure for our second vowel, both of which are its.
        # Each side is parameterised by the end of the closure that the other
        # side owns. The run-in and the onset are aimed at the closure's last
        # frame, which the following vowel sets; the run-out and the offset are
        # aimed at its first, which the preceding vowel sets.
        want_out = vb[bb]

        pre = list(va[:aa])
        onset_v = va[aa]
        if va2 is not None and len(va2) == len(va):
            a2a, a2b = a2["span"]
            if (a2a, a2b) == (aa, ba):
                pre = between(va[:aa], va[ba], va2[:aa], va2[a2b], want_out)
                onset_v = between([va[aa]], va[ba], [va2[aa]], va2[a2b],
                                  want_out)[0]

        # The same trick on the run-out, parameterised by the closure's first
        # frame, was tried and made the answer worse: 82 carriers within one
        # per cent against 88, and the run-out's own error unmoved at 2.3 per
        # cent. So whatever the preceding vowel does to a run-out is not
        # linear in the closure's onset, and the run-out is left as the right
        # carrier measured it. Most of what is wrong there is voicing, which
        # is an utterance-level staircase and not the pair's at all.
        tail = list(vb[bb + 1:])
        offset_v = vb[bb]

        # Rebuild the closure with its corrected onset.
        if flat_a and flat_b:
            hold_vals = [offset_v] * (ba - aa + 1)
        else:
            k = ba - aa
            hold_vals = [onset_v + (int((offset_v - onset_v) * i / float(k))
                                    if k else 0) for i in range(k + 1)]

        seq = pre + hold_vals + list(tail)
        seq = (seq + [seq[-1]] * n_out)[:n_out]
        for i in range(n_out):
            out[i][p] = seq[i]
    return out


def main(argv):
    if len(argv) < 4:
        sys.stderr.write(__doc__)
        return 2
    truelen = "--true-length" in argv
    argv = [x for x in argv if x != "--true-length"]
    train = collections.defaultdict(list)
    for path in argv[1:3]:
        for name, rec in cases(path).items():
            train[("left", rec["cons"], rec["v1"])].append(rec)
            train[("right", rec["cons"], rec["v2"])].append(rec)
    held = cases(argv[3])

    exact = 0
    tried = 0
    close = 0
    wrong = collections.Counter()
    errs = collections.defaultdict(list)
    lenbad = 0
    bycons = collections.defaultdict(list)
    byregion = collections.Counter()
    regionsize = collections.Counter()
    FORMANTS = ("f1", "f2", "f3", "f4", "f5")
    for name, want in sorted(held.items()):
        al = train.get(("left", want["cons"], want["v1"])) or []
        bl = train.get(("right", want["cons"], want["v2"])) or []
        if not al or not bl:
            continue
        tried += 1
        pair_a = (al[0], al[1] if len(al) > 1 else None)
        pair_b = (bl[0], bl[1] if len(bl) > 1 else None)
        got = compose(pair_a, pair_b,
                      len(want["frames"]) if truelen else None)
        if len(got) != len(want["frames"]):
            lenbad += 1
        bad = False
        fmax = 0
        wa, wb = want["span"]
        for i, (mine, theirs) in enumerate(zip(got, want["frames"])):
            where = ("run-in" if i < wa else
                     "closure" if i <= wb else "run-out")
            for p in mine:
                t = theirs.get(p)
                if t is None or mine[p] == t:
                    continue
                wrong[p] += 1
                byregion[where] += 1
                e = abs(mine[p] - t)
                errs[p].append(e)
                if p in FORMANTS:
                    fmax = max(fmax, 100.0 * e / max(1, t))
                bad = True
        if not bad:
            exact += 1
        if fmax <= 1.0:
            close += 1
        bycons[want["cons"]].append(fmax)
        n = len(want["frames"])
        regionsize["run-in"] += wa
        regionsize["closure"] += wb - wa + 1
        regionsize["run-out"] += max(0, n - wb - 1)
    print("composed %d held-out carriers from the two training squares%s"
          % (tried, " (length given, not predicted)" if truelen else ""))
    print("  %d reproduce every parameter of every frame" % exact)
    print("  %d come out the wrong length" % lenbad)
    print()
    print("  %d have every formant within one per cent, which is under the"
          % close)
    print("     ear's threshold for telling two formants apart")
    print()
    print("where the error is, as wrong values against values compared:")
    for r in ("run-in", "closure", "run-out"):
        tot = regionsize[r] * 60
        print("  %-8s %7d wrong of about %8d  (%.1f per cent)"
              % (r, byregion[r], tot, 100.0 * byregion[r] / max(1, tot)))
    print()
    print("worst formant error per consonant, per cent:")
    rows = sorted(bycons.items(), key=lambda kv: -max(kv[1]))
    for cons, v in rows:
        v = sorted(v)
        print("  %-3s worst %6.1f   median %5.2f   %d of %d within one"
              % (cons, v[-1], v[len(v) // 2],
                 sum(1 for x in v if x <= 1.0), len(v)))
    print()
    print("%-4s %8s %7s %7s %7s" % ("par", "frames", "median", "95th",
                                    "worst"))
    for p, c in wrong.most_common():
        v = sorted(errs[p])
        print("%-4s %8d %7d %7d %7d"
              % (p, c, v[len(v) // 2], v[int(len(v) * 0.95)], v[-1]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
