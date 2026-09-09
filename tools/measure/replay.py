#!/usr/bin/env python3
"""Build frames from the measured table, and hold them against the engine's.

The point of measuring a phoneme is to be able to produce it, and the point of
producing it is that the comparison is exact: same text, same parameter
frames, and the audio is identical because the synthesiser below is unchanged.
No ear, no alignment, no judgement.

So this reads every `lang/measured/enus-*.txt' table, builds the frames each
case wants, and compares them against what the engine actually gave KlattSynth
for the same text. What it does not model is f0, which is the intonation and
moves over an utterance rather than belonging to a phoneme; the tables say so
and this excludes it from the comparison rather than pretending.

    tools/measure/replay.py <probe> [name]...
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
    """The shared stills, and each phoneme's stills and tracks."""
    shared, phones = {}, collections.OrderedDict()
    cur = None
    for line in open(path):
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        parts = s.split()
        if parts[0] == "shared":
            for kv in parts[1:]:
                k, v = kv.split("=")
                shared[k] = int(v)
        elif parts[0] == "phone":
            head = dict(kv.split("=", 1) for kv in parts[2:])
            cur = {"name": parts[1], "still": {}, "track": {},
                   "frames": int(head["frames"]), "text": head["text"]}
            phones[parts[1]] = cur
        elif parts[0] == "still":
            cur["still"][parts[1]] = int(parts[2])
        elif parts[0] == "track":
            pts = []
            for tok in parts[2:]:
                at, rest = tok.split(":")
                if "/" in rest:
                    val, den = rest.split("/")
                    pts.append((int(at), int(val), int(den)))
                else:
                    pts.append((int(at), int(rest), 0))
            cur["track"][parts[1]] = pts
    return shared, phones


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


def walk(pts, n):
    """One track's value at every frame, the straight lines walked."""
    out = [None] * n
    for k in range(len(pts) - 1):
        i0, v0, den = pts[k]
        i1, v1, _ = pts[k + 1]
        if not den:
            den = 2 * (i1 - i0)
        d = v1 - v0
        for i in range(i0, min(i1, n - 1) + 1):
            t = min(den, 2 * (i - i0))
            # Truncated towards nought, which is what C does converting it, so
            # a falling parameter appears to round up and a rising one down.
            out[i] = v0 + int(d * t / float(den)) if den else v0
    last_at, last_v, _ = pts[-1]
    for i in range(last_at, n):
        out[i] = last_v
    for i in range(n):
        if out[i] is None:
            out[i] = pts[0][1]
    return out


def build(shared, phone):
    """The frames one phoneme wants."""
    n = phone["frames"]
    base = dict(shared)
    base.update(phone["still"])
    walked = {name: walk(pts, n) for name, pts in phone["track"].items()}
    out = []
    for i in range(n):
        frame = dict(base)
        for name, seq in walked.items():
            frame[name] = seq[i]
        out.append(frame)
    return out


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    probe = argv[1]
    here = os.path.join(ROOT, "lang", "measured")
    tables = [("vowels", "enus-vowels.txt"),
              ("consonants", "enus-consonants.txt"),
              ("pairs", "enus-pairs.txt"),
              ("pairs2", "enus-pairs2.txt"),
              ("holdout", "enus-holdout.txt")]
    idx = {n: i for i, n in enumerate(NAMES)}
    total = good = 0
    for what, fname in tables:
        path = os.path.join(here, fname)
        if not os.path.exists(path):
            continue
        shared, phones = table(path)
        want = [a for a in argv[2:] if a in phones] or (
            list(phones) if not argv[2:] else [])
        if not want:
            continue
        print("--- %s" % what)
        # With hundreds of cases a line each is noise, so only the ones that
        # differ are named and the rest are counted.
        n, b = run(probe, shared, phones, want, idx, quiet=len(want) > 50)
        total += n
        good += n - b
    print()
    print("%d of %d cases reproduced exactly, f0 aside" % (good, total))
    return 0


def run(probe, shared, phones, want, idx, quiet=False):
    bad = 0
    for v in want:
        got = frames_of(probe, phones[v]["text"])
        live = [r for r in got
                if any(r[idx[s]] >= 20 for s in ("av", "af", "ah"))]
        mine = build(shared, phones[v])
        wrong = collections.Counter()
        # f0 is the intonation and is not the phoneme's; the table says so.
        check = [n for n in mine[0] if n not in ("step", "f0")] if mine else []
        for theirs, ours in zip(live, mine):
            for n in check:
                if theirs[idx[n]] != ours[n]:
                    wrong[n] += 1
        if len(live) != len(mine):
            wrong["frames"] += 1
        if wrong:
            bad += 1
            print("%-3s %4d frames, differs: %s" % (
                v, len(live),
                " ".join("%s in %d" % (n, c) for n, c in wrong.most_common(6))))
        elif not quiet:
            print("%-3s %4d frames, every parameter as the engine had it" % (
                v, len(live)))
    if quiet:
        print("    %d cases, %d as the engine had them" % (len(want),
                                                           len(want) - bad))
    return len(want), bad


if __name__ == "__main__":
    sys.exit(main(sys.argv))
