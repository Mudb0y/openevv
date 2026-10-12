#!/usr/bin/env python3
"""Write rulex.dat, the RuLex lexicon in the form eci_russian.c reads it.

RuLex (https://github.com/poretsky/rulex) is Igor Poretsky's pronunciation
dictionary for Russian, made for his Ru_tts and used by RHVoice. It is under
the GNU GPL, version 2, which is why it is a file of its own beside the engine
rather than a table compiled into it: the engine reads it if it is there and
does without it if it is not. tools/russian/rulex/ holds RuLex's own source
files as they are published -- the test dictionary gzipped, since it is nine
megabytes -- and tools/russian/NOTICE says whose they are.

What goes in:

  every word form of RuLex's test dictionary, which is the forms its lexicon
  generates, some 430,000 of them, with the direct dictionary and its fixes
  over them, each with its pronunciation: RuLex respells as well as stresses,
  so что is што, солнце is сонце, компьютер is компьютэр, and a function word
  carries no stress at all;

  its prefix classifiers and its general rules, as text, for the engine to
  apply to a word it does not find: a prefix is cut off and what is left is
  looked up again, and failing that a general rule says where the stress is
  from the shape of the word.

The words are packed the way stress.py packs its table: a letter is a byte,
numbered from one in the order абвгдеёжзийклмнопрстуфхцчшщъыьэюя, sorted, each
word as how much it shares with the one before and the rest, every 32nd
starting afresh. Each word is followed by its pronunciation: one byte when the
pronunciation is the word's own letters with a stress -- the vowel the stress
is on counting from nought, 127 for no stress, 0x80 added when that vowel is
an е to be read as ё -- or 255 and then the pronunciation in full, a byte of
length and its letters, with 34 for the stress mark and 36 for a hyphen.

The file:

    "EVVRULEX" (8 bytes), then little-endian 32-bit words:
    words, blocks, bytes of words, bytes of rules
    the offset of each block
    the words
    the rules, UTF-8: lines of "#prefix" and "#general" sections, a prefix
    rule being its pattern, a space and what replaces the prefix

usage: rulex.py [--src DIR] [--out FILE]
"""

import argparse
import gzip
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "rulex")

VOWELS = "аеёиоуыэюя"
LETTERS = "абвгдеёжзийклмнопрстуфхцчшщъыьэюя"
STRESS, WEAK, HYPHEN = 34, 35, 36
BLOCK = 32


def read_koi(path):
    # The test dictionary is kept compressed; the rest as RuLex publishes it.
    if not os.path.exists(path) and os.path.exists(path + ".gz"):
        opener, path = gzip.open, path + ".gz"
    else:
        opener = open
    with opener(path, "rb") as f:
        for raw in f:
            line = raw.decode("koi8-r").rstrip("\r\n")
            if line.strip():
                yield line


def dictionary(path, into):
    for line in read_koi(path):
        parts = line.split(" ")
        if len(parts) != 2:
            continue
        word, pron = parts
        if not word or any(c not in LETTERS for c in word):
            continue
        if any(c not in LETTERS + "+=-" for c in pron):
            continue
        into[word] = pron


def code(word):
    return bytes(LETTERS.index(c) + 1 for c in word)


def pronunciation(word, pron):
    """The bytes that say how `word' is pronounced."""
    bare = pron.replace("+", "").replace("=", "")
    stress = None
    k = -1
    for c in pron:
        if c in VOWELS:
            k += 1
        elif c == "+":
            stress = k
            break
    if stress is None and "ё" in bare:
        stress = sum(c in VOWELS for c in bare[: bare.index("ё")])
    if bare == word and (stress is None or stress < 127):
        return bytes([127 if stress is None else stress])
    if (stress is not None and stress < 127 and "-" not in bare
            and len(bare) == len(word)):
        # The same letters but the stressed е written ё: еще and ещё.
        vowels = [i for i, c in enumerate(word) if c in VOWELS]
        if stress < len(vowels):
            at = vowels[stress]
            if word[at] == "е" and bare[at] == "ё" \
                    and word[:at] + "ё" + word[at + 1:] == bare:
                return bytes([stress | 0x80])
    out = bytearray()
    for c in pron:
        if c == "+":
            out.append(STRESS)
        elif c == "=":
            continue
        elif c == "-":
            out.append(HYPHEN)
        else:
            out.append(LETTERS.index(c) + 1)
    return bytes([255, len(out)]) + bytes(out)


def rules(src):
    out = ["#prefix"]
    for line in read_koi(os.path.join(src, "prefix.rules")):
        out.append(line.strip())
    out.append("#general")
    for line in read_koi(os.path.join(src, "general.rules")):
        out.append(line.strip())
    return ("\n".join(out) + "\n").encode("utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=SRC)
    ap.add_argument("--out", default=os.path.join(HERE, "rulex.dat"))
    args = ap.parse_args()

    words = {}
    dictionary(os.path.join(args.src, "lexicon.test"), words)
    dictionary(os.path.join(args.src, "explicit.dict"), words)
    dictionary(os.path.join(args.src, "fixes.dict"), words)

    rows = sorted((code(w), pronunciation(w, p)) for w, p in words.items()
                  if len(w) <= 255)
    data = bytearray()
    blocks = []
    previous = b""
    for n, (word, pron) in enumerate(rows):
        if n % BLOCK == 0:
            blocks.append(len(data))
            shared = 0
        else:
            shared = 0
            limit = min(len(word), len(previous), 255)
            while shared < limit and word[shared] == previous[shared]:
                shared += 1
        rest = word[shared:]
        data += bytes([shared, len(rest)]) + rest + pron
        previous = word
    text = rules(args.src)

    with open(args.out, "wb") as f:
        f.write(b"EVVRULEX")
        f.write(struct.pack("<4I", len(rows), len(blocks), len(data), len(text)))
        f.write(struct.pack("<%dI" % len(blocks), *blocks))
        f.write(data)
        f.write(text)
    print("rulex: %d words, %d bytes, into %s" % (len(rows), os.path.getsize(args.out), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
