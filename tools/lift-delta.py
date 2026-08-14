#!/usr/bin/env python3
"""Read a generated Delta rule object and account for every instruction.

The Delta compiler emits threaded calls: push a few constants, call a runtime
primitive, branch on what it returns. If that model is right then a handful of
instruction shapes explain the whole object, and anything left over is a hole
in the model rather than a hard case. This reports the coverage so the hole
list can be worked down.

It does not emit code yet. Knowing what the model misses comes first.
"""

import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def disassemble(obj):
    out = subprocess.run(
        ["llvm-objdump", "-d", "-r", "--no-show-raw-insn", obj],
        check=True, capture_output=True, text=True).stdout

    functions = []
    current = None
    pending_reloc = None

    for line in out.splitlines():
        if line.startswith("Disassembly of section"):
            current = {"name": None, "insns": []}
            functions.append(current)
            continue
        if current is None:
            continue

        m = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
        if m:
            if current["name"] is None:
                current["name"] = m.group(1)
            else:
                current["insns"].append(("label", m.group(1), None))
            continue

        # Relocations are printed the same shape as instructions, so they have
        # to be recognised first or they get counted as code.
        m = re.search(r"IMAGE_REL_I386_\S+\s+(\S+)$", line)
        if m:
            if current["insns"]:
                kind, mnem, ops = current["insns"][-1]
                current["insns"][-1] = (kind, mnem,
                                        (ops or "") + "  ->" + m.group(1))
            continue

        m = re.match(r"^\s+[0-9a-f]+:\s+(\S+)\s*(.*)$", line)
        if m:
            current["insns"].append(("insn", m.group(1), m.group(2).strip()))
            continue

    # MSVC puts a switch's jump table after the function body, and the
    # disassembler decodes those addresses as instructions. Everything past
    # the last return is data, so cut there and count it separately.
    for fn in functions:
        last = -1
        for i, (kind, mnem, _ops) in enumerate(fn["insns"]):
            if kind == "insn" and mnem in ("retl", "ret"):
                last = i
        fn["data"] = 0
        if last >= 0:
            fn["data"] = sum(1 for k, _m, _o in fn["insns"][last + 1:]
                             if k == "insn")
            fn["insns"] = fn["insns"][:last + 1]
        else:
            fn["data"] = sum(1 for k, _m, _o in fn["insns"] if k == "insn")
            fn["insns"] = []

    return [f for f in functions if f["name"]]


# Instruction shapes the threaded-call model expects.
SHAPES = [
    ("frame",     r"^(pushl|popl)\s+%(ebp|esi|edi|ebx)$"),
    ("frame",     r"^movl\s+%esp, %ebp$"),
    ("frame",     r"^(sub|add)l\s+\$0x[0-9a-f]+, %esp$"),
    ("frame",     r"^(leave|retl?)$"),
    ("frame",     r"^movl\s+%ebp, %esp$"),
    ("arg",       r"^pushl\s+\$?-?0x[0-9a-f]+"),
    ("arg",       r"^pushl\s+%e[a-d]x$"),
    ("arg",       r"^pushl\s+-?0x[0-9a-f]+\(%e(bp|sp)\)$"),
    ("arg",       r"^leal\s+-?0x[0-9a-f]+\(%e(bp|sp)\), %e[a-d]x$"),
    ("arg",       r"^movl\s+-?0x[0-9a-f]+\(%ebp\), %e(si|di|[a-d]x)$"),
    ("arg",       r"^movl\s+%e[a-d]x, -?0x[0-9a-f]+\(%ebp\)$"),
    ("arg",       r"^andl\s+\$0x0, -?0x[0-9a-f]+\(%ebp\)$"),
    ("call",      r"^calll?\s"),
    ("test",      r"^testl?\s+%e[a-d]x, %e[a-d]x$"),
    ("test",      r"^cmpl?\s"),
    ("branch",    r"^j(mp|e|ne|g|ge|l|le|a|ae|b|be|s|ns)\s"),
    ("stackadj",  r"^popl?\s+%e[a-d]x$"),
    # The state pointer lives in esi, and rules read and write 16-bit fields
    # in it and in their own frame directly rather than through a primitive.
    ("field",     r"^mov(w|zwl|swl)\s+.*%(e?si|e?di)"),
    ("field",     r"^mov(w|zwl|swl)\s+"),
    ("field",     r"^cmpw\s+"),
    ("field",     r"^orw\s+"),
    ("field",     r"^andw\s+"),
    ("field",     r"^leal\s+-?0x[0-9a-f]+\(%e(si|di)\), %e[a-d]x$"),
    ("field",     r"^leal\s+-?0x[0-9a-f]+\(%e(si|di)\), %e(si|di)$"),
    ("arith",     r"^(incl?|decl?|negl?|notl?)\s"),
    ("arith",     r"^xorl\s+%e[a-d]x, %e[a-d]x$"),
    ("arith",     r"^xorl\s+%e(si|di), %e(si|di)$"),
    ("arith",     r"^(addl|subl|imull|sarl|shll|shrl|andl|orl)\s"),
    ("arith",     r"^movl\s+%e[a-d]x, %e[a-d]x$"),
    ("arith",     r"^movl\s+\$-?0x[0-9a-f]+, "),
    ("arith",     r"^movl\s+%e(si|di), "),
    ("switch",    r"^jmpl?\s+\*"),
    # Zero padding and jump table entries both disassemble as this.
    ("pad",       r"^addb\s+%al, \(%eax\)$"),
    # Everything else the rules do inline: scalar moves and arithmetic on
    # locals, on the state through a pointer, and the odd division. A lifter
    # has to translate these rather than re-emit a call, so they are counted
    # apart from the threaded part.
    ("inline",    r"^(mov[blwq]?|movz[bw]l|movs[bw]l)\s"),
    ("inline",    r"^(add|sub|and|or|xor|cmp|test|inc|dec|neg|not|adc|sbb)[blw]?\s"),
    ("inline",    r"^(imul|mul|idiv|div|sar|shl|shr|rol|ror)[blwq]?\s"),
    ("inline",    r"^(cltd|cwtl|cbtw|nop|leal)\b"),
    ("inline",    r"^(set|cmov)[a-z]+\s"),
    ("inline",    r"^(pushl|popl)\s"),
    ("inline",    r"^(rep|repne|scas|stos|movs|cmps)[a-z]*\b"),
]


def classify(mnem, ops):
    text = (mnem + " " + (ops or "")).strip()
    text = re.sub(r"\s*->\S+$", "", text)
    text = re.sub(r"\s*#.*$", "", text).strip()
    for name, pattern in SHAPES:
        if re.match(pattern, text):
            return name
    return None


def main():
    if len(sys.argv) < 2:
        print("usage: lift-delta.py <object> [<object> ...]", file=sys.stderr)
        return 2

    code = 0        # instructions in the function body
    data = 0        # words after the body: jump tables and alignment padding
    covered = 0
    unknown = collections.Counter()
    prims = collections.Counter()
    mix = collections.Counter()
    nfuncs = 0

    for obj in sys.argv[1:]:
        print("lift-delta: reading %s" % os.path.basename(obj))
        for fn in disassemble(obj):
            nfuncs += 1
            data += fn["data"]
            for kind, mnem, ops in fn["insns"]:
                if kind != "insn":
                    continue
                what = classify(mnem, ops)
                if what == "pad":
                    data += 1
                    continue
                code += 1
                if what is None:
                    key = re.sub(r"0x[0-9a-f]+", "N",
                                 (mnem + " " + (ops or "")).strip())
                    key = re.sub(r"\s*->\S+$", "", key)
                    unknown[key] += 1
                else:
                    covered += 1
                    mix[what] += 1
                    if what == "call" and ops and "->" in ops:
                        prims[ops.split("->")[-1]] += 1

    print()
    print("functions: %d" % nfuncs)
    print("code instructions: %d" % code)
    print("jump tables and padding: %d words" % data)
    print("accounted for: %d of %d (%.2f%%)"
          % (covered, code, 100.0 * covered / max(code, 1)))
    print("distinct call targets: %d" % len(prims))
    print()
    print("=== what the code is made of ===")
    for what, n in mix.most_common():
        print("  %-10s %7d  %5.1f%%" % (what, n, 100.0 * n / max(code, 1)))
    print()
    print("=== shapes the model does not explain, most common first ===")
    for text, n in unknown.most_common(60):
        print("  %6d  %s" % (n, text))
    if not unknown:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
