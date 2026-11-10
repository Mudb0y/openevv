#!/usr/bin/env python3
"""Give a big dictionary back quickly.

The arena gives a block back by finding the free block in front of it, and
that used to be a walk over every free block below it. A dictionary's entries
are given back in the order of its hash table rather than of their addresses,
so deleting one walked a list its own earlier entries had made longer: the
69,000 entries of the community's US English root file took 23 seconds to
delete on a desktop and minutes on a Raspberry Pi, and a screen reader that
rereads its dictionaries when a file changes was silent for all of it. That
was issue 49.

So this makes a dictionary that size and deletes it, and fails if deleting it
takes longer than three seconds; it takes about a hundredth of one now. The
words are made up rather than read from a file, since the size is the point,
and the same ones every run. A thirty-two bit build has no arena and passes
whatever the arena does. It needs neither Wine nor IBM's objects.

usage: test/lib/bigdict.py <eci.dll or libeci.so>
"""

import ctypes
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import langs  # noqa: E402  the library's two kinds and its prototypes

ENGLISH = 0x10000

#: As many as the community's root file has.
ENTRIES = 69000

#: Far from both what it took and what it takes.
ALLOWED = 3.0


def declare(dll):
    langs.declare(dll)
    v = ctypes.c_void_p
    dll.eciNewDict.restype = v
    dll.eciNewDict.argtypes = [v]
    dll.eciSetDict.argtypes = [v, v]
    dll.eciUpdateDict.argtypes = [v, v, ctypes.c_int, ctypes.c_char_p,
                                  ctypes.c_char_p]
    dll.eciDeleteDict.restype = v
    dll.eciDeleteDict.argtypes = [v, v]


def main(path):
    dll = langs.LOADER(os.path.abspath(path))
    declare(dll)
    if ENGLISH not in langs.languages(dll):
        print("bigdict.py: this library has no US English, which the"
              " dictionary is made in")
        return 0

    h = dll.eciNewEx(ENGLISH)
    if not h:
        raise SystemExit("bigdict.py: no instance")
    d = dll.eciNewDict(h)
    if not d:
        raise SystemExit("bigdict.py: no dictionary")
    pick = random.Random(49)
    letters = "abcdefghijklmnopqrstuvwxyz"
    for n in range(ENTRIES):
        word = "".join(pick.choice(letters)
                       for _ in range(pick.randint(5, 12))) + str(n)
        if dll.eciUpdateDict(h, d, 1, word.encode(), b"`[.1tEst]"):
            raise SystemExit("bigdict.py: the dictionary refused %s" % word)
    if dll.eciSetDict(h, d):
        raise SystemExit("bigdict.py: the dictionary was not taken")

    start = time.monotonic()
    dll.eciSetDict(h, None)
    dll.eciDeleteDict(h, d)
    took = time.monotonic() - start
    dll.eciDelete(h)

    if took > ALLOWED:
        print("bigdict.py: %d entries took %.2f seconds to delete, where %.0f"
              " is allowed" % (ENTRIES, took, ALLOWED))
        return 1
    print("bigdict.py: %d entries deleted in %.2f seconds" % (ENTRIES, took))
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__.strip())
    sys.exit(main(sys.argv[1]))
