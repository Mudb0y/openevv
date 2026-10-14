#!/usr/bin/env python3
"""Lift the rules of an ETI Eloquence 6.1 language module into the upper form.

Apple ships ETI's 6.1 engine with VoiceOver, and its language modules carry
the rules as 64-bit code compiled from the same Delta compiler's output that
IBM's 4.3 objects were compiled from, under the same rule names, calling the
same runtime by the same names with the same arguments. What differs is the
dress: arguments in registers rather than pushed, a different compiler's
register choices, pointers twice as wide, and three of the runtime's smallest
operations written out inline rather than called. So this reads a rule the way
tools/rules/lift.py reads one of IBM's, instruction by instruction with nothing
guessed, and writes it in the upper form, which is what our compiler already
turns into the machine's notation.

Every instruction of every rule has to fall into one of the shapes below or it
is counted as a hole and named. A rule with a hole is not written.

What a register holds is followed through the rule. A value that can be named
wherever it is read -- a constant, the address of a local or a variable, a
string, the rule's own arguments -- is named where it is read. Anything else
that lives in a register across a branch or a call is given a local of its
own, so the upper form says exactly what the register said.

usage: tools/rules/lift64.py census <module.dylib>
       tools/rules/lift64.py rule <module.dylib> <name>
"""

import collections
import struct
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


# ---- reading the module -------------------------------------------------

INSN = re.compile(r"^\s+([0-9a-f]+):\s+(\S+)\s*(.*?)\s*$")
HEAD = re.compile(r"^([0-9a-f]+) <_?(.+)>:$")
NOTE = re.compile(r"##\s*0x([0-9a-f]+)(?:\s+<_?([^>+]+)(?:\+0x([0-9a-f]+))?>)?")


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
    tail = text[start:].strip()
    if tail:
        parts.append(tail)
    return parts


class Module:
    """The functions of one module, its symbols and its bytes."""

    def __init__(self, path):
        self.path = path
        self.bytes = open(path, "rb").read()
        nm = subprocess.run(["llvm-nm", "-n", path], check=True,
                            capture_output=True, text=True).stdout
        self.symbols = []
        for line in nm.splitlines():
            w = line.split()
            if len(w) == 3:
                self.symbols.append((int(w[0], 16), w[1], w[2].lstrip("_")))
        self.data = sorted((a, n) for a, k, n in self.symbols
                           if k in "dDsSbB")
        # Each source file's own copies of runtime entries, which the
        # compiler specialised: a fence that takes only the machine is
        # fence(state, 0, null_str) with the constants folded in.
        self.private = {a: n for a, k, n in self.symbols if k == "t"}
        dis = subprocess.run(["llvm-objdump", "-d", "--no-show-raw-insn",
                              path], check=True, capture_output=True,
                             text=True).stdout
        # Where the stubs into the C library sit, and the table their
        # addresses are loaded from: setjmp and the stack check are called
        # through the first, and the stack check's guard read out of the
        # second. Both move from one module to the next.
        heads = subprocess.run(["llvm-objdump", "-h", path], check=True,
                               capture_output=True, text=True).stdout
        self.sections = {}
        for line in heads.splitlines():
            w = line.split()
            if len(w) >= 4 and w[0].isdigit():
                self.sections.setdefault(w[1], (int(w[3], 16),
                                                int(w[3], 16) + int(w[2], 16)))
        self.stubs_at = self.sections.get("__stubs", (0, 0))
        # Which entry of another library each stub is, by name: the C
        # library's exp and log are reached through them.
        self.stub_names = {}
        ind = subprocess.run(["llvm-objdump", "--macho", "--indirect-symbols",
                              path], capture_output=True, text=True).stdout
        inside = False
        for line in ind.splitlines():
            if line.startswith("Indirect symbols for"):
                inside = "__stubs" in line
                continue
            w = line.split()
            if inside and len(w) == 3 and w[0].startswith("0x"):
                self.stub_names[int(w[0], 16)] = w[2].lstrip("_")
        self.got_at = self.sections.get("__got", (0, 0))
        self.functions = collections.OrderedDict()
        self.at = {}
        name = None
        # A name can belong to a rule and to a source file's private helper
        # both -- gettok is a rule and also a function of the runtime's own --
        # so where a name is said twice, the one the module exports keeps it
        # and a private one is known by its address as well. Until this was
        # said, the private gettok answered for the rule, and a call to the
        # rule was handed the private one's arguments.
        said = collections.Counter(m.group(2) for m in map(HEAD.match,
                                                            dis.splitlines())
                                   if m)
        for line in dis.splitlines():
            m = HEAD.match(line)
            if m:
                name = m.group(2)
                addr = int(m.group(1), 16)
                if said[name] > 1 and addr in self.private:
                    name = "%s@%x" % (name, addr)
                self.functions[name] = []
                self.at[addr] = name
                continue
            m = INSN.match(line)
            if not m and name is not None and "##" in line and \
                    self.functions[name] and self.functions[name][-1][3] is None:
                # A comment that says what a shuffle does takes the line and
                # the address it reads goes on the next one.
                n = NOTE.search(line[line.index("##"):])
                if n:
                    a_, m_, o_, _ = self.functions[name][-1]
                    self.functions[name][-1] = (a_, m_, o_,
                                                (int(n.group(1), 16),
                                                 n.group(2)))
                continue
            if not m or name is None:
                continue
            text = m.group(3)
            note = None
            if "##" in text:
                text, _, rest = text.partition("##")
                n = NOTE.search("##" + rest)
                if n:
                    note = (int(n.group(1), 16), n.group(2))
            self.functions[name].append(
                (int(m.group(1), 16), m.group(2), split_operands(text.strip()),
                 note))

    def in_strings(self, addr):
        lo, hi = self.sections.get("__cstring", (0, 0))
        return lo <= addr < hi

    def record_lengths(self):
        """How long a record of each statement type is, out of the module's
        own statement table: an entry is 0x60 bytes and the length is a word
        0x44 into it."""
        if getattr(self, "_lengths", None) is None:
            base = [a for a, k, n in self.symbols if n == "vstmtbl"]
            self._lengths = {}
            if base:
                for i in range(32):
                    at = base[0] + i * 0x60
                    if not int.from_bytes(self.bytes[at:at + 8], "little"):
                        break
                    self._lengths[i] = self.int32(at + 0x44)
        return self._lengths

    def int32(self, addr):
        return int.from_bytes(self.bytes[addr:addr + 4], "little", signed=True)

    def data_symbol(self, addr):
        """The data symbol at an address and its bytes up to the next one."""
        import bisect
        i = bisect.bisect_right(self.data, (addr, "￿")) - 1
        if i < 0 or self.data[i][0] != addr:
            return None, b""
        end = self.data[i + 1][0] if i + 1 < len(self.data) else addr + 64
        return self.data[i][1], self.bytes[addr:end]

    def rules(self):
        return [n for n, ins in self.functions.items()
                if any(m == "callq" and o and o[0].endswith("<_ventproc>")
                       for _a, m, o, _n in ins)]


# ---- registers ----------------------------------------------------------

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
# The sixteen that hold 6.1's doubles, which here hold fixed values and are
# kept the way any other register is.
XMM = tuple("xmm%d" % _k for _k in range(16))
# The one-time answer the machine may have left for a dispatch, read through
# whichever register holds the state.
ANSWER = re.compile(r"^0x68\(%r[a-z0-9]+\)$")
for _x in XMM:
    REG[_x] = _x


def reg_of(text):
    return REG.get(text.lstrip("%"))


def reg_width(text):
    t = text.lstrip("%")
    if t.startswith("xmm"):
        return 8
    if t.startswith("r") and (t[1:].isdigit() or t in ("rax", "rbx", "rcx",
                                                         "rdx", "rsi", "rdi",
                                                         "rbp", "rsp")):
        return 8
    if t.endswith("d") or t.startswith("e"):
        return 4
    if t.endswith("w") or t in ("ax", "bx", "cx", "dx", "si", "di"):
        return 2
    return 1


ARGS = ("rdi", "rsi", "rdx", "rcx", "r8", "r9")
# The registers a call may change. The sixteen that hold doubles are not
# among them: 6.1 was built so that the compiler knew which of its own
# functions touch them, and it keeps a double in one across a call to any
# that does not -- which is every entry of the runtime a rule calls.
CALLER = ("rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11")
MEM = re.compile(r"^(-?0x[0-9a-f]+|-?\d+)?\((%[a-z0-9]+)(?:,(%[a-z0-9]+)(?:,(\d))?)?\)$")

# Values that can be named wherever they are read.
PURE = ("imm", "laddr", "gaddr", "sym", "state", "param", "code")

CONDS = {"je": "e", "jne": "ne", "jl": "l", "jge": "ge", "jg": "g",
         "jle": "le", "jb": "b", "jae": "ae", "ja": "a", "jbe": "be",
         "js": "s", "jns": "ns"}
WORDS = {"e": "is", "ne": "is not", "l": "is less than", "ge": "is at least",
         "g": "is more than", "le": "is at most", "b": "is below",
         "ae": "is not below", "a": "is above", "be": "is not above"}
MOVES = ("movq", "movl", "movw", "movb", "movzwl", "movzbl", "movswl",
         "movsbl", "movslq", "movzbw", "movsbw", "movzwq", "movzbq",
         "movswq", "movsbq")
WIDTH_OF = {"movq": 8, "movl": 4, "movw": 2, "movb": 1, "movzwl": 2,
            "movzbl": 1, "movswl": 2, "movsbl": 1, "movslq": 4, "movzbw": 1,
            "movsbw": 1, "movzwq": 2, "movzbq": 1, "movswq": 2, "movsbq": 1}
OPPOSITE = {"e": "ne", "ne": "e", "l": "ge", "ge": "l", "g": "le", "le": "g",
            "b": "ae", "ae": "b", "a": "be", "be": "a", "s": "ns", "ns": "s"}

# What 6.1 does in doubles and the machine here in fixed values, and the
# constant the two differ by.
SSE = ("movsd", "movapd", "movaps", "movups", "movupd", "xorps", "xorpd",
       "unpcklpd", "shufpd", "divpd", "haddpd", "movlpd", "movddup", "mulpd",
       "unpckhpd",
       "cvtsi2sd", "cvtsi2sdl",
       "cvtsi2sdq", "cvttsd2si", "cvttsd2siq", "mulsd", "divsd", "addsd",
       "subsd", "ucomisd")
ONE = 10000


def as_fixed(raw):
    """A double as a whole number of ten-thousandths, rounded to the nearest:
    0.85 is held exactly as 8500 where the double only approximates it."""
    v = struct.unpack("<d", raw)[0]
    n = int(abs(v) * ONE + 0.5)
    return -n if v < 0 else n


# How far under a whole number a fixed value may fall and still be read as
# it when 6.1 truncates a double: one unit, a ten-thousandth.
SLACK = 1

# Entries of 6.1's runtime handed a double, in xmm0, after how many of the
# ordinary arguments; ours are handed the fixed value as one more of those.
FLOAT_ARGS = {"move_f": 1, "npush_f": 0}
# Entries 6.1 kept the name of and changed what they do, which a lifted rule
# calls under the name of 6.1's version so that a 4.3 language is untouched.
AS_61 = {"dur_ass": "dur_ass61"}
# The C library's mathematics, which a few rules call on a double and which
# the runtime here does in fixed values.
LIBM = {"exp": "fixed_exp", "log": "fixed_log", "exp2": "fixed_exp2",
        "pow": "fixed_pow"}

# The entries of the runtime that are handed a place in one of the language's
# lists, which list, and which argument besides the state it is.
LISTED = {"setd_lookup": ("set", 1), "actd_lookup": ("action", 0)}

SIGNED_OF = {"movswl": True, "movsbl": True, "movslq": True, "movsbw": True,
             "movswq": True, "movsbq": True}

class Hole(Exception):
    pass


def our_arities(lang="enus"):
    """How many arguments after the state each of our runtime's entries
    takes, as our own rules call them: every call in the lower notation says
    how many it hands over, the state included."""
    from evv import ROOT
    rules = set()
    calls = collections.defaultdict(collections.Counter)
    where = os.path.join(ROOT, "lang", lang, "rules")
    for f in sorted(os.listdir(where)):
        if not f.endswith(".dr"):
            continue
        for line in open(os.path.join(where, f)):
            w = line.split()
            if not w:
                continue
            if w[0] == "rule":
                rules.add(w[1])
            elif w[0] == "call" and len(w) >= 4 and w[2] == "arity":
                calls[w[1]][int(w[3])] += 1
    out = {name: c.most_common(1)[0][0] - 1 for name, c in calls.items()
           if name not in rules and not name.startswith("ZZ")}
    # What the runtime's own declarations say wins: the lower notation counts
    # what was on the stack at the call, and a slot an earlier call left there
    # is counted with it.
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "entrysig", os.path.join(ROOT, "tools", "rules", "entrysig.py"))
    es = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(es)
    # Except where the declaration ends in an ellipsis, which says nothing
    # of how many a call hands over: registerPhoneme takes eighteen.
    variadic = set()
    for base, _dirs, files in os.walk(os.path.join(ROOT, "src")):
        for fn in files:
            if fn.endswith((".c", ".h")):
                text = open(os.path.join(base, fn), encoding="utf-8").read()
                variadic |= set(re.findall(r"\b(\w+)\s*\([^()]*\.\.\.\s*\)",
                                           text))
    for name in list(out):
        sig = es._read().get(name)
        if sig is not None and sig[1] and name not in variadic:
            out[name] = len(sig[1]) - 1
    VARIADIC.update(variadic)
    return out


# Our entries declared with an ellipsis, which a call may hand any number.
VARIADIC = set()


# Entries whose shape 6.1 changed: how many arguments 6.1 hands over, and how
# to make ours out of them. forall_cont_from lost the argument ours still
# takes and never reads, the third.
SHAPE = {"forall_cont_from": (4, lambda a: a[:2] + [(("imm", 0), None)] + a[2:])}


ARITY = None


class Rule:
    """One rule, read into blocks of statements in the upper form."""

    def __init__(self, module, name):
        self.m = module
        self.name = name
        # A parity jump follows a comparison of doubles and is taken only when
        # one was not a number, which a fixed value never is: so jp is never
        # taken and jnp always.
        self.ins = [(a, "nop" if m == "jp" else "jmp" if m == "jnp" else m,
                     o, n) for a, m, o, n in module.functions[name]]
        self.state_in_rbx()
        self.by_addr = {a: i for i, (a, _m, _o, _n) in enumerate(self.ins)}
        self.lo = self.ins[0][0]
        self.hi = self.ins[-1][0] + 16
        self.holes = []
        self.tables = {}
        self.table_steps = set()
        self.table_index = {}

    def state_in_rbx(self):
        """Everything here reads the machine's state as rbx, which is where
        clang keeps it in all but a few rules; in those it is another of the
        registers a call keeps, and rbx something else. The two are swapped
        by name throughout, which changes nothing the rule does."""
        other = None
        for _a, m, o, _n in self.ins[:16]:
            if m == "movq" and o and o[0] == "%rdi" and \
                    reg_of(o[1]) in ("r12", "r13", "r14", "r15", "rbx"):
                other = reg_of(o[1])
                break
        if other in (None, "rbx"):
            return
        names = {"%rbx": "%" + other, "%ebx": "%" + other + "d",
                 "%bx": "%" + other + "w", "%bl": "%" + other + "b"}
        names.update({v: k for k, v in list(names.items())})
        pat = re.compile("|".join(re.escape(k) + r"\b" for k in names))

        def swap(t):
            return pat.sub(lambda mm: names[mm.group(0)], t)
        self.ins = [(a, m, [swap(x) for x in o], n) for a, m, o, n in self.ins]

    # -- control flow ----------------------------------------------------

    def target(self, ops):
        m = re.match(r"^0x([0-9a-f]+)", ops[0])
        return int(m.group(1), 16) if m else None

    def table_at(self, i):
        """The dispatch whose indirect jump is instruction i: its base and
        how many entries it has. The compiler always writes it the same way:
        a bound checked with a compare and a `ja', a sign-extending load out
        of the table and an add of the base back in."""
        base_reg = None
        index_reg = None
        count = None
        # The register jumped through, the one the table's base is added
        # from and the one the entry is chosen by are whichever the compiler
        # picked. The constants every entry shares can sit between the add
        # and the jump, and so can a copy of the count into every register
        # and local a place it leads to keeps it in -- fifteen of them in
        # Portuguese's por_vals -- so the bound may be two dozen back.
        jump_reg = reg_of(self.ins[i][2][0].lstrip("*"))
        same = set()
        for j in range(i - 1, max(i - 40, -1), -1):
            _a, m, o, _n = self.ins[j]
            if m == "addq" and len(o) == 2 and reg_of(o[1]) == jump_reg and \
                    base_reg is None:
                base_reg = reg_of(o[0])
            if m == "movslq" and len(o) == 2 and reg_of(o[1]) == jump_reg \
                    and index_reg is None:
                mm = re.match(r"^\((%[a-z0-9]+),(%[a-z0-9]+),4\)$", o[0])
                if mm:
                    index_reg = reg_of(mm.group(2))
                    same = {index_reg}
            # The bound may be tested on the register the index was widened
            # or copied from, and at any width.
            if m in ("movzwl", "movzbl", "movl", "movq") and len(o) == 2 and \
                    o[0].startswith("%") and reg_of(o[1]) in same:
                same.add(reg_of(o[0]))
            if m in ("cmpl", "cmpq", "cmpw", "cmpb") and \
                    o[0].startswith("$") and \
                    reg_of(o[1]) in (same or {jump_reg}):
                count = int(o[0].lstrip("$"), 0) + 1
                break
        if base_reg is None or count is None:
            return None
        bases = set()
        for _a, m, o, n in self.ins:
            if m == "leaq" and len(o) == 2 and reg_of(o[1]) == base_reg \
                    and "(%rip)" in o[0] and n and self.lo <= n[0] < self.hi:
                bases.add(n[0])
        if len(bases) > 1:
            # Several tables through one register: the one loaded nearest
            # before the jump is this jump's.
            near = [n for _a, m, o, n in self.ins[max(i - 40, 0):i]
                    if m == "leaq" and len(o) == 2 and
                    reg_of(o[1]) == base_reg and "(%rip)" in o[0] and n]
            bases = {near[-1][0]} if near else set()
        if len(bases) != 1:
            return None
        base = bases.pop()
        targets = [base + self.m.int32(base + 4 * k) for k in range(count)]
        # The load out of the table and the add of its base, which a switch
        # written out as tests leaves out.
        for j in range(i - 1, max(i - 16, -1), -1):
            _a, m, o, _n = self.ins[j]
            if (m == "movslq" and o and o[0].startswith("(%")) or \
                    (m == "addq" and len(o) == 2 and reg_of(o[0]) == base_reg
                     and reg_of(o[1]) == jump_reg):
                self.table_steps.add(_a)
        self.table_index[self.ins[i][0]] = index_reg or jump_reg
        return base, targets

    def walk(self):
        """Every instruction reachable from the entry, and the blocks they
        fall into. What is not reachable is data: the compiler puts the jump
        tables in among the code."""
        seen = set()
        work = [self.lo]
        leaders = {self.lo}
        while work:
            a = work.pop()
            while a in self.by_addr and a not in seen:
                seen.add(a)
                i = self.by_addr[a]
                _a, m, o, _n = self.ins[i]
                nxt = self.ins[i + 1][0] if i + 1 < len(self.ins) else None
                if m in CONDS:
                    t = self.target(o)
                    leaders.add(t)
                    leaders.add(nxt)
                    work.append(t)
                elif m == "jmp":
                    t = self.target(o)
                    leaders.add(t)
                    work.append(t)
                    if nxt is not None:
                        leaders.add(nxt)
                    break
                elif m == "jmpq":
                    tab = self.table_at(i)
                    if tab is None:
                        self.holes.append("jmpq %s" % ",".join(o))
                        break
                    self.tables[a] = tab
                    for t in tab[1]:
                        leaders.add(t)
                        work.append(t)
                    break
                elif m == "retq":
                    break
                elif m == "callq" and o and "__stack_chk_fail" in o[0]:
                    break
                elif m == "callq" and o and self.is_stub(o[0]) == "chk":
                    break
                a = nxt
        # The dispatch starts where the one-time answer is read, even when
        # that is straight after a call in the middle of a block.
        for a in seen:
            _a, m, o, _n = self.ins[self.by_addr[a]]
            if m == "movq" and o and ANSWER.match(o[0]):
                leaders.add(a)
        # The way out starts where vretproc is handed the state, so that
        # whatever a rule does on its way there is ordinary code.
        for a in seen:
            i = self.by_addr[a]
            _a, m, o, _n = self.ins[i]
            if m == "callq" and o and o[0].endswith("<_vretproc>"):
                # Handed it out of rbx, or out of the frame where a rule that
                # wanted rbx for something else keeps it.
                prev = self.ins[i - 1] if i else None
                j = i - 1 if prev and prev[1] == "movq" and \
                    prev[2][1] == "%rdi" and (prev[2][0] == "%rbx" or
                                              prev[2][0].endswith("(%rbp)")) \
                    else i
                leaders.add(self.ins[j][0])
        self.reach = sorted(seen)
        self.leaders = sorted(x for x in leaders if x in seen)
        self.blocks = []
        cur = None
        for a in self.reach:
            if a in self.leaders or cur is None:
                cur = [a, []]
                self.blocks.append(cur)
            cur[1].append(self.by_addr[a])
        # Which block follows which, for following registers through.
        self.succ = {}
        starts = [b[0] for b in self.blocks]
        for k, (start, idx) in enumerate(self.blocks):
            last = self.ins[idx[-1]]
            _a, m, o, _n = last
            out = []
            follow = starts[k + 1] if k + 1 < len(starts) else None
            if m in CONDS:
                out = [self.target(o), self.ins[idx[-1] + 1][0]]
            elif m == "jmp":
                out = [self.target(o)]
            elif m == "jmpq":
                out = list(self.tables.get(last[0], (0, []))[1])
            elif m == "retq":
                out = []
            elif m == "callq" and o and self.is_stub(o[0]) == "chk":
                # The stack check failing, which does not come back.
                out = []
            else:
                nxt = self.ins[idx[-1] + 1][0] if idx[-1] + 1 < len(self.ins) \
                    else None
                out = [nxt] if nxt in self.by_addr and nxt in seen else []
            self.succ[start] = [t for t in out if t is not None]

    def hoist_shared_loads(self):
        """A block that starts by loading through a register every way in has
        just pointed at a variable or a local: clang shares the load and the
        code after it between ways in that each picked a different place to
        read, which leaves the register holding a different address on each
        and nothing to name it by where they meet. Each way in loads its own
        place instead, and the shared block starts after the load."""
        preds = {}
        for blk, outs in self.succ.items():
            for t in outs:
                preds.setdefault(t, []).append(blk)
        starts = dict(self.blocks)
        for blk, idx in self.blocks:
            a, m, o, _n = self.ins[idx[0]]
            if m not in MOVES or len(o) != 2 or not o[1].startswith("%"):
                continue
            mm = MEM.match(o[0])
            if not mm or mm.group(3) or \
                    mm.group(2) in ("%rbx", "%rbp", "%rsp", "%rip"):
                continue
            r = reg_of(mm.group(2))
            if reg_of(o[1]) != r or len(preds.get(blk, ())) < 2:
                continue
            d = int(mm.group(1), 0) if mm.group(1) else 0
            sites = []
            for p in preds[blk]:
                pidx = starts[p]
                k = pidx[-1]
                last = self.ins[k]
                if last[1] == "jmp" and self.target(last[2]) == blk:
                    k -= 1
                elif last[1] in CONDS or last[1] == "jmp" or \
                        self.ins[k + 1][0] != blk:
                    break
                if k < pidx[0]:
                    break
                _pa, pm, po, _pn = self.ins[k]
                pmm = MEM.match(po[0]) if pm == "leaq" else None
                if not pmm or reg_of(po[1]) != r or pmm.group(3) or \
                        pmm.group(2) not in ("%rbx", "%rbp"):
                    break
                sites.append((k, (int(pmm.group(1), 0) if pmm.group(1)
                                  else 0) + d, pmm.group(2)))
            else:
                for k, off, base in sites:
                    pa = self.ins[k][0]
                    at = ("-0x%x" % -off if off < 0 else "0x%x" % off) + \
                        "(%s)" % base
                    self.ins[k] = (pa, m, [at, o[1]], None)
                self.ins[idx[0]] = (a, "nop", [], None)

    def is_stub(self, text):
        """What a call into the stubs is: the compiler calls the C library
        through them, and only three of its entries ever appear in a rule."""
        m = re.match(r"^0x([0-9a-f]+)", text)
        if not m:
            return None
        a = int(m.group(1), 16)
        lo, hi = self.m.stubs_at
        if a < lo or a >= hi:
            return None
        return self.stubs().get(a)

    _stubs = None

    def stubs(self):
        """Which of the stubs into the C library is setjmp and which the stack
        check failing, told apart by how they are called: setjmp is handed
        the address of a frame slot and its answer is tested at once, and the
        failing check is called last, with nothing after it but padding."""
        if Rule._stubs is not None and Rule._stubs[0] is self.m:
            return Rule._stubs[1]
        out = {}
        # A rule calls exactly three: setjmp at the landing place, the stack
        # check on the way out, and nothing else. They are told apart by
        # where they are called from rather than by binding names, which the
        # disassembly does not print.
        for name, ins in self.m.functions.items():
            for k, (a, m, o, _n) in enumerate(ins):
                if m != "callq" or not o:
                    continue
                mm = re.match(r"^0x([0-9a-f]+)", o[0])
                if not mm:
                    continue
                t = int(mm.group(1), 16)
                if not (self.m.stubs_at[0] <= t < self.m.stubs_at[1]):
                    continue
                prev = ins[k - 1] if k else None
                if prev and prev[1] == "leaq" and prev[2][0].endswith("(%rbp)") \
                        and reg_of(prev[2][1]) == "rdi" and k + 1 < len(ins) \
                        and ins[k + 1][1] in ("testl", "pushq"):
                    out.setdefault(t, "setjmp")
        for name, ins in self.m.functions.items():
            for k, (a, m, o, _n) in enumerate(ins):
                if m == "callq" and o and k + 1 < len(ins) and \
                        ins[k + 1][1] in ("nop", "<unknown>", "ud2") :
                    mm = re.match(r"^0x([0-9a-f]+)", o[0])
                    if mm and self.m.stubs_at[0] <= int(mm.group(1), 16) \
                            < self.m.stubs_at[1]:
                        out.setdefault(int(mm.group(1), 16), "chk")
        Rule._stubs = (self.m, out)
        return out


    # -- reading operands --------------------------------------------------

    def value_of(self, st, text, width=None, signed=False):
        """A source operand as a value."""
        if text.startswith("$"):
            return ("imm", int(text.lstrip("$"), 0))
        if text.startswith("%"):
            r = reg_of(text)
            if r is None:
                raise Hole("register %s" % text)
            v = st["regs"].get(r, ("X",))
            if v[0] == "X":
                return ("X", r)
            if v[0] == "home" and v[2] is not None and v[2][0] in PURE:
                v = v[2]
            w = reg_width(text)
            if w < 8 and v[0] == "imm":
                return ("imm", v[1] & ((1 << (8 * w)) - 1))
            return v
        p = self.addr_of(st, text)
        if p[0] == "gotval":
            return ("canary",)
        if p[0] == "stackparam":
            return ("param", p[1])
        if p[0] == "local" and p[1] in st["slots"]:
            return st["slots"][p[1]]
        return ("mem", p, width or 4, signed, None)

    def addr_of(self, st, text):
        """What a memory operand names, as a place."""
        m = MEM.match(text)
        if not m:
            raise Hole("operand %s" % text)
        off = int(m.group(1), 0) if m.group(1) else 0
        if m.group(3):
            raise Hole("indexed %s" % text)
        if m.group(2) == "%rip":
            raise Hole("rip memory %s" % text)
        base = reg_of(m.group(2))
        if base == "rsp":
            return ("stackarg", off)
        # rbx is the machine's state but in a few rules short of registers,
        # which keep the state in the frame for a while and rbx for a number.
        if base == "rbx" and st["regs"].get("rbx", ("state",))[0] == "state":
            return ("glob", off)
        if base == "rbp":
            # Above the saved frame pointer and the return are the arguments
            # past the fifth, which the caller pushed.
            if off >= 16:
                return ("stackparam", 6 + (off - 16) // 8)
            return ("local", off)
        v = st["regs"].get(base, ("X",))
        if v[0] == "home" and v[2] is not None:
            v = v[2]
        if v[0] == "state":
            return ("glob", off)
        if v[0] == "sp":
            return ("stackarg", off)
        if v[0] == "laddr":
            return ("local", v[1] + off)
        if v[0] == "gaddr":
            return ("glob", v[1] + off)
        if v[0] == "got":
            return ("gotval", v[1])
        if v[0] == "param":
            return ("through", v, off)
        # A pointer the rule keeps in a register: its own local holds it,
        # and the place is reached through that.
        if v[0] in ("X", "mem", "saved", "stale", "ans", "home") and \
                base not in ("rsp", "rbp", "rbx"):
            if base not in self.homes or \
                    st["regs"].get(base, ("X",))[0] != "home":
                raise Need(("home", base))
            return ("through", ("home", base), off)
        raise Hole("memory through a register holding %s" % v[0])

    # -- which registers an instruction reads and writes -------------------

    def regs_read(self, i):
        """The registers an instruction reads, and the ones it writes."""
        _a, m, o, _n = self.ins[i]
        reads, writes = set(), set()
        def addr_regs(t):
            mm = MEM.match(t)
            if mm:
                for g in (mm.group(2), mm.group(3)):
                    if g and g != "%rip":
                        r = reg_of(g)
                        if r:
                            reads.add(r)
        for t in o:
            if not t.startswith("%"):
                addr_regs(t)
        if m == "callq":
            name = re.search(r"<_([^>+]+)>", o[0]) if o else None
            n = None
            if name:
                shape = SHAPE.get(name.group(1))
                n = shape[0] if shape else self.arity_of(name.group(1))
            reads |= set(ARGS[:(n if n is not None else 5) + 1])
            writes |= set(CALLER)
            return reads, writes
        if m == "pushq":
            if o[0].startswith("%"):
                reads.add(reg_of(o[0]))
            return reads, writes
        if m == "popq":
            writes.add(reg_of(o[0]))
            return reads, writes
        if m in ("xorl", "xorq") and len(o) == 2 and o[0] == o[1]:
            writes.add(reg_of(o[1]))
            return reads, writes
        if m.startswith("set"):
            writes.add(reg_of(o[0]))
            reads.add(reg_of(o[0]))
            return reads, writes
        if m == "ucomisd":
            for t in o:
                if t.startswith("%"):
                    reads.add(reg_of(t))
            return reads, writes
        srcs, dst = (o[:-1], o[-1]) if len(o) >= 2 else (o, None)
        for t in srcs:
            if t.startswith("%"):
                reads.add(reg_of(t))
        if dst and dst.startswith("%"):
            r = reg_of(dst)
            plain = m in ("movq", "movl", "leaq", "movzwl", "movzbl", "movswl",
                          "movsbl", "movslq", "movabsq")
            if not plain or m.startswith("cmov"):
                reads.add(r)
            if not (m.startswith("cmp") or m.startswith("test")):
                writes.add(r)
            if m in ("movw", "movb") or reg_width(dst) < 4:
                reads.add(r)
        elif dst and not dst.startswith("$"):
            addr_regs(dst)
        if m.startswith("cmp") or m.startswith("test"):
            for t in o:
                if t.startswith("%"):
                    reads.add(reg_of(t))
        return reads, writes

    # -- what the rule is made of ---------------------------------------
    #
    # Three kinds of block are not written as they stand. The way out calls
    # vretproc and answers what one register holds, nought for a match and
    # ninety-four for giving up, so a jump into it is `match' or `give up'
    # depending on what that register holds on the way in. The dispatch
    # reads a one-time answer the machine may have left at 0x68 and calls
    # vback when there is none, which is what our vback does by itself, then
    # jumps through a table on the number that came back: a jump into it is
    # `backtrack', and each table entry is a place bound to its number. And
    # the landing place and ventproc at the top are what our compiler writes
    # for every rule, so they and the tests of what they answer go.

    def shared_pops(self):
        """A pop a block starts with, shared by ways in that each pushed a
        constant: each of those pushes is the register being written."""
        self.push_to = {}
        self.pop_skip = set()
        preds = {}
        for blk, outs in self.succ.items():
            for t in outs:
                preds.setdefault(t, set()).add(blk)
        starts = dict(self.blocks)
        for blk, idx in self.blocks:
            _a, m, o, _n = self.ins[idx[0]]
            if m != "popq" or blk not in preds:
                continue
            r = reg_of(o[0])
            found = {}
            for p in preds[blk]:
                pidx = starts[p]
                last = self.ins[pidx[-1]]
                k = pidx[-1] if last[1] != "jmp" else pidx[-1] - 1
                if self.ins[k][1] != "pushq" or not self.ins[k][2][0].startswith("$"):
                    found = None
                    break
                found[self.ins[k][0]] = r
            if found:
                self.push_to.update(found)
                self.pop_skip.add(self.ins[idx[0]][0])

    def classify(self):
        self.shared_pops()
        self.exits = {}
        self.exit_tail = set()
        self.depth_reg = None
        self.depth_slot = self.depth_store = None
        self.slow_block = None
        self.exit_tags = {}
        self.tag_facts = {}
        self.tag_reg = "rax"
        self.dispatch_head = set()
        self.dispatch_rest = set()
        self.tags = {}
        self.frame_locals = set()
        starts = {b[0]: b for b in self.blocks}
        for start, idx in self.blocks:
            ms = [self.ins[i] for i in idx]
            if any(m == "callq" and o and o[0].endswith("<_vretproc>")
                   for _a, m, o, _n in ms):
                self.exits[start] = self.exit_answer(idx)
                for t in self.succ[start]:
                    self.exit_tail.add(t)
            first = ms[0]
            if first[1] == "movq" and first[2] and \
                    (first[2][0] == "0x68(%rbx)" or
                     (ANSWER.match(first[2][0]) and self.asks_vback(start))):
                # Through another register only where the state has been
                # taken out of the frame into it, which a call to vback on
                # one of the two ways on is what says.
                self.dispatch_head.add(start)
            # A rule that plants nothing asks only whether there is an answer
            # to clear, since it gives up whatever the answer is.
            if first[1] == "cmpq" and first[2] == ["$0x0", "0x68(%rbx)"]:
                self.dispatch_head.add(start)
        if len(self.dispatch_head) > 1:
            raise Hole("%d dispatches" % len(self.dispatch_head))
        for head in self.dispatch_head:
            self.read_dispatch(head)

    # A plant leaves a number, and the dispatch reads it back: the one-time
    # answer at 0x68 or vback's, then a decoder -- a jump table, or for a
    # few numbers a chain of compares -- that ends in the place the number
    # carries on at. Rather than read the decoder's shape, every number the
    # rule can plant is put through it, the way the machine would.
    DECODER = ("movl", "movq", "movb", "addl", "subl", "andl", "decl", "incl",
               "cmpl", "testl", "jmp", "movslq", "addq", "jmpq", "leaq",
               "xorl", "pushq", "popq", "movsd", "xorps", "xorpd", "movw",
               "movapd", "movaps") + \
        tuple(CONDS)

    def asks_vback(self, start):
        starts = {b[0]: b[1] for b in self.blocks}
        return any(m == "callq" and o and o[0].endswith("<_vback>")
                   for t in self.succ.get(start, ())
                   for _a, m, o, _n in (self.ins[i] for i in starts.get(t, ())))

    def read_dispatch(self, head):
        starts = {b[0]: b[1] for b in self.blocks}
        hidx = starts[head]
        first = self.ins[hidx[0]]
        self.tag_reg = "rax" if first[1] == "cmpq" else reg_of(first[2][1])
        # The two ways the number arrives: the fast path clears 0x68 and
        # jumps on; the slow one calls vback and falls through. Both reach
        # the decoder at the same place.
        succ = self.succ[head]
        fast = slow = None
        for t in succ:
            ms = [self.ins[i][1:3] for i in starts[t]]
            if any(m == "callq" and o[0].endswith("<_vback>") for m, o in ms):
                slow = t
            elif any(m == "andq" and o[0] == "$0x0" and ANSWER.match(o[1])
                     for m, o in ms):
                fast = t
        if fast is None or slow is None:
            raise Hole("a dispatch of another shape")
        self.dispatch_rest |= {fast, slow}
        decoder = self.succ[fast][0]
        if self.succ[slow] != [decoder]:
            raise Hole("the two halves of the dispatch part")
        # The register the count is handed to vback in, and the one it is
        # kept in across the dispatch, if any: the slow half moves the first
        # into rsi and clears the second after vback; the fast half copies
        # the first into the second.
        source = "rsi"
        cleared = set()
        after = False
        for i in starts[slow]:
            _a, m, o, _n = self.ins[i]
            if m == "callq":
                after = True
            elif not after and m == "movl" and reg_of(o[1]) == "rsi" and \
                    o[0].startswith("%"):
                source = reg_of(o[0])
            elif after and m in ("xorl", "xorq") and o[0] == o[1]:
                cleared.add(reg_of(o[1]))
        self.depth_reg = None
        for i in starts[fast]:
            _a, m, o, _n = self.ins[i]
            if m == "movl" and reg_of(o[0]) == source and \
                    reg_of(o[1]) in cleared:
                self.depth_reg = reg_of(o[1])
        # Or kept in the frame: the two halves meet on a store of the count
        # into a local, which the slow half cleared first.
        self.depth_slot = self.depth_store = None
        _a, m, o, _n = self.ins[starts[decoder][0]]
        if self.depth_reg is None and m == "movl" and len(o) == 2 and \
                reg_of(o[0]) == source and source in cleared:
            mm = MEM.match(o[1])
            if mm and mm.group(2) == "%rbp" and not mm.group(3):
                self.depth_slot = int(mm.group(1), 0)
                self.depth_store = _a
        if self.depth_reg is None and self.depth_slot is None:
            # Or a register each half sets its own way: the fast half to a
            # copy of the count and the slow one to nought, through a third
            # register it cleared after vback.
            def run(idx, slow_half):
                held = {source: "count"} if not slow_half else {}
                after = not slow_half
                for i in idx:
                    _a, m, o, _n = self.ins[i]
                    if m == "callq":
                        held = {r: v for r, v in held.items()
                                if r not in CALLER}
                        after = True
                    elif not after:
                        if m == "movl" and reg_of(o[1]) == "rsi" and \
                                o[0].startswith("%"):
                            held[reg_of(o[0])] = "count"
                    elif m in ("xorl", "xorq") and o[0] == o[1]:
                        held[reg_of(o[1])] = 0
                    elif m in ("movl", "movq") and o[0].startswith("%") \
                            and o[1].startswith("%"):
                        held[reg_of(o[1])] = held.get(reg_of(o[0]))
                    elif len(o) == 2 and o[-1].startswith("%"):
                        held.pop(reg_of(o[-1]), None)
                return held
            fast_held = run(starts[fast], False)
            slow_held = run(starts[slow], True)
            both = [r for r, v in fast_held.items()
                    if v == "count" and slow_held.get(r) == 0
                    and r != self.tag_reg]
            if len(both) == 1:
                self.depth_reg = both[0]
        self.slow_block = slow
        # Every number the decoder can tell apart: past the end of its table
        # or past the largest number it compares against, it gives up.
        # Only a small constant can be a number a plant left: anything the
        # rule compares against that is larger is some other value, and
        # trying every number up to it would never finish. The most any of
        # IBM's rules plants is fifty-three.
        top = 0
        for t in self.tables.values():
            top = max(top, len(t[1]) + 1)
        for blk, idx in self.blocks:
            for i in idx:
                _a, m, o, _n = self.ins[i]
                if m == "cmpl" and o[0].startswith("$"):
                    k = int(o[0].lstrip("$"), 0)
                    if 0 <= k < 512:
                        top = max(top, k + 2)
        self.tags = {}
        self.exit_facts = {}
        seen = set()
        for k in range(1, max(top, 2) + 1):
            try:
                t, visited, facts, every = self.decode(decoder, k)
            except Hole:
                continue
            seen |= visited
            if t in self.exits:
                self.exit_tags.setdefault(t, []).append(k)
                self.exit_facts[(t, k)] = every
                continue
            if self.gives_up(t):
                # A block that only moves registers into the way out answers
                # whatever it leaves there, which is not always giving up:
                # the way out says which once the moves are made.
                out, every = self.through_moves(t, every)
                self.exit_tags.setdefault(out, []).append(k)
                self.exit_facts[(out, k)] = every
                continue
            if t in self.exit_tail:
                continue
            old = self.tag_facts.setdefault(t, facts)
            if old != facts:
                # Numbers that reach one place having set a register two
                # ways leave it unknown there, so a place that read it would
                # stop the lift rather than read either.
                for r in set(old) | set(facts):
                    if old.get(r) != facts.get(r):
                        old[r] = ("X",)
            self.tags.setdefault(t, []).append(k)
        if not self.tags and not self.exit_tags:
            raise Hole("a dispatch that goes nowhere")
        self.dispatch_rest |= seen

    def through_moves(self, t, facts):
        """What the registers hold once a block that only moves them has
        run, from what they held on the way in, and the way out it falls
        into."""
        f = dict(facts)
        pushes = []
        idx = dict(self.blocks)[t]
        for i in idx:
            _a, m, o, _n = self.ins[i]
            if m in ("movl", "movq"):
                src = reg_of(o[0])
                f[reg_of(o[1])] = f.get(src, ("reg", src))
            elif m == "xorl":
                f[reg_of(o[1])] = 0
            elif m == "pushq":
                pushes.append(int(o[0].lstrip("$"), 0) if o[0].startswith("$")
                              else f.get(reg_of(o[0]), ("reg", reg_of(o[0]))))
            elif m == "popq":
                f[reg_of(o[0])] = pushes.pop() if pushes else ("X",)
        return self.moves_into(t), f

    def gives_up(self, t):
        """A block that only moves registers on its way into the way out,
        by falling into it or by a jump to it."""
        return self.moves_into(t) in self.exits

    def moves_into(self, t):
        """Where a block that only moves registers goes: the way out it falls
        or jumps into, or None for any other block."""
        idx = dict(self.blocks).get(t)
        if idx is None:
            return None
        for i in idx:
            _a, m, o, _n = self.ins[i]
            if m in ("movl", "movq") and o[0].startswith("%") and o[1].startswith("%"):
                continue
            if m in ("pushq", "popq") or (m == "xorl" and o[0] == o[1]):
                continue
            if m == "jmp" and i == idx[-1] and o and o[0].startswith("0x"):
                return int(o[0].split()[0], 16)
            return None
        return self.ins[idx[-1] + 1][0] if idx[-1] + 1 < len(self.ins) else None

    def frame_store(self, i):
        """A register stored into the rule's own frame: (offset, width)."""
        _a, m, o, _n = self.ins[i]
        if m in ("movw", "movl", "movq", "movb") and len(o) == 2 and \
                o[0].startswith("%") and not o[0].startswith("%xmm"):
            mm = MEM.match(o[1])
            if mm and mm.group(2) == "%rbp" and not mm.group(3):
                return (int(mm.group(1), 0) if mm.group(1) else 0,
                        WIDTH_OF[m])
        return None

    def frame_load(self, i):
        """A load out of the rule's own frame into a register: the decoder
        may hoist one that every place it leads to shares."""
        _a, m, o, _n = self.ins[i]
        if m in ("movq", "movl") and len(o) == 2 and o[1].startswith("%"):
            mm = MEM.match(o[0])
            if mm and mm.group(2) == "%rbp" and not mm.group(3):
                return int(mm.group(1), 0) if mm.group(1) else 0
        return None

    def through_memory(self, i):
        """Whether an instruction moves a value through memory, which is the
        rule's own work and never the decoder's. convert_eng_phone_number's
        two numbers that match both reach a block that stores the end of the
        phone number into its argument; read as part of the decoder, that
        block was never written out, so one number lost the store and the
        other had nowhere to go."""
        _a, m, o, _n = self.ins[i]
        if self.frame_load(i) is not None:
            return False
        return m in ("movb", "movw", "movl", "movq") and any("(" in x for x in o)

    def decode(self, start, k):
        """Run the decoder with k in eax and say where it ends up. It ends at
        whatever a table jump lands on, and at any block the rest of the rule
        also reaches: that is a place in the rule, not part of the decoder."""
        regs = {self.tag_reg: k & 0xffffffff}
        flags = None
        a = start
        visited = set()
        preds = {}
        for blk, outs in self.succ.items():
            for t in outs:
                preds.setdefault(t, set()).add(blk)
        region = set(self.dispatch_head) | self.dispatch_rest
        tabled = False
        # A register the decoder sets to a constant on the way, which the
        # code at the place reads: clang hoists a value every place a table
        # leads to shares into the decoder itself, so that it is only true
        # of the places reached through it. A copy of a register the decoder
        # never wrote is that register's value as the dispatch began, and
        # anything else it writes is unknown at the place.
        facts = {}
        stores = []
        pushed = []
        def put(r, v, fact):
            regs[r] = v
            facts[r] = fact
        def done(a):
            # The number's own register is the number until the decoder
            # writes something else into it, as it may with what is left of
            # the count once the table entry is read.
            kept = {r: v for r, v in facts.items()
                    if not (r == self.tag_reg and v == ("X",)) and
                    not (r in CALLER and v == ("X",))}
            if stores:
                kept["stores"] = tuple(stores)
            return a, visited, kept, dict(facts)
        for _step in range(200):
            if a is not None and a in self.leaders and a != start:
                if tabled or (preds.get(a, set()) - region - visited):
                    return done(a)
            if a is None or a not in self.by_addr:
                raise Hole("a dispatch that runs off the end")
            if a in self.exits or a in self.exit_tail:
                return done(a)
            i = self.by_addr[a]
            _a, m, o, _n = self.ins[i]
            if m not in self.DECODER:
                return done(a)
            if a in self.leaders:
                blk = a
                ok = all(self.ins[j][0] == self.depth_store or
                         self.frame_store(j) is not None or
                         (self.ins[j][1] in self.DECODER
                          and not self.through_memory(j)
                          and not (self.ins[j][1] == "pushq" and
                                   not self.ins[j][2][0].startswith("$")))
                         for j in dict(self.blocks)[blk])
                if not ok:
                    return done(a)
                visited.add(blk)
            nxt = self.ins[i + 1][0] if i + 1 < len(self.ins) else None
            if a == self.depth_store:
                a = nxt
                continue
            def val(t):
                if t.startswith("$"):
                    return int(t.lstrip("$"), 0) & 0xffffffff
                return regs.get(reg_of(t))
            if self.frame_store(i) is not None:
                # A register written into the frame, which every place the
                # decoder leads to would have done first: said at each.
                off, w = self.frame_store(i)
                src = reg_of(o[0])
                stores.append((off, w, facts.get(src, ("reg", src))
                               if src not in regs else regs[src]))
                a = nxt
                continue
            if self.frame_load(i) is not None:
                w = 8 if m == "movq" else 4
                put(reg_of(o[1]), None,
                    ("mem", ("local", self.frame_load(i)), w, False, None))
                a = nxt
                continue
            if m in ("movl", "movq", "movapd", "movaps") and \
                    o[0].startswith("%") and o[1].startswith("%"):
                src = reg_of(o[0])
                v = val(o[0])
                if src not in regs:
                    fact = ("reg", src)
                elif isinstance(v, int):
                    fact = v
                else:
                    fact = facts.get(src, ("X",))
                put(reg_of(o[1]), v, fact)
            elif m in ("movb", "movw", "movl", "movq") and \
                    o[0].startswith("$") and o[1].startswith("%"):
                # A byte is taken for the whole register: every rule seen
                # doing this had cleared it or held a small count in it.
                put(reg_of(o[1]), val(o[0]), val(o[0]))
            elif m == "pushq" and o[0].startswith("$"):
                pushed.append(val(o[0]))
            elif m == "popq" and pushed:
                v = pushed.pop()
                put(reg_of(o[0]), v, v)
            elif m in ("pushq", "popq"):
                return done(a)
            elif m == "movb" or self.through_memory(i):
                return done(a)
            elif m in ("addl", "subl", "andl"):
                x, y = val(o[0]), val(o[1])
                if x is None or y is None:
                    return done(a)
                put(reg_of(o[1]), {"addl": y + x, "subl": y - x,
                                   "andl": y & x}[m] & 0xffffffff, ("X",))
            elif m in ("decl", "incl"):
                y = val(o[0])
                if y is None:
                    return done(a)
                put(reg_of(o[0]), (y + (1 if m == "incl" else -1)) & 0xffffffff,
                    ("X",))
            elif m == "xorl" and o[0] == o[1]:
                put(reg_of(o[1]), 0, 0)
            elif m == "cmpl":
                x, y = val(o[0]), val(o[1])
                if x is None or y is None:
                    return done(a)
                flags = (y, x)
            elif m == "testl":
                x, y = val(o[0]), val(o[1])
                if x is None or y is None:
                    return done(a)
                flags = (x & y, 0)
            elif m in CONDS:
                y, x = flags
                sy = y - (1 << 32) if y & 0x80000000 else y
                sx = x - (1 << 32) if x & 0x80000000 else x
                cc = CONDS[m]
                go = {"e": y == x, "ne": y != x, "b": y < x, "ae": y >= x,
                      "a": y > x, "be": y <= x, "l": sy < sx, "ge": sy >= sx,
                      "g": sy > sx, "le": sy <= sx}[cc]
                if go:
                    nxt = self.target(o)
            elif m == "jmp":
                nxt = self.target(o)
            elif m == "movsd" and o[0].endswith("(%rip)") and \
                    o[1].startswith("%xmm") and _n:
                # A double the places it leads to share, hoisted into it.
                facts[reg_of(o[1])] = as_fixed(
                    self.m.bytes[_n[0]:_n[0] + 8])
            elif m in ("xorps", "xorpd") and o[0] == o[1]:
                facts[reg_of(o[1])] = 0
            elif m in ("movsd", "xorps", "xorpd", "movapd", "movaps"):
                return done(a)
            elif m == "leaq":
                # An address the decoder points a register at for the place
                # it leads to: a local, a variable or a constant, as any
                # other lea would name it.
                mm = MEM.match(o[0])
                fact = ("X",)
                if mm and not mm.group(3):
                    off = int(mm.group(1), 0) if mm.group(1) else 0
                    if mm.group(2) == "%rbp":
                        fact = ("laddr", off)
                    elif mm.group(2) == "%rbx":
                        fact = ("gaddr", off)
                    elif mm.group(2) == "%rip" and _n and \
                            not self.lo <= _n[0] < self.hi:
                        # A jump table's own base is the decoder's business
                        # and nothing a place reads.
                        fact = ("sym", _n[0])
                facts[reg_of(o[1])] = fact
                regs.pop(reg_of(o[1]), None)
            elif m == "movslq":
                mm = re.match(r"^\((%[a-z0-9]+),(%[a-z0-9]+),4\)$", o[0])
                if not mm:
                    return done(a)
                put(reg_of(o[1]), ("index", regs.get(reg_of(mm.group(2)))),
                    ("X",))
            elif m == "addq":
                facts[reg_of(o[1])] = ("X",)
            elif m == "jmpq":
                tab = self.tables.get(a)
                idx = regs.get(reg_of(o[0].lstrip("*")))
                if tab is None or not isinstance(idx, tuple):
                    raise Hole("a dispatch table that cannot be read")
                n = idx[1]
                if n is None or n >= len(tab[1]):
                    raise Hole("a number past the end of its table")
                nxt = tab[1][n]
                tabled = True
            a = nxt
        raise Hole("a dispatch that does not end")

    def exit_answer(self, idx):
        """What the way out answers: read from vretproc to the return,
        through the stack check, which splits it into two blocks."""
        i = idx[0]
        while self.ins[i][1] != "callq" or \
                not self.ins[i][2][0].endswith("<_vretproc>"):
            i += 1
        i += 1
        while i < len(self.ins):
            _a, m, o, _n = self.ins[i]
            if m == "retq":
                break
            if m == "movl" and reg_of(o[1]) == "rax" and o[0].startswith("%"):
                return ("reg", reg_of(o[0]))
            if m == "xorl" and reg_of(o[0]) == "rax" and reg_of(o[1]) == "rax":
                return ("imm", 0)
            if m == "pushq" and o[0].startswith("$") and \
                    self.ins[i + 1][1] == "popq" and \
                    reg_of(self.ins[i + 1][2][0]) == "rax":
                return ("imm", int(o[0].lstrip("$"), 0))
            i += 1
        raise Hole("a way out answering nothing that can be read")

    def frame_accesses(self):
        """Every place in the frame the rule reads or writes, as its offset,
        how wide, and whether it is only having its address taken: through
        rbp, and through a register a lea pointed into the frame, which is
        how a rule reaches a record it keeps -- and with them every constant
        kind and field written into the frame, through either."""
        out = []
        kinds = []
        held = {}
        # What a register holds is only carried down a straight line: where
        # control can arrive from anywhere but the instruction before, the
        # walk in address order has not seen the way in, and a register the
        # code reloaded on that way would be taken to hold whatever it held
        # above -- which is how a store through the caller's pointer was read
        # as one into the frame.
        blocks = dict(self.blocks)
        joined = set()
        for blk, outs in self.succ.items():
            last = blocks[blk][-1] if blocks.get(blk) else None
            for t in outs:
                if t in self.by_addr and (last is None or
                                          self.by_addr[t] != last + 1):
                    joined.add(t)
        for a in self.reach:
            if a in joined:
                held = {}
            _a, m, o, _n = self.ins[self.by_addr[a]]
            width = {"movups": 16, "movaps": 16, "movupd": 16, "movapd": 16,
                     "movsd": 8}.get(m) or WIDTH_OF.get(m) or \
                (8 if m.endswith("q") else 4 if m.endswith("l") else
                 2 if m.endswith("w") else 1)
            for k, t in enumerate(o):
                mm = MEM.match(t)
                if not mm or mm.group(3):
                    continue
                d = int(mm.group(1), 0) if mm.group(1) else 0
                if mm.group(2) == "%rbp" and d >= 16:
                    continue
                if mm.group(2) == "%rbp":
                    off = d
                elif reg_of(mm.group(2)) in held and \
                        held[reg_of(mm.group(2))][0] == "laddr":
                    off = held[reg_of(mm.group(2))][1] + d
                else:
                    continue
                out.append((off, width, m == "leaq"))
                if k == 1 and m == "movl" and len(o) == 2:
                    v = int(o[0][1:], 16) if o[0].startswith("$") else \
                        held.get(reg_of(o[0]), (None, None))[1] \
                        if o[0].startswith("%") and \
                        held.get(reg_of(o[0]), ("",))[0] == "imm" else None
                    if v is not None:
                        kinds.append((off, v))
            if m == "callq":
                for r in CALLER:
                    held.pop(r, None)
                continue
            if len(o) == 2 and o[1].startswith("%"):
                r = reg_of(o[1])
                mm = MEM.match(o[0])
                if m == "leaq" and mm and mm.group(2) == "%rbp" and \
                        not mm.group(3):
                    held[r] = ("laddr", int(mm.group(1), 0)
                               if mm.group(1) else 0)
                elif m in ("movl", "movq") and o[0].startswith("$"):
                    held[r] = ("imm", int(o[0][1:], 0) & 0xffffffff)
                elif m in ("movl", "movq") and o[0].startswith("%") and \
                        reg_of(o[0]) in held:
                    held[r] = held[reg_of(o[0])]
                else:
                    held.pop(r, None)
            elif m in ("popq", "incl", "decl", "incq", "decq", "negl") and o:
                held.pop(reg_of(o[0]), None)
        return out, kinds

    def locals_used(self):
        """Which places in the frame the rule uses, and as what. A place
        whose address goes anywhere is a cell, which is sixteen bytes here
        and eight in our machine: the kind at nought, the field at two and
        the value eight bytes in, where ours keeps it four in."""
        accesses, kinds = self.frame_accesses()
        self.cells = {off for off, _w, lea in accesses if lea}
        self.scalars = {}
        # A location naming a statement type and no field holds a whole
        # record of that type, and 6.1 keeps the record sixteen bytes into
        # it where ours keeps it four in. Which type it is, is the kind the
        # rule writes into it with the field of minus one.
        self.records = {}
        lengths = self.m.record_lengths()
        for at, v in kinds:
            if v >> 16 == 0xffff and (v & 0xffff) in lengths:
                self.records[at] = lengths[v & 0xffff]
                self.cells.add(at)
        # A cell of a short is four bytes, and a long's value is eight bytes
        # into it: a place read anywhere past a cell but its field and that
        # value is not in it, and says the cell is a short one, as another
        # cell starting inside its sixteen does.
        self.short_cells = {c for c in self.cells
                            if c not in self.records and
                            any(c < d < c + 16 for d in self.cells)}
        for off, _w, lea in accesses:
            if lea:
                continue
            below = [c for c in self.cells if c <= off < c + 16]
            if below and off - max(below) not in (0, 2, 8) and \
                    max(below) not in self.records:
                self.short_cells.add(max(below))
        for off, w, lea in accesses:
            if lea or self.cell_of(off) is not None:
                continue
            self.scalars[off] = max(self.scalars.get(off, 0), min(w, 8))

    # -- names -----------------------------------------------------------

    def cell_of(self, off):
        """The cell a place in the frame is part of, if any: the nearest
        below it that spans it."""
        below = [c for c in self.cells
                 if c <= off < c + (4 if c in self.short_cells else
                                    0x10 + self.records[c]
                                    if c in self.records else 16)]
        return max(below) if below else None

    def local_name(self, off):
        return "c%x" % -off if off in self.cells else "v%x" % -off

    def local_place(self, off, width):
        """A place in the frame, as the upper form says it."""
        if off in self.frame_locals:
            raise Hole("the rule reads the machine's own frame at %d" % off)
        # The nearest cell at or below, not the first that spans it: a short
        # cell is four bytes, and 6.1 lays two or three of them side by side
        # inside what a location's sixteen would span, so the field of the
        # second read as six bytes into the first.
        c = self.cell_of(off)
        if c is None:
            return [self.local_name(off)]
        inner = off - c
        if c in self.records and inner >= 0x10:
            k = inner - 0x10
            if width not in (1, 2, 4) or k + width > self.records[c]:
                raise Hole("%d bytes at %d of a record of %d"
                           % (width, k, self.records[c]))
            return ["cell", self.local_name(c),
                    {1: "byte", 2: "half", 4: "word"}[width], str(k)]
        if inner == 0 and width == 4:
            return [self.local_name(c)]
        part = {0: "kind", 2: "field", 8: "value"}.get(inner)
        if part is None or (part != "value" and width != 2):
            raise Hole("a cell read at %d wide %d" % (inner, width))
        return ["cell", self.local_name(c), part]

    def glob_words(self, off, width):
        self.globals_used.add((off, width))
        return ["global", {1: "byte", 2: "half"}.get(width, "word"),
                self.gmap(off, width)]

    def sym_words(self, addr):
        name, data = self.m.data_symbol(addr)
        if name is None and self.m.in_strings(addr):
            # A string the compiler pooled with others and named nothing:
            # its bytes are up to its nought.
            end = self.m.bytes.index(b"\0", addr)
            name, data = "cstring_%x" % addr, self.m.bytes[addr:end + 1]
        if name is None:
            raise Hole("an address at 0x%x that is no symbol" % addr)
        self.syms_used.add((addr, name))
        return ["sym", self.smap(addr, name, data)]

    # -- values as the upper form says them -------------------------------

    def words(self, v, st, reg=None):
        """A value as words, or Need when it has to be kept somewhere first."""
        k = v[0]
        if k == "imm":
            n = v[1]
            if n >= 1 << 63:
                n -= 1 << 64
            if n >= 1 << 31 and n < 1 << 32:
                n -= 1 << 32
            return [str(n)]
        if k == "laddr":
            if v[1] in self.frame_locals:
                raise Hole("the machine's own frame handed on")
            if v[1] not in self.cells and v[1] not in self.scalars:
                raise Hole("the address of a local nothing declares")
            return ["addr", self.local_name(v[1])]
        if k == "gaddr":
            return ["addr", self.var_for(v[1])]
        if k == "sym":
            return self.sym_words(v[1])
        if k == "state":
            return ["state"]
        if k == "param":
            return ["arg", str(v[1])]
        if k == "mem":
            place, width, _signed, d = v[1], v[2], v[3], v[4]
            if place[0] == "glob":
                words = self.glob_words(place[1], width)
                if _signed and width < 4:
                    words.insert(1, "signed")
                return words
            if place[0] == "local":
                return self.local_place(place[1], width)
            if place[0] == "through" and place[1][0] in ("param", "home"):
                ptr = ["arg", str(place[1][1])] if place[1][0] == "param" \
                    else ["h_" + place[1][1]]
                return ["through", *ptr,
                        *({1: "byte", 2: "half"}.get(width, "word").split()),
                        str(self.through_offset(place[2], width))]
            raise Hole("a read through a pointer")
        if k == "ans":
            if st["said"] == v[2]:
                return ["answer"]
            raise Need(("save", v[1]))
        if k == "home":
            return ["h_" + v[1]]
        if k == "unwindv":
            return ["unwind"]
        if k in ("snap", "pack"):
            return [v[1]]
        if k == "cc":
            if reg is None:
                raise Hole("a comparison kept as a value nobody names")
            raise Need(("home", reg))
        if k == "saved":
            return ["s%x" % v[1]]
        if k == "stale":
            raise Need(("save", v[1]))
        if k == "X":
            r = v[1] if len(v) > 1 else reg
            if r is None:
                raise Hole("a value nobody can name")
            raise Need(("home", r))
        raise Hole("a value that is %s" % k)

    def through_offset(self, off, width):
        """Where in a cell or a record a caller handed over the bytes 6.1
        reads at off are in ours: the kind and the field where they are,
        the value four in rather than eight, and a record's bytes four in
        rather than sixteen."""
        if off in (0, 2) or (off == 8 and width in (4, 8)):
            return {0: 0, 2: 2, 8: 4}[off]
        if off >= 0x10:
            return 4 + off - 0x10
        raise Hole("%d bytes at %d of something handed over" % (width, off))

    def var_for(self, off):
        """A variable of the language by its address, which the upper form
        can only name once the rule has declared it."""
        name = "g%x" % off
        self.vars_used[name] = off
        return name

    # -- one instruction at a time -----------------------------------------

    def overwritten_before_use(self, a, r):
        """Whether register r is written between the test at a and the
        instruction that reads the flags it set."""
        i = self.by_addr[a] + 1
        while i < len(self.ins):
            _b, m, o, _n = self.ins[i]
            # A set writes its register before the test it carries out is
            # said, so a set into the register tested overwrites it first.
            if m.startswith("set") and m[3:] in WORDS and o and \
                    reg_of(o[0]) == r:
                return True
            if m in CONDS or m.startswith("cmov") or m.startswith("set"):
                return False
            if m in ("callq", "jmp", "jmpq", "retq", "ucomisd") or \
                    m.startswith("cmp") or m.startswith("test"):
                return True
            _r, w = self.regs_read(i)
            if r in w:
                return True
            i += 1
        return True

    def flags_signed(self, a):
        """Whether what reads the flags a compare at a sets reads them as a
        signed comparison: the first jump, set or move on a condition after
        it, before anything sets them again. Equality is read either way and
        is taken as unsigned."""
        i = self.by_addr[a] + 1
        while i < len(self.ins):
            m = self.ins[i][1]
            cc = CONDS.get(m) or \
                (m[3:] if m.startswith("set") and m[3:] in WORDS else None) or \
                (m[4:] if m.startswith("cmov") and m[4:] in WORDS else None) or \
                (m[4:-1] if m.startswith("cmov") and m[4:-1] in WORDS else None)
            if cc is not None:
                return cc in ("l", "le", "g", "ge", "s", "ns")
            if m.startswith(("cmp", "test", "add", "sub", "and", "or", "xor",
                             "inc", "dec", "neg", "shl", "sar", "shr",
                             "imul", "callq", "ucomisd")):
                return False
            i += 1
        return False

    def narrow(self, st, lines, v, w, a, k):
        """An operand of a compare or a test narrower than a word, as the
        flags see it: only its low w bytes, sign-extended when what reads
        the flags compares signed. A half read out of memory is read again at
        that width and signedness; anything else is copied and narrowed."""
        signed = self.flags_signed(a)
        bits = 8 * w
        if v[0] == "imm":
            n = v[1] & ((1 << bits) - 1)
            if signed and n >= 1 << (bits - 1):
                n -= 1 << bits
            return ("imm", n)
        if v[0] == "mem":
            if v[2] >= w:
                return ("mem", v[1], w, signed, v[4])
            if not v[3] or signed:
                return v
            raise Hole("a %d-byte unsigned compare of a signed read" % w)
        inner = v[2] if v[0] == "home" else None
        if inner is not None and inner[0] == "mem" and (
                (inner[2] < w and (not inner[3] or signed)) or
                (inner[2] == w and inner[3] == signed)):
            # A register loaded from memory no wider, and extended the way
            # the comparison reads it, already holds what the flags see.
            return v
        name = "n%x_%d" % (a, k)
        self.snaps.add(name)
        self.say(st, lines, "set", name, "to", *self.words(v, st))
        if signed:
            self.say(st, lines, "shift", "left", str(32 - bits), "into", name)
            self.say(st, lines, "shift", "right", str(32 - bits), "into", name)
        else:
            self.say(st, lines, "and", str((1 << bits) - 1), "into", name)
        return ("snap", name)

    def snap(self, st, lines, v, a, k):
        """What a test reads, as it stands when the test is made. A
        register's own local may be written again before the branch or the
        move that reads the flags, so where it is its value is copied now."""
        if v[0] == "home" and (v[2] is None or v[2][0] not in PURE) and \
                self.overwritten_before_use(a, v[1]):
            name = "k%x_%d" % (a, k)
            self.snaps.add(name)
            self.say(st, lines, "set", name, "to", "h_" + v[1])
            return ("snap", name)
        return v

    def say(self, st, lines, *line):
        lines.append(list(line))
        st["said"] = (st["said"][0], st["said"][1] + 1)

    def put_reg(self, st, lines, r, v):
        """A register is written. One that has to keep its value across a
        branch or a call has a local of its own, and the local is written
        too, whatever the value is, so every path into a join agrees."""
        st["fresh"].add(r)
        if v[0] == "home" and v[1] != r and \
                (v[2] is None or v[2][0] not in PURE):
            # Another register's local, which that register may write again
            # before this one is read: so this one takes a copy now.
            if r not in self.homes:
                raise Need(("home", r))
        framed = v[0] == "laddr" and v[1] in self.frame_locals
        if v[0] == "mem" and v[2] == 8 and (
                (v[1][0] == "local" and self.record_data(v[1][1])) or
                (v[1][0] == "through" and v[1][2] >= 0x10)):
            # Eight bytes of a record on their way into another: nothing
            # here holds eight bytes, so the copy is said where it lands.
            st["regs"][r] = v
            return
        if r in self.homes and v[0] == "cc":
            test = self.cond_words(dict(st, cond=v[1]), v[2])
            self.say(st, lines, "set", "h_" + r, "to", "0")
            self.say(st, lines, "if", *test)
            self.say(st, lines, "set", "h_" + r, "to", "1")
            self.say(st, lines, "end")
            st["regs"][r] = ("home", r, None)
            return
        # The state is never kept: rbx holds it throughout, and a register
        # the compiler also uses for something else is only handed it for a
        # call.
        if r in self.homes and not framed and \
                v[0] not in ("got", "canary", "frame", "code", "sp", "state"):
            if v[0] == "imm" and r not in XMM and self.is_double(v[1]):
                # A double's bits in an ordinary register, which a store of
                # eight bytes takes for the double it is; the register's
                # local holds four, so it is given the fixed value. Written
                # as the bits, por_ph_t_dur set a duration of 34 to nought.
                v = ("imm", as_fixed(struct.pack(
                    "<Q", v[1] & 0xffffffffffffffff)))
            self.say(st, lines, "set", "h_" + r, "to", *self.words(v, st, r))
            # What the local now holds, for reading it as an address: the
            # innermost value, since a local that remembered another local
            # that remembered a third would grow every round and never settle.
            while v is not None and v[0] == "home":
                v = v[2]
            st["regs"][r] = ("home", r, v)
        else:
            st["regs"][r] = v

    def forget_memory(self, st, place=None):
        """What was read out of memory is no longer what memory holds."""
        for r, v in list(st["regs"].items()):
            if v[0] != "mem":
                continue
            if place is None or v[1] == place or place[0] == "through" \
                    or v[1][0] == "through":
                st["regs"][r] = ("stale", v[4])

    def step(self, b, st, lines, i):
        a, m, o, note = self.ins[i]
        regs = st["regs"]

        if m in ("nop", "nopl", "nopw"):
            return
        if a in self.pop_skip or a in self.table_steps or \
                (m == "jmpq" and a in self.tables):
            return
        if a in self.push_to:
            self.put_reg(st, lines, self.push_to[a], self.value_of(st, o[0]))
            return
        if m == "pushq":
            if reg_of(o[0]) in ("rbp", "rbx", "r12", "r13", "r14", "r15") \
                    and b == self.lo and not st["begun"]:
                return
            st["pushes"].append(self.value_of(st, o[0], 8))
            return
        if m == "popq":
            if not st["pushes"] and st.get("cleanup"):
                # Arguments a call took off the stack, popped to clear it.
                st["cleanup"] -= 1
                st["regs"][reg_of(o[0])] = ("X",)
                return
            if not st["pushes"]:
                raise Hole("a pop of nothing")
            self.put_reg(st, lines, reg_of(o[0]), st["pushes"].pop())
            return
        if m == "subq" and o[1] == "%rsp":
            return
        if m == "addq" and o[1] == "%rsp":
            n = int(o[0].lstrip("$"), 0) // 8
            del st["pushes"][len(st["pushes"]) - n:]
            return
        st["begun"] = True

        if m in ("leal", "leaq") and self.lea_is_arithmetic(st, o[0]):
            return self.lea_arith(st, lines, o)
        if m in ("imull", "imulw", "imulq"):
            return self.multiply(st, lines, m, o)
        if m in ("cwtl", "cltq", "cwtd"):
            if m == "cwtd":
                raise Hole("cwtd")
            if "rax" not in self.homes:
                v = st["regs"].get("rax", ("X", "rax"))
                if v[0] == "imm":
                    n = v[1] & (0xffff if m == "cwtl" else 0xffffffff)
                    top = 0x8000 if m == "cwtl" else 0x80000000
                    self.put_reg(st, lines, "rax",
                                 ("imm", n - 2 * top if n & top else n))
                    return
                raise Need(("home", "rax"))
            if m == "cwtl":
                self.say(st, lines, "shift", "left", "16", "into", "h_rax")
                self.say(st, lines, "shift", "right", "16", "into", "h_rax")
            st["regs"]["rax"] = ("home", "rax", None)
            st["fresh"].add("rax")
            return
        if m in ("negl", "negw", "negq") and o[0].startswith("%"):
            r = reg_of(o[0])
            v = st["regs"].get(r, ("X", r))
            if v[0] == "imm":
                self.put_reg(st, lines, r, ("imm", -v[1]))
                return
            if r not in self.homes or v[0] != "home":
                raise Need(("home", r))
            self.say(st, lines, "negate", "h_" + r)
            st["regs"][r] = ("home", r, None)
            st["fresh"].add(r)
            # The flags a negation leaves are those of what it left.
            st["cond"] = ("test", self.snap(st, lines, st["regs"][r], a, 0), r)
            return

        if m == "leaq":
            src, dst = o
            r = reg_of(dst)
            if src.endswith("(%rip)"):
                if note and self.lo <= note[0] < self.hi:
                    self.put_reg(st, lines, r, ("code", note[0]))
                else:
                    self.put_reg(st, lines, r, ("sym", note[0]))
                return
            p = self.addr_of(st, src)
            if p[0] == "local":
                self.put_reg(st, lines, r, ("laddr", p[1]))
            elif p[0] == "glob":
                self.put_reg(st, lines, r, ("gaddr", p[1]))
            else:
                raise Hole("leaq %s" % src)
            return

        if m in ("xorl", "xorq") and o[0] == o[1]:
            self.put_reg(st, lines, reg_of(o[1]), ("imm", 0))
            return

        # The disassembler prints a negative double's bits as a negative
        # number, so whoever reads them as a double masks them first.
        if m == "movabsq" and o[0].startswith("$") and o[1].startswith("%"):
            self.put_reg(st, lines, reg_of(o[1]), ("imm", int(o[0][1:], 0)))
            return
        if m == "movq" and len(o) == 2 and o[1].startswith("%xmm"):
            # A double's bits handed from an ordinary register.
            v = self.value_of(st, o[0])
            if v[0] != "imm":
                raise Hole("a double's bits from %s" % (v[0],))
            self.put_reg(st, lines, reg_of(o[1]),
                         ("imm", as_fixed(struct.pack(
                             "<Q", v[1] & 0xffffffffffffffff))))
            return
        if m in SSE:
            return self.sse(b, st, lines, a, m, o, note)

        if m in MOVES:
            return self.move(b, st, lines, a, m, o, note)

        if m == "andq" and o[0] == "$0x0" and not o[1].startswith("%"):
            return self.store(b, st, lines, a, self.addr_of(st, o[1]), 8,
                              ("imm", 0))

        if m in ("testl", "testq", "testw", "testb") and o[0] == o[1]:
            v = self.value_of(st, o[0])
            if v[0] == "frame":
                st["cond"] = ("frame",)
                return
            w = {"l": 4, "q": 8, "w": 2, "b": 1}[m[-1]]
            if w < 4:
                v = self.narrow(st, lines, v, w, a, 0)
            st["cond"] = ("test", self.snap(st, lines, v, a, 0), reg_of(o[0]))
            return
        if m in ("cmpl", "cmpq", "cmpw", "cmpb"):
            w = {"l": 4, "q": 8, "w": 2, "b": 1}[m[-1]]
            src = self.value_of(st, o[0], w)
            dst = self.value_of(st, o[1], w)
            if dst[0] == "canary" or src[0] == "canary":
                st["cond"] = ("frame",)
                return
            if w < 4:
                dst = self.narrow(st, lines, dst, w, a, 0)
                src = self.narrow(st, lines, src, w, a, 1)
            st["cond"] = ("cmp", self.snap(st, lines, dst, a, 0),
                          self.snap(st, lines, src, a, 1),
                          reg_of(o[1]) if o[1].startswith("%") else None,
                          reg_of(o[0]) if o[0].startswith("%") else None)
            return

        if m in CONDS:
            return
        if m == "jmp":
            return

        if m.startswith("set") and m[3:] in WORDS and len(o) == 1:
            if st["cond"] is None:
                raise Hole("a %s of nothing tested" % m)
            self.put_reg(st, lines, reg_of(o[0]), ("cc", st["cond"], m[3:]))
            return

        if m.startswith("cmov") and m[4:] in CONDS.values() or \
                (m.startswith("cmov") and m[4:-1] in CONDS.values()):
            cc = m[4:] if m[4:] in CONDS.values() else m[4:-1]
            r = reg_of(o[1])
            if r not in self.homes:
                raise Need(("home", r))
            src = self.value_of(st, o[0])
            self.say(st, lines, "if", *self.cond_words(st, cc))
            self.say(st, lines, "set", "h_" + r, "to", *self.words(src, st, reg_of(o[0])))
            self.say(st, lines, "end")
            regs[r] = ("home", r, None)
            return

        if m in ("incl", "decl", "incw", "decw", "incq", "decq") or \
                m[:-1] in ("add", "sub", "and", "or", "shl", "sar", "shr"):
            return self.arith(b, st, lines, a, m, o)

        if m == "callq":
            return self.call(b, st, lines, a, o)

        raise Hole("%s %s" % (m, ",".join(o)))

    def fixed_value(self, st, text, note, a):
        """A double operand as a fixed value: a register, a constant in the
        module's data, or a place in memory, which is the same place a word
        is read from here."""
        if text.startswith("%"):
            r = reg_of(text)
            v = st["regs"].get(r, ("X", r))
            # A scalar operation on a register holding two reads the lower.
            return v[1] if v[0] == "pair" else v
        if text.endswith("(%rip)"):
            return ("imm", as_fixed(self.m.bytes[note[0]:note[0] + 8]))
        p = self.addr_of(st, text)
        if p[0] == "local" and p[1] in st["slots"]:
            return st["slots"][p[1]]
        return ("mem", p, 8, True, a)

    @staticmethod
    def is_double(n):
        """Whether sixty-four bits a rule stores are a double rather than a
        whole number: they do not fit in thirty-two signed, and read as a
        double they are a number of the size a rule deals in."""
        n &= 0xffffffffffffffff
        if n < 0x80000000 or n >= 0xffffffff80000000:
            return False
        v = struct.unpack("<d", struct.pack("<Q", n))[0]
        return v == v and 1e-6 <= abs(v) <= 1e9

    def record_data(self, off):
        """Whether a place in the frame is in the bytes of a record."""
        c = self.cell_of(off)
        return c is not None and c in self.records and off - c >= 0x10

    def store_eight(self, st, lines, p, v, src):
        """Eight bytes into a record, which the machine here writes as two
        words: a constant split in two, or eight bytes read from another
        record or from what a caller handed over, copied a word at a time."""
        halves = []
        if src and src.startswith("%xmm") and v[0] in ("home", "imm", "mem",
                                                        "saved"):
            # A double into a record: the field is a fixed value here, in the
            # first four of the eight bytes 6.1 gave it.
            self.say(st, lines, "set", *self.local_place(p[1], 4), "to",
                     *self.words(v, st, reg_of(src) if src else None))
            st["slots"].pop(p[1], None)
            self.forget_memory(st, p)
            return
        if v[0] == "imm":
            n = v[1] & 0xffffffffffffffff
            lo, hi = n & 0xffffffff, n >> 32
            halves = [[str(lo - ((lo & 0x80000000) << 1))],
                      [str(hi - ((hi & 0x80000000) << 1))]]
        elif v[0] == "mem" and v[2] == 8:
            q = v[1]
            if q[0] == "local" and self.record_data(q[1]):
                halves = [self.local_place(q[1], 4),
                          self.local_place(q[1] + 4, 4)]
            elif q[0] == "through" and q[1][0] == "param" and q[2] >= 0x10:
                halves = [["through", "arg", str(q[1][1]), "word",
                           str(self.through_offset(q[2], 4))],
                          ["through", "arg", str(q[1][1]), "word",
                           str(self.through_offset(q[2] + 4, 4))]]
        if not halves:
            raise Hole("eight bytes of %s into a record" % (v[0],))
        c = self.cell_of(p[1])
        for k, words in enumerate(halves):
            # Only as much as the record has: 6.1 copies eight bytes of a
            # five-byte record, and the three after it are nothing.
            left = c + 0x10 + self.records[c] - (p[1] + 4 * k)
            if left <= 0:
                continue
            w = 4 if left >= 4 else 2 if left >= 2 else 1
            if w < 4 and words[0] in ("cell", "through"):
                # The same place read narrower: its width is the word
                # before its offset.
                words = words[:-2] + [{2: "half", 1: "byte"}[w], words[-1]]
            self.say(st, lines, "set", *self.local_place(p[1] + 4 * k, w),
                     "to", *words)
        st["slots"].pop(p[1], None)
        self.forget_memory(st, p)

    def part(self, p, k):
        """The four bytes k into a place, as ours lays them out: words to
        read them with and to write them with, or None where 6.1 has bytes
        ours has not -- the padding of a cell's header and the top of its
        eight-byte value."""
        if p[0] == "local":
            off = p[1] + k
            c = self.cell_of(off)
            if c is None:
                if off in self.scalars:
                    return [self.local_name(off)], [self.local_name(off)]
                # Past the end of a record a copy of sixteen at a time
                # runs on into bytes nothing reads.
                below = [x for x in self.records if x < off]
                if below and off < max(below) + 0x10 + \
                        ((self.records[max(below)] + 15) & ~15):
                    return None
                raise Hole("sixteen bytes of the frame that are no local")
            inner = off - c
            if c in self.records and inner >= 0x10:
                left = c + 0x10 + self.records[c] - off
                if left <= 0:
                    return None
                w = self.local_place(off, 4 if left >= 4 else
                                     2 if left >= 2 else 1)
                return w, w
            if c in self.records and inner in (4, 8, 12):
                # A record's header in 6.1 has a pointer where ours starts
                # the record itself.
                return None
            if inner == 0:
                return [self.local_name(c)], [self.local_name(c)]
            if inner == 8:
                w = ["cell", self.local_name(c), "value"]
                return w, w
            return None
        if p[0] == "through" and p[1][0] in ("param", "home"):
            off = p[2] + k
            if off % 16 in (4, 12) and off < 16:
                return None
            ptr = ["arg", str(p[1][1])] if p[1][0] == "param" \
                else ["h_" + p[1][1]]
            at = self.through_offset(off, 4)
            return ["through", *ptr, "word", str(at)], \
                ["put", ptr, at]
        raise Hole("sixteen bytes of %s" % (p[0],))

    def copy_sixteen(self, st, lines, src, dst):
        """A cell or a record copied sixteen bytes at a time, written as the
        parts of it the machine here has."""
        for k in (0, 4, 8, 12):
            a, b = self.part(src, k), self.part(dst, k)
            if a is None or b is None:
                continue
            if b[1][0] == "put":
                self.say(st, lines, "put", *a[0], "into", *b[1][1], "at",
                         str(b[1][2]))
            else:
                self.say(st, lines, "set", *b[1], "to", *a[0])
        self.forget_memory(st)

    def clear_local(self, st, lines, off, n):
        """Nought into n bytes of the frame as 6.1 lays them out, which is
        each part of each cell or record they cover as ours lays it out:
        the eight bytes 6.1 pads a cell's header to have nothing here."""
        k = off
        while k < off + n:
            c = self.cell_of(k)
            if c is None:
                if k in self.scalars:
                    self.say(st, lines, "set", self.local_name(k), "to", "0")
                    st["slots"].pop(k, None)
                    k += self.scalars[k]
                else:
                    k += 1
                continue
            inner = k - c
            if c in self.records and inner >= 0x10:
                left = min(off + n, c + 0x10 + self.records[c]) - k
                w = 4 if left >= 4 else 2 if left >= 2 else 1
                self.say(st, lines, "set", *self.local_place(k, w), "to", "0")
                k += w
            elif inner == 0:
                self.say(st, lines, "set", self.local_name(c), "to", "0")
                k += 4
            elif inner == 8 and c not in self.short_cells:
                self.say(st, lines, "set", "cell", self.local_name(c),
                         "value", "to", "0")
                k += 8
            else:
                k += 4
        self.forget_memory(st)

    def pair_next(self, a):
        """Whether a sixteen-byte load is the second of two packed doubles,
        which the next few instructions say by working on both halves."""
        i = self.by_addr[a]
        return any(m in ("shufpd", "divpd", "haddpd")
                   for _a, m, _o, _n in self.ins[i + 1:i + 4])

    def pair_half(self, st, lines, r, which):
        """One of the two doubles a register holds once the compiler has
        packed two into it."""
        v = st["regs"].get(r, ("X", r))
        if v[0] == "pair":
            return v[1 + which]
        if which == 0:
            return v
        # A pair lost where it met something else on the way here: every
        # way into that place writes both halves where the pair is kept, the
        # next time round.
        want = tuple(sorted(x for x in self.pair_meets if x not in self.pairs))
        if want:
            raise Need(("pairs", want))
        raise Hole("the top half of %s, which holds one double" % r)

    def pair_of(self, st, lines, r, lo, hi):
        """A pair written into the two locals a packed product or quotient
        leaves its halves in, so that a pair made one way and a pair made
        another are the same pair where the two ways meet."""
        out = []
        for k, v in enumerate((lo, hi)):
            tmp = "v%s_%d" % (r, k)
            self.packs.add(tmp)
            if v != ("pack", tmp):
                self.say(st, lines, "set", tmp, "to", *self.words(v, st))
            out.append(("pack", tmp))
        return ("pair", out[0], out[1])

    def packed(self, st, lines, a, m, o, note):
        """Two doubles side by side in one register, which 6.1's compiler made
        of a pair of divisions it could do at once: each half is worked as
        the single double it is, the upper one in a local of its own."""
        regs = st["regs"]
        if m == "unpcklpd":
            s, d = reg_of(o[0]), reg_of(o[1])
            regs[d] = ("pair", self.pair_half(st, lines, d, 0),
                       self.pair_half(st, lines, s, 0))
            return
        if m == "unpckhpd":
            s, d = reg_of(o[0]), reg_of(o[1])
            regs[d] = ("pair", self.pair_half(st, lines, d, 1),
                       self.pair_half(st, lines, s, 1))
            return
        if m == "movddup":
            s, d = reg_of(o[0]), reg_of(o[1])
            v = self.pair_half(st, lines, s, 0)
            regs[d] = self.pair_of(st, lines, d, v, v)
            return
        if m == "movapd" and not o[0].startswith("%"):
            p = self.addr_of(st, o[0])
            if p[0] != "local":
                raise Hole("two doubles out of %s" % (p[0],))
            regs[reg_of(o[1])] = ("pair", self.fixed_value(st, o[0], note, a),
                                  ("mem", ("local", p[1] + 8), 8, True, a))
            return
        if m == "movapd":
            regs[reg_of(o[1])] = regs.get(reg_of(o[0]), ("X",))
            return
        if m == "shufpd" and o[0] == "$0x2" and o[1].endswith("(%rip)"):
            d = reg_of(o[2])
            hi = ("imm", as_fixed(self.m.bytes[note[0] + 8:note[0] + 16]))
            regs[d] = ("pair", self.pair_half(st, lines, d, 0), hi)
            return
        if m in ("divpd", "mulpd", "haddpd"):
            d = reg_of(o[1])
            out = []
            if o[0].endswith("(%rip)"):
                def half(k):
                    at = note[0] + 8 * k
                    return ("imm", as_fixed(self.m.bytes[at:at + 8]))
            else:
                def half(k, s=reg_of(o[0])):
                    return self.pair_half(st, lines, s, k)
            if m in ("divpd", "mulpd"):
                halves = [(self.pair_half(st, lines, d, k), half(k))
                          for k in (0, 1)]
            else:
                halves = [(self.pair_half(st, lines, d, 0),
                           self.pair_half(st, lines, d, 1)),
                          (half(0), half(1))]
            for k, (x, y) in enumerate(halves):
                tmp = "v%s_%d" % (d, k)
                self.packs.add(tmp)
                if x != ("pack", tmp):
                    self.say(st, lines, "set", tmp, "to", *self.words(x, st))
                if m == "divpd":
                    self.say(st, lines, "scale", tmp, "by", str(ONE), "over",
                             *self.words(y, st))
                elif m == "mulpd":
                    self.say(st, lines, "scale", tmp, "by",
                             *self.words(y, st), "over", str(ONE))
                else:
                    self.say(st, lines, "add", *self.words(y, st), "to", tmp)
                out.append(("pack", tmp))
            regs[d] = ("pair", out[0], out[1])
            return
        if m == "movlpd":
            p = self.addr_of(st, o[1])
            return self.store(None, st, lines, a, p, 8,
                              self.pair_half(st, lines, reg_of(o[0]), 0), o[0])
        raise Hole("%s %s" % (m, ",".join(o)))

    def sse(self, b, st, lines, a, m, o, note):
        """6.1's doubles as fixed values. A move is a move; a conversion from
        a whole number multiplies by ONE and one back truncates; a product
        and a quotient are scaled back by ONE in sixty-four bits; and a
        comparison is of whole numbers."""
        regs = st["regs"]
        if m in ("unpcklpd", "unpckhpd", "shufpd", "divpd", "mulpd", "haddpd",
                 "movlpd", "movddup") or \
                (m == "movapd" and any(regs.get(reg_of(x), ("X",))[0] == "pair"
                                       for x in o if x.startswith("%xmm"))) \
                or (m == "movapd" and not o[0].startswith("%") and
                    self.pair_next(a)):
            return self.packed(st, lines, a, m, o, note)
        if m in ("xorps", "xorpd"):
            if o[0] != o[1]:
                raise Hole("%s of two registers" % m)
            r = reg_of(o[1])
            if r in self.pairs:
                # Both halves are nought, and a place that meets a packed
                # pair on another way in reads both. The register's own local
                # is written too, for a place that meets it instead.
                if r in self.homes:
                    self.say(st, lines, "set", "h_" + r, "to", "0")
                regs[r] = self.pair_of(st, lines, r, ("imm", 0), ("imm", 0))
                return
            self.put_reg(st, lines, r, ("imm", 0))
            return
        if m in ("movups", "movupd", "movaps", "movapd") and \
                not o[0].startswith("%") and o[1].startswith("%xmm"):
            # Sixteen bytes read to be written somewhere else whole: a cell
            # or a record copied, which the store below says part by part.
            self.put_reg(st, lines, reg_of(o[1]),
                         ("blob", self.addr_of(st, o[0])))
            return
        if m in ("movups", "movupd", "movaps", "movapd") and \
                o[0].startswith("%xmm") and not o[1].startswith("%"):
            v = regs.get(reg_of(o[0]), ("X",))
            p = self.addr_of(st, o[1])
            if v[0] == "blob":
                return self.copy_sixteen(st, lines, v[1], p)
            if v != ("imm", 0):
                # One double spilled into a slot of sixteen: the half above
                # it is nothing anybody reads.
                return self.store(None, st, lines, None, p, 8, v, o[0])
            if p[0] != "local":
                raise Hole("sixteen bytes cleared through %s" % (p[0],))
            return self.clear_local(st, lines, p[1], 16)
        if m in ("movapd", "movaps"):
            if not (o[0].startswith("%xmm") and o[1].startswith("%xmm")):
                raise Hole("%s %s" % (m, ",".join(o)))
            r = reg_of(o[0])
            self.put_reg(st, lines, reg_of(o[1]), regs.get(r, ("X", r)))
            return
        if m == "movsd":
            src, dst = o
            if dst.startswith("%"):
                self.put_reg(st, lines, reg_of(dst),
                             self.fixed_value(st, src, note, a))
                return
            p = self.addr_of(st, dst)
            r = reg_of(src)
            return self.store(b, st, lines, a, p, 8, regs.get(r, ("X", r)),
                              src)
        if m.startswith("cvtsi2sd"):
            src, dst = o
            x = reg_of(dst)
            v = self.value_of(st, src, 8 if m.endswith("q") else 4, True)
            if v[0] == "imm":
                n = v[1]
                if not m.endswith("q") and n & 0x80000000:
                    n -= 1 << 32
                self.put_reg(st, lines, x, ("imm", (n & 0xffffffff
                                                    if n >= 0 else n) * ONE))
                return
            if x not in self.homes:
                raise Need(("home", x))
            words = self.words(v, st, reg_of(src) if src.startswith("%")
                               else None)
            self.say(st, lines, "set", "h_" + x, "to", *words)
            self.say(st, lines, "multiply", str(ONE), "into", "h_" + x)
            regs[x] = ("home", x, None)
            st["fresh"].add(x)
            return
        if m.startswith("cvttsd2si"):
            # Truncated with one ten-thousandth of slack away from nought.
            # A value that is a whole number in 6.1's doubles to sixteen
            # places can come out one unit under it here, every step having
            # been rounded to ten-thousandths: Portuguese takes a duration
            # over a count as a percentage and multiplies it back, and 144
            # came back 143.9999 and was cut to 143 where 6.1 cut 144.
            src, dst = o
            r = reg_of(dst)
            v = self.fixed_value(st, src, note, a)
            if v[0] == "imm":
                n = (abs(v[1]) + SLACK) // ONE
                self.put_reg(st, lines, r, ("imm", -n if v[1] < 0 else n))
                return
            if r not in self.homes:
                raise Need(("home", r))
            words = self.words(v, st, reg_of(src) if src.startswith("%")
                               else None)
            self.say(st, lines, "set", "h_" + r, "to", *words)
            self.say(st, lines, "add", str(SLACK), "to", "h_" + r)
            self.say(st, lines, "if", "h_" + r, "is", "less", "than",
                     str(SLACK))
            self.say(st, lines, "subtract", str(2 * SLACK), "from", "h_" + r)
            self.say(st, lines, "end")
            self.say(st, lines, "truncate", "h_" + r, "by", "1", "over",
                     str(ONE))
            regs[r] = ("home", r, None)
            st["fresh"].add(r)
            return
        if m in ("mulsd", "divsd", "addsd", "subsd"):
            src, dst = o
            x = reg_of(dst)
            v = self.fixed_value(st, src, note, a)
            cur = regs.get(x, ("X", x))
            if cur[0] == "imm" and v[0] == "imm":
                k, n = cur[1], v[1]
                if m == "mulsd":
                    r = (abs(k * n) + ONE // 2) // ONE
                    r = -r if (k < 0) != (n < 0) else r
                elif m == "divsd":
                    if n == 0:
                        raise Hole("a constant divided by nought")
                    r = (abs(k * ONE) + abs(n) // 2) // abs(n)
                    r = -r if (k < 0) != (n < 0) else r
                else:
                    r = k + n if m == "addsd" else k - n
                self.put_reg(st, lines, x, ("imm", r))
                return
            if cur[0] == "pair" and x in self.homes:
                # A scalar operation on a packed pair works on the lower,
                # and what it leaves is read as one double from then on.
                self.say(st, lines, "set", "h_" + x, "to",
                         *self.words(cur[1], st))
                cur = ("home", x, None)
            if x not in self.homes or cur[0] != "home":
                raise Need(("home", x))
            words = self.words(v, st, reg_of(src) if src.startswith("%")
                               else None)
            if m == "mulsd":
                self.say(st, lines, "scale", "h_" + x, "by", *words,
                         "over", str(ONE))
            elif m == "divsd":
                self.say(st, lines, "scale", "h_" + x, "by", str(ONE),
                         "over", *words)
            elif m == "addsd":
                self.say(st, lines, "add", *words, "to", "h_" + x)
            else:
                self.say(st, lines, "subtract", *words, "from", "h_" + x)
            regs[x] = ("home", x, None)
            st["fresh"].add(x)
            return
        if m == "ucomisd":
            src, dst = o
            sv = self.fixed_value(st, src, note, a)
            dv = self.fixed_value(st, dst, note, a)
            st["cond"] = ("cmp", self.snap(st, lines, dv, a, 0),
                          self.snap(st, lines, sv, a, 1), reg_of(dst),
                          reg_of(src) if src.startswith("%") else None,
                          "float")
            return
        raise Hole("%s %s" % (m, ",".join(o)))

    def lea_is_arithmetic(self, st, text):
        """Whether a lea computes a number rather than an address: an index
        register, or a base that holds a number rather than a place."""
        if re.match(r"^(-?0x[0-9a-f]+|-?\d+)?\(,%[a-z0-9]+,\d\)$", text):
            return True
        mm = MEM.match(text)
        if not mm or mm.group(2) == "%rip":
            return False
        if mm.group(3):
            return True
        base = reg_of(mm.group(2)) if mm.group(2) else None
        if base in (None, "rbx", "rbp", "rsp"):
            return False
        v = st["regs"].get(base, ("X",))
        if v[0] == "home" and v[2] is not None:
            v = v[2]
        return v[0] not in ("state", "laddr", "gaddr", "param", "sp", "got",
                            "code", "sym")

    def lea_arith(self, st, lines, o):
        """base + index * scale + displacement, worked out in a scratch local
        so that a destination that is also one of the two is read before it
        is written."""
        mm = re.match(r"^(-?0x[0-9a-f]+|-?\d+)?\((%[a-z0-9]+)?(?:,(%[a-z0-9]+),"
                      r"(\d))?\)$", o[0])
        if not mm:
            raise Hole("lea of %s" % o[0])
        disp = int(mm.group(1), 0) if mm.group(1) else 0
        base = reg_of(mm.group(2)) if mm.group(2) else None
        index = reg_of(mm.group(3)) if mm.group(3) else None
        scale = int(mm.group(4)) if mm.group(4) else 1
        dst = reg_of(o[1])
        regs = st["regs"]
        bv = regs.get(base, ("X", base)) if base else ("imm", 0)
        iv = regs.get(index, ("X", index)) if index else ("imm", 0)
        if bv[0] == "imm" and iv[0] == "imm":
            self.put_reg(st, lines, dst, ("imm", (bv[1] + iv[1] * scale + disp)
                                          & 0xffffffff))
            return
        if dst not in self.homes:
            raise Need(("home", dst))
        bw = self.words(bv, st, base) if base else None
        iw = self.words(iv, st, index) if index else None
        self.scratch = True
        if index:
            self.say(st, lines, "set", "scratch", "to", *iw)
            if scale != 1:
                self.say(st, lines, "multiply", str(scale), "into", "scratch")
            if base:
                self.say(st, lines, "add", *bw, "to", "scratch")
        else:
            self.say(st, lines, "set", "scratch", "to", *bw)
        if disp:
            self.say(st, lines, "add", str(disp), "to", "scratch")
        self.say(st, lines, "set", "h_" + dst, "to", "scratch")
        regs[dst] = ("home", dst, None)
        st["fresh"].add(dst)

    def multiply(self, st, lines, m, o):
        """A product, of two operands into the second or of a constant and a
        source into a third."""
        regs = st["regs"]
        if len(o) == 3:
            k, src, dst = o
        elif len(o) == 2:
            k, src, dst = None, o[0], o[1]
        else:
            raise Hole("%s with one operand" % m)
        if not dst.startswith("%"):
            raise Hole("%s into memory" % m)
        r = reg_of(dst)
        sv = self.value_of(st, src, {"imulw": 2, "imull": 4}.get(m, 8), True)
        cur = sv if k is not None else regs.get(r, ("X", r))
        by = ("imm", int(k.lstrip("$"), 0)) if k is not None else sv
        if cur[0] == "imm" and by[0] == "imm":
            self.put_reg(st, lines, r, ("imm", (cur[1] * by[1]) & 0xffffffff))
            return
        if r not in self.homes:
            raise Need(("home", r))
        if k is not None:
            self.scratch = True
            self.say(st, lines, "set", "scratch", "to",
                     *self.words(sv, st, reg_of(src) if src.startswith("%")
                                 else None))
            self.say(st, lines, "multiply", str(by[1]), "into", "scratch")
            self.say(st, lines, "set", "h_" + r, "to", "scratch")
        else:
            if cur[0] != "home":
                raise Need(("home", r))
            self.say(st, lines, "multiply",
                     *self.words(by, st, reg_of(src) if src.startswith("%")
                                 else None), "into", "h_" + r)
        regs[r] = ("home", r, None)
        st["fresh"].add(r)

    # The three of the machine's own blocks a fence written out inline
    # reaches: the variables, where it keeps the count, and the arrays of the
    # fenced statement types and of where each is in that list.
    FENCE_BLOCKS = {0xc0: "vars", 0xf8: "chars", 0x108: "index"}

    def inline_fence(self, st, lines, a, m, o):
        """A fence 6.1's compiler wrote out inline: the count stored, the
        index cleared, and a statement type and its place written for each.
        It is said as the call it was once the last type is written."""
        src, dst = o
        if m == "movq" and src in ("0xc0(%rbx)", "0xf8(%rbx)", "0x108(%rbx)") \
                and dst.startswith("%"):
            st["regs"][reg_of(dst)] = ("machine",
                                       self.FENCE_BLOCKS[int(src[:-6], 16)])
            return True
        if m != "movb" or dst.startswith("%"):
            return False
        mm = MEM.match(dst)
        if not mm:
            return False
        held = st["regs"].get(reg_of(mm.group(2)), ("X",))
        if held[0] != "machine":
            return False

        def known(text):
            if text.startswith("$"):
                return int(text[1:], 0)
            v = st["regs"].get(reg_of(text), ("X",))
            if v[0] == "home":
                v = v[2] or ("X",)
            return v[1] if v[0] == "imm" else None
        # Each written as a constant, or out of a register that holds one,
        # with the place in the index chosen by a register the same way.
        n = known(src)
        k = int(mm.group(1), 0) if mm.group(1) else 0
        if mm.group(3):
            step = known(mm.group(3))
            if step is None or mm.group(4) not in (None, "1"):
                return False
            k += step
        if n is None:
            return False
        if held[1] == "vars" and k == 0x1fb9:
            st["fence"] = {"n": n, "chars": {}}
        elif held[1] == "chars" and st.get("fence"):
            st["fence"]["chars"][k] = n
        elif held[1] == "index" and st.get("fence"):
            pass
        else:
            raise Hole("an inline fence of another shape")
        f = st.get("fence")
        if f and len(f["chars"]) == f["n"] and f["n"] and \
                (held[1] == "index" and k == f["chars"][f["n"] - 1]):
            data = bytes(f["chars"][i] for i in range(f["n"])) + b"\x00"
            self.say(st, lines, "call", "fence", str(f["n"]), "sym",
                     self.smap(0, "fence:" + data.hex(), data))
            self.callees["fence"] = 2
            st["fence"] = None
        return True

    def move(self, b, st, lines, a, m, o, note):
        src, dst = o
        if self.inline_fence(st, lines, a, m, o):
            return
        w = WIDTH_OF[m]
        signed = SIGNED_OF.get(m, False)
        if src.endswith("(%rip)"):
            if note and self.m.got_at[0] <= note[0] < self.m.got_at[1]:
                self.put_reg(st, lines, reg_of(dst), ("got", note[0]))
                return
            if note and note[1] and note[1].startswith("string_") and \
                    w == 1 and dst.startswith("%"):
                # A byte of one of the module's strings, which nothing
                # writes: the type an inline fence fences, taken out of the
                # string it was handed rather than written as a constant.
                self.put_reg(st, lines, reg_of(dst),
                             ("imm", self.m.bytes[note[0]]))
                return
            raise Hole("a read of the module's data at 0x%x" % note[0])
        if dst.startswith("%"):
            r = reg_of(dst)
            if src.startswith("%") or src.startswith("$"):
                v = self.value_of(st, src)
            else:
                p = self.addr_of(st, src)
                if p[0] == "gotval":
                    v = ("canary",)
                elif p[0] == "stackparam":
                    v = ("param", p[1])
                elif p[0] == "local" and p[1] in st["slots"]:
                    v = st["slots"][p[1]]
                else:
                    v = ("mem", p, w, signed, a)
            if v[0] == "mem" and ("save", a) in self.saves and \
                    not (w == 8 and p[0] == "local" and
                         self.record_data(p[1])):
                # Eight bytes of a record are copied where they are used and
                # not kept: no local here is eight bytes.
                self.say(st, lines, "set", "s%x" % a, "to", *self.words(v, st))
                v = ("saved", a)
            self.put_reg(st, lines, r, v)
            return
        p = self.addr_of(st, dst)
        v = self.value_of(st, src, w)
        return self.store(b, st, lines, a, p, w, v, src)

    def store(self, b, st, lines, a, p, w, v, src=None):
        if v[0] == "canary":
            return
        if w == 8 and v[0] == "imm" and self.is_double(v[1]):
            # A double written as its bits from an ordinary register: what
            # clang does with a constant it has no reason to load.
            v = ("imm", as_fixed(struct.pack("<Q",
                                             v[1] & 0xffffffffffffffff)))
        if p[0] == "stackarg":
            st["stackargs"][p[1]] = v
            return
        if p[0] == "glob" and p[1] < 0x148:
            return self.machine_store(b, st, lines, a, p[1], w, v)
        if p[0] == "local" and w == 8 and self.record_data(p[1]):
            return self.store_eight(st, lines, p, v, src)
        if p[0] == "through" and w == 8 and p[2] >= 0x10 and \
                v[0] == "mem" and v[2] == 8:
            # Eight bytes of a record copied into one a caller handed over,
            # a word at a time.
            q = v[1]
            ptr = ["arg", str(p[1][1])] if p[1][0] == "param" \
                else ["h_" + p[1][1]]
            for k in (0, 4):
                if q[0] == "local" and self.record_data(q[1]):
                    words = self.local_place(q[1] + k, 4)
                elif q[0] == "through" and q[2] >= 0x10:
                    words = self.words(("mem", (q[0], q[1], q[2] + k), 4,
                                        False, None), st)
                else:
                    raise Hole("eight bytes of %s into a record" % (q[0],))
                self.say(st, lines, "put", *words, "into", *ptr, "at",
                         str(self.through_offset(p[2] + k, 4)))
            self.forget_memory(st, p)
            return
        if p[0] == "local":
            if p[1] in self.frame_locals:
                raise Hole("a write to the machine's own frame")
            if p[1] in self.scalars and v[0] in PURE:
                st["slots"][p[1]] = v
            else:
                st["slots"].pop(p[1], None)
            place = self.local_place(p[1], w)
        elif p[0] == "glob":
            place = self.glob_words(p[1], w)
        elif p[0] == "through" and p[1][0] in ("param", "home"):
            inner = self.through_offset(p[2], w)
            ptr = ["arg", str(p[1][1])] if p[1][0] == "param" \
                else ["h_" + p[1][1]]
            size = {1: ["byte"], 2: ["half"]}.get(w, [])
            self.say(st, lines, "put", *self.words(v, st, reg_of(src or "")),
                     "into", *ptr, "at", str(inner), *size)
            self.forget_memory(st, p)
            return
        else:
            raise Hole("a write through %s" % (p[1][0],))
        self.say(st, lines, "set", *place, "to",
                 *self.words(v, st, reg_of(src) if src else None))
        self.forget_memory(st, p)

    def machine_store(self, b, st, lines, a, off, w, v):
        """The machine's own registers, written inline: loading the left or
        the right scan pointer. Ours is a call, so the three stores that
        make one become it once all three are seen."""
        inl = st["inline"]
        if off == 0x70:
            inl["lnode"] = v
        elif off == 0x80 and v == ("imm", 0):
            inl["loff"] = True
        elif off == 0x88 and v == ("imm", 1):
            inl["lflag"] = True
        elif off == 0x90:
            inl["rnode"] = v
        elif off == 0xa8 and v == ("imm", 1):
            inl["rflag"] = True
        elif off == 0xa0 and v == ("imm", 0):
            inl["roff"] = True
        else:
            raise Hole("a write to the machine at 0x%x" % off)
        left = "lnode" in inl and inl.get("loff") and inl.get("lflag")
        right = "rnode" in inl and inl.get("rflag") and \
            (inl.get("loff") or inl.get("roff"))
        # Both written out together, as lpta_rpta_loadp is, wait for each
        # other: the one clear of the offset serves the two.
        if left and (right or "rnode" not in inl):
            self.inline_load(st, lines, "lpta_loadp", inl.pop("lnode"))
            inl.pop("lflag")
            if not right:
                inl.pop("loff")
        if right and "lnode" not in inl:
            self.inline_load(st, lines, "rpta_loadp", inl.pop("rnode"))
            inl.pop("rflag")
            inl.pop("loff", None)
            inl.pop("roff", None)

    def inline_load(self, st, lines, entry, node):
        """The cell a pointer register was loaded from: the value it copied
        sits eight bytes into one, so the cell is eight bytes before it."""
        if node[0] == "home" and node[2] is not None:
            node = node[2]
        if node[0] in ("mem", "stale", "saved"):
            if node[0] != "mem":
                raise Hole("%s of a value that has moved" % entry)
            p = node[1]
            if p[0] == "glob":
                cell = ["addr", self.var_for(p[1] - 8)]
            elif p[0] == "local" and p[1] - 8 in self.cells:
                cell = ["addr", self.local_name(p[1] - 8)]
            elif p[0] == "through" and p[1][0] == "param" and p[2] == 8:
                cell = ["arg", str(p[1][1])]
            else:
                raise Hole("%s from %s" % (entry, p))
        else:
            raise Hole("%s of %s" % (entry, node[0]))
        self.say(st, lines, "call", entry, *cell)
        # Three stores in 6.1, which change no register of the rule's: only
        # what memory held is no longer known.
        self.forget_memory(st)

    def arith(self, b, st, lines, a, m, o):
        op = m[:-1]
        if op in ("inc", "dec"):
            src, dst = None, o[0]
        elif op in ("shl", "sar", "shr") and len(o) == 1:
            # A shift with no count shifts by one.
            src, dst = "$1", o[0]
        elif len(o) == 2:
            src, dst = o
        else:
            raise Hole("%s with %d operands" % (m, len(o)))
        if dst.startswith("%"):
            r = reg_of(dst)
            v = st["regs"].get(r, ("X",))
            k = int(src.lstrip("$"), 0) if src and src.startswith("$") else None
            if v[0] == "imm" and (src is None or k is not None):
                n = v[1]
                # The processor takes a shift count's low five bits.
                c = (k or 0) & 31
                n = {"inc": n + 1, "dec": n - 1, "add": n + (k or 0),
                     "sub": n - (k or 0), "and": n & (k or 0),
                     "or": n | (k or 0), "shl": n << c,
                     "sar": n >> c, "shr": n >> c}[op]
                self.put_reg(st, lines, r, ("imm", n))
                self.flags_from(st, lines, ("imm", n), r, m, a)
                return
            if r not in self.homes or v[0] != "home":
                raise Need(("home", r))
            self.arith_on(st, lines, op, ["h_" + r], src,
                          {"w": 2, "b": 1}.get(m[-1], 4))
            st["regs"][r] = ("home", r, None)
            st["fresh"].add(r)
            self.flags_from(st, lines, st["regs"][r], r, m, a)
            return
        p = self.addr_of(st, dst)
        w = {"l": 4, "q": 8, "w": 2, "b": 1}[m[-1]]
        if w == 1:
            raise Hole("arithmetic on one byte")
        if p[0] == "local":
            place = self.local_place(p[1], w)
            st["slots"].pop(p[1], None)
        elif p[0] == "glob" and p[1] >= 0x148:
            place = self.glob_words(p[1], w)
        elif p[0] == "through" and p[1][0] in ("param", "home"):
            # Read, worked on and put back, since a place reached through a
            # pointer is not one an operation can name.
            words = self.words(("mem", p, w, False, a), st)
            self.scratch = True
            self.say(st, lines, "set", "scratch", "to", *words)
            self.arith_on(st, lines, op, ["scratch"], src, w)
            ptr = ["arg", str(p[1][1])] if p[1][0] == "param" \
                else ["h_" + p[1][1]]
            self.say(st, lines, "put", "scratch", "into", *ptr, "at",
                     str(self.through_offset(p[2], w)),
                     *{1: ["byte"], 2: ["half"]}.get(w, []))
            self.forget_memory(st, p)
            name = "n%x_r" % a
            self.snaps.add(name)
            self.say(st, lines, "set", name, "to", "scratch")
            self.flags_from(st, lines, ("snap", name), None, m, a)
            return
        else:
            raise Hole("arithmetic on %s" % (p,))
        self.arith_on(st, lines, op, place, src, w)
        self.forget_memory(st, p)
        self.flags_from(st, lines, ("mem", p, w, False, a), None, m, a)

    def flags_from(self, st, lines, v, r, m, a):
        """The flags an operation leaves are those of what it left, which is
        what a branch after it reads: held against nought, at the width the
        operation was done in."""
        if not self.flags_read(a):
            st["cond"] = None
            return
        w = {"l": 4, "q": 8, "w": 2, "b": 1}.get(m[-1], 4)
        if w < 4:
            v = self.narrow(st, lines, v, w, a, 2)
        st["cond"] = ("test", self.snap(st, lines, v, a, 2), r)

    def flags_read(self, a):
        """Whether anything reads the flags the instruction at a leaves
        before something else sets them. A jump away is taken to mean they
        may be read where it lands."""
        i = self.by_addr[a] + 1
        while i < len(self.ins):
            m = self.ins[i][1]
            if m in CONDS or (m.startswith("set") and m[3:] in WORDS) or \
                    m.startswith("cmov") or m == "jmp" or m == "jmpq":
                return True
            if m.startswith(("cmp", "test", "add", "sub", "and", "or", "xor",
                             "inc", "dec", "neg", "shl", "sar", "shr",
                             "imul", "callq", "ucomisd", "ret")):
                return False
            i += 1
        return False

    def arith_on(self, st, lines, op, place, src, width=4):
        if op == "inc":
            self.say(st, lines, "increment", *place)
        elif op == "dec":
            self.say(st, lines, "decrement", *place)
        else:
            word = {"add": ("add", "to"), "sub": ("subtract", "from"),
                    "and": ("and", "into"), "or": ("or", "into"),
                    "shl": ("shift left", "into"),
                    "sar": ("shift right", "into")}.get(op)
            if word is None:
                raise Hole("%s on a variable" % op)
            # A source in memory is read as wide as the operation is.
            v = self.value_of(st, src, width)
            self.say(st, lines, *word[0].split(), *self.words(v, st, reg_of(src)),
                     word[1], *place)

    # -- calls -----------------------------------------------------------

    # Two entries 6.1 has and ours does not: each is a load and a set of the
    # scan direction in one, which ours writes as two calls and a wrapper.
    SPLIT = {"lpta_loadp_setscan_r": ("lpta_loadp", "setscan_r"),
             "lpta_loadp_setscan_l": ("lpta_loadp", "setscan_l")}

    def call(self, b, st, lines, a, o):
        t = o[0]
        regs = st["regs"]
        if self.is_stub(t) == "setjmp":
            self.after_call(st, ("frame",))
            return
        stub = self.m.stub_names.get(int(t.split()[0], 16)) \
            if t.startswith("0x") else None
        if stub == "memset" and regs.get("rdi") == ("machine", "index") and \
                st.get("fence") is not None:
            # The index cleared for an inline fence; a fence of nothing ends
            # here.
            if st["fence"]["n"] == 0:
                self.say(st, lines, "call", "fence", "0", "sym",
                         self.smap(0, "null_str", b"\x00"))
                self.callees["fence"] = 2
                st["fence"] = None
            for r in CALLER:
                regs[r] = ("X",)
            return
        if stub in LIBM:
            # The C library on a double, which the runtime here answers in
            # fixed values: handed in a register and answered in one.
            args = ["xmm0", "xmm1"] if stub == "pow" else ["xmm0"]
            words = []
            for r in args:
                words += self.words(regs.get(r, ("X", r)), st, r)
            self.say(st, lines, "call", LIBM[stub], *words)
            self.callees[LIBM[stub]] = len(args)
            self.after_call(st, ("ans", a, None))
            regs["xmm0"], regs["rax"] = regs["rax"], ("X",)
            if ("save", a) in self.saves:
                self.say(st, lines, "set", "s%x" % a, "to", "answer")
                regs["xmm0"] = ("saved", a)
            if "xmm0" in self.homes:
                self.say(st, lines, "set", "h_xmm0", "to", "answer")
                regs["xmm0"] = ("home", "xmm0", None)
            return
        mm = re.search(r"<_([^>+]+)>", t)
        if not mm or not t.startswith("0x"):
            raise Hole("a call to %s" % t)
        name = mm.group(1)
        if name == "ventproc":
            self.after_call(st, ("frame",))
            return
        if name in ("vretproc", "vback"):
            raise Hole("%s outside the way out" % name)
        at = int(t.split()[0], 16)
        if at in self.m.private:
            if regs.get("rdi") != ("state",):
                raise Hole("%s is not handed the state" % name)
            n = self.arity_of_private(at)
            if name == "fence" and n == 0:
                self.say(st, lines, "call", "fence", "0", "sym",
                         self.smap(0, "null_str", b"\x00"))
                self.after_call(st, ("ans", a, None))
                return
            if name == "fence" and n == 1:
                # The count folded in and the string handed over: the copy
                # stores the count where fence keeps it before reading the
                # string, which is the one argument.
                count = self.private_fence_count(at)
                if count is None:
                    raise Hole("a private fence whose count cannot be read")
                self.say(st, lines, "call", "fence", str(count),
                         *self.words(regs.get("rsi", ("X",)), st, "rsi"))
                self.after_call(st, ("ans", a, None))
                return
            if name != "fence" or n != 2:
                raise Hole("a private %s with %d arguments" % (name, n))
        if regs.get("rdi") != ("state",):
            raise Hole("%s is not handed the state" % name)
        shape = SHAPE.get(name)
        n = shape[0] if shape else self.arity_of(name)
        if name in FLOAT_ARGS:
            n = FLOAT_ARGS[name]
        if n is None:
            n = 0
            for k, r in enumerate(ARGS[1:], 1):
                if r in st["fresh"]:
                    n = k
            for k in range(1, n + 1):
                if ARGS[k] not in st["fresh"]:
                    raise Hole("%s handed a register nothing wrote" % name)
        args = []
        for k in range(1, min(n, 5) + 1):
            r = ARGS[k]
            args.append((regs.get(r, ("X",)), r))
        for v in reversed(st["pushes"]):
            args.append((v, None))
        for off in sorted(st["stackargs"]):
            args.append((st["stackargs"][off], None))
        if len(args) < n and name not in VARIADIC:
            # Said short, the call would hand the entry whatever was left in
            # the argument area, which is how forto_adv_upto_r came to step a
            # loop to an end nobody named.
            raise Hole("%s handed %d of its %d arguments"
                       % (name, len(args), n))
        if name in FLOAT_ARGS:
            args.append((regs.get("xmm0", ("X", "xmm0")), "xmm0"))
        if shape:
            args = shape[1](args)
        words = [self.words(v, st, r) for v, r in args]
        chain = None
        if name in LISTED and self.setmap is not None:
            which, k = LISTED[name]
            v = inner = args[k][0]
            while inner is not None and inner[0] == "home":
                inner = inner[2]
            if inner is not None and inner[0] == "imm":
                words[k] = [str(self.setmap(which, inner[1]))]
            elif v[0] == "home" and k < 5 and self.reaching_imms(a, ARGS[k + 1]):
                # Paths that pick different lists meet before one call, so
                # there is a call for each number a path can bring, each with
                # its own list's number in ours.
                chain = (v[1], which, k, sorted(self.reaching_imms(a, ARGS[k + 1])))
            else:
                raise Hole("a %s chosen at run time" % which)
        self.callees[name] = len(args)
        if name in self.SPLIT:
            if len(words) != 2:
                raise Hole("%s with %d arguments" % (name, len(words)))
            first, second = self.SPLIT[name]
            self.say(st, lines, "call", first, *words[0])
            self.say(st, lines, "call", second, *words[1])
        elif chain:
            r, which, k, nums = chain
            for n in nums:
                words[k] = [str(self.setmap(which, n))]
                flat = [w for ws in words for w in ws]
                if n != nums[-1]:
                    self.say(st, lines, "if", "h_" + r, "is", str(n))
                self.say(st, lines, "call", name, *flat)
                if n != nums[-1]:
                    self.say(st, lines, "go", "to", "q%x" % a)
                    self.say(st, lines, "end")
            lines.append(["place", "q%x" % a])
        else:
            flat = [w for ws in words for w in ws]
            self.say(st, lines, "call", AS_61.get(name, name), *flat)
        self.after_call(st, ("ans", a, None))
        if ("save", a) in self.saves:
            self.say(st, lines, "set", "s%x" % a, "to", "answer")
            regs["rax"] = ("saved", a)
        if "rax" in self.homes:
            self.say(st, lines, "set", "h_rax", "to", *self.words(regs["rax"], st))
            regs["rax"] = ("home", "rax", None)

    def reaching_imms(self, a, r):
        """Every constant r can hold at the instruction at a, from walking
        back along each path to whatever last wrote it. None when a path
        writes it any other way, crosses a call or reaches the top."""
        preds = {}
        for blk, outs in self.succ.items():
            for t in outs:
                preds.setdefault(t, set()).add(blk)
        where = self.by_addr[a]
        todo = []
        for start, idx in self.blocks:
            if where in idx:
                todo.append((start, idx.index(where)))
        out, seen = set(), set()
        while todo:
            b, j = todo.pop()
            if (b, j) in seen:
                continue
            seen.add((b, j))
            idx = self.block_idx[b]
            for k in range(j - 1, -1, -1):
                _a, m, o, _n = self.ins[idx[k]]
                if m == "callq":
                    return None
                if not o or not o[-1].startswith("%") or reg_of(o[-1]) != r \
                        or m.startswith(("cmp", "test", "push")):
                    continue
                prev = self.ins[idx[k - 1]] if k > 0 else None
                if m in ("xorl", "xorq") and o[0] == o[1]:
                    out.add(0)
                elif m in ("movl", "movq") and o[0].startswith("$"):
                    out.add(int(o[0][1:], 0))
                elif m == "popq" and prev and prev[1] == "pushq" and \
                        prev[2][0].startswith("$"):
                    out.add(int(prev[2][0][1:], 0))
                else:
                    return None
                break
            else:
                if not preds.get(b):
                    return None
                todo += [(p, len(self.block_idx[p])) for p in preds[b]]
        return out

    def arity_of_private(self, at):
        """How many argument registers a private copy reads before it writes
        them, from its own code."""
        name = self.m.at.get(at)
        ins = self.m.functions.get(name, [])
        start = [k for k, x in enumerate(ins) if x[0] == at]
        if not start:
            return 0
        seen, n = set(), 0
        for _a, m, o, _n in ins[start[0]:start[0] + 30]:
            if m in ("retq", "jmp", "callq"):
                break
            for k, t in enumerate(o):
                r = reg_of(t) if t.startswith("%") else None
                if r in ARGS[1:] and r not in seen:
                    if k == 0:
                        n = max(n, ARGS.index(r))
                    seen.add(r)
            if len(o) == 2 and o[1].startswith("%"):
                seen.add(reg_of(o[1]))
        return n

    def private_fence_count(self, at):
        """The count a private copy of fence was specialised with: it stores
        it where fence keeps the count, 0x1fb9 into the block 0xc0 names."""
        for _a, m, o, _n in self.m.functions.get(self.m.at.get(at), []):
            if m == "movb" and len(o) == 2 and o[0].startswith("$") and \
                    o[1].startswith("0x1fb9("):
                return int(o[0][1:], 0)
        return None

    def arity_of(self, name):
        """How many arguments a call hands over besides the state: ours for
        an entry of the runtime, and for a rule what the rule itself reads."""
        global ARITY
        if ARITY is None:
            ARITY = our_arities()
        if name in self.SPLIT:
            return 2
        if name in ARITY:
            return ARITY[name]
        if name in self.m.functions:
            cache = self.m.__dict__.setdefault("params_cache", {})
            if name not in cache:
                r = Rule(self.m, name)
                try:
                    r.walk()
                    cache[name] = r.params() if not r.holes else None
                except (Hole, IndexError, KeyError):
                    cache[name] = None
            return cache[name]
        return None

    def after_call(self, st, ans):
        for r in CALLER:
            st["regs"][r] = ("X",)
        if ans is not None:
            if ans[0] == "ans":
                ans = ("ans", ans[1], st["said"])
            st["regs"]["rax"] = ans
        st["fresh"] = set()
        st["cleanup"] = len(st["pushes"])
        st["pushes"] = []
        st["stackargs"] = {}
        self.forget_memory(st)

    def cond_words(self, st, cc):
        c = st["cond"]
        if c is None:
            raise Hole("a branch on nothing tested")
        if c[0] == "bool":
            # A comparison each way in made for itself and kept as one or
            # nought, read as the condition it was kept for or its opposite.
            if cc == c[2]:
                return [c[1], "is", "not", "0"]
            if cc == OPPOSITE.get(c[2]):
                return [c[1], "is", "0"]
            raise Hole("a kept comparison read as %s" % cc)
        if c[0] == "test":
            # A value tested against itself sets the sign and zero flags from
            # the value alone, so every condition on them is the value held
            # against nought; the overflow flag is clear, so less is negative.
            signed = {"e": "is", "ne": "is not", "le": "is at most",
                      "g": "is more than", "s": "is less than",
                      "l": "is less than", "ns": "is at least",
                      "ge": "is at least"}.get(cc)
            if signed is None:
                raise Hole("a test read as %s" % cc)
            return self.words(c[1], st, c[2]) + signed.split() + ["0"]
        if c[0] == "cmp":
            # The sign of a difference is the comparison as long as the
            # difference cannot overflow, and the compiler only reads it so
            # against nought.
            word = WORDS.get(cc) or {"s": "is less than",
                                     "ns": "is at least"}.get(cc)
            if word is None:
                raise Hole("a comparison read as %s" % cc)
            if c[0] == "cmp" and len(c) > 5 and c[5] == "float":
                # ucomisd sets the flags the way an unsigned comparison does,
                # and fixed values compare as signed whole numbers.
                word = {"is below": "is less than",
                        "is not below": "is at least",
                        "is above": "is more than",
                        "is not above": "is at most"}.get(word, word)
            return self.words(c[1], st, c[3]) + word.split() + \
                self.words(c[2], st, c[4])
        raise Hole("a branch on %s" % c[0])

    # -- a block at a time -----------------------------------------------

    def clone(self, st):
        return {"regs": dict(st["regs"]), "fresh": set(st["fresh"]),
                "fence": st.get("fence"), "cleanup": st.get("cleanup", 0),
                "pushes": list(st["pushes"]), "cond": st["cond"],
                "said": st["said"], "inline": dict(st["inline"]),
                "slots": dict(st["slots"]), "begun": st["begun"],
                "stackargs": dict(st.get("stackargs", {}))}

    def entry_state(self):
        regs = {"rdi": ("state",), "rsp": ("sp",)}
        for k, r in enumerate(ARGS[1:], 1):
            regs[r] = ("param", k)
        return {"regs": regs, "fresh": set(), "pushes": [], "cond": None,
                "said": (self.lo, 0), "inline": {}, "slots": {},
                "begun": False, "stackargs": {}}

    def meet(self, a, b):
        regs = {}
        for r in set(a["regs"]) | set(b["regs"]):
            x, y = a["regs"].get(r, ("X",)), b["regs"].get(r, ("X",))
            if x != y and "pair" in (x[0], y[0]):
                # A packed pair one way in and not the other, which matters
                # only if the upper half is read after: pair_half asks then.
                self.pair_meets.add(r)
            if x == y and x[0] != "X":
                regs[r] = x
            elif x[0] == "home" and y[0] == "home":
                regs[r] = ("home", r, None)
            elif r in XMM and r in self.homes and "home" in (x[0], y[0]):
                # A double one way in left in its local and the other way
                # never written: the local holds the first, which is all a
                # place that reads it can mean.
                regs[r] = ("home", r, None)
            else:
                regs[r] = ("X",)
        slots = {k: v for k, v in a["slots"].items()
                 if b["slots"].get(k) == v}
        # An argument a call takes off the stack may be written on each way
        # in before the ways meet and the call is made: adjust_por_accents
        # writes its forto's last on all three.
        stackargs = {k: v for k, v in a["stackargs"].items()
                     if b["stackargs"].get(k) == v}
        return {"regs": regs, "fresh": a["fresh"] & b["fresh"],
                "pushes": a["pushes"] if a["pushes"] == b["pushes"] else [],
                "cond": a["cond"] if a["cond"] == b["cond"] else None,
                "said": a["said"], "inline": {}, "slots": slots,
                "begun": True, "stackargs": stackargs}

    def pre_exit(self, t):
        """The way out a block leads straight into if all it does is move
        registers about: what it answers then depends on the way in, which
        is where each way in has to say it."""
        idx = self.block_idx.get(t)
        if not idx or t in self.exits or t in self.dispatch_head or \
                t in self.dispatch_rest:
            return None
        for i in idx:
            _a, m, o, _n = self.ins[i]
            if m in ("movl", "movq") and o[0].startswith("%") and \
                    o[1].startswith("%") and not o[0].startswith("%xmm"):
                continue
            if (m in ("xorl", "xorq") and o[0] == o[1]) or \
                    (m == "pushq" and o[0].startswith("$")) or m == "popq":
                continue
            if m == "jmp" and i == idx[-1]:
                continue
            return None
        outs = self.succ.get(t, [])
        return outs[0] if len(outs) == 1 and outs[0] in self.exits else None

    def edge(self, st, t):
        nxt = self.pre_exit(t)
        if nxt is not None:
            s = self.clone(st)
            for i in self.block_idx[t]:
                _a, m, o, _n = self.ins[i]
                if m in ("movl", "movq"):
                    s["regs"][reg_of(o[1])] = s["regs"].get(reg_of(o[0]),
                                                            ("X",))
                elif m in ("xorl", "xorq"):
                    s["regs"][reg_of(o[1])] = ("imm", 0)
                elif m == "pushq":
                    s["pushes"].append(("imm", int(o[0].lstrip("$"), 0)))
                elif m == "popq":
                    s["regs"][reg_of(o[0])] = s["pushes"].pop() \
                        if s["pushes"] else ("X",)
            return self.edge(s, nxt)
        if t in self.exits:
            ans = self.exits[t]
            # The way out may move the answer about before it calls
            # vretproc, so run what it does first on a copy.
            s = self.clone(st)
            for i in self.block_idx[t]:
                _a, m, o, _n = self.ins[i]
                if m == "callq":
                    break
                if m in ("movl", "movq") and o[0].startswith("%") and \
                        o[1].startswith("%"):
                    s["regs"][reg_of(o[1])] = s["regs"].get(reg_of(o[0]),
                                                            ("X",))
                elif m == "pushq" and o[0].startswith("$"):
                    s["pushes"].append(("imm", int(o[0].lstrip("$"), 0)))
                elif m == "popq" and s["pushes"]:
                    s["regs"][reg_of(o[0])] = s["pushes"].pop()
                elif m in ("xorl", "xorq") and o[0] == o[1]:
                    s["regs"][reg_of(o[1])] = ("imm", 0)
                elif m == "movq" and reg_of(o[1]) == "rdi":
                    pass
                else:
                    raise Hole("a way out that does %s first" % m)
            v = ans if ans[0] == "imm" else s["regs"].get(ans[1], ("X",))
            if v[0] == "home" and v[2] is not None:
                v = v[2]
            if v == ("imm", 0):
                return [["match"]]
            if v == ("imm", 94):
                return [["give", "up"]]
            raise Hole("a way out answering %s" % (v,))
        if t in self.dispatch_head:
            return self.backtrack_lines(st)
        if t in self.dispatch_rest:
            raise Hole("a jump into the middle of the dispatch")
        return [["go", "to", "p%x" % t]]

    def matched_tags(self):
        """Numbers the dispatch sends straight to a way out that matches."""
        out = []
        for t, ks in sorted(self.exit_tags.items()):
            ans = self.exits[t]
            for k in ks:
                # What the decoder put in the answer's register on the way,
                # and failing that what it held as the dispatch began.
                fact = self.exit_facts.get((t, k), {}).get(ans[1]) \
                    if ans[0] == "reg" else None
                if ans[0] == "imm":
                    v = ans
                elif isinstance(fact, int):
                    v = ("imm", fact)
                elif fact is not None and fact[0] != "reg":
                    v = fact
                else:
                    src = fact[1] if fact is not None else ans[1]
                    st = getattr(self, "dispatch_state", None)
                    v = st["regs"].get(src, ("X",)) if st else ("X",)
                    if v[0] == "home" and v[2] is not None:
                        v = v[2]
                if v == ("imm", 0):
                    out.append(k)
                elif v != ("imm", 94):
                    raise Hole("a number that leads to a way out answering %s"
                               % (v,))
        return sorted(out)

    def do_block(self, b, st, lines):
        idx = self.block_idx[b]
        st["said"] = (b, 0)
        if b in self.exits or b in self.exit_tail:
            return []
        if b in self.dispatch_rest:
            return []
        if self.pre_exit(b) is not None:
            return []
        if b in self.dispatch_head:
            self.dispatch_state = self.clone(st)
            s = self.clone(st)
            for r in CALLER:
                s["regs"][r] = ("X",)
            if self.depth_reg:
                s["regs"][self.depth_reg] = ("home", self.depth_reg, None) \
                    if self.depth_reg in self.homes else ("X",)
            if self.depth_slot is not None:
                s["slots"].pop(self.depth_slot, None)
            s["fresh"] = set()
            s["pushes"] = []
            s["cond"] = None
            self.dispatch_regs = dict(s["regs"])
            outs = []
            for t in self.tags:
                st_t = self.clone(s)
                for r, v in self.tag_facts.get(t, {}).items():
                    if r != "stores":
                        st_t["regs"][r] = self.fact_value(r, v)
                outs.append((t, st_t))
            return outs

        facts = self.tag_facts.get(b, {}) if b in self.tags else {}
        stores = facts.get("stores", ())
        facts = {r: v for r, v in facts.items() if r != "stores"}
        said = [(r, v) for r, v in sorted(facts.items())
                if r in self.homes and v != ("X",)]
        if said or stores or any(isinstance(v, int) and r not in CALLER
                                 for r, v in facts.items()):
            # What the decoder set on the way is only so when the rule comes
            # back here, so it is said where only coming back reaches.
            self.say(st, lines, "go", "to", "p%x" % b)
            for k in self.tags[b]:
                lines.append(["place", "t%x_%d" % (b, k), "on", str(k)])
            self.say_unwind(st, lines)
            for off, w, v in stores:
                if isinstance(v, int):
                    words = [str(v)]
                elif v[0] == "reg":
                    words = self.words(self.fact_value(v[1], v, False), st,
                                       v[1])
                else:
                    words = self.words(v, st)
                self.say(st, lines, "set", *self.local_place(off, w), "to",
                         *words)
                st["slots"].pop(off, None)
            for r, v in said:
                if isinstance(v, int):
                    self.say(st, lines, "set", "h_" + r, "to", str(v))
                else:
                    self.say(st, lines, "set", "h_" + r, "to",
                             *self.words(self.fact_value(r, v, False), st))
            lines.append(["place", "p%x" % b])
        else:
            lines.append(["place", "p%x" % b])
            for k in self.tags.get(b, []):
                lines.append(["place", "t%x_%d" % (b, k), "on", str(k)])
            if b in self.tags:
                self.say_unwind(st, lines)
        for i in idx:
            self.step(b, st, lines, i)
        if st["inline"]:
            raise Hole("a scan pointer half loaded")
        last = self.ins[idx[-1]]
        m = last[1]
        nxt = self.ins[idx[-1] + 1][0] if idx[-1] + 1 < len(self.ins) else None
        if m in CONDS:
            t = self.target(last[2])
            if st["cond"] == ("frame",):
                # Nonzero from setjmp or ventproc is the rule failing, which
                # our compiler's own entry already sends to giving up.
                if m == "jne":
                    return [(nxt, st)]
                if m == "je":
                    # The way on is a jump past the block that gives up, and
                    # when anything else reaches that block it is written
                    # out next, so the jump has to be said.
                    if self.written_after(b) != t:
                        self.say(st, lines, "go", "to", "p%x" % t)
                    return [(t, st)]
                raise Hole("the landing place read as %s" % m)
            body = self.edge(st, t)
            self.say(st, lines, "if", *self.cond_words(st, CONDS[m]))
            for line in body:
                self.say(st, lines, *line)
            self.say(st, lines, "end")
            for line in self.fall(st, nxt):
                self.say(st, lines, *line)
            return [(t, st), (nxt, st)]
        if m == "jmp":
            t = self.target(last[2])
            self.keep_flags(st, lines, t)
            for line in self.edge(st, t):
                self.say(st, lines, *line)
            return [(t, st)]
        if m == "jmpq" and last[0] in self.tables:
            # A switch of the C the rules were compiled through: each entry
            # a test of the number it was chosen by, the bound already tested.
            r = self.table_index[last[0]]
            idx = self.words(st["regs"].get(r, ("X", r)), st, r)
            outs = []
            for k, t in enumerate(self.tables[last[0]][1]):
                self.say(st, lines, "if", *idx, "is", str(k))
                for line in self.edge(st, t):
                    self.say(st, lines, *line)
                self.say(st, lines, "end")
                outs.append((t, st))
            self.say(st, lines, "give", "up")
            return outs
        if m == "retq":
            raise Hole("a return that is not the way out")
        self.keep_flags(st, lines, nxt)
        for line in self.fall(st, nxt):
            self.say(st, lines, *line)
        return [(nxt, st)]

    def needs_flags(self, t):
        """The condition a block reads before it sets one, if any: clang
        shares a tail that picks with cmov between ways in that each
        compared something different."""
        for i in self.block_idx.get(t, []):
            _a, m, o, _n = self.ins[i]
            if m in CONDS:
                return CONDS[m]
            if m.startswith("cmov"):
                return m[4:] if m[4:] in OPPOSITE else m[4:-1]
            if m.startswith("set") and m[3:] in OPPOSITE:
                return m[3:]
            if m.startswith(("cmp", "test", "ucomisd", "callq", "add", "sub",
                             "and", "or", "xor", "inc", "dec", "sh", "sa",
                             "neg", "imul")):
                return None
        return None

    def keep_flags(self, st, lines, t):
        """Before going to a block that reads a condition it did not make,
        say what that condition is as one or nought."""
        cc = self.needs_flags(t)
        if cc is None or st["cond"] is None or st["cond"][0] == "bool" or \
                st["cond"] == ("frame",):
            return
        name = "f%x" % t
        self.kept.add(name)
        test = self.cond_words(st, cc)
        self.say(st, lines, "set", name, "to", "0")
        self.say(st, lines, "if", *test)
        self.say(st, lines, "set", name, "to", "1")
        self.say(st, lines, "end")
        st["cond"] = ("bool", name, cc)

    def say_unwind(self, st, lines):
        """Where the dispatch kept the count it was handed, it now holds what
        is left of it."""
        if self.depth_reg in self.homes:
            self.say(st, lines, "set", "h_" + self.depth_reg, "to", "unwind")
        if self.depth_slot is not None:
            self.say(st, lines, "set", *self.local_place(self.depth_slot, 4),
                     "to", "unwind")
            st["slots"].pop(self.depth_slot, None)

    def fact_value(self, r, v, homed=True):
        """What a register the decoder set holds at a place it leads to."""
        if isinstance(v, int):
            v = ("imm", v)
        elif v[0] == "reg" and v[1] == self.depth_reg:
            # A copy of the count the dispatch was handed is what is left of
            # it.
            v = ("unwindv",)
        elif v[0] == "reg":
            src = v[1]
            v = ("X",) if src in CALLER else \
                self.dispatch_regs.get(src, ("X",))
            if v == ("X",) and src not in CALLER and src in self.homes:
                # Whatever the register held as the dispatch began is in its
                # local, whichever way the dispatch was reached.
                v = ("home", src, None)
            if v == ("X",) and src not in CALLER and not homed:
                # A copy of a register the dispatch kept but nobody can name
                # the value of: that register wants a local of its own.
                raise Need(("home", src))
            if v[0] == "home" and v[2] is not None and v[2][0] in PURE:
                v = v[2]
            if v[0] == "home" and r not in self.homes:
                raise Need(("home", r))
        if homed and r in self.homes and v[0] != "X":
            while v is not None and v[0] == "home":
                v = v[2]
            return ("home", r, v)
        return v

    def written_after(self, b):
        """The first block after b that the rule's text will hold: the way
        out, the dispatch and blocks nothing reaches are not written."""
        silent = set(self.exits) | self.exit_tail | self.dispatch_rest | \
            self.dispatch_head | {s for s, _i in self.blocks
                                  if self.pre_exit(s) is not None}
        starts = [s for s, _idx in self.blocks]
        for s in starts[starts.index(b) + 1:]:
            if s in self.entered and s not in silent:
                return s
        return None

    def depth_at(self, st):
        """What the slow half of the dispatch would hand vback as the count,
        by running what it does before the call on what holds on the way in.
        Some halves work it out from the last call's answer: one if it
        matched and nought if not."""
        regs = dict(st["regs"])
        cond = None
        for i in self.block_idx[self.slow_block]:
            _a, m, o, _n = self.ins[i]
            if m == "callq":
                break
            if m in ("xorl", "xorq") and o[0] == o[1]:
                regs[reg_of(o[1])] = ("imm", 0)
            elif m in ("testl", "testq") and o[0] == o[1]:
                cond = regs.get(reg_of(o[0]), ("X", reg_of(o[0])))
            elif m in ("sete", "setne") and cond is not None:
                regs[reg_of(o[0])] = ("eq0" if m == "sete" else "ne0", cond)
            elif m in ("movl", "movq", "movb") and o[0].startswith("%") and \
                    o[1].startswith("%"):
                # A value nobody can name is named by the register it came
                # from, which is the one that wants a local, not the one the
                # count is handed over in.
                v = regs.get(reg_of(o[0]), ("X", reg_of(o[0])))
                regs[reg_of(o[1])] = ("X", reg_of(o[0])) \
                    if v == ("X",) or v[0] == "cc" else v
            elif m in ("movl", "movq") and o[0].startswith("$"):
                regs[reg_of(o[1])] = ("imm", int(o[0].lstrip("$"), 0))
            elif m == "movq" and reg_of(o[1]) == "rdi":
                pass
            elif m == "movsd" and o[0].startswith("%xmm"):
                # A double kept over the call to vback, which the compiler
                # thought might change it.
                pass
            else:
                raise Hole("a dispatch that does %s before vback" % m)
        return regs.get("rsi", ("X", "rsi"))

    def backtrack_lines(self, st):
        d = self.depth_at(st)
        if d[0] in ("eq0", "ne0"):
            test = self.words(d[1], st)
            hit, miss = ("1", "0") if d[0] == "eq0" else ("0", "1")
            return [["if", *test, "is", "0"], ["set", "unwind", "to", hit],
                    ["end"], ["if", *test, "is", "not", "0"],
                    ["set", "unwind", "to", miss], ["end"], ["backtrack"]]
        return [["set", "unwind", "to", *self.words(d, st, "rsi")],
                ["backtrack"]]

    def fall(self, st, t):
        if t in self.exits or t in self.dispatch_head or \
                t in self.dispatch_rest or self.pre_exit(t) is not None:
            return self.edge(st, t)
        return []

    def run(self):
        self.block_idx = {s: idx for s, idx in self.blocks}
        order = [s for s, _idx in self.blocks]
        ins = {self.lo: self.entry_state()}
        text = {}
        needs = set()
        self.pair_meets = set()
        for _round in range(80):
            changed = False
            self.entered = set(ins)
            for b in order:
                if b not in ins:
                    continue
                st = self.clone(ins[b])
                lines = []
                try:
                    outs = self.do_block(b, st, lines)
                except Need as n:
                    needs.add(n.args[0])
                    outs = []
                text[b] = lines
                for t, s in outs:
                    old = ins.get(t)
                    new = self.meet(old, s) if old is not None else self.clone(s)
                    if old is None or new["regs"] != old["regs"] or \
                            new["slots"] != old["slots"] or \
                            new["fresh"] != old["fresh"] or \
                            new["cond"] != old["cond"] or \
                            new["pushes"] != old["pushes"]:
                        ins[t] = new
                        changed = True
            if not changed:
                break
        return text, needs

    def find_frame(self):
        """The five places the machine writes to, and the landing place: the
        compiler hands each its address in the calls at the top."""
        for k, (a, m, o, _n) in enumerate(self.ins):
            if m != "callq":
                continue
            is_vent = o[0].endswith("<_ventproc>")
            if not is_vent and self.is_stub(o[0]) != "setjmp":
                continue
            want = ARGS[1:] if is_vent else ("rdi",)
            for j in range(k - 1, max(k - 12, -1), -1):
                _a, mj, oj, _nj = self.ins[j]
                if mj == "leaq" and reg_of(oj[1]) in want and \
                        oj[0].endswith("(%rbp)"):
                    off = int(MEM.match(oj[0]).group(1), 0)
                    self.frame_locals.add(off)
        self.cells -= self.frame_locals
        for off in self.frame_locals:
            self.scalars.pop(off, None)
        # The stack check's own word.
        for k, (a, m, o, n) in enumerate(self.ins):
            if m == "movq" and len(o) == 2 and o[0] == "%rax" and \
                    o[1].endswith("(%rbp)") and k >= 2 and \
                    self.ins[k - 1][1] == "movq" and \
                    self.ins[k - 1][2][0] == "(%rax)":
                off = int(MEM.match(o[1]).group(1), 0)
                self.scalars.pop(off, None)
                self.canary = off

    def params(self):
        """How many arguments the rule is handed besides the state: the
        highest argument register it reads before anything writes it."""
        seen = set()
        n = 0
        for a in self.reach[:40]:
            _a, m, o, _n = self.ins[self.by_addr[a]]
            if m == "callq":
                break
            # A register cleared or popped into is written, not read.
            if (m in ("xorl", "xorq") and len(o) == 2 and o[0] == o[1]) or \
                    m == "popq":
                seen.add(reg_of(o[-1]))
                continue
            for k, t in enumerate(o):
                r = reg_of(t) if t.startswith("%") else None
                if r in ARGS[1:] and r not in seen:
                    if k == 0 or m.startswith("cmp") or m.startswith("test"):
                        n = max(n, ARGS.index(r))
                    seen.add(r)
            if len(o) == 2 and o[1].startswith("%"):
                seen.add(reg_of(o[1]))
        # And the arguments past the fifth, which are wherever the rule
        # reads them from above its frame.
        for a in self.reach:
            for t in self.ins[self.by_addr[a]][2]:
                mm = MEM.match(t)
                if mm and mm.group(2) == "%rbp" and not mm.group(3) and \
                        mm.group(1) and int(mm.group(1), 0) >= 16:
                    n = max(n, 6 + (int(mm.group(1), 0) - 16) // 8)
        return n

    def lift(self, gmap, smap, setmap=None):
        """The rule in the upper form. gmap names a variable, smap a string,
        and setmap, when given, a lookup set or a dictionary action: each
        is numbered by its place in the language's own list, and 6.1's lists
        are not 4.3's, so a number carried across as it stands would look in
        some other list without anything saying so."""
        self.gmap = gmap
        self.smap = smap
        self.setmap = setmap
        self.globals_used = set()
        self.syms_used = set()
        self.vars_used = {}
        self.callees = {}
        self.canary = None
        self.scratch = False
        self.kept = set()
        self.packs = set()
        # What the registers hold where the dispatch begins, which a place it
        # leads to can be reached before: until then none of them is known.
        self.dispatch_regs = {}
        try:
            self.walk()
            if self.holes:
                return None
            self.hoist_shared_loads()
            self.classify()
            self.locals_used()
            self.frame_locals = set()
            self.find_frame()
            self.homes = set()
            self.pairs = set()
            self.saves = set()
            self.snaps = set()
            again = False
            for _ in range(40):
                text, needs = self.run()
                if not needs:
                    break
                new = {n for n in needs if not (
                    (n[0] == "home" and n[1] in self.homes) or
                    (n[0] == "pairs" and set(n[1]) <= self.pairs) or
                    n in self.saves)}
                if not new:
                    # A place the dispatch leads to may come before the
                    # dispatch in the rule, and read what the dispatch held
                    # a round ago: one round more, with nothing new, settles
                    # that or says it never will.
                    if again:
                        raise Hole("a value that still cannot be named: %s"
                                   % sorted(needs)[0][0])
                    again = True
                    continue
                again = False
                for n in new:
                    if n[0] == "home":
                        if n[1] is None or n[1] in ("rsp", "rbp"):
                            raise Hole("a register that cannot have a home")
                        self.homes.add(n[1])
                    elif n[0] == "pairs":
                        self.pairs.update(n[1])
                    else:
                        self.saves.add(n)
            else:
                raise Hole("the registers never settle")
            return self.write(text)
        except Hole as h:
            self.holes.append(str(h))
            return None

    def write(self, text):
        out = ["rule %s takes %d" % (self.name, self.params() + 1), "  instead"]
        for c in sorted(self.cells, reverse=True):
            out.append("  local %s bytes %d"
                       % (self.local_name(c),
                          max(8, 4 + self.records[c]) if c in self.records
                          else 8))
        for off, w in sorted(self.scalars.items(), reverse=True):
            if off == self.canary:
                continue
            kind = {1: "byte", 2: "half"}.get(w, "word")
            out.append("  local %s %s" % (self.local_name(off), kind))
        for r in sorted(self.homes):
            out.append("  local h_%s" % r)
        for s in sorted(self.saves):
            out.append("  local s%x" % s[1])
        for s in sorted(self.snaps):
            out.append("  local %s" % s)
        if getattr(self, "scratch", False):
            out.append("  local scratch")
        for k in sorted(self.kept):
            out.append("  local %s" % k)
        for k in sorted(self.packs):
            out.append("  local %s" % k)
        for name, off in sorted(self.vars_used.items()):
            out.append("  variable %s word %s" % (name, self.gmap(off, 0)))
        tail = []
        for k in self.matched_tags():
            tail.append(["place", "tm_%d" % k, "on", str(k)])
        if tail:
            tail.append(["match"])
        depth = 1
        for s, _idx in self.blocks + [(None, None)]:
            if s is None:
                for line in tail:
                    out.append("  " + " ".join(line))
                continue
            for line in text.get(s, []):
                if line[0] == "end":
                    depth -= 1
                out.append("  " * depth + " ".join(str(w) for w in line))
                if line[0] == "if":
                    depth += 1
        out.append("end")
        return "\n".join(out) + "\n"


class Need(Exception):
    pass


# ---- reports --------------------------------------------------------------


def census(path):
    m = Module(path)
    names = m.rules()
    clean = 0
    holes = collections.Counter()
    first = {}
    callees = collections.Counter()
    for name in names:
        r = Rule(m, name)
        text = r.lift(lambda off, w: "G%x" % off,
                      lambda addr, name, data: "S%x" % addr)
        if text is not None and not r.holes:
            clean += 1
            for c in r.callees:
                callees[c] += 1
        else:
            for h in r.holes:
                key = " ".join(h.split()[:4])
                holes[key] += 1
                first.setdefault(key, name)
    print("lift64: %s" % os.path.basename(path))
    print("rules: %d" % len(names))
    print("rules read completely: %d" % clean)
    print("rules with holes: %d" % (len(names) - clean))
    if holes:
        print()
        print("=== holes by shape ===")
        for k, n in holes.most_common(40):
            print("  %5d  %-50s first in %s" % (n, k, first[k]))
    return 0 if clean == len(names) else 1


def one(path, name):
    m = Module(path)
    r = Rule(m, name)
    text = r.lift(lambda off, w: "G%x" % off,
                  lambda addr, nm, data: "S%x" % addr)
    if text is None:
        print("%s: %s" % (name, "; ".join(r.holes)))
        return 1
    sys.stdout.write(text)
    return 0


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "census":
        return census(sys.argv[2])
    if len(sys.argv) >= 4 and sys.argv[1] == "rule":
        return one(sys.argv[2], sys.argv[3])
    print(__doc__.split("usage:")[1].strip())
    return 2


if __name__ == "__main__":
    sys.exit(main())
