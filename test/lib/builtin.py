#!/usr/bin/env python3
"""The dictionary a language carries, eciBuiltInDict.

US English carries a dictionary of its own in the library, begun from the
community's IBMTTSDictionaries, and an instance consults it after whatever
dictionary the caller has given it. This
holds the engine to that: an entry it carries is read exactly as the same
entry is when a caller loads it, the caller's own entry for a word wins over
it in every volume, a dictionary the caller has given that does not hold the
word leaves it to the one carried, a root entry reaches the word's endings,
the abbreviation volume answers only while abbreviations are on, and the
switch answers as the engine's other settings do. A language that carries
none says the same with the switch either way.

The dictionary also splits a word with a capital inside it that nothing
holds into the words it is made of, NixOS as Nix OS. That is held to reading
exactly as the parts written apart do, with a name beginning such as Mc, a
plural of capitals and a word a root volume holds left whole, and British
English, which carries no entries, splitting all the same.

A main entry is a whole word, and one written in phones used to leave a
possessive's ending outside its brackets to be spelled out. The possessive
is held to reading exactly as an entry for it written out would, whichever
dictionary holds the word, and a caller's entry in words for it to leave the
ending to the rules as before.

A word in small letters English could not say is cut into the words,
chunks and letters it can be said as, usrdct as U S R D C T and cgroup as C
group. That is held to reading exactly as the pieces written apart do with
the dictionary off, since what the cut writes is never looked up again; a
plural of letters only where the letters end in an abbreviation a main
volume holds; a word English could say, a kept word, a Roman numeral, a Mc
name and a word in capitals left whole; and a caller's entry for the word
winning over the cut.

A reading that ends in a letter said by name, nginx's engine X among them,
is held to keeping the dot of a file name after it, nginx.conf as engine X
dot conf, and to reading a sentence's full stop and a comma after it as the
same reading written out does.

What a word is read as is asked of eciGeneratePhonemes, which says it plainly;
one check holds the samples as well, so that what the synthesiser is handed
is known to follow.

It needs neither Wine nor IBM's objects.

usage: test/lib/builtin.py <eci.dll or libeci.so>
"""

import ctypes
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import langs  # noqa: E402  the library's two kinds and its prototypes

ENGLISH = 0x10000
BRITISH = 0x10001

SYNTH_MODE = 0
DICTIONARY = 3
PHONEME_MODE = 7
BUILTIN = 34

MAIN, ROOT, ABBR = 0, 1, 2

PHONEME_BUFFER = 1

#: Words the dictionary US English carries holds, each with the entry it
#: holds, written the way a caller would load it.
CARRIED_ROOT = (b"teamtalk", b"`[.1tim.2tck]")
CARRIED_MAIN = (b"Allrecipes", b"all recipes")
CARRIED_PHONES = (b"TeX", b"`[.1tEk]")
CARRIED_ABBR = (b"WWII", b"world war two")


def declare(dll):
    langs.declare(dll)
    v = ctypes.c_void_p
    dll.eciSetParam.argtypes = [v, ctypes.c_int, ctypes.c_int]
    dll.eciGetParam.argtypes = [v, ctypes.c_int]
    dll.eciReset.argtypes = [v]
    dll.eciSetDefaultParam.argtypes = [ctypes.c_int, ctypes.c_int]
    dll.eciGetDefaultParam.argtypes = [ctypes.c_int]
    dll.eciGeneratePhonemes.argtypes = [v, ctypes.c_int, ctypes.c_char_p]
    dll.eciNewDict.restype = v
    dll.eciNewDict.argtypes = [v]
    dll.eciSetDict.argtypes = [v, v]
    dll.eciDeleteDict.restype = v
    dll.eciDeleteDict.argtypes = [v, v]
    dll.eciUpdateDict.argtypes = [v, v, ctypes.c_int, ctypes.c_char_p,
                                  ctypes.c_char_p]


class Reader:
    """An instance that answers what a text is read as."""

    def __init__(self, dll, lang=ENGLISH, builtin=None, entries=(),
                 abbreviations=False):
        self.dll = dll
        self.said = []
        self.buf = ctypes.create_string_buffer(8192)
        cb = langs.CALLBACK_TYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int,
                                 ctypes.c_int, ctypes.c_void_p)

        @cb
        def on_message(h, msg, param, data):
            if msg == PHONEME_BUFFER and param > 0:
                self.said.append(self.buf.raw[:param])
            return 1

        self.callback = on_message
        self.h = dll.eciNewEx(lang)
        if not self.h:
            raise SystemExit("builtin.py: no instance in 0x%x" % lang)
        dll.eciRegisterCallback(self.h, self.callback, None)
        # Phonemes come only in the synthesis mode that waits to be told.
        dll.eciSetParam(self.h, SYNTH_MODE, 1)
        dll.eciSetParam(self.h, PHONEME_MODE, 1)
        if abbreviations:
            dll.eciSetParam(self.h, DICTIONARY, 0)
        if builtin is not None:
            dll.eciSetParam(self.h, BUILTIN, builtin)
        self.dict = None
        if entries:
            self.dict = dll.eciNewDict(self.h)
            for vol, word, says in entries:
                if dll.eciUpdateDict(self.h, self.dict, vol, word, says):
                    raise SystemExit("builtin.py: the dictionary refused %r"
                                     % word)
            if dll.eciSetDict(self.h, self.dict):
                raise SystemExit("builtin.py: the dictionary was not taken")

    def read(self, text):
        del self.said[:]
        self.dll.eciAddText(self.h, text)
        self.dll.eciGeneratePhonemes(self.h, len(self.buf), self.buf)
        return b"".join(self.said)

    def close(self):
        if self.dict:
            self.dll.eciSetDict(self.h, None)
            self.dll.eciDeleteDict(self.h, self.dict)
        self.dll.eciDelete(self.h)


def read(dll, text, **how):
    r = Reader(dll, **how)
    out = r.read(text)
    r.close()
    return out


FAILED = []


def check(what, ok):
    print("builtin.py: %s %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        FAILED.append(what)


def setting_checks(dll):
    v = langs.Voice(dll, ENGLISH)
    h = v.h
    check("an instance starts with it on", dll.eciGetParam(h, BUILTIN) == 1)
    check("a new value is taken and the old one answered",
          dll.eciSetParam(h, BUILTIN, 0) == 1
          and dll.eciGetParam(h, BUILTIN) == 0)
    check("a value it has not got is refused and changes nothing",
          dll.eciSetParam(h, BUILTIN, 2) == -1
          and dll.eciSetParam(h, BUILTIN, -1) == -1
          and dll.eciGetParam(h, BUILTIN) == 0)
    dll.eciReset(h)
    check("a reset puts back the default", dll.eciGetParam(h, BUILTIN) == 1)
    v.close()

    check("the default is on", dll.eciGetDefaultParam(BUILTIN) == 1)
    check("and can be changed for instances made after",
          dll.eciSetDefaultParam(BUILTIN, 0) == 1)
    later = langs.Voice(dll, ENGLISH)
    check("which start with it", dll.eciGetParam(later.h, BUILTIN) == 0)
    later.close()
    word = CARRIED_ROOT[0]
    off_by_default = read(dll, word)
    dll.eciSetDefaultParam(BUILTIN, 1)
    check("and read as one set to off does",
          off_by_default == read(dll, word, builtin=0))


def reading_checks(dll):
    for vol, (word, says), name in ((ROOT, CARRIED_ROOT, "root"),
                                    (MAIN, CARRIED_MAIN, "main")):
        off = read(dll, word, builtin=0)
        on = read(dll, word)
        loaded = read(dll, word, builtin=0, entries=[(vol, word, says)])
        check("%s: %s is read otherwise with it on" % (name, word.decode()),
              on != off and on)
        check("%s: and exactly as the same entry loaded by a caller"
              % name, on == loaded)

    word, says = CARRIED_ROOT
    mine = (ROOT, word, b"tea time")
    by_caller = read(dll, word, builtin=0, entries=[mine])
    check("a caller's root entry wins over it",
          read(dll, word, entries=[mine]) == by_caller
          and by_caller != read(dll, word))
    word2, says2 = CARRIED_MAIN
    mine2 = (MAIN, word2, b"recipe site")
    by_caller2 = read(dll, word2, builtin=0, entries=[mine2])
    check("a caller's main entry wins over it",
          read(dll, word2, entries=[mine2]) == by_caller2
          and by_caller2 != read(dll, word2))

    other = [(ROOT, b"zorblat", b"door bell"), (MAIN, b"Zorblat", b"door")]
    check("a caller's dictionary without the word leaves it to the one"
          " carried", read(dll, word, entries=other) == read(dll, word))
    check("and so does one with only another volume loaded",
          read(dll, word, entries=[(MAIN, b"Zorblat", b"door")])
          == read(dll, word))

    plural = word + b"s"
    check("a root entry reaches the word's endings",
          read(dll, plural) == read(dll, plural, builtin=0,
                                    entries=[(ROOT, word, says)])
          and read(dll, plural) != read(dll, plural, builtin=0))

    abbr, means = CARRIED_ABBR
    check("the abbreviation volume is silent while abbreviations are off",
          read(dll, abbr) == read(dll, abbr, builtin=0))
    on = read(dll, abbr, abbreviations=True)
    check("and answers while they are on, as the entry loaded by a caller",
          on != read(dll, abbr, builtin=0, abbreviations=True)
          and on == read(dll, abbr, builtin=0, abbreviations=True,
                         entries=[(ABBR, abbr, means)]))

    sentence = b"We met on teamtalk and talked."
    r = Reader(dll)
    first = r.read(sentence)
    dll.eciSetParam(r.h, BUILTIN, 0)
    second = r.read(sentence)
    dll.eciSetParam(r.h, BUILTIN, 1)
    third = r.read(sentence)
    r.close()
    check("turned off and on again on one instance, between utterances",
          second == read(dll, sentence, builtin=0) and third == first
          and first != second)


def split_checks(dll, have):
    for word, parts in ((b"NixOS", b"Nix OS"), (b"HTMLElement", b"HTML Element"),
                        (b"iOS", b"i OS")):
        check("%s is read as %s" % (word.decode(), parts.decode()),
              read(dll, word) == read(dll, parts)
              and read(dll, word) != read(dll, word, builtin=0))
    for word in (b"McDonald", b"DiCaprio", b"SDKs"):
        check("%s is left whole" % word.decode(),
              read(dll, word) == read(dll, word, builtin=0))
    check("a word a root volume holds is left for the root lookup",
          read(dll, b"TeamTalk") == read(dll, CARRIED_ROOT[0])
          and read(dll, b"TeamTalk") != read(dll, b"Team Talk"))
    mine = [(ROOT, b"zorbfoo", b"door")]
    check("and so is one the caller's root volume holds",
          read(dll, b"ZorbFoo", entries=mine)
          == read(dll, b"zorbfoo", entries=mine)
          and read(dll, b"ZorbFoo") == read(dll, b"Zorb Foo"))
    if BRITISH in have:
        check("British English, which carries no entries, splits as well",
              read(dll, b"NixOS", lang=BRITISH)
              == read(dll, b"Nix OS", lang=BRITISH)
              and read(dll, b"NixOS", lang=BRITISH)
              != read(dll, b"NixOS", lang=BRITISH, builtin=0))


def cut_checks(dll, have):
    # The cut's pieces are handed to the rules and never looked up again, so
    # they are held to the pieces read with the dictionary off.
    for word, parts in ((b"usrdct", b"U S R D C T"), (b"cgroup", b"C group"),
                        (b"tmpfiles", b"T M P files"),
                        (b"vgchange", b"V G change"), (b"fdopen", b"F D open"),
                        (b"cvsexportcommit", b"C V S export commit"),
                        (b"gnutls", b"gnu T L S"), (b"procfs", b"proc F S"),
                        (b"maxcpus", b"max C P U's")):
        check("%s is cut as %s" % (word.decode(), parts.decode()),
              read(dll, word) == read(dll, parts, builtin=0)
              and read(dll, word) != read(dll, word, builtin=0))
    for word in (b"twelfths", b"brzezinski", b"algorithm", b"against",
                 b"couldn", b"xvii", b"mcfarland", b"comm", b"freq",
                 b"USRDCT"):
        check("%s is left whole" % word.decode(),
              read(dll, word) == read(dll, word, builtin=0))
    mine = [(MAIN, b"usrdct", b"user dict")]
    check("a caller's main entry for the word wins over the cut",
          read(dll, b"usrdct", entries=mine)
          == read(dll, b"usrdct", builtin=0, entries=mine))
    if BRITISH in have:
        check("British English cuts as well",
              read(dll, b"cgroup", lang=BRITISH)
              == read(dll, b"C group", lang=BRITISH, builtin=0)
              and read(dll, b"cgroup", lang=BRITISH)
              != read(dll, b"cgroup", lang=BRITISH, builtin=0))


def dot_checks(dll):
    # A reading ending in a letter said by name used to swallow the dot of
    # a file name, the rules taking the letter for an initial.
    for name in (b"nginx.conf", b"kubectl.yaml", b"cpu.c",
                 b"lvmlockd.service"):
        check("%s keeps its dot" % name.decode(),
              b"`[.1dat]" in read(dll, name))
    for text in (b"I use nginx.", b"I use nginx, and more."):
        check("and %s reads as before" % text.decode(),
              read(dll, text) == read(dll, text.replace(b"nginx",
                                                          b"engine X")))
    mine = [(MAIN, b"zorbq", b"zorb Q")]
    check("and so does a caller's entry",
          b"`[.1dat]" in read(dll, b"zorbq.conf", entries=mine))


def possessive_checks(dll):
    word, says = CARRIED_PHONES
    mine = word + b"'s"
    check("%s reads as an entry for it written out would" % mine.decode(),
          read(dll, mine) == read(dll, mine, builtin=0,
                                  entries=[(MAIN, mine, b"`[.1tEks]")])
          and read(dll, mine) != read(dll, mine, builtin=0))
    zorb = [(MAIN, b"Zorbas", b"`[.1zcr.0bxs]")]
    check("and one of a caller's, the ending a syllable after a hiss",
          read(dll, b"Zorbas's", entries=zorb)
          == read(dll, b"Zorbas's",
                  entries=[(MAIN, b"Zorbas's", b"`[.1zcr.0bx.0sXz]")]))
    check("a caller's entry in words for the word leaves it to the rules",
          read(dll, mine, entries=[(MAIN, word, b"tech")])
          == read(dll, b"tech's"))


def sample_checks(dll):
    text = "We met on teamtalk."

    def said(builtin, entries=()):
        v = langs.Voice(dll, ENGLISH)
        d = None
        if builtin is not None:
            dll.eciSetParam(v.h, BUILTIN, builtin)
        if entries:
            d = dll.eciNewDict(v.h)
            for vol, word, says in entries:
                dll.eciUpdateDict(v.h, d, vol, word, says)
            dll.eciSetDict(v.h, d)
        v.speak(text)
        out = bytes(v.said)
        if d:
            dll.eciSetDict(v.h, None)
            dll.eciDeleteDict(v.h, d)
        v.close()
        return out

    on = said(None)
    check("the samples follow: %d with it, %d without" % (
          len(on) // 2, len(said(0)) // 2),
          on != said(0)
          and on == said(0, [(ROOT,) + CARRIED_ROOT]))


def main(path):
    dll = langs.LOADER(os.path.abspath(path))
    declare(dll)
    have = langs.languages(dll)
    if ENGLISH not in have:
        print("builtin.py: this library has no US English, the one language"
              " that carries a dictionary")
        return 0

    setting_checks(dll)
    reading_checks(dll)
    split_checks(dll, have)
    possessive_checks(dll)
    cut_checks(dll, have)
    dot_checks(dll)
    sample_checks(dll)
    if BRITISH in have:
        word = CARRIED_ROOT[0]
        check("British English carries no entries, and reads one the same"
              " either way",
              read(dll, word, lang=BRITISH)
              == read(dll, word, lang=BRITISH, builtin=0))

    if FAILED:
        print("builtin.py: %d failed" % len(FAILED))
        return 1
    print("builtin.py: all passed")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__.strip())
    sys.exit(main(sys.argv[1]))
