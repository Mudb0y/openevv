#!/usr/bin/env python3
"""An instance still speaks after a run the engine gave up on.

The guards in the machine -- a walk that has gone round, a node of nought, a
stack run out, the checks that force an error backtrack -- give an utterance
up rather than let the process fault or hang. Until 11 November 2026 the
instance it happened on said nothing ever again: the error stayed thrown,
eciReset did not reach it, and nothing told the caller, so a screen reader
was left running with a voice that had gone quiet. The engine now restarts
the machine after a run it gave up on, and this holds it to that.

In each English the library has, the first forty strings of
test/cases/crashers.txt are spoken one at a time on one instance, each
between two sentences, and the sentence after each has to be said in full:
as long as it is on a fresh instance, to within a tenth. Not sample for
sample, since an instance's second utterance is not its first even when
nothing was given up. At least one of the forty has to be given up, or the
check has proved nothing. The strings are named by line and never printed:
they are text that kills IBM's engine, and a screen reader reads this.

Then the run of digits that overran the stack, a hundred and thirty-one
digits and more in both Englishes until the stack was given 6.1's size: three
hundred has to be read rather than given up, and the sentence after it said.

It needs neither Wine nor IBM's objects.

usage: test/lib/giveup.py <eci.dll or libeci.so>
"""

import ctypes
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import langs  # noqa: E402  the library's two kinds and its prototypes

ENGLISHES = (0x10000, 0x10001)
SENTENCE = b"Hello there, how are you?"
STRINGS = 40

FAILED = []


def speak(v, text):
    """What one utterance said, on an instance that may have spoken before."""
    v.said.clear()
    if not v.dll.eciAddText(v.h, text):
        raise SystemExit("giveup.py: 0x%x refused the text" % v.lang)
    if not v.dll.eciSynthesize(v.h):
        raise SystemExit("giveup.py: 0x%x refused to speak" % v.lang)
    for _ in range(6000):
        if not v.dll.eciSpeaking(v.h):
            break
        time.sleep(0.01)
    return bytes(v.said)


def check(what, ok):
    print("giveup.py: %s %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        FAILED.append(what)


def main(path):
    dll = langs.LOADER(os.path.abspath(path))
    langs.declare(dll)
    have = langs.languages(dll)
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "cases", "crashers.txt"), "rb") as f:
        strings = [line.rstrip(b"\r\n") for line in f][:STRINGS]

    englishes = [lang for lang in ENGLISHES if lang in have]
    if not englishes:
        print("giveup.py: no English, which the strings were found in")
        return 1

    for lang in englishes:
        fresh = langs.Voice(dll, lang)
        alone = speak(fresh, SENTENCE)
        fresh.close()

        v = langs.Voice(dll, lang)
        speak(v, SENTENCE)
        given_up = 0
        silent = []
        for n, text in enumerate(strings, 1):
            if not speak(v, text):
                given_up += 1
            after = len(speak(v, SENTENCE))
            if abs(after - len(alone)) * 10 > len(alone):
                silent.append(n)
        v.close()
        check("0x%x: a sentence after each of %d strings is said in full%s"
              % (lang, len(strings),
                           "" if not silent else
                           ", but not after lines %s" % silent[:8]),
              not silent)
        check("0x%x: %d of the %d were given up, so the check reached a "
              "given-up run" % (lang, given_up, len(strings)), given_up > 0)

        v = langs.Voice(dll, lang)
        digits = speak(v, b"7" * 300)
        after = speak(v, SENTENCE)
        v.close()
        check("0x%x: three hundred digits are read" % lang, len(digits) > 0)
        check("0x%x: and the sentence after them is said in full" % lang,
              abs(len(after) - len(alone)) * 10 <= len(alone))

    if FAILED:
        print("giveup.py: %d failed" % len(FAILED))
        return 1
    print("giveup.py: all passed")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__.strip())
    sys.exit(main(sys.argv[1]))
