#!/usr/bin/env python3
"""What a voice does with pitch, measured the same way on both sides.

The intonation rules write bare numbers -- Italian's high accent writes 99 and
120 into the pitch record -- and there is no way to tell from the rules what
those mean in hertz, nor whether the melody they produce is anything like the
language's. Judging it by ear says only "that sounds Italian", which is an
opinion about two things at once.

So this measures a voice. It reads a wav, tracks the fundamental frequency, and
reduces the contour to the handful of numbers intonation is actually made of:
where the voice sits, how wide it ranges, how far it drifts down across a
phrase, how it starts, and how far it falls or rises at the end. The same tool
reads a human recording and reads our own output, which is the whole point --
otherwise the comparison has no units.

Every height is in semitones from the speaker's own median, never in hertz. A
man and a woman speaking the same sentence with the same melody are an octave
apart in hertz and identical in semitones, and a corpus has both.

It also measures rhythm, which needs no marked-up recording either: the
syllables are the loud places in the envelope, and how unevenly they are spaced
is what tells one language's rhythm from another's.

The tracker is normalised cross-correlation on a low-passed, decimated copy,
chosen frame by frame across the whole utterance at once rather than one frame
at a time, and then again with only the candidates near the middle the first
pass found. Both of those are there for the same reason, which
contour() explains. Pure Python and no numpy, because the dev shell has
neither and a dependency for six hundred lines of arithmetic is a poor trade.

Checked two ways. The men and the women of a corpus have to come out with the
same melody once it is in semitones, and over 335 read Polish sentences they
agree to within a tenth of one; and Praat, given the same two-pass treatment,
agrees with this on every file it was held against.

usage: pitch.py <wav>...              one line of numbers a wav
       pitch.py trace <wav>           the contour itself, a frame to a line
       pitch.py survey <tsv> <dir>    a FLEURS-shaped corpus, as one summary
       pitch.py shape <tsv> <dir>     the average shape of a statement in it
"""

import math
import multiprocessing
import os
import struct
import sys
from array import array
from operator import mul


# What a speaking voice can do. Anything outside this is a tracking error
# rather than a pitch, and letting it in costs more than the frames it saves.
F0_MIN = 60.0
F0_MAX = 400.0

STEP = 0.010        # how often to ask
WINDOW = 0.045      # how much to ask about; three periods of the lowest F0
VOICED = 0.45       # correlation below this is not a period
# A window several periods long fits the octave below about as well as it fits
# the truth, so a tie-break is needed. This one is small and only breaks ties:
# the work of keeping a tracker on the right octave is done by the second pass
# in contour(), not by a prior about what pitch a voice ought to have.
OCTAVE = float(os.environ.get("PITCH_OCTAVE", "0.02"))
OCTAVE_REF = 150.0  # where that charge is nothing, so it does not quietly
                    # make every voiced frame dearer than an unvoiced one
# How far from its own middle a voice is allowed to go, once the middle is
# known. Read speech spans four or five semitones, so nine either way is
# generous, and it is half of what an octave error needs.
SPREAD = 9.0


def read_wav(path):
    """One wav as a list of samples, mixed to mono.

    Read by hand rather than through the wave module, which refuses anything
    but integer samples: our own output is sixteen-bit and the corpora are
    thirty-two-bit float, and a tool that reads only one of them cannot
    compare them.
    """
    raw = open(path, "rb").read()
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise SystemExit("pitch: %s is not a wav" % path)
    fmt = None
    data = None
    at = 12
    while at + 8 <= len(raw):
        kind = raw[at:at + 4]
        size = struct.unpack("<I", raw[at + 4:at + 8])[0]
        body = raw[at + 8:at + 8 + size]
        if kind == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif kind == b"data":
            data = body
        at += 8 + size + (size & 1)
    if fmt is None or data is None:
        raise SystemExit("pitch: %s has no format or no samples" % path)
    tag, ch, rate, _bps, _align, bits = fmt

    if tag == 1 and bits == 16:
        xs = array("h")
        xs.frombytes(data[:len(data) - len(data) % 2])
        xs = [float(v) for v in xs]
    elif tag == 3 and bits == 32:
        xs = array("f")
        xs.frombytes(data[:len(data) - len(data) % 4])
        # Brought up to the same loudness as sixteen-bit, so that one floor
        # for silence serves both.
        xs = [v * 32768.0 for v in xs]
    else:
        raise SystemExit("pitch: %s is format %d at %d bits, which this does"
                         " not read" % (path, tag, bits))

    if ch > 1:
        xs = [sum(xs[i:i + ch]) / ch for i in range(0, len(xs) - ch + 1, ch)]
    return rate, xs


def decimate(rate, xs, want=4000.0):
    """A copy low enough to correlate cheaply and low-passed so that nothing
    above it folds back in as a false period."""
    d = max(1, int(round(rate / want)))
    if d == 1:
        return float(rate), [float(x) for x in xs]
    # A moving average of d*2 samples has its first null at rate/(2d), which is
    # the new Nyquist. Two of them in a row, because one leaks badly.
    k = d * 2
    run = 0
    box = [0.0] * len(xs)
    for i, x in enumerate(xs):
        run += x
        if i >= k:
            run -= xs[i - k]
        box[i] = run / k
    run = 0.0
    for i in range(len(box)):
        run += box[i]
        if i >= k:
            run -= box[i - k]
        box[i] = run / k
    return rate / d, box[::d]


def candidates(rate, xs, f0_min=F0_MIN, f0_max=F0_MAX):
    """Every period each frame could plausibly have, with how well it fits.

    Deciding a frame on its own is what makes a tracker jump an octave: a
    voice periodic at T is also fairly periodic at 2T, and half the harmonics
    of a weak fundamental make it look periodic at T/2. Both are local maxima
    of the correlation and either can win a single frame by a hair. So the
    frame offers all of them and the choosing happens later, over the whole
    utterance at once.
    """
    fs, y = decimate(rate, xs)
    n = len(y)
    win = int(WINDOW * fs)
    hop = int(STEP * fs)
    lo = max(2, int(fs / f0_max))
    hi = min(win - 1, int(fs / f0_min))
    if n < win + hi or hop < 1:
        return []

    # Sums of squares up to each sample, so a window's energy is a
    # subtraction rather than a loop. Every lag of every frame wants one.
    cs = [0.0] * (n + 1)
    for i, v in enumerate(y):
        cs[i + 1] = cs[i] + v * v

    starts = list(range(0, n - win - hi, hop))
    rms = [math.sqrt(max(0.0, cs[s + win] - cs[s]) / win) for s in starts]
    loud = sorted(rms)
    # Below a fortieth of the loud end of the file there is no voice to find,
    # only room noise that correlates with itself.
    floor = 0.025 * loud[int(0.95 * (len(loud) - 1))] if loud else 0.0

    out = []
    for k, start in enumerate(starts):
        t = start / fs
        if rms[k] <= floor:
            out.append((t, rms[k], []))
            continue
        here = y[start:start + win]
        e0 = cs[start + win] - cs[start]
        if e0 <= 0.0:
            out.append((t, rms[k], []))
            continue
        rs = []
        for lag in range(lo, hi + 1):
            a = start + lag
            e1 = cs[a + win] - cs[a]
            rs.append(sum(map(mul, here, y[a:a + win])) / math.sqrt(e0 * e1)
                      if e1 > 0.0 else 0.0)

        got = []
        for j in range(1, len(rs) - 1):
            if rs[j] < 0.25 or rs[j] <= rs[j - 1] or rs[j] < rs[j + 1]:
                continue
            lag = lo + j
            # Where the parabola through the three points peaks, so the answer
            # is not quantised to whole samples.
            a, b, c = rs[j - 1], rs[j], rs[j + 1]
            den = a - 2 * b + c
            if den != 0.0:
                lag += 0.5 * (a - c) / den
            got.append((fs / lag, rs[j]))
        got.sort(key=lambda p: -p[1])
        out.append((t, rms[k], got[:6]))
    return out


def track(cands):
    """One path through the candidates, chosen for the whole utterance.

    A frame pays for how badly its period fits, and a step between frames pays
    for how far the pitch moved. A real voice moves a semitone or two between
    one frame and the next, so a jump of twelve has to be paid for twelve
    times over -- which is what an octave error cannot afford, however well it
    fits the frame it sits in.
    """
    if not cands:
        return []
    unvoiced = 1.0 - VOICED          # what a frame pays for having no pitch
    switch = 0.20                    # and for starting or stopping voicing
    per_semitone = 0.05

    # Each state is an index into the frame's candidates, or -1 for unvoiced.
    prev_cost = None
    prev_states = None
    back = []
    for _t, _rms, got in cands:
        states = [-1] + list(range(len(got)))
        cost = []
        for s in states:
            cost.append(unvoiced if s < 0 else 1.0 - got[s][1]
                        + OCTAVE * math.log(OCTAVE_REF / got[s][0], 2.0))
        if prev_cost is None:
            prev_cost, prev_states = cost, states
            back.append([None] * len(states))
            continue
        step = []
        for i, s in enumerate(states):
            best = None
            for j, p in enumerate(prev_states):
                c = prev_cost[j]
                if s < 0 or p < 0:
                    c += 0.0 if (s < 0 and p < 0) else switch
                else:
                    a = cands[len(back) - 1][2][p][0]
                    b = got[s][0]
                    c += per_semitone * abs(12.0 * math.log(b / a, 2.0))
                if best is None or c < best[0]:
                    best = (c, j)
            step.append(best[1])
            cost[i] += best[0]
        back.append(step)
        prev_cost, prev_states = cost, states

    # Walk back from the cheapest end.
    at = min(range(len(prev_cost)), key=lambda i: prev_cost[i])
    path = [0] * len(cands)
    for i in range(len(cands) - 1, -1, -1):
        path[i] = at
        at = back[i][at] if back[i][at] is not None else 0

    out = []
    for i, (t, rms, got) in enumerate(cands):
        s = ([-1] + list(range(len(got))))[path[i]]
        if s < 0:
            out.append((t, 0.0, rms, 0.0))
        else:
            out.append((t, got[s][0], rms, got[s][1]))
    return drop_stragglers(out)


def contour(rate, xs, f0_min=F0_MIN, f0_max=F0_MAX):
    """The fundamental frequency every ten milliseconds, and how loud the
    frame was. Zero where the frame is not voiced.

    Twice, because the first answer is what makes the second one possible. A
    tracker asked to choose between a pitch and the octave below it has
    nothing to go on inside one frame, and the two are equally periodic; but
    across a whole sentence one of them is where the voice lives and the other
    is nowhere near it. So the first pass finds the middle -- which survives
    the errors, being a median -- and the second pass is not offered them.
    """
    cands = candidates(rate, xs, f0_min, f0_max)
    first = track(cands)
    v = [f for _t, f, _rms, _r in first if f]
    if len(v) < 8:
        return first
    mid = median(v)
    lo = mid * 2.0 ** (-SPREAD / 12.0)
    hi = mid * 2.0 ** (SPREAD / 12.0)
    near = [(t, rms, [c for c in got if lo <= c[0] <= hi])
            for t, rms, got in cands]
    return track(near)


def drop_stragglers(frames):
    """One or two voiced frames alone among unvoiced ones are a click or a
    creak, not a pitch, and at the edge of a word they are the tracker
    guessing at a fricative."""
    f = [x[1] for x in frames]
    run = 0
    for i in range(len(f) + 1):
        if i < len(f) and f[i]:
            run += 1
            continue
        if run and run < 3:
            for j in range(i - run, i):
                f[j] = 0.0
        run = 0
    return [(frames[i][0], f[i], frames[i][2], frames[i][3])
            for i in range(len(frames))]


def semitones(f, ref):
    return 12.0 * math.log(f / ref, 2.0)


def median(xs):
    s = sorted(xs)
    n = len(s)
    if not n:
        return 0.0
    return s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])


def pct(xs, p):
    s = sorted(xs)
    if not s:
        return 0.0
    return s[min(len(s) - 1, max(0, int(round(p * (len(s) - 1)))))]


def summary(frames):
    """The contour as the numbers intonation is made of.

    Everything but `median' is in semitones from that median, so two speakers
    an octave apart come out the same.
    """
    v = [(t, f) for t, f, _rms, _r in frames if f]
    if len(v) < 8:
        return None
    hz = [f for _t, f in v]
    mid = median(hz)
    st = [(t, semitones(f, mid)) for t, f in v]

    # How far the voice drifts down across the whole thing, by least squares
    # on the voiced frames. Declination is the part of a contour that is not
    # about any one word.
    n = len(st)
    tm = sum(t for t, _ in st) / n
    sm = sum(s for _, s in st) / n
    num = sum((t - tm) * (s - sm) for t, s in st)
    den = sum((t - tm) ** 2 for t, _ in st)
    slope = num / den if den else 0.0

    t0, t1 = st[0][0], st[-1][0]
    onset = median([s for t, s in st if t <= t0 + 0.15])
    tail = median([s for t, s in st if t >= t1 - 0.15])
    # The last movement, from the highest point of the last half second to the
    # end of it: a fall in a statement, a rise in a question.
    last = [s for t, s in st if t >= t1 - 0.50]
    move = (last[-1] - max(last)) if last else 0.0

    peaks = []
    for i in range(1, n - 1):
        if st[i][1] > st[i - 1][1] and st[i][1] >= st[i + 1][1]:
            peaks.append(st[i][1])
    big = [p for p in peaks if p > 1.0]

    out = {
        "seconds": t1 - t0,
        "voiced": len(v) / float(len(frames)),
        "median": mid,
        "span": pct([s for _, s in st], 0.90) - pct([s for _, s in st], 0.10),
        "top": pct([s for _, s in st], 0.95),
        "bottom": pct([s for _, s in st], 0.05),
        "declination": slope,
        "onset": onset,
        "tail": tail,
        "move": move,
        "peaks": len(big) / max(0.001, t1 - t0),
        "peak_height": median(big) if big else 0.0,
    }
    r = rhythm(frames)
    out.update(r if r else {"syllables": 0.0, "gap": 0.0,
                            "variability": 0.0})
    return out


def nuclei(frames):
    """Where the syllables are, from loudness alone.

    A syllable is a loud place with quieter places either side of it, and in a
    voice the loud place is the vowel. Taking them from the envelope means no
    alignment and no dictionary is needed, which is the only reason rhythm can
    be measured over a corpus nobody has marked up.

    The dip either side has to be real -- three decibels -- or every wobble
    inside one long vowel counts as another syllable.
    """
    db = []
    for t, f0, rms, _r in frames:
        db.append((t, 20.0 * math.log(rms, 10.0) if rms > 1.0 else -60.0, f0))
    # Half of forty milliseconds either side, which smooths the pitch period
    # out of the envelope without smoothing a short vowel away.
    k = 2
    sm = []
    for i in range(len(db)):
        lo = max(0, i - k)
        hi = min(len(db), i + k + 1)
        sm.append(sum(x[1] for x in db[lo:hi]) / (hi - lo))

    top = max(sm) if sm else -60.0
    out = []
    for i in range(1, len(sm) - 1):
        if sm[i] < top - 25.0 or not db[i][2]:
            continue
        if sm[i] <= sm[i - 1] or sm[i] < sm[i + 1]:
            continue
        left = min(sm[max(0, i - 25):i + 1])
        right = min(sm[i:i + 26])
        if sm[i] - max(left, right) >= 3.0:
            out.append(db[i][0])
    return out


def rhythm(frames):
    """How the syllables are spaced: how many a second, and how unlike its
    neighbour each one is.

    The second number is the pairwise variability index, which is what tells
    one language's rhythm from another's: a language that gives every syllable
    the same time scores low, and one that stretches the stressed and squeezes
    the rest scores high. It compares each interval with the next rather than
    with the average, so speaking faster or slower does not change it.
    """
    at = nuclei(frames)
    if len(at) < 4:
        return None
    gaps = [at[i + 1] - at[i] for i in range(len(at) - 1)]
    gaps = [g for g in gaps if g < 0.60]
    if len(gaps) < 3:
        return None
    pv = [abs(gaps[i] - gaps[i + 1]) / (0.5 * (gaps[i] + gaps[i + 1]))
          for i in range(len(gaps) - 1)]
    return {
        "syllables": len(at) / (frames[-1][0] - frames[0][0]),
        "gap": 1000.0 * median(gaps),
        "variability": 100.0 * (sum(pv) / len(pv)),
    }


ORDER = ["seconds", "voiced", "median", "span", "top", "bottom",
         "declination", "onset", "tail", "move", "peaks", "peak_height",
         "syllables", "gap", "variability"]


def show(path):
    rate, xs = read_wav(path)
    s = summary(contour(rate, xs))
    if not s:
        print("%s: too little voice to measure" % os.path.basename(path))
        return False
    print("%s: %s" % (os.path.basename(path),
                      ", ".join("%s %.2f" % (k, s[k]) for k in ORDER)))
    return True


def trace(path):
    rate, xs = read_wav(path)
    frames = contour(rate, xs)
    v = [f for _t, f, _rms, _r in frames if f]
    mid = median(v) if v else 0.0
    print("# %s, median %.1f hz" % (os.path.basename(path), mid))
    print("# seconds, hertz, semitones from the median, loudness, correlation")
    for t, f0, rms, r in frames:
        if f0:
            print("%6.3f %7.2f %+7.2f %8.1f %5.2f"
                  % (t, f0, semitones(f0, mid), rms, r))
        else:
            print("%6.3f       -       -  %8.1f %5.2f" % (t, rms, r))
    return True


def measure(path):
    """One file, as a summary or nothing. Named at the top level because a
    pool has to be able to pickle it."""
    try:
        rate, xs = read_wav(path)
        return summary(contour(rate, xs))
    except SystemExit:
        return None


def survey(tsv, where):
    """A corpus in FLEURS's shape: the wav's name in the second column, the
    text as written in the third, the speaker's sex in the last.

    The text is only read for its final mark, because that is what says
    whether the melody at the end is a statement's or a question's.
    """
    rows = []
    for line in open(tsv, encoding="utf-8"):
        w = line.rstrip("\n").split("\t")
        if len(w) < 7:
            continue
        p = os.path.join(where, w[1])
        if os.path.exists(p):
            rows.append((p, w[2], w[6]))

    # An hour of audio measured a frame at a time in Python is minutes on one
    # core and seconds on all of them, and there is nothing shared to get
    # wrong: a file's contour depends on that file alone.
    jobs = min(len(rows), os.cpu_count() or 1)
    with multiprocessing.Pool(jobs) as pool:
        got = pool.map(measure, [r[0] for r in rows], chunksize=1)

    kinds = {}
    done = 0
    for (path, text, sex), s in zip(rows, got):
        if not s:
            continue
        end = text.rstrip()[-1:]
        kind = {".": "statement", "?": "question", "!": "exclamation"}.get(end)
        if kind is None:
            kind = "unmarked"
        for k in (kind, kind + " " + sex.lower(), "all"):
            kinds.setdefault(k, []).append(s)
        done += 1

    if not done:
        print("pitch: nothing in %s to measure" % where)
        return False
    print("# %s, %d of %d utterances measured" % (tsv, done, len(rows)))
    print("# every height in semitones from that utterance's own median")
    for k in sorted(kinds):
        g = kinds[k]
        print("%-20s n %-4d %s" % (k, len(g), ", ".join(
            "%s %.2f" % (f, median([x[f] for x in g])) for f in ORDER)))
    return True


SLICES = 20
TAIL = 12          # hundredths of a second at the end, a slice each


def one_shape(path):
    """One utterance as a shape rather than as numbers: its pitch over the
    stretch it is voiced, in twenty equal slices, and again over the last
    second in tenths.

    Two views because the two halves of a contour are measured differently.
    What an accent does is a proportion of the phrase, so it wants the phrase
    stretched to a fixed length; what the end does is a fixed number of
    milliseconds of falling, and stretching a long sentence and a short one to
    the same width would smear it away.
    """
    try:
        rate, xs = read_wav(path)
    except SystemExit:
        return None
    v = [(t, f) for t, f, _rms, _r in contour(rate, xs) if f]
    if len(v) < 8:
        return None
    mid = median([f for _t, f in v])
    st = [(t, semitones(f, mid)) for t, f in v]
    t0, t1 = st[0][0], st[-1][0]
    if t1 - t0 < 0.5:
        return None

    whole = [[] for _ in range(SLICES)]
    for t, s in st:
        k = min(SLICES - 1, int(SLICES * (t - t0) / (t1 - t0)))
        whole[k].append(s)
    end = [[] for _ in range(TAIL)]
    for t, s in st:
        back = t1 - t
        if back < TAIL * 0.1:
            end[TAIL - 1 - int(back / 0.1)].append(s)
    return ([sum(b) / len(b) if b else None for b in whole],
            [sum(b) / len(b) if b else None for b in end])


def shape(tsv, where):
    """The average shape of a statement in one voice, human or ours.

    A median down each slice rather than a mean, so that one utterance the
    tracker lost cannot bend the answer.
    """
    rows = []
    for line in open(tsv, encoding="utf-8"):
        w = line.rstrip("\n").split("\t")
        if len(w) < 7 or w[2].rstrip()[-1:] != ".":
            continue
        p = os.path.join(where, w[1])
        if os.path.exists(p):
            rows.append(p)

    jobs = min(len(rows), os.cpu_count() or 1)
    with multiprocessing.Pool(jobs) as pool:
        got = [g for g in pool.map(one_shape, rows, chunksize=1) if g]
    if not got:
        print("pitch: nothing in %s to measure" % where)
        return False

    print("# %s, %d statements" % (tsv, len(got)))
    print("# across the phrase, in twentieths, semitones from the median")
    print("  " + " ".join(
        "%+5.2f" % median([g[0][i] for g in got if g[0][i] is not None])
        for i in range(SLICES)))
    print("# the last %d hundredths of a second, a tenth each" % (TAIL * 10))
    print("  " + " ".join(
        "%+5.2f" % median([g[1][i] for g in got if g[1][i] is not None])
        for i in range(TAIL)))
    return True


def main(argv):
    if argv[:1] == ["trace"] and len(argv) == 2:
        return 0 if trace(argv[1]) else 1
    if argv[:1] == ["survey"] and len(argv) == 3:
        return 0 if survey(argv[1], argv[2]) else 1
    if argv[:1] == ["shape"] and len(argv) == 3:
        return 0 if shape(argv[1], argv[2]) else 1
    if not argv:
        print(__doc__.strip())
        return 2
    ok = True
    for p in argv:
        ok = show(p) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
