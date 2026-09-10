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
    tools/measure/compose.py <train> <train> <holdout> --write <dir> [case]...

`--write' puts two frame files a case into a directory, the measured frames
and the composed ones, in the form test/harness/klattplay.c reads. That is how
this gets listened to rather than counted: both sides through the same
renderer, so the only difference is the frames. With no case named it writes
the four that span the range -- the closest, the median, the worst, and the
worst velar.
"""

import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import loci as L                                         # noqa: E402
import replay as R                                       # noqa: E402

SKIP = ("step", "f0")

# What the ear actually catches. Stas heard /akaga/ at 40 per cent waveform
# difference and barely heard /asaka/ at 72, and the difference between them
# was not size but kind: /akaga/ had twenty-four frames where the voicing was
# nought and should have been 47, which is a hundred and twenty milliseconds
# of the voice cutting out and coming back. A formant in the wrong place is
# forgiven; the voice stopping is not. So this is the metric that matters, and
# the waveform ratio is only a rough guide beside it.
DROP_ON = 20        # the engine is voicing here
DROP_OFF = 10       # and we are not
DROP_FRAMES = 3     # for long enough to be a gap rather than a glitch


def dropouts(truth, mine, name="av"):
    """Runs where the engine is voicing and the composition is not.

    Answers a list of (first frame, length). A run of three frames is fifteen
    milliseconds, which is about where a gap stops being a click.
    """
    out = []
    run = None
    for i in range(min(len(truth), len(mine))):
        t = truth[i][name] if isinstance(truth[i], dict) else truth[i]
        m = mine[i][name] if isinstance(mine[i], dict) else mine[i]
        if t >= DROP_ON and m < DROP_OFF:
            run = i if run is None else run
        else:
            if run is not None and i - run >= DROP_FRAMES:
                out.append((run, i - run))
            run = None
    if run is not None and len(mine) - run >= DROP_FRAMES:
        out.append((run, len(mine) - run))
    return out

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
        # A dot is the edge of a word: `h:.E' is /h/ opening a word before
        # /E/, `m:a.' is /m/ closing one after /a/. The pair corpus has no
        # such case, every carrier in it being a vowel, a consonant and a
        # vowel, and a real word has one at each end.
        fr = R.build(shared, phone)
        got = L.hold_marked(fr)
        if got is None:
            continue
        span, marker = got
        out[name] = {"cons": cons,
                     "v1": None if vv[0] == "." else vv[0],
                     "v2": None if vv[1] == "." else vv[1],
                     "frames": fr, "span": span, "marker": marker,
                     "text": phone["text"]}
    return out


# How far outside the two measurements the wanted target may lie before the
# straight line through them stops meaning anything. Two carriers whose own
# targets are six hertz apart say nothing about a target four hundred and
# fifty away, and extrapolating anyway multiplies every difference between
# them by seventy-five: /g/, /k/ and /G/ before /u/ went from six per cent
# wrong to two hundred. Beyond this the shape is rescaled instead.
WEIGHT_LIMIT = 3.0


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
        # Two measurements that agree say the parameter does not respond to
        # the target at all, and the measurement stands. Rescaling here
        # instead was tried and cost 24 exact cases: it distorts everything
        # that genuinely does not move.
        return list(s1)
    w = (want - t1) / float(t2 - t1)
    if abs(w) > WEIGHT_LIMIT:
        # Unidentifiable: say so rather than guess, and let the caller use the
        # straight line it would have drawn before any of this. Rescaling the
        # shape instead was tried and was no better -- it left /g/ and /k/
        # before /u/ where they were and added a fresh group at thirty per
        # cent.
        return None
    return [a + int((b - a) * w) for a, b in zip(s1, s2)]


def nearest(cands, span):
    """The carriers of a pair, the one whose own closure matches first.

    An anchor is not always the same length in every carrier of a pair: /l/
    between /W/ and /O/ anchors over seventeen frames and between /W/ and /H/
    over forty-three, because /W/ moves the third bandwidth itself and one of
    the two anchors caught the vowel's movement rather than the consonant's.
    Taking whichever carrier came first then laid a seventeen-frame closure
    into a forty-three frame one and put twenty-eight frames of silence in the
    middle of the word.

    So the one whose closure is nearest the length wanted goes first. That is
    a smaller fix than telling the vowel's bandwidth from the consonant's,
    and it is the alignment that actually matters.
    """
    if span is None or len(cands) < 2:
        return cands
    want = span[1] - span[0]
    # The marker first, then the length: a pair anchored by aspiration going
    # to nought is a better witness than one anchored by the third formant
    # wandering, and choosing on length alone let the weaker anchor win and
    # cost two dropouts.
    return sorted(cands, key=lambda c: (c.get("marker", 9),
                                        abs((c["span"][1] - c["span"][0])
                                            - want)))


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

        # The run-in AND the closure, both from the left pair's own measured
        # shape. A straight line across the closure is wrong for anything
        # voiced: /C/ holds av flat at nought and a line is right by accident,
        # but /J/ ramps 0 0 4 12 20 27 35 40 41 43 44 45 as voicing returns
        # through the affricate, and /g/ steps -- 20 for twelve frames, then
        # nought for three while af jumps to 62 for the burst. Neither is a
        # line between its ends, and imposing one is why the voiced obstruents
        # were the ones that failed.
        pre = list(va[:ba + 1])
        shaped = False
        if va2 is not None:
            # Only the closure has to line up, not the whole carrier. The two
            # left carriers share their first vowel and their consonant but
            # not their second, so their total lengths differ -- /J/'s are 98
            # and 82 frames -- and requiring those to match silently skipped
            # the shape for every consonant, which is why the first attempt
            # at this left /J/ exactly as wrong as before.
            a2a, a2b = a2["span"]
            if (a2a, a2b) == (aa, ba):
                got_shape = between(va[:ba + 1], va[ba], va2[:a2b + 1],
                                    va2[a2b], want_out)
                if got_shape is not None:
                    pre = got_shape
                    shaped = True

        # The same trick on the run-out, parameterised by the closure's first
        # frame, was tried and made the answer worse: 82 carriers within one
        # per cent against 88, and the run-out's own error unmoved at 2.3 per
        # cent. So whatever the preceding vowel does to a run-out is not
        # linear in the closure's onset, and the run-out is left as the right
        # carrier measured it. Most of what is wrong there is voicing, which
        # is an utterance-level staircase and not the pair's at all.
        tail = list(vb[bb + 1:])
        offset_v = vb[bb]

        if shaped:
            # The closure came with the shape, so nothing to rebuild.
            seq = pre + list(tail)
        elif flat_a and flat_b:
            seq = pre[:aa] + [offset_v] * (ba - aa + 1) + list(tail)
        else:
            k = ba - aa
            onset_v = va[aa]
            seq = pre[:aa] + [
                onset_v + (int((offset_v - onset_v) * i / float(k))
                           if k else 0) for i in range(k + 1)] + list(tail)

        
        seq = (seq + [seq[-1]] * n_out)[:n_out]
        for i in range(n_out):
            out[i][p] = seq[i]
    return out


def write_frames(path, frames, borrow=None):
    """One case as klattplay reads it: a header line, then a line a frame.

    `step' and `f0' are not in the tables -- `step' is five milliseconds a
    frame and f0 is the intonation, which belongs to the utterance and not to
    any phoneme -- so both are borrowed from the measured case. That is the
    point rather than a shortcut: the synthesiser makes no samples at all
    without a duration, and lending both sides the same pitch is what leaves
    only the phonetic parameters to listen to.
    """
    order = list(R.NAMES)
    with open(path, "w") as f:
        f.write("\t".join(order) + "\n")
        for i, fr in enumerate(frames):
            row = []
            for n in order:
                if n in ("step", "f0") and borrow is not None:
                    j = min(i, len(borrow) - 1)
                    row.append(str(borrow[j][n]))
                else:
                    row.append(str(fr.get(n, 0)))
            f.write("\t".join(row) + "\n")


def main(argv):
    if len(argv) < 4:
        sys.stderr.write(__doc__)
        return 2
    truelen = "--true-length" in argv
    argv = [x for x in argv if x != "--true-length"]
    writedir = None
    if "--write" in argv:
        i = argv.index("--write")
        writedir = argv[i + 1]
        wanted = argv[i + 2:]
        argv = argv[:i]
        truelen = True
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
    drops = {}
    byregion = collections.Counter()
    percase = {}
    regionsize = collections.Counter()
    FORMANTS = ("f1", "f2", "f3", "f4", "f5")
    for name, want in sorted(held.items()):
        al = train.get(("left", want["cons"], want["v1"])) or []
        bl = train.get(("right", want["cons"], want["v2"])) or []
        if not al or not bl:
            continue
        tried += 1
        al = nearest(al, want["span"])
        bl = nearest(bl, want["span"])
        pair_a = (al[0], al[1] if len(al) > 1 else None)
        pair_b = (bl[0], bl[1] if len(bl) > 1 else None)
        got = compose(pair_a, pair_b,
                      len(want["frames"]) if truelen else None)
        if len(got) != len(want["frames"]):
            lenbad += 1
        bad = False
        fmax = 0
        nwrong = 0
        wa, wb = want["span"]
        for i, (mine, theirs) in enumerate(zip(got, want["frames"])):
            where = ("run-in" if i < wa else
                     "closure" if i <= wb else "run-out")
            for p in mine:
                t = theirs.get(p)
                if t is None or mine[p] == t:
                    continue
                wrong[p] += 1
                nwrong += 1
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
        d = dropouts(want["frames"], got)
        if d:
            drops[name] = sum(x[1] for x in d)
        percase[name] = (fmax, got, nwrong)
        n = len(want["frames"])
        regionsize["run-in"] += wa
        regionsize["closure"] += wb - wa + 1
        regionsize["run-out"] += max(0, n - wb - 1)
    if writedir is not None:
        os.makedirs(writedir, exist_ok=True)
        # step and f0 are not in the tables, so they come from the engine
        # itself, once, for the handful of cases being written.
        probe = os.environ.get("EVV_PROBE", "./build/probe")
        idx = {n: i for i, n in enumerate(R.NAMES)}
        if wanted == ["--all"]:
            wanted = sorted(percase)
        elif not wanted:
            # Ranked by how many values are wrong, not by the formants alone:
            # a case with no formant error at all can still differ in voicing,
            # and the control has to be one that differs in nothing.
            ranked = sorted(percase, key=lambda k: (percase[k][2],
                                                    percase[k][0]))
            velar = [k for k in ranked if k.split(":")[0] in ("k", "g", "G")]
            wanted = [ranked[0], ranked[len(ranked) // 2], ranked[-1]]
            if velar:
                wanted.append(velar[-1])
        for name in wanted:
            if name not in percase:
                sys.stderr.write("compose: no held-out %s\n" % name)
                continue
            tag = name.replace(":", "-")
            rows = R.frames_of(probe, held[name]["text"])
            live = [r for r in rows
                    if any(r[idx[k]] >= 20 for k in ("av", "af", "ah"))]
            raw = [{"step": r[idx["step"]], "f0": r[idx["f0"]]}
                   for r in live]
            write_frames(os.path.join(writedir, tag + ".true.tsv"),
                         held[name]["frames"], raw)
            write_frames(os.path.join(writedir, tag + ".mine.tsv"),
                         percase[name][1], raw)
            print("%-9s %6d values wrong, worst formant %6.2f per cent   %s"
                  % (name, percase[name][2], percase[name][0], tag))
        return 0

    print("composed %d held-out carriers from the two training squares%s"
          % (tried, " (length given, not predicted)" if truelen else ""))
    print("  %d reproduce every parameter of every frame" % exact)
    print("  %d come out the wrong length" % lenbad)
    print()
    if drops:
        print("  %d have the voice cut out where the engine has it: %s"
              % (len(drops), ", ".join(
                  "%s (%d frames)" % (k, v) for k, v in
                  sorted(drops.items(), key=lambda kv: -kv[1])[:6])))
    else:
        print("  none has the voice cut out where the engine has it")
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
