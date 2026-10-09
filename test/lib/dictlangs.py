#!/usr/bin/env python3
"""Keep a dictionary across a change of language.

A dictionary lives inside the engine of the language it was made in, and a
language change used to let that engine go while the caller still held the
dictionary. Putting it back in force afterwards then called through an
engine that was no longer there, and the process died. That is exactly how
the IBMTTS and Eloquence 64 drivers use the interface -- one dictionary per
language, put back as its language returns -- so a screen reader with
dictionaries loaded went down the second time a document changed language.
IBM shipped one language per binary and never reached it; a build with two
does.

Two things are asked. The sequence that faulted has to finish. And a
dictionary has to still be working when its language comes back: one
instance says a made-up word its dictionary spells as another, and a second
instance, with the same dictionaries and the same history, says that other
word outright. The two have to come out the same samples. Both instances
live in one process, which langs.py is what says is safe.

It needs English and German in the library, and says so and passes when it
has not got both. It needs neither Wine nor IBM's objects.

usage: test/lib/dictlangs.py <eci.dll or libeci.so>
"""

import ctypes
import hashlib
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import langs  # noqa: E402  the library's two kinds and its prototypes

ENGLISH = 0x10000
GERMAN = 0x40000
LANGUAGE = 9

#: A word neither language has, and what each dictionary says it is.
MADE_UP = "zorbleck"
MEANS = {ENGLISH: "banana", GERMAN: "Apfel"}


def declare(dll):
    langs.declare(dll)
    v = ctypes.c_void_p
    dll.eciNewDict.restype = v
    dll.eciNewDict.argtypes = [v]
    dll.eciSetDict.argtypes = [v, v]
    dll.eciLoadDict.argtypes = [v, v, ctypes.c_int, ctypes.c_char_p]
    dll.eciDeleteDict.restype = v
    dll.eciDeleteDict.argtypes = [v, v]
    dll.eciSetParam.argtypes = [v, ctypes.c_int, ctypes.c_int]


class Voice(langs.Voice):
    def said_now(self):
        """What it has handed back since it was last asked."""
        said = bytes(self.said)
        del self.said[:]
        return said

    def to(self, lang):
        self.dll.eciSetParam(self.h, LANGUAGE, lang)

    def dictionary(self, path):
        """A dictionary made in the language in force, filled from a file
        and put in force."""
        d = self.dll.eciNewDict(self.h)
        if not d:
            raise SystemExit("dictlangs.py: no dictionary")
        if self.dll.eciLoadDict(self.h, d, 0, path.encode(langs.CODEC)):
            raise SystemExit("dictlangs.py: %s would not load" % path)
        self.dll.eciSetDict(self.h, d)
        return d


def history(voice, files, last):
    """English with a dictionary, German with one, English again with its
    own put back, and German again with its: what a reader does with a
    German quotation in an English page, twice. What comes back is the last
    thing said in each language."""
    english = voice.dictionary(files[ENGLISH])
    voice.speak("One.")
    voice.to(GERMAN)
    german = voice.dictionary(files[GERMAN])
    voice.speak("Eins.")
    voice.to(ENGLISH)
    voice.dll.eciSetDict(voice.h, english)
    voice.said_now()
    voice.speak(last[ENGLISH])
    heard_english = voice.said_now()
    voice.to(GERMAN)
    voice.dll.eciSetDict(voice.h, german)
    voice.speak(last[GERMAN])
    heard_german = voice.said_now()
    voice.dll.eciDeleteDict(voice.h, english)
    voice.dll.eciDeleteDict(voice.h, german)
    return heard_english, heard_german


def main(path):
    dll = langs.LOADER(os.path.abspath(path))
    declare(dll)
    have = langs.languages(dll)
    if ENGLISH not in have or GERMAN not in have:
        print("dictlangs.py: this library has not got both English and"
              " German, so there is no change of language to make")
        return 0

    with tempfile.TemporaryDirectory() as tmp:
        files = {}
        for lang, word in MEANS.items():
            files[lang] = os.path.join(tmp, "%x.dic" % lang)
            with open(files[lang], "wb") as f:
                f.write(("%s\t%s\r\n" % (MADE_UP, word)).encode(langs.CODEC))

        # The other language's dictionary put in force while this one's is
        # still open: the shortest road to the fault.
        v = Voice(dll, ENGLISH)
        english = v.dll.eciNewDict(v.h)
        v.to(GERMAN)
        v.dll.eciSetDict(v.h, v.dll.eciNewDict(v.h))
        v.dll.eciSetDict(v.h, english)
        v.to(ENGLISH)
        v.dll.eciSetDict(v.h, english)
        v.speak("Still here.")
        v.close()
        print("dictlangs.py: a dictionary of another language put in force"
              " and survived")

        bad = 0
        made_up = {ENGLISH: MADE_UP, GERMAN: MADE_UP}
        through = Voice(dll, ENGLISH)
        outright = Voice(dll, ENGLISH)
        heard = history(through, files, made_up)
        meant = history(outright, files, MEANS)
        through.close()
        outright.close()
        for lang, a, b in zip((ENGLISH, GERMAN), heard, meant):
            same = a == b and len(a) > 0
            print("dictlangs.py: 0x%x %6d samples %s %s"
                  % (lang, len(a) // 2, hashlib.sha256(a).hexdigest()[:16],
                     "its dictionary still in force" if same
                     else "DIFFERS: its dictionary was lost on the way"))
            if not same:
                bad = 1

        # And the comparison could have failed: the made-up word said with
        # no dictionary at all is not the word it stands for.
        plain = Voice(dll, ENGLISH)
        plain.speak(MADE_UP)
        alone = plain.said_now()
        plain.close()
        word = Voice(dll, ENGLISH)
        word.speak(MEANS[ENGLISH])
        if alone == word.said_now():
            print("dictlangs.py: the made-up word sounds like its meaning"
                  " with no dictionary, so this proves nothing")
            bad = 1
        word.close()
    return bad


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__.strip())
    sys.exit(main(sys.argv[1]))
