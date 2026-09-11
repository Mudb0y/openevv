#!/usr/bin/env python3
"""How long each segment lasts, and what that turns out to depend on.

`lang/enus/enus.segments' holds what every segment's parameters reach and no
span at all, because a span is not a property of a segment: the same phoneme
between the same neighbours at the same stress is one length in one word and
another in the next. `docs/authoring.md' has the measurement -- stress,
syllable count, something further off than a neighbour, position in the word.

So a duration table wants a longer key, and this measures how much longer
rather than guessing. It harvests every segment of the word corpus with the
prosodic context around it, then asks of each candidate key: of the contexts
seen more than once, how many came out the same length every time. A key that
answers everywhere is the key.

    tools/module/durations.py <probe> [<tag>] [--words N] [--jobs N]
                              [--save <path>] [--load <path>]
"""

import collections
import os
import re
import sys
from concurrent.futures import ProcessPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import segments as S                                     # noqa: E402

PROBE = None


def syllables(body):
    """The annotation split into syllables, each with its stress.

    A stress mark stands before the syllable it marks, so the marks are the
    boundaries. A body that opens without one has an unmarked run in front,
    which is kept as a syllable of its own so the phonemes are not lost.
    """
    inner = re.sub(r'^`\[|\]$', '', body)
    out = []
    for piece in re.split(r'(\.[0-9])', inner):
        if not piece:
            continue
        if re.match(r'^\.[0-9]$', piece):
            out.append([piece[1], []])
        else:
            if not out:
                out.append(["0", []])
            out[-1][1].extend(piece)
    return [(st, ph) for st, ph in out if ph]


def prosody(body):
    """Every unit of a word with the prosodic context around it.

    One entry a run: the unit and its two neighbours and its own stress, as
    the segment table keys them, and then which syllable it is in, how many
    there are, how far from the end, where it sits inside its syllable, and
    what the stress is either side of that syllable.
    """
    syls = syllables(body)
    flat = []
    for i, (st, ph) in enumerate(syls):
        for p in ph:
            flat.append((p, st, i))
    got = S.units([(p, st) for p, st, _ in flat])
    # `units' folds /h/ into the vowel after it and splits the affricates
    # and diphthongs in two, so walk both lists together to keep each unit's
    # syllable.
    where = []
    i = 0
    for unit, left, right, stress in got:
        where.append(flat[min(i, len(flat) - 1)][2])
        # A split half does not consume a phoneme of its own the second time
        # round, and a fold consumes two.
        if unit.endswith("2") and unit[:-1] in S.TWO_RUNS:
            pass
        elif unit.endswith("1") and unit[:-1] in S.TWO_RUNS:
            i += 1
        elif len(unit) == 2 and unit[0] == "h":
            i += 2
        else:
            i += 1
    n = len(syls)
    # What the last syllable of the word is like. `abacuses' with its final
    # vowel swapped moves only that vowel and the consonant after it, but
    # `hElo' moves all three of its segments, and only when the final vowel
    # is a lax one -- /E/, /I/, /X/, /H/, /a/, /A/ -- and not for /e/, /o/,
    # /i/ or /u/. A word ending in an open lax syllable is being analysed
    # differently, and that reaches back to the first syllable.
    # What each syllable is built out of. The coda is the thing the segment
    # key could not see and the thing that matters most: a vowel is short
    # before a voiceless consonant and long before a voiced one or in an
    # open syllable -- /W/ is 114 ms in `bausch' and 237 in `baum', /O/ is
    # 108 in `benoit' and 228 in `annoys'. A diphthong's first half has the
    # diphthong itself as its right neighbour, so the consonant after it is
    # not a neighbour at all and only the syllable says what it is.
    shape = []
    for st, ph in syls:
        nuc = [i for i, p in enumerate(ph) if p in S.VOWELS]
        if nuc:
            shape.append(("".join(ph[:nuc[0]]), "".join(ph[nuc[-1] + 1:])))
        else:
            shape.append(("".join(ph), ""))
    lastv = "."
    lastopen = "1"
    if syls:
        ph = syls[-1][1]
        for p in ph:
            if p in S.VOWELS:
                lastv = p
        if ph and ph[-1] not in S.VOWELS:
            lastopen = "0"
    out = []
    for k, (unit, left, right, stress) in enumerate(got):
        s = where[k]
        out.append({
            "unit": unit, "left": left, "right": right, "stress": stress,
            "syl": s, "nsyl": n, "fromend": n - 1 - s,
            "prev": syls[s - 1][0] if s else ".",
            "next": syls[s + 1][0] if s + 1 < n else ".",
            "lastv": lastv, "lastopen": lastopen,
            "onset": shape[s][0] or ".", "coda": shape[s][1] or ".",
            "coda1": (shape[s][1] or ".")[0],
            # And whether the next syllable begins with a consonant, which
            # decides whether this one is open in effect: /A/ in `ballad' is
            # 142 ms and in `balfour' 78, the difference being that the
            # second syllable of `ballad' has no onset of its own so the /l/
            # between them behaves as its onset rather than as this
            # syllable's coda.
            "nexton": (shape[s + 1][0] or ".") if s + 1 < n else ".",
            "prevcoda": (shape[s - 1][1] or ".") if s else ".",
        })
    return out


# The formants share a segment's piece boundaries. The excitation envelopes
# and the voice-quality parameters have their own, measured, so what this
# table holds is the formants' and the generator says so.
SHAPED = ("f1", "b1", "f2", "b2", "f3", "b3", "f4", "b4", "f5", "b5",
          "fnp", "fnz")


def pieces(run):
    """The lengths each formant's track through a segment is built out of.

    Per parameter, because they do not agree: of 4,149 segments measured,
    2,455 have one span list across all five formants and 1,694 do not, the
    commonest disagreement being (0, 62) against (0, 77) -- one formant
    stopping at the segment's end and another running on past it because it
    has no target there. So a segment has no single answer, and the table
    keeps the majority list with the parameters that differ named beside it.
    """
    out = {}
    for name in SHAPED:
        gs = run[2].get(name, ())
        if gs:
            out[name] = tuple((at, span) for at, span, _, _, _ in gs)
    return out


def harvest(job):
    """One word's segments with their lengths and their prosodic context."""
    word, body = job
    runs = S.tap(PROBE, body)
    got = prosody(body)
    if len(runs) != len(got) + 1:
        return []
    out = []
    for i, f in enumerate(got):
        out.append((f, runs[i][1] - runs[i][0], pieces(runs[i])))
    return out


def setup(probe):
    global PROBE
    PROBE = probe


# Each candidate key, as the fields it is made of. They are cumulative on
# purpose: the question is not which single thing a duration depends on but
# how much has to be named before it stops varying.
KEYS = (
    ("segment", ("unit", "left", "right", "stress")),
    ("and how many syllables", ("unit", "left", "right", "stress", "nsyl")),
    ("and which syllable", ("unit", "left", "right", "stress", "syl",
                            "nsyl")),
    ("and how far from the end", ("unit", "left", "right", "stress",
                                  "fromend")),
    ("both ways round", ("unit", "left", "right", "stress", "syl", "nsyl",
                         "fromend")),
    ("segment and the coda", ("unit", "left", "right", "stress", "coda1")),
    ("and the whole coda", ("unit", "left", "right", "stress", "coda")),
    ("and the syllable entire", ("unit", "left", "right", "stress",
                                 "onset", "coda")),
    ("and where the syllable is", ("unit", "left", "right", "stress",
                                   "onset", "coda", "syl", "nsyl",
                                   "fromend")),
    ("and the stress either side", ("unit", "left", "right", "stress",
                                    "onset", "coda", "syl", "nsyl",
                                    "fromend", "prev", "next")),
    ("and the next syllable's onset", ("unit", "left", "right", "stress",
                                       "onset", "coda", "syl", "nsyl",
                                       "fromend", "prev", "next",
                                       "nexton")),
    ("and the one before's coda", ("unit", "left", "right", "stress",
                                   "onset", "coda", "syl", "nsyl",
                                   "fromend", "prev", "next", "nexton",
                                   "prevcoda")),
)


# What a duration turns out to depend on, measured rather than chosen: the
# segment and its neighbours and stress, the syllable it sits in entire, where
# that syllable is in the word, the stress either side of it, and whether the
# next syllable begins with a consonant. With all of that named, 1,711
# contexts are still seen twice or more and 35 of them disagree, by at most
# fourteen milliseconds; with the segment alone, 524 of 1,998 disagree by up
# to 196.
KEY = ("unit", "left", "right", "stress", "onset", "coda", "syl", "nsyl",
       "fromend", "prev", "next", "nexton")


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    probe = os.path.abspath(argv[1])
    tag = argv[2] if len(argv) > 2 and not argv[2].startswith("-") else "enus"

    def opt(name, dflt):
        return argv[argv.index(name) + 1] if name in argv else dflt

    limit = int(opt("--words", "0"))
    jobs = int(opt("--jobs", str(os.cpu_count() or 4)))
    load = opt("--load", None)
    save = opt("--save", None)

    rows = []
    if load:
        for line in open(load):
            f = line.rstrip("\n").split("\t")
            rows.append((dict(zip(f[:-2:2], f[1:-2:2])), int(f[-2]),
                         {}))
    else:
        words = S.annotations(tag)
        if limit:
            words = words[:limit]
        sys.stderr.write("durations: %d words, %d jobs\n" % (len(words), jobs))
        done = 0
        with ProcessPoolExecutor(jobs, initializer=setup,
                                 initargs=(probe,)) as pool:
            for got in pool.map(harvest, words, chunksize=32):
                done += 1
                if done % 4000 == 0:
                    sys.stderr.write("durations: %d words\n" % done)
                rows.extend(got)
    sys.stderr.write("durations: %d segments\n" % len(rows))
    if save:
        with open(save, "w") as f:
            for fields, dur, ps in rows:
                f.write("\t".join(
                    "%s\t%s" % (k, fields[k]) for k in sorted(fields))
                    + "\t%d\t%s\n" % (dur, ",".join(str(x) for x in ps)))

    out = opt("--out", None)
    if out:
        table = {}
        clash = 0
        for f, dur, ps in rows:
            k = tuple(str(f[x]) for x in KEY)
            common = collections.Counter(ps.values()).most_common(1)
            best = common[0][0] if common else ()
            odd = tuple(sorted((n, v) for n, v in ps.items() if v != best))
            if k in table and table[k] != (dur, best, odd):
                clash += 1
            table.setdefault(k, (dur, best, odd))
        with open(out, "w") as fh:
            fh.write("# How long each segment of %s lasts.\n" % tag)
            fh.write("#\n")
            fh.write("# A duration is not a property of a segment, so the "
                     "key is longer than\n")
            fh.write("# the segment table's: %s.\n"
                     % ", ".join(KEY))
            fh.write("# Then the length in milliseconds and the pieces it "
                     "is built out of,\n")
            fh.write("# which are the spans of its formant breakpoints. A "
                     "line with a\n")
            fh.write("# parameter named after the pieces is that "
                     "parameter's own split,\n")
            fh.write("# which differs for two segments in five: one formant "
                     "stops at the\n")
            fh.write("# segment's end and another runs on past it, having "
                     "no target there.\n")
            fh.write("#\n")
            fh.write("# Written by tools/module/durations.py. See "
                     "docs/authoring.md.\n")
            for k in sorted(table):
                dur, best, odd = table[k]
                fh.write("%s %d %s\n"
                         % (" ".join(k), dur,
                            ",".join("%d:%d" % x for x in best) or "-"))
                for name, spans in odd:
                    fh.write("%s %d %s %s\n"
                             % (" ".join(k), dur,
                                ",".join("%d:%d" % x for x in spans)
                                or "-",
                                name))
        sys.stderr.write("durations: %d contexts written, %d that disagreed\n"
                         % (len(table), clash))

    for name, fields in KEYS:
        seen = collections.defaultdict(lambda: [set(), 0])
        for f, dur, _ in rows:
            e = seen[tuple(str(f[k]) for k in fields)]
            e[0].add(dur)
            e[1] += 1
        # Only contexts seen more than once can disagree, so a key long
        # enough to name nearly every segment on its own scores perfectly by
        # having nothing to compare. The rate among the repeated ones is the
        # honest number, and how many repeat at all says whether the key is
        # a model or a list.
        repeat = [v for v in seen.values() if v[1] > 1]
        many = [v for v in repeat if len(v[0]) > 1]
        spread = sorted(max(v[0]) - min(v[0]) for v in many)
        sys.stderr.write(
            "%-30s %7d contexts, %6d seen twice or more, %5d of those "
            "disagreeing%s\n"
            % (name, len(seen), len(repeat), len(many),
               ", worst %d ms, median %d"
               % (spread[-1], spread[len(spread) // 2]) if many else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
