#!/usr/bin/env python3
"""What crosses from the front of the engine to the back.

The engine answers `eciGeneratePhonemes' with the annotations it would have
acted on -- each word's phonemes and stress inside a pronunciation annotation,
with the pauses it decided on between them.  If that answer, spoken back with
annotations on, drives the synthesiser exactly as the text did, then the
annotation is the whole of the interface between the two halves of the engine
and a front end of ours can be a program that turns text into one.

So: speak the case, ask what it was made of, speak that back, and hold the two
sets of synthesiser frames against each other parameter by parameter.  A
parameter that differs is something the front of the engine knows and the
annotation cannot say.  `src/port/evv_klatttap.c' is what writes the frames and
the audio is byte-identical with it idle.

One repair is needed on the way and it is not a fudge: the report separates
words by nothing at all, and two pronunciation annotations run together are one
token to the engine, which then spells the whole run out.  A space goes back in
front of every annotation.

usage: seam.py <cases.txt> ...
"""

import os
import re
import subprocess
import sys
import tempfile
from collections import Counter

PROBE = os.environ.get("EVV_PROBE", "build/probe")
PHONEMES = os.environ.get("EVV_PHONEMES", "build/phonemes")


def report(work, text):
    """What the engine says the text is made of, ready to be spoken back."""
    where = os.path.join(work, "one.txt")
    with open(where, "w") as f:
        f.write(text + "\n")
    said = subprocess.run([PHONEMES, where], capture_output=True,
                          text=True).stdout.strip()
    if not said.startswith("[") or not said.endswith("]"):
        return None
    return re.sub(r"(?<=[^ ])`", " `", said[1:-1])


def frames(work, text, annotations):
    """Every frame the synthesiser was handed, and the names of the columns."""
    tap = os.path.join(work, "tap")
    subprocess.run([PROBE, text, os.path.join(work, "out.wav")]
                   + (["a"] if annotations else []),
                   capture_output=True,
                   env=dict(os.environ, EVV_KLATT_TAP=tap))
    if not os.path.exists(tap):
        return None, None
    rows = [l.split("\t") for l in open(tap).read().splitlines()]
    os.remove(tap)
    if len(rows) < 2:
        return None, None
    return rows[0], rows[1:]


def main(paths):
    cases = []
    for p in paths:
        for line in open(p):
            line = line.rstrip("\n")
            if line and not line.startswith("#"):
                cases.append(line)

    exact = pitch = other = length = silent = 0
    ever = Counter()
    worst = 0

    with tempfile.TemporaryDirectory() as work:
        for n, text in enumerate(cases, 1):
            said = report(work, text)
            if not said:
                silent += 1
                print("case %d: the engine said nothing" % n)
                continue

            names, spoken = frames(work, text, False)
            _, echoed = frames(work, said, True)
            if spoken is None or echoed is None:
                silent += 1
                print("case %d: no frames" % n)
                continue

            if len(spoken) != len(echoed):
                length += 1
                print("case %d: %d frames against %d -- %s"
                      % (n, len(spoken), len(echoed), text))
                continue

            differ = Counter()
            for a, b in zip(spoken, echoed):
                for j, (u, v) in enumerate(zip(a, b)):
                    if u != v:
                        differ[names[j]] += 1
                        if names[j] == "f0":
                            worst = max(worst, abs(int(u) - int(v)))

            for k in differ:
                ever[k] += 1

            if not differ:
                exact += 1
            elif set(differ) == {"f0"}:
                pitch += 1
                print("case %d: the pitch alone, in %d of %d frames -- %s"
                      % (n, differ["f0"], len(spoken), text))
            else:
                other += 1
                print("case %d: %s -- %s"
                      % (n, ", ".join("%s in %d frames" % (k, v)
                                      for k, v in differ.most_common()), text))

    print()
    print("%d cases: %d frame for frame, %d differ in the pitch alone, "
          "%d differ in some other parameter, %d differ in length, "
          "%d said nothing"
          % (len(cases), exact, pitch, other, length, silent))
    if worst:
        print("the pitch differs by at most %.1f hertz" % (worst / 10.0))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
