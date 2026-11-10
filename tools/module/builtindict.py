#!/usr/bin/env python3
"""The dictionary a language carries, written as C out of lang/<tag>/dictionary.

A caller can load a dictionary of its own into an instance: a text file, one
entry a line, the word, a tab and what is said in its place. A language can
carry one in the same form, and an instance consults it after whatever the
caller has loaded, so everything the caller says still wins. This writes the
one a language carries.

lang/<tag>/dictionary holds it as text:

  main.dic, root.dic, abbr.dic
      the three volumes, in UTF-8, one entry a line; a line starting # is a
      remark. US English's began as the community's IBMTTSDictionaries and
      each file says how it is laid out
  split
      if it is there, a word with a capital inside it that no dictionary
      holds is read as the words it is made of; each line is a beginning
      that makes such a word a name to be left whole, Mc for McDonald
  fold
      if it is there, each line is a letter and what the language's
      normaliser writes in its place before any dictionary is asked, and a
      word with the letter in it is filed under what is written: English's
      niño as ninyo. A word written that way already keeps its own reading
  sayable, pieces
      if sayable is there, a word in small letters that no dictionary holds
      and that the language could not say is cut into words, chunks it can
      say and letters said by name; tools/module/sayable.py writes both and
      says what they hold. The pieces are compiled under EVV_NO_PIECES's
      absence, so a build that defines it carries the test without them

Each line is read the way the engine reads a file a caller loads -- a line
with no tab is no entry, white space round both halves goes, every white
space inside the replacement is a space, a word longer than 128 bytes and a
replacement longer than 512 are cut there -- and a repeated word keeps the
last reading of it, as a second file loaded into the same volume does. What
the engine checks besides, that every character is one it can name, it checks
when a word is found rather than here.

usage: tools/module/builtindict.py write <lang dir>
       tools/module/builtindict.py dump <lang dir> <volume>
"""

import os
import sys

VOLUMES = ("main", "root", "abbr")
WORD_MAX = 0x80
SAYS_MAX = 0x200


def parse(raw):
    """One line as the engine's loader reads it, or None where it drops it."""
    line = raw.rstrip(b"\r\n")
    line = line.lstrip(b" \t\r\n\v\f")
    if not line or b"\t" not in line:
        return None
    word, _, says = line.partition(b"\t")
    word = word.rstrip(b" \t\r\n\v\f")
    says = says.lstrip(b"\t ")
    says = bytes(32 if c in b" \t\r\n\v\f" else c for c in says).rstrip(b" ")
    if not word or not says:
        return None
    return word[:WORD_MAX], says[:SAYS_MAX]


def read_fold(here):
    """What the normaliser writes for each letter lang/<tag>/dictionary/fold
    names, as (letter, written) pairs in the Western set."""
    path = os.path.join(here, "fold")
    out = []
    if not os.path.exists(path):
        return out
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        if line.startswith("#") or not line.strip():
            continue
        f = line.split()
        if len(f) != 2:
            sys.exit("builtindict: %s line %d is not a letter and what is "
                     "written for it" % (path, n))
        out.append((f[0].encode("cp1252"), f[1].encode("cp1252")))
    return out


def read_dic(path):
    """A volume's entries in the order a repeated word is decided in: the
    words filed under what the normaliser writes first, so that one written
    that way already comes after it and keeps its reading."""
    written, folded = [], []
    if not os.path.exists(path):
        return written
    fold = read_fold(os.path.dirname(path))
    with open(path, "rb") as f:
        for n, raw in enumerate(f, 1):
            if raw.startswith(b"#"):
                continue
            try:
                raw = raw.decode("utf-8").encode("cp1252")
            except (UnicodeDecodeError, UnicodeEncodeError) as e:
                sys.exit("builtindict: %s line %d: %s" % (path, n, e))
            entry = parse(raw)
            if not entry:
                continue
            word = entry[0]
            for letter, written_as in fold:
                word = word.replace(letter, written_as)
            if word == entry[0]:
                written.append(entry)
            else:
                folded.append((word[:WORD_MAX], entry[1]))
    return folded + written


def volumes(lang):
    """Each volume as the build lays it down: word to reading, sorted."""
    here = os.path.join(lang, "dictionary")
    return {vol: sorted(dict(read_dic(os.path.join(here, vol + ".dic"))).items())
            for vol in VOLUMES}


# A volume's file opens with remarks, then a blank line, then the body: the
# entries the tools write, sorted and with no remark among them. Whatever
# follows the body was decided by hand, each block under a remark saying
# why, and the tools read it but never write it. The build reads the lot.

def layout(path):
    """A volume's file as (head, body, tail): the opening remarks with the
    blank line after them, the body as a word to reading dict of text, and
    the rest verbatim."""
    lines = open(path, encoding="utf-8").read().split("\n")
    at = lines.index("") + 1
    end = at
    while end < len(lines) and lines[end] and not lines[end].startswith("#"):
        end += 1
    body = {}
    for line in lines[at:end]:
        entry = parse(line.encode("utf-8"))
        if entry:
            body[entry[0].decode("utf-8")] = entry[1].decode("utf-8")
    return ("\n".join(lines[:at]) + "\n", body,
            "\n".join(lines[end:]).rstrip("\n"))


def write_layout(path, head, body, tail):
    rows = sorted(body.items(), key=lambda e: (e[0].lower(), e[0]))
    text = head + "".join("%s\t%s\n" % r for r in rows)
    if tail:
        text += tail + "\n"
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def hand_words(path):
    """The words a volume's blocks decided by hand say something for."""
    _head, _body, tail = layout(path)
    return {parse(line.encode("utf-8"))[0].decode("utf-8")
            for line in tail.split("\n")
            if not line.startswith("#") and parse(line.encode("utf-8"))}


def read_removed(lang):
    """lang/<tag>/dictionary/removed as (volume, word) to (reading, why)."""
    path = os.path.join(lang, "dictionary", "removed")
    out = {}
    if not os.path.exists(path):
        return out
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        if line.startswith("#") or not line.strip():
            continue
        f = line.rstrip("\n").split("\t")
        if len(f) != 4 or f[0] not in VOLUMES:
            sys.exit("builtindict: %s line %d is not a volume, a word, a "
                     "reading and why" % (path, n))
        out[(f[0], f[1])] = (f[2], f[3])
    return out


def add_removed(lang, rows):
    """Append (volume, word, reading, why) rows to removed, kept sorted."""
    path = os.path.join(lang, "dictionary", "removed")
    text = open(path, encoding="utf-8").read()
    head = text[:text.index("\n\n") + 2]
    have = read_removed(lang)
    for vol, word, says, why in rows:
        have[(vol, word)] = (says, why)
    with open(path, "w", encoding="utf-8") as f:
        f.write(head)
        for (vol, word), (says, why) in sorted(
                have.items(), key=lambda e: (VOLUMES.index(e[0][0]),
                                             e[0][1].lower(), e[0][1])):
            f.write("%s\t%s\t%s\t%s\n" % (vol, word, says, why))


def read_split(lang):
    """The beginnings that leave a word whole, or None where the language
    does not split words at all."""
    path = os.path.join(lang, "dictionary", "split")
    if not os.path.exists(path):
        return None
    out = []
    with open(path, "rb") as f:
        for n, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith(b"#"):
                continue
            try:
                out.append(line.decode("utf-8").encode("cp1252"))
            except (UnicodeDecodeError, UnicodeEncodeError) as e:
                sys.exit("builtindict: %s line %d: %s" % (path, n, e))
    return out


def read_sayable(lang):
    """The onsets, codas and words kept whole, or None where the language
    cuts nothing."""
    path = os.path.join(lang, "dictionary", "sayable")
    if not os.path.exists(path):
        return None
    out = {"onset": [], "coda": [], "word": []}
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        f = line.split()
        if not f or f[0].startswith("#"):
            continue
        if len(f) != 2 or f[0] not in out:
            sys.exit("builtindict: %s line %d: %r" % (path, n, line.strip()))
        out[f[0]].append(f[1].encode("ascii"))
    return out


def read_pieces(lang):
    path = os.path.join(lang, "dictionary", "pieces")
    if not os.path.exists(path):
        return []
    return [l.strip().encode("ascii") for l in open(path, encoding="utf-8")
            if l.strip() and not l.startswith("#")]


def wordset(name, words, lines):
    """A set as the engine reads it: one block of text, each word ended by a
    null and in byte order, and where every sixteenth word begins."""
    words = sorted(set(words))
    if not words:
        return "{ 0, 0, 0 }"
    lines.append("static const char %s_words[] =" % name)
    offsets, at = [], 0
    for i, w in enumerate(words):
        if i % 16 == 0:
            offsets.append(at)
        at += len(w) + 1
    for i in range(0, len(words), 8):
        lines.append("    " + " ".join('"%s\\0"' % w.decode() for w in words[i:i + 8]))
    lines[-1] += ";"
    lines.append("static const uint32_t %s_index[] = {" % name)
    for i in range(0, len(offsets), 12):
        lines.append("    " + ", ".join(str(o) for o in offsets[i:i + 12]) + ",")
    lines.append("};")
    lines.append("")
    return "{ %s_words, %s_index, %d }" % (name, name, len(words))


def literal(b):
    """A C string literal for the bytes, octal past ASCII and a question mark
    escaped so that no pair of them reads as a trigraph."""
    s = []
    for c in b:
        if c in (0x22, 0x5c):
            s.append("\\" + chr(c))
        elif c == 0x3f:
            s.append("\\?")
        elif 0x20 <= c < 0x7f:
            s.append(chr(c))
        else:
            s.append("\\%03o" % c)
    return '"' + "".join(s) + '"'


def write(lang):
    tag = os.path.basename(os.path.normpath(lang))
    vols = volumes(lang)
    path = os.path.join(lang, "delta_builtin_%s.c" % tag)
    lines = ["/* Generated by tools/module/builtindict.py out of",
             "   %s/dictionary. Do not edit. */" % lang.rstrip("/"),
             "",
             '#include "delta_lang.h"',
             ""]
    names = []
    for vol in VOLUMES:
        if not vols[vol]:
            names.append(("0", 0))
            continue
        name = "%s_builtin_%s" % (tag, vol)
        lines.append("static const delta_dict_entry %s[] = {" % name)
        for word, says in vols[vol]:
            lines.append("    { %s, %s }," % (literal(word), literal(says)))
        lines.append("};")
        lines.append("")
        names.append((name, len(vols[vol])))
    whole = read_split(lang)
    if whole:
        lines.append("static const char *const %s_builtin_whole[] = {" % tag)
        for w in whole:
            lines.append("    %s," % literal(w))
        lines.append("    0")
        lines.append("};")
        lines.append("")
    say = read_sayable(lang)
    sets = ["{ 0, 0, 0 }"] * 4
    if say:
        sets[0] = wordset("%s_builtin_onset" % tag, say["onset"], lines)
        sets[1] = wordset("%s_builtin_coda" % tag, say["coda"], lines)
        sets[2] = wordset("%s_builtin_kept" % tag, say["word"], lines)
        lines.append("#ifndef EVV_NO_PIECES")
        sets[3] = wordset("%s_builtin_pieces" % tag, read_pieces(lang), lines)
        lines.append("#define %s_BUILTIN_PIECES %s" % (tag.upper(), sets[3]))
        lines.append("#else")
        lines.append("#define %s_BUILTIN_PIECES { 0, 0, 0 }" % tag.upper())
        lines.append("#endif")
        lines.append("")
        sets[3] = "%s_BUILTIN_PIECES" % tag.upper()
    lines.append("const delta_builtin_dict %s_builtin_dict = {" % tag)
    lines.append("    { %s }," % ", ".join(n for n, _c in names))
    lines.append("    { %s }," % ", ".join(str(c) for _n, c in names))
    lines.append("    %d," % (whole is not None))
    lines.append("    %s," % ("%s_builtin_whole" % tag if whole else "0"))
    for st in sets:
        lines.append("    %s," % st)
    lines.append("};")
    text = "\n".join(lines) + "\n"
    if os.path.exists(path) and open(path).read() == text:
        return
    with open(path, "w") as f:
        f.write(text)
    print("%s: %s%s%s" % (path, ", ".join("%d %s" % (len(vols[v]), v)
                                           for v in VOLUMES),
                           "" if whole is None else ", split",
                           "" if not say else ", sayable"))


def main(argv):
    if len(argv) == 2 and argv[0] == "write":
        write(argv[1])
        return 0
    if len(argv) == 3 and argv[0] == "dump" and argv[2] in VOLUMES:
        out = sys.stdout.buffer
        for word, says in volumes(argv[1])[argv[2]]:
            out.write(word + b"\t" + says + b"\n")
        return 0
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
