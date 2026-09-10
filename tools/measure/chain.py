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

# Voicing is the utterance's and not a phoneme's, and splicing it from two
# carriers of different lengths lands every step in the wrong place -- which
# is most of what is still wrong with a chain.
#
# Three things had to be right and each was found by the answer getting
# worse. The drop must not be taken off twice, a pair carrier's run-out
# already being the staircase. A closure must keep its own shape rather than
# one held value, /t/ holding 35 for four frames and then dropping to nought
# for its burst. And the voicing does not come back the moment the closure
# ends -- a voiceless stop holds it at nought through its release as well,
# /p/ for four frames past the `ah == 0' span and /k/ for five -- so starting
# the staircase there moved every step boundary with it and cost /atapa/ 60
# per cent against splicing's 14.5.
#
# The rule, measured over /a/, /aa/, /ama/, /amama/, /amamama/, /imi/, /umu/,
# /AmA/, /imu/, /ini/ and /asa/: the staircase does not decline across an
# utterance, it RESETS after every consonant. After each closure the voicing
# resumes at the FOLLOWING vowel's own value less three and steps down by one,
# four steps, over that vowel. /a/ is 50 and gives 47 46 45 44; /A/ is 49 and
# gives 46 45 44 43; /u/ is 59 and gives 56 55 54 53, and it gives those in
# /imu/ as well as /umu/, so it is the vowel after the consonant that decides
# and not the one before.
#
# /imi/ looks like an exception and is not: /i/ is 55, so its staircase starts
# at 52, which is also what /m/ holds through its own closure, and the two
# runs merge into one of seventeen frames.
#
# The first vowel of an utterance is its own value and then one less, two
# steps rather than four, since nothing has reset it yet.
AV_DROP = 3        # below the following vowel's own value
AV_STEPS = 4       # steps across a vowel that follows a consonant
AV_FIRST_STEPS = 2 # across the first vowel of the utterance
# Letting go at the end is not a fixed number of frames: it is thirteen at
# 175 words a minute, eight at 250, four at 350, three at 450 and one at 700,
# so it scales with the rate like everything else. Taking it as thirteen
# always reserved thirteen frames where the engine used three and squeezed the
# whole staircase into what was left, which cost /atapa/ at 450 words a
# minute 51 per cent against splicing's 16.3. It is read off the right-hand
# carrier instead, as the trailing frames whose voicing falls by more than one
# a frame -- a staircase steps by one and a release plunges.
AV_RELEASE = 13    # only the fallback, where no carrier says otherwise

# How far the third formant has to leave the middle of the utterance to say a
# sonorant is there. A vowel's own f3 sits between 2300 and 2800 and the
# sonorants take it to 1600, 2250, 2800 or 3000.
F3_MARK = 200


def release_len(tail):
    """How many frames at the end of a carrier are the letting go."""
    k = 0
    i = len(tail) - 1
    while i > 0 and tail[i - 1] - tail[i] > 1:
        k += 1
        i -= 1
    return k + 1 if k else 0


def live_frames(probe, text, wpm=None):
    got = R.frames_of(probe, text, wpm)
    return [r for r in got
            if any(r[IDX[s]] >= 20 for s in ("av", "af", "ah"))]


def runs_where(frames, test, edges=(False, False)):
    """Every run of frames the test holds over, edges included if asked."""
    out, i, n = [], 0, len(frames)
    while i < n:
        if test(frames[i]):
            j = i
            while j + 1 < n and test(frames[j + 1]):
                j += 1
            # A run touching an edge is the utterance's own onset or release
            # in a carrier, and a word-edge consonant in a word. `edges' says
            # which this is: the phoneme string knows whether it opens or
            # closes on a consonant.
            if (i > 0 or edges[0]) and (j < n - 1 or edges[1]):
                out.append((i, j))
            i = j + 1
        else:
            i += 1
    return out


def closures(frames, want=None, edges=(False, False)):
    """Where each consonant of the utterance is, by whichever marker shows it.

    Three markers, the same three `loci.hold' uses and for the same reason:
    aspiration going to nought is the obstruents and the nasals, voicing going
    to nought is /h/, and the third bandwidth leaving the vowel's own 150 is
    the five sonorants, which have no amplitude marker at all. Asking only
    the first, which this did until 9 September 2026, finds no consonant
    whatever in /alara/ or /awaya/ and the composition refuses the whole
    utterance.

    Runs touching either edge are the utterance's own onset and release and
    are left out. Where two markers cover the same stretch the longer wins.

    `want' says how many consonants the phoneme string has. Too few runs and
    the longest is split at its loudest frame, which is how two closures that
    have merged are told apart -- at speed the vowel between them is too
    short for aspiration to return. Too many and the longest `want' of them
    are kept.
    """
    n = len(frames)
    if not n:
        return []
    # Strictly in order, and a later marker is only asked when the ones
    # before it have not found enough. Pooling all three and taking the
    # longest instead was tried: it found the sonorants but broke the
    # obstruents, because voicing also touches nought in a vowel's release
    # and the third bandwidth also moves in /W/, so /akaga/ went from 12.3
    # per cent to 59.1 and gained a dropout it did not have.
    # The third bandwidth marks a sonorant in a carrier and does not always
    # mark one in a word: /l/ takes b3 from 150 to 400 between two /a/ and
    # leaves it at 150 throughout `hello'. The third formant marks it in both,
    # and marks every sonorant -- /r/ takes f3 to 1618, /l/ to 3000, /y/ to
    # 2800, /w/ to 2250, /R/ to 1600, against a vowel's 2300 to 2800 -- so it
    # is the last marker asked, being the broadest and the likeliest to fire
    # where it should not.
    base = frames[0][IDX["b3"]]
    f3s = sorted(f[IDX["f3"]] for f in frames)
    mid_f3 = f3s[len(f3s) // 2] if f3s else 0
    markers = [lambda f: f[IDX["ah"]] == 0,
               lambda f: f[IDX["av"]] == 0,
               lambda f: f[IDX["b3"]] != base,
               lambda f: abs(f[IDX["f3"]] - mid_f3) > F3_MARK]
    out = []
    for test in markers:
        if want is not None and len(out) >= want:
            break
        for a, b in sorted(runs_where(frames, test, edges),
                           key=lambda r: r[0] - r[1]):
            if not any(a <= y and x <= b for x, y in out):
                out.append((a, b))
    out.sort()

    while want is not None and len(out) < want:
        best = None
        for k, (a, b) in enumerate(out):
            if b - a >= 2 and (best is None or
                               b - a > out[best][1] - out[best][0]):
                best = k
        if best is None:
            break
        a, b = out[best]
        peak = max(range(a + 1, b), key=lambda i: frames[i][IDX["av"]])
        if frames[peak][IDX["av"]] <= frames[a][IDX["av"]] and \
           frames[peak][IDX["av"]] <= frames[b][IDX["av"]]:
            break
        out[best:best + 1] = [(a, peak - 1), (peak + 1, b)]

    if want is not None and len(out) > want:
        out = sorted(sorted(out, key=lambda r: r[0] - r[1])[:want])
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
    names = ["enus-pairs.txt", "enus-pairs2.txt", "enus-holdout.txt",
             "enus-initial.txt", "enus-final.txt"]
    if wpm is not None:
        at_rate = ["enus-pairs-%d.txt" % wpm, "enus-pairs2-%d.txt" % wpm,
                   "enus-holdout-%d.txt" % wpm]
        at_rate += ["enus-initial-%d.txt" % wpm, "enus-final-%d.txt" % wpm]
        if all(os.path.exists(os.path.join(here, f)) for f in at_rate[:3]):
            names = [f for f in at_rate
                     if os.path.exists(os.path.join(here, f))]
    for f in names:
        p = os.path.join(here, f)
        if not os.path.exists(p):
            continue
        for name, rec in K.cases(p).items():
            left[(rec["cons"], rec["v1"])].append(rec)
            right[(rec["cons"], rec["v2"])].append(rec)
    # A carrier with a closure recorded is a better witness than one without,
    # and the second of a pair is what the straight line through two
    # measurements is drawn from -- so a spanless one arriving first would
    # disable that interpolation. /l/ has six pairs with no closure and
    # putting them first cost /alara/ 56 per cent against 103.
    for tab in (left, right):
        for k in tab:
            tab[k].sort(key=lambda c: c.get("span") is None)
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
    cons = [p for p in phonemes if p not in VOWELS]
    edges = (bool(phonemes) and phonemes[0] not in VOWELS,
             bool(phonemes) and phonemes[-1] not in VOWELS)
    spans = closures(frames, len(cons), edges)
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
            lp = left.get((c, vp))
            rp = right.get((c, vn))
            # A word-edge consonant has a vowel on one side only, and the key
            # for the missing side is (c, None) -- which lumps all sixteen
            # carriers of that consonant together, so choosing among them by
            # span length picks one with the wrong vowel. During /h/ the
            # formants are already the following vowel's, /h/ having none of
            # its own -- it is that vowel's shape excited by noise -- so a
            # wrong vowel is 780 hertz of f2 wrong from the first frame.
            # Naming the side that does have a vowel fixes it: `hE' is both
            # the initial /h/ and the (h, E) pair.
            #
            # Using ONLY the edge carrier, rather than interpolating across
            # the carriers of (c, vowel), was tried both ways round and was
            # much worse -- 150 per cent against 93.7 -- so the ordinary
            # carriers of the pair are evidently telling it something the
            # edge one alone does not.
            if vp is None:
                lp = rp
            elif vn is None:
                rp = lp

            if not lp or not rp:
                return None, "no measured pair for %s around %s" % (c, vp)
            la, la2 = lp[0], (lp[1] if len(lp) > 1 else None)
            rb = rp[0]
            if la.get("span") is None:
                la = dict(la, span=(0, len(la["frames"]) - 1))
            if rb.get("span") is None:
                rb = dict(rb, span=(0, len(rb["frames"]) - 1))
            if la2 is not None and la2.get("span") is None:
                la2 = None
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
            seq[:room] = got if got else [frames[0][IDX[name]]] * room

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
                seq[i] = last if last is not None else frames[0][IDX[name]]
            last = seq[i]
        for i in range(n):
            out[i][name] = seq[i]

    # The measured voicing rule, which beats splicing on seven of eight
    # chains -- /aCaSa/ 10.4 per cent to 3.6, /akaga/ 12.3 to 7.9, /atapa/
    # 14.5 to 10.9 -- and is never worse by more than a fifth of a point.
    # EVV_CHAIN_AV=0 splices instead, which is how the two were compared.
    if os.environ.get("EVV_CHAIN_AV") != "0":
        av = voicing(frames, spans, order, left, right, n)
        for i in range(n):
            if av[i] is not None:
                out[i]["av"] = av[i]
    return out, None


def staircase(top, steps, n):
    """`n' frames declining one step at a time from `top', in `steps' runs."""
    if n <= 0:
        return []
    out = []
    for k in range(steps):
        lo = (n * k) // steps
        hi = (n * (k + 1)) // steps
        out += [top - k] * (hi - lo)
    return out[:n] + [top - steps + 1] * max(0, n - len(out))


def voicing(frames, spans, order, left, right, n):
    """The whole utterance's voicing, generated rather than spliced.

    `frames' are the engine's own, indexed by IDX; a pair carrier's frames come
    out of the tables and are keyed by name. Mixing the two is a KeyError and
    was one.
    """
    seq = [None] * n

    # How long the letting go is, from whichever carrier ends the utterance.
    rel = 0
    if spans and order:
        c, vp, vn = order[len(spans) - 1]
        rp = right.get((c, vn))
        if rp and rp[0].get("span") is not None:
            t = [f["av"] for f in rp[0]["frames"][rp[0]["span"][1] + 1:]]
            rel = release_len(t)
    if rel == 0:
        rel = AV_RELEASE
    rel = min(rel, n)

    # The first vowel: its own value, then one less.
    first = spans[0][0] if spans else n - rel
    top = frames[0][IDX["av"]]
    if first > 0:
        seq[:first] = staircase(top, AV_FIRST_STEPS, first)

    for k, ((c, vp, vn), (a, b)) in enumerate(zip(order, spans)):
        # The closure is left alone: voicing through it is the consonant's own
        # shape and not a single value -- /t/ holds 35 for four frames and
        # then drops to nought for the burst -- so the shaped path that fits
        # every other parameter fits this one too. Holding one value across it
        # was the last thing wrong here.
        rp = right.get((c, vn))

        lo = b + 1
        hi = spans[k + 1][0] if k + 1 < len(spans) else n - rel
        if hi <= lo:
            continue
        # The voicing does not come back the moment the closure ends. A
        # voiceless stop holds it at nought through its release as well: /p/
        # for four frames past the `ah == 0' span and /k/ for five. The pair
        # carrier's own run-out carries those zeros, so they are taken from
        # it, and the staircase begins after them -- which also puts its step
        # boundaries where the engine has them, since starting four frames
        # early moved every one of them.
        top4 = None
        off = 0
        if rp and rp[0].get("span") is not None:
            tail = [f["av"] for f in rp[0]["frames"][rp[0]["span"][1] + 1:]]
            if tail:
                top4 = max(tail)
                while off < len(tail) and tail[off] < 20:
                    off += 1
        if top4 is None:
            top4 = frames[lo][IDX["av"]]
        off = min(off, hi - lo)
        for i in range(lo, lo + off):
            seq[i] = 0
        seq[lo + off:hi] = staircase(top4, AV_STEPS, hi - lo - off)

    # Letting go: from one below wherever it had got to, down to nought.
    at = n - rel
    if at > 0 and seq[at - 1] is not None:
        start = seq[at - 1] - 1
    else:
        start = top - AV_FIRST_STEPS
    for i in range(at, n):
        k = i - at
        seq[i] = max(0, start - int(start * k / float(max(1, rel - 1))))

    return seq


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
