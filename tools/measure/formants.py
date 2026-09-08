#!/usr/bin/env python3
"""What the synthesiser was told, as a corpus.

`KlattSynth' takes one frame of sixty-two parameters and answers a run of
samples, and it is the only way anything reaches the formant engine. So the
tap in src/port/evv_klatttap.c writes down every value this engine ever asks
for, and a run over a corpus is IBM's parameter data observed at the point of
use -- not a table read out of a module, and not a measurement by ear.

What comes out is a reference corpus: a text, and the frames it produced. It
is not a table of formant targets per phoneme, and that is deliberate. A front
end of our own has to produce these frames; comparing frames against frames
for the same text is an exact test that needs no phoneme labels and no
alignment, and it is a far sharper standard than an ear. A per-phoneme table
is something to derive from this corpus afterwards, by whatever segmentation
turns out to suit -- and having the corpus means more than one can be tried.

Durations per phoneme are not here because no path answers them cleanly: the
rules only fill the proportion when a caller has asked for phoneme
indices, and the run that asks redirects the audio away, so the frames and the
durations cannot come from one utterance. The phoneme string is available
separately from `build/phonemes'.

    tools/measure/formants.py <probe> <corpus file> [out.tsv]

The corpus file is one text a line. `test/cases/plain-enus.txt' and its
fellows are corpora; so is `test/words.txt' if a much longer run is wanted.
"""

import os
import subprocess
import sys
import tempfile

# In frame order, as KlattSynth reads them. The first is the step in
# milliseconds; src/klatt/klatt_run.c has the same list as parmNames, less
# the trailing duration slot which is not a parameter.
NAMES = (
    "step f0 av oq tl fl di ah af f1 b1 df1 db1 f2 b2 f3 b3 f4 b4 f5 b5 "
    "f6 b6 f7 b7 f8 b8 fnp bnp fnz bnz ftp btp ftz btz "
    "a1f a2f a3f a4f a5f a6f a7f a8f ab b1f b2f b3f b4f b5f b6f b7f b8f "
    "anv a1v a2v a3v a4v a5v a6v a7v a8v atv"
).split()


def frames_of(probe, text):
    """Every parameter frame one text gave the synthesiser, in order."""
    with tempfile.TemporaryDirectory() as work:
        case = os.path.join(work, "case.txt")
        tap = os.path.join(work, "tap.tsv")
        with open(case, "w") as f:
            f.write(text.rstrip("\n") + "\n")
        env = dict(os.environ)
        env["EVV_KLATT_TAP"] = tap
        subprocess.run([probe, "@" + case, os.path.join(work, "case.wav")],
                       capture_output=True, env=env)
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


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    probe, path = argv[1], argv[2]
    where = argv[3] if len(argv) > 3 else None

    texts = [l.rstrip("\n") for l in open(path, errors="replace")
             if l.strip()]

    out = open(where, "w") if where else sys.stdout
    out.write("line\tframe\t" + "\t".join(NAMES) + "\n")
    total = 0
    for n, text in enumerate(texts, 1):
        rows = frames_of(probe, text)
        for i, row in enumerate(rows):
            out.write("%d\t%d\t%s\n"
                      % (n, i, "\t".join(str(v) for v in row)))
        total += len(rows)
        sys.stderr.write("formants: line %d of %d, %d frames\r"
                         % (n, len(texts), total))
        sys.stderr.flush()
    if where:
        out.close()
    sys.stderr.write("\nformants: %d frames over %d lines\n"
                     % (total, len(texts)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
