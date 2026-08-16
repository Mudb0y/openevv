#!/usr/bin/env python3
"""Turn the lifted rules into bytecode the interpreter can run.

The front end in delta-lift.py hands back each rule as blocks of operations
over operands. This writes those out as a byte stream, with the constants,
the string addresses and the runtime entry points pulled into pools beside
it so the stream itself carries only indices.

Two things the rules name cannot be written as C identifiers: the string
constants the Microsoft compiler mangled, and nothing else. Those are
declared here under names that can be, and a rename file is written beside
the source for the link to answer the real names with; see the Makefile.
"""

import collections
import importlib.util
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

spec = importlib.util.spec_from_file_location(
    "delta_lift", os.path.join(ROOT, "tools", "delta-lift.py"))
dl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dl)

# The opcodes. Kept in one place so the interpreter can be checked against
# this list by eye.
OPS = [
    "CALL", "JUMP", "BRANCH", "CMP", "ALU2", "ALU1", "LOAD", "STORE",
    "SWITCH", "MAP", "RETURN", "SCALE", "ADDK", "MUL", "DIV", "WIDEN",
    "SETCC",
]
OP = {name: i for i, name in enumerate(OPS)}

# What an operand can be. A value stands for itself; a location has to be
# read through, at whatever width the operation asks for.
KINDS = ["NONE", "IMM", "SYM", "SLOT", "SLOTADDR", "STATE", "STATEFLD",
         "REG", "IND"]
K = {name: i for i, name in enumerate(KINDS)}

# The conditions the rules branch on, and the widths and kinds of the
# operations that set the flags they read.
CONDS = ["e", "ne", "a", "ae", "b", "be", "g", "ge", "l", "le", "s", "ns"]
COND = {name: i for i, name in enumerate(CONDS)}

CMPS = ["testl", "testw", "testb", "cmpl", "cmpw", "cmpb"]
CMPK = {name: i for i, name in enumerate(CMPS)}

ALUS = ["addl", "addw", "subl", "subw", "andl", "andw", "orl", "orw",
        "incl", "incw", "decl", "decw", "shll", "shlw", "sarl", "sarw",
        "negl", "negw", "sbbl", "imull", "imulw"]
ALUK = {name: i for i, name in enumerate(ALUS)}

MOVS = ["movl", "movw", "movb", "movswl", "movzwl", "movsbl", "movzbl"]
MOVK = {name: i for i, name in enumerate(MOVS)}

# The eight general registers in the order the machine numbers them, and the
# widths a name can address one at.
REGNUM = {"eax": 0, "ecx": 1, "edx": 2, "ebx": 3,
          "esp": 4, "ebp": 5, "esi": 6, "edi": 7}
WIDE = {"e": 0}


def reg_code(name):
    """A register name as a byte: which register, and how much of it."""
    n = name.lstrip("%")
    if n in REGNUM:
        return REGNUM[n]
    if len(n) == 2 and n[1] == "x" and "e" + n in REGNUM:
        return 0x10 | REGNUM["e" + n]
    if n in ("si", "di", "sp", "bp"):
        return 0x10 | REGNUM["e" + n]
    if len(n) == 2 and n[1] == "l" and "e" + n[0] + "x" in REGNUM:
        return 0x20 | REGNUM["e" + n[0] + "x"]
    if len(n) == 2 and n[1] == "h" and "e" + n[0] + "x" in REGNUM:
        return 0x30 | REGNUM["e" + n[0] + "x"]
    raise ValueError("register %s" % name)


class Pool:
    """A list with no repeats, remembering where each thing landed."""

    def __init__(self):
        self.items = []
        self.index = {}

    def add(self, item):
        if item not in self.index:
            self.index[item] = len(self.items)
            self.items.append(item)
        return self.index[item]


def raw_bytes(obj):
    """The bytes of every labelled region of an object, by label.

    The tag maps the backtracker dispatches through are data sitting in the
    middle of the code, so they have to be read as bytes rather than
    disassembled. Everything is read here and only the maps are kept.
    """
    text = subprocess.run(
        ["llvm-objdump", "-d", obj], check=True,
        capture_output=True, text=True).stdout
    out = {}
    label = None
    for line in text.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
        if m:
            label = m.group(1)
            out[label] = bytearray()
            continue
        if label is None:
            continue
        m = re.match(r"^\s+[0-9a-f]+:\s+((?:[0-9a-f]{2} )+)", line)
        if m:
            out[label].extend(int(b, 16) for b in m.group(1).split())
    return out


class Emitter:
    def __init__(self):
        self.code = bytearray()
        self.imm = Pool()
        self.sym = Pool()
        self.entry = Pool()
        self.maps = bytearray()
        self.mapat = {}
        self.rules = []
        self.origin = {}
        self.obj = ""
        self.trouble = collections.Counter()

    # ---- the byte stream -------------------------------------------------

    def u8(self, v):
        self.code.append(v & 0xff)

    def u16(self, v):
        self.code.append(v & 0xff)
        self.code.append((v >> 8) & 0xff)

    def hole16(self):
        at = len(self.code)
        self.u16(0)
        return at

    def patch16(self, at, v):
        self.code[at] = v & 0xff
        self.code[at + 1] = (v >> 8) & 0xff

    # ---- operands --------------------------------------------------------

    def operand(self, op, pbase):
        if op is None:
            self.u8(K["NONE"])
            return
        kind = op[0]
        if kind == "imm":
            self.u8(K["IMM"])
            self.u16(self.imm.add(op[1] & 0xffffffff))
        elif kind == "sym":
            self.u8(K["SYM"])
            self.u16(self.sym.add((self.obj, op[1])))
        elif kind == "param":
            self.u8(K["SLOT"])
            self.u16(pbase + 4 * op[1])
        elif kind == "paramaddr":
            self.u8(K["SLOTADDR"])
            self.u16(pbase + 4 * op[1])
        elif kind == "slot":
            self.u8(K["SLOT"])
            self.u16(op[1])
        elif kind == "slotaddr":
            self.u8(K["SLOTADDR"])
            self.u16(op[1])
        elif kind == "state":
            self.u8(K["STATE"])
            self.u16(op[1])
        elif kind == "statefld":
            self.u8(K["STATEFLD"])
            self.u16(op[1])
        elif kind == "reg":
            self.u8(K["REG"])
            self.u8(reg_code(op[1]))
        elif kind == "loaded":
            self.u8(K["REG"])
            self.u8(reg_code(op[3]))
        elif kind == "indirect":
            self.u8(K["IND"])
            self.operand(op[1], pbase)
            self.u16(op[2])
        else:
            raise ValueError("operand %r" % (op,))

    # ---- one rule --------------------------------------------------------

    def rule(self, name, d, bytes_by_label, obj):
        self.obj = obj
        start = len(self.code)
        labels = {}
        fixups = []

        def target(text):
            fixups.append((self.hole16(), d.resolve(text)))

        for _label, addr, block in d.blocks:
            labels[addr] = len(self.code)
            for op in block:
                kind = op[0]
                if kind == "call":
                    self.u8(OP["CALL"])
                    self.u16(self.entry.add(op[1]))
                    self.u8(len(op[2]))
                    for a in op[2]:
                        self.operand(a, d.pbase)
                elif kind == "jump":
                    self.u8(OP["JUMP"])
                    target(op[1])
                elif kind == "branch":
                    self.u8(OP["BRANCH"])
                    self.u8(COND[op[1][1:]])
                    target(op[2])
                elif kind == "cmp":
                    self.u8(OP["CMP"])
                    self.u8(CMPK[op[1]])
                    self.operand(op[2], d.pbase)
                    self.operand(op[3], d.pbase)
                elif kind == "alu":
                    if op[2] is None:
                        self.u8(OP["ALU1"])
                        self.u8(ALUK[op[1]])
                        self.operand(op[3], d.pbase)
                    else:
                        self.u8(OP["ALU2"])
                        self.u8(ALUK[op[1]])
                        self.operand(op[2], d.pbase)
                        self.operand(op[3], d.pbase)
                elif kind == "load":
                    self.u8(OP["LOAD"])
                    self.u8(MOVK[op[1]])
                    self.operand(op[2], d.pbase)
                    self.u8(reg_code(op[3]))
                elif kind == "store":
                    self.u8(OP["STORE"])
                    self.u8(MOVK[op[1]])
                    self.operand(op[2], d.pbase)
                    self.operand(op[3], d.pbase)
                elif kind == "switch":
                    self.u8(OP["SWITCH"])
                    self.operand(op[2], d.pbase)
                    self.u16(len(op[1]))
                    for t in op[1]:
                        target(t)
                elif kind == "map":
                    self.u8(OP["MAP"])
                    self.u16(self.map_at(op[1], bytes_by_label))
                    self.operand(op[2], d.pbase)
                    self.u8(reg_code(op[3]))
                elif kind == "return":
                    self.u8(OP["RETURN"])
                    self.operand(op[1], d.pbase)
                elif kind == "scale":
                    self.u8(OP["SCALE"])
                    self.u16(self.imm.add(op[1] & 0xffffffff))
                    self.operand(op[2], d.pbase)
                    self.operand(op[3], d.pbase)
                    self.u8(op[4])
                    self.u8(reg_code(op[5]))
                elif kind == "addk":
                    self.u8(OP["ADDK"])
                    self.u16(self.imm.add(op[1] & 0xffffffff))
                    self.operand(op[2], d.pbase)
                    self.u8(reg_code(op[3]))
                elif kind == "mul":
                    self.u8(OP["MUL"])
                    self.u8(ALUK[op[1]])
                    self.operand(op[2], d.pbase)
                    self.operand(op[3], d.pbase)
                    self.u8(reg_code(op[4]))
                elif kind == "div":
                    self.u8(OP["DIV"])
                    self.u8(1 if op[1] == "idivl" else 0)
                    self.operand(op[2], d.pbase)
                elif kind == "widen":
                    self.u8(OP["WIDEN"])
                    self.u8(1 if op[1] == "cltd" else 0)
                elif kind == "setcc":
                    self.u8(OP["SETCC"])
                    self.u8(COND[op[1]])
                    self.u8(reg_code(op[2]))
                else:
                    raise ValueError("operation %r" % (kind,))

        for at, addr in fixups:
            if addr not in labels:
                raise ValueError("jump to 0x%x, which is nowhere" % (addr or 0))
            self.patch16(at, labels[addr] - start)

        self.rules.append((name, start, len(self.code) - start,
                           d.frame, d.pbase, d.params))

    def map_at(self, label, bytes_by_label):
        if label in self.mapat:
            return self.mapat[label]
        body = bytes_by_label.get(label)
        if body is None:
            raise ValueError("no bytes for the %s table" % label)
        self.mapat[label] = len(self.maps)
        self.maps.extend(body)
        return self.mapat[label]


def c_name(obj, sym, n):
    """A name for a string constant, unique across the whole language.

    Every object numbers its own strings from one, so the names collide;
    and the ones the Microsoft compiler mangled are not C identifiers at
    all. Both get a fresh name here, and the object they came from is told
    to answer to it. """
    return "evv_%s_%d" % (re.sub(r"[^A-Za-z0-9_]", "_", obj[:-4]), n)


def write_c(e, where, out_c, out_h, out_syms):
    names = []
    per_obj = collections.defaultdict(list)
    for n, (obj, real) in enumerate(e.sym.items):
        name = c_name(obj, real, n)
        per_obj[obj].append((real, name))
        names.append(name)
    e.sym_renames = per_obj

    with open(out_c, "w") as f:
        f.write("/* Generated by tools/delta-emit.py. Do not edit.\n"
                "\n"
                "   The language's rules as bytecode, with the constants,\n"
                "   the string addresses and the runtime entry points they\n"
                "   name pulled out beside them. */\n\n")
        f.write('#include "delta_rules.h"\n\n')

        f.write("const uint8_t delta_rule_code[] = {\n")
        for i in range(0, len(e.code), 16):
            f.write("    " + ",".join("%d" % b for b in e.code[i:i + 16])
                    + ",\n")
        f.write("};\n\n")

        f.write("const int32_t delta_rule_imm[] = {\n")
        for i in range(0, len(e.imm.items), 8):
            f.write("    " + ",".join(
                "%d" % ((v ^ 0x80000000) - 0x80000000)
                for v in e.imm.items[i:i + 8]) + ",\n")
        f.write("};\n\n")

        if e.maps:
            f.write("const uint8_t delta_rule_map[] = {\n")
            for i in range(0, len(e.maps), 16):
                f.write("    " + ",".join("%d" % b
                                          for b in e.maps[i:i + 16]) + ",\n")
            f.write("};\n\n")
        else:
            f.write("const uint8_t delta_rule_map[] = { 0 };\n\n")

        f.write("/* The runtime the rules call. Declared without argument\n"
                "   lists because every arity from none to twenty-five\n"
                "   appears among them, and each call site says how many it\n"
                "   is passing. */\n")
        for name in e.entry.items:
            if name != "setjmp3":
                f.write("extern void %s();\n" % name)
        f.write("\nconst delta_rule_fn delta_rule_entry[] = {\n")
        for name in e.entry.items:
            if name == "setjmp3":
                f.write("    0,  /* planted by the interpreter itself */\n")
            else:
                f.write("    (delta_rule_fn)%s,\n" % name)
        f.write("};\n\n")

        f.write("/* Their names, for saying what a run did. */\n"
                "const char *const delta_rule_entry_name[] = {\n")
        for name in e.entry.items:
            f.write('    "%s",\n' % name)
        f.write("};\n\n")

        f.write("/* The language's string constants. */\n")
        for name in names:
            f.write("extern const char %s[];\n" % name)
        f.write("\nconst void *const delta_rule_sym[] = {\n")
        for name in names:
            f.write("    %s,\n" % name)
        f.write("};\n\n")

        f.write("const delta_rule delta_rules[] = {\n")
        for name, off, length, frame, pbase, params in e.rules:
            f.write('    { "%s", "%s", %d, %d, %d, %d, %d },\n'
                    % (name, e.origin.get(name, ""), off, length, frame,
                       pbase, params))
        f.write("};\n\n")
        f.write("const int delta_rule_count = %d;\n\n" % len(e.rules))
        f.write("/* The landing place a rule plants for a backtrack is not\n"
                "   a call the interpreter can make on its behalf. */\n")
        f.write("const int delta_rule_setjmp = %d;\n"
                % (e.entry.index.get("setjmp3", -1)))

    biggest = max((r[3] + r[4] + 4 * r[5]) for r in e.rules) if e.rules else 0
    with open(out_h, "w") as f:
        f.write("/* Generated by tools/delta-emit.py. Do not edit. */\n\n"
                "#ifndef DELTA_RULES_H\n#define DELTA_RULES_H\n\n"
                "#include <stdint.h>\n\n"
                "typedef void (*delta_rule_fn)(void);\n\n"
                "typedef struct {\n"
                "    const char *name;\n"
                "    const char *object;   /* which one it was compiled in */\n"
                "    int32_t     offset;   /* into the bytecode */\n"
                "    int32_t     length;\n"
                "    int32_t     frame;    /* bytes below the frame base */\n"
                "    int32_t     pbase;    /* where its arguments start */\n"
                "    int32_t     params;\n"
                "} delta_rule;\n\n"
                "extern const uint8_t      delta_rule_code[];\n"
                "extern const int32_t      delta_rule_imm[];\n"
                "extern const uint8_t      delta_rule_map[];\n"
                "extern const delta_rule_fn delta_rule_entry[];\n"
                "extern const char *const delta_rule_entry_name[];\n"
                "extern const void *const  delta_rule_sym[];\n"
                "extern const delta_rule   delta_rules[];\n"
                "extern const int          delta_rule_count;\n"
                "extern const int          delta_rule_setjmp;\n\n"
                "/* The largest frame any rule asks for, base and arguments\n"
                "   included, so one buffer serves them all. */\n"
                "#define DELTA_RULE_FRAME_MAX %d\n\n"
                "int32_t delta_run_rule(void *state, const delta_rule *r,\n"
                "                       const int32_t *args, int nargs);\n"
                "const delta_rule *delta_find_rule(const char *name);\n\n"
                "#endif\n" % biggest)

    return sum(len(v) for v in per_obj.values())


def write_shims(e, out_c, out_ren):
    """One C function per rule, standing in for the compiled one.

    Everything that reaches a rule by name reaches this instead, which hands
    the rule's own arguments to the interpreter. The rename file beside it
    puts each compiled rule out of the way so the link answers with these.
    """
    with open(out_c, "w") as f:
        f.write("/* Generated by tools/delta-emit.py. Do not edit.\n\n"
                "   Each of the language's rules under its own name, run by\n"
                "   the interpreter rather than by the code its compiler\n"
                "   generated. The first argument is always the machine. */\n\n")
        f.write('#include "delta_rules.h"\n\n')
        for i, (name, _off, _len, _fr, _pb, params) in enumerate(e.rules):
            n = max(params, 1)
            args = ", ".join("int32_t a%d" % j for j in range(n))
            f.write("int32_t %s(%s)\n{\n" % (name, args))
            f.write("    int32_t a[%d];\n\n" % n)
            for j in range(n):
                f.write("    a[%d] = a%d;\n" % (j, j))
            f.write("    return delta_run_rule((void *)(intptr_t)a0,\n"
                    "                          &delta_rules[%d], a, %d);\n}\n\n"
                    % (i, n))
    return len(e.rules)


def defining_objects(where):
    """Which object defines each name, so that the one to be stood aside
    from can be told apart from the ones that only call it."""
    out = {}
    for obj in sorted(f for f in os.listdir(where) if f.endswith(".obj")):
        text = subprocess.run(["llvm-nm", os.path.join(where, obj)],
                              capture_output=True, text=True).stdout
        for line in text.splitlines():
            m = re.match(r"^[0-9a-f]+ [TtDdBbRr] _(\w+)$", line.strip())
            if m and m.group(1) not in out:
                out[m.group(1)] = obj
    return out


def write_trace(e, where, out_c, out_dir, known):
    """A wrapper for every runtime entry the rules call, saying what it was
    asked before handing over.

    Linked into a run of the engine as it was and into a run with the
    interpreter, the two say the same things about themselves, so the first
    place they part company can be found. """
    defs = defining_objects(where)
    rules = set(name for name, *_ in e.rules)
    wrapped = []
    by_obj = collections.defaultdict(list)

    for name in e.entry.items:
        if name in rules or name == "setjmp3":
            continue
        obj = defs.get(name)
        if obj is None:
            continue
        wrapped.append((name, known.get(name, 1)))
        by_obj[obj].append(name)

    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)
    for obj, names in sorted(by_obj.items()):
        with open(os.path.join(out_dir, obj[:-4] + ".ren"), "w") as f:
            for name in sorted(names):
                f.write("_%s _ibm_%s\n" % (name, name))
    with open(os.path.join(out_dir, "objects"), "w") as f:
        for obj in sorted(by_obj):
            f.write("%s\n" % obj[:-4])

    with open(out_c, "w") as f:
        f.write("/* Generated by tools/delta-emit.py. Do not edit. */\n\n"
                "#include <stdio.h>\n#include <stdlib.h>\n"
                "#include <stdint.h>\n\n"
                "static int on = -1;\n\n"
                "static void say(const char *name, const int32_t *a, int n)\n"
                "{\n"
                "    int i;\n\n"
                "    if (on < 0)\n"
                "        on = getenv(\"DELTA_CALL_TRACE\") != 0;\n"
                "    if (!on)\n        return;\n"
                "    fprintf(stderr, \"  %s(\", name);\n"
                "    for (i = 0; i < n; i++)\n"
                "        fprintf(stderr, \"%s%08x\", i ? \", \" : \"\",\n"
                "                (unsigned)a[i]);\n"
                "    fprintf(stderr, \")\\n\");\n"
                "    fflush(stderr);\n}\n\n")
        for name, n in wrapped:
            args = ", ".join("int32_t a%d" % j for j in range(n))
            f.write("extern int32_t ibm_%s();\n" % name)
            f.write("int32_t %s(%s)\n{\n" % (name, args))
            f.write("    int32_t a[%d];\n\n" % n)
            for j in range(n):
                f.write("    a[%d] = a%d;\n" % (j, j))
            f.write('    say("%s", a, %d);\n' % (name, n))
            f.write("    return ibm_%s(%s);\n}\n\n"
                    % (name, ", ".join("a%d" % j for j in range(n))))
    return len(wrapped), len(by_obj)


def write_reference(e, out_c):
    """The same rules again, but each one only says it ran and then hands
    over to the code the language's compiler generated.

    Linked in place of the stand-ins it gives a run of the original engine
    that says the same things about itself as a run of the interpreted one,
    so the two can be set side by side and the first place they part
    company found. """
    with open(out_c, "w") as f:
        f.write("/* Generated by tools/delta-emit.py. Do not edit. */\n\n"
                "#include <stdio.h>\n#include <stdlib.h>\n"
                '#include "delta_rules.h"\n\n'
                "static long calls;\n"
                "static int on = -1;\n\n"
                "static void say(const char *name, const int32_t *a, int n)\n"
                "{\n"
                "    int i;\n\n"
                "    if (on < 0)\n"
                "        on = getenv(\"DELTA_RULE_TRACE\") != 0;\n"
                "    if (!on)\n        return;\n"
                "    fprintf(stderr, \"rule %ld: %s(\", ++calls, name);\n"
                "    for (i = 0; i < n; i++)\n"
                "        fprintf(stderr, \"%s%08x\", i ? \", \" : \"\",\n"
                "                (unsigned)a[i]);\n"
                "    fprintf(stderr, \")\\n\");\n"
                "    fflush(stderr);\n}\n\n")
        for name, _off, _len, _fr, _pb, params in e.rules:
            n = max(params, 1)
            args = ", ".join("int32_t a%d" % j for j in range(n))
            f.write("extern int32_t ibm_%s();\n" % name)
            f.write("int32_t %s(%s)\n{\n" % (name, args))
            f.write("    int32_t a[%d];\n\n" % n)
            for j in range(n):
                f.write("    a[%d] = a%d;\n" % (j, j))
            f.write('    say("%s", a, %d);\n' % (name, n))
            f.write("    return ibm_%s(%s);\n}\n\n"
                    % (name, ", ".join("a%d" % j for j in range(n))))
    return len(e.rules)


def write_renames(e, where):
    """One rename file per object that defines rules.

    It has to be per object rather than one list for everything: renaming a
    rule everywhere would take the calls other objects make to it with it,
    and those are exactly the calls that have to arrive at the stand-in. */
    """
    if not os.path.isdir(where):
        os.makedirs(where)
    by_obj = collections.defaultdict(list)
    for name, obj in e.origin.items():
        by_obj[obj].append(name)
    for obj in sorted(set(by_obj) | set(e.sym_renames)):
        with open(os.path.join(where, obj[:-4] + ".ren"), "w") as f:
            for name in sorted(by_obj.get(obj, [])):
                f.write("_%s _ibm_%s\n" % (name, name))
            for real, name in e.sym_renames.get(obj, []):
                f.write("%s _%s\n" % (real if real.startswith("?")
                                       else "_" + real, name))
        with open(os.path.join(where, obj[:-4] + ".glob"), "w") as f:
            for _real, name in e.sym_renames.get(obj, []):
                f.write("_%s\n" % name)
    with open(os.path.join(where, "objects"), "w") as f:
        for obj in sorted(set(by_obj) | set(e.sym_renames)):
            f.write("%s\n" % obj[:-4])
    return len(by_obj)


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    where = argv[0] if argv else os.path.join(ROOT, "analysis", "enus")
    out = os.path.join(ROOT, "src")

    # One pass to learn how many arguments each entry takes, since a call
    # reached by a path that did not write them cannot say for itself.
    arity = collections.defaultdict(collections.Counter)
    for obj in sorted(f for f in os.listdir(where) if f.endswith(".obj")):
        for name, items in dl.read_functions(os.path.join(where, obj)):
            if not dl.is_rule(items):
                continue
            data, tables = dl.find_data(items)
            d = dl.Decoder(name, items, data, tables).run()
            for _l, _s, block in d.blocks:
                for op in block:
                    if op[0] == "call" and op[2]:
                        arity[op[1]][len(op[2])] += 1
    known = {k: v.most_common(1)[0][0] for k, v in arity.items()}

    e = Emitter()
    failed = collections.Counter()

    for obj in sorted(f for f in os.listdir(where) if f.endswith(".obj")):
        path = os.path.join(where, obj)
        funcs = list(dl.read_functions(path))
        if not any(dl.is_rule(items) for _n, items in funcs):
            continue
        raws = None
        for name, items in funcs:
            if not dl.is_rule(items):
                continue
            data, tables = dl.find_data(items)
            d = dl.Decoder(name, items, data, tables, known).run()
            if d.holes:
                failed["holes"] += 1
                continue
            if raws is None:
                raws = raw_bytes(path)
            try:
                e.rule(name, d, raws, obj)
                e.origin[name] = obj
            except ValueError as err:
                failed[str(err).split(" ")[0]] += 1
                if failed[str(err).split(" ")[0]] < 3:
                    print("  %s in %s: %s" % (name, obj, err))

    print("rules emitted: %d" % len(e.rules))
    print("bytecode: %d bytes" % len(e.code))
    print("constants: %d, strings: %d, entry points: %d"
          % (len(e.imm.items), len(e.sym.items), len(e.entry.items)))
    print("tag maps: %d bytes" % len(e.maps))
    if failed:
        print("rules not emitted: %s" % dict(failed))

    n = write_c(e, where,
                os.path.join(out, "delta_rules_enus.c"),
                os.path.join(out, "delta_rules.h"),
                None)
    print("string constants named afresh: %d" % n)
    m = write_shims(e, os.path.join(out, "delta_rules_shim.c"), None)
    print("stand-in rules written: %d" % m)
    write_reference(e, os.path.join(out, "delta_rules_ref.c"))
    k = write_renames(e, os.path.join(out, "rules"))
    print("objects whose rules step aside: %d" % k)
    w, wo = write_trace(e, where, os.path.join(out, "delta_rules_trace.c"),
                        os.path.join(out, "trace"), known)
    print("runtime entries wrapped for a trace: %d over %d objects" % (w, wo))
    return 0


if __name__ == "__main__":
    sys.exit(main())
