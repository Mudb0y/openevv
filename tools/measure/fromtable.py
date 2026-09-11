#!/usr/bin/env python3
"""Check a word's formants against the segment table, segment by segment.

`lang/enus/enus.segments' says what each segment's parameters reach, keyed by
the phoneme, the phoneme either side and the stress. It does not say how long
anything takes, because that is the duration model's and is not a property of
a segment -- `docs/authoring.md' has the measurement.

So what can be checked is the values, and it is checked where the table
states them: every run of every word, every parameter, against what the
table says that phoneme in that context at that stress reaches. A word all
of whose segments agree is a word whose formants the table could have
generated, given the durations.

Rebuilding the frames by substitution was tried first and measures nothing
useful: the table collapses a repeated target and drops one that runs
through into the next segment, so putting its values back on the engine's
own gap list mostly misaligns them, and the misalignment is the harness's
rather than the table's.

    tools/measure/fromtable.py <probe> [--words N] [--tag enus]
"""

import collections
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "module"))
sys.path.insert(0, os.path.join(ROOT, "tools", "measure"))

import segments as S                                     # noqa: E402
import replay as R                                       # noqa: E402

# The excitation envelopes go with f0 rather than with the formants. They are
# how hard the utterance is being voiced and how hard it is being blown, and
# the voicing staircase was measured as the utterance's rather than a
# phoneme's long before any of this -- `docs/authoring.md' has it. So they are
# counted separately rather than held against a segment table.
ENVELOPE = ("av", "af", "ah", "tl")


def load(tag):
    """Both tables: the eight parameters a context decides, and the rest.

    `<tag>.phonemes' holds the nineteen a context does not decide, one line
    a phoneme and a stress, and reads as base entries with no overrides.
    """
    base = {}
    over = {}
    paths = [os.path.join(ROOT, "lang", tag, "%s.segments" % tag),
             os.path.join(ROOT, "lang", tag, "%s.phonemes" % tag)]
    for path in paths:
        if os.path.exists(path):
            _read(path, base, over)
    return base, over


def _read(path, base, over):
    for line in open(path):
        if line.startswith("#"):
            continue
        f = line.split()
        # A segments line is phoneme, stress, parameter, `base' or a
        # context, then values; a phonemes line has no context and so is one
        # field shorter.
        if len(f) < 4:
            continue
        unit, stress, name = f[0], f[1], f[2]
        # A trailing `>' says the last stretch has no target of its own; a
        # value written `from:to' starts somewhere other than where the
        # stretch before left off.
        runson = f[-1] == ">"
        vals = f[:-1] if runson else f

        def read(items):
            out = []
            for x in items:
                if ":" in x:
                    a, b = x.split(":")
                    out.append((int(a), int(b)))
                else:
                    out.append((None, int(x)))
            return tuple(out)

        # A phonemes line has no `base' word and no context: phoneme,
        # stress, parameter, values.
        if f[3] == "base":
            base[(unit, stress, name)] = (read(vals[4:]), runson)
        elif len(f) > 5 and not f[3].isdigit() and f[3] != "-":
            # A set of left neighbours and a set of right ones, written as
            # the phonemes run together.
            over.setdefault((unit, stress, name), []).append(
                (set(f[3]), set(f[4]), (read(vals[5:]), runson)))
        else:
            base[(unit, stress, name)] = (read(vals[3:]), runson)


def targets(base, over, unit, left, right, stress, name):
    """What this phoneme's parameter does here: the first rectangle that
    covers the context, or the base."""
    for lefts, rights, value in over.get((unit, stress, name), ()):
        if left in lefts and right in rights:
            return value
    return base.get((unit, stress, name))


def onto(said, want):
    """The table's targets laid on the spans the engine actually used.

    Three things can differ and each is the duration model's doing rather
    than a disagreement. A target repeated is collapsed in the table, so the
    engine's extra breakpoint takes the same value. A segment cut short
    drops the targets it never reached, so the engine has fewer and the last
    of them is where the trajectory stopped, which the table cannot state
    and is taken as given. Otherwise they line up one to one.
    """
    out = []
    for i, (at, span, v0, v) in enumerate(said):
        if i < len(want):
            out.append((at, span, v0, want[i]))
        elif want:
            out.append((at, span, v0, want[-1]))
        else:
            out.append((at, span, v0, v))
    if len(want) > len(said) and said:
        # Cut short: keep the engine's own endpoint for the last one, which
        # is a value on the path and not a target.
        out[-1] = said[-1]
    return out


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    probe = argv[1]

    def opt(name, dflt):
        return argv[argv.index(name) + 1] if name in argv else dflt

    tag = opt("--tag", "enus")
    limit = int(opt("--words", "300"))
    base, over = load(tag)
    S.setup(probe)

    exact = 0
    tried = 0
    skipped = 0
    segs = 0
    badsegs = 0
    shaped = 0
    wrong = collections.Counter()
    missing = collections.Counter()
    for word, body in S.annotations(tag)[:limit]:
        ph = S.marked(body)
        runs = S.tap(probe, body)
        units = S.units(ph)
        if len(runs) != len(units) + 1:
            skipped += 1
            continue
        tried += 1
        bad = set()
        badshape = set()
        for i, (unit, left, right, stress) in enumerate(units):
            for name, gs in runs[i][2].items():
                if name in ("step", "f0"):
                    continue
                segs += 1
                said = []
                for _, _, _, v, ends in gs:
                    if not ends:
                        continue
                    if not said or said[-1] != v:
                        said.append(v)
                said = tuple(said)
                got = targets(base, over, unit, left, right, stress, name)
                want = tuple(v for _, v in got[0]) if got else None
                if want is None:
                    # A segment that sets nothing has no entry, and a run
                    # whose gaps all carry on into the next segment sets
                    # nothing. Those two agree with each other.
                    if said:
                        missing[name] += 1
                        badsegs += 1
                        bad.add(name)
                        if name not in ENVELOPE:
                            badshape.add(name)
                elif want != said and not S.truncates(
                        tuple((None, v) for v in said),
                        tuple((None, v) for v in want), gs[0][2]):
                    wrong[name] += 1
                    badsegs += 1
                    bad.add(name)
                    if name not in ENVELOPE:
                        badshape.add(name)
        if not bad:
            exact += 1
        if not badshape:
            shaped += 1
    print("%d words, %d entirely as the table says, %d with something that "
          "is not, %d whose runs did not line up"
          % (tried + skipped, exact, tried - exact, skipped))
    print("%d words right in every parameter but the excitation envelopes"
          % shaped)
    print("%d segment parameters, %d of them disagreeing" % (segs, badsegs))
    if wrong:
        print("disagreed:  %s"
              % "  ".join("%s %d" % kv for kv in wrong.most_common(10)))
    if missing:
        print("not in the table: %s"
              % "  ".join("%s %d" % kv for kv in missing.most_common(6)))
    return 0


def value(gaps, at):
    """The line in force at `at', in whole numbers truncating toward nought."""
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


if __name__ == "__main__":
    sys.exit(main(sys.argv))
