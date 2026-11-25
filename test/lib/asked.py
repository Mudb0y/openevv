#!/usr/bin/env python3
"""Settings asked for through the interface, held to the same settings
asked for in the text.

Phrase prediction, the abbreviation dictionary, the text mode and the number
mode can each be asked for in the text with an annotation -- `pp1, `da1,
`ts1 and `ts2, `ty0 -- and through the interface with eciSetDefaultParam,
for every instance made afterwards, and eciSetParam, for one. The dictionary
is asked for with nought, the parameter being IBM's and inverted. eciSetParam
refuses eleven as IBM's did, so phrase prediction has no per-instance call,
and this holds that refusal too.

So in every language the library has, each sentence is spoken on a new
instance with the annotation in its text, and that length is what each way
through the interface has to give: on a new instance, and again after an
utterance has been stopped at its first buffer. The second is the one that
failed. A stop puts a language lifted from 6.1 back through the line an
engine starts with, `v1 `ts0 `da0 `ty1 `pp0, and the interface then sends
only what it takes to differ from that line. Until 24 November 2026 it took
the line to be g_DefaultEnvironment, which is exactly what
eciSetDefaultParam moves, so a setting asked for that way read as already
sent and was never sent again: Brazilian Portuguese and Finnish lost every
one of these at their first stop. eciSetParam was never affected, and the
languages of 4.3's interface, which are told everything after a stop, were
not either.

Lengths and not bytes, for the reason test/lib/afterstop.py gives. Each
sentence is also spoken without the annotation, and where a sentence is
written for a language it has to come out a different length there, or the
sentence never reached the setting and the check says nothing. Elsewhere the
sentence is spoken all the same and a length that does not move is only
reported. Japanese is passed over, as in afterstop.py.

It needs neither Wine nor IBM's objects.

usage: test/lib/asked.py <eci.dll or libeci.so>
"""

import ctypes
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import afterstop  # noqa: E402  an instance that can be stopped mid-utterance
import langs  # noqa: E402

JAPANESE = 0x80000
INPUT_TYPE, TEXT_MODE, DICTIONARY = 1, 2, 3
NUMBER_MODE, PHRASE_PREDICTION = 10, 11

ENUS, PTBR, FIFI = 0x10000, 0x70000, 0x90000

#: A sentence with punctuation, numbers and symbols, which every text mode
#: but the third reads otherwise in each of the three languages.
MARKS = {
    ENUS: "Call 555-1234 at 3:45; it's $12.50, ok? Yes!! 1997 #7",
    PTBR: "Ligue 3456-7890 às 3:45; custa R$ 12,50, certo? Sim!! 1997 #7",
    FIFI: "Soita 3456-7890 klo 3.45; se maksaa 12,50 euroa, eikö?"
          " Kyllä!! 1997 #7",
}

#: Each setting: its name, the annotation that asks for it in the text, the
#: calls that ask for it through the interface, and a sentence in each
#: language it is known to move. A language with no sentence of its own is
#: given US English's, and a length that does not move there is reported
#: rather than failed; Portuguese has none for the number mode, which reads
#: its years alike either way. Finnish's phrase prediction sentence is the
#: one test/cases/eti-fifi.txt leaves out because phrase prediction decides
#: it.
SETTINGS = (
    ("phrase prediction", "`pp1 ",
     (("eciSetDefaultParam(11, 1)", "default", PHRASE_PREDICTION, 1),),
     {ENUS: "This is a test sentence that measures how fast the voice"
            " speaks when there is no punctuation in it at all",
      PTBR: "Esta é uma frase de teste para medir a velocidade da voz.",
      FIFI: "Hän tuli 1. sijalle ja minä 2. sijalle."}),
    ("the dictionary", "`da1 ",
     (("eciSetDefaultParam(eciDictionary, 0)", "default", DICTIONARY, 0),
      ("eciSetParam(h, eciDictionary, 0)", "instance", DICTIONARY, 0)),
     {ENUS: "Dr. Smith took 5 mg today on St. John St.",
      PTBR: "O Dr. Silva tomou 5 mg hoje.",
      FIFI: "Tri Virtanen otti 5 mg tänään, ks. myös esim. klo 5."}),
    ("text mode 1", "`ts1 ",
     (("eciSetDefaultParam(eciTextMode, 1)", "default", TEXT_MODE, 1),
      ("eciSetParam(h, eciTextMode, 1)", "instance", TEXT_MODE, 1)),
     MARKS),
    ("text mode 2", "`ts2 ",
     (("eciSetDefaultParam(eciTextMode, 2)", "default", TEXT_MODE, 2),
      ("eciSetParam(h, eciTextMode, 2)", "instance", TEXT_MODE, 2)),
     MARKS),
    ("number mode 0", "`ty0 ",
     (("eciSetDefaultParam(eciNumberMode, 0)", "default", NUMBER_MODE, 0),
      ("eciSetParam(h, eciNumberMode, 0)", "instance", NUMBER_MODE, 0)),
     {ENUS: "In 1997 and 1850 and 2001, 1492 people.",
      FIFI: "Vuonna 1997 ja 1850 ja 2001, 1492 ihmista."}),
)

FAILED = []


def check(what, ok):
    print("%s: %s" % ("ok" if ok else "FAILED", what), flush=True)
    if not ok:
        FAILED.append(what)


def annotated(dll, lang, text):
    v = afterstop.Voice(dll, lang)
    dll.eciSetParam(v.h, INPUT_TYPE, 1)
    n = v.say(text)
    v.close()
    return n


def asked(dll, lang, text, how, which, value):
    """The text on a new instance and after a stop, asked for one way."""
    old = None
    if how == "default":
        old = dll.eciSetDefaultParam(which, value)
    v = afterstop.Voice(dll, lang)
    if how == "instance":
        check("0x%x: eciSetParam(h, %d, %d) is taken" % (lang, which, value),
              dll.eciSetParam(v.h, which, value) >= 0)
    fresh = v.say(text)
    v.say(afterstop.STOPPED, abort_at=1)
    after = v.say(text)
    v.close()
    if how == "default":
        # Answered in the caller's sense, so it goes straight back.
        dll.eciSetDefaultParam(which, old)
    return fresh, after


def main(path):
    dll = langs.LOADER(os.path.abspath(path))
    langs.declare(dll)
    dll.eciSetParam.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
    dll.eciSetDefaultParam.argtypes = [ctypes.c_int, ctypes.c_int]

    for lang in langs.languages(dll):
        if lang == JAPANESE:
            continue

        v = afterstop.Voice(dll, lang)
        check("0x%x: eciSetParam(h, 11, 1) is refused, as IBM's was" % lang,
              dll.eciSetParam(v.h, PHRASE_PREDICTION, 1) == -1)
        v.close()

        for name, anno, ways, sentences in SETTINGS:
            text = sentences.get(lang, sentences.get(ENUS))
            plain = annotated(dll, lang, text)
            want = annotated(dll, lang, anno + text)
            if lang in sentences:
                check("0x%x: %s moves %r (%d samples without, %d with %s)"
                      % (lang, name, text[:24], plain, want, anno.strip()),
                      plain != want)
            elif plain == want:
                print("  0x%x: %s does not move %r" % (lang, name, text[:24]))
            for label, how, which, value in ways:
                fresh, after = asked(dll, lang, text, how, which, value)
                check("0x%x: %s gives %s's %d samples on a new instance"
                      " (%d)" % (lang, label, anno.strip(), want, fresh),
                      fresh == want)
                check("0x%x: %s gives them after a stop as well (%d)"
                      % (lang, label, after), after == want)

    if FAILED:
        print("asked.py: %d failed" % len(FAILED))
        return 1
    print("asked.py: all passed")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    sys.exit(main(sys.argv[1]))
