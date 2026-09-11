#!/usr/bin/env python3
"""An utterance as a list of segments, and a word rebuilt out of borrowed ones.

`EVV_ARRAY_TAP` says what the engine interpolated each frame from: a value at
a moment and a straight line to the next. It also says where each generator
run began and ended, and there is exactly one run per phoneme plus a trailing
silence -- /atapa/ gives six, /sIstxm/ seven, and the /st/ cluster does not
merge them. Each run's breakpoints tile exactly its own interval.

So an utterance is a list of segments, each a duration and a handful of
breakpoints, and a word can be assembled from segments harvested out of other
utterances. Whether that comes out identical to the engine's own is the
question this answers, because it is the question of whether a segment depends
on more than its immediate neighbours.

    tools/measure/segs.py <probe>
    tools/measure/segs.py <probe> --words <phoneme string>...
    tools/measure/segs.py <probe> --durations <phoneme string>...

The first runs the hand-picked borrowing tests. The second builds each word
out of segments harvested from donor utterances made up for the purpose --
`aCV' for a word-initial consonant, `aC1VC2a' for a vowel between two
consonants, and so on -- so nothing about the word itself is used except how
long each of its segments is.

That exception is the finding. A segment's values and the spans between them
are fixed by the segment and its two neighbours, and one span absorbs
whatever duration the segment is given: /hE/ before /l/ is 570, 1650 and
2480 rising to 1500 and 2680, over spans of 80 and 74 in `hello' and 80 and
87 in the made-up `hEla', which is the whole of the difference between them.
So the values are a table and the durations are a separate question, which
is the same division `tools/measure/chain.py' already worked under.
"""

import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import replay as R                                       # noqa: E402

STEP = 5


def tapped(probe, text):
    """One utterance's segments and the frames the engine made from them.

    A breakpoint belongs to the run it was crossed during, which is where it
    stands in the file and not which run's interval contains it: a cursor
    reset leaves two gaps over one moment and only the order says which was
    in force.
    """
    with tempfile.TemporaryDirectory() as w:
        c = os.path.join(w, "c.txt")
        with open(c, "w") as f:
            f.write(text + "\n")
        env = dict(os.environ)
        env["EVV_ARRAY_TAP"] = os.path.join(w, "a.txt")
        env["EVV_KLATT_TAP"] = os.path.join(w, "k.txt")
        subprocess.run([probe, "@" + c, os.path.join(w, "c.wav"), "a"],
                       capture_output=True, env=env)
        segs = []
        times = []
        for line in open(env["EVV_ARRAY_TAP"]):
            f = line.split()
            if not f:
                continue
            if f[0] == "run":
                segs.append({"from": int(f[2]), "to": int(f[4]), "par": {}})
            elif f[0] == "frame":
                times.append(int(f[1]))
            elif f[0] == "at" and segs:
                lo, hi = f[5].split("..")
                segs[-1]["par"].setdefault(f[2], []).append(
                    (int(f[1]), int(f[4]), int(lo), int(hi)))
        frames = []
        if os.path.exists(env["EVV_KLATT_TAP"]):
            for line in open(env["EVV_KLATT_TAP"]):
                if line.startswith("#") or line.startswith("step\t"):
                    continue
                p = line.rstrip("\n").split("\t")
                if len(p) == len(R.NAMES):
                    frames.append([int(x) for x in p])
        for s in segs:
            s["dur"] = s["to"] - s["from"]
        return segs, times, frames


def relative(seg):
    """One segment's breakpoints with its own start as the origin."""
    out = {}
    for name, gaps in seg["par"].items():
        out[name] = [(at - seg["from"], span, v0, v1)
                     for at, span, v0, v1 in gaps]
    return out


def assemble(chosen):
    """Segments laid end to end: the breakpoints, and how long altogether."""
    par = {}
    at = 0
    for dur, gaps in chosen:
        for name, gs in gaps.items():
            for a, span, v0, v1 in gs:
                par.setdefault(name, []).append((at + a, span, v0, v1))
        at += dur
    return par, at


def value(gaps, at):
    """The line in force at `at'. List order breaks a tie, as the cursor
    does."""
    best = None
    for a, span, v0, v1 in gaps:
        if a <= at:
            best = (a, span, v0, v1)
    if best is None:
        return None
    a, span, v0, v1 = best
    rise = v1 - v0
    if rise == 0 or span == 0:
        return v0
    n = (at - a) * rise
    return (abs(n) // span) * (1 if n >= 0 else -1) + v0


def unfold(par, total, step=STEP):
    """Every frame the breakpoints imply, on the plain grid the engine uses."""
    out = []
    at = 0
    while at <= total - (total % step or step):
        if at > total:
            break
        row = {}
        for name, gaps in par.items():
            v = value(gaps, at)
            if v is not None:
                row[name] = v
        out.append((at, row))
        at += step
    return out


# f0 is not a segment's property. It is the phrase's melody, laid over the
# whole utterance by the intonation pass, so a segment borrowed from another
# utterance carries the wrong piece of a different tune and disagrees on
# nearly every frame. That is right rather than a failure, and it is the one
# parameter a segment table cannot hold.
APART = ("f0",)


def compare(built, frames):
    """Which parameters disagree, and on how many frames."""
    wrong = {}
    n = min(len(built), len(frames))
    for i in range(n):
        for name, v in built[i][1].items():
            if name in APART:
                continue
            if v != frames[i][R.NAMES.index(name)]:
                wrong[name] = wrong.get(name, 0) + 1
    return wrong, n


# Each case: the word to rebuild, and where each of its segments is borrowed
# from -- an utterance and which of its segments. The point is that no
# segment comes from the word itself, so a match says a segment depends on
# nothing beyond its immediate neighbours.
CASES = (
    # Borrowed across utterances: the vowel between the two stops comes from
    # a carrier whose outer vowels are /u/, so a match says the two away
    # neighbours do not reach it.
    ("atapa", (("ata", 0), ("ata", 1), ("utapu", 2), ("apa", 1),
               ("apa", 2), ("apa", 3))),
    ("ata",   (("atapa", 0), ("atapa", 1), ("ata", 2), ("ata", 3))),
    ("asasa", (("asa", 0), ("asa", 1), ("isasi", 2), ("asa", 1),
               ("asa", 2), ("asa", 3))),
    ("akapa", (("aka", 0), ("aka", 1), ("ukapu", 2), ("apa", 1),
               ("apa", 2), ("apa", 3))),
    # Borrowed across positions: /p/ from the second syllable of a longer
    # word, and /t/ likewise, both in the same immediate context.
    ("atapa", (("ata", 0), ("ata", 1), ("utapu", 2), ("apapa", 3),
               ("apa", 2), ("apa", 3))),
    ("ata",   (("atata", 0), ("atata", 3), ("ata", 2), ("ata", 3))),
    # /h/ has no segment of its own, being the following vowel's shape
    # excited by noise, so `hElo' is three segments and a silence.
    ("hElo",  (("hEla", 0), ("Elo", 1), ("Elo", 2), ("Elo", 3))),
    # A cluster, which does not merge: /sIstxm/ is seven runs for six
    # phonemes.
    ("sIstxm", (("sIta", 0), ("sIsa", 1), ("Ista", 1), ("astxma", 2),
                ("astxma", 3), ("axm", 2), ("axm", 3))),
)


VOWELS = set("iIeEAauUocYWOXHx")


def contexts(phonemes):
    """One entry a segment: what it is, and what is either side of it.

    /h/ has no segment of its own -- it is the following vowel's shape
    excited by noise, and the engine gives the two of them one run -- so it
    is folded into the vowel after it and the pair keeps the vowel's own
    place in the chain.
    """
    units = []
    i = 0
    while i < len(phonemes):
        if (phonemes[i] == "h" and i + 1 < len(phonemes)
                and phonemes[i + 1] in VOWELS):
            units.append("".join(phonemes[i:i + 2]))
            i += 2
        else:
            units.append(phonemes[i])
            i += 1
    out = []
    for k, u in enumerate(units):
        out.append((u, units[k - 1][-1] if k else None,
                    units[k + 1][0] if k + 1 < len(units) else None))
    return out


def donor(unit, left, right):
    """An utterance made up to hold this segment, and which segment it is.

    Nothing more than the segment and its two neighbours, because nothing
    more reaches it: what a neighbour has on its own far side changes how
    long the segment is and not one of its values, and the length is taken
    from the target rather than from here.
    """
    # A vowel is padded with /a/ on any side that has a neighbour, so it
    # sits inside a word rather than at its edge: a vowel alone in `pa' is a
    # stressed monosyllable and takes the full targets where the same vowel
    # at the end of /atapa/ is reduced. A consonant is not padded, its
    # neighbours being vowels already -- padding it gives `aataa' for /t/
    # between two /a/, and a vowel sequence is not the same context.
    pad = "a" if unit[-1] in VOWELS else ""
    text = (((pad + left) if left else "") + unit
            + ((right + pad) if right else ""))
    for k, (u, l, r) in enumerate(contexts(list(text))):
        if (u, l, r) == (unit, left, right):
            return text, k
    return text, None


def harvest(get, phonemes):
    """Each segment of `phonemes', taken from a donor utterance of its own."""
    out = []
    for unit, left, right in contexts(phonemes):
        text, k = donor(unit, left, right)
        if k is None:
            return None, "no donor holds %s between %s and %s" % (
                unit, left, right)
        segs, _, _ = get(text)
        if k >= len(segs):
            return None, "%s has no segment %d for %s" % (text, k, unit)
        out.append((text, k, segs[k]))
    return out, None


def stretched(seg, want):
    """One segment's breakpoints, with the last span absorbing the length.

    Which is what the engine does: /hE/ before /l/ keeps its 80 and gives
    the rest of the word's length to the span after it.
    """
    rel = relative(seg)
    have = seg["dur"]
    if want == have:
        return rel, None
    out = {}
    for name, gaps in rel.items():
        gaps = list(gaps)
        a, span, v0, v1 = gaps[-1]
        span += want - have
        if span < 0:
            return None, "%s's last span would be %d" % (name, span)
        gaps[-1] = (a, span, v0, v1)
        out[name] = gaps
    return out, None


def words(probe, get, targets):
    """Hold each word's own segments against ones harvested from donors.

    Not a frame rebuild, because that would need a duration model and there
    is not one yet. This asks the question a duration model would leave
    over: given a segment and its two neighbours, are the values and the
    breakpoints between them already decided? A `same' line says they are,
    and the spans it names are the ones the length went into.
    """
    tally = {"same": 0, "spans": 0, "values": 0, "shape": 0}
    which = {}
    for text in targets:
        segs, _, _ = get(text)
        got, why = harvest(get, list(text))
        if got is None:
            print("%-12s %s" % (text, why))
            continue
        mark = {"same": 0, "spans": 0, "values": 0, "shape": 0}
        for i, (src, k, seg) in enumerate(got):
            if i >= len(segs):
                break
            bad_shape = bad_value = False
            mine = relative(segs[i])
            theirs = relative(seg)
            names = sorted(set(mine) | set(theirs))
            spans = []
            for name in names:
                if name in APART:
                    continue
                a = mine.get(name, [])
                b = theirs.get(name, [])
                if len(a) != len(b):
                    bad_shape = True
                    continue
                for ga, gb in zip(a, b):
                    if (ga[0], ga[2], ga[3]) != (gb[0], gb[2], gb[3]):
                        bad_value = True
                        break
                else:
                    diff = [j for j, (ga, gb) in enumerate(zip(a, b))
                            if ga[1] != gb[1]]
                    if diff:
                        spans.append("%s@%s" % (name, ",".join(
                            "%d/%d" % (j, len(a) - 1) for j in diff)))
            for x in spans:
                which[x.split("@")[1]] = which.get(x.split("@")[1], 0) + 1
            kind = ("shape" if bad_shape else "values" if bad_value
                    else "spans" if spans else "same")
            mark[kind] += 1
            tally[kind] += 1
        print("%-12s %2d segments from %-28s %s"
              % (text, len(got), ",".join(src for src, _, _ in got),
                 "  ".join("%s %d" % (k, v) for k, v in
                           (("identical", mark["same"]),
                            ("span only", mark["spans"]),
                            ("values", mark["values"]),
                            ("shape", mark["shape"])) if v)))
    print()
    print("%d segments identical, %d differing only in a span, "
          "%d in a value, %d in shape"
          % (tally["same"], tally["spans"], tally["values"], tally["shape"]))
    if which:
        print("the spans that differ, as position out of last: %s"
              % ", ".join("%s x%d" % kv for kv in sorted(which.items())))
    return 1 if tally["values"] or tally["shape"] else 0


def pieces(seg):
    """The lengths a segment is built out of.

    A segment is not one stretch. `insert_2ptv' puts a duration into field 9
    of the spine several times over -- /hE/ in `hello' is 80 and then 74 --
    and those numbers are, to the digit, the spans of the segment's formant
    breakpoints. So the boundaries can be read back off the gaps without
    tapping the spine at all: every offset any parameter changes at is a
    piece boundary.
    """
    rel = relative(seg)
    edges = {0, seg["dur"]}
    for gaps in rel.values():
        for a, span, _, _ in gaps:
            if 0 <= a <= seg["dur"]:
                edges.add(a)
            if 0 <= a + span <= seg["dur"]:
                edges.add(a + span)
    e = sorted(edges)
    return [b - a for a, b in zip(e, e[1:])]


def durations(probe, get, targets):
    """How long each segment is, gathered by what is around it.

    The question is whether a duration is decided by the same key the values
    are -- the phoneme and its two neighbours -- and it is not.

    Totals are what to compare, not pieces. A boundary is only visible where
    some parameter changes at it, so /p/ between two /a/ reads as 30+70+5 in
    four words and as one piece of 105 in a fifth, which is the same segment
    with one boundary unobserved rather than a different one.
    """
    seen = {}
    for text in targets:
        segs, _, _ = get(text)
        units = contexts(list(text))
        for i, (unit, left, right) in enumerate(units):
            if i >= len(segs):
                break
            key = "%s:%s_%s" % (unit, left or ".", right or ".")
            seen.setdefault(key, []).append(
                (text, i, len(units), segs[i]["dur"], pieces(segs[i])))
    same = []
    hidden = []
    differ = []
    for key in sorted(seen):
        rows = seen[key]
        if len(rows) < 2:
            continue
        if len(set(r[3] for r in rows)) > 1:
            differ.append((key, rows))
        elif len(set(tuple(r[4]) for r in rows)) > 1:
            hidden.append((key, rows))
        else:
            same.append((key, rows))
    for key, rows in differ:
        print("%-12s %s" % (key, "  ".join(
            "%s #%d of %d, %d ms" % (t, i, n, d)
            for t, i, n, d, p in rows)))
    print()
    print("%d contexts the same length wherever they were seen, "
          "%d the same length with a boundary unobserved, %d different"
          % (len(same), len(hidden), len(differ)))
    if same:
        print("the same: %s" % ", ".join(k for k, _ in same))
    return 0


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    probe = argv[1]
    cache = {}

    def get(text):
        if text not in cache:
            cache[text] = tapped(probe, "`[.1%s]" % text)
        return cache[text]

    if "--durations" in argv:
        return durations(probe, get, argv[argv.index("--durations") + 1:])
    if "--words" in argv:
        return words(probe, get, argv[argv.index("--words") + 1:])

    bad = 0
    for want, sources in CASES:
        _, _, frames = get(want)
        chosen = []
        for text, k in sources:
            segs, _, _ = get(text)
            if k >= len(segs):
                print("%-8s %s has no segment %d" % (want, text, k))
                bad = 1
                chosen = None
                break
            chosen.append((segs[k]["dur"], relative(segs[k])))
        if chosen is None:
            continue
        par, total = assemble(chosen)
        built = unfold(par, total)
        wrong, n = compare(built, frames)
        print("%-8s %3d frames engine, %3d built, %s"
              % (want, len(frames), len(built),
                 "all exact" if not wrong and n == len(frames) else
                 " ".join("%s:%d" % kv for kv in sorted(wrong.items()))
                 or "lengths differ"))
        if wrong or n != len(frames):
            bad = 1
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv))
