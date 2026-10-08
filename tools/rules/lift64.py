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
        self.got_at = self.sections.get("__got", (0, 0))
        self.functions = collections.OrderedDict()
        self.at = {}
        name = None
        for line in dis.splitlines():
            m = HEAD.match(line)
            if m:
                name = m.group(2)
                self.functions[name] = []
                self.at[int(m.group(1), 16)] = name
                continue
            m = INSN.match(line)
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


def reg_of(text):
    return REG.get(text.lstrip("%"))


def reg_width(text):
    t = text.lstrip("%")
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
CALLER = ("rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11")
MEM = re.compile(r"^(-?0x[0-9a-f]+|-?\d+)?\((%[a-z0-9]+)(?:,(%[a-z0-9]+),(\d))?\)$")

# Values that can be named wherever they are read.
PURE = ("imm", "laddr", "gaddr", "sym", "state", "param", "code")

CONDS = {"je": "e", "jne": "ne", "jl": "l", "jge": "ge", "jg": "g",
         "jle": "le", "jb": "b", "jae": "ae", "ja": "a", "jbe": "be",
         "js": "s", "jns": "ns"}
WORDS = {"e": "is", "ne": "is not", "l": "is less than", "ge": "is at least",
         "g": "is more than", "le": "is at most", "b": "is below",
         "ae": "is not below", "a": "is above", "be": "is not above"}
MOVES = ("movq", "movl", "movw", "movb", "movzwl", "movzbl", "movswl",
         "movsbl", "movslq", "movzbw", "movsbw", "movzwq", "movzbq")
WIDTH_OF = {"movq": 8, "movl": 4, "movw": 2, "movb": 1, "movzwl": 2,
            "movzbl": 1, "movswl": 2, "movsbl": 1, "movslq": 4, "movzbw": 1,
            "movsbw": 1, "movzwq": 2, "movzbq": 1}
# The entries of the runtime that are handed a place in one of the language's
# lists, which list, and which argument besides the state it is.
LISTED = {"setd_lookup": ("set", 1), "actd_lookup": ("action", 0)}

SIGNED_OF = {"movswl": True, "movsbl": True, "movslq": True, "movsbw": True}

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
    for name in list(out):
        sig = es._read().get(name)
        if sig is not None and sig[1]:
            out[name] = len(sig[1]) - 1
    return out


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
        self.ins = module.functions[name]
        self.by_addr = {a: i for i, (a, _m, _o, _n) in enumerate(self.ins)}
        self.lo = self.ins[0][0]
        self.hi = self.ins[-1][0] + 16
        self.holes = []
        self.tables = {}

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
        count = None
        for j in range(i - 1, max(i - 8, -1), -1):
            _a, m, o, _n = self.ins[j]
            if m == "addq" and len(o) == 2 and reg_of(o[1]) == "rax":
                base_reg = reg_of(o[0])
            if m == "cmpl" and o[0].startswith("$") and \
                    reg_of(o[1]) == "rax":
                count = int(o[0].lstrip("$"), 0) + 1
                break
        if base_reg is None or count is None:
            return None
        bases = set()
        for _a, m, o, n in self.ins:
            if m == "leaq" and len(o) == 2 and reg_of(o[1]) == base_reg \
                    and "(%rip)" in o[0] and n and self.lo <= n[0] < self.hi:
                bases.add(n[0])
        if len(bases) != 1:
            return None
        base = bases.pop()
        targets = [base + self.m.int32(base + 4 * k) for k in range(count)]
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
            if m == "movq" and o and o[0] == "0x68(%rbx)":
                leaders.add(a)
        # The way out starts where vretproc is handed the state, so that
        # whatever a rule does on its way there is ordinary code.
        for a in seen:
            i = self.by_addr[a]
            _a, m, o, _n = self.ins[i]
            if m == "callq" and o and o[0].endswith("<_vretproc>"):
                j = i - 1 if i and self.ins[i - 1][1] == "movq" and \
                    self.ins[i - 1][2] == ["%rbx", "%rdi"] else i
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
        if base == "rbx":
            return ("glob", off)
        if base == "rbp":
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
            if first[1] == "movq" and first[2] and first[2][0] == "0x68(%rbx)":
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
               "xorl") + tuple(CONDS)

    def read_dispatch(self, head):
        starts = {b[0]: b[1] for b in self.blocks}
        hidx = starts[head]
        self.tag_reg = reg_of(self.ins[hidx[0]][2][1])
        # The two ways the number arrives: the fast path clears 0x68 and
        # jumps on; the slow one calls vback and falls through. Both reach
        # the decoder at the same place.
        succ = self.succ[head]
        fast = slow = None
        for t in succ:
            ms = [self.ins[i][1:3] for i in starts[t]]
            if any(m == "callq" and o[0].endswith("<_vback>") for m, o in ms):
                slow = t
            elif any(m == "andq" and o == ["$0x0", "0x68(%rbx)"] for m, o in ms):
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
        seen = set()
        for k in range(1, max(top, 2) + 1):
            try:
                t, visited, facts = self.decode(decoder, k)
            except Hole:
                continue
            seen |= visited
            if t in self.exits:
                self.exit_tags.setdefault(t, []).append(k)
                continue
            if t in self.exit_tail or self.gives_up(t):
                continue
            if self.tag_facts.setdefault(t, facts) != facts:
                raise Hole("a place the dispatch enters two ways")
            self.tags.setdefault(t, []).append(k)
        if not self.tags and not self.exit_tags:
            raise Hole("a dispatch that goes nowhere")
        self.dispatch_rest |= seen

    def gives_up(self, t):
        """A block that only moves registers on its way into the way out."""
        idx = dict(self.blocks).get(t)
        if idx is None:
            return False
        for i in idx:
            _a, m, o, _n = self.ins[i]
            if m in ("movl", "movq") and o[0].startswith("%") and o[1].startswith("%"):
                continue
            if m in ("pushq", "popq") or (m == "xorl" and o[0] == o[1]):
                continue
            return False
        last = self.ins[idx[-1]]
        nxt = self.ins[idx[-1] + 1][0] if idx[-1] + 1 < len(self.ins) else None
        return nxt in self.exits

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
        # of the places reached through it.
        facts = {}
        def done(a):
            return a, visited, {r: v for r, v in facts.items()
                                if r not in CALLER and r != self.tag_reg}
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
                ok = all(self.ins[j][1] in self.DECODER for j in
                         dict(self.blocks)[blk])
                if not ok:
                    return done(a)
                visited.add(blk)
            nxt = self.ins[i + 1][0] if i + 1 < len(self.ins) else None
            def val(t):
                if t.startswith("$"):
                    return int(t.lstrip("$"), 0) & 0xffffffff
                return regs.get(reg_of(t))
            if m in ("movl", "movq") and o[0].startswith("%") and o[1].startswith("%"):
                regs[reg_of(o[1])] = val(o[0])
            elif m in ("movb", "movl", "movq") and o[0].startswith("$") and \
                    o[1].startswith("%"):
                # A byte is taken for the whole register: every rule seen
                # doing this had cleared it or held a small count in it.
                regs[reg_of(o[1])] = facts[reg_of(o[1])] = val(o[0])
            elif m == "movb":
                return done(a)
            elif m in ("addl", "subl", "andl"):
                x, y = val(o[0]), val(o[1])
                if x is None or y is None:
                    return done(a)
                regs[reg_of(o[1])] = {"addl": y + x, "subl": y - x,
                                      "andl": y & x}[m] & 0xffffffff
            elif m in ("decl", "incl"):
                y = val(o[0])
                if y is None:
                    return done(a)
                regs[reg_of(o[0])] = (y + (1 if m == "incl" else -1)) & 0xffffffff
            elif m == "xorl" and o[0] == o[1]:
                regs[reg_of(o[1])] = 0
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
            elif m == "leaq":
                pass
            elif m == "movslq":
                mm = re.match(r"^\((%[a-z0-9]+),(%[a-z0-9]+),4\)$", o[0])
                if not mm:
                    return done(a)
                regs[reg_of(o[1])] = ("index", regs.get(reg_of(mm.group(2))))
            elif m == "addq":
                pass
            elif m == "jmpq":
                tab = self.tables.get(a)
                idx = regs.get("rax")
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

    def locals_used(self):
        """Which places in the frame the rule uses, and as what. A place
        whose address goes anywhere is a cell, which is sixteen bytes here
        and eight in our machine: the kind at nought, the field at two and
        the value eight bytes in, where ours keeps it four in."""
        self.cells = set()
        self.scalars = {}
        for a in self.reach:
            _a, m, o, _n = self.ins[self.by_addr[a]]
            for t in o:
                mm = MEM.match(t)
                if not mm or mm.group(2) != "%rbp":
                    continue
                off = int(mm.group(1), 0) if mm.group(1) else 0
                if m == "leaq":
                    self.cells.add(off)
        for a in self.reach:
            _a, m, o, _n = self.ins[self.by_addr[a]]
            if m == "leaq":
                continue
            for t in o:
                mm = MEM.match(t)
                if not mm or mm.group(2) != "%rbp":
                    continue
                off = int(mm.group(1), 0) if mm.group(1) else 0
                if any(c <= off < c + 16 for c in self.cells):
                    continue
                w = WIDTH_OF.get(m, 8 if m.endswith("q") else
                                 4 if m.endswith("l") else
                                 2 if m.endswith("w") else 1)
                self.scalars[off] = max(self.scalars.get(off, 0), w)

    # -- names -----------------------------------------------------------

    def local_name(self, off):
        return "c%x" % -off if off in self.cells else "v%x" % -off

    def local_place(self, off, width):
        """A place in the frame, as the upper form says it."""
        if off in self.frame_locals:
            raise Hole("the rule reads the machine's own frame at %d" % off)
        for c in self.cells:
            if c <= off < c + 16:
                inner = off - c
                if inner == 0 and width == 4:
                    return [self.local_name(c)]
                part = {0: "kind", 2: "field", 8: "value"}.get(inner)
                if part is None or (part != "value" and width != 2):
                    raise Hole("a cell read at %d wide %d" % (inner, width))
                return ["cell", self.local_name(c), part]
        return [self.local_name(off)]

    def glob_words(self, off, width):
        self.globals_used.add((off, width))
        return ["global", {1: "byte", 2: "half"}.get(width, "word"),
                self.gmap(off, width)]

    def sym_words(self, addr):
        name, data = self.m.data_symbol(addr)
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
            raise Hole("a read through a pointer")
        if k == "ans":
            if st["said"] == v[2]:
                return ["answer"]
            raise Need(("save", v[1]))
        if k == "home":
            return ["h_" + v[1]]
        if k == "unwindv":
            return ["unwind"]
        if k == "snap":
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
            if m in CONDS or m.startswith("cmov") or m.startswith("set"):
                return False
            if m in ("callq", "jmp", "jmpq", "retq") or \
                    m.startswith("cmp") or m.startswith("test"):
                return True
            _r, w = self.regs_read(i)
            if r in w:
                return True
            i += 1
        return True

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
        if r in self.homes and v[0] == "cc":
            test = self.cond_words(dict(st, cond=v[1]), v[2])
            self.say(st, lines, "set", "h_" + r, "to", "0")
            self.say(st, lines, "if", *test)
            self.say(st, lines, "set", "h_" + r, "to", "1")
            self.say(st, lines, "end")
            st["regs"][r] = ("home", r, None)
            return
        if r in self.homes and not framed and \
                v[0] not in ("got", "canary", "frame", "code", "sp"):
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
        if a in self.pop_skip:
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

        if m in MOVES:
            return self.move(b, st, lines, a, m, o, note)

        if m == "andq" and o[0] == "$0x0" and not o[1].startswith("%"):
            return self.store(b, st, lines, a, self.addr_of(st, o[1]), 8,
                              ("imm", 0))

        if m in ("testl", "testq", "testw", "testb") and o[0] == o[1]:
            v = self.value_of(st, o[0])
            st["cond"] = ("frame",) if v[0] == "frame" else \
                ("test", self.snap(st, lines, v, a, 0), reg_of(o[0]))
            return
        if m in ("cmpl", "cmpq", "cmpw", "cmpb"):
            w = {"l": 4, "q": 8, "w": 2, "b": 1}[m[-1]]
            src = self.value_of(st, o[0], w)
            dst = self.value_of(st, o[1], w)
            if dst[0] == "canary" or src[0] == "canary":
                st["cond"] = ("frame",)
                return
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

    def move(self, b, st, lines, a, m, o, note):
        src, dst = o
        w = WIDTH_OF[m]
        signed = SIGNED_OF.get(m, False)
        if src.endswith("(%rip)"):
            if note and self.m.got_at[0] <= note[0] < self.m.got_at[1]:
                self.put_reg(st, lines, reg_of(dst), ("got", note[0]))
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
                elif p[0] == "local" and p[1] in st["slots"]:
                    v = st["slots"][p[1]]
                else:
                    v = ("mem", p, w, signed, a)
            if v[0] == "mem" and ("save", a) in self.saves:
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
        if p[0] == "stackarg":
            st["stackargs"][p[1]] = v
            return
        if p[0] == "glob" and p[1] < 0x148:
            return self.machine_store(b, st, lines, a, p[1], w, v)
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
        elif p[0] == "through" and p[1][0] == "param":
            inner = {0: 0, 2: 2, 8: 4}.get(p[2])
            if inner is None:
                raise Hole("a write %d into a caller's cell" % p[2])
            self.say(st, lines, "put", *self.words(v, st, reg_of(src or "")),
                     "into", "arg", str(p[1][1]), "at", str(inner))
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
        else:
            raise Hole("a write to the machine at 0x%x" % off)
        if "lnode" in inl and inl.get("loff") and inl.get("lflag"):
            self.inline_load(st, lines, "lpta_loadp", inl.pop("lnode"))
            inl.pop("loff"); inl.pop("lflag")
        if "rnode" in inl and inl.get("rflag") and inl.get("loff"):
            self.inline_load(st, lines, "rpta_loadp", inl.pop("rnode"))
            inl.pop("rflag"); inl.pop("loff")

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
        self.after_call(st, None)

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
                return
            if r not in self.homes or v[0] != "home":
                raise Need(("home", r))
            self.arith_on(st, lines, op, ["h_" + r], src)
            st["regs"][r] = ("home", r, None)
            st["fresh"].add(r)
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
        else:
            raise Hole("arithmetic on %s" % (p,))
        self.arith_on(st, lines, op, place, src)
        self.forget_memory(st, p)

    def arith_on(self, st, lines, op, place, src):
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
            v = self.value_of(st, src)
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
            if name != "fence" or n != 2:
                raise Hole("a private %s with %d arguments" % (name, n))
        if regs.get("rdi") != ("state",):
            raise Hole("%s is not handed the state" % name)
        shape = SHAPE.get(name)
        n = shape[0] if shape else self.arity_of(name)
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
            self.say(st, lines, "call", name, *flat)
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
        st["pushes"] = []
        st["stackargs"] = {}
        self.forget_memory(st)

    def cond_words(self, st, cc):
        c = st["cond"]
        if c is None:
            raise Hole("a branch on nothing tested")
        if c[0] == "test":
            if cc not in ("e", "ne"):
                raise Hole("a test read as %s" % cc)
            return self.words(c[1], st, c[2]) + \
                (["is", "0"] if cc == "e" else ["is", "not", "0"])
        if c[0] == "cmp":
            if cc not in WORDS:
                raise Hole("a comparison read as %s" % cc)
            return self.words(c[1], st, c[3]) + WORDS[cc].split() + \
                self.words(c[2], st, c[4])
        raise Hole("a branch on %s" % c[0])

    # -- a block at a time -----------------------------------------------

    def clone(self, st):
        return {"regs": dict(st["regs"]), "fresh": set(st["fresh"]),
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
            if x == y and x[0] != "X":
                regs[r] = x
            elif x[0] == "home" and y[0] == "home":
                regs[r] = ("home", r, None)
            else:
                regs[r] = ("X",)
        slots = {k: v for k, v in a["slots"].items()
                 if b["slots"].get(k) == v}
        return {"regs": regs, "fresh": a["fresh"] & b["fresh"],
                "pushes": a["pushes"] if a["pushes"] == b["pushes"] else [],
                "cond": a["cond"] if a["cond"] == b["cond"] else None,
                "said": a["said"], "inline": {}, "slots": slots,
                "begun": True, "stackargs": {}}

    def edge(self, st, t):
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
            if ans[0] == "imm":
                v = ans
            else:
                st = getattr(self, "dispatch_state", None)
                v = st["regs"].get(ans[1], ("X",)) if st else ("X",)
                if v[0] == "home" and v[2] is not None:
                    v = v[2]
            if v == ("imm", 0):
                out += ks
            elif v != ("imm", 94):
                raise Hole("a number that leads to a way out answering %s" % (v,))
        return sorted(out)

    def do_block(self, b, st, lines):
        idx = self.block_idx[b]
        st["said"] = (b, 0)
        if b in self.exits or b in self.exit_tail:
            return []
        if b in self.dispatch_rest:
            return []
        if b in self.dispatch_head:
            self.dispatch_state = self.clone(st)
            s = self.clone(st)
            for r in CALLER:
                s["regs"][r] = ("X",)
            if self.depth_reg:
                s["regs"][self.depth_reg] = ("home", self.depth_reg, None) \
                    if self.depth_reg in self.homes else ("X",)
            s["fresh"] = set()
            s["pushes"] = []
            s["cond"] = None
            outs = []
            for t in self.tags:
                st_t = self.clone(s)
                for r, v in self.tag_facts.get(t, {}).items():
                    st_t["regs"][r] = ("home", r, ("imm", v)) \
                        if r in self.homes else ("imm", v)
                outs.append((t, st_t))
            return outs

        facts = self.tag_facts.get(b) if b in self.tags else None
        if facts:
            # What the decoder set on the way is only so when the rule comes
            # back here, so it is said where only coming back reaches.
            self.say(st, lines, "go", "to", "p%x" % b)
            for k in self.tags[b]:
                lines.append(["place", "t%x_%d" % (b, k), "on", str(k)])
            if self.depth_reg in self.homes:
                self.say(st, lines, "set", "h_" + self.depth_reg, "to", "unwind")
            for r, v in sorted(facts.items()):
                if r in self.homes:
                    self.say(st, lines, "set", "h_" + r, "to", str(v))
            lines.append(["place", "p%x" % b])
        else:
            lines.append(["place", "p%x" % b])
            for k in self.tags.get(b, []):
                lines.append(["place", "t%x_%d" % (b, k), "on", str(k)])
            if b in self.tags and self.depth_reg in self.homes:
                self.say(st, lines, "set", "h_" + self.depth_reg, "to", "unwind")
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
            for line in self.edge(st, t):
                self.say(st, lines, *line)
            return [(t, st)]
        if m == "retq":
            raise Hole("a return that is not the way out")
        for line in self.fall(st, nxt):
            self.say(st, lines, *line)
        return [(nxt, st)]

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
                regs[reg_of(o[1])] = regs.get(reg_of(o[0]), ("X", reg_of(o[0])))
            elif m in ("movl", "movq") and o[0].startswith("$"):
                regs[reg_of(o[1])] = ("imm", int(o[0].lstrip("$"), 0))
            elif m == "movq" and reg_of(o[1]) == "rdi":
                pass
            else:
                raise Hole("a dispatch that does %s before vback" % m)
        v = regs.get("rsi", ("X", "rsi"))
        if v[0] == "home" and v[1] == self.depth_reg:
            return ("unwindv",)
        return v

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
        if t in self.exits or t in self.dispatch_head or t in self.dispatch_rest:
            return self.edge(st, t)
        return []

    def run(self):
        self.block_idx = {s: idx for s, idx in self.blocks}
        order = [s for s, _idx in self.blocks]
        ins = {self.lo: self.entry_state()}
        text = {}
        needs = set()
        for _round in range(80):
            changed = False
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
            for k, t in enumerate(o):
                r = reg_of(t) if t.startswith("%") else None
                if r in ARGS[1:] and r not in seen:
                    if k == 0 or m.startswith("cmp") or m.startswith("test"):
                        n = max(n, ARGS.index(r))
                    seen.add(r)
            if len(o) == 2 and o[1].startswith("%"):
                seen.add(reg_of(o[1]))
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
        try:
            self.walk()
            if self.holes:
                return None
            self.classify()
            self.locals_used()
            self.frame_locals = set()
            self.find_frame()
            self.homes = set()
            self.saves = set()
            self.snaps = set()
            for _ in range(40):
                text, needs = self.run()
                if not needs:
                    break
                new = {n for n in needs if not (
                    (n[0] == "home" and n[1] in self.homes) or n in self.saves)}
                if not new:
                    raise Hole("a value that still cannot be named: %s"
                               % sorted(needs)[0][0])
                for n in new:
                    if n[0] == "home":
                        if n[1] is None or n[1] in ("rbx", "rsp", "rbp"):
                            raise Hole("a register that cannot have a home")
                        self.homes.add(n[1])
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
            out.append("  local %s bytes 8" % self.local_name(c))
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
