#!/usr/bin/env python3
"""Formant frames out of the two tables, with no rule in the path.

`lang/enus/enus.segments' says what each segment's parameters reach and
`lang/enus/enus.durations' says how long each segment lasts and how it splits
into pieces. Between them that is a formant track: the pieces are the spans,
the targets are what each span ends at, and a frame every five milliseconds is
the straight line between them in whole numbers.

So this generates a word's frames from the tables and the phoneme string
alone, and holds them against what the synthesiser was actually told. The
trailing silence is left out of the comparison: it is not a phoneme and the
duration table has no entry for it.

    tools/measure/generate.py <probe> [--words N] [--tag enus] [--show WORD]
    tools/measure/generate.py <probe> --wav <out dir> <word>...

The second writes three wave files a word, all rendered by
`test/harness/klattplay' so the comparison is fair: the engine's own frames,
the frames these tables make, and a third with the tables' spectrum over the
engine's excitation. The excitation envelopes are where the tables are
weakest -- they are how hard the utterance is voiced and blown, and that is
the utterance's business rather than a segment's -- so the third says how
much of any difference is the spectrum and how much is the envelope.

The pitch is borrowed in all cases. It is the phrase's melody and no segment
table can hold it; this engine generates it in a pass of its own.
"""

import collections
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "module"))
sys.path.insert(0, os.path.join(ROOT, "tools", "measure"))

import segments as S                                     # noqa: E402
import durations as D                                    # noqa: E402
import replay as R                                       # noqa: E402
import fromtable as F                                    # noqa: E402

STEP = 5

# Every parameter the map drives, less f0. The pitch is the phrase's melody
# and no segment table can hold it, so a word generated here borrows it and
# nothing else -- this engine generates it in its own pass.
SHAPED = ("av", "oq", "tl", "fl", "di", "ah", "af", "f1", "b1", "f2", "b2",
          "f3", "b3", "f4", "b4", "f5", "b5", "fnp", "fnz", "ftp", "ftz",
          "a1f", "a2f", "a3f", "a4f", "a5f", "ab")


def load_durations(tag):
    """Each segment's length and, per parameter, where its gaps fall.

    A default line gives the split most of the formants share; a line with a
    parameter named after it gives that one's own, which differs for two
    segments in five because a gap can run past the segment's end.
    """
    path = os.path.join(ROOT, "lang", tag, "%s.durations" % tag)
    out = {}
    n = len(D.KEY)

    def split(text):
        if text == "-":
            return ()
        return tuple(tuple(int(x) for x in part.split(":"))
                     for part in text.split(","))

    last = None
    for line in open(path):
        if line.startswith("#"):
            continue
        f = line.split()
        if f and f[0] == "+":
            # A continuation: the parameters sharing one split, for the
            # segment above.
            if last is not None and len(f) >= 3:
                spans = split(f[-1])
                for name in f[1:-1]:
                    out[last][2][name] = spans
            continue
        if len(f) < n + 2:
            continue
        last = tuple(f[:n])
        out[last] = (int(f[n]), split(f[n + 1]), {})
    return out


def defaults(probe, body):
    """What a parameter holds before anything sets it.

    The generator fills a frame from a table of defaults and then overwrites
    whichever streams the language's map names, so these are the language's
    and not the word's. The array tap prints them once a run.
    """
    import re
    import subprocess
    import tempfile
    with tempfile.TemporaryDirectory() as w:
        c = os.path.join(w, "c.txt")
        with open(c, "w") as fh:
            fh.write(body + "\n")
        env = dict(os.environ)
        env["EVV_ARRAY_TAP"] = os.path.join(w, "a.txt")
        subprocess.run([probe, "@" + c, os.path.join(w, "c.wav"), "a"],
                       capture_output=True, env=env)
        out = {}
        for line in open(env["EVV_ARRAY_TAP"]):
            m = re.match(r'map (\S+)\s+stream \S+\s+default (-?\d+)', line)
            if m:
                out.setdefault(m.group(1), int(m.group(2)))
        return out


def lay(pieces, targets, start):
    """One segment's gaps: the pieces as spans, the targets as their ends.

    Three ways they can fail to match one to one, all of them the duration
    model's doing. The table collapses a repeated target, so a segment with
    more pieces than targets holds the last one. A segment cut short has
    more targets than pieces, and the value it stopped at is on the path but
    not in the table, so it is interpolated across what is left. And a
    segment with no target at all sets nothing and its pieces carry the
    value through.
    """
    gaps = []
    at = 0
    v = start
    if not targets:
        return [(0, sum(pieces), start, start)] if pieces else []
    for i, span in enumerate(pieces):
        if i < len(targets):
            end = targets[i]
        elif len(targets):
            end = targets[-1]
        gaps.append((at, span, v, end))
        v = end
        at += span
    if len(targets) > len(pieces) and gaps:
        # Cut short: the last piece heads for the next target it would have
        # reached and stops where the piece runs out. How far it got depends
        # on how long the full stretch would have been, which the table does
        # not hold, so the best available is the target itself.
        at, span, v0, _ = gaps[-1]
        gaps[-1] = (at, span, v0, targets[len(pieces) - 1])
    return gaps


# The voicing is a rule, not a lookup, and this is the rule measured over
# three thousand words. Every vowel has a base: stressed it runs from that
# base down two across the vowel, unstressed or secondary from three below
# the base down four more. The schwas are the exception and drop three.
#
# The composer measured this once before and got it half right, reading the
# two-step form as belonging to the first vowel of an utterance. Its
# carriers were all stressed on the first vowel, so `first' and `stressed'
# could not be told apart; at scale it is the stress.
AV_BASE = {"i": 55, "I": 57, "e": 55, "E": 54, "A": 49, "a": 50,
           "u": 59, "U": 57, "o": 56, "c": 52, "H": 54}
AV_SCHWA = {"x": (52, 50, 51, 48), "X": (53, 51, 50, 47)}


def voicing(vowel, stress, total):
    """One vowel's voicing, as a single stretch, or None for no rule."""
    if vowel in AV_SCHWA:
        a, b, c, d = AV_SCHWA[vowel]
        lo, hi = (a, b) if stress == "1" else (c, d)
    elif vowel in AV_BASE:
        base = AV_BASE[vowel]
        lo, hi = (base, base - 2) if stress == "1" else (base - 3, base - 7)
    else:
        return None
    return ((1, max(0, total - 1)), lo, hi)


# The aspiration is simpler than the voicing: held at 34 through a vowel and
# at nought through a consonant, the latter in every one of the thousands of
# consonant segments measured. A vowel that an /h/ folds into holds 42
# instead, which is the /h/ itself being the vowel's shape excited by noise.
AH_VOWEL = 34
AH_H = 42
AH_CONSONANT = 0

# The tilt is flat nought in 10,883 segments of 12,287 and 35 falling to
# nought in the rest, which are only the voiced stops and the flap. It falls
# within the segment: /F/ holds 35 for its first stretch and nought after.
TL_BURST = set("bdgF")


def aspiration(unit, total):
    """One segment's aspiration, as a single stretch."""
    if unit[-1] not in S.VOWELS:
        return ((0, total), AH_CONSONANT, AH_CONSONANT)
    v = AH_H if unit[0] == "h" and len(unit) > 1 else AH_VOWEL
    return ((0, total), v, v)


# The pitch is not a segment's property at all: it is one contour over the
# whole word, five points, and its values barely move. Measured over twelve
# hundred words -- it starts flat, rises to a peak inside the accented
# vowel, falls twice and holds. The start is the higher value when the first
# syllable carries the accent and the lower when it does not.
F0_HIGH = 1217
F0_LOW = 1044
F0_PEAK = 1293
F0_FALL = 792
F0_END = 742
F0_RISE = 124         # milliseconds of rise, measured median
F0_PEAK_AT = 0.47     # how far through the accented vowel the peak sits
F0_SECOND = 58        # how long the second fall takes, measured median
F0_LAND = 3           # and where it lands, before the word's end


def pitch(last, accent, total):
    """The word's pitch contour: five stretches, as the engine draws them.

    Two anchors, both measured. The peak sits 0.47 of the way through the
    accented vowel -- quartiles 0.41 and 0.63 -- and **the first fall ends
    where the last vowel does**, median six milliseconds past it against
    quartiles of minus 47 and plus 21. Neither a fixed offset from the
    word's end nor a fraction of the word came close to the second: the
    quartiles on those ran 0.69 to 0.89 of the word, which is no anchor at
    all.
    """
    if accent is None:
        return []
    lo, hi = accent
    peak = lo + int((hi - lo) * F0_PEAK_AT)
    rise = max(0, peak - F0_RISE)
    # The second fall has to land, and landing is what makes a word sound
    # finished: clipping it left `hello' ending at 79.2 hertz instead of
    # 74.2, which Stas heard at once as the engine's comma rather than its
    # full stop. It lasts 58 milliseconds -- quartiles 40 and 64, the one
    # tight number in the tail -- and ends three before the word does.
    second = max(peak + 2, total - F0_LAND)
    # The first fall ends where the last vowel does, or 58 milliseconds
    # before the landing, whichever comes first. Either alone is worse:
    # anchoring only to the vowel clips the second fall away on a word that
    # ends in one, and anchoring only to the landing puts the fall late on a
    # word that ends in consonants. Together, 2.0 hertz at the median
    # against 2.7 for the second and an unfinished word for the first.
    end = last[1] if last else total
    fall = max(peak + 1, min(end, second - F0_SECOND))
    start = F0_HIGH if lo <= 0 else F0_LOW
    out = [(0, rise, start, start),
           (rise, peak - rise, start, F0_PEAK),
           (peak, fall - peak, F0_PEAK, F0_FALL),
           (fall, second - fall, F0_FALL, F0_END)]
    if second < total:
        out.append((second, total - second, F0_END, F0_END))
    return [g for g in out if g[1] >= 0]


# The formant family, as against the excitation envelopes and gains.
SPECTRUM = ("f1", "b1", "f2", "b2", "f3", "b3", "f4", "b4", "f5", "b5",
            "fnp", "fnz", "ftp", "ftz")


def write_frames(path, rows):
    """The frames in the shape klattplay reads: a header and 62 numbers."""
    with open(path, "w") as f:
        f.write("\t".join(R.NAMES) + "\n")
        for row in rows:
            f.write("\t".join(str(int(v)) for v in row) + "\n")


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    probe = argv[1]

    def opt(name, dflt):
        return argv[argv.index(name) + 1] if name in argv else dflt

    tag = opt("--tag", "enus")
    limit = int(opt("--words", "200"))
    show = opt("--show", None)
    base, over = F.load(tag)
    durs = load_durations(tag)
    S.setup(probe)

    words = S.annotations(tag)
    if "--body" in argv:
        # A pronunciation given outright, for a language with no word
        # baseline: only English has one.
        words = [(b, b) for b in argv[argv.index("--body") + 1:]]
        show = None
    elif show:
        words = [(w, b) for w, b in words if w == show]
    else:
        words = words[:limit]
    if not words:
        sys.stderr.write("generate: no such word\n")
        return 2
    dflt = defaults(probe, words[0][1])

    wav = None
    if "--wav" in argv:
        wav = argv[argv.index("--wav") + 1]
        names = argv[argv.index("--wav") + 2:]
        allw = dict(S.annotations(tag))
        words = [(w, allw[w]) for w in names if w in allw]
        missing = [w for w in names if w not in allw]
        if missing:
            sys.stderr.write("generate: not in the corpus: %s\n"
                             % " ".join(missing))
        if not words:
            return 2
        os.makedirs(wav, exist_ok=True)
    dflt = defaults(probe, words[0][1])

    exact = 0
    tried = 0
    skipped = 0
    cells = 0
    badcells = 0
    wrong = collections.Counter()
    for word, body in words:
        pros = D.prosody(body)
        frames = R.frames_of(probe, body)
        runs = S.tap(probe, body)
        if not frames or len(runs) != len(pros) + 1:
            skipped += 1
            continue
        # Every segment's pieces and every parameter's targets. Where a
        # segment has more pieces than targets it holds the last one: the
        # table collapses a repeated target, and /A/ at the start of `abbey'
        # is a piece of nought and one of 157 that both end at 1650.
        #
        # Looking ahead instead -- running the spare pieces on to the next
        # segment's target, on the grounds that an unset target places no
        # breakpoint -- was tried and is wrong. It turns /A/'s second piece
        # into a ramp to the /b/'s 1100 where the engine holds 1650, and it
        # took the frame error from 4 per cent to 29. The cases it was meant
        # to fix are key disagreements in the segment table, not a structure
        # the generator can recover.
        plan = []
        ok = True
        for f in pros:
            k = tuple(str(f[x]) for x in D.KEY)
            if k not in durs:
                ok = False
                break
            total, spans, odd = durs[k]
            want = {}
            for name in SHAPED:
                if name not in dflt:
                    continue
                got = F.targets(base, over, f["unit"], f["left"],
                                f["right"], f["stress"], name)
                want[name] = got if got else ((), False)
            plan.append((total, spans, odd, want))
        if not ok:
            skipped += 1
            continue
        tried += 1

        # One flat list a parameter: a span and what it ends at, with None
        # where the segment has no target for that stretch. Then coalesce,
        # because a stretch with no target has no breakpoint either and
        # belongs to the next gap: schwa in `aback' is 62 milliseconds and
        # the /b/ after it 15, and the engine draws one gap of 77 ending at
        # the /b/'s locus rather than two.
        # A gap whose span overshoots its own segment is one the segment has
        # no target for: the engine draws no breakpoint at the segment's end
        # and the line runs on to whatever the next segment wants. The spans
        # say so by themselves -- schwa in `aback' is 62 milliseconds with a
        # second gap of 77, and that gap ends at the /b/'s locus -- so no
        # flag is needed and the one tried first was unstable, being
        # duration-dependent.
        gaps = collections.defaultdict(list)
        v = {}
        at = 0
        f_of = pros
        for j, (total, spans, odd, want) in enumerate(plan):
            for name, value in want.items():
                targets = value[0]
                where = odd.get(name, spans)
                # The rule says what the voiced part of a segment does, not
                # where it starts: /hE/ in `hello' is silent through the /h/
                # and only then runs 51 down to 47, so laying the rule over
                # the whole segment voices the /h/ and made the word 143 per
                # cent different instead of 43. The table already knows
                # where the stretches fall; only their values are replaced,
                # and only for the last one, which is the vowel proper.
                # A rule over the whole segment rather than its voiced tail
                # was tried, so that the aspiration and the tilt could leave
                # the table altogether. It costs 0.2 points of accuracy and
                # the table did not shrink in the end, so the rules stay
                # where they are: on the last stretch only.
                rule = None
                unit = f_of[j]
                if os.environ.get("EVV_EXCITE_RULE") != "0" and where:
                    if name == "av":
                        rule = voicing(unit["unit"][-1], unit["stress"],
                                       total)
                    elif name == "ah":
                        rule = aspiration(unit["unit"], total)
                if name not in v:
                    v[name] = targets[0][1] if targets else dflt.get(name, 0)
                # Where the line runs on to, when this segment has no
                # target for its last stretch -- but only if the next
                # segment's first target continues from here. If that target
                # jumps, nothing runs into it and the stretch holds instead:
                # the flap in `tomato' begins its tilt at 35 out of nowhere
                # a quarter of the way through itself, and running the vowel
                # before it up to that 35 sweeps the tilt across the whole
                # vowel, which is what Stas heard as the word phasing.
                ahead = None
                for later in plan[j + 1:]:
                    nxt = later[3].get(name, ((), False))[0]
                    if nxt:
                        if nxt[0][0] is None:
                            ahead = nxt[0][1]
                        break
                # Where there are more stretches than targets, which of
                # them the spare stretches belong to depends on whether the
                # last target jumps. A jump means a new value starts there,
                # so it belongs at the end and the padding goes in front:
                # /b/'s burst in `banana' is 51 on the third of three
                # stretches, and padding at the back put it on the first,
                # which smeared the burst over the whole closure and turned
                # the stop into a fricative. Without a jump the last target
                # is a hold and the padding goes behind it, which is what
                # /A/ at the start of `abbey' wants.
                shift = 0
                if targets and len(where) > len(targets) \
                        and targets[-1][0] is not None:
                    shift = len(where) - len(targets)
                for i, (rel, span) in enumerate(where):
                    jump = None
                    k = i - shift
                    if 0 <= k < len(targets):
                        jump, end = targets[k]
                    elif targets:
                        end = targets[0][1] if k < 0 else targets[-1][1]
                    else:
                        end = v[name]
                    if rel + span > total and ahead is not None:
                        end = ahead
                    # A stretch that starts somewhere other than where the
                    # last one left off says so, and the voicing does it two
                    # times in five. Chaining regardless is what made a
                    # generated word fluctuate in volume.
                    start = v[name] if jump is None else jump
                    if rule is not None and i == len(where) - 1:
                        _, start, end = rule
                    gaps[name].append((at + rel, span, start, end))
                    v[name] = end
            at += total
        if os.environ.get("EVV_F0_RULE") == "1":
            acc = lastv = None
            t = 0
            for i, f in enumerate(pros):
                d = plan[i][0]
                if f["unit"][-1] in S.VOWELS:
                    lastv = (t, t + d)
                    if f["stress"] == "1" and acc is None:
                        acc = (t, t + d)
                t += d
            gaps["f0"] = pitch(lastv, acc, at)

        if wav:
            import subprocess
            play = os.path.join(ROOT, "build", "klattplay")
            made = {"engine": [], "tables": [], "spectrum": []}
            for i, fr in enumerate(frames):
                t = i * STEP
                if t >= at:
                    break
                made["engine"].append(list(fr))
                a = list(fr)
                b = list(fr)
                for name, gs in gaps.items():
                    got = F.value(gs, t)
                    if got is None:
                        continue
                    a[R.NAMES.index(name)] = got
                    if name in SPECTRUM:
                        b[R.NAMES.index(name)] = got
                made["tables"].append(a)
                made["spectrum"].append(b)
            for kind, rows in made.items():
                tsv = os.path.join(wav, "%s.%s.tsv" % (word, kind))
                out = os.path.join(wav, "%s.%s.wav" % (word, kind))
                write_frames(tsv, rows)
                subprocess.run([play, tsv, out], capture_output=True)
            print("%s: %d frames, three wave files in %s"
                  % (word, len(made["engine"]), wav))
            continue

        bad = set()
        for i, fr in enumerate(frames):
            t = i * STEP
            if t >= at:
                break
            cells += 1
            for name, gs in gaps.items():
                got = F.value(gs, t)
                if got is not None and got != fr[R.NAMES.index(name)]:
                    bad.add(name)
                    badcells += 1
                    wrong[name] += 1
        if not bad:
            exact += 1
        if show:
            print("%s: %s" % (word, "frame for frame" if not bad
                              else "differs in " + " ".join(sorted(bad))))
    print("%d words, %d frame for frame from the tables, %d differing, "
          "%d not covered" % (tried + skipped, exact, tried - exact, skipped))
    print("%d parameter values over those frames, %d wrong (%.3f per cent)"
          % (cells * len(SHAPED), badcells,
             100.0 * badcells / (cells * len(SHAPED)) if cells else 0))
    if wrong:
        print("what differed: %s"
              % "  ".join("%s %d" % kv for kv in wrong.most_common(12)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
