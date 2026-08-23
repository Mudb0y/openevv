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

# Which language is being written, and what that makes its names. Every
# table a module defines carries it -- enus_delta_rules, dede_delta_rules --
# because a program may have several modules in it and IBM gave them all the
# same names. A one-element list so that the naming helpers below can be
# plain functions and still see it.
TAG = ["enus"]


def N(name):
    """What this language calls one of its own tables."""
    return "%s_%s" % (TAG[0], name)

spec = importlib.util.spec_from_file_location(
    "delta_lift", os.path.join(ROOT, "tools", "delta-lift.py"))
dl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dl)

spec = importlib.util.spec_from_file_location(
    "delta_link", os.path.join(ROOT, "tools", "delta-link.py"))
dlk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dlk)

# The opcodes. Kept in one place so the interpreter can be checked against
# this list by eye.
OPS = [
    "CALL", "JUMP", "BRANCH", "CMP", "ALU2", "ALU1", "LOAD", "STORE",
    "SWITCH", "MAP", "RETURN", "SCALE", "ADDK", "MUL", "DIV", "WIDEN",
    "SETCC", "PUSH", "SETARG", "POPN", "POPREG", "FTOL",
]
OP = {name: i for i, name in enumerate(OPS)}

# What an operand can be. A value stands for itself; a location has to be
# read through, at whatever width the operation asks for.
KINDS = ["NONE", "IMM", "SYM", "SLOT", "SLOTADDR", "STATE", "STATEFLD",
         "REG", "IND"]
K = {name: i for i, name in enumerate(KINDS)}

# The steps of one floating-point expression: an integer pushed, then a
# constant or another integer combined into it. Nothing else appears.
FSTEP = {"ld": 0, "addi": 1, "mulk": 2, "addk": 3}

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
                    self.u8(op[2])
                    self.u8(min(op[3], 255))
                elif kind == "push":
                    self.u8(OP["PUSH"])
                    self.operand(op[1], d.pbase)
                elif kind == "setarg":
                    self.u8(OP["SETARG"])
                    self.u8(op[1])
                    self.operand(op[2], d.pbase)
                elif kind == "popn":
                    self.u8(OP["POPN"])
                    self.u8(op[1])
                elif kind == "popreg":
                    self.u8(OP["POPREG"])
                    self.u8(reg_code(op[1]))
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
                elif kind == "ftol":
                    # A little floating point, which only the Frenches have.
                    # The double constants go in the ordinary constant pool as
                    # two halves, so nothing new has to be carried beside the
                    # rules to hold them.
                    self.u8(OP["FTOL"])
                    self.u8(len(op[1]))
                    for step in op[1]:
                        self.u8(FSTEP[step[0]])
                        if step[0] in ("ld", "addi"):
                            self.operand(step[1], d.pbase)
                        else:
                            self.u16(self.imm.add(step[1] & 0xffffffff))
                            self.u16(self.imm.add((step[1] >> 32)
                                                  & 0xffffffff))
                    self.u8(reg_code(op[2]))
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
            where = labels[addr] - start
            if where > 0xffff:
                raise SystemExit(
                    "delta-emit: rule %s is %d bytes, and a jump names a "
                    "place in it as sixteen bits, so it cannot reach %d"
                    % (d.name, len(self.code) - start, where))
            self.patch16(at, where)

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
    return "%s_evv_%s_%d" % (TAG[0], re.sub(r"[^A-Za-z0-9_]", "_", obj[:-4]), n)


def store_name(obj, secname, number):
    """A section is named for its object, itself and its number, because an
    object may hold several sections of the same name."""
    return "%s_evv_%s%s_%d" % (TAG[0],
                               re.sub(r"[^A-Za-z0-9_]", "_", obj[:-4]),
                               re.sub(r"[^A-Za-z0-9_]", "_", secname), number)


def write_consts(e, where, out_c, tag="enus"):
    """The bytes behind every address the rules name.

    Each one is somewhere in a data section of the object it was compiled
    with, and the sections come out whole rather than one array per name,
    because nothing says a rule never reads past the end of the string it
    asked for. What the rules get is an offset into the section.

    Nothing outside the rules reaches any of these, which is what makes a
    copy of them safe: they are the compiler's own constants, local to the
    object, and the code that named them is code we run ourselves.
    """
    defs = defining_objects(where)
    local = local_symbols(where)
    seen = {}
    names = []
    reader = {}
    for obj, real in e.sym.items:
        # A name the object keeps to itself belongs to that object however
        # many others number a string the same way; only a name of its own
        # across the language is worth looking up.
        who = obj if local.get((obj, real)) else defs.get(real, obj)
        o = reader.get(who)
        if o is None:
            o = reader[who] = dlk.Coff(os.path.join(where, who))
        key = real if real.startswith("?") else "_" + real
        if key not in o.symbol:
            raise ValueError("%s is not in %s" % (real, who))
        section, value = o.symbol[key]
        store = store_name(who, o.secname[section], section)
        seen[store] = o.section[section]
        names.append("%s + %d" % (store, value))

    with open(out_c, "w") as f:
        f.write("/* Generated by tools/delta-emit.py. Do not edit.\n"
                "\n"
                "   Everything the rules name by address: the language's\n"
                "   string constants and the compiler's own, as they lie in\n"
                "   the object each was compiled into.\n"
                "\n"
                "   A pronunciation changed in lang/%s/%s.dict is appended\n"
                % (tag, tag) +
                "   here by tools/delta-dict.py, which gives the edited\n"
                "   action a record of its own rather than writing over the\n"
                "   one it had, since several actions can name the same\n"
                "   bytes. Running this lifter again puts IBM's own back\n"
                "   and loses those edits. */\n"
                "\n#include <stdint.h>\n"
                "\n#include \"delta_rules_%s.h\"\n" % tag)
        for store in sorted(seen):
            data = seen[store]
            f.write("\nuint8_t %s[%d] = {\n" % (store, len(data)))
            for i in range(0, len(data), 16):
                f.write("    " + ",".join("%d" % b for b in data[i:i + 16])
                        + ",\n")
            f.write("};\n")
    with open(out_c, "a") as f:
        f.write("\n/* Where each store is and how big, so that startup can\n"
                "   copy them somewhere a value can name. src/delta_syms.c is\n"
                "   the only reader: the machine holds addresses of these, and\n"
                "   an address in the program is not one a value can hold\n"
                "   unless the program was linked low, which a shared library\n"
                "   cannot be. sizeof rather than a number, so that a record\n"
                "   appended by tools/delta-dict.py is counted without this\n"
                "   table being touched. */\n")
        f.write("const delta_store %s[] = {\n" % N("delta_const_store"))
        for store in sorted(seen):
            f.write("    { %s, sizeof %s },\n" % (store, store))
        f.write("    { 0, 0 },\n};\n")

    return sorted(seen), names


def write_c(e, where, out_c, out_h, out_syms, stores, names, tag="enus"):
    e.sym_renames = collections.defaultdict(list)

    with open(out_c, "w") as f:
        f.write("/* Generated by tools/delta-emit.py. Do not edit.\n"
                "\n"
                "   The language's rules as bytecode, with the constants,\n"
                "   the string addresses and the runtime entry points they\n"
                "   name pulled out beside them.\n"
                "\n"
                "   A pronunciation changed in lang/%s/%s.dict is written\n"
                % (tag, tag) +
                "   back here by tools/delta-dict.py: the pools grow, and\n"
                "   the edited action gets a block of its own at the end of\n"
                "   its rule with the switch pointed at it. Running this\n"
                "   lifter again puts IBM's own back and loses those\n"
                "   edits. */\n\n")
        f.write('#include "delta_rules_%s.h"\n\n' % tag)

        f.write("const uint8_t %s[] = {\n" % N("delta_rule_code"))
        for i in range(0, len(e.code), 16):
            f.write("    " + ",".join("%d" % b for b in e.code[i:i + 16])
                    + ",\n")
        f.write("};\n\n")

        f.write("const int32_t %s[] = {\n" % N("delta_rule_imm"))
        for i in range(0, len(e.imm.items), 8):
            f.write("    " + ",".join(
                "%d" % ((v ^ 0x80000000) - 0x80000000)
                for v in e.imm.items[i:i + 8]) + ",\n")
        f.write("};\n\n")

        if e.maps:
            f.write("const uint8_t %s[] = {\n" % N("delta_rule_map"))
            for i in range(0, len(e.maps), 16):
                f.write("    " + ",".join("%d" % b
                                          for b in e.maps[i:i + 16]) + ",\n")
            f.write("};\n\n")
        else:
            f.write("const uint8_t %s[] = { 0 };\n\n"
                    % N("delta_rule_map"))

        f.write("/* The runtime the rules call. Declared without argument\n"
                "   lists because every arity from none to twenty-five\n"
                "   appears among them, and each call site says how many it\n"
                "   is passing. */\n")
        mine = set(r[0] for r in e.rules)
        for name in e.entry.items:
            if name != "setjmp3":
                f.write("extern void %s();\n"
                        % (N(name) if name in mine else name))
        f.write("\nconst delta_rule_fn %s[] = {\n"
                % N("delta_rule_entry"))
        for name in e.entry.items:
            if name == "setjmp3":
                f.write("    0,  /* planted by the interpreter itself */\n")
            else:
                f.write("    (delta_rule_fn)%s,\n"
                        % (N(name) if name in mine else name))
        f.write("};\n\n")

        f.write("/* Their names, for saying what a run did. */\n"
                "const char *const %s[] = {\n"
                % N("delta_rule_entry_name"))
        for name in e.entry.items:
            f.write('    "%s",\n' % name)
        f.write("};\n\n")

        f.write("/* The language's string constants, and whatever else a\n"
                "   rule names by address. */\n")
        for name in stores:
            f.write("extern uint8_t %s[];\n" % name)

        f.write("\nconst void *const %s[] = {\n" % N("delta_rule_sym"))
        for name in names:
            f.write("    %s,\n" % name)
        f.write("};\n\n")
        f.write("const int %s = %d;\n\n"
                % (N("delta_rule_sym_count"), len(names)))

        f.write("const delta_rule %s[] = {\n" % N("delta_rules"))
        for name, off, length, frame, pbase, params in e.rules:
            f.write('    { "%s", "%s", %d, %d, %d, %d, %d },\n'
                    % (name, e.origin.get(name, ""), off, length, frame,
                       pbase, params))
        f.write("};\n\n")
        f.write("const int %s = %d;\n\n"
                % (N("delta_rule_count"), len(e.rules)))
        f.write("/* The landing place a rule plants for a backtrack is not\n"
                "   a call the interpreter can make on its behalf. */\n")
        f.write("const int %s = %d;\n"
                % (N("delta_rule_setjmp"), e.entry.index.get("setjmp3", -1)))

    biggest = max((r[3] + r[4] + 4 * r[5]) for r in e.rules) if e.rules else 0
    up = tag.upper()   # for the guard on the header
    with open(out_h, "w") as f:
        f.write("/* Generated by tools/delta-emit.py. Do not edit.\n\n"
                "   What this language calls its own tables. Every one of\n"
                "   them carries the language, because a program may have\n"
                "   several language modules in it and IBM gave the tables\n"
                "   the same names in every one; src/delta_lang.h says how\n"
                "   the engine reaches whichever is in force, and\n"
                "   delta_lang_%s.c is where these are gathered. */\n\n"
                "#ifndef DELTA_RULES_%s_H\n#define DELTA_RULES_%s_H\n\n"
                "#include <stdint.h>\n\n"
                "#include \"delta_lang.h\"\n\n"
                "extern const delta_store  %s[];\n"
                "extern const uint8_t      %s[];\n"
                "extern const int32_t      %s[];\n"
                "extern const uint8_t      %s[];\n"
                "extern const delta_rule_fn %s[];\n"
                "extern const char *const  %s[];\n"
                "extern const void *const  %s[];\n"
                "extern const int          %s;\n"
                "extern const delta_rule   %s[];\n"
                "extern const int          %s;\n"
                "extern const int          %s;\n\n"
                % (tag, up, up,
                   N("delta_const_store"), N("delta_rule_code"),
                   N("delta_rule_imm"), N("delta_rule_map"),
                   N("delta_rule_entry"), N("delta_rule_entry_name"),
                   N("delta_rule_sym"), N("delta_rule_sym_count"),
                   N("delta_rules"), N("delta_rule_count"),
                   N("delta_rule_setjmp")))

        f.write("/* And the same under the plain names, for the generated C\n"
                "   beside this, which says what the rules said. Safe as\n"
                "   macros because only this language's own files include\n"
                "   this header, and each of them includes just the one. */\n")
        for nm in ("delta_const_store", "delta_rule_code", "delta_rule_imm",
                   "delta_rule_map", "delta_rule_entry",
                   "delta_rule_entry_name", "delta_rule_sym",
                   "delta_rule_sym_count", "delta_rules", "delta_rule_count",
                   "delta_rule_setjmp"):
            f.write("#define %-22s %s\n" % (nm, N(nm)))
        f.write("\n")

        f.write("/* Every entry a rule can call. A call in the decompiled C\n"
                "   names the entry it was written against rather than the\n"
                "   index this table happens to give it. Plain names, not\n"
                "   the language's: only this language's own files read\n"
                "   them, and each of those includes only this header. */\n"
                "enum {\n")
        for i, nm in enumerate(e.entry.items):
            f.write("    DELTA_ENTRY_%s = %d,\n" % (nm, i))
        f.write("};\n\n")

        f.write("/* The largest frame any rule asks for, base and arguments\n"
                "   included, so one buffer serves them all. */\n"
                "#define DELTA_RULE_FRAME_MAX %d\n\n"
                "#endif\n" % biggest)

    return len(names)


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
                "   generated. The first argument is always the machine.\n\n"
                "   The names carry the language in front of them, because\n"
                "   two languages share most of these and a program may have\n"
                "   both in it. What a run reports is the name without it,\n"
                "   which is what the rule table holds. */\n\n")
        f.write('#include "delta_rules_%s.h"\n\n' % TAG[0])
        for i, (name, _off, _len, _fr, _pb, params) in enumerate(e.rules):
            n = max(params, 1)
            args = ", ".join("int32_t a%d" % j for j in range(n))
            f.write("int32_t %s(%s)\n{\n" % (N(name), args))
            f.write("    int32_t a[%d];\n\n" % n)
            for j in range(n):
                f.write("    a[%d] = a%d;\n" % (j, j))
            f.write("    return delta_run_rule((void *)(intptr_t)a0,\n"
                    "                          &%s[%d], a, %d);\n}\n\n"
                    % (N("delta_rules"), i, n))
    return len(e.rules)


def local_symbols(where):
    """Which names each object keeps to itself.

    Every object numbers its own string constants from one, so those names
    collide and have to be renamed; a name that is already global belongs to
    one object only and is left alone. """
    out = {}
    for obj in sorted(f for f in os.listdir(where) if f.endswith(".obj")):
        text = subprocess.run(["llvm-nm", os.path.join(where, obj)],
                              capture_output=True, text=True).stdout
        for line in text.splitlines():
            m = re.match(r"^[0-9a-f]+ ([a-z]) (\S+)$", line.strip())
            if m:
                out[(obj, m.group(2).lstrip("_"))] = True
    return out


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


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    where = argv[0] if argv else os.path.join(ROOT, "analysis", "enus")
    # Where the lifted C goes. Reading a language other than the one the
    # engine is built from is for comparison, so it writes beside its objects
    # rather than into lang/enus, where it would stand on the English build.
    out = argv[1] if len(argv) > 1 else os.path.join(ROOT, "lang", "enus")
    # Which language this is, which is what the written files are named for.
    # The objects say so themselves -- one language to a library -- so it is
    # taken from the directory they were unpacked into unless told otherwise.
    tag = argv[2] if len(argv) > 2 else os.path.basename(where.rstrip("/\\"))
    TAG[0] = tag
    os.makedirs(out, exist_ok=True)

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
                        arity[op[1]][op[2]] += 1
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

    rules_done = len(e.rules)

    # The helper thunks the compiler generated beside the rules. They are
    # not rules, so nothing above picks them up, but they are the same kind
    # of thing: a fixed run of calls into the runtime, and the interpreter
    # runs them the same way.
    glob = os.path.join(where, "glob.obj")
    if os.path.exists(glob):
        raws = None
        for name, items in dl.read_functions(glob):
            if not name.startswith("ZZ"):
                continue
            data, tables = dl.find_data(items)
            d = dl.Decoder(name, items, data, tables, known).run()
            if d.holes:
                failed["holes"] += 1
                continue
            # A thunk passes its caller's arguments through, and the ones it
            # never reads itself are still there to be passed on, so where
            # the call sites say more than it reads, they are right.
            d.params = max(d.params, known.get(name, 0))
            if raws is None:
                raws = raw_bytes(glob)
            try:
                e.rule(name, d, raws, "glob.obj")
                e.origin[name] = "glob.obj"
            except ValueError as err:
                failed[str(err).split(" ")[0]] += 1

    print("rules emitted: %d, helper thunks: %d"
          % (rules_done, len(e.rules) - rules_done))
    print("bytecode: %d bytes" % len(e.code))
    print("constants: %d, strings: %d, entry points: %d"
          % (len(e.imm.items), len(e.sym.items), len(e.entry.items)))
    print("tag maps: %d bytes" % len(e.maps))
    if failed:
        print("rules not emitted: %s" % dict(failed))

    stores, names = write_consts(e, where,
                                 os.path.join(out, "delta_consts_%s.c" % tag),
                                 tag)
    print("stores of named bytes: %d" % len(stores))
    n = write_c(e, where,
                os.path.join(out, "delta_rules_%s.c" % tag),
                os.path.join(out, "delta_rules_%s.h" % tag),
                None, stores, names, tag)
    print("addresses the rules name: %d" % n)
    m = write_shims(e, os.path.join(out, "delta_rules_shim_%s.c" % tag), None)
    print("stand-in rules written: %d" % m)
    return 0


if __name__ == "__main__":
    sys.exit(main())
