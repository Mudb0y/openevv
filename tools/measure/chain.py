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

    tools/measure/chain.py <probe> <klattplay> <phonemes> [<phonemes>...]

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


def live_frames(probe, text):
    got = R.frames_of(probe, text)
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


def load_pairs():
    """Every measured carrier, by the pair on each side of its consonant."""
    here = os.path.join(ROOT, "lang", "measured")
    left = collections.defaultdict(list)
    right = collections.defaultdict(list)
    for f in ("enus-pairs.txt", "enus-pairs2.txt", "enus-holdout.txt"):
        p = os.path.join(here, f)
        if not os.path.exists(p):
            continue
        for name, rec in K.cases(p).items():
            left[(rec["cons"], rec["v1"])].append(rec)
            right[(rec["cons"], rec["v2"])].append(rec)
    return left, right


def moving_head(seq):
    """How many frames at the front of a run-out are still moving."""
    n = len(seq)
    k = 0
    while k + 1 < n and seq[k + 1] != seq[k]:
        k += 1
    return min(k + 1, n)


def moving_tail(seq):
    """How many frames at the end of a run-in are already moving."""
    n = len(seq)
    k = 0
    while k + 1 < n and seq[n - 1 - k] != seq[n - 2 - k]:
        k += 1
    return min(k + 1, n)


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
    # Too much: cut from the middle, leaving both ends whole where possible.
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
            for i in range(a, b + 1):
                j = i - a
                seq[i] = closure[k][min(j, len(closure[k]) - 1)]

        # Before the first closure: the left carrier's own prefix, which
        # carries the vowel's onset as well as the run-in.
        room = spans[0][0]
        if room > 0:
            src = runin[0]
            seq[:room] = (src[-room:] if len(src) >= room
                          else list(src) + [src[-1]] * (room - len(src))
                          if src else [frames[0][name]] * room)

        # Between two closures: the run-out of the first meeting the run-in of
        # the second, with the vowel holding in between.
        for k in range(len(spans) - 1):
            lo = spans[k][1] + 1
            hi = spans[k + 1][0]
            room = hi - lo
            if room <= 0:
                continue
            outp = runout[k][:moving_head(runout[k])] if runout[k] else []
            inp = runin[k + 1]
            inp = inp[len(inp) - moving_tail(inp):] if inp else []
            seq[lo:hi] = stitch(outp, inp, room)

        # After the last closure: the right carrier's run-out entire.
        lo = spans[-1][1] + 1
        room = n - lo
        if room > 0:
            src = runout[len(spans) - 1]
            seq[lo:n] = (list(src[:room]) if len(src) >= room
                         else list(src) + [src[-1]] * (room - len(src))
                         if src else [seq[lo - 1]] * room)

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
    left, right = load_pairs()
    work = os.path.join(ROOT, "build", "chain")
    if not os.path.isdir(work):
        os.makedirs(work)

    print("%-12s %6s %8s %8s %7s  %s"
          % ("utterance", "frames", "signal", "differs", "ratio", "note"))
    for ph in argv[3:]:
        text = "`[.1%s]" % ph
        frames = live_frames(probe, text)
        truth = [{nm: r[IDX[nm]] for nm in R.NAMES} for r in frames]
        mine, err = compose_chain(ph, frames, left, right)
        if mine is None:
            print("%-12s %6d %8s %8s %7s  %s"
                  % (ph, len(frames), "-", "-", "-", err))
            continue
        base = os.path.join(work, ph)
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
        print("%-12s %6d %8.0f %8.0f %6.1f%%  %d values wrong"
              % (ph, len(frames), ra, rd, 100.0 * rd / max(1.0, ra), wrong))
    print()
    print("frames and wave files in %s" % os.path.relpath(work, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
