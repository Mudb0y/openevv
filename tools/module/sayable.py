#!/usr/bin/env python3
"""What a language needs to tell a word that can be said from one that has to
be spelled, written out of word lists that carry no terms.

A word no dictionary holds goes to the letter rules, which read anything as a
word, so usrdct, cgroup and tmpfiles came out as single invented words. The
engine now asks of such a word, written in small letters, whether English
could say it: whether it begins the way an English word can begin, ends the
way one can end, and can be divided between its vowels into an ending and a
beginning. Where it cannot, it is cut into known words, chunks that can be
said and letters said by name, whichever is cheapest, the way eSpeak NG reads
a word it cannot pronounce: cgroup as C group, tmpfiles as T M P files,
usrdct as U S R D C T. src/eci/dict/eci_usrdct.c does that; this writes
what it asks.

Two files in lang/<tag>/dictionary:

  sayable   `onset' lines, the consonants a word may begin with; `coda'
            lines, the consonants one may end with; and `word' lines, words
            and names that fail the test but are words, so are never cut:
            algorithm, against, Edinburgh, couldnt, and every word the
            language's own tables hold that fails it. Without this file the
            engine cuts nothing.
  pieces    short common words a cut may use, and below the body blocks
            written by hand: technical words said as words (arg, env, conf),
            abbreviations held together (str, tmp) and prefixes (re, un).
            Without this file, or built with EVV_NO_PIECES, a cut uses
            chunks and letters only, which saves about 140 kilobytes and
            reads env as E N V and fdopen as F dopen.

The beginnings and endings are those of the Moby crossword and common word
lists, each kept where at least eight words have it, so that a loan like the
pf of pfennig does not make tmpfiles sayable. The words come from Moby's
crossword, common, name and place lists and from CMU's dictionary of 1993,
less every word CMU says letter by letter, which is an abbreviation and
should be cut. Moby was put in the public domain by Grady Ward; CMU said of
its 1993 release that any use is completely unrestricted.

usage: tools/module/sayable.py <lang dir> <moby dir> <cmudict.0.3>

  moby dir      https://www.gutenberg.org/files/3201/files/ : crosswd.txt,
                common.txt, names.txt, names-f.txt, names-m.txt, places.txt
  cmudict.0.3   https://www.gutenberg.org/files/3205/files/cmudict.txt

It rewrites the body of pieces and the whole of sayable, and leaves the hand
block of pieces as it is.
"""

import collections
import os
import re
import sys

VOWELS = set("aeiou")
SHORT = ("my", "in", "on", "of", "up", "go", "do", "no", "to", "at", "is",
         "it", "by")
# Common words that in technical text are nearly always the tail of a plural
# abbreviation, so that a cut would read cpus as C pus.
NOT_PIECES = ("pus",)
ONSET_ADD = ("gw", "sr", "shl", "shm", "ll", "mn")
ONSET_DROP = ("yttr", "ytt")
# Endings real words have, too rarely to pass the count: against, disc,
# harsh, Welsh, world, amongst. Without them const, desc and virsh are cut.
CODA_ADD = ("nst", "sc", "rsh", "lsh", "rld", "ngst")
LETTERS = {"a": "EY", "b": "B IY", "c": "S IY", "d": "D IY", "e": "IY",
           "f": "EH F", "g": "JH IY", "h": "EY CH", "i": "AY", "j": "JH EY",
           "k": "K EY", "l": "EH L", "m": "EH M", "n": "EH N", "o": "OW",
           "p": "P IY", "q": "K Y UW", "r": "AA R", "s": "EH S", "t": "T IY",
           "u": "Y UW", "v": "V IY", "w": "D AH B AH L Y UW", "x": "EH K S",
           "y": "W AY", "z": "Z IY"}


def vowel(w, i):
    c = w[i]
    return c in VOWELS or (c == "y" and i > 0 and w[i - 1] not in VOWELS)


def onset(w):
    j = 0
    while j < len(w) and not vowel(w, j):
        j += 1
    return w[:j], j < len(w)


def coda(w):
    k = len(w)
    while k > 0 and not vowel(w, k - 1):
        k -= 1
    return w[k:]


class Test:
    """The same test the engine makes, so that what this protects is exactly
    what the engine would otherwise cut."""

    def __init__(self, onsets, codas):
        self.onsets, self.codas = onsets, codas

    def medial(self, w):
        i, n = 0, len(w)
        while i < n and not vowel(w, i):
            i += 1
        while i < n:
            while i < n and vowel(w, i):
                i += 1
            j = i
            while j < n and not vowel(w, j):
                j += 1
            if j >= n:
                return True
            run = w[i:j]
            if not any(self.coda(run[:k]) and run[k:] in self.onsets
                       for k in range(len(run) + 1)):
                return False
            i = j
        return True

    def coda(self, c):
        if c in self.codas:
            return True
        n = "k" if c == "q" else re.sub(r"(.)\1+", r"\1", c)
        return n != c and n in self.codas

    def sayable(self, w):
        if not any(vowel(w, i) for i in range(len(w))):
            return False
        o, _ = onset(w)
        return (o == "" or o in self.onsets) and self.coda(coda(w)) \
            and self.medial(w)


def lines(path):
    return [l.strip() for l in open(path, encoding="latin-1")]


def table_words(lang):
    tag = os.path.basename(os.path.normpath(lang))
    out = set()
    for l in open(os.path.join(lang, "%s.dict" % tag), encoding="utf-8"):
        m = re.match(r"^  (\S+) \d", l)
        if m:
            out.add(m.group(1))
    return out


def cmu(path):
    """Each word of CMU's 1993 dictionary, and whether it is said letter by
    letter in every form CMU gives."""
    forms = collections.defaultdict(set)
    for l in open(path, encoding="latin-1"):
        if l.startswith("##") or "  " not in l:
            continue
        w, p = l.rstrip("\n").split("  ", 1)
        w = re.sub(r"\(\d\)$", "", w).lower()
        if not re.fullmatch(r"[a-z]{2,}", w):
            continue
        s = re.sub(r"\d", "", p)
        spelled = " ".join(LETTERS[c] for c in w)
        forms[w].add(s in (spelled, spelled.replace("D AH B AH L Y UW",
                                                    "D AH B Y UW")))
    return {w: all(k) for w, k in forms.items()}


CONTRACTIONS = {b + "nt" for b in "could would should did does has have had "
                "is are was were must might need ought dare ca wo do".split()}
# What is left of them where a curly apostrophe split the word.
CONTRACTIONS |= {b + "n" for b in "could would should did does has had is "
                 "are was were must might need ought dare".split()}
CONTRACTIONS |= {"cant", "wont", "dont", "aint", "shant", "im", "ive",
                 "youre", "theyre", "youve", "theyve", "weve", "hes", "shes",
                 "lets", "thats", "whats", "theres", "heres", "wheres",
                 "whos", "hows", "whys", "youll", "theyll", "itll", "hed",
                 "shed", "youd", "theyd", "wed", "yall"}


def hand_block(path):
    """What follows the body of pieces: remarks and words written by hand."""
    if not os.path.exists(path):
        return ""
    text = open(path, encoding="utf-8").read().split("\n")
    i = 0
    while i < len(text) and text[i].startswith("#"):
        i += 1
    while i < len(text) and text[i] and not text[i].startswith("#"):
        i += 1
    while i < len(text) and not text[i].startswith("#"):
        i += 1
    return "\n".join(text[i:]).rstrip("\n")


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    lang, moby, cmufile = argv
    real = set()
    for f in ("crosswd.txt", "common.txt"):
        real |= {w for w in lines(os.path.join(moby, f))
                 if re.fullmatch(r"[a-z]+", w)}
    names = set()
    for f in ("names.txt", "names-f.txt", "names-m.txt", "places.txt"):
        names |= {w.lower() for w in lines(os.path.join(moby, f))
                  if re.fullmatch(r"[A-Za-z]+", w)}

    on, co = collections.Counter(), collections.Counter()
    for w in real:
        o, has = onset(w)
        if has:
            on[o] += 1
            co[coda(w)] += 1
    onsets = {o for o, n in on.items() if n >= 8 and o} | set(ONSET_ADD)
    onsets -= set(ONSET_DROP)
    codas = {c for c, n in co.items() if n >= 8 and c} | set(CODA_ADD)
    test = Test(onsets | {""}, codas | {""})

    pieces = sorted(({w for w in lines(os.path.join(moby, "common.txt"))
                      if re.fullmatch(r"[a-z]{3,7}", w)} | set(SHORT))
                    - set(NOT_PIECES))
    said = cmu(cmufile)
    candidates = real | names | CONTRACTIONS | table_words(lang) \
        | {w for w, letters in said.items() if not letters}
    protect = sorted(w for w in candidates
                     if re.fullmatch(r"[a-z]{2,}", w)
                     and any(vowel(w, i) for i in range(len(w)))
                     and not test.sayable(w))

    d = os.path.join(lang, "dictionary")
    with open(os.path.join(d, "sayable"), "w", encoding="utf-8") as f:
        f.write("# What tells a word that can be said from one to be spelled, "
                "written by\n# tools/module/sayable.py, which says where each "
                "line comes from: onset\n# and coda lines are how a word may "
                "begin and end, word lines are words\n# that fail the test "
                "and are never cut. Not to be edited by hand.\n\n")
        for o in sorted(onsets):
            f.write("onset %s\n" % o)
        for c in sorted(codas):
            f.write("coda %s\n" % c)
        for w in protect:
            f.write("word %s\n" % w)
    path = os.path.join(d, "pieces")
    tail = hand_block(path)
    with open(path, "w", encoding="utf-8") as f:
        f.write("# Words a word that cannot be said may be cut into, one a "
                "line. The body is\n# written by tools/module/sayable.py out "
                "of Moby's common words of three to\n# seven letters and a "
                "few of two; blocks after it are written by hand, each\n# "
                "under a "
                "remark, and the tool leaves them alone.\n\n")
        f.write("\n".join(pieces) + "\n")
        if tail:
            f.write("\n" + tail + "\n")
    print("%s: %d onsets, %d codas, %d words kept whole, %d pieces"
          % (d, len(onsets), len(codas), len(protect), len(pieces)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
