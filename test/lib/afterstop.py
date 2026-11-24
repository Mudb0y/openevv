#!/usr/bin/env python3
"""What an instance says after a stop, held to what a new one says.

A stop restarts the machine, which puts every one of its variables back to
nought. That takes back the line an engine is put through when it is made --
`v1 `ts0 `da0 `ty1 `pp0, which among other things turns phrase prediction
and the abbreviation dictionary off -- and a language of the older interface
is told every setting again before it next speaks, which puts it right. A
language lifted from 6.1 is told only what its caller has changed, and until
24 November 2026 nothing put it right: after the first stop Brazilian
Portuguese spoke every sentence of any length five per cent longer, with the
breaks phrase prediction guesses at, and Brazilian Portuguese and Finnish
both read Dr. and mg through the abbreviation dictionary, for as long as the
instance lived. Through Speech Dispatcher a stop is every key a screen
reader user presses while it speaks.

So in every language the library has, a sentence is spoken on a new
instance, a second utterance is stopped at its first buffer by answering
eciDataAbort, and the sentence is spoken again; it has to come out exactly
as long as it did the first time. As long and not the same samples: what an
instance has already said moves the samples of what it says next, by
design, and a stop does not take that back. A length it does take back,
since a length is the sum of the durations the rules chose and the rules
start again from nought, which is also why only the utterance straight after
the stop is compared: one more, and in both Englishes and Canadian French
the history has moved a long sentence's length as well. The stopped utterance has to have been cut short, or the stop
was never reached and the check says nothing.

Japanese is passed over: its romanizer stands between the text and the
machine, and none of the sentences here are Japanese.

It needs neither Wine nor IBM's objects.

usage: test/lib/afterstop.py <eci.dll or libeci.so>
"""

import ctypes
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import langs  # noqa: E402  the library's two kinds and its prototypes

JAPANESE = 0x80000
WAVEFORM = 0
PROCESSED, ABORT = 1, 2

#: What every language is asked, beside its own sentence in langs.SAY. The
#: first is the one the Portuguese fault was found on and is long enough for
#: phrase prediction to break; the second has two abbreviations in it. What
#: they mean in another language does not matter: the rules of every
#: language read them, and that is what is being held still.
SENTENCES = (
    "Esta é uma frase de teste para medir a velocidade da voz.",
    "O Dr. Silva tomou 5 mg hoje, e depois saiu para a rua com o cachorro.",
)

#: Stopped at its first buffer, which every one of these fills.
STOPPED = "Janela do terminal, Alt Tab. " * 4


class Voice(langs.Voice):
    """An instance that can be told to answer eciDataAbort at a buffer."""

    def __init__(self, dll, lang):
        super().__init__(dll, lang)
        self.abort_at = None
        self.buffers = 0
        cb = langs.CALLBACK_TYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int,
                                 ctypes.c_int, ctypes.c_void_p)

        @cb
        def on_message(h, msg, param, data):
            if msg == WAVEFORM:
                self.said.extend(bytes(self.buf)[:param * 2])
                self.buffers += 1
                if self.abort_at is not None and self.buffers >= self.abort_at:
                    return ABORT
            return PROCESSED

        self.callback = on_message
        dll.eciRegisterCallback(self.h, self.callback, None)

    def say(self, text, abort_at=None):
        """How many samples the text came to."""
        self.said = bytearray()
        self.buffers = 0
        self.abort_at = abort_at
        self.speak(text)
        return len(self.said) // 2


FAILED = []


def check(what, ok):
    print("%s: %s" % ("ok" if ok else "FAILED", what), flush=True)
    if not ok:
        FAILED.append(what)


def main(path):
    dll = langs.LOADER(os.path.abspath(path))
    langs.declare(dll)

    for lang in langs.languages(dll):
        if lang == JAPANESE:
            continue
        v = Voice(dll, lang)
        whole = v.say(STOPPED)
        v.close()
        for sentence in (langs.SAY.get(lang, langs.FALLBACK),) + SENTENCES:
            v = Voice(dll, lang)
            fresh = v.say(sentence)
            cut = v.say(STOPPED, abort_at=1)
            after = v.say(sentence)
            v.close()
            check("0x%x: %r was cut short" % (lang, STOPPED[:20]),
                  0 < cut < whole)
            check("0x%x: %r after a stop is as long as on a new instance"
                  " (%d and %d samples)" % (lang, sentence[:24], fresh, after),
                  fresh == after)

    if FAILED:
        print("afterstop.py: %d failed" % len(FAILED))
        return 1
    print("afterstop.py: all passed")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    sys.exit(main(sys.argv[1]))
