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
three to twelve long: an apostrophe or a capital is a different question and
the dictionaries answer those separately.

The list is written into the tree rather than read from the store at run
time, because a baseline is worth nothing if the thing it was recorded
against can move underneath it.

usage: tools/measure/wordlist.py [count] [> test/cases/words-enus.txt]
"""

import glob
import os
import re
import sys

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
    print("# Written by tools/measure/wordlist.py. See test/words.sh.")
    for w in picked:
        print(w)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
