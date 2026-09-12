#!/usr/bin/env python3
"""The engine's formant tracks as breakpoints, and the frames rebuilt from them.

A formant track is not a curve and not a value some rule wrote down. It is a
list of breakpoints in one of the language's arrays -- a value at an offset in
milliseconds, and a straight line in whole numbers to the next -- and
`sendArrayParameters' turns that into a frame every five milliseconds by
interpolating. `EVV_ARRAY_TAP' writes the list out and `EVV_KLATT_TAP' writes
the frames, so the two together say whether the list is the whole story.

It is. Every parameter of every frame comes back from the breakpoints alone,
with no measurement and no fitting, which means the breakpoint list is the
representation to reproduce and the frames are merely what it unfolds into.

    tools/measure/breaks.py <probe> <text>...

Prints, per utterance, how many frames came back exactly and which parameters
did not.
"""

import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import replay as R                                       # noqa: E402


def tap(probe, text):
    """One utterance's breakpoints and frames, from the two taps at once."""
    with tempfile.TemporaryDirectory() as w:
        c = os.path.join(w, "c.txt")
        with open(c, "w") as f:
            f.write(text + "\n")
        env = dict(os.environ)
        env["EVV_ARRAY_TAP"] = os.path.join(w, "a.txt")
        env["EVV_KLATT_TAP"] = os.path.join(w, "k.txt")
        subprocess.run([probe, "@" + c, os.path.join(w, "c.wav"), "a"],
                       capture_output=True, text=True, env=env)
        breaks, runs = read_breaks(env["EVV_ARRAY_TAP"])
        rows = []
        for line in open(env["EVV_KLATT_TAP"]):
            if line.startswith("#") or line.startswith("step\t"):
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) == len(R.NAMES):
                rows.append([int(x) for x in parts])
        return breaks, runs, rows


def read_breaks(path):
    """Per parameter, the gaps the cursor crossed, and every frame's moment.

    The moment has to come from the tap rather than from the `run' header: a
    bounded run trims its end to a whole step and then starts the next from
    where the last one really got to, so the header's `from' is four
    milliseconds out by the second segment of /atapa/ and every value after
    it reads wrong.
    """
    out = {}
    runs = []          # every frame's own moment, in order
    if not os.path.exists(path):
        return out, runs
    for line in open(path):
        f = line.split()
        if f and f[0] == "frame":
            runs.append(int(f[1]))
        elif f and f[0] == "at":
            # at <at_l> <name> span <span> <val_l>..<val_r>, and which frame
            # was being built when the cursor crossed onto it. That index is
            # what makes the lookup a replay of the cursor rather than a
            # search by time, and a search by time gets it wrong: a stream
            # whose cursor is reset mid-utterance has two gaps covering one
            # moment, and only the order says which was in force.
            lo, hi = f[5].split("..")
            out.setdefault(f[2], []).append(
                (max(0, len(runs) - 1), int(f[1]), int(f[4]),
                 int(lo), int(hi)))
    return out, runs


def value(gaps, i, at):
    """The line the cursor was holding while frame `i', at moment `at', was
    built."""
    best = None
    for frame, at_l, span, val_l, val_r in gaps:
        if frame <= i:
            best = (at_l, span, val_l, val_r)
    if best is None:
        return None
    at_l, span, val_l, val_r = best
    rise = val_r - val_l
    if rise == 0 or span == 0:
        return val_l
    # Whole numbers, truncating toward nought, which is what C does.
    n = (at - at_l) * rise
    return (abs(n) // span) * (1 if n >= 0 else -1) + val_l


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    probe = argv[1]
    bad = 0
    for text in argv[2:]:
        breaks, runs, frames = tap(probe, "`[.1%s]" % text)
        if not frames:
            print("%-12s no frames" % text)
            bad = 1
            continue
        times = runs
        wrong = {}
        n = min(len(times), len(frames))
        for i in range(n):
            for name in R.NAMES:
                if name in ("step", "f0") or name not in breaks:
                    continue
                got = value(breaks[name], i, times[i])
                if got is not None and got != frames[i][R.NAMES.index(name)]:
                    wrong[name] = wrong.get(name, 0) + 1
        print("%-12s %3d frames, %3d timed, %d parameters tracked, %s"
              % (text, len(frames), len(times), len(breaks),
                 "all exact" if not wrong else
                 " ".join("%s:%d" % kv for kv in sorted(wrong.items()))))
        if wrong or len(times) != len(frames):
            bad = 1
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv))
