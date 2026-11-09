#!/usr/bin/env python3
"""The language's lookup sets as words.

A lookup set is a run of keys, each one code per character in the alphabet
of the input statement's first field and a nul after it, sorted by those
codes, which is the order the engine's binary search walks: the vowels
before the consonants and the small letters before the capitals. Every set
English declares is one, and so is every set of the other languages: lists
of words the rules ask whether a run of letters is one of. The compound
rules ask three of them -- the words a compound may begin with, the words
it may end with, and the words that look like one and are not -- and the
rest are the endings, prefixes, homographs and names the rules know by name.

  dump <tag>     write lang/<tag>/<tag>.words out of delta_sets_<tag>.c
  build <tag>    lay every set the words file changes down again in
                 delta_sets_<tag>.c
  check <tag>    say whether the two agree

The words file holds a line `set <name>' for each set, in the order the
language installs them, and under it the set's words, one a line and
indented, in the order of the alphabet rather than of the codes; building
sorts them as the engine needs. A character the alphabet does not spell as
one ordinary letter is written in brackets, as `tools/module/dict.py' writes
it in <tag>.dict. A remark starts with #.

A set the words file names that the language does not declare yet is
added after the last one, under the next number, keyed by the input
statement as every set is; a rule asks it by that number.

Building with nothing changed writes nothing. A set that changed is laid
down where it was if it still fits there, and its table record says its new
size and count, so the change shows in the C as the one set. One that has
grown past where the next set starts has the whole store laid down again,
each set in the order it lay and on a four-byte boundary, rather than a copy
at the end with its old place left behind, which every such edit would add
to.
`tools/module/sets.py dump <tag>' then writes <tag>.sets out of the C, as
after `tools/module/dict.py' -- that and not `make tables-dump', which
writes the other text forms out of the C as well and does not give them back
as they are -- and `make tables-check' holds the forms to each other.
"""

import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from evv import ROOT, sibling  # noqa: E402

lex = sibling("module/lexicon")
sets_mod = sibling("module/sets")

ENTRY = 0x24

# The statement whose first field's alphabet a set's keys are spelled in:
# the input one, in every language.
INPUT = 1

HEAD = """# The lookup sets %(tag)s declares, as words. Written by
# tools/module/setwords.py out of lang/%(tag)s/delta_sets_%(tag)s.c and read
# back by it to lay a changed set down again.
#
# Each set is a line `set' and its name, and under it its words, one a line
# and indented, in the order of the alphabet; building sorts them into the
# order the engine searches in. A character the alphabet does not spell as
# one ordinary letter is written in brackets, by name where the name picks
# it out and by number where two codes share it. What each set is for is
# what the rules that ask it do with it: docs/rules.md says how to find
# them.
"""


def paths(tag):
    d = os.path.join(ROOT, "lang", tag)
    return (os.path.join(d, "delta_sets_%s.c" % tag),
            os.path.join(d, "delta_link_%s.c" % tag),
            os.path.join(d, "%s.words" % tag))


def alphabet(tag):
    _c, link, _w = paths(tag)
    alpha = lex.alphabets(open(link, encoding="utf-8").read())
    return alpha


def model(tag):
    c, _link, _w = paths(tag)
    return sets_mod.model_from_c(c)


def sets_of(m, alpha):
    """Each set as (slot, name, statement, keys as code lists)."""
    table, store = m["set_table"], m["sets"]["store"]
    out = []
    for slot, name, off in m["sets"]["at"]:
        e = table[slot * ENTRY:(slot + 1) * ENTRY]
        stmt, count, size = struct.unpack_from("<iii", e, 8)
        seg = store[off:off + size]
        keys = [list(k) for k in seg.split(b"\0")[:-1]]
        if len(keys) != count or not seg.endswith(b"\0"):
            raise SystemExit("setwords: %s is not %d keys in %d bytes"
                             % (name, count, size))
        out.append((slot, name, stmt, keys))
    return out


def plain_order(text):
    return (text.lower(), text)


def dump(tag):
    alpha = alphabet(tag)
    m = model(tag)
    lines = [HEAD % {"tag": tag}]
    n = 0
    for _slot, name, stmt, keys in sets_of(m, alpha):
        a = alpha[stmt]
        once = lex.unique_names(a)
        words = sorted((lex.word(k, a, once) for k in keys), key=plain_order)
        lines.append("set %s" % re.sub(r"_setentries$", "", name))
        lines.extend("  " + w for w in words)
        lines.append("")
        n += len(words)
    _c, _l, wpath = paths(tag)
    with open(wpath, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print("setwords: %d sets, %d words, written to %s"
          % (len(m["sets"]["at"]), n, os.path.relpath(wpath, ROOT)))
    return 0


def read_words(tag):
    _c, _l, wpath = paths(tag)
    out, cur = {}, None
    for n, raw in enumerate(open(wpath, encoding="utf-8"), 1):
        line = raw.rstrip("\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        where = "%s line %d" % (os.path.basename(wpath), n)
        if not line.startswith(" "):
            w = line.split()
            if len(w) != 2 or w[0] != "set":
                raise SystemExit("setwords: %s: a set is `set <name>'" % where)
            cur = w[1] + "_setentries"
            if cur in out:
                raise SystemExit("setwords: %s: %s twice" % (where, w[1]))
            out[cur] = []
            continue
        if cur is None:
            raise SystemExit("setwords: %s: a word before any set" % where)
        out[cur].append((line.strip(), where))
    return out


def keys_of(words, a, once):
    got = []
    for text, where in words:
        try:
            codes = tuple(lex.codes_of(text, a, once))
        except ValueError as e:
            raise SystemExit("setwords: %s: %s" % (where, e))
        if 0 in codes or not codes:
            raise SystemExit("setwords: %s: %r cannot be a key"
                             % (where, text))
        # A word twice does no harm to the search, and IBM's nouns
        # hold sabbatical twice.
        got.append(codes)
    return [list(k) for k in sorted(got)]


def wanted(tag, m, alpha):
    """Each set's keys as the words file says them, in the engine's order,
    and the sets it names that the language does not declare yet, in the
    order it names them."""
    words = read_words(tag)
    have = {name for _s, name, _o in m["sets"]["at"]}
    out = {}
    for slot, name, stmt, keys in sets_of(m, alpha):
        if name not in words:
            raise SystemExit("setwords: the words file has no set %s"
                             % name[:-len("_setentries")])
        a = alpha[stmt]
        out[slot] = (name, keys_of(words[name], a, lex.unique_names(a)), keys)
    a = alpha[INPUT]
    new = [(name, keys_of(words[name], a, lex.unique_names(a)))
           for name in words if name not in have]
    return out, new


def build(tag, write=True):
    alpha = alphabet(tag)
    m = model(tag)
    want, new = wanted(tag, m, alpha)
    store = bytes(m["sets"]["store"])
    table = bytearray(m["set_table"])
    at = list(m["sets"]["at"])
    starts = sorted({off for _s, _n, off in at} | {len(store)})
    changed, blobs, fits = [], {}, True
    for slot, name, off in at:
        _name, keys, old = want[slot]
        if keys == old:
            continue
        changed.append(name[:-len("_setentries")])
        blob = b"".join(bytes(k) + b"\0" for k in keys)
        blobs[slot] = blob
        if len(blob) > starts[starts.index(off) + 1] - off:
            fits = False
        struct.pack_into("<ii", table, slot * ENTRY + 0x0c, len(keys), len(blob))
    if fits:
        # Each where it lay, the rest of its room nought.
        out = bytearray(store)
        for slot, name, off in at:
            if slot in blobs:
                room = starts[starts.index(off) + 1] - off
                out[off:off + room] = blobs[slot] + bytes(room - len(blobs[slot]))
    else:
        # One that has grown past the next set is not copied to the end with
        # its old place left behind, which every edit would add to: the store
        # is laid down again, each set in the order it lay and on a four-byte
        # boundary, as tightly as the language's own compiler laid it.
        out = bytearray()
        for slot, name, off in sorted(at, key=lambda x: x[2]):
            out += bytes((-len(out)) % 4)
            size = struct.unpack_from("<i", m["set_table"], slot * ENTRY + 0x10)[0]
            blob = blobs.get(slot, store[off:off + size])
            at[slot] = (slot, name, len(out))
            out += blob
    if new:
        # A set the language does not declare yet goes after the last one,
        # under the next number, keyed by the input statement as every set
        # is, and the rules name it by that number. Its blob goes at the end
        # of the store.
        room = m.get("link", {}).get("set_room", 0) // ENTRY
        if len(at) + len(new) > room:
            raise SystemExit("setwords: %s has room for %d sets"
                             % (tag, room))
        for name, keys in new:
            out += bytes((-len(out)) % 4)
            blob = b"".join(bytes(k) + b"\0" for k in keys)
            entry = bytearray(ENTRY)
            struct.pack_into("<iii", entry, 8, INPUT, len(keys), len(blob))
            at.append((len(at), name, len(out)))
            table += entry
            out += blob
            changed.append(name[:-len("_setentries")])
            print("setwords: %s: set %s is new, number %d"
                  % (tag, name[:-len("_setentries")], len(at) - 1))
    if not changed:
        print("setwords: %s: every set is as the words file says" % tag)
        return 0
    if not write:
        print("setwords: %s: %d sets differ from the words file: %s"
              % (tag, len(changed), " ".join(changed[:12])))
        return 1
    m["sets"]["store"] = bytes(out)
    m["sets"]["at"] = at
    m["set_table"] = bytes(table)
    c, _l, _w = paths(tag)
    sets_mod.emit(m, c, tag)
    print("setwords: %s: %d sets laid down again: %s"
          % (tag, len(changed), " ".join(changed[:12])))
    print("setwords: now `tools/module/sets.py dump %s' to write %s.sets after it"
          % (tag, tag))
    return 0


def main(argv):
    if len(argv) == 2 and argv[0] == "dump":
        return dump(argv[1])
    if len(argv) == 2 and argv[0] == "build":
        return build(argv[1])
    if len(argv) == 2 and argv[0] == "check":
        return build(argv[1], write=False)
    sys.exit(__doc__)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
