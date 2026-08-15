#!/usr/bin/env python3
"""Lift the generated Delta rules out of a language module.

A rule is one compiled function: a landing pad, an activation record, then a
flat run of calls to the runtime with a little inline arithmetic between them,
and a jump table at the end that the backtracker dispatches through. All of
that is recoverable, so this reads it rather than reimplementing it.

Nothing is guessed. Every instruction in every rule has to fall into one of the
shapes below or it is counted as a hole and named.
"""

import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

RULE_OBJECTS = ("ea_", "ed_", "es_", "et_", "ut_")

CONDS = ("je", "jne", "jz", "jnz", "jl", "jle", "jg", "jge", "ja", "jae",
         "jb", "jbe", "js", "jns", "jo", "jp")

ALU = ("addl", "addw", "addb", "subl", "subw", "subb", "andl", "andw", "andb",
       "orl", "orw", "orb", "xorl", "xorw", "xorb", "adcl", "adcb", "sbbl",
       "sbbb", "shll", "shlw", "sarl", "sarw", "imull", "imulw", "negl",
       "negw", "incl", "incw", "decl", "decw", "notl", "xchgl")

CMP = ("cmpl", "cmpw", "cmpb", "testl", "testw", "testb")

MOV = ("movl", "movw", "movb", "movzbl", "movsbl", "movzwl", "movswl")

MEM = re.compile(r"^(-?0x[0-9a-f]+|-?\d+)?\((%[a-z]+)\)$")

INDEXED = re.compile(
    r"^(-?0x[0-9a-f]+|-?\d+)?\((%[a-z]+)?,\s*(%[a-z]+)(?:,\s*(\d))?\)$")


def split_operands(text):
    """The instruction's operands, with commas inside an address left alone."""
    parts = []
    depth = 0
    start = 0
    for i, ch in enumerate(text):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == "," and depth == 0:
            parts.append(text[start:i].strip())
            start = i + 1
    parts.append(text[start:].strip())
    return parts


def disassemble(obj):
    return subprocess.run(
        ["llvm-objdump", "-d", "-r", "--no-show-raw-insn", obj],
        check=True, capture_output=True, text=True).stdout


def read_functions(obj):
    """Every function in the object as (name, [item]).

    An item is (address, label, mnemonic, operands, [relocations]). A label
    item carries no instruction; it marks where a jump can land. The compiler
    gives each rule its own section, so a symbol at address zero starts a new
    function and everything after it belongs to that one.
    """
    result = []
    name = None
    items = []

    for line in disassemble(obj).splitlines():
        m = re.match(r"^([0-9a-f]+) <(.+)>:$", line)
        if m:
            addr, label = int(m.group(1), 16), m.group(2)
            if addr == 0 and label.startswith("_"):
                if name is not None:
                    result.append((name, items))
                name, items = label[1:], []
            items.append((addr, label, None, None, []))
            continue

        m = re.search(r"IMAGE_REL_I386_(\S+)\s+(\S+)$", line)
        if m and items:
            items[-1][4].append((m.group(1), m.group(2)))
            continue

        m = re.match(r"^\s+([0-9a-f]+):\s+(\S+)\s*(.*?)(?:\s+#.*)?$", line)
        if m:
            items.append((int(m.group(1), 16), None, m.group(2),
                          m.group(3).strip(), []))

    if name is not None:
        result.append((name, items))
    return result


def find_data(items):
    """The regions of a rule that are data rather than code.

    Two kinds sit inside the code: the jump table the backtracker dispatches
    through, and the byte table that maps a tag onto an entry in it. Both are
    named by a full-width relocation on a real instruction, so the regions
    fall out by working outward from the code until nothing new is named.
    Disassembling either produces nonsense, so they are lifted out first.
    """
    regions = []
    label = None
    body = []

    for addr, lab, mnem, ops, relocs in items:
        if lab is not None:
            if label is not None:
                regions.append((label, body))
            label, body = lab, []
            continue
        body.append((addr, mnem, ops, relocs))
    if label is not None:
        regions.append((label, body))

    # The tables sit behind the code that names them, so one pass in
    # address order is enough: a region already named as a table is not read
    # for names of its own, which is what keeps a jump table's entries from
    # being mistaken for tables themselves.
    data = set()
    for lab, body in regions:
        if lab in data:
            continue
        for _addr, mnem, ops, relocs in body:
            names = mnem in MOV or (mnem in ("jmp", "jmpl")
                                    and ops.startswith("*"))
            if not names:
                continue
            for kind, target in relocs:
                if kind == "DIR32" and target.startswith("$"):
                    data.add(target)

    tables = {}
    for lab, body in regions:
        if lab not in data:
            continue
        entries = [t for _a, _m, _o, relocs in body
                   for k, t in relocs if k == "DIR32"]
        if entries:
            tables[lab] = entries

    return data, tables


class Decoder:
    """One rule, turned into blocks of operations.

    The state pointer and the tag counter live in callee-saved registers for
    the whole body, so registers are tracked across the function and only the
    caller-saved ones are dropped at a call.
    """

    def __init__(self, name, items, data, tables):
        self.name = name
        self.items = items
        self.data = data
        self.tables = tables
        self.holes = []
        self.blocks = []
        self.regs = {}
        # The argument area as the compiler sees it: every slot that is
        # physically on the stack, and whether it was written for the call
        # being set up or left behind by an earlier one.
        self.stack = []
        self.saved = []
        self.frame = 0
        self.params = 0
        self.pbase = 8
        self.ops = []
        self.labels = {}
        self.addr = 0

    def hole(self, what):
        self.holes.append(what)

    def mem(self, text):
        """An address the rule can name, or None if it is not one."""
        m = MEM.match(text)
        if not m:
            return None
        off = int(m.group(1), 0) if m.group(1) else 0
        base = m.group(2)
        if base == "%esp":
            # A slot of the argument area. The compiler writes over a slot a
            # previous call left behind rather than pushing again.
            n = len(self.stack) - 1 - off // 4
            return ("pending", n) if 0 <= n < len(self.stack) else None
        if base == "%ebp":
            if off >= self.pbase:
                self.params = max(self.params, (off - self.pbase) // 4 + 1)
                return ("param", (off - self.pbase) // 4)
            return ("slot", off)
        held = self.regs.get(base)
        if held is None:
            return None
        if held[0] == "state":
            return ("statefld", off)
        if held[0] == "slotaddr":
            return ("slot", held[1] + off)
        if held[0] in ("param", "slot", "statefld", "arg"):
            return ("indirect", held, off)
        if held[0] == "loaded":
            return ("indirect", held[1], off)
        return None

    def value(self, text):
        """A source operand: a constant, a register's contents, or memory."""
        if text.startswith("$"):
            return ("imm", int(text.lstrip("$"), 0))
        if text.startswith("%"):
            return self.regs.get(text, ("reg", text))
        return self.mem(text)

    def emit(self, op):
        self.ops.append((self.addr, op))

    def start_block(self, label, addr=0):
        self.labels[addr] = label
        for r in ("%eax", "%ecx", "%edx"):
            self.regs.pop(r, None)

    def prologue(self, i):
        """Read past the frame setup and work out where the arguments are.

        A small frame gets the textbook prologue and the arguments start
        eight bytes above the base pointer. A large one has the base pointer
        planted part way down the frame instead, so the arguments start that
        much further up and locals sit on both sides of it.
        """
        it = self.items
        if it[i][2] in ("pushl", "push") and it[i][3] == "%ebp":
            nxt = it[i + 1]
            if nxt[2] in ("movl", "mov") and nxt[3] == "%esp, %ebp":
                self.pbase = 8
                i += 2
            else:
                m = re.match(r"^-(0x[0-9a-f]+|\d+)\(%esp\), %ebp$",
                             nxt[3] or "")
                if nxt[2] in ("leal", "lea") and m:
                    self.pbase = int(m.group(1), 0) + 8
                    i += 2
        if it[i][2] in ("subl", "sub") and it[i][3].endswith("%esp"):
            self.frame = int(it[i][3].split(",")[0].lstrip("$"), 0)
            i += 1
        return i

    def run(self):
        it = self.items
        i = 1 if it and it[0][1] is not None else 0
        self.start_block(it[0][1] if i else self.name, 0)
        i = self.prologue(i)

        while i < len(it):
            addr, label, mnem, ops, relocs = it[i]
            i += 1

            if label is not None:
                if label in self.data:
                    # Data, not code: skip to the next label.
                    while i < len(it) and it[i][1] is None:
                        i += 1
                    continue
                self.start_block(label, addr)
                continue

            reloc = relocs[0][1] if relocs else None
            self.addr = addr
            try:
                i = self.step(i, mnem, ops, reloc)
            except (ValueError, IndexError):
                self.hole("%s %s" % (mnem, ops))

        self.build_blocks()
        return self

    def step(self, i, mnem, ops, reloc):
        it = self.items

        if mnem in ("pushl", "push"):
            # A push and an immediate pop is how the compiler writes a small
            # constant into a register.
            if i < len(it) and it[i][2] in ("popl", "pop") \
                    and it[i][3].startswith("%") and ops.startswith("$"):
                self.regs[it[i][3]] = ("imm", int(ops.lstrip("$"), 0))
                return i + 1
            if reloc:
                self.stack.append([("sym", reloc.lstrip("_")), True])
                return i
            if ops in ("%ebx", "%esi", "%edi") and ops not in self.regs \
                    and not self.stack:
                self.saved.append(ops)
                return i
            v = self.value(ops)
            if v is None:
                self.hole("push %s" % ops)
                v = ("?", ops)
            self.stack.append([v, True])
            return i

        if mnem in ("popl", "pop"):
            if ops in ("%ebx", "%esi", "%edi", "%ebp") and not self.stack:
                return i
            if self.stack:
                self.stack.pop()
            return i

        if mnem in ("leal", "lea") and "," in ops:
            parts = split_operands(ops)
            if len(parts) != 2:
                self.hole("lea %s" % ops)
                return i
            src, dst = parts
            if not dst.startswith("%"):
                self.hole("lea into %s" % dst)
                return i
            m2 = INDEXED.match(src)
            if m2:
                # Scaled address arithmetic: the compiler's way of writing a
                # multiplication by a small constant.
                disp = int(m2.group(1), 0) if m2.group(1) else 0
                base = self.regs.get(m2.group(2)) if m2.group(2) else None
                index = self.regs.get(m2.group(3))
                self.regs[dst] = ("computed",)
                scale = int(m2.group(4)) if m2.group(4) else 1
                self.emit(("scale", disp, base, index, scale, dst))
                return i
            m = MEM.match(src)
            if m:
                off = int(m.group(1), 0) if m.group(1) else 0
                base = m.group(2)
                if base == "%ebp":
                    self.regs[dst] = ("slotaddr", off)
                    if off >= self.pbase:
                        n = (off - self.pbase) // 4
                        self.params = max(self.params, n + 1)
                        self.regs[dst] = ("paramaddr", n)
                    return i
                held = self.regs.get(base)
                if held and held[0] == "state":
                    self.regs[dst] = ("state", held[1] + off)
                    return i
                if held and held[0] == "slotaddr":
                    self.regs[dst] = ("slotaddr", held[1] + off)
                    return i
                if held and held[0] in ("loaded", "computed", "imm"):
                    # Not an address at all: adding a constant to a value
                    # without disturbing the flags.
                    self.regs[dst] = ("computed",)
                    self.emit(("addk", off, held, dst))
                    return i
            self.hole("lea %s" % src)
            self.regs[dst] = ("?", src)
            return i

        if mnem in ("calll", "call"):
            # The arguments are the run of slots written for this call,
            # counted down from the top. Anything below was left behind.
            n = 0
            while n < len(self.stack) and self.stack[-1 - n][1]:
                n += 1
            args = [slot[0] for slot in self.stack[len(self.stack) - n:]]
            args.reverse()
            for slot in self.stack:
                slot[1] = False
            self.emit(("call", reloc.lstrip("_") if reloc else "?", args))
            for r in ("%eax", "%ecx", "%edx"):
                self.regs.pop(r, None)
            self.regs["%eax"] = ("result",)
            return i

        if mnem in ("addl", "add") and ops.endswith("%esp") \
                and ops.startswith("$"):
            n = int(ops.split(",")[0].lstrip("$"), 0) // 4
            del self.stack[len(self.stack) - n:]
            return i

        if mnem in MOV and "," in ops:
            return self.move(i, mnem, ops, reloc)

        if mnem in ("xorl", "xorw", "xorb") and "," in ops:
            src, dst = split_operands(ops)
            if src == dst and src.startswith("%"):
                self.regs[dst] = ("imm", 0)
                return i

        # Three operands: multiply a value by a constant into a register.
        if mnem in ("imull", "imulw") and ops.count(",") == 2:
            a, b, dst = split_operands(ops)
            va, vb = self.value(a), self.value(b)
            if va is None or vb is None or not dst.startswith("%"):
                self.hole("%s %s" % (mnem, ops))
                return i
            self.regs[dst] = ("computed",)
            self.emit(("mul", mnem, va, vb, dst))
            return i

        if mnem in ALU and "," in ops:
            src, dst = split_operands(ops)
            a, b = self.value(src), self.value(dst)
            if a is None or b is None:
                self.hole("%s %s" % (mnem, ops))
                return i
            if dst.startswith("%"):
                self.regs[dst] = self.fold(mnem, a, b)
            self.emit(("alu", mnem, a, b))
            return i

        if mnem in ALU:
            b = self.value(ops)
            if b is None:
                self.hole("%s %s" % (mnem, ops))
                return i
            if ops.startswith("%"):
                self.regs[ops] = self.fold(mnem, None, b)
            self.emit(("alu", mnem, None, b))
            return i

        if mnem in CMP and "," in ops:
            src, dst = split_operands(ops)
            a, b = self.value(src), self.value(dst)
            if a is None or b is None:
                self.hole("%s %s" % (mnem, ops))
                return i
            self.emit(("cmp", mnem, a, b))
            return i

        if mnem in ("cltd", "cwtl"):
            self.emit(("widen", mnem))
            return i

        if mnem in ("idivl", "divl") :
            a = self.value(ops)
            if a is None:
                self.hole("%s %s" % (mnem, ops))
                return i
            self.emit(("div", mnem, a))
            self.regs["%eax"] = ("computed",)
            self.regs["%edx"] = ("computed",)
            return i

        if mnem in CONDS:
            self.emit(("branch", mnem, self.target(ops)))
            return i

        if mnem in ("jmp", "jmpl") and not ops.startswith("*"):
            self.emit(("jump", self.target(ops)))
            return i

        if mnem in ("jmp", "jmpl") and ops.startswith("*"):
            # The backtracker's dispatch. The table is named by the
            # relocation on the instruction itself.
            table = None
            for _kind, target in it_relocs(self.items, i - 1):
                table = target
            if table is None or table not in self.tables:
                self.hole("dispatch with no table")
                return i
            self.emit(("switch", self.tables[table]))
            return i

        if mnem in ("retl", "ret"):
            self.emit(("return",))
            return i

        if mnem == "leave":
            return i

        if mnem in ("scasb", "repne") or mnem == "<unknown>":
            self.hole("%s %s" % (mnem, ops))
            return i

        self.hole("%s %s" % (mnem, ops))
        return i

    def move(self, i, mnem, ops, reloc=None):
        src, dst = split_operands(ops)

        # A read out of one of the rule's own tables: the byte map that turns
        # a backtrack tag into an entry in the jump table.
        if reloc and reloc in self.data and dst.startswith("%"):
            self.regs[dst] = ("computed",)
            self.emit(("map", reloc, self.value(src.split("(")[-1]
                                                .rstrip(")"))))
            return i

        if dst.startswith("%"):
            v = self.value(src)
            if v is None:
                self.hole("%s %s" % (mnem, ops))
                self.regs[dst] = ("?", src)
                return i
            if v[0] == "param" and v[1] == 0:
                # The rule's own first argument is the machine state, and it
                # stays in a register for the whole body.
                self.regs[dst] = ("state", 0)
                return i
            if src.startswith("%") or src.startswith("$"):
                self.regs[dst] = v
                return i
            self.regs[dst] = ("loaded", v, mnem)
            self.emit(("load", mnem, v, dst))
            return i

        where = self.mem(dst)
        if where is None:
            self.hole("%s %s" % (mnem, ops))
            return i
        v = self.value(src)
        if v is None:
            self.hole("%s %s" % (mnem, ops))
            return i
        if where[0] == "pending":
            self.stack[where[1]] = [v, True]
            return i
        self.emit(("store", mnem, v, where))
        return i

    def resolve(self, text):
        """A branch target as an address. The compiler names the nearest
        label and counts forward from it, so a jump can land in the middle
        of a block."""
        m = re.match(r"^(.+?)(?:\+(0x[0-9a-f]+|\d+))?$", text)
        label, off = m.group(1), int(m.group(2), 0) if m.group(2) else 0
        for addr, lab in self.labels.items():
            if lab == label:
                return addr + off
        return None

    def build_blocks(self):
        """Cut the operations into blocks at every address something jumps
        to, not only at the labels the compiler wrote."""
        leaders = set(self.labels)
        for _addr, op in self.ops:
            if op[0] == "branch":
                leaders.add(self.resolve(op[2]))
            elif op[0] == "jump":
                leaders.add(self.resolve(op[1]))
            elif op[0] == "switch":
                for entry in op[1]:
                    leaders.add(self.resolve(entry))
        leaders.discard(None)

        cuts = sorted(leaders)
        self.blocks = []
        current = None
        for addr, op in self.ops:
            while cuts and addr >= cuts[0]:
                start = cuts.pop(0)
                current = []
                self.blocks.append((self.labels.get(start,
                                                    "at_0x%x" % start),
                                    start, current))
            if current is None:
                current = []
                self.blocks.append((self.name, 0, current))
            current.append(op)

        # A landing place past the last operation is still a place a jump can
        # go: the tail the compiler shares between several failure paths.
        for start in cuts:
            self.blocks.append((self.labels.get(start, "at_0x%x" % start),
                                start, []))

        self.starts = {addr: name for name, addr, _ops in self.blocks}

    def fold(self, mnem, a, b):
        """What a register holds after an arithmetic step, when that is
        still knowable. The tag the backtracker dispatches on is counted up
        this way, so following it is what turns a tag into a constant."""
        if b is None or b[0] != "imm":
            return ("computed",)
        n = b[1]
        if mnem in ("incl", "incw"):
            return ("imm", n + 1)
        if mnem in ("decl", "decw"):
            return ("imm", n - 1)
        if a is not None and a[0] == "imm":
            k = a[1]
            if mnem in ("addl", "addw", "addb"):
                return ("imm", n + k)
            if mnem in ("subl", "subw", "subb"):
                return ("imm", n - k)
            if mnem in ("andl", "andw", "andb"):
                return ("imm", n & k)
            if mnem in ("orl", "orw", "orb"):
                return ("imm", n | k)
            if mnem in ("shll", "shlw"):
                return ("imm", n << k)
        return ("computed",)

    def target(self, ops):
        m = re.search(r"<([^>]+)>", ops)
        if not m:
            return ops
        return m.group(1)


def it_relocs(items, index):
    return items[index][4]


def show_operand(op):
    if op is None:
        return "-"
    kind = op[0]
    if kind == "imm":
        return str(op[1])
    if kind == "sym":
        return op[1]
    if kind == "param":
        return "arg%d" % op[1]
    if kind == "paramaddr":
        return "&arg%d" % op[1]
    if kind == "slot":
        return "loc%+d" % op[1]
    if kind == "slotaddr":
        return "&loc%+d" % op[1]
    if kind == "state":
        return "state" if not op[1] else "state+0x%x" % op[1]
    if kind == "statefld":
        return "var+0x%x" % op[1]
    if kind == "indirect":
        return "[%s]%+d" % (show_operand(op[1]), op[2])
    if kind == "loaded":
        return show_operand(op[1])
    if kind == "result":
        return "result"
    if kind == "computed":
        return "computed"
    return "?" + str(op[1:])


def show(op):
    kind = op[0]
    if kind == "call":
        return "%s(%s)" % (op[1], ", ".join(show_operand(a) for a in op[2]))
    if kind == "branch":
        return "%s -> %s" % (op[1], op[2])
    if kind == "jump":
        return "goto %s" % op[1]
    if kind == "cmp":
        return "%s %s, %s" % (op[1], show_operand(op[2]), show_operand(op[3]))
    if kind == "alu":
        return "%s %s, %s" % (op[1], show_operand(op[2]), show_operand(op[3]))
    if kind == "load":
        return "%s %s -> %s" % (op[1], show_operand(op[2]), op[3])
    if kind == "store":
        return "%s %s -> %s" % (op[1], show_operand(op[2]), show_operand(op[3]))
    if kind == "switch":
        return "switch over %d entries" % len(op[1])
    if kind == "map":
        return "index the %s table by %s" % (op[1], show_operand(op[2]))
    if kind == "return":
        return "return"
    if kind == "scale":
        return "scale %s by %d plus %s and %d -> %s" \
            % (show_operand(op[3]), op[4], show_operand(op[2]), op[1], op[5])
    if kind == "addk":
        return "%s plus %d -> %s" % (show_operand(op[2]), op[1], op[3])
    if kind == "mul":
        return "%s times %s -> %s" \
            % (show_operand(op[2]), show_operand(op[3]), op[4])
    if kind == "div":
        return "divide by %s" % show_operand(op[2])
    if kind == "widen":
        return op[1]
    return str(op)


def dump(where, wanted):
    for obj in sorted(f for f in os.listdir(where)
                      if f.endswith(".obj") and f.startswith(RULE_OBJECTS)):
        for name, items in read_functions(os.path.join(where, obj)):
            if name != wanted:
                continue
            data, tables = find_data(items)
            d = Decoder(name, items, data, tables).run()
            print("%s in %s: %d arguments, %d bytes of frame"
                  % (name, obj, d.params, d.frame))
            for label, _start, block in d.blocks:
                print("  %s:" % label)
                for op in block:
                    print("    %s" % show(op))
            if d.holes:
                print("  holes: %s" % "; ".join(d.holes))
            return 0
    print("no rule called %s" % wanted)
    return 1


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    where = argv[0] if argv else os.path.join(ROOT, "analysis", "enus")

    for a in sys.argv[1:]:
        if a.startswith("--rule="):
            return dump(where, a.split("=", 1)[1])

    objects = sorted(f for f in os.listdir(where)
                     if f.endswith(".obj") and f.startswith(RULE_OBJECTS))

    total = 0
    clean = 0
    holes = collections.Counter()
    examples = {}
    calls = collections.Counter()
    ops = collections.Counter()
    dangling = 0
    blocks = 0

    for obj in objects:
        for name, items in read_functions(os.path.join(where, obj)):
            data, tables = find_data(items)
            d = Decoder(name, items, data, tables).run()
            total += 1
            if d.holes:
                for h in d.holes:
                    key = h.split()[0]
                    holes[key] += 1
                    examples.setdefault(key, (obj, name, h))
            else:
                clean += 1
            blocks += len(d.blocks)
            for _label, _start, block in d.blocks:
                for op in block:
                    targets = []
                    if op[0] == "branch":
                        targets = [op[2]]
                    elif op[0] == "jump":
                        targets = [op[1]]
                    elif op[0] == "switch":
                        targets = op[1]
                    for t in targets:
                        if d.resolve(t) not in d.starts:
                            dangling += 1
                    ops[op[0]] += 1
                    if op[0] == "call":
                        calls[op[1]] += 1

    print("delta-lift: reading %s" % os.path.basename(where))
    print("rules: %d" % total)
    print("rules the decoder accounted for completely: %d" % clean)
    print("rules with holes: %d" % (total - clean))
    print("distinct runtime entry points they call: %d" % len(calls))
    print("blocks: %d" % blocks)
    print("jumps that do not land on a block: %d" % dangling)
    print()
    print("=== operations ===")
    for kind, n in ops.most_common():
        print("  %-10s %d" % (kind, n))

    if holes:
        print()
        print("=== holes by shape ===")
        for kind, n in holes.most_common(20):
            obj, name, text = examples[kind]
            print("  %-12s %6d   first in %s %s: %s"
                  % (kind, n, obj, name, text))

    return 1 if holes else 0


if __name__ == "__main__":
    sys.exit(main())
