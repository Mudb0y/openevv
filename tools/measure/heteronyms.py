#!/usr/bin/env python3
"""Which English heteronyms the engine actually gets right, by asking it.

A heteronym is a word spelled one way and said two, and which one is meant is
decided by the grammar around it: the RECord you keep against the reCORD you
make. The engine has machinery for this and uses it -- `read' is RED after
`have' and REED after `will' -- so the question is not whether it can but for
which words it does.

Membership of a dictionary is not that question and answering it that way
misleads: `record' is in neither homograph table and behaves correctly
anyway. So this asks the engine instead. Each word is put in two frames that
force opposite readings, spoken through `eciGeneratePhonemes', and the two
answers compared. A word that says the same thing in both is wrong in one of
them, whatever any table holds; a word that says two things is doing its job.

What this cannot tell you is whether the two are the RIGHT two. That wants a
pronunciation lexicon to check against, and is the next tool along. This one
finds the words that are not even trying, which is the larger pile and the
cheaper one to fix.

usage: tools/measure/heteronyms.py [build/phonemes]
"""

import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

# The frames. The word sits at a known position in each, so its own phonemes
# can be picked out of the line without guessing.
NOUN = ("the %s is here.", 1)
VERB = ("they will %s it.", 2)

# Not every heteronym is a noun against a verb, and a frame that assumes one
# reports a word as broken when it is the frame that is wrong. `wound' is an
# injury or the past of wind, and both of the frames above want the injury;
# `minute' is sixty seconds or very small, and the second is an adjective.
# These carry their own pair, and the number beside each says where the word
# falls in it.
CUSTOM = {
    "wound":  (("the wound is here.", 1), ("she wound the clock.", 1)),
    "minute": (("the minute is here.", 1), ("a minute speck of dust.", 1)),
    "bass":   (("the bass is here.", 1), ("he plays the bass guitar.", 3)),
    "number": (("the number is here.", 1), ("my hands are number now.", 3)),
    "live":   (("they will live here.", 2), ("a live show tonight.", 1)),
    "close":  (("the close of day.", 1), ("stand close to me.", 1)),
    "row":    (("the row is here.", 1), ("they will row it.", 2)),
    "sow":    (("the sow is here.", 1), ("they will sow it.", 2)),
    "tear":   (("the tear is here.", 1), ("they will tear it.", 2)),
    "invalid":(("the invalid is here.", 1), ("an invalid excuse.", 1)),
}

# The two-syllable Latinate pairs are the biggest family in English and the
# most regular: the noun takes the first syllable and the verb the second.
# The rest change a vowel instead.
STRESS = """addict combat compound conduct conflict console content contest
contract contrast convert convict decrease defect desert digest discount
escort excuse export extract ferment implant import impress incline increase
insert insult intern invalid object perfect permit pervert present proceed
produce progress project protest rebel record refund refuse reject relay
subject survey suspect torment transfer transplant transport upset"""

VOWEL = """bass bow close dove lead live minute number read row sow tear wind
wound resume use house abuse"""

ATE = """alternate appropriate approximate associate delegate deliberate
duplicate elaborate estimate graduate initiate intimate moderate predicate
separate"""


def phonemes_of(binary, lines):
    """Speak each line and give back the bracketed phonemes, word by word."""
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f:
        path = f.name
        f.write('\n'.join(lines) + '\n')
    try:
        out = subprocess.run([binary, path], capture_output=True, text=True,
                             timeout=300).stdout
    finally:
        os.unlink(path)
    got = []
    for line in out.splitlines():
        got.append(re.findall(r'\[(\.[^\]]*)\]', line))
    return got


def main(argv):
    binary = argv[0] if argv else os.path.join(ROOT, 'build', 'phonemes')
    if not os.access(binary, os.X_OK):
        print("heteronyms: no %s; `make phonemes' builds it" % binary,
              file=sys.stderr)
        return 2

    groups = [("stress pairs", STRESS), ("vowel changes", VOWEL),
              ("-ate words", ATE)]
    total = same = 0
    report = []

    for name, blob in groups:
        words = sorted(set(blob.split()))
        lines = []
        frames = []
        for w in words:
            a, b = CUSTOM.get(w, (NOUN, VERB))
            frames.append((a, b))
            lines.append(a[0] % w if '%s' in a[0] else a[0])
            lines.append(b[0] % w if '%s' in b[0] else b[0])
        said = phonemes_of(binary, lines)

        flat = []
        for i, w in enumerate(words):
            (fa, ia), (fb, ib) = frames[i]
            n, v = said[2 * i], said[2 * i + 1]
            if len(n) <= ia or len(v) <= ib:
                flat.append((w, None, None))
                continue
            flat.append((w, n[ia], v[ib]))

        bad = [(w, a, b) for w, a, b in flat if a is not None and a == b]
        total += len(flat)
        same += len(bad)
        report.append((name, len(flat), bad))

    for name, n, bad in report:
        print("%s: %d words, %d say the same thing in both frames"
              % (name, n, len(bad)))
    print()
    print("%d of %d heteronyms do not distinguish their two readings."
          % (same, total))
    print()
    for name, n, bad in report:
        if not bad:
            continue
        print("%s -- the same in both:" % name)
        for w, a, _ in bad:
            print("   %-14s %s" % (w, a))
        print()
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
