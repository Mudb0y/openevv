#!/usr/bin/env python3
"""Read ñ in English as n and y.

Both Englishes gave an accented letter they have no rule for the plain
letter it is written on before their letter rules saw it, so ñ was n:
El Niño was nee-no and España es-pah-nuh. ETI Eloquence 6.1 says the ny of
the languages ñ comes from, and since 3 November 2026 ours does too, by
writing the two letters n and y where the ñ was (issue 50).

Every dictionary is asked after that, so each had filed its ñ words where
the n put them: IBM's doña and señor were dona and senor, and US English's
carried dictionary held jalapeño both ways and could only ever find the
plain one. So the carried dictionary files a word written with ñ under n
and y, and the words IBM and the community wrote only under n are written
under n and y as well.

So this holds three things, in both Englishes where a word is in both. A word
no dictionary holds has the phone n followed by the phone y. A word with an
entry under n reads with ñ exactly as it reads without, so that nothing a
dictionary said was lost. And an entry the carried dictionary wrote with ñ,
which could never be found before, is found. The word lists cannot ask any
of this, being plain ASCII, and the recorded cases hand the engine a case's
bytes as they are in the file. It needs neither Wine nor IBM's objects.

usage: test/lib/enye.py <eci.dll or libeci.so>
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import builtin  # noqa: E402  an instance that answers what a text is read as
import langs  # noqa: E402

ENGLISH = 0x10000
BRITISH = 0x10001
ENGLISHES = {ENGLISH: "US English", BRITISH: "British English"}

#: Words no dictionary holds.
UNLISTED = ("España", "Peña", "cañon")

#: Each word with ñ, the plain spelling an entry holds it under, and the
#: Englishes that hold it.
TWINS = (
    ("señor", "senor", (ENGLISH, BRITISH)),
    ("señora", "senora", (ENGLISH, BRITISH)),
    ("señorita", "senorita", (ENGLISH, BRITISH)),
    ("doña", "dona", (ENGLISH,)),
    ("Muñoz", "Munoz", (ENGLISH,)),
    ("jalapeño", "jalapeno", (ENGLISH,)),
    ("piñata", "pinata", (ENGLISH,)),
    ("mañana", "manana", (ENGLISH,)),
)

#: A word the carried dictionary writes with ñ, and what its entry says.
CARRIED = ("niño", b"`[.1ni.0nyo]")

FAILED = []


def said(dll, text, lang):
    return builtin.read(dll, text.encode("cp1252"), lang=lang).strip()


def fail(why):
    print("enye.py: " + why)
    FAILED.append(why)


def main(path):
    dll = langs.LOADER(os.path.abspath(path))
    builtin.declare(dll)
    have = langs.languages(dll)
    for lang, name in ENGLISHES.items():
        if lang not in have:
            print("enye.py: this library has no %s" % name)
            continue
        for word in UNLISTED:
            phones = said(dll, word, lang).decode("latin-1")
            if not any(".%dny" % s in phones for s in (0, 1, 2)):
                fail("%s reads %s as %s, with no n and y"
                     % (name, word, phones))
        for word, plain, where in TWINS:
            if lang not in where:
                continue
            got, want = said(dll, word, lang), said(dll, plain, lang)
            if got != want:
                fail("%s reads %s as %s and %s as %s"
                     % (name, word, got.decode("latin-1"), plain,
                        want.decode("latin-1")))
    if ENGLISH in have:
        word, entry = CARRIED
        got = said(dll, word, ENGLISH)
        if got != entry:
            fail("US English reads %s as %s, not as its entry %s"
                 % (word, got.decode("latin-1"), entry.decode("latin-1")))
    if not FAILED:
        print("enye.py: ñ is n and y in both Englishes, and every dictionary"
              " finds its ñ words")
    return 1 if FAILED else 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__.strip())
    sys.exit(main(sys.argv[1]))
