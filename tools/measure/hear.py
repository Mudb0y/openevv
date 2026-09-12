#!/usr/bin/env python3
"""How different a composed utterance actually sounds, end to end.

Counting wrong parameter values overstates the problem and this says by how
much. A formant thirty-two per cent out in /k/ between /A/ and /u/ sits at a
frame whose voicing is nought -- frication only -- so there is almost no sound
for it to be wrong in, and eleven wrong values in that case come out quieter
than eighty-eight in a nasal. So the measure that decides whether the format
is usable is not the frames but the waveform: render both sides through
test/harness/klattplay.c and compare the samples.

Stas listened to five of these on 9 September 2026 and could not tell the
pairs apart, guessing at the one that turned out to have the smallest
difference of the four. So a few per cent here is inaudible, and that is what
licenses composing from pair tables instead of measuring the full cross
product.

    tools/measure/hear.py <probe> <klattplay> [case]...

With cases named it also writes a self-describing A/B file for each, the
engine speaking which side is which so it can be listened to with nothing on
screen to read alongside.
"""

import math
import os
import struct
import subprocess
import sys
import tempfile
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import compose as K                                      # noqa: E402
import replay as R                                       # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
RATE = 11025
IDX = {n: i for i, n in enumerate(R.NAMES)}

# klattplay renders at about half the engine's gain: the voice's decibel
# offsets and the volume multiplier both reach the synthesiser from a rule at
# run time and cannot be known from outside. It is a constant and it applies
# to both sides, so it cannot make or hide a difference between them; the
# spoken labels are brought down to meet it rather than the other way about,
# which would clip.
LABEL_GAIN = (1, 2)


def render(play, tsv, wav):
    subprocess.run([play, tsv, wav, str(RATE)], capture_output=True)


def samples(path):
    w = wave.open(path)
    n = w.getnframes()
    out = struct.unpack("<%dh" % n, w.readframes(n))
    w.close()
    return out


def rms(s):
    return math.sqrt(sum(float(v) * v for v in s) / max(1, len(s)))


def gain(s, num, den):
    return [max(-32768, min(32767, v * num // den)) for v in s]


def say(probe, text, path):
    with tempfile.TemporaryDirectory() as w:
        c = os.path.join(w, "t.txt")
        with open(c, "w") as f:
            f.write(text + "\n")
        subprocess.run([probe, "@" + c, path, "a"], capture_output=True)


def write_wav(path, s):
    w = wave.open(path, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(struct.pack("<%dh" % len(s), *s))
    w.close()


def ab_file(probe, base, intro):
    """One file that says what it is and then alternates the two sides."""
    work = os.path.dirname(base)
    a = list(samples(base + ".true.wav"))
    b = list(samples(base + ".mine.wav"))
    parts = []
    for text in (intro, "Engine.", "Ours."):
        p = os.path.join(work, "_say.wav")
        say(probe, text, p)
        parts.append(gain(list(samples(p)), *LABEL_GAIN))
    lead, la, lb = parts
    quiet = [0] * (RATE * 120 // 1000)
    mid = [0] * (RATE * 400 // 1000)
    end = [0] * (RATE * 700 // 1000)
    out = lead + [0] * (RATE // 2)
    for _ in range(2):
        out += la + quiet + a + mid + lb + quiet + b + end
    write_wav(base + ".ab.wav", out)
    return len(out) / float(RATE)


def loudest_error(true_tsv, mine_tsv):
    """The worst formant error, and how much sound was there to carry it."""
    def rows(p):
        out = []
        for line in open(p):
            if line.startswith("#") or line.startswith("step\t"):
                continue
            q = line.rstrip("\n").split("\t")
            if len(q) == len(R.NAMES):
                out.append([int(x) for x in q])
        return out
    ta, tb = rows(true_tsv), rows(mine_tsv)
    worst = (0.0, None)
    for i, (fa, fb) in enumerate(zip(ta, tb)):
        for p in ("f1", "f2", "f3", "f4", "f5"):
            t = fa[IDX[p]]
            if not t:
                continue
            e = 100.0 * abs(t - fb[IDX[p]]) / t
            if e > worst[0]:
                worst = (e, (i, p, fa[IDX["av"]], fa[IDX["af"]],
                             fa[IDX["ah"]]))
    return worst


INTRO = {
    "S:uo": "The control. Composed exactly. Nothing should differ.",
}


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    probe, play = argv[1], argv[2]
    named = argv[3:]

    here = os.path.join(ROOT, "lang", "measured")
    work = tempfile.mkdtemp(prefix="hear.")
    args = [os.path.join(here, "enus-pairs.txt"),
            os.path.join(here, "enus-pairs2.txt"),
            os.path.join(here, "enus-holdout.txt"),
            "--write", work] + named
    rc = K.main(["compose"] + args)
    if rc:
        return rc

    ratios = []
    print()
    print("%-9s %8s %8s %7s   %s" % ("case", "signal", "differs", "ratio",
                                     "worst formant error sits at"))
    for f in sorted(os.listdir(work)):
        if not f.endswith(".true.tsv"):
            continue
        tag = f[:-len(".true.tsv")]
        base = os.path.join(work, tag)
        for side in ("true", "mine"):
            render(play, base + "." + side + ".tsv",
                   base + "." + side + ".wav")
        a, b = samples(base + ".true.wav"), samples(base + ".mine.wav")
        d = [x - y for x, y in zip(a, b)]
        ra, rdiff = rms(a), rms(d)
        ratio = 100.0 * rdiff / max(1.0, ra)
        ratios.append(ratio)
        e, w = loudest_error(base + ".true.tsv", base + ".mine.tsv")
        where = "nothing wrong" if w is None else (
            "%s %.0f%% at frame %d, av %d af %d ah %d"
            % (w[1], e, w[0], w[2], w[3], w[4]))
        print("%-9s %8.0f %8.0f %6.1f%%   %s"
              % (tag.replace("-", ":"), ra, rdiff, ratio, where))
        name = tag.replace("-", ":")
        if named and named != ["--all"]:
            secs = ab_file(probe, base,
                           INTRO.get(name, "%s between the two vowels."
                                     % name.split(":")[0]))
            print("            %s.ab.wav, %.1f seconds" % (base, secs))
    if ratios:
        ratios.sort()
        print()
        print("%d cases: median %.1f per cent, worst %.1f, %d under five"
              % (len(ratios), ratios[len(ratios) // 2], ratios[-1],
                 sum(1 for r in ratios if r < 5.0)))
    print()
    print("frames in %s" % work)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
