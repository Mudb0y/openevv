#!/usr/bin/env python3
"""Square, curly and angle brackets read as round ones.

Every language pauses at a round bracket as at a comma and at the other three
not at all, so the engine reads a pair of any of them that encloses something
as round brackets before the rules see it. This holds it to that by the
strongest test there is: in every language the library has but Japanese, an
aside in each of the three says exactly what it says in round brackets,
sample for sample.

Then what has to be left alone. A lone angle bracket is a comparison and is
still read as one. In a text mode that names punctuation a square bracket is
still a square one, whether the mode was set as a parameter or written into
the text as an annotation, and the default mode written back after it is
followed too. And a phonetic spelling, which is written in square brackets,
is still a phonetic spelling when a pair of brackets comes after it.

It needs neither Wine nor IBM's objects.

usage: test/lib/brackets.py <eci.dll or libeci.so>
"""

import ctypes
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import langs  # noqa: E402  the library's two kinds and its prototypes

ENGLISH = 0x10000
JAPANESE = 0x80000

INPUT_TYPE = 1
TEXT_MODE = 2
SPELL_ALL = 2

FAILED = []


def declare(dll):
    langs.declare(dll)
    dll.eciSetParam.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]


def said(dll, lang, text, **params):
    """What a fresh instance says, so that two texts are compared from the
    same history."""
    v = langs.Voice(dll, lang)
    if params.get("annotated"):
        dll.eciSetParam(v.h, INPUT_TYPE, 1)
    if params.get("mode"):
        dll.eciSetParam(v.h, TEXT_MODE, params["mode"])
    v.speak(text)
    out = bytes(v.said)
    v.close()
    return out


def check(what, ok):
    print("brackets.py: %s %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        FAILED.append(what)


def main(path):
    dll = langs.LOADER(os.path.abspath(path))
    declare(dll)
    have = langs.languages(dll)

    for lang in have:
        if lang == JAPANESE:
            continue
        round_ = said(dll, lang, "Anna (Maria) Anna.")
        for kind in ("[]", "{}", "<>"):
            got = said(dll, lang, "Anna %sMaria%s Anna." % (kind[0], kind[1]))
            check("0x%x: an aside in %s says what it says in ()" % (lang, kind),
                  got == round_ and len(got) > 0)

    if ENGLISH not in have:
        print("brackets.py: no English, which the rest is written in")
    else:
        check("a lone > is still read as a comparison",
              said(dll, ENGLISH, "Anna > Maria.")
              != said(dll, ENGLISH, "Anna Maria."))
        check("spelling every character, [ is still not (",
              said(dll, ENGLISH, "Anna [Maria] Anna.", mode=SPELL_ALL)
              != said(dll, ENGLISH, "Anna (Maria) Anna.", mode=SPELL_ALL))
        check("after `ts2 in the text, [ is still not (",
              said(dll, ENGLISH, "Anna `ts2 [x] `ts0 Anna.", annotated=True)
              != said(dll, ENGLISH, "Anna `ts2 (x) `ts0 Anna.", annotated=True))
        check("after `ts0 in the text, [ is ( again",
              said(dll, ENGLISH, "Anna `ts2 x `ts0 [Maria] Anna.",
                   annotated=True)
              == said(dll, ENGLISH, "Anna `ts2 x `ts0 (Maria) Anna.",
                      annotated=True))
        phonetic = said(dll, ENGLISH, "`[.1hEl.0o] [Maria] Anna.",
                        annotated=True)
        check("a phonetic spelling before a pair is left a phonetic spelling",
              phonetic == said(dll, ENGLISH, "`[.1hEl.0o] (Maria) Anna.",
                               annotated=True)
              and phonetic != said(dll, ENGLISH, "`(.1hEl.0o) (Maria) Anna.",
                                   annotated=True))

    if FAILED:
        print("brackets.py: %d failed" % len(FAILED))
        return 1
    print("brackets.py: all passed")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__.strip())
    sys.exit(main(sys.argv[1]))
