#!/usr/bin/env python3
"""Speak every language a library has, from one process, at once.

A build may have more than one language in it, and the only way to be sure
none of the engine has quietly stayed global is to have two of them going
at the same time and check that neither has moved. So both instances are
made before either speaks, and what each produces is held against what the
same library produces speaking that language on its own.

Byte for byte, because that is the standard everything else here is held
to: a language that sounds nearly right beside another one is exactly the
failure this is looking for. What each language is held against is a
process of its own, and it has to be: the engine does not say a sentence in
the same samples twice running on one instance -- the same 38,423 samples
come out under a different hash each time -- so the only utterance whose
samples can be compared is the first one an instance speaks.

It is the check that needs neither Wine nor IBM's objects, like
test/hash.sh, and unlike test/hash.sh it says nothing about whether the
audio is right -- test/suite.sh is what says that. This says the audio does
not depend on what else is loaded.

usage: langs.py <eci.dll>
"""

import ctypes
import hashlib
import os
import sys
import time

FRAME = 2048

# What the caller says: a sentence of the language that speaks it, for
# whichever of them a library turns out to have. A language with no line
# here is spoken the English one, which still answers the question this is
# asking -- it is the samples not moving that matters, not what they say.
SAY = {
    0x10000: "The quick brown fox jumps over the lazy dog.",
    0x10001: "The quick brown fox jumps over the lazy dog.",
    0x20000: "El rapido zorro marron salta sobre el perro perezoso.",
    0x20001: "El rapido zorro marron salta sobre el perro perezoso.",
    0x30000: "Le rapide renard brun saute par-dessus le chien paresseux.",
    0x30001: "Le rapide renard brun saute par-dessus le chien paresseux.",
    0x40000: "Der schnelle braune Fuchs springt ueber den faulen Hund.",
    0x50000: "La rapida volpe marrone salta sopra il cane pigro.",
}
FALLBACK = SAY[0x10000]


def declare(dll):
    """Everything that takes or answers a handle has to say so, or ctypes
    truncates it to an int and the engine is handed half an address."""
    dll.eciNewEx.restype = ctypes.c_void_p
    dll.eciNewEx.argtypes = [ctypes.c_int]
    dll.eciGetAvailableLanguages.argtypes = [ctypes.c_void_p,
                                             ctypes.POINTER(ctypes.c_int)]
    dll.eciRegisterCallback.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                        ctypes.c_void_p]
    dll.eciSetOutputBuffer.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                       ctypes.c_void_p]
    dll.eciAddText.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    dll.eciSynthesize.argtypes = [ctypes.c_void_p]
    dll.eciSpeaking.argtypes = [ctypes.c_void_p]
    dll.eciDelete.restype = ctypes.c_void_p
    dll.eciDelete.argtypes = [ctypes.c_void_p]


class Voice:
    """One instance, and the samples it has handed back."""

    def __init__(self, dll, lang):
        self.dll = dll
        self.lang = lang
        self.buf = (ctypes.c_short * FRAME)()
        self.said = bytearray()

        cb = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int,
                                ctypes.c_long, ctypes.c_void_p)

        @cb
        def on_message(h, msg, param, data):
            if msg == 0:                       # eciWaveformBuffer
                self.said.extend(bytes(self.buf)[:param * 2])
            return 1                           # eciDataProcessed

        # Kept on the object, or Python collects it and the engine calls
        # back into nothing.
        self.callback = on_message

        self.h = dll.eciNewEx(lang)
        if not self.h:
            raise SystemExit("langs.py: no instance in 0x%x" % lang)
        dll.eciRegisterCallback(self.h, self.callback, None)
        if not dll.eciSetOutputBuffer(self.h, FRAME, self.buf):
            raise SystemExit("langs.py: 0x%x refused a sample buffer" % lang)

    def speak(self, text):
        if not self.dll.eciAddText(self.h, text.encode("mbcs")):
            raise SystemExit("langs.py: 0x%x refused the text" % self.lang)
        if not self.dll.eciSynthesize(self.h):
            raise SystemExit("langs.py: 0x%x refused to speak" % self.lang)
        for _ in range(3000):
            if not self.dll.eciSpeaking(self.h):
                break
            time.sleep(0.01)

    def close(self):
        self.dll.eciDelete(self.h)
        self.h = None


def languages(dll):
    """Asked with no room, which answers how many there are, and then with
    room, which answers which."""
    count = ctypes.c_int(0)
    if dll.eciGetAvailableLanguages(None, ctypes.byref(count)):
        raise SystemExit("langs.py: it would not say what languages it has")
    n = count.value
    if n < 1:
        raise SystemExit("langs.py: it has no language in it")
    out = (ctypes.c_uint * n)()
    count = ctypes.c_int(n)
    dll.eciGetAvailableLanguages(out, ctypes.byref(count))
    return [out[i] for i in range(count.value)]


def alone(path, lang, text):
    """The same language in a process of its own, so that what it says
    beside another one can be held against it."""
    dll = ctypes.WinDLL(os.path.abspath(path))
    declare(dll)
    v = Voice(dll, lang)
    v.speak(text)
    said = bytes(v.said)
    v.close()
    return said


def main(path):
    dll = ctypes.WinDLL(os.path.abspath(path))
    declare(dll)

    langs = languages(dll)
    print("langs.py: %d language%s: %s"
          % (len(langs), "" if len(langs) == 1 else "s",
             ", ".join("0x%x" % l for l in langs)))
    if len(langs) < 2:
        print("langs.py: one language, so there is nothing to interfere"
              " with it")
        return 0

    # Every one of them alive before any of them speaks.
    voices = [Voice(dll, l) for l in langs]

    bad = 0
    for v in voices:
        v.speak(SAY.get(v.lang, FALLBACK))

    for v in voices:
        together = bytes(v.said)
        v.close()
        apart = alone(path, v.lang, SAY.get(v.lang, FALLBACK))
        same = together == apart
        print("langs.py: 0x%x %6d samples %s %s"
              % (v.lang, len(together) // 2,
                 hashlib.sha256(together).hexdigest()[:16],
                 "as it speaks alone" if same
                 else "DIFFERS from what it says alone"))
        if not same:
            bad = 1

    if bad:
        print("langs.py: a language changed because another was loaded")
    return bad


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__.strip())
    sys.exit(main(sys.argv[1]))
