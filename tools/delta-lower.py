#!/usr/bin/env python3
"""The lower notation: one operation to a line, read and written.

This is the half of the notation that is the machine's rather than any
language's -- the registers, the operands, the operations, and a rule as the
blocks it is made of. It was in delta-notation.py, which lifts IBM's objects
into it; tools/delta-upper.py compiles into it from above. Neither of those
may have a copy of its own, since a print and a parse that disagree is the one
fault this form can have, so it lives here and both load it.

Nothing here knows what a rule means, which language it belongs to, or where
it came from.
"""


# ---- registers ----------------------------------------------------------
#
# The machine has eight, and the lifter still calls them by the x86 names the
# compiler used. Here they are numbered, with a letter for how much of one is
# meant: bare for all of it, `w' for the low half, `b' and `h' for the two
# bytes of that half. The reader turns them back into the names the emitter
# encodes, so nothing downstream has to know about this.

REG_FULL = ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi")
REG_TEXT = {}
for _i, _n in enumerate(REG_FULL):
    REG_TEXT[_n] = "r%d" % _i
for _n, _i in (("ax", 0), ("cx", 1), ("dx", 2), ("bx", 3),
               ("sp", 4), ("bp", 5), ("si", 6), ("di", 7)):
    REG_TEXT[_n] = "r%dw" % _i
for _n, _i in (("al", 0), ("cl", 1), ("dl", 2), ("bl", 3)):
    REG_TEXT[_n] = "r%db" % _i
for _n, _i in (("ah", 0), ("ch", 1), ("dh", 2), ("bh", 3)):
    REG_TEXT[_n] = "r%dh" % _i
REG_BACK = {v: "%" + k for k, v in REG_TEXT.items()}


def put_reg(name):
    n = str(name).lstrip("%")
    if n not in REG_TEXT:
        raise ValueError("no written form for the register %r" % (name,))
    return REG_TEXT[n]


def get_reg(text):
    if text not in REG_BACK:
        raise ValueError("no register called %r" % (text,))
    return REG_BACK[text]


# ---- operands -----------------------------------------------------------
#
# An operand is one or two words, never more, so a line can be read left to
# right and each operand taken as it comes. Which it is says how many words
# it has, so nothing needs punctuation to tell them apart.
#
# `loaded' is written as the register it is, because that is all the emitter
# takes from it: what the notation has to preserve is the bytecode, and a
# form that says more than the bytecode records would be a form with
# something in it nobody reads.

ONE_WORD = ("none",)
# Two words, whose second is a number.
NUMBERED = ("imm", "param", "paramaddr", "slot", "slotaddr",
            "state", "statefld")
# Two words, whose second is a name: a register, or a constant the object
# named rather than numbered.
NAMED = ("sym", "reg")
TWO_WORD = NUMBERED + NAMED


def put_operand(o, out):
    if o is None:
        out.append("none")
        return
    kind = o[0]
    if kind == "loaded":
        out.extend(("reg", put_reg(o[3])))
    elif kind == "indirect":
        out.append("ind")
        put_operand(o[1], out)
        out.append(str(o[2]))
    elif kind == "reg":
        out.extend(("reg", put_reg(o[1])))
    elif kind in TWO_WORD:
        out.extend((kind, str(o[1])))
    else:
        raise ValueError("operand %r has no written form" % (o,))


def get_operand(w):
    """Take one operand off the front of a list of words."""
    kind = w.pop(0)
    if kind == "none":
        return None
    if kind == "ind":
        inner = get_operand(w)
        return ("indirect", inner, int(w.pop(0)))
    if kind == "reg":
        return ("reg", get_reg(w.pop(0)))
    if kind in NAMED:
        return (kind, w.pop(0))
    if kind in NUMBERED:
        return (kind, int(w.pop(0)))
    raise ValueError("no operand called %r" % (kind,))


# ---- operations ---------------------------------------------------------
#
# Each entry says how to write one operation and how to read it back. The two
# are kept side by side deliberately: an operation whose print and parse
# disagree is the one fault this file can have, and putting them in one place
# is the cheapest way to see it.
#
# A word that is a plain number is written as one; anything the emitter looks
# up in a table -- a comparison kind, a condition, a register -- is written
# under the name the lifter gave it, so the text says what the machine does
# rather than which slot of which table it came from.


def put_op(op, out, name_of):
    k = op[0]
    out.append(k)
    if k == "call":
        out.extend((str(op[1]), "arity", str(op[2]), "depth", str(op[3])))
    elif k in ("push", "return"):
        put_operand(op[1], out)
    elif k == "setarg":
        out.append(str(op[1]))
        put_operand(op[2], out)
    elif k == "popn":
        out.append(str(op[1]))
    elif k == "popreg":
        out.append(put_reg(op[1]))
    elif k == "jump":
        out.extend(("to", name_of(op[1])))
    elif k == "branch":
        out.extend((str(op[1]), "to", name_of(op[2])))
    elif k in ("cmp", "alu"):
        out.append(str(op[1]))
        put_operand(op[2], out)
        put_operand(op[3], out)
    elif k == "load":
        out.append(str(op[1]))
        put_operand(op[2], out)
        out.extend(("into", put_reg(op[3])))
    elif k == "store":
        out.append(str(op[1]))
        put_operand(op[2], out)
        put_operand(op[3], out)
    elif k == "switch":
        put_operand(op[2], out)
        out.append("to")
        out.extend(name_of(t) for t in op[1])
    elif k == "map":
        out.append(str(op[1]))
        put_operand(op[2], out)
        out.extend(("into", put_reg(op[3])))
    elif k == "addk":
        out.append(str(op[1]))
        put_operand(op[2], out)
        out.extend(("into", put_reg(op[3])))
    elif k == "scale":
        out.append(str(op[1]))
        put_operand(op[2], out)
        put_operand(op[3], out)
        out.extend((str(op[4]), "into", put_reg(op[5])))
    elif k == "mul":
        out.append(str(op[1]))
        put_operand(op[2], out)
        put_operand(op[3], out)
        out.extend(("into", put_reg(op[4])))
    elif k == "div":
        out.append(str(op[1]))
        put_operand(op[2], out)
    elif k == "ftol":
        # from <a> [times <bits>] [plusk <bits>] [plus <a>] ... into <reg>
        for step in op[1]:
            if step[0] == "ld":
                out.append("from")
                put_operand(step[1], out)
            elif step[0] == "addi":
                out.append("plus")
                put_operand(step[1], out)
            else:
                out.extend(("times" if step[0] == "mulk" else "plusk",
                            "0x%016x" % step[1]))
        out.extend(("into", put_reg(op[2])))
    elif k == "widen":
        out.append(str(op[1]))
    elif k == "setcc":
        out.extend((str(op[1]), put_reg(op[2])))
    else:
        raise ValueError("operation %r has no written form" % (k,))


def word(w, expect):
    got = w.pop(0)
    if got != expect:
        raise ValueError("expected %r and found %r" % (expect, got))


def get_op(w):
    k = w.pop(0)
    if k == "call":
        entry = w.pop(0)
        word(w, "arity"); arity = int(w.pop(0))
        word(w, "depth"); depth = int(w.pop(0))
        return ("call", entry, arity, depth)
    if k in ("push", "return"):
        return (k, get_operand(w))
    if k == "setarg":
        n = int(w.pop(0))
        return ("setarg", n, get_operand(w))
    if k == "popn":
        return ("popn", int(w.pop(0)))
    if k == "popreg":
        return ("popreg", get_reg(w.pop(0)))
    if k == "jump":
        word(w, "to")
        return ("jump", w.pop(0))
    if k == "branch":
        cond = w.pop(0)
        word(w, "to")
        return ("branch", cond, w.pop(0))
    if k in ("cmp", "alu"):
        kind = w.pop(0)
        a = get_operand(w)
        b = get_operand(w)
        return (k, kind, a, b)
    if k == "load":
        kind = w.pop(0)
        a = get_operand(w)
        word(w, "into")
        return ("load", kind, a, get_reg(w.pop(0)))
    if k == "store":
        kind = w.pop(0)
        a = get_operand(w)
        b = get_operand(w)
        return ("store", kind, a, b)
    if k == "switch":
        a = get_operand(w)
        word(w, "to")
        targets = list(w)
        del w[:]
        return ("switch", targets, a)
    if k == "map":
        table = w.pop(0)
        a = get_operand(w)
        word(w, "into")
        return ("map", table, a, get_reg(w.pop(0)))
    if k == "addk":
        imm = int(w.pop(0))
        a = get_operand(w)
        word(w, "into")
        return ("addk", imm, a, get_reg(w.pop(0)))
    if k == "scale":
        imm = int(w.pop(0))
        a = get_operand(w)
        b = get_operand(w)
        n = int(w.pop(0))
        word(w, "into")
        return ("scale", imm, a, b, n, get_reg(w.pop(0)))
    if k == "mul":
        kind = w.pop(0)
        a = get_operand(w)
        b = get_operand(w)
        word(w, "into")
        return ("mul", kind, a, b, get_reg(w.pop(0)))
    if k == "div":
        kind = w.pop(0)
        return ("div", kind, get_operand(w))
    if k == "ftol":
        steps = []
        while w and w[0] != "into":
            how = w.pop(0)
            if how == "from":
                steps.append(("ld", get_operand(w)))
            elif how == "plus":
                steps.append(("addi", get_operand(w)))
            elif how in ("times", "plusk"):
                steps.append(("mulk" if how == "times" else "addk",
                              int(w.pop(0), 16)))
            else:
                raise ValueError("no floating step called %r" % (how,))
        word(w, "into")
        return ("ftol", tuple(steps), get_reg(w.pop(0)))
    if k == "widen":
        return ("widen", w.pop(0))
    if k == "setcc":
        return ("setcc", w.pop(0), get_reg(w.pop(0)))
    raise ValueError("no operation called %r" % (k,))


# ---- a rule, written and read -------------------------------------------


class Written:
    """A rule read back out of the text, in the shape the emitter wants.

    The emitter asks a rule for its blocks, the three numbers of its shape,
    and where a label is. It never asks anything else, which is why this is
    eight lines rather than the lifter's several hundred.
    """

    def __init__(self, name, obj, frame, pbase, params):
        self.name = name
        self.obj = obj
        self.frame = frame
        self.pbase = pbase
        self.params = params
        self.blocks = []
        self.holes = 0
        self._at = {}

    def block(self, label):
        at = len(self.blocks) + 1
        self._at[label] = at
        self.blocks.append((label, at, []))
        return self.blocks[-1][2]

    def resolve(self, text):
        return self._at.get(text)


def write_rule(name, obj, d, tables, out):
    """One rule as lines. The tables a MAP reads come with it, since the whole
    point is that the object is not needed again."""
    out.append("rule %s from %s" % (name, obj))
    out.append("shape frame %d argbase %d params %d"
               % (d.frame, d.pbase, d.params))

    wanted = []
    for _l, _a, block in d.blocks:
        for op in block:
            if op[0] == "map" and op[1] not in wanted:
                wanted.append(op[1])
    for t in wanted:
        body = tables.get(t)
        if body is None:
            raise ValueError("no bytes for the %s table" % t)
        out.append("table %s %s" % (t, " ".join("%02x" % b for b in body)))

    # A branch says which block it lands on, under that block's own name. The
    # lifter's own target text is the nearest label and a count forward from
    # it, which is how the compiler wrote it and not a name the text can use.
    at = {}
    for label, addr, _b in d.blocks:
        if label in at and at[label] != addr:
            raise ValueError("two blocks both called %s" % label)
        at[label] = addr
    # Blocks are numbered rather than named after the address they had in
    # IBM's object. What the address was is kept on the line, because while
    # rules are still being lifted it is the only way back to the
    # disassembly; once the text is the source and nothing is lifted any
    # more, the reader already ignores it and it can go.
    numbered = {}
    for i, (label, addr, _b) in enumerate(d.blocks):
        numbered[addr] = "L%d" % i

    def name_of(text):
        addr = d.resolve(text)
        if addr not in numbered:
            raise ValueError("branch to %r, which is not the start of a block"
                             % (text,))
        return numbered[addr]

    for label, addr, block in d.blocks:
        out.append("label %s was %s" % (numbered[addr], label))
        for op in block:
            words = []
            put_op(op, words, name_of)
            out.append("  " + " ".join(words))
    out.append("end")


def read_rules(lines):
    """Every rule in a run of lines, and the tables they carry."""
    out = []
    cur = None
    body = None
    tables = {}

    for raw in lines:
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        w = line.split()
        head = w[0]

        if head == "rule":
            cur = {"name": w[1], "obj": w[3]}
            body = None
        elif head == "shape":
            cur["frame"] = int(w[2])
            cur["pbase"] = int(w[4])
            cur["params"] = int(w[6])
            cur["d"] = Written(cur["name"], cur["obj"], cur["frame"],
                               cur["pbase"], cur["params"])
        elif head == "table":
            tables[w[1]] = bytes(int(x, 16) for x in w[2:])
        elif head == "label":
            body = cur["d"].block(w[1])
        elif head == "end":
            out.append((cur["name"], cur["d"], cur["obj"]))
            cur = None
        else:
            if body is None:
                raise ValueError("an operation before any label: %r" % line)
            body.append(get_op(w))
    return out, tables


