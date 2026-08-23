#!/usr/bin/env python3
"""The rules as text that can be read, edited and compiled back.

The rules reach us as compiled x86 in IBM's objects. `delta-lift.py` turns one
into blocks of operations over operands, and `delta-emit.py` turns those into
the bytecode the engine runs. That middle form is the thing worth writing down:
it is one to one with what the machine does, so it can be written out and read
back without loss, and once it is in the tree the rules can be edited and the
objects are no longer needed to rebuild them.

This lifts IBM's objects into that form and holds the tree against them.
`write' lifts an object and prints it; `read' parses it back; `check' does both
and holds the bytecode emitted from each against the other, byte for byte,
which is the whole proof. The form itself -- the printing and the parsing -- is
in tools/delta-lower.py, because tools/delta-upper.py writes the same form from
above and a second copy of the print and the parse is the one fault it can
have.

What the text is not is the readable C that `delta-decompile.py` writes. That
restructures into loops and conditionals for a person to read, and inverting it
exactly would be hard. Reading and round-tripping are different jobs, so they
have different forms.

usage: delta-notation.py write  <object> [> file]
       delta-notation.py rewrite         the two generated files, lifted only
       delta-notation.py authored        the same with the upper form in
       delta-notation.py authored-check  and that held against the tree
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

# Which language this reads. English is the only one with a notation tree, but
# the reader and writer are the machine's rather than English's, so a rule from
# another module can be round-tripped to check that a form covers it -- which is
# how the floating-point form was checked, since no English rule has any.
LANG = os.environ.get("EVV_NOTATION_LANG", "enus")
OBJECTS = os.path.join(ROOT, "analysis", LANG)

# The emitter names every table after the language, and it has to be told
# which: a table named for English in Italian's module is how one language's
# rules end up under another's name.
de.TAG[0] = LANG

# The lower notation itself -- the registers, the operands, the operations and
# a rule as its blocks -- is in tools/delta-lower.py, because the compiler in
# tools/delta-upper.py writes the same form from above and a second copy of
# the print and the parse is the one fault this form can have.
dlo = load("delta_lower", "tools/delta-lower.py")
Written = dlo.Written
write_rule = dlo.write_rule
read_rules = dlo.read_rules


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


TREE = os.path.join(ROOT, "lang", LANG, "rules")
# Which language this speaks for. Only English has a notation tree so far; the
# tables carry the language's name since two of them can be linked into one
# program, so the name is wanted in three places and is written down once.
TAG = LANG
SHIPPED = os.path.join(ROOT, "lang", TAG, "delta_rules_%s.c" % TAG)
SHIPPED_H = os.path.join(ROOT, "lang", TAG, "delta_rules_%s.h" % TAG)
# Each rule under its own name, standing in for the compiled one. It is the
# emitter's to write and follows from the same rule list, so it belongs
# wherever that list is built rather than only in the lifter.
SHIPPED_SHIM = os.path.join(ROOT, "lang", TAG,
                            "delta_rules_shim_%s.c" % TAG)


def shipped_bytecode():
    """The bytecode the engine actually runs, out of the tree."""
    import re as _re
    s = open(SHIPPED).read()
    m = _re.search(r"const uint8_t %s_delta_rule_code\[\] = \{(.*?)\};" % TAG,
                   s, _re.S)
    if m is None:
        raise ValueError("no %s_delta_rule_code in %s" % (TAG, SHIPPED))
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
                                        os.path.join(tmp, "consts.c"), TAG)
    out = ["# Where each address the rules name falls: which store of the",
           "# language's own bytes, and how far into it. Written by",
           "# tools/delta-notation.py out of IBM's objects. The bytes",
           "# themselves are in delta_consts_enus.c.",
           "#",
           "# An address is named by the object that compiled it and the",
           "# symbol it had there, because that pair is what a rule holds and",
           "# a list in order would only be right as long as no rule is ever",
           "# added, moved or written afresh.",
           ""]
    for st in stores:
        out.append("store %s" % st)
    for (obj, real), nm in zip(e.sym.items, names):
        store, _plus, off = nm.split()
        out.append("at %s %s %s %s" % (obj, real, store, off))
    open(SYMBOLS, "w").write("\n".join(out) + "\n")
    print("%d stores and %d addresses in %s"
          % (len(stores), len(names), os.path.relpath(SYMBOLS, ROOT)))
    return True


def read_symbols():
    """The stores, and where each object's symbol falls in one of them."""
    stores = []
    where = {}
    for line in open(SYMBOLS):
        w = line.split()
        if not w or w[0].startswith("#"):
            continue
        if w[0] == "store":
            stores.append(w[1])
        elif w[0] == "at":
            where[(w[1], w[2])] = "%s + %s" % (w[3], w[4])
    return stores, where


def symbol_table(e, where):
    """The addresses the rules name, in the order the pool numbered them.

    A rule that names a constant nothing has named before -- which is what
    writing a new rule means -- would otherwise be handed an index into a
    table that stops short of it, and read whatever lies after. So a symbol
    with nowhere recorded is said out loud here rather than left to be heard.
    """
    out = []
    for obj, real in e.sym.items:
        # A constant of ours belongs to the language rather than to any one
        # object, so it is recorded against none and answers whoever asks.
        if (obj, real) not in where and ("*", real) in where:
            obj = "*"
        if (obj, real) not in where:
            raise ValueError(
                "%s names %s, and %s does not say where that is. A constant"
                " of our own is added by tools/delta-consts.py."
                % (obj, real, os.path.relpath(SYMBOLS, ROOT)))
        out.append(where[(obj, real)])
    return out


# The one file in the tree written in the other upper form, the wrappers as
# what they stand for. It is read by `upper-prove' and by nothing else, and
# the real-rule compiler would not know what to do with a line of it.
WRAPPERS = "wrappers.up"


def text_files():
    """The tree's rule files, in the order the emitter takes them: every
    object's in the order of the objects, and glob.obj's thunks last. The
    pools are numbered by that order, so it is not ours to choose."""
    files = sorted(f for f in os.listdir(TREE)
                   if f.endswith(".dr") or (f.endswith(".up")
                                            and f != WRAPPERS))
    stems = sorted(set(f.rsplit(".", 1)[0] for f in files))
    return ([f for f in stems if f != "glob"] + [f for f in stems
                                                 if f == "glob"])


def text_rules(upper=True):
    """Every rule the engine runs, out of the text alone.

    A rule written in the upper form stands in for the one of the same name
    in the lower, in the same position, so that the rule keeps the number
    everything else reaches it by. A name that is only in the upper form
    comes after that object's own.

    Asked for the lower form alone, it ignores the upper files alogether,
    which is what the check that the lifted text still reproduces the tree
    wants: a rule written afresh is meant to differ, so counting it there
    would turn that check red and leave it red.
    """
    du = load("delta_upper", "tools/delta-upper.py") if upper else None
    out = []
    authored = []
    for stem in text_files():
        low = os.path.join(TREE, stem + ".dr")
        up = os.path.join(TREE, stem + ".up")
        rules, tables = ([], {})
        if os.path.exists(low):
            rules, tables = read_rules(open(low))
        if upper and os.path.exists(up):
            written = du.compile_file(up, LANG)
            by_name = dict((r[0], r) for r in written)
            rules = [by_name.pop(name, (name, d, obj))
                     for name, d, obj in rules]
            rules += [by_name[r[0]] for r in written if r[0] in by_name]
            authored += [r[0] for r in written]
        out.append((stem, rules, tables))
    return out, authored


def from_text(upper=True):
    """The whole language's rules in one emitter, out of the text."""
    e = de.Emitter()
    n = 0
    files, authored = text_rules(upper)
    for _stem, rules, tables in files:
        for name, d, obj in rules:
            e.rule(name, d, tables, obj)
            e.origin[name] = obj
            n += 1
    if authored:
        print("rules written in the upper form: %d (%s)"
              % (len(authored), ", ".join(sorted(authored))))
    return e, n


def regenerate(write=False, upper=None):
    """Write the engine's bytecode file out of the text, and see whether it is
    the one in the tree.

    This is the whole point of the exercise: if it matches, the rules can be
    rebuilt from text a person can edit, and IBM's objects are wanted for the
    comparison suite and nothing else.

    Asked to write, it puts the two files in the language's own directory
    instead of comparing, which is how a rule written in the upper form gets
    into a build. Nothing else about the two is different.

    What it compares is the lifted text alone, because a rule written afresh
    in the upper form is meant to differ from the one it stands in for and
    counting it here would leave this check red for as long as the rule
    existed. `authored\' is what writes those in, and tools/upper-check.sh is
    what says whether they do the same thing.
    """
    import tempfile
    e, n = from_text(upper=write if upper is None else upper)
    print("rules read out of %s: %d (no object opened)"
          % (os.path.relpath(TREE, ROOT), n))

    stores, where = read_symbols()
    names = symbol_table(e, where)
    print("stores %d, addresses %d of the %d recorded"
          % (len(stores), len(names), len(where)))

    if write:
        de.write_c(e, None, SHIPPED, SHIPPED_H, None, stores, names, TAG)
        de.write_shims(e, SHIPPED_SHIM, None)
        for what in (SHIPPED, SHIPPED_H, SHIPPED_SHIM):
            print("%-26s %d bytes, written"
                  % (os.path.basename(what), os.path.getsize(what)))
        return True

    with tempfile.TemporaryDirectory() as tmp:
        out_c = os.path.join(tmp, "delta_rules_enus.c")
        out_h = os.path.join(tmp, "delta_rules_%s.h" % TAG)
        out_shim = os.path.join(tmp, "shim.c")
        de.write_c(e, None, out_c, out_h, None, stores, names, TAG)
        de.write_shims(e, out_shim, None)
        got = open(out_c, "rb").read()
        got_h = open(out_h, "rb").read()
        got_shim = open(out_shim, "rb").read()

    ok = True
    for what, made, have in (("delta_rules_%s.c" % TAG, got, SHIPPED),
                             ("delta_rules_%s.h" % TAG, got_h, SHIPPED_H),
                             ("delta_rules_shim_%s.c" % TAG, got_shim,
                              SHIPPED_SHIM)):
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

    # And the text as it stands in the tree, which is what gets built from.
    # Everything above re-lifts and writes the text afresh, so it tests the
    # writer and the reader and says nothing about whether what is stored is
    # still current. That is exactly how a stale tree slipped past this once.
    from_tree = de.Emitter()
    files = sorted(f for f in os.listdir(TREE) if f.endswith(".dr"))
    files = ([f for f in files if f != "glob.dr"]
             + [f for f in files if f == "glob.dr"])
    for f in files:
        rules, tables = read_rules(open(os.path.join(TREE, f)))
        for name, d, obj in rules:
            from_tree.rule(name, d, tables, obj)
            from_tree.origin[name] = obj

    text = bytes(from_text.code)
    lift_ = bytes(from_lift.code)
    tree = bytes(from_tree.code)
    print("rules taken: %d" % n)
    print("written afresh: %d, from a fresh lift: %d, from the tree: %d,"
          " what the engine runs: %d"
          % (len(text), len(lift_), len(tree), len(want)))

    ok = True
    if text != lift_:
        print("the text and a fresh lift do not agree")
        ok = False
    if tree != want:
        print("the text in the tree is stale and does not reproduce the"
              " bytecode the engine runs; write it again with"
              " make notation")
        ok = False
    if text != want:
        print("a text written afresh does not reproduce the bytecode")
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

    if what == "authored":
        return 0 if regenerate(write=True) else 1

    if what == "authored-check":
        return 0 if regenerate(upper=True) else 1

    if what == "rewrite":
        return 0 if regenerate(write=True, upper=False) else 1

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
