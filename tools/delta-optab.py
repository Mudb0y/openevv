#!/usr/bin/env python3
"""Decode every helper thunk in a language module into an operation table.

A thunk is a fixed sequence of runtime calls with some arguments wired to
constants and the rest passed through from the caller. That is already an
instruction set with its operands encoded, so the table is recovered rather
than designed.

Nothing is guessed. Every instruction in every thunk has to fall into one of
the shapes below or it is reported as a hole, and a pass-through has to land
exactly on one of the thunk's own arguments or it is reported too.
"""

import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_thunks(obj):
    """Every ZZ helper in the object, as a list of (mnemonic, operands,
    relocation)."""
    out = subprocess.run(
        ["llvm-objdump", "-d", "-r", "--no-show-raw-insn", obj],
        check=True, capture_output=True, text=True).stdout

    result = []
    name = None
    body = []

    def flush():
        if name is not None and name.startswith("_ZZ"):
            result.append((name[1:], list(body)))

    for line in out.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
        if m:
            flush()
            name, body = m.group(1), []
            continue
        m = re.search(r"IMAGE_REL_I386_(\S+)\s+(\S+)$", line)
        if m and body:
            body[-1] = (body[-1][0], body[-1][1], m.group(2))
            continue
        m = re.match(r"^\s+[0-9a-f]+:\s+(\S+)\s*(.*?)(?:\s+#.*)?$", line)
        if m:
            body.append((m.group(1), m.group(2).strip(), None))

    flush()
    return result


def decode(body):
    """One thunk as a list of calls, each with its operands.

    Returns (calls, holes). An operand is ('imm', n), ('sym', name) or
    ('arg', i) for the thunk's own i'th argument. Most thunks address their
    arguments off the stack pointer, so depth tracks how far it has been
    pushed; a few set up a frame first and address them off the base pointer
    instead. A byte-wide argument is widened through a register on the way,
    which is why registers are tracked at all.
    """
    calls = []
    operands = []
    holes = []
    regs = {}
    depth = 0
    framed = False
    boolean = False
    short = False
    saved = set()
    i = 0

    def slot_esp(off):
        n = off - 4 - 4 * depth
        return None if n < 0 or n % 4 else n // 4

    def slot_ebp(off):
        n = off - 8
        return None if n < 0 or n % 4 else n // 4

    while i < len(body):
        mnem, ops, reloc = body[i]
        i += 1

        if mnem in ("pushl", "push"):
            if ops == "%ebp" and not framed and not calls and not operands:
                # The frame prologue, not an operand.
                if i < len(body) and body[i][0] in ("movl", "mov") \
                        and body[i][1] == "%esp, %ebp":
                    framed = True
                    i += 1
                    continue

            if reloc:
                operands.append(("sym", reloc.lstrip("_")))
            elif ops.startswith("$"):
                operands.append(("imm", int(ops.lstrip("$"), 0)))
            elif ops.endswith("(%esp)") or ops.endswith("(%ebp)"):
                base = ops[-5:-1]
                text = ops[:-6]
                off = int(text, 0) if text else 0
                n = slot_ebp(off) if base == "%ebp" else slot_esp(off)
                if n is None:
                    holes.append("pass-through %s at depth %d" % (ops, depth))
                    operands.append(("?", ops))
                else:
                    operands.append(("arg", n))
            elif ops in regs:
                operands.append(regs[ops])
            elif ops in ("%esi", "%edi", "%ebx"):
                # Saved on the way in, before it holds anything of ours.
                saved.add(ops)
                depth += 1
                continue
            else:
                holes.append("push %s" % ops)
                operands.append(("?", ops))
            depth += 1
            continue

        # An address inside something already held: the language's variable
        # blocks live at fixed offsets inside the state.
        if mnem in ("leal", "lea") and "," in ops:
            src, dst = [p.strip() for p in ops.split(",", 1)]
            m2 = re.match(r"^(-?0x[0-9a-f]+|-?\d+)?\((%[a-z]+)\)$", src)
            if m2 and dst.startswith("%"):
                off = int(m2.group(1), 0) if m2.group(1) else 0
                base = regs.get(m2.group(2))
                if base and base[0] == "arg":
                    regs[dst] = ("argoff", base[1], off)
                elif base and base[0] == "argoff":
                    regs[dst] = ("argoff", base[1], base[2] + off)
                else:
                    holes.append("lea %s" % src)
                    regs[dst] = ("?", src)
                continue

        # A store of a constant into a field of something the thunk holds,
        # done on the way past. It is an operation in its own right, so it
        # goes in the sequence where it happens.
        m3 = re.match(r"^(movl|movw|movb)$", mnem)
        if m3 and "," in ops:
            src, dst = [p.strip() for p in ops.split(",", 1)]
            m4 = re.match(r"^(-?0x[0-9a-f]+|-?\d+)?\((%[a-z]+)\)$", dst)
            if src.startswith("$") and m4:
                off = int(m4.group(1), 0) if m4.group(1) else 0
                base = regs.get(m4.group(2))
                width = {"movl": 32, "movw": 16, "movb": 8}[m3.group(1)]
                if base and base[0] == "arg":
                    where = ("argoff", base[1], off)
                elif base and base[0] == "argoff":
                    where = ("argoff", base[1], base[2] + off)
                else:
                    holes.append("store into %s" % dst)
                    where = ("?", dst)
                calls.append(("store%d" % width,
                              [("imm", int(src.lstrip("$"), 0)), where]))
                continue

        m = re.match(r"^(movl|mov|movzbl|movsbl|movzwl|movswl)$", mnem)
        if m and "," in ops:
            src, dst = [p.strip() for p in ops.split(",", 1)]
            if dst.startswith("%") and (src.endswith("(%esp)")
                                        or src.endswith("(%ebp)")):
                base = src[-5:-1]
                text = src[:-6]
                off = int(text, 0) if text else 0
                n = slot_ebp(off) if base == "%ebp" else slot_esp(off)
                if n is None:
                    holes.append("load %s at depth %d" % (src, depth))
                    regs[dst] = ("?", src)
                else:
                    regs[dst] = ("arg", n)
                continue
            if dst.startswith("%") and src.startswith("$"):
                regs[dst] = ("imm", int(src.lstrip("$"), 0))
                continue
            if dst.startswith("%") and src.startswith("%"):
                regs[dst] = regs.get(src, ("?", src))
                continue

        if mnem in ("calll", "call"):
            calls.append((reloc.lstrip("_") if reloc else "?",
                          list(operands)))
            operands = []
            # Only the caller-saved registers are lost across a call; the
            # others are exactly why a thunk saves one on the way in.
            for r in ("%eax", "%ecx", "%edx"):
                regs.pop(r, None)
            continue

        if mnem in ("popl", "pop") and ops.startswith("%"):
            if ops == "%ebp" and framed:
                continue
            if ops in saved:
                saved.discard(ops)
            depth -= 1
            continue

        if mnem in ("addl", "add") and ops.endswith("%esp"):
            depth -= int(ops.split(",")[0].lstrip("$"), 0) // 4
            continue

        if mnem == "leave":
            continue

        # Stop at the first call that fails. Written as a test and a jump to
        # a tail that returns one; a pop of the argument may sit between.
        if mnem in ("testl", "test") and ops == "%eax, %eax":
            k = i
            if k < len(body) and body[k][0] in ("popl", "pop") \
                    and body[k][1].startswith("%"):
                depth -= 1
                k += 1
            if k < len(body) and body[k][0] in ("jne", "jnz"):
                short = True
                i = k + 1
                continue
            holes.append("test with no short circuit")
            continue

        # The tail that short circuit jumps to: return one.
        if mnem in ("xorl", "xor") and ops == "%eax, %eax" \
                and i < len(body) and body[i][0] in ("incl", "inc") \
                and body[i][1] == "%eax":
            i += 1
            continue

        # The compiler's way of writing "result != 0": negate, borrow, negate.
        if mnem in ("negl", "neg") and ops == "%eax" \
                and i + 1 < len(body) \
                and body[i][0] in ("sbbl", "sbb") \
                and body[i][1] == "%eax, %eax" \
                and body[i + 1][0] in ("negl", "neg") \
                and body[i + 1][1] == "%eax":
            boolean = True
            i += 2
            continue

        if mnem in ("retl", "ret"):
            break

        holes.append("%s %s" % (mnem, ops))

    return calls, holes, boolean, short


def render(calls):
    parts = []
    for prim, operands in calls:
        # Pushes run backwards through the argument list, so undo that and
        # the table reads as the call it is.
        operands = list(reversed(operands))
        shown = []
        for op in operands:
            kind = op[0]
            value = op[1] if len(op) == 2 else (op[1], op[2])
            if kind == "imm":
                shown.append(str(value))
            elif kind == "sym":
                shown.append(value)
            elif kind == "arg":
                shown.append("$%d" % value)
            elif kind == "argoff":
                shown.append("$%d+0x%x" % (value[0], value[1]))
            else:
                shown.append("?" + str(value))
        parts.append("%s(%s)" % (prim, ", ".join(shown)))
    return " ; ".join(parts)


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    obj = argv[0] if argv else \
        os.path.join(ROOT, "analysis", "enus", "glob.obj")

    found = read_thunks(obj)
    prims = collections.Counter()
    lengths = collections.Counter()
    bad = []
    table = []
    booleans = 0
    shorts = 0

    for name, body in found:
        calls, holes, boolean, short = decode(body)
        if holes:
            bad.append((name, holes))
        table.append((name, calls, boolean, short))
        booleans += 1 if boolean else 0
        shorts += 1 if short else 0
        lengths[len(calls)] += 1
        for prim, _ops in calls:
            prims[prim] += 1

    print("delta-optab: reading %s" % os.path.basename(obj))
    print("thunks: %d" % len(found))
    print("primitives they call: %d" % len(prims))
    print("thunks the decoder could not fully account for: %d" % len(bad))
    print("thunks that normalise their result to a boolean: %d" % booleans)
    print("thunks that stop at the first failure: %d" % shorts)
    print()
    print("=== how many calls a thunk makes ===")
    for n in sorted(lengths):
        print("  %d call%s: %d thunks" % (n, "" if n == 1 else "s", lengths[n]))

    if bad:
        print()
        print("=== holes, first few ===")
        for name, holes in bad[:10]:
            print("  %s: %s" % (name, "; ".join(holes[:3])))

    if "--table" in sys.argv:
        print()
        print("=== the table ===")
        for name, calls, boolean, short in table:
            print("  %-44s %s%s%s"
                  % (name, render(calls),
                     "  [stop on first failure]" if short else "",
                     "  -> bool" if boolean else ""))

    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
