#!/usr/bin/env python3
"""A whole language module read out of ETI Eloquence 6.1 as Apple ships it.

Apple's modules carry everything a language is beside its rules -- the
variables, the statement table, the sets and dictionaries, the voices and the
phonemes -- as 64-bit data and code compiled from the same Delta output IBM's
4.3 objects were. tools/rules/lift64.py reads the rules; this reads the rest
and writes it in the text forms every module in the tree already has, through
the same writers, so a language lifted from 6.1 is text like any other and
`make tables-check' holds it the same way.

Three things differ from what IBM's objects hold, and each is said where it is
read. A pointer is eight bytes and is stored in Apple's chained-fixup form,
which for a rebase is the target in the low 36 bits. A cell is sixteen bytes
with its value eight in, where ours is eight with it four in, so every
variable is renumbered by its place in the list rather than carried across by
offset. And 6.1 declares a fifth kind of variable, a double, which the machine
here keeps as ten-thousandths (DG_FIXED in src/delta/delta.h).

usage: tools/module/lift61.py <module.dylib> <tag> globals|statements|sets|rules|settings
"""

import collections
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from evv import ROOT, sibling

dgl = sibling("module/globals")


LANG_NAMES = {"fifi": "Finnish", "ptbr": "Brazilian Portuguese",
              "kokr": "Korean", "zhcn": "Mandarin Chinese",
              "zhtw": "Taiwanese Mandarin"}

# The family and dialect each is, which are 6.1's own numbers -- the sections
# of the eci.ini Apple's engine reads -- and the library name its settings
# carry.
LANG_NUMBERS = {"fifi": (9, 0, "Static Engine FIN"),
                "ptbr": (7, 0, "Static Engine PTB")}


# ---- reading the module ---------------------------------------------------


class Module:
    """The bytes, the symbols and the code of one thin 64-bit module."""

    def __init__(self, path):
        self.path = path
        self.bytes = open(path, "rb").read()
        nm = subprocess.run(["llvm-nm", "-n", path], check=True,
                            capture_output=True, text=True).stdout
        self.symbol = {}
        for line in nm.splitlines():
            w = line.split()
            if len(w) == 3:
                self.symbol.setdefault(w[2].lstrip("_"), int(w[0], 16))
        self._code = {}

    def q(self, a):
        return int.from_bytes(self.bytes[a:a + 8], "little")

    def word(self, a, signed=True):
        return int.from_bytes(self.bytes[a:a + 4], "little", signed=signed)

    def half(self, a, signed=True):
        return int.from_bytes(self.bytes[a:a + 2], "little", signed=signed)

    def pointer(self, a):
        """What a pointer-sized slot points at, or None. A rebase in Apple's
        chained fixups keeps its target in the low 36 bits; a bind, which is
        a symbol of another library, has the top bit set and is nothing a
        language's own tables point at."""
        v = self.q(a)
        if v >> 63:
            return None
        v &= (1 << 36) - 1
        return v or None

    def cstring(self, a):
        if a is None:
            return None
        end = self.bytes.index(b"\0", a)
        return self.bytes[a:end].decode("latin-1")

    def code(self, name):
        """One function's instructions, as (address, mnemonic, operands,
        the address a comment names)."""
        if name not in self._code:
            text = subprocess.run(
                ["llvm-objdump", "-d", "--no-show-raw-insn",
                 "--disassemble-symbols=_" + name, self.path],
                check=True, capture_output=True, text=True).stdout
            out = []
            for line in text.splitlines():
                m = re.match(r"^\s+([0-9a-f]+):\s+(\S+)\s*(.*?)\s*$", line)
                if not m:
                    continue
                rest, note = m.group(3), None
                if "##" in rest:
                    rest, _, c = rest.partition("##")
                    n = re.search(r"0x([0-9a-f]+)", c)
                    if n and "(%rip)" in rest:
                        note = int(n.group(1), 16)
                    rest = rest.strip()
                out.append((int(m.group(1), 16), m.group(2),
                            split_operands(rest), note))
            self._code[name] = out
        return self._code[name]


def split_operands(text):
    parts, depth, start = [], 0, 0
    for i, ch in enumerate(text):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == "," and depth == 0:
            parts.append(text[start:i].strip())
            start = i + 1
    if text[start:].strip():
        parts.append(text[start:].strip())
    return parts


REG = {}
for _n in "abcd":
    for _f in ("%sl" % _n, "%sx" % _n, "e%sx" % _n, "r%sx" % _n):
        REG[_f] = "r%sx" % _n
for _n in ("si", "di", "bp", "sp"):
    for _f in (_n + "l", _n, "e" + _n, "r" + _n):
        REG[_f] = "r" + _n
for _k in range(8, 16):
    for _f in ("r%d" % _k, "r%dd" % _k, "r%dw" % _k, "r%db" % _k):
        REG[_f] = "r%d" % _k


def reg(t):
    t = t.lstrip("%")
    return REG.get(t, t)


# ---- the variables --------------------------------------------------------
#
# delta_new allocates the machine and then, unrolled, writes every cell's tag
# and five tables of pointers to the values in it: one table a kind, each
# holding its list twice. The tables are what say which cell is which kind, so
# they are what is read; the tags are checked against them.

STORE = {"movq": 8, "movl": 4, "movw": 2, "movb": 1, "andq": 8, "movups": 16,
         "movaps": 16, "movabsq": 8}
MEM = re.compile(r"^(-?0x[0-9a-f]+)?\((%[a-z0-9]+)\)$")

# Where delta_new keeps each kind's table in the machine, and what a pointer
# in it points at: the value, which is this far into the cell.
TABLES = {0x18: ("W", 8), 0x20: ("C", None), 0x28: ("L", 8), 0x30: ("S", 2),
          0x38: ("F", 8)}
TAG = {"W": -6, "L": -3, "S": -4, "F": -5}


def replay_delta_new(m):
    """What delta_new writes, by block: the machine is the first block
    allocated and each table one after it."""
    regs, stack, spill = {}, [], {}
    image = collections.defaultdict(dict)
    nalloc = 0

    def place(tok):
        mm = MEM.match(tok)
        if not mm:
            raise SystemExit("lift61: delta_new writes through %s" % tok)
        r = reg(mm.group(2))
        k = int(mm.group(1), 16) if mm.group(1) else 0
        if r == "rbp":
            return ("spill", k)
        b = regs.get(r)
        if not b or b[0] != "ptr":
            raise SystemExit("lift61: delta_new writes through %s, which"
                             " holds %r" % (tok, b))
        return (b[1], b[2] + k)

    for _a, op, o, note in m.code("delta_new"):
        if op == "callq":
            nalloc += 1
            regs = {"rbx": regs.get("rbx")}
            regs["rax"] = ("ptr", nalloc, 0)
            continue
        if op in ("je", "jne", "jmp", "testq", "retq", "subq"):
            continue
        if op == "pushq":
            stack.append(("imm", int(o[0][1:], 16)) if o[0].startswith("$")
                         else regs.get(reg(o[0])))
            continue
        if op == "popq":
            regs[reg(o[0])] = stack.pop() if stack else None
            continue
        if op == "xorl":
            regs[reg(o[1])] = ("imm", 0)
            continue
        if op == "addq":
            v = regs.get(reg(o[1]))
            if v and v[0] == "ptr":
                regs[reg(o[1])] = ("ptr", v[1], v[2] + int(o[0][1:], 16))
            continue
        if op == "leaq":
            if "(%rip)" in o[0]:
                regs[reg(o[1])] = ("rip", note)
            else:
                p = place(o[0])
                regs[reg(o[1])] = ("ptr", p[0], p[1]) if p[0] != "spill" \
                    else None
            continue
        if op not in STORE:
            raise SystemExit("lift61: delta_new does %s" % op)
        src, dst = o
        if dst.startswith("%"):
            d = dst if dst.startswith("%xmm") else reg(dst)
            if src.startswith("$"):
                regs[d] = ("imm", int(src[1:], 16))
            elif src.startswith("%"):
                regs[d] = regs.get(src if src.startswith("%xmm") else reg(src))
            elif "(%rip)" in src:
                regs[d] = ("bytes", m.bytes[note:note + STORE[op]])
            else:
                p = place(src)
                if p[0] == "spill":
                    regs[d] = spill.get(p[1])
                else:
                    v = image[p[0]].get(p[1])
                    regs[d] = v[1] if v else None
            continue
        p = place(dst)
        if src.startswith("$"):
            val = ("imm", int(src[1:], 16))
        elif op == "andq":
            val = ("imm", 0)
        else:
            val = regs.get(src if src.startswith("%xmm") else reg(src))
        if p[0] == "spill":
            spill[p[1]] = val
        else:
            image[p[0]][p[1]] = (STORE[op], val)
    return image


def cells_61(m):
    """6.1's variables in the order they are declared, each as its cell's
    offset in 6.1's machine, its kind, and for a compound what it holds."""
    image = replay_delta_new(m)
    state = image[1]
    by_kind = {}
    for off, (kind, back) in TABLES.items():
        v = state.get(off)
        if not v or not v[1] or v[1][0] != "ptr":
            raise SystemExit("lift61: delta_new keeps no table at 0x%x" % off)
        t = image[v[1][1]]
        entries = [t[o][1] for o in sorted(t)]
        n = len(entries) // 2
        if entries[:n] != entries[n:]:
            raise SystemExit("lift61: the table at 0x%x is not two halves"
                             % off)
        by_kind[kind] = (entries[:n], back)
    cells = []
    for kind in ("W", "L", "S", "F"):
        entries, back = by_kind[kind]
        for e in entries:
            cells.append((e[2] - back, kind, None))
    # A compound's entry is the cell and then its kind and size as one word.
    entries, _ = by_kind["C"]
    for k in range(0, len(entries), 2):
        cell, packed = entries[k], entries[k + 1][1]
        cells.append((cell[2], "C", (packed & 0xffffffff, packed >> 32)))
    cells.sort()

    # The layout walked again from the kinds alone, so that nothing about
    # 6.1's is taken on trust: a cell is sixteen bytes on an eight-byte
    # boundary, a short four on two, and a compound sixteen and its bytes on
    # eight.
    at = cells[0][0]
    for a, kind, extra in cells:
        want = 2 if kind == "S" else 8
        at = (at + want - 1) & ~(want - 1)
        if at != a:
            raise SystemExit("lift61: 6.1's layout diverges at 0x%x, where"
                             " 0x%x was expected" % (a, at))
        at += {"S": 4}.get(kind, 16) if kind != "C" else \
            (16 + extra[1] + 7) & ~7
        if kind in TAG:
            got = state.get(a)
            if got and got[1] and got[1][0] == "imm":
                tag = got[1][1] & 0xffff
                if tag != TAG[kind] & 0xffff:
                    raise SystemExit("lift61: the cell at 0x%x is tagged %d"
                                     " and listed as %s" % (a, tag, kind))
    return cells


def varmap(m, tag, where=None):
    """Where each of 6.1's variables is in ours: its value's offset in 6.1's
    machine to its value's offset in ours, with the width a rule reads it at.
    A compound maps its whole run, since a rule may read inside one."""
    ours, _end = dgl.read_text(where or dgl.text_path(tag))
    theirs = cells_61(m)
    if [(k, x) for _a, k, x in ours] != [(k, x) for _a, k, x in theirs]:
        raise SystemExit("lift61: %s.globals does not declare 6.1's"
                         " variables" % tag)
    out = {}
    for (a61, kind, extra), (a, _k, _x) in zip(theirs, ours):
        if kind == "S":
            out[a61 + 2] = (a + 2, 2)
            out[a61] = (a, 0)
        elif kind == "C":
            for k in range(extra[1]):
                out[a61 + 16 + k] = (a + 4 + k, 1)
            out[a61 + 2] = (a + 2, 2)
            out[a61] = (a, 0)
        else:
            out[a61 + 8] = (a + 4, 4)
            out[a61 + 2] = (a + 2, 2)
            out[a61] = (a, 0)
    # The few of the machine's own fields a rule reads, which sit where IBM's
    # layout put them here rather than where 6.1's does.
    out.update(MACHINE)
    return out


# 6.1's offset of a field of the machine a rule reads, and ours.
MACHINE = {0x120: (0x98, 1)}     # how many statement types


def write_globals(m, tag):
    cells = cells_61(m)
    path = dgl.text_path(tag)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    n = dgl.write_text(cells, path, tag)
    kinds = collections.Counter(k for _a, k, _x in cells)
    print("%d variables as %d lines in %s: %s"
          % (len(cells), n, os.path.relpath(path, ROOT),
             ", ".join("%d %s" % (kinds[k], dgl.TEXT_KIND.get(k, "compound"))
                       for k in ("W", "L", "S", "F", "C"))))


# ---- the statement table -------------------------------------------------
#
# The same table IBM's link object holds, with every pointer eight bytes: an
# entry is 0x60 bytes where IBM's is 0x40, a field's description 0x28 where
# IBM's is 0x18, and the two words IBM never named are eight bytes each. A
# reader and a writer are each a function of one instruction, an offset into
# the record, as in IBM's. What is new is a field 6.1 keeps as a double; the
# record keeps 6.1's layout, and the eight bytes it had hold a fixed value in
# the first four here, which is what kind -5 says to the machine.

ENTRY, FIELD = 0x60, 0x28
FIXED_KIND = -5


def accessor(m, name):
    """What a reader adds to the record it is handed."""
    for _a, op, o, _n in m.code(name):
        if op == "leaq" and reg(o[1]) == "rax":
            mm = MEM.match(o[0])
            if mm and reg(mm.group(2)) == "rdi":
                return int(mm.group(1), 16) if mm.group(1) else 0
        if op == "movq" and o == ["%rdi", "%rax"]:
            return 0
    raise SystemExit("lift61: %s is not a plain reader" % name)


def writer(m, name):
    """The offset and the width a writer copies a field with."""
    for _a, op, o, _n in m.code(name):
        if op in ("movb", "movw", "movl", "movq", "movsd") and \
                len(o) == 2 and "(%rdi)" in o[1]:
            mm = MEM.match(o[1])
            off = int(mm.group(1), 16) if mm.group(1) else 0
            return off, {"movb": 1, "movw": 2, "movl": 4, "movq": 8,
                         "movsd": 8}[op]
    raise SystemExit("lift61: %s is not a plain writer" % name)


def as_fixed(raw):
    """A double as the whole number the machine keeps it as, in the units the
    lifter writes the rules in, so that a field's fresh value and a rule's
    constant are on one scale."""
    return sibling("rules/lift64").as_fixed(raw)


def name_of(m, addr):
    for nm, a in m.symbol.items():
        if a == addr:
            return nm
    return None


def statements_61(m):
    base = m.symbol["vstmtbl"]
    names = sorted(set(m.symbol.values()))

    def extent(a):
        after = [x for x in names if x > a]
        return after[0] - a

    stmts = []
    nstmts = None
    for _a, op, o, _n in m.code("link_new"):
        if op == "movb" and o[1] == "0x120(%rbx)":
            nstmts = int(o[0][1:], 16)
    if nstmts is None:
        raise SystemExit("lift61: link_new says nothing of how many types")
    for i in range(nstmts):
        at = base + i * ENTRY
        e = {"name": m.cstring(m.pointer(at))}
        e["u18"] = m.word(at + 0x30)
        e["u1c"] = m.word(at + 0x38)
        nfields = m.word(at + 0x40)
        e["length"] = m.word(at + 0x44)
        e["stride"] = m.word(at + 0x48)
        e["varlen"] = m.word(at + 0x4c)
        e["whole"] = m.word(at + 0x50)
        e["marks"] = list(m.bytes[at + 0x54:at + 0x58])
        e["u38"] = m.word(at + 0x58)
        e["u3c"] = m.word(at + 0x5c)
        fields, gets, puts = (m.pointer(at + 8), m.pointer(at + 0x10),
                              m.pointer(at + 0x18))
        fresh = bytearray(m.bytes[m.pointer(at + 0x28):
                                  m.pointer(at + 0x28) + e["length"]]) \
            if m.pointer(at + 0x28) else bytearray(e["length"])
        e["field"] = []
        for k in range(nfields):
            fa = fields + k * FIELD
            f = {"name": m.cstring(m.pointer(fa)),
                 "format": m.cstring(m.pointer(fa + 8)),
                 "u0c": m.word(fa + 0x18),
                 "kind": m.half(fa + 0x1e),
                 "flag": m.bytes[fa + 0x20]}
            nvalues = m.half(fa + 0x1c)
            f["read"] = accessor(m, name_of(m, m.pointer(gets + 8 * k)))
            put = m.pointer(puts + 8 * k)
            f["write"] = None
            if put:
                off, width = writer(m, name_of(m, put))
                if width == 8:
                    if f["kind"] != FIXED_KIND:
                        raise SystemExit("lift61: %s.%s is eight bytes and"
                                         " not a double" % (e["name"],
                                                            f["name"]))
                    width = 4
                    raw = bytes(fresh[off:off + 8])
                    fresh[off:off + 8] = \
                        as_fixed(raw).to_bytes(4, "little", signed=True) \
                        + bytes(4)
                f["write"] = (off, width)
            values = m.pointer(fa + 0x10)
            if values:
                f["values"] = [m.cstring(m.pointer(values + 8 * j))
                               for j in range(nvalues)]
            else:
                f["values"] = None
                if nvalues:
                    f["nvalues"] = nvalues
            e["field"].append(f)
        e["fresh"] = bytes(fresh)
        var = m.pointer(at + 0x20)
        e["variants"] = m.bytes[var:var + extent(var)] if var else None
        stmts.append(e)

    # What viasizes writes when the language starts: the stride and the
    # length of a variant, as one eight-byte store of two words.
    sizes = []
    for _a, op, o, note in m.code("viasizes"):
        if op == "movabsq":
            pair = int(o[0][1:], 16)
        elif op == "movq" and note is not None:
            off = note - base
            i, inner = off // ENTRY, off % ENTRY
            if inner != 0x48:
                raise SystemExit("lift61: viasizes writes 0x%x of an entry"
                                 % inner)
            sizes.append((i, 0x28, pair & 0xffffffff))
            sizes.append((i, 0x2c, pair >> 32))
    return stmts, sizes


def write_statements(m, tag):
    dlk = sibling("module/link")
    stmts, sizes = statements_61(m)
    path = dlk.text_path(tag)
    n = dlk.write_text(stmts, sizes, path, tag)
    print("%d statement types, %d fields, in %s"
          % (len(stmts), n, os.path.relpath(path, ROOT)))


# ---- the sets and the dictionary ------------------------------------------
#
# Each set and each action is described by eight words of eight bytes where
# IBM's are nine and ten words of four: the name, the store, the statement
# type and three counts, then the width and the value dictinit fills in, and
# for an action the two bytes and two words after them. Each comes over
# word for word into IBM's shape. The stores are bytes and come over whole.

SET_DESC, ACT_DESC = 0x24, 0x28


def describe(raw, size):
    out = bytearray()
    for k in range(6):
        w = int.from_bytes(raw[8 * k:8 * k + 8], "little")
        if w >> 32 not in (0, 0xffffffff):
            raise SystemExit("lift61: a descriptor word 0x%x is wider than"
                             " four bytes" % w)
        out += (w & 0xffffffff).to_bytes(4, "little")
    out += raw[0x30:0x30 + size - 0x18]
    return bytes(out)


def installed(m, func):
    """The slot and the address each pointer a *_dict_new writes."""
    out = []
    held = {}
    for _a, op, o, note in m.code(func):
        if op == "leaq" and note is not None:
            held[reg(o[1])] = note
        elif op == "movq" and o[0].startswith("%") and reg(o[0]) in held \
                and o[1].endswith("(%rax)"):
            mm = MEM.match(o[1])
            slot = (int(mm.group(1), 16) if mm.group(1) else 0) // 8
            out.append((slot, held[reg(o[0])]))
    return sorted(out)


def sets_61(m):
    numbers = {}
    for _a, op, o, note in m.code("link_new"):
        if op in ("movb", "movw", "movl") and o[0].startswith("$"):
            # The machine is in rdi on the way in and rbx after it is kept.
            numbers[o[1].replace("%rdi)", "%rbx)")] = int(o[0][1:], 16)
        if op == "leaq" and note is not None:
            numbers.setdefault("lea", []).append(note)
    nsets, nacts = numbers["0x132(%rbx)"], numbers["0x140(%rbx)"]
    allocs = [int(o[0][1:], 16) for _a, op, o, _n in m.code("link_new")
              if op == "movl" and o[1] == "%edi"] + \
        [None] * 3
    pushes = [int(o[0][1:], 16) for _a, op, o, _n in m.code("link_new")
              if op == "pushq" and o[0].startswith("$")]
    sdesc = m.symbol["vsetdtbl_glob"]
    adesc = m.symbol["vactdtbl_glob"]
    set_table = b"".join(describe(m.bytes[sdesc + 64 * k:sdesc + 64 * k + 64],
                                  SET_DESC) for k in range(nsets))
    act_table = b"".join(describe(m.bytes[adesc + 64 * k:adesc + 64 * k + 64],
                                  ACT_DESC) for k in range(nacts))
    names = sorted(set(m.symbol.values()))

    def store(func):
        at = installed(m, func)
        lo = min(a for _s, a in at)
        last = max(a for _s, a in at)
        hi = [x for x in names if x > last][0]
        return {"store": m.bytes[lo:hi],
                "at": [(slot, name_of(m, a), a - lo) for slot, a in at]}

    lf = m.symbol["vlfnames_glob"]
    streams = []
    for k in range(numbers["0x130(%rbx)"]):
        streams.append(m.cstring(m.pointer(lf + 8 * k)))
    dictfile = [n for n in numbers["lea"]
                if n not in (lf, sdesc, adesc)]
    room = 64
    return {"set_table": set_table, "act_table": act_table,
            "sets": store("set_dict_new"), "acts": store("act_dict_new"),
            "streams": streams, "dictfile": m.cstring(dictfile[0]),
            "link": {"fence_room": numbers["0xe8(%rbx)"],
                     "chars": pushes[0], "index": pushes[1],
                     "marks": pushes[2],
                     "statements": numbers["0x120(%rbx)"],
                     "set_room": allocs[0] // room * SET_DESC,
                     "act_room": allocs[1] // room * ACT_DESC}}


def write_sets(m, tag):
    dse = sibling("module/sets")
    model = sets_61(m)
    nsets, nacts = dse.write_text(model, dse.text_path(tag), tag)
    print("%d sets and %d actions, %d and %d bytes of entries, in %s"
          % (nsets, nacts, len(model["sets"]["store"]),
             len(model["acts"]["store"]),
             os.path.relpath(dse.text_path(tag), ROOT)))


# ---- the rules ------------------------------------------------------------
#
# tools/rules/lift64.py reads each rule; what this adds is the language's own
# names for what the rules name by number. A variable is renumbered by its
# place in the list; a string or a table of bytes a rule names by address
# becomes a store in rules/constants, under the address 6.1 had it at; and a
# set or an action keeps its number, since the lists are 6.1's own here.
#
# The rules go into files a source file of 6.1's at a time, which is as far
# as the module says where they came from: each file has a private copy of
# fence the compiler specialised for it, and the copies sit among the file's
# own functions.

HEADER = """# The rules of %(name)s, lifted by tools/rules/lift64.py out of ETI
# Eloquence 6.1 as Apple ships it, by tools/module/lift61.py. Each says
# `instead' because it is written afresh rather than re-expressed from one of
# IBM's: nothing of IBM's is under it.
"""


def write_rules(path, tag, out, name, globals_text=None):
    l64 = sibling("rules/lift64")
    m = Module(path)
    vm = varmap(m, tag, globals_text)
    consts = {}

    def gmap(off, width):
        hit = vm.get(off)
        if hit is None:
            raise l64.Hole("a variable of 6.1's at 0x%x nothing declares" % off)
        return str(hit[0])

    def smap(addr, nm, data):
        if nm == "null_str":
            consts["eti_null"] = b"\x00"
            return "eti_null"
        if nm and nm.startswith("fence:"):
            key = "eti_fence_" + nm[6:]
        else:
            key = "eti_s_%x" % addr
        if consts.get(key, data) != data:
            raise SystemExit("lift61: %s named twice with different bytes"
                             % key)
        consts[key] = data
        return key

    mod = l64.Module(path)
    names = mod.rules()
    at = {n: mod.functions[n][0][0] for n in names}
    marks = sorted(a for a, n in mod.private.items()
                   if n == "fence" or n.startswith("fence@"))
    files = collections.defaultdict(list)
    holes = []
    for n in sorted(names, key=lambda n: at[n]):
        r = l64.Rule(mod, n)
        text = r.lift(gmap, smap)
        if text is None:
            holes.append((n, r.holes))
            continue
        k = sum(1 for x in marks if x <= at[n])
        files[k].append(text)
    os.makedirs(out, exist_ok=True)
    for k, texts in sorted(files.items()):
        with open(os.path.join(out, "f61_%02d.up" % k), "w",
                  encoding="utf-8") as f:
            f.write(HEADER % {"name": name})
            for t in texts:
                f.write("\n" + t)
    with open(os.path.join(out, "constants"), "w", encoding="utf-8") as f:
        f.write("# Bytes the rules of %s name by address, out of ETI Eloquence"
                " 6.1 as Apple\n# ships it: each under the address 6.1 had"
                " it at, and the fences its\n# compiler wrote out inline under"
                " the statement types they fence.\n\n" % name)
        width = max(len(k) for k in consts)
        for k in sorted(consts):
            f.write("bytes %-*s %s\n" % (width, k,
                                          " ".join("%02x" % b
                                                   for b in consts[k])))
    print("%d rules in %d files, %d strings, %d not lifted"
          % (sum(len(v) for v in files.values()), len(files), len(consts),
             len(holes)))
    for n, h in holes:
        print("  %s: %s" % (n, "; ".join(h)))


# ---- the settings ---------------------------------------------------------
#
# What IBM's modules carry as an ini in the image, 6.1 says in two rules that
# its installer ran: one sets the eight voices' numbers as variables and hands
# their addresses to registerVoice, and one hands registerPhoneme the record
# of every phoneme. Apple's build writes neither anywhere, so they are read
# out of the rules as lifted: each number is a constant the rule stores.
# Around them is Italian's ini less what belongs to the concatenative engine,
# which is out of scope and which 6.1's modules never had.

def rule_text(rules_dir, name):
    for f in sorted(os.listdir(rules_dir)):
        if not f.endswith(".up"):
            continue
        text = open(os.path.join(rules_dir, f), encoding="utf-8").read()
        m = re.search(r"^rule %s takes \d+$(.*?)^end$" % re.escape(name),
                      text, re.M | re.S)
        if m:
            return m.group(1)
    return None


def voices_of(rules_dir):
    consts = {}
    for line in (rule_text(rules_dir, "set_voice_param_constants") or
                 "").splitlines():
        w = line.split()
        if w[:3] == ["set", "global", "half"] and w[4] == "to":
            consts[int(w[3])] = int(w[5])
        elif w[:5] == ["and", "0", "into", "global", "half"]:
            consts[int(w[5])] = 0
    text = rule_text(rules_dir, "register_voices") or ""
    var = dict(re.findall(r"^\s*variable (\S+) word (\d+)$", text, re.M))
    # registerVoice is handed the language, the voice's number and two
    # streams, then the eight numbers in the order IBM's ini writes them.
    held, out = {}, {}
    for line in text.splitlines():
        w = line.split()
        if w[:3] == ["call", "move_i", "addr"]:
            held[w[3]] = int(w[4])
        elif w[:2] == ["call", "registerVoice"]:
            names = w[2:][1::2]
            out[held[names[2]]] = [consts[int(var[n]) + 2] for n in names[-8:]]
    return [out[k] for k in sorted(out)]


def phonemes_of(rules_dir):
    for f in sorted(os.listdir(rules_dir)):
        if not f.endswith(".up"):
            continue
        for m in re.finditer(r"^rule (\S+) takes \d+$(.*?)^end$",
                             open(os.path.join(rules_dir, f),
                                  encoding="utf-8").read(), re.M | re.S):
            if "call registerPhoneme" not in m.group(2):
                continue
            held, out = {}, []
            for line in m.group(2).splitlines():
                w = line.split()
                if w[:3] == ["call", "move_i", "addr"]:
                    held[w[3]] = int(w[4])
                elif w[:2] == ["call", "registerPhoneme"]:
                    args = w[2:]
                    locals_ = [args[k + 1] for k in range(0, len(args), 2)
                               if args[k] == "addr"]
                    out.append([held[x] for x in locals_[3:]])
            return out
    return []


def write_settings(tag, rules_dir, family, dialect, library, template="itit"):
    dse = sibling("module/settings")
    blob, _name = dse.read_text(dse.text_path(template))
    keep = []
    section = None
    for rec in blob[:-3].split(b"\x00"):
        line = rec.decode("latin-1")
        m = re.search(r"\[(\d+)\.(\d+)\]$", line)
        if m:
            section = "language"
            keep.append(line[:m.start()] + "[%d.%d]" % (family, dialect))
            keep.append("Path=eci%s.syn" % tag)
            keep.append("Version=6.1")
            for k, v in enumerate(voices_of(rules_dir), 1):
                keep.append("Voice%d=%s" % (k, " ".join(str(x) for x in v)))
            for k, p in enumerate(phonemes_of(rules_dir)):
                keep.append("Phoneme%d=%s" % (k, " ".join(str(x) for x in p)))
            continue
        if section == "language":
            continue
        keep.append(line)
    out = b"\x00".join(x.encode("latin-1") for x in keep) + dse.TERMINATOR
    path = dse.text_path(tag)
    n = dse.write_text(tag, out, library, path)
    print("%d records in %s" % (n, os.path.relpath(path, ROOT)))


def main():
    if len(sys.argv) != 4:
        print(__doc__.split("usage:")[1].strip())
        return 2
    m = Module(sys.argv[1])
    what = sys.argv[3]
    if what == "globals":
        write_globals(m, sys.argv[2])
        return 0
    if what == "statements":
        write_statements(m, sys.argv[2])
        return 0
    if what == "sets":
        write_sets(m, sys.argv[2])
        return 0
    if what == "rules":
        write_rules(sys.argv[1], sys.argv[2],
                    os.path.join(ROOT, "lang", sys.argv[2], "rules"),
                    LANG_NAMES.get(sys.argv[2], sys.argv[2]))
        return 0
    if what == "settings":
        # Out of the rules rather than the module: the voices and phonemes
        # are what two of the lifted rules register, so `rules' comes first.
        family, dialect, library = LANG_NUMBERS[sys.argv[2]]
        write_settings(sys.argv[2],
                       os.path.join(ROOT, "lang", sys.argv[2], "rules"),
                       family, dialect, library)
        return 0
    print(__doc__.split("usage:")[1].strip())
    return 2


if __name__ == "__main__":
    sys.exit(main())
