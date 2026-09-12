#!/usr/bin/env python3
"""Every phoneme's parameter targets, measured rather than read.

What a phoneme sounds like is a rule in the language module -- `<x>_ph_<name>`
setting the source parameters and calling a locus rule for the place it is
articulated at, where the formant targets live. Reading those rules is
archaeology. Speaking the phoneme and writing down what the synthesiser was
told is not, and it answers the same question exactly: the tap in
src/port/evv_klatttap.c sees every frame `KlattSynth' is given, and one
phoneme spoken on its own gives that phoneme's frames.

So each phoneme goes in as a pronunciation annotation -- `` `[.1X] ``, which
the engine reads in its own alphabet -- and what comes out is the value each
of the twenty-five driven parameters held while the phoneme was sounding.
Voiced ones are found by `av', frication by `af', aspiration by `ah'; a
phoneme that never raises any of the three makes no sound on its own and says
so rather than being guessed at.

The steady value is the commonest one, not the mean. A target is a plateau the
transitions run between, so the mode is the target and an average would be the
target pulled towards whatever it was moving from.

    tools/measure/phonetargets.py <probe> <phoneme>... [-o out.tsv]

With no phonemes it measures the inventory `tools/module/phonemes.py' knows,
which wants EVVLANG or a tag.
"""

import collections
import os
import subprocess
import sys
import tempfile

NAMES = (
    "step f0 av oq tl fl di ah af f1 b1 df1 db1 f2 b2 f3 b3 f4 b4 f5 b5 "
    "f6 b6 f7 b7 f8 b8 fnp bnp fnz bnz ftp btp ftz btz "
    "a1f a2f a3f a4f a5f a6f a7f a8f ab b1f b2f b3f b4f b5f b6f b7f b8f "
    "anv a1v a2v a3v a4v a5v a6v a7v a8v atv"
).split()

# The twenty-five English drives, in frame order. Everything else never moves
# over a corpus, so a target for it would be the constant it always is.
DRIVEN = ("f0 av oq tl di ah af f1 b1 f2 b2 f3 b3 f4 b4 f5 b5 fnz "
          "a1f a2f a3f a4f a5f ab").split()

# Sounding at all: one of these above a threshold means the frame is the
# phoneme rather than the silence around it.
SOUNDING = ("av", "af", "ah")
FLOOR = 20


def frames_of(probe, text, mode="a"):
    """The parameter frames one text gave the synthesiser."""
    with tempfile.TemporaryDirectory() as work:
        case = os.path.join(work, "case.txt")
        tap = os.path.join(work, "tap.tsv")
        with open(case, "w") as f:
            f.write(text + "\n")
        env = dict(os.environ)
        env["EVV_KLATT_TAP"] = tap
        subprocess.run([probe, "@" + case, os.path.join(work, "case.wav"),
                        mode], capture_output=True, env=env)
        rows = []
        if not os.path.exists(tap):
            return rows
        for line in open(tap):
            if line.startswith("#") or line.startswith("step\t"):
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) == len(NAMES):
                rows.append([int(x) for x in parts])
        return rows


def envelope(probe, phoneme):
    """How the voicing of one phoneme rises, holds and lets go.

    The shape is always a plateau, a second run one lower, and a release ramp,
    and what differs is three numbers: where the droop falls, how many frames
    the release takes, and what it does over them. Twelve of the sixteen
    vowels release from two below the plateau to nought as
    `top - int(top * k / (frames - 1))', truncated; `I', `E', `U' and `H' do
    not, and theirs is written down as measured because the arithmetic behind
    it has not been found.
    """
    rows = frames_of(probe, "`[.1%s]" % phoneme)
    idx = {n: i for i, n in enumerate(NAMES)}
    live = [r[idx["av"]] for r in rows
            if any(r[idx[s]] >= FLOOR for s in SOUNDING)]
    if not live:
        return None
    runs = []
    for v in live:
        if runs and runs[-1][0] == v:
            runs[-1][1] += 1
        else:
            runs.append([v, 1])
    if len(runs) < 3:
        return None
    av0 = runs[0][0]
    hold = runs[0][1]
    release = live[hold + runs[1][1]:]
    top = release[0]
    n = len(release)
    fits = (top == av0 - 2 and release[-1] == 0
            and all(release[k] == top - int(top * k / float(n - 1))
                    for k in range(n)))
    return {"n": len(live), "hold": hold, "relfr": n,
            "fits": fits, "release": release}


def targets(probe, phoneme):
    """What one phoneme held while it was sounding, parameter by parameter."""
    rows = frames_of(probe, "`[.1%s]" % phoneme)
    if not rows:
        return None, 0
    idx = {n: i for i, n in enumerate(NAMES)}
    live = [r for r in rows
            if any(r[idx[s]] >= FLOOR for s in SOUNDING)]
    if not live:
        return None, 0
    out = {}
    for n in DRIVEN:
        counts = collections.Counter(r[idx[n]] for r in live)
        out[n] = counts.most_common(1)[0][0]
    return out, len(live)


def inventory(tag):
    """The phonemes the module declares, from the statements it was lifted with."""
    root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    p = os.path.join(root, "lang", tag, "%s.statements" % tag)
    names = []
    if not os.path.exists(p):
        return names
    want = False
    for line in open(p):
        s = line.strip()
        if s.startswith("phone "):
            want = True
            continue
        if want:
            if not s or s.startswith("statement") or " " in s.split(":")[0]:
                if s.startswith("values"):
                    continue
            if s.startswith("values"):
                names = s.split(None, 1)[1].split()
                break
    return names


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    probe = argv[1]
    rest = argv[2:]
    where = None
    if "-o" in rest:
        i = rest.index("-o")
        where = rest[i + 1]
        rest = rest[:i] + rest[i + 2:]

    phons = rest or inventory(os.environ.get("EVVLANG", "enus").split("/")[-1])
    if not phons:
        sys.stderr.write("phonetargets: name the phonemes to measure\n")
        return 2

    out = open(where, "w") if where else sys.stdout
    out.write("phoneme\tframes\t" + "\t".join(DRIVEN) + "\n")
    silent = []
    for p in phons:
        got, n = targets(probe, p)
        if got is None:
            silent.append(p)
            continue
        out.write("%s\t%d\t%s\n"
                  % (p, n, "\t".join(str(got[k]) for k in DRIVEN)))
    if where:
        out.close()
    sys.stderr.write("phonetargets: %d measured, %d made no sound alone%s\n"
                     % (len(phons) - len(silent), len(silent),
                        (": " + " ".join(silent)) if silent else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
