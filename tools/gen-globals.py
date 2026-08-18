#!/usr/bin/env python3
"""Lift the machine's global variable list out of glob.obj.

delta_new is six and a half thousand instructions of straight-line stores
because the compiler unrolled the loops that lay the variables out. What
they lay out is a list: one entry per variable, four kinds of entry. This
reads the stores back, works out what the list was, and writes it as data.

Usage: gen-globals.py analysis/enus/glob.obj src/delta_globals_enus.c
"""

import collections
import re
import subprocess
import sys

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
        at = (at + align[kind] - 1) & ~(align[kind] - 1)
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


def emit(cells, end, out):
    names = {"W": "DG_WORD", "L": "DG_LONG", "S": "DG_SHORT",
             "C": "DG_COMPOUND"}
    kinds = [names[k] for _, k, _ in cells]
    comps = [x for _, k, x in cells if k == "C"]

    f = open(out, "w")
    f.write("""/* The machine's global variables, as English declares them.
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
    f.write("const int8_t delta_globals[] = {\n")
    for i in range(0, len(kinds), 6):
        f.write("    " + ", ".join(kinds[i:i + 6]) + ",\n")
    f.write("};\n\n")
    f.write("const int32_t delta_globals_n = %d;\n\n" % len(kinds))
    f.write("/* What the compound ones hold, in the same order they appear\n"
            "   in the list above. */\n")
    f.write("const delta_compound_decl delta_compounds[] = {\n")
    for init, n in comps:
        f.write("    { %d, %d },\n" % (init, n))
    f.write("};\n\n")
    f.write("const int32_t delta_compounds_n = %d;\n" % len(comps))
    f.close()

    kept = collections.Counter(k for _, k, _ in cells)
    print("%d variables (%d word, %d long, %d short, %d compound), "
          "cells run to 0x%x"
          % (len(cells), kept["W"], kept["L"], kept["S"], kept["C"], end))


if __name__ == "__main__":
    obj, out = sys.argv[1], sys.argv[2]
    emit(*lift(replay(obj)), out=out)
