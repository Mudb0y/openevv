#!/usr/bin/env python3
"""The word list test/words.sh holds the engine to, written out of SCOWL.

The sentence gate has 98 English cases in it. That is the right size for
asking whether the engine still says what it said, and far too small for
asking whether a rule change was a good idea: a change that mends forty words
and breaks four hundred passes it without a murmur. So there is a second gate
a level down, one word to a line, and this writes the list it reads.

SCOWL's `wamerican.35' is the common half of English -- fifty thousand words
before the long tail of proper nouns and technical terms -- and a stride
through it sorted gives an even spread across the alphabet rather than
twenty thousand words beginning with A. Words are lowercased, letters only,
three to twelve long: an apostrophe or a capital is a different question.

And then every word the language's own dictionaries hold, which the sample
mostly does not: 4,317 of the 5,042 were missing on 6 September 2026. Those
are exactly the words a change to a dictionary can move, so a gate that
cannot see them cannot answer the question people will most often be asking
it. Leaving them out cost a false pass the same day -- `slugabed' was edited,
the gate said every one as it was, and it was saying nothing at all.

The list is written into the tree rather than read from the store at run
time, because a baseline is worth nothing if the thing it was recorded
against can move underneath it.

usage: tools/measure/wordlist.py [count] [> test/cases/words-enus.txt]
"""

import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WANT = 20000
SCOWL = '/nix/store/*-scowl-*/share/dict/wamerican.35'


def main(argv):
    want = int(argv[0]) if argv else WANT
    found = sorted(glob.glob(SCOWL))
    if not found:
        print("wordlist: no scowl in the store; nix shell nixpkgs#scowl",
              file=sys.stderr)
        return 2

    words = set()
    for line in open(found[0], encoding='utf-8', errors='replace'):
        w = line.strip().lower()
        if re.fullmatch(r'[a-z]{3,12}', w):
            words.add(w)
    words = sorted(words)
    if not words:
        return 2

    # Evenly spaced by fraction rather than by whole steps: asking for twenty
    # thousand of thirty-eight thousand with an integer stride gives a stride
    # of one, which is the first twenty thousand and stops at M.
    if want >= len(words):
        picked = words
    else:
        picked = [words[(i * len(words)) // want] for i in range(want)]
    print("# %d words of English, spread evenly through the %d in scowl's"
          % (len(picked), len(words)))
    print("# wamerican.35, which is the common half of the language.")
    # Every word the dictionaries hold, on top of the sample.
    held = set()
    dictf = os.path.join(os.path.dirname(os.path.dirname(HERE)),
                         'lang', 'enus', 'enus.dict')
    if os.path.exists(dictf):
        for line in open(dictf, encoding='utf-8', errors='replace'):
            m = re.match(r'^  (\S+) ', line)
            if m and re.fullmatch(r'[a-z]{1,20}', m.group(1)):
                held.add(m.group(1))

    both = sorted(set(picked) | held)
    print("# with every word the dictionaries hold folded in: %d of them,"
          % len(held))
    print("# which are the words a change to a dictionary can move.")
    print("# Written by tools/measure/wordlist.py. See test/words.sh.")
    for w in both:
        print(w)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
