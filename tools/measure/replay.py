#!/usr/bin/env python3
"""Build frames from the measured table, and hold them against the engine's.

The point of measuring the targets is to be able to produce them, and the
point of producing them is that the comparison is exact: same text, same
parameter frames, and the audio is identical because the synthesiser below is
unchanged. No ear, no alignment, no judgement.

So this reads `lang/measured/enus-vowels.txt', builds the frames one held
vowel wants, and compares them against what the engine actually gave
KlattSynth for the same vowel. What it does not model is f0, which is the
intonation and moves over an utterance rather than belonging to the vowel; the
table says so and this excludes it from the comparison rather than pretending.

    tools/measure/replay.py <probe> [vowel]...
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

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def table(path):
    """The shared parameters and the per-vowel ones."""
    shared, cols, rows = {}, [], {}
    for line in open(path):
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        parts = s.split()
        if parts[0] == "shared":
            for kv in parts[1:]:
                k, v = kv.split("=")
                shared[k] = int(v)
        elif parts[0] == "vowel":
            cols = parts[1:]
        else:
            rows[parts[0]] = dict(zip(
                cols, (int(x.rstrip("%")) for x in parts[1:])))
    return shared, cols, rows


def frames_of(probe, text):
    with tempfile.TemporaryDirectory() as work:
        case = os.path.join(work, "case.txt")
        tap = os.path.join(work, "tap.tsv")
        with open(case, "w") as f:
            f.write(text + "\n")
        env = dict(os.environ)
        env["EVV_KLATT_TAP"] = tap
        subprocess.run([probe, "@" + case, os.path.join(work, "case.wav"),
                        "a"], capture_output=True, env=env)
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


# What the utterance does to a vowel, as against what the vowel is.
#
# An isolated vowel does not hold anything flat, and none of what it does is
# the vowel's. The voice quality settles at the start, the voicing declines
# over the vowel and releases at the end, and in running speech the release
# would be a transition into whatever came next. So it is modelled here beside
# f0 rather than put in the table.
#
# The two onset ramps are the same every time and are written down as measured
# rather than fitted: open quotient rises from eighteen to its resting
# fifty-six over five frames, and diplophonia falls from a hundred to nought
# over the same five -- a creaky start, which is a real thing about voices and
# not an artefact.
OQ_ONSET = (18, 27, 36, 45, 54)
DI_ONSET = (100, 77, 53, 29, 5)

# The voicing lets go over the last thirteen frames -- sixty-five
# milliseconds, the same however loud it was -- starting two below its plateau
# and falling to nought in twelve even steps. Rounded up, not to nearest: i
# gives 53 49 45 40 36 31 27 23 18 14 9 5 0, which is ceil and not round.
# Aspiration rises by one from the second of those frames.
AV_RELEASE = 13


def build(shared, vowel, n):
    """The frames a held vowel wants: the table, glided and enveloped."""
    out = []
    # Frames held at the first target before the glide starts. A one per
    # cent hold is nought frames, not one: e sits at 470 for a single frame
    # because that is where the glide begins, not because it holds.
    hold = n * int(vowel.get("hold", 100)) // 100
    av0 = vowel["av"]
    release_at = n - AV_RELEASE
    for i in range(n):
        frame = dict(shared)

        # The formants hold the first target for as long as the table says,
        # then glide to the second.
        if hold >= n:
            for k in ("f1", "f2", "f3"):
                frame[k] = vowel[k + "a"]
        elif i < hold:
            for k in ("f1", "f2", "f3"):
                frame[k] = vowel[k + "a"]
        else:
            # The glide finishes when the release begins and the second
            # target is held through it: e runs 470 to 350 over
            # forty-eight of its sixty-one frames and sits there for the
            # last thirteen.
            span = max(1, n - AV_RELEASE - hold)
            t = min(1.0, (i - hold) / float(span))
            for k in ("f1", "f2", "f3"):
                a, b = vowel[k + "a"], vowel[k + "b"]
                # The step is truncated towards nought, not rounded, which is
                # what C does converting it. So a falling formant appears to
                # round up and a rising one down: 470 less 2.5 is 468 because
                # int(-2.5) is -2, and 1800 plus 4.17 is 1804.
                frame[k] = a + int((b - a) * t)
        frame["b1"] = vowel["b1"]

        # The voice quality settling in.
        if i < len(OQ_ONSET):
            frame["oq"] = OQ_ONSET[i]
        if i < len(DI_ONSET):
            frame["di"] = DI_ONSET[i]

        # Voicing: the table's value, one lower past the midpoint, then let go.
        # Halfway through the glide and one more, which is not halfway
        # through the vowel: the release is thirteen frames and the droop
        # sits at the middle of what is left. i drops at twenty-one of
        # fifty-three, a at twenty-seven of sixty-five, X at eighteen of
        # forty-eight, and the same arithmetic gives all three.
        av = av0 - (1 if i >= (n - AV_RELEASE) // 2 + 1 else 0)
        if i >= release_at:
            top = av0 - 2
            k = i - release_at
            left = AV_RELEASE - 1 - k
            av = -((-top * left) // (AV_RELEASE - 1))   # ceil
            if k > 0:
                frame["ah"] = shared["ah"] + 1
        frame["av"] = max(0, av)
        out.append(frame)
    return out


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    probe = argv[1]
    shared, cols, rows = table(os.path.join(ROOT, "lang", "measured",
                                            "enus-vowels.txt"))
    # The table's own columns, not the shared ones.
    for v in rows:
        rows[v] = {k: rows[v][k] for k in rows[v]}
    want = argv[2:] or list(rows)
    idx = {n: i for i, n in enumerate(NAMES)}

    # f0 is the intonation and is not the vowel's; the table says so.
    check = [n for n in shared if n != "f0"] + ["f1", "f2", "f3", "b1", "av"]

    bad = 0
    for v in want:
        if v not in rows:
            sys.stderr.write("replay: no measured %s\n" % v)
            continue
        got = frames_of(probe, "`[.1%s]" % v)
        live = [r for r in got
                if any(r[idx[s]] >= 20 for s in ("av", "af", "ah"))]
        mine = build(shared, rows[v], len(live))
        wrong = collections.Counter()
        for theirs, ours in zip(live, mine):
            for n in check:
                if theirs[idx[n]] != ours[n]:
                    wrong[n] += 1
        if wrong:
            bad += 1
            print("%-3s %4d frames, differs: %s" % (
                v, len(live),
                " ".join("%s in %d" % (n, c) for n, c in wrong.most_common(6))))
        else:
            print("%-3s %4d frames, every parameter as the engine had it" % (
                v, len(live)))
    print()
    print("%d of %d vowels reproduced exactly, f0 aside"
          % (len(want) - bad, len(want)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
