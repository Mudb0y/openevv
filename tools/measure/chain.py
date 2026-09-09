#!/usr/bin/env python3
"""Compose a whole utterance from the pair tables, not just one carrier.

Everything so far composes a single vowel-consonant-vowel carrier. A word is a
chain of those, overlapping: in /atapa/ the /t/ is the (a,t) and (t,a) pairs,
the /p/ is (a,p) and (p,a), and the /a/ between them is the run-out of one and
the run-in of the next with the vowel's own body in the middle. So the pair
tables are enough for a word if they can be stitched, and this is the stitch.

What this does not do is decide the timing. When each closure starts and how
long it lasts is a language's business, decided long before the synthesiser
sees anything, so the frame layout is taken from the engine's own frames for
the same text. That is deliberate rather than a shortcut: it separates the
question this can answer -- do the tables describe real speech -- from the
question of what a language chooses to do, which is not a formant question.

    tools/measure/chain.py <probe> <klattplay> [--wpm N] <phonemes>...

A phoneme string is what the engine itself accepts, so `atapa' means the
utterance `` `[.1atapa] ``.
"""

import collections
import math
import os
import struct
import subprocess
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import compose as K                                      # noqa: E402
import replay as R                                       # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
IDX = {n: i for i, n in enumerate(R.NAMES)}
SKIP = ("step", "f0")
RATE = 11025

VOWELS = set("iIeEAauUocYWOXHx")


def live_frames(probe, text, wpm=None):
    got = R.frames_of(probe, text, wpm)
    return [r for r in got
            if any(r[IDX[s]] >= 20 for s in ("av", "af", "ah"))]


def closures(frames):
    """Every stretch with no aspiration: one a consonant that closes."""
    n = len(frames)
    out, i = [], 0
    while i < n:
        if frames[i][IDX["ah"]] == 0:
            j = i
            while j + 1 < n and frames[j + 1][IDX["ah"]] == 0:
                j += 1
            out.append((i, j))
            i = j + 1
        else:
            i += 1
    return out


def load_pairs(wpm=None):
    """Every measured carrier, by the pair on each side of its consonant.

    A rate of its own has tables of its own, since speed changes the number of
    frames: `enus-pairs-450.txt' beside `enus-pairs.txt'. Falling back to the
    175 tables when a rate has none is deliberate -- it is what composing at
    an unmeasured rate has to do, and the stretch in `fit' is what makes it
    possible at all.
    """
    here = os.path.join(ROOT, "lang", "measured")
    left = collections.defaultdict(list)
    right = collections.defaultdict(list)
    names = ["enus-pairs.txt", "enus-pairs2.txt", "enus-holdout.txt"]
    if wpm is not None:
        at_rate = ["enus-pairs-%d.txt" % wpm, "enus-pairs2-%d.txt" % wpm,
                   "enus-holdout-%d.txt" % wpm]
        if all(os.path.exists(os.path.join(here, f)) for f in at_rate):
            names = at_rate
    for f in names:
        p = os.path.join(here, f)
        if not os.path.exists(p):
            continue
        for name, rec in K.cases(p).items():
            left[(rec["cons"], rec["v1"])].append(rec)
            right[(rec["cons"], rec["v2"])].append(rec)
    return left, right


# Both sides of a region are handed over whole and `stitch' cuts the middle.
# Trying to find where each transition ends first was worse than not trying:
# a run-out BEGINS with a plateau -- after a /k/ closure the voicing is still
# nought for several frames before it returns -- so a rule that stops at the
# first repeated value captured one frame of silence and held it across the
# whole vowel. In /akaga/ that put twenty-four frames of av at nought where
# the engine has 47 declining to 44, which is a hundred and twenty
# milliseconds of the voice cutting out and coming back, and is exactly what
# Stas heard.


def fit(seq, room, keep):
    """A measured shape into the room it has, stretching only if it must.

    A shape measured at very nearly the right length is used as measured, and
    only one measured at a different length is stretched. Both halves matter.
    Stretching everything cost the default rate its accuracy -- /atapa/ went
    from 14.5 per cent to 39.4 -- because the tables are measured at 175 words
    a minute and at 175 the room already fits. Stretching nothing is what
    breaks at speed, where the room is a fifth of what was measured.

    `keep' says which end to hold on to when the difference is small: `tail'
    for a run-in, whose last frame meets the closure, `head' for a run-out,
    whose first frame leaves it.
    """
    m = len(seq)
    if room <= 0:
        return []
    if not m:
        return None
    if m == room:
        return list(seq)
    # Within a couple of frames the shape is the right shape and only its
    # edge moves; beyond that the rate has changed and it has to be stretched.
    if abs(m - room) <= 2:
        if room < m:
            return list(seq[m - room:]) if keep == "tail" else list(seq[:room])
        pad = [seq[0]] * (room - m) if keep == "tail" else \
              [seq[-1]] * (room - m)
        return pad + list(seq) if keep == "tail" else list(seq) + pad
    return resample(seq, room)


def resample(seq, n):
    """A measured shape at a different length, by the stretch law.

    The tables are measured at 175 words a minute, which is what probe speaks
    at, and speed changes the number of frames rather than their length --
    `step' stays five milliseconds at every rate, so /atapa/ is 115 frames at
    175 and 12 at 700. Laying a measured shape down at its measured length is
    therefore wrong at any other rate, and wrong by a factor of ten at the
    top of the range.

    So a shape is stretched to the room it has, read at fractional positions
    and truncated as the engine's own arithmetic truncates. This is the
    stretch law applied to a whole trajectory rather than to a single ramp.
    """
    m = len(seq)
    if n <= 0 or m == 0:
        return []
    if m == 1:
        return [seq[0]] * n
    if n == 1:
        return [seq[0]]
    out = []
    for i in range(n):
        pos = i * (m - 1) / float(n - 1)
        k = int(pos)
        if k >= m - 1:
            out.append(seq[m - 1])
        else:
            out.append(seq[k] + int((seq[k + 1] - seq[k]) * (pos - k)))
    return out


def stitch(out, tail, want):
    """One region's values, held or trimmed in its middle to reach `want'.

    A region between two closures is a run-out, a stretch of the vowel doing
    nothing much, and a run-in. Both ends belong to the consonants either side
    and must not be touched, so what gives is the middle: held longer when the
    region is longer than the two transitions, cut when it is shorter. Writing
    the run-out and the run-in into the same stretch and letting the second
    overwrite the first -- which a first version of this did -- leaves the
    vowel with no run-out at all and puts 179 of 180 wrong values in the
    vowels.
    """
    have = len(out) + len(tail)
    if want <= 0:
        return []
    if have == want:
        return list(out) + list(tail)
    if have < want:
        # Hold whatever the two sides meet at.
        mid = out[-1] if out else (tail[0] if tail else 0)
        return list(out) + [mid] * (want - have) + list(tail)
    # Too much for the room: squeeze both sides rather than cut either, which
    # is what a higher rate asks for. Cutting from the middle is right when
    # the middle is a vowel holding still, and wrong when there is no middle
    # at all -- above 350 words a minute a region is four or five frames and
    # a run-out alone is six, so cutting takes the transitions themselves.
    # Cut from the middle, keeping both ends whole. The middle of a region is
    # the vowel holding still and is what a shorter region has less of; the
    # ends are the transitions and are where all the information is.
    # Resampling both proportionally instead was tried and cost the default
    # rate more than it gained at speed -- /atapa/ 14.5 per cent to 39.1 --
    # because compressing a transition distorts it where dropping a frame of
    # steady vowel does not.
    cut = have - want
    keep_out = max(0, len(out) - (cut + 1) // 2)
    keep_tail = max(0, len(tail) - cut // 2)
    seq = list(out[:keep_out]) + list(tail[len(tail) - keep_tail:])
    if len(seq) > want:
        seq = seq[:want]
    while len(seq) < want:
        seq.append(seq[-1] if seq else 0)
    return seq


def compose_chain(phonemes, frames, left, right):
    """The whole utterance, from the tables, laid out on the engine's timing."""
    spans = closures(frames)
    cons = [p for p in phonemes if p not in VOWELS]
    vows = [p for p in phonemes if p in VOWELS]
    if len(spans) != len(cons):
        return None, ("%d closures but %d consonants in the string"
                      % (len(spans), len(cons)))

    n = len(frames)
    names = [k for k in R.NAMES if k not in SKIP]
    out = [dict() for _ in range(n)]

    # Which vowel is on each side of each consonant, from the string.
    order = []
    vi = 0
    for p in phonemes:
        if p in VOWELS:
            vi += 1
        else:
            order.append((p, vows[vi - 1] if vi else None,
                          vows[vi] if vi < len(vows) else None))

    # Each consonant's own material, gathered once so the regions between
    # them can be stitched rather than fought over.
    for name in names:
        closure = {}
        runin = {}
        runout = {}
        for k, ((c, vp, vn), (a, b)) in enumerate(zip(order, spans)):
            lp = left.get((c, vp)) if vp else None
            rp = right.get((c, vn)) if vn else None
            if not lp or not rp:
                return None, "no measured pair for %s around %s" % (c, vp)
            la, la2 = lp[0], (lp[1] if len(lp) > 1 else None)
            rb = rp[0]
            lav = [f[name] for f in la["frames"]]
            lav2 = ([f[name] for f in la2["frames"]]
                    if la2 is not None else None)
            rbv = [f[name] for f in rb["frames"]]
            laa, lab = la["span"]
            rba, rbb = rb["span"]

            piece = list(lav[:lab + 1])
            if lav2 is not None:
                l2a, l2b = la2["span"]
                if (l2a, l2b) == (laa, lab):
                    got = K.between(lav[:lab + 1], lav[lab],
                                    lav2[:l2b + 1], lav2[l2b], rbv[rbb])
                    if got is not None:
                        piece = got
            closure[k] = piece[laa:lab + 1]
            runin[k] = piece[:laa]
            runout[k] = list(rbv[rbb + 1:])

        seq = [None] * n
        for k, (a, b) in enumerate(spans):
            # The closure is stretched to the room the engine gave it, which
            # at speed is a fraction of what it was measured at.
            fitted = fit(closure[k], b - a + 1, "head")
            for i in range(a, b + 1):
                seq[i] = fitted[i - a]

        # Before the first closure: the left carrier's own prefix, which
        # carries the vowel's onset as well as the run-in.
        room = spans[0][0]
        if room > 0:
            src = runin[0]
            got = fit(src, room, "tail") if src else None
            seq[:room] = got if got else [frames[0][name]] * room

        # Between two closures: the run-out of the first meeting the run-in of
        # the second, with the vowel holding in between.
        for k in range(len(spans) - 1):
            lo = spans[k][1] + 1
            hi = spans[k + 1][0]
            room = hi - lo
            if room <= 0:
                continue
            seq[lo:hi] = stitch(runout[k], runin[k + 1], room)

        # After the last closure: the right carrier's run-out entire.
        lo = spans[-1][1] + 1
        room = n - lo
        if room > 0:
            src = runout[len(spans) - 1]
            got = fit(src, room, "head") if src else None
            seq[lo:n] = got if got else [seq[lo - 1]] * room

        last = None
        for i in range(n):
            if seq[i] is None:
                seq[i] = last if last is not None else frames[0][name]
            last = seq[i]
        for i in range(n):
            out[i][name] = seq[i]
    return out, None


def write_frames(path, frames, borrow):
    order = list(R.NAMES)
    with open(path, "w") as f:
        f.write("\t".join(order) + "\n")
        for i, fr in enumerate(frames):
            row = []
            for nm in order:
                if nm in ("step", "f0"):
                    row.append(str(borrow[min(i, len(borrow) - 1)][IDX[nm]]))
                else:
                    row.append(str(fr.get(nm, 0)))
            f.write("\t".join(row) + "\n")


def rms(s):
    return math.sqrt(sum(float(v) * v for v in s) / max(1, len(s)))


def samples(path):
    w = wave.open(path)
    k = w.getnframes()
    out = struct.unpack("<%dh" % k, w.readframes(k))
    w.close()
    return out


def main(argv):
    if len(argv) < 4:
        sys.stderr.write(__doc__)
        return 2
    probe, play = argv[1], argv[2]
    rest = [x for x in argv[3:] if x != "--own-tables"]
    wpm = None
    if rest and rest[0] == "--wpm":
        wpm = int(rest[1])
        rest = rest[2:]
    left, right = load_pairs(wpm if "--own-tables" in argv else None)
    argv = [x for x in argv if x != "--own-tables"]
    work = os.path.join(ROOT, "build", "chain")
    if not os.path.isdir(work):
        os.makedirs(work)

    print("%-12s %6s %8s %8s %7s  %s"
          % ("utterance", "frames", "signal", "differs", "ratio", "note"))
    for ph in rest:
        text = "`[.1%s]" % ph
        frames = live_frames(probe, text, wpm)
        truth = [{nm: r[IDX[nm]] for nm in R.NAMES} for r in frames]
        mine, err = compose_chain(ph, frames, left, right)
        if mine is None:
            print("%-12s %6d %8s %8s %7s  %s"
                  % (ph, len(frames), "-", "-", "-", err))
            continue
        base = os.path.join(work, ph + ("" if wpm is None else ".%d" % wpm))
        write_frames(base + ".true.tsv", truth, frames)
        write_frames(base + ".mine.tsv", mine, frames)
        for side in ("true", "mine"):
            subprocess.run([play, base + "." + side + ".tsv",
                            base + "." + side + ".wav", str(RATE)],
                           capture_output=True)
        a, b = samples(base + ".true.wav"), samples(base + ".mine.wav")
        d = [x - y for x, y in zip(a, b)]
        ra, rd = rms(a), rms(d)
        wrong = sum(1 for i in range(len(mine))
                    for nm in mine[i] if mine[i][nm] != truth[i][nm])
        drop = K.dropouts(truth, mine)
        note = "%d values wrong" % wrong
        if drop:
            note += ";  VOICE CUTS OUT: %s" % ", ".join(
                "%d frames at %d" % (n, a) for a, n in drop)
        print("%-12s %6d %8.0f %8.0f %6.1f%%  %s"
              % (ph, len(frames), ra, rd, 100.0 * rd / max(1.0, ra), note))
    print()
    print("frames and wave files in %s" % os.path.relpath(work, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
