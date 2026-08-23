#!/usr/bin/env python3
"""The machine's global variable list: lifted out of glob.obj, or written.

delta_new is six and a half thousand instructions of straight-line stores
because the compiler unrolled the loops that lay the variables out. What
they lay out is a list: one entry per variable, four kinds of entry. This
reads the stores back, works out what the list was, and writes it as data.

It also writes that list as text and reads it back, which is what a language
IBM never shipped needs: `lang/<tag>/<tag>.globals' is the declaration and
`regenerate' says whether the C written from it is the C in the tree, byte for
byte. Only the kinds are in the text. Where each variable lands and how big a
machine of the language is are worked out from them by the same walk the
engine does, so there is one source for the layout rather than two that can
disagree.

usage: gen-globals.py <glob.obj> <out.c>    lift and write the C
       gen-globals.py dump <tag>            write the text out of the objects
       gen-globals.py regenerate <tag>       the C from the text against the tree
       gen-globals.py write <tag>            the C from the text, for real
       gen-globals.py where <tag> <offset>.. which variable sits at a byte
"""

import os
import collections
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

INSN = re.compile(r"^\s+([0-9a-f]+):\s+(\S+)\s*(.*)$")
WIDTH = {"movl": 4, "movw": 2, "movb": 1}
FULL = {"ax": "eax", "cx": "ecx", "dx": "edx", "bx": "ebx", "si": "esi",
        "di": "edi", "al": "eax", "cl": "ecx", "dl": "edx", "bl": "ebx"}

# What delta_new works with: the block in esi, and four tables it allocates
# and stores at these offsets.
T_WORD, T_COMPOUND, T_LONG, T_SHORT = 0x14, 0x18, 0x1c, 0x20
DG_BASE = 0xb0


def widen(name):
    return FULL.get(name, name)


def parse_mem(tok):
    m = re.match(r"^(-?0x[0-9a-f]+)?\((%[a-z]+)\)$", tok.strip())
    if not m:
        return None
    return (m.group(2)[1:], int(m.group(1), 16) if m.group(1) else 0)


def replay(obj):
    """Run delta_new's stores against a shadow of what it writes.

    Registers hold one of ('imm', n), ('addr', off) for a pointer into the
    block, ('load', off) for something read back out of it, or ('alloc', k)
    for what the kth call returned. Every instruction in the function is
    one of these forms; anything else is a bug in this script rather than
    something to skip over."""
    text = subprocess.run(
        ["llvm-objdump", "-d", "--no-show-raw-insn",
         "--disassemble-symbols=_delta_new", obj],
        capture_output=True, text=True, check=True).stdout

    regs, pushes, home, nalloc = {}, [], {}, 0
    image = collections.defaultdict(dict)

    for line in text.splitlines():
        m = INSN.match(line)
        if not m:
            continue
        op, args = m.group(2), m.group(3).split("#")[0].strip()

        if op == "pushl":
            if args.startswith("$"):
                pushes.append(("imm", int(args[1:], 16)))
            elif args.startswith("%"):
                pushes.append(regs.get(widen(args[1:])))
            else:
                pushes.append(None)
            continue
        if op == "popl":
            v = pushes.pop() if pushes else None
            if args.startswith("%"):
                regs[widen(args[1:])] = v
            continue
        if op == "calll":
            pushes = []
            regs["ecx"] = regs["edx"] = None
            nalloc += 1
            regs["eax"] = ("alloc", nalloc)
            continue
        if op in ("jmp", "je", "jne", "cmpl", "testl", "retl", "addl"):
            continue
        if op == "xorl":
            a, b = [x.strip() for x in args.split(",")]
            regs[widen(a[1:])] = ("imm", 0) if a == b else None
            continue
        if op == "leal":
            src, dst = [x.strip() for x in args.split(", ")]
            mem = parse_mem(src)
            if not mem or mem[0] != "esi":
                raise SystemExit("unexpected leal: " + line)
            regs[widen(dst[1:])] = ("addr", mem[1])
            continue
        if op not in WIDTH:
            raise SystemExit("unexpected instruction: " + line)

        src, dst = [x.strip() for x in args.split(", ")]

        if dst.startswith("%") and "(" not in dst:
            r = widen(dst[1:])
            if src.startswith("%") and "(" not in src:
                regs[r] = regs.get(widen(src[1:]))
            elif src.startswith("$"):
                regs[r] = ("imm", int(src[1:], 16))
            else:
                mem = parse_mem(src)
                if not mem or mem[0] != "esi" or op != "movl":
                    raise SystemExit("unexpected load: " + line)
                regs[r] = ("load", mem[1])
            continue

        mem = parse_mem(dst)
        if not mem:
            raise SystemExit("unexpected store: " + line)
        reg, off = mem
        base = ("D", 0) if reg == "esi" else regs.get(reg)
        if base and base[0] == "alloc":
            base = ("load", home[base[1]])
        if base and base[0] == "addr":
            base, off = ("D", 0), base[1] + off
        if base is None:
            raise SystemExit("store through an unknown base: " + line)

        if src.startswith("$"):
            val = ("imm", int(src[1:], 16))
        elif src.startswith("%") and "(" not in src:
            val = regs.get(widen(src[1:]))
        else:
            raise SystemExit("unexpected source: " + line)
        if val is None:
            raise SystemExit("store of an unknown value: " + line)
        if val[0] == "alloc" and base[0] == "D":
            home[val[1]] = off

        image["D" if base[0] == "D" else base[1]][off] = (WIDTH[op], val)

    return image


def lift(image):
    block = image["D"]

    def table(which):
        t = image[which]
        return [t[o][1] for o in sorted(t)]

    # Each index holds its list twice; take one copy.
    def half(v):
        n = len(v) // 2
        if v[:n] != v[n:]:
            raise SystemExit("index is not two identical halves")
        return v[:n]

    words = [v[1] - 4 for v in half(table(T_WORD))]
    longs = [v[1] - 4 for v in half(table(T_LONG))]
    shorts = [v[1] - 2 for v in half(table(T_SHORT))]
    c = table(T_COMPOUND)
    comps = half([(c[i][1], c[i + 1][1], c[i + 2][1])
                  for i in range(0, len(c), 3)])

    cells = ([(a, "W", None) for a in words] + [(a, "L", None) for a in longs]
             + [(a, "S", None) for a in shorts]
             + [(a, "C", (i, n)) for a, i, n in comps])
    cells.sort()

    # Check the list reproduces every offset, so that nothing about the
    # layout is being taken on trust.
    size = {"W": 8, "L": 8, "S": 4}
    align = {"W": 4, "L": 4, "S": 2, "C": 2}
    at = DG_BASE
    for a, kind, extra in cells:
        # A compound of the seventh kind holds four-byte items and is laid on
        # a four-byte boundary; every other kind wants two. This is the only
        # thing about a compound that is not the same for all of them, and
        # four languages could not be lifted at all until it was found: their
        # layout came out two bytes short at the first such compound that
        # landed on an odd pair of bytes. Across all nine modules every one of
        # the thirty-six is four-aligned and no other kind's are, and the five
        # that lifted before this reproduce their committed data byte for
        # byte, which is what says it is this and not the payload rounding.
        want = 4 if kind == "C" and extra[0] == 6 else align[kind]
        at = (at + want - 1) & ~(want - 1)
        if at != a:
            raise SystemExit("layout diverges at 0x%x, expected 0x%x" % (a, at))
        at += size[kind] if kind != "C" else 4 + ((extra[1] + 1) & ~1)

    tag = {"W": -6, "L": -3, "S": -4}
    for a, kind, _ in cells:
        if kind == "C":
            continue
        seen = block[a][1][1]
        if seen >= 0x8000:
            seen -= 0x10000
        if seen != tag[kind]:
            raise SystemExit("tag at 0x%x is not %d" % (a, tag[kind]))

    return cells, at


def emit(cells, end, out, lang):
    # One language to a module, and the objects it was read from say which --
    # not the directory being written to, which used to name it and quietly
    # renamed every symbol when the output went anywhere but lang/<tag>. That
    # is the same trap delta-emit.py was fixed for.
    names = {"W": "DG_WORD", "L": "DG_LONG", "S": "DG_SHORT",
             "C": "DG_COMPOUND"}
    kinds = [names[k] for _, k, _ in cells]
    comps = [x for _, k, x in cells if k == "C"]

    f = open(out, "w")
    f.write("""/* The machine's global variables, as the language declares them.
 *
 * Generated by tools/gen-globals.py from glob.obj. Do not edit.
 *
 * One entry per variable, in the order the language declared them. The
 * order is what matters: it decides both where each variable lands in the
 * tail of delta_state and what its number is, because delta_new walks this
 * list once and numbers each kind as it goes.
 */

#include <stdint.h>
#include "delta.h"

""")
    f.write("const int8_t %s_delta_globals[] = {\n" % lang)
    for i in range(0, len(kinds), 6):
        f.write("    " + ", ".join(kinds[i:i + 6]) + ",\n")
    f.write("};\n\n")
    f.write("const int32_t %s_delta_globals_n = %d;\n\n"
            % (lang, len(kinds)))
    f.write("/* What the compound ones hold, in the same order they appear\n"
            "   in the list above. */\n")
    f.write("const delta_compound_decl %s_delta_compounds[] = {\n"
            % lang)
    for init, n in comps:
        f.write("    { %d, %d },\n" % (init, n))
    f.write("};\n\n")
    f.write("const int32_t %s_delta_compounds_n = %d;\n\n"
            % (lang, len(comps)))
    f.write("/* Where the last of those cells ends, which is how big one\n"
            "   machine of this language has to be. The struct in delta.h\n"
            "   stops at the named fields; the cells are the rest of an\n"
            "   allocation this size. */\n")
    f.write("const int32_t %s_delta_state_bytes = 0x%x;\n" % (lang, end))
    f.close()

    kept = collections.Counter(k for _, k, _ in cells)
    print("%d variables (%d word, %d long, %d short, %d compound), "
          "cells run to 0x%x"
          % (len(cells), kept["W"], kept["L"], kept["S"], kept["C"], end))


# ---- the same list as text ---------------------------------------------
#
# One line a kind, with how many of them in a row, because a language declares
# its variables in runs: English's 794 come out as a couple of hundred lines
# rather than 794. A compound is a line of its own, since each carries what it
# holds. Nothing here says where a variable lands: that is the walk's answer
# and the walk is above.

TEXT_KIND = {"W": "word", "L": "long", "S": "short"}
BACK_KIND = dict((v, k) for k, v in TEXT_KIND.items())

HEAD = """# The machine's global variables, as %(tag)s declares them.
#
# One line to a kind and how many in a row, in the order the language declares
# them, which is the order that decides both where each lands in the tail of
# the machine and what its number is. A compound says what it holds: the kind
# of item and how many.
#
# A `name\' line gives one of them a name of ours: the kind, then which one of
# that kind it is counting from nought, then what to call it. IBM\'s own names
# are gone -- the only record of the list is a disassembly that carries kinds
# and not names -- so these are what the language is worked out to mean, and a
# rule written in the upper form can then say `set f1_locus to 1000\' rather
# than naming a byte offset. Nothing here is required, and a name that is
# wrong is a wrong name rather than a wrong build.
#
# Written by tools/gen-globals.py. Where each variable lands and how big a
# machine of this language is are not in here -- they follow from the kinds,
# by the same walk delta_new does.
"""


def kept_names(path):
    """The `name\' lines already in a text, so that writing it again keeps
    them. They are worked out by hand and nothing can put them back."""
    if not os.path.exists(path):
        return []
    return [line.rstrip("\n") for line in open(path)
            if line.split()[:1] == ["name"]]


def write_text(cells, path, tag):
    names = kept_names(path)
    out = [HEAD % {"tag": tag}]
    run = None
    for _a, kind, extra in cells:
        if kind == "C":
            if run:
                out.append("%s %d" % run)
                run = None
            out.append("compound %d %d" % extra)
        elif run and run[0] == TEXT_KIND[kind]:
            run = (run[0], run[1] + 1)
        else:
            if run:
                out.append("%s %d" % run)
            run = (TEXT_KIND[kind], 1)
    if run:
        out.append("%s %d" % run)
    if names:
        out.append("")
        out.append("# What we have worked out the language means by some of"
                   " them.")
        out += names
    open(path, "w").write("\n".join(out) + "\n")
    return len(out) - 1


def read_text(path, want_names=False):
    """The declaration back as the cells the writer takes, with the layout
    walked again from the kinds alone.

    Asked for the names as well, it answers what each named variable is: where
    it sits and how wide it is, by the same walk. A name is against a kind and
    a number within that kind, which is how the machine itself numbers them.
    """
    kinds = []
    named = []
    for n, raw in enumerate(open(path), 1):
        line = raw.split("#")[0].strip()
        if not line:
            continue
        w = line.split()
        where = "%s line %d" % (os.path.basename(path), n)
        if w[0] == "name":
            if len(w) != 4:
                raise SystemExit("%s: a name says the kind, which one and"
                                 " what to call it" % where)
            if w[1] not in BACK_KIND:
                raise SystemExit("%s: no kind called %r" % (where, w[1]))
            named.append((BACK_KIND[w[1]], int(w[2]), w[3]))
        elif w[0] == "compound":
            if len(w) != 3:
                raise SystemExit("%s: a compound says what it holds" % where)
            kinds.append(("C", (int(w[1]), int(w[2]))))
        elif w[0] in BACK_KIND:
            if len(w) != 2:
                raise SystemExit("%s: say how many in the run" % where)
            kinds += [(BACK_KIND[w[0]], None)] * int(w[1])
        else:
            raise SystemExit("%s: no kind called %r" % (where, w[0]))

    size = {"W": 8, "L": 8, "S": 4}
    align = {"W": 4, "L": 4, "S": 2, "C": 2}
    cells = []
    at = DG_BASE
    for kind, extra in kinds:
        want = 4 if kind == "C" and extra[0] == 6 else align[kind]
        at = (at + want - 1) & ~(want - 1)
        cells.append((at, kind, extra))
        at += size[kind] if kind != "C" else 4 + ((extra[1] + 1) & ~1)
    if not want_names:
        return cells, at

    # Which cell each name means: the nth of its kind, counted in the order
    # the language declares them, which is the order delta_new numbers them.
    # A cell begins with the two bytes that say what kind it is and holds its
    # value after them: two bytes in for a short, which keeps its value where
    # a compiled location keeps its field, and four for a word or a long,
    # which keep it where one keeps its value. A rule names the value, so
    # that is what a name answers.
    seen = {}
    width = {"W": 4, "L": 4, "S": 2, "C": 4}
    value_at = {"W": 4, "L": 4, "S": 2, "C": 0}
    where_of = {}
    for a, kind, _extra in cells:
        n = seen.get(kind, 0)
        seen[kind] = n + 1
        where_of[(kind, n)] = a
    out = {}
    for kind, n, name in named:
        if (kind, n) not in where_of:
            raise SystemExit("this language has no %s number %d to call %r"
                             % (TEXT_KIND.get(kind, kind), n, name))
        out[name] = (where_of[(kind, n)] + value_at[kind], width[kind])
    return cells, at, out


def names_of(tag):
    """What this language calls the variables somebody has named."""
    path = text_path(tag)
    if not os.path.exists(path):
        return {}
    return read_text(path, want_names=True)[2]


def text_path(tag):
    return os.path.join(ROOT, "lang", tag, "%s.globals" % tag)


def out_path(tag):
    return os.path.join(ROOT, "lang", tag, "delta_globals_%s.c" % tag)


def dump(tag):
    obj = os.path.join(ROOT, "analysis", tag, "glob.obj")
    cells, _end = lift(replay(obj))
    n = write_text(cells, text_path(tag), tag)
    print("%d variables as %d lines in %s"
          % (len(cells), n, os.path.relpath(text_path(tag), ROOT)))
    return True


def regenerate(tag, write=False):
    import tempfile
    cells, end = read_text(text_path(tag))
    if write:
        emit(cells, end, out_path(tag), tag)
        print("%s written" % os.path.relpath(out_path(tag), ROOT))
        return True
    with tempfile.TemporaryDirectory() as tmp:
        made = os.path.join(tmp, "globals.c")
        emit(cells, end, made, tag)
        got = open(made, "rb").read()
    want = open(out_path(tag), "rb").read()
    if got == want:
        print("%s: %d bytes, the same as the tree's"
              % (os.path.basename(out_path(tag)), len(got)))
        return True
    print("%s differs: %d bytes against %d"
          % (os.path.basename(out_path(tag)), len(got), len(want)))
    a, b = got.split(b"\n"), want.split(b"\n")
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            print("  first line that differs is %d" % (i + 1))
            print("  made: %s" % a[i][:70])
            print("  tree: %s" % b[i][:70])
            break
    return False


def where(tag, offsets):
    """Which variable each byte offset is, as the kind and the number within
    it that a `name\' line uses. A rule names a variable by the offset, so
    this is how one is worked back to something that can be named."""
    cells, _end = read_text(text_path(tag))
    size = {"W": 8, "L": 8, "S": 4}
    seen = {}
    spans = []
    for a, kind, extra in cells:
        n = seen.get(kind, 0)
        seen[kind] = n + 1
        wide = size[kind] if kind != "C" else 4 + ((extra[1] + 1) & ~1)
        spans.append((a, a + wide, kind, n, extra))
    named = names_of(tag)
    by_off = dict((off, nm) for nm, (off, _w) in named.items())
    for text in offsets:
        off = int(text, 0)
        for a, end, kind, n, extra in spans:
            if a <= off < end:
                # A cell begins with the two bytes that say what kind it is,
                # so the value a rule writes is two bytes in. That is where a
                # rule names it, which is why this takes an offset inside a
                # cell rather than the cell's own start.
                print("%5d is %s number %d, %d bytes into it%s%s"
                      % (off, TEXT_KIND.get(kind, "compound"), n, off - a,
                         " (it holds %d of kind %d)" % (extra[1], extra[0])
                         if kind == "C" else "",
                         ", called %s" % by_off[off] if off in by_off else ""))
                break
        else:
            print("%5d is past the end of the variables" % off)
    return True


if __name__ == "__main__":
    if len(sys.argv) > 3 and sys.argv[1] == "where":
        sys.exit(0 if where(sys.argv[2], sys.argv[3:]) else 1)
    if len(sys.argv) == 3 and sys.argv[1] == "dump":
        sys.exit(0 if dump(sys.argv[2]) else 1)
    if len(sys.argv) == 3 and sys.argv[1] == "regenerate":
        sys.exit(0 if regenerate(sys.argv[2]) else 1)
    if len(sys.argv) == 3 and sys.argv[1] == "write":
        sys.exit(0 if regenerate(sys.argv[2], write=True) else 1)
    if len(sys.argv) != 3:
        print(__doc__.strip())
        sys.exit(2)
    obj, out = sys.argv[1], sys.argv[2]
    # analysis/<tag>/glob.obj, so the directory holding the object is the tag.
    tag = os.path.basename(os.path.dirname(os.path.abspath(obj)))
    emit(*lift(replay(obj)), out=out, lang=tag)
