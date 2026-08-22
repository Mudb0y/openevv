#!/usr/bin/env python3
"""The rules as text that can be read, edited and compiled back.

The rules reach us as compiled x86 in IBM's objects. `delta-lift.py` turns one
into blocks of operations over operands, and `delta-emit.py` turns those into
the bytecode the engine runs. That middle form is the thing worth writing down:
it is one to one with what the machine does, so it can be written out and read
back without loss, and once it is in the tree the rules can be edited and the
objects are no longer needed to rebuild them.

This is that form as text, both ways. `write' lifts an object and prints it;
`read' parses it back; `check' does both and holds the bytecode emitted from
each against the other, byte for byte, which is the whole proof. Nothing here
is allowed to be clever: a line means one operation and the parse is the
inverse of the print.

What the text is not is the readable C that `delta-decompile.py` writes. That
restructures into loops and conditionals for a person to read, and inverting it
exactly would be hard. Reading and round-tripping are different jobs, so they
have different forms.

usage: delta-notation.py write  <object> [> file]
       delta-notation.py read   <file>
       delta-notation.py check  <object> [...]
       delta-notation.py check-all
       delta-notation.py tree            write lang/enus/rules
       delta-notation.py verify          the tree against IBM's objects
"""

import importlib.util
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name,
                                                  os.path.join(ROOT, path))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


dl = load("delta_lift", "tools/delta-lift.py")
de = load("delta_emit", "tools/delta-emit.py")

OBJECTS = os.path.join(ROOT, "analysis", "enus")

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


# ---- the proof ----------------------------------------------------------


def emit_one(name, d, obj, tables):
    """The bytecode of one rule, from a pool of its own so that two runs of
    the same rule can be held against each other."""
    e = de.Emitter()
    e.rule(name, d, tables, obj)
    return bytes(e.code)


def arities():
    """How many arguments each entry takes, learned from every call site.

    The real pipeline does this pass before it lifts anything, and hands the
    answer to the lifter: a call reached by a path that did not write its
    arguments cannot say for itself how many it takes. Lifting without it gives
    a different -- and wrong -- answer for some rules, so anything meaning to
    reproduce what the engine runs has to do it too.
    """
    seen = {}
    for obj in sorted(f for f in os.listdir(OBJECTS) if f.endswith(".obj")):
        for name, items in dl.read_functions(os.path.join(OBJECTS, obj)):
            if not dl.is_rule(items):
                continue
            data, tables = dl.find_data(items)
            d = dl.Decoder(name, items, data, tables).run()
            for _l, _s, block in d.blocks:
                for op in block:
                    if op[0] == "call" and op[2]:
                        seen.setdefault(op[1], {})
                        seen[op[1]][op[2]] = seen[op[1]].get(op[2], 0) + 1
    return dict((k, max(v.items(), key=lambda kv: kv[1])[0])
                for k, v in seen.items())


def thunks(known):
    """The helper thunks the compiler generated beside the rules, in glob.obj.

    Not rules by the lifter's test, but the same kind of thing and the
    interpreter runs them the same way, so the text has to hold them too or it
    is only two thirds of what the engine runs.
    """
    path = os.path.join(OBJECTS, "glob.obj")
    if not os.path.exists(path):
        return [], {}
    got = []
    tables = None
    for name, items in dl.read_functions(path):
        if not name.startswith("ZZ"):
            continue
        data, tabs = dl.find_data(items)
        d = dl.Decoder(name, items, data, tabs, known).run()
        if d.holes:
            continue
        # A thunk passes its caller's arguments through, and the ones it never
        # reads are still there to be passed on, so a call site that says more
        # than the thunk reads is the one that is right.
        d.params = max(d.params, known.get(name, 0))
        if tables is None:
            tables = de.raw_bytes(path)
        got.append((name, d))
    return got, (tables or {})


def lift(path, known=None):
    """Every rule in an object, lifted, with the object's raw tables."""
    got = []
    tables = None
    for name, items in dl.read_functions(path):
        if not dl.is_rule(items):
            continue
        data, tabs = dl.find_data(items)
        d = dl.Decoder(name, items, data, tabs, known).run() if known \
            else dl.Decoder(name, items, data, tabs).run()
        if d.holes:
            continue
        if tables is None:
            tables = de.raw_bytes(path)
        got.append((name, d))
    return got, (tables or {})


def check(obj):
    path = os.path.join(OBJECTS, obj)
    rules, tables = lift(path)
    same = 0
    differed = []
    unwritten = []

    for name, d in rules:
        lines = []
        try:
            write_rule(name, obj, d, tables, lines)
        except ValueError as why:
            unwritten.append((name, str(why)))
            continue
        back, back_tables = read_rules(lines)
        if len(back) != 1:
            differed.append((name, "read back as %d rules" % len(back)))
            continue
        _n, d2, _o = back[0]
        want = emit_one(name, d, obj, tables)
        got = emit_one(name, d2, obj, back_tables)
        if want == got:
            same += 1
        else:
            differed.append((name, "%d bytes against %d"
                             % (len(want), len(got))))

    print("%-16s %3d rules, %3d round-tripped byte for byte" %
          (obj, len(rules), same))
    for name, why in unwritten:
        print("    no written form: %s -- %s" % (name, why))
    for name, why in differed:
        print("    differed: %s -- %s" % (name, why))
    return not (differed or unwritten)


TREE = os.path.join(ROOT, "lang", "enus", "rules")
SHIPPED = os.path.join(ROOT, "lang", "enus", "delta_rules_enus.c")


def shipped_bytecode():
    """The bytecode the engine actually runs, out of the tree."""
    import re as _re
    s = open(SHIPPED).read()
    m = _re.search(r"const uint8_t delta_rule_code\[\] = \{(.*?)\};", s,
                   _re.S)
    if m is None:
        raise ValueError("no delta_rule_code in %s" % SHIPPED)
    return bytes(int(x) for x in m.group(1).replace("\n", "").split(",")
                 if x.strip())


def all_rules(known):
    """Every rule the engine runs, in the order the emitter takes them: each
    object's rules in the order of the objects, then glob.obj's thunks."""
    for obj in objects_with_rules():
        got, tables = lift(os.path.join(OBJECTS, obj), known)
        for name, d in got:
            yield name, d, obj, tables
    got, tables = thunks(known)
    for name, d in got:
        yield name, d, "glob.obj", tables


SYMBOLS = os.path.join(TREE, "symbols")


def write_symbols():
    """Where each address the rules name falls, so the objects are not needed.

    A rule names a constant by a symbol; the bytes behind it are a whole data
    section of the object it was compiled into, and what the rule gets is an
    offset into that section. The bytes are already in the tree, in
    delta_consts_enus.c. What was not in the tree was the mapping -- which
    store, and how far in -- and it is the last thing the emitter needed the
    objects for. 75 stores and 6,718 addresses, so it is small.
    """
    import tempfile
    known = arities()
    e = de.Emitter()
    for name, d, obj, tables in all_rules(known):
        e.rule(name, d, tables, obj)
        e.origin[name] = obj
    with tempfile.TemporaryDirectory() as tmp:
        stores, names = de.write_consts(e, OBJECTS,
                                        os.path.join(tmp, "consts.c"))
    out = ["# Where each address the rules name falls: which store of the",
           "# language's own bytes, and how far into it. Written by",
           "# tools/delta-notation.py out of IBM's objects. The bytes",
           "# themselves are in delta_consts_enus.c.",
           ""]
    for st in stores:
        out.append("store %s" % st)
    for nm in names:
        store, _plus, off = nm.split()
        out.append("at %s %s" % (store, off))
    open(SYMBOLS, "w").write("\n".join(out) + "\n")
    print("%d stores and %d addresses in %s"
          % (len(stores), len(names), os.path.relpath(SYMBOLS, ROOT)))
    return True


def read_symbols():
    stores = []
    names = []
    for line in open(SYMBOLS):
        w = line.split()
        if not w or w[0].startswith("#"):
            continue
        if w[0] == "store":
            stores.append(w[1])
        elif w[0] == "at":
            names.append("%s + %s" % (w[1], w[2]))
    return stores, names


def regenerate():
    """Write the engine's bytecode file out of the text, and see whether it is
    the one in the tree.

    This is the whole point of the exercise: if it matches, the rules can be
    rebuilt from text a person can edit, and IBM's objects are wanted for the
    comparison suite and nothing else.
    """
    import tempfile
    e = de.Emitter()
    n = 0
    # In the order the emitter takes them, which is what the shared pools are
    # numbered by: every object's rules in the order of the objects, and
    # glob.obj's thunks last. Nothing here opens an object.
    files = sorted(f for f in os.listdir(TREE) if f.endswith(".dr"))
    files = ([f for f in files if f != "glob.dr"]
             + [f for f in files if f == "glob.dr"])
    for f in files:
        rules, tables = read_rules(open(os.path.join(TREE, f)))
        for name, d, obj in rules:
            e.rule(name, d, tables, obj)
            e.origin[name] = obj
            n += 1
    print("rules read out of %s: %d (no object opened)"
          % (os.path.relpath(TREE, ROOT), n))

    stores, names = read_symbols()
    print("stores %d, addresses %d" % (len(stores), len(names)))

    with tempfile.TemporaryDirectory() as tmp:
        out_c = os.path.join(tmp, "delta_rules_enus.c")
        out_h = os.path.join(tmp, "delta_rules.h")
        de.write_c(e, None, out_c, out_h, None, stores, names)
        got = open(out_c, "rb").read()
        got_h = open(out_h, "rb").read()

    ok = True
    for what, made, have in (("delta_rules_enus.c", got, SHIPPED),
                             ("delta_rules.h", got_h,
                              os.path.join(ROOT, "lang", "enus",
                                           "delta_rules.h"))):
        want = open(have, "rb").read()
        if made == want:
            print("%-22s %d bytes, the same as the tree's" % (what, len(made)))
        else:
            print("%-22s differs: %d bytes against %d"
                  % (what, len(made), len(want)))
            a = made.split(b"\n")
            b = want.split(b"\n")
            for i in range(min(len(a), len(b))):
                if a[i] != b[i]:
                    print("  first line that differs is %d" % (i + 1))
                    print("  made: %s" % a[i][:100])
                    print("  tree: %s" % b[i][:100])
                    break
            ok = False
    return ok


def prove():
    """Emit every rule out of the text and hold the whole stream against the
    bytecode in the tree.

    This is the check that matters. The pools -- constants, strings, entry
    points, tag maps -- are shared across the whole language and numbered in
    the order the rules are taken, so reproducing the stream byte for byte says
    the text carries not just each rule but every rule, in order, with nothing
    added and nothing left out. A per-rule comparison cannot say that.
    """
    known = arities()
    print("entries whose arity was learned: %d" % len(known))

    want = shipped_bytecode()
    from_text = de.Emitter()
    from_lift = de.Emitter()
    n = 0

    for name, d, obj, tables in all_rules(known):
        lines = []
        write_rule(name, obj, d, tables, lines)
        back, back_tables = read_rules(lines)
        if len(back) != 1:
            print("    %s read back as %d rules" % (name, len(back)))
            return False
        from_lift.rule(name, d, tables, obj)
        from_text.rule(name, back[0][1], back_tables, obj)
        n += 1

    text = bytes(from_text.code)
    lift_ = bytes(from_lift.code)
    print("rules taken: %d" % n)
    print("from the text: %d bytes, from a fresh lift: %d, in the tree: %d"
          % (len(text), len(lift_), len(want)))

    ok = True
    if text != lift_:
        print("the text and a fresh lift do not agree")
        ok = False
    if text != want:
        print("the text does not reproduce the bytecode in the tree")
        for i in range(min(len(text), len(want))):
            if text[i] != want[i]:
                print("  first difference at byte %d" % i)
                break
        ok = False
    if ok:
        print("the text reproduces the engine's bytecode byte for byte")
    return ok



def objects_with_rules():
    for obj in sorted(f for f in os.listdir(OBJECTS) if f.endswith(".obj")):
        path = os.path.join(OBJECTS, obj)
        if any(dl.is_rule(i) for _n, i in dl.read_functions(path)):
            yield obj


def to_tree():
    """Write every object's rules into the tree, one file to an object.

    One file an object because that is the grain the rules already have: the
    table records which object a rule came from, and the sources in src are
    named after theirs for the same reason -- a file that can be held against
    the thing it came from can be checked against it.
    """
    os.makedirs(TREE, exist_ok=True)
    known = arities()
    rules = 0
    per_object = {}

    for name, d, obj, tables in all_rules(known):
        per_object.setdefault(obj, []).append((name, d, tables))

    for obj in sorted(per_object):
        out = ["# The rules of %s, written by tools/delta-notation.py." % obj,
               "# One operation to a line. See docs/building.md.",
               ""]
        for name, d, tables in per_object[obj]:
            write_rule(name, obj, d, tables, out)
            out.append("")
        where = os.path.join(TREE, obj[:-4] + ".dr")
        open(where, "w").write("\n".join(out) + "\n")
        rules += len(per_object[obj])
        print("%-16s %4d rules" % (obj, len(per_object[obj])))
    print("%d rules in %s" % (rules, os.path.relpath(TREE, ROOT)))
    return True


def verify():
    """Hold the text in the tree against IBM's objects.

    Every rule is emitted twice, once from the text and once from a fresh lift
    of the object it names, and the bytecode has to match byte for byte. That
    is what says the text in the tree is still what IBM's code does -- and,
    once a rule has been deliberately changed, which rules those are: an
    unedited rule matches and an edited one is named.
    """
    ok = True
    total = same = 0
    known = arities()
    lifted_by_object = {}
    tables_by_object = {}
    for name, d, obj, tables in all_rules(known):
        lifted_by_object.setdefault(obj, {})[name] = d
        tables_by_object[obj] = tables

    for obj in sorted(lifted_by_object):
        where = os.path.join(TREE, obj[:-4] + ".dr")
        if not os.path.exists(where):
            print("%-16s no text in the tree" % obj)
            ok = False
            continue
        written, wtables = read_rules(open(where))
        by_name = lifted_by_object[obj]
        ltables = tables_by_object[obj]
        for name, d2, o in written:
            total += 1
            if name not in by_name:
                print("    %s: in the tree and not in %s" % (name, obj))
                ok = False
                continue
            want = emit_one(name, by_name[name], o, ltables)
            got = emit_one(name, d2, o, wtables)
            if want == got:
                same += 1
            else:
                print("    %s: differs from %s" % (name, obj))
                ok = False
        for name in by_name:
            if not any(n == name for n, _d, _o in written):
                print("    %s: in %s and not in the tree" % (name, obj))
                ok = False
    print("%d rules in the tree, %d the same as IBM's objects" % (total, same))
    return ok


# ---- the upper layer ----------------------------------------------------
#
# What a rule stands for, rather than what the machine does to arrive at it.
# It covers the wrappers, which are 2,335 of the 3,377 rules and are each one
# primitive with its arguments baked in -- the name already spells them, so
# this only says out loud what `ZZtest_string_s_2_1_ZZstring480' is spelling.
#
# It does not try to cover the 1,042 real rules. Those are programs: a median
# of 28 calls over 15 blocks, 1,058 distinct shapes between them, and only 12%
# fitting even a loose template of tests and ordinary actions. For those the
# readable form is the C the decompiler writes, and the naming it already does
# is the win.
#
# A wrapper that does not fit is left in the lower form and said so. Two of the
# 2,335 do not: they clean up in a way the idiom does not describe, and
# stretching the upper form to hold two rules would be the wrong trade.

UPPER = os.path.join(TREE, "wrappers.up")


def upper_of(name, d):
    """A wrapper as what it stands for, or None if it is not one.

    The shape, which every one of them has: a single block, then for each call
    a run of pushes whose last is the state, then the call, then one cleanup
    for the lot and the answer. Anything else is not a wrapper.
    """
    if len(d.blocks) != 1 or d.frame != 0 or d.pbase != 8:
        return None
    ops = d.blocks[0][2]
    if not ops or ops[-1] != ("return", ("reg", "%eax")):
        return None

    calls = []
    pending = []
    held = None          # a widened argument waiting to be pushed
    i = 0
    while i < len(ops) - 1:
        k = ops[i][0]
        if k == "load":
            # A byte or a half word widened before it is handed over. Only
            # ever into r0, and only ever pushed next but one at the latest.
            kind, src, into = ops[i][1], ops[i][2], ops[i][3]
            if into != "%eax" or src[0] != "param" or held is not None:
                return None
            if kind == "movzbl":
                held = ("widened", src[1], "byte")
            elif kind == "movzwl":
                held = ("widened", src[1], "half")
            elif kind == "movl":
                held = ("widened", src[1], "word")
            else:
                return None
        elif k == "push":
            if ops[i][1] == ("reg", "%eax") and held is not None:
                pending.append(held)
                held = None
            else:
                pending.append(ops[i][1])
        elif k == "call":
            entry, arity, depth = ops[i][1], ops[i][2], ops[i][3]
            if len(pending) != arity or arity < 1:
                return None
            # The state is pushed last, so it is the first argument.
            if pending[-1] != ("param", 0):
                return None
            calls.append((entry, list(reversed(pending[:-1]))))
            pending = []
        elif k in ("popn", "popreg"):
            break
        else:
            return None
        i += 1

    if not calls or pending or held is not None:
        return None

    total = sum(1 + len(a) for _e, a in calls)
    cleanup = ops[i:-1]
    base = ([("popreg", "%ecx"), ("popreg", "%ecx")] if total == 2
            else [("popn", total)])
    truth = [("alu", "negl", None, ("reg", "%eax")),
             ("alu", "sbbl", ("reg", "%eax"), ("reg", "%eax")),
             ("alu", "negl", None, ("reg", "%eax"))]
    if cleanup == base:
        return calls, False
    if cleanup == base + truth:
        return calls, True
    return None


def upper_operand(o):
    kind = o[0]
    if kind == "imm":
        return str(o[1])
    if kind == "sym":
        return o[1]
    if kind == "param":
        return "arg %d" % o[1]
    if kind == "widened":
        return "arg %d as %s" % (o[1], o[2])
    return None


def upper_lines(name, d, calls, truth):
    out = ["wrapper %s takes %d%s"
           % (name, d.params, " answering truth" if truth else "")]
    for entry, args in calls:
        words = [entry]
        for a in args:
            w = upper_operand(a)
            if w is None:
                return None
            words.append(w)
        out.append("  " + " ".join(words))
    return out


def upper_read(lines):
    """The upper form back as (name, params, calls)."""
    got = []
    cur = None
    for raw in lines:
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        w = line.split()
        if w[0] == "wrapper":
            if cur:
                got.append(cur)
            cur = (w[1], int(w[3]), [], "truth" in w)
        else:
            entry = w.pop(0)
            args = []
            while w:
                if w[0] == "arg":
                    w.pop(0)
                    n = int(w.pop(0))
                    if w and w[0] == "as":
                        w.pop(0)
                        args.append(("widened", n, w.pop(0)))
                    else:
                        args.append(("param", n))
                else:
                    t = w.pop(0)
                    try:
                        args.append(("imm", int(t)))
                    except ValueError:
                        args.append(("sym", t))
            cur[2].append((entry, args))
    if cur:
        got.append(cur)
    return got


def upper_compile(name, params, calls, truth=False):
    """The upper form down into the lower one: the same ops, in the same order,
    with the idioms the compiler used -- the state pushed last so that it is
    the first argument, the depth running on across the calls, and one cleanup
    for all of them."""
    d = Written(name, "glob.obj", 0, 8, params)
    body = d.block("L0")
    depth = 0
    kinds = {"byte": "movzbl", "half": "movzwl", "word": "movl"}
    for entry, args in calls:
        for a in reversed(args):
            if a[0] == "widened":
                body.append(("load", kinds[a[2]], ("param", a[1]), "%eax"))
                body.append(("push", ("reg", "%eax")))
            else:
                body.append(("push", a))
        body.append(("push", ("param", 0)))
        depth += 1 + len(args)
        body.append(("call", entry, 1 + len(args), depth))
    if depth == 2:
        body.append(("popreg", "%ecx"))
        body.append(("popreg", "%ecx"))
    else:
        body.append(("popn", depth))
    if truth:
        body.append(("alu", "negl", None, ("reg", "%eax")))
        body.append(("alu", "sbbl", ("reg", "%eax"), ("reg", "%eax")))
        body.append(("alu", "negl", None, ("reg", "%eax")))
    body.append(("return", ("reg", "%eax")))
    return d


def write_upper():
    lifted, tables = read_rules(open(os.path.join(TREE, "glob.dr")))
    out = ["# The wrappers, as the primitive each stands for. Written by",
           "# tools/delta-notation.py. Every one takes the machine's state as",
           "# its first argument, so that is not written; `arg n' is the",
           "# wrapper's own nth.",
           ""]
    done = left = unfaithful = 0
    for name, d, _obj in lifted:
        fit = upper_of(name, d)
        lines = upper_lines(name, d, fit[0], fit[1]) if fit else None
        if lines is None:
            left += 1
            continue
        # Only write what can be compiled back to the same bytecode. Some
        # wrappers widen an argument, and the compiler put that load where it
        # suited it rather than always in one place; where this cannot
        # reproduce the placement, the upper form would be a re-description
        # that is not the rule, so the rule stays in the lower form.
        made = upper_compile(name, d.params, fit[0], fit[1])
        if emit_one(name, made, "glob.obj", tables) != \
           emit_one(name, d, "glob.obj", tables):
            unfaithful += 1
            left += 1
            continue
        out.extend(lines)
        done += 1
    open(UPPER, "w").write("\n".join(out) + "\n")
    print("%d wrappers written to %s, %d left in the lower form"
          % (done, os.path.relpath(UPPER, ROOT), left))
    print("  of those left, %d because this could not reproduce them exactly"
          % unfaithful)
    return True


def upper_prove():
    """Compile every wrapper's upper form down and hold it against the lower
    form in the tree -- the same operations in the same order, and the same
    bytecode. Byte-identity is the point: this is a re-expression of a rule
    that already exists, so anything but identical is a difference nobody
    asked for."""
    lifted, tables = read_rules(open(os.path.join(TREE, "glob.dr")))
    have = dict((n, d) for n, d, _o in lifted)
    same = 0
    differed = []
    for name, params, calls, truth in upper_read(open(UPPER)):
        if name not in have:
            differed.append((name, "not in the lower form"))
            continue
        made = upper_compile(name, params, calls, truth)
        want = emit_one(name, have[name], "glob.obj", tables)
        got = emit_one(name, made, "glob.obj", tables)
        if got == want:
            same += 1
        else:
            differed.append((name, "%d bytes against %d" % (len(got), len(want))))
    print("%d wrappers compiled from what they stand for, byte for byte the"
          " same as the lower form" % same)
    for name, why in differed[:10]:
        print("    %s: %s" % (name, why))
    if len(differed) > 10:
        print("    and %d more" % (len(differed) - 10))
    return not differed


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip())
        return 2
    what = sys.argv[1]

    if what == "write":
        obj = sys.argv[2]
        rules, tables = lift(os.path.join(OBJECTS, obj))
        out = []
        for name, d in rules:
            write_rule(name, obj, d, tables, out)
            out.append("")
        print("\n".join(out))
        return 0

    if what == "read":
        rules, _t = read_rules(open(sys.argv[2]))
        print("%d rules read" % len(rules))
        return 0

    if what == "check":
        ok = True
        for obj in sys.argv[2:]:
            ok = check(obj) and ok
        return 0 if ok else 1

    if what == "upper":
        return 0 if write_upper() else 1

    if what == "upper-prove":
        return 0 if upper_prove() else 1

    if what == "symbols":
        return 0 if write_symbols() else 1

    if what == "regenerate":
        return 0 if regenerate() else 1

    if what == "prove":
        return 0 if prove() else 1

    if what == "tree":
        return 0 if to_tree() else 1

    if what == "verify":
        return 0 if verify() else 1

    if what == "check-all":
        ok = True
        for obj in sorted(f for f in os.listdir(OBJECTS)
                          if f.endswith(".obj")):
            path = os.path.join(OBJECTS, obj)
            if not any(dl.is_rule(i) for _n, i in dl.read_functions(path)):
                continue
            ok = check(obj) and ok
        return 0 if ok else 1

    print(__doc__.strip())
    return 2


if __name__ == "__main__":
    sys.exit(main())
