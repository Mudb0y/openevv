#!/usr/bin/env python3
"""Which pauses the engine shortens, eciPauseMode.

The engine pauses for as long as at a full stop wherever it is made to finish
a stretch of text: the end of an utterance, and every change of voice, speed,
pitch, inflection, volume or language. The IBMTTS and Eloquence 64 drivers
shorten that pause by writing the engine's pause annotation with a value of
one into their text, at the end and, if asked, before every mark. The engine
does the same thing itself now, and this holds it to doing exactly that: what
it says with the setting on is what it says with the setting off and the
annotation written in by hand where the drivers write it, sample for sample,
with index marks arriving at the same samples.

That is checked for the end of an utterance, a change of voice in the middle
of one, a capital letter spelled at a raised pitch, a change at the very end,
a change of language, text with annotations turned off, and every mark in
every language the library has. The setting's own answers come first, and
the checks that it leaves alone what it should -- punctuation, a pause the
caller chose -- come last.

It needs neither Wine nor IBM's objects.

usage: test/lib/pauses.py <eci.dll or libeci.so>
"""

import ctypes
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import langs  # noqa: E402  the library's two kinds and its prototypes

ENGLISH = 0x10000
GERMAN = 0x40000
JAPANESE = 0x80000

INPUT_TYPE = 1
LANGUAGE = 9
PAUSE_MODE = 33
NEVER, AT_END, ALWAYS = 0, 1, 2

INDEX_REPLY = 2

#: The marks the drivers shorten, as the engine's byte set has them.
MARKS = [b",", b".", b";", b":", b"?", b"!", b" -", b" \x96", b" \x97"]

#: A word to put either side of a mark, for every language the engine has.
#: What it means does not matter; that the engine pauses after it does.
NAMES = (b"Anna", b"Maria")


def declare(dll):
    langs.declare(dll)
    v = ctypes.c_void_p
    dll.eciSetParam.argtypes = [v, ctypes.c_int, ctypes.c_int]
    dll.eciGetParam.argtypes = [v, ctypes.c_int]
    dll.eciInsertIndex.argtypes = [v, ctypes.c_int]
    dll.eciReset.argtypes = [v]
    dll.eciSetDefaultParam.argtypes = [ctypes.c_int, ctypes.c_int]
    dll.eciStop.argtypes = [v]
    dll.eciGetDefaultParam.argtypes = [ctypes.c_int]


class Voice(langs.Voice):
    """An instance that also writes down the sample each index mark
    arrived at."""

    def __init__(self, dll, lang, mode, annotated=True):
        super().__init__(dll, lang)
        self.marks = []
        cb = langs.CALLBACK_TYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int,
                                 ctypes.c_int, ctypes.c_void_p)

        @cb
        def on_message(h, msg, param, data):
            if msg == 0:
                self.said.extend(bytes(self.buf)[:param * 2])
            elif msg == INDEX_REPLY:
                self.marks.append((param, len(self.said) // 2))
            return 1

        self.callback = on_message
        dll.eciRegisterCallback(self.h, self.callback, None)
        dll.eciSetParam(self.h, INPUT_TYPE, 1 if annotated else 0)
        dll.eciSetParam(self.h, PAUSE_MODE, mode)

    def say(self, steps):
        """Text, index marks and changes of language, in order, as one
        utterance."""
        for step in steps:
            if isinstance(step, bytes):
                if not self.dll.eciAddText(self.h, step):
                    raise SystemExit("pauses.py: the text was refused")
            elif isinstance(step, int):
                self.dll.eciInsertIndex(self.h, step)
            else:
                self.dll.eciSetParam(self.h, LANGUAGE, step[1])
        self.dll.eciSynthesize(self.h)
        for _ in range(3000):
            if not self.dll.eciSpeaking(self.h):
                break
            time.sleep(0.01)
        return bytes(self.said), list(self.marks)


def spoken(dll, lang, mode, steps, annotated=True):
    v = Voice(dll, lang, mode, annotated)
    out = v.say(steps)
    v.close()
    return out


def language(code):
    return ("language", code)


FAILED = []


def check(what, ok):
    print("pauses.py: %s %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        FAILED.append(what)


def same(dll, what, mode, steps, by_hand, lang=ENGLISH, annotated=True):
    """The setting on what was written, and the drivers' annotation written
    in by hand with the setting off, have to come out the same."""
    got = spoken(dll, lang, mode, steps, annotated)
    want = spoken(dll, lang, NEVER, by_hand)
    check("%s: %d samples, %s" % (what, len(got[0]) // 2,
                                  "as written by hand" if got == want
                                  else "DIFFERS from it written by hand"),
          got == want and len(got[0]) > 0)
    return got


def setting_checks(dll):
    v = Voice(dll, ENGLISH, AT_END)
    h = v.h
    dll.eciSetParam(h, PAUSE_MODE, AT_END)
    check("an instance answers what it was given",
          dll.eciGetParam(h, PAUSE_MODE) == AT_END)
    check("a new value is taken and the old one answered",
          dll.eciSetParam(h, PAUSE_MODE, ALWAYS) == AT_END
          and dll.eciGetParam(h, PAUSE_MODE) == ALWAYS)
    check("a value it has not got is refused and changes nothing",
          dll.eciSetParam(h, PAUSE_MODE, 3) == -1
          and dll.eciSetParam(h, PAUSE_MODE, -1) == -1
          and dll.eciGetParam(h, PAUSE_MODE) == ALWAYS)
    dll.eciReset(h)
    check("a reset puts back the default, which is the end of text only",
          dll.eciGetParam(h, PAUSE_MODE) == AT_END)
    v.close()

    check("the default is the end of text only",
          dll.eciGetDefaultParam(PAUSE_MODE) == AT_END)
    check("and can be changed for instances made after",
          dll.eciSetDefaultParam(PAUSE_MODE, NEVER) == AT_END)
    later = langs.Voice(dll, ENGLISH)
    check("which start with it", dll.eciGetParam(later.h, PAUSE_MODE) == NEVER)
    later.close()
    dll.eciSetDefaultParam(PAUSE_MODE, AT_END)

    # And one made with Never as its default speaks as one set to Never.
    dll.eciSetDefaultParam(PAUSE_MODE, NEVER)
    plain = langs.Voice(dll, ENGLISH)
    plain.speak("Desktop list")
    by_default = bytes(plain.said)
    plain.close()
    dll.eciSetDefaultParam(PAUSE_MODE, AT_END)
    check("a default of Never is what the instance speaks with",
          by_default == spoken(dll, ENGLISH, NEVER, [b"Desktop list"])[0])


def at_end_checks(dll, have):
    end = same(dll, "the end of an utterance", AT_END,
               [b"Desktop list"], [b"Desktop list `p1 "])
    full = spoken(dll, ENGLISH, NEVER, [b"Desktop list"])
    check("and that is the same speech cut short",
          len(end[0]) < len(full[0]) and full[0][:len(end[0])] == end[0])

    same(dll, "with index marks between the words", AT_END,
         [b"Desktop ", 1, b"list ", 2, b"Recycle bin"],
         [b"Desktop ", 1, b"list ", 2, b"Recycle bin `p1 "])
    same(dll, "a voice change in the middle", AT_END,
         [b"Desktop `vb60 list `vb50 is here"],
         [b"Desktop `p1 `vb60 list `p1 `vb50 is here `p1 "])
    # Each piece in a call of its own, as a screen reader sends a spelled
    # word: a letter written against an annotation is read as something
    # else in spelling mode, pause or no pause.
    same(dll, "a capital spelled at a raised pitch", AT_END,
         [b"`ts1 ", b"`vb70 ", b"H", b"`vb50 ", b"i", b"`ts0 "],
         [b"`ts1 ", b"`vb70 ", b"H", b" `p1 ", b"`vb50 ", b"i", b" `p1 ", b"`ts0 "])
    same(dll, "a change of pitch at the very end", AT_END,
         [b"Desktop list `vb50 "], [b"Desktop list `p1 `vb50 "])
    if GERMAN in have:
        same(dll, "a change of language in the middle", AT_END,
             [b"Desktop list", language(GERMAN), b"Guten Tag"],
             [b"Desktop list `p1 ", language(GERMAN), b"Guten Tag `p1 "])
    same(dll, "text with annotations turned off", AT_END,
         [b"Desktop list"], [b"Desktop list `p1 "], annotated=False)


def always_checks(dll, have):
    same(dll, "every mark in a sentence", ALWAYS,
         [b"Hello there, how are you? Fine; thanks: good - yes."],
         [b"Hello there `p1, how are you `p1? Fine `p1; thanks `p1: good  `p1- yes `p1."])
    same(dll, "and the index marks among them", ALWAYS,
         [b"One, ", 1, b"two. ", 2, b"three"],
         [b"One `p1, ", 1, b"two `p1. ", 2, b"three `p1 "])

    # In every language, each mark on its own: shortened as the drivers
    # shorten it, and never longer than unshortened, which is what a mark
    # the engine started naming would be.
    for lang in have:
        if lang == JAPANESE:
            continue
        bad = []
        for mark in MARKS:
            text = NAMES[0] + mark + b" " + NAMES[1]
            hand = NAMES[0] + mark[:-1] + b" `p1" + mark[-1:] + b" " + NAMES[1] + b" `p1 "
            got = spoken(dll, lang, ALWAYS, [text])
            want = spoken(dll, lang, NEVER, [hand])
            unshortened = spoken(dll, lang, NEVER, [text])
            if got != want or len(got[0]) > len(unshortened[0]):
                bad.append(mark.strip().decode("latin-1"))
        check("0x%x: every mark as the drivers shorten it%s"
              % (lang, "" if not bad else ", but not " + " ".join(bad)), not bad)


def unchanged_checks(dll, have):
    """What the setting has to leave alone."""
    for what, text in (("a sentence that ends in a full stop", b"Desktop list."),
                       ("one that ends in a quoted one", b'He said "hi."'),
                       ("one that ends in a phonetic spelling and a full stop",
                        b"Say `[h.E.l.oU]."),
                       ("a pause the caller asked for at the end", b"Desktop list `p300 "),
                       ("and the engine's own, which is how Eloquence 64 asks",
                        b"Desktop list `p0 ")):
        a = spoken(dll, ENGLISH, AT_END, [text])
        b = spoken(dll, ENGLISH, NEVER, [text])
        check("%s is left as it was" % what, a == b)

    # Text the engine was given and then told to forget is not finished with
    # a short pause when the next thing makes it finish a stretch.
    def after_a_stop(mode):
        v = Voice(dll, ENGLISH, mode)
        dll.eciAddText(v.h, b"Desktop list")
        time.sleep(0.05)
        dll.eciStop(v.h)
        del v.said[:]
        out = v.say([b"`vb60 Hello there."])
        v.close()
        return out
    check("what was said before a stop is forgotten with it",
          after_a_stop(AT_END) == after_a_stop(NEVER))

    # The romanizer ends every sentence it hands the engine with a full stop
    # of its own, so no Japanese text ends open.
    if JAPANESE in have:
        text = "こんにちは".encode("cp932")
        a = spoken(dll, JAPANESE, AT_END, [text])
        b = spoken(dll, JAPANESE, NEVER, [text])
        check("Japanese, which its romanizer ends with a full stop, is left as"
              " it was", a == b and len(a[0]) > 0)


def main(path):
    dll = langs.LOADER(os.path.abspath(path))
    declare(dll)
    have = langs.languages(dll)
    if ENGLISH not in have:
        print("pauses.py: this library has no English, which every case here"
              " is written in")
        return 0

    setting_checks(dll)
    at_end_checks(dll, have)
    always_checks(dll, have)
    unchanged_checks(dll, have)

    if FAILED:
        print("pauses.py: %d failed" % len(FAILED))
        return 1
    print("pauses.py: all passed")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__.strip())
    sys.exit(main(sys.argv[1]))
