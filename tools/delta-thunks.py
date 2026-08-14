#!/usr/bin/env python3
"""Decode the Delta compiler's helper thunks into an opcode table.

Every generated language module carries one glob.obj holding a few thousand
tiny helpers. Each is a fixed sequence of runtime calls with some arguments
wired to constants and the rest passed through from the caller. That is
already an instruction set: the sequence is the opcode and the constants are
its operands. This recovers it so the shapes can be counted before a bytecode
is designed around them.
"""

import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def thunks(obj):
    out = subprocess.run(
        ["llvm-objdump", "-d", "-r", "--no-show-raw-insn", obj],
        check=True, capture_output=True, text=True).stdout

    result = []
    name = None
    body = []

    def flush():
        if name is not None and name.startswith("_ZZ"):
            result.append((name, list(body)))

    for line in out.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
        if m:
            flush()
            name = m.group(1)
            body = []
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
    """Reduce one thunk to its call sequence and the operands it wires in."""
    ops = []
    args = []
    for mnem, operands, reloc in body:
        if mnem.startswith("push"):
            if reloc:
                args.append(("sym", reloc))
            elif operands.startswith("$"):
                args.append(("imm", operands.lstrip("$")))
            elif "(%esp)" in operands:
                args.append(("arg", operands))
            else:
                args.append(("reg", operands))
        elif mnem.startswith("call"):
            ops.append((reloc or "?", list(args)))
            args = []
        elif mnem.startswith(("add", "pop")) and "%esp" in operands or mnem == "popl":
            continue
        elif mnem.startswith("ret"):
            break
    return ops


def shape(ops):
    """The opcode: the call sequence plus which operand slots are constant."""
    parts = []
    for prim, args in ops:
        kinds = ",".join(k for k, _v in args)
        parts.append("%s(%s)" % (prim.lstrip("_"), kinds))
    return " ; ".join(parts)


def main():
    obj = sys.argv[1] if len(sys.argv) > 1 else \
        os.path.join(ROOT, "analysis", "enus", "glob.obj")

    print("delta-thunks: reading %s" % os.path.basename(obj))
    found = thunks(obj)
    print("thunks: %d" % len(found))

    shapes = collections.Counter()
    prims = collections.Counter()
    per_shape_example = {}

    for name, body in found:
        ops = decode(body)
        s = shape(ops)
        shapes[s] += 1
        per_shape_example.setdefault(s, name)
        for prim, _args in ops:
            prims[prim.lstrip("_")] += 1

    print("distinct call sequences (candidate opcodes): %d" % len(shapes))
    print("distinct runtime primitives used: %d" % len(prims))
    print()
    print("=== the twenty most common shapes ===")
    for s, n in shapes.most_common(20):
        print("  %5d  %s" % (n, s))
    print()
    print("=== how the shapes are distributed ===")
    counts = sorted(shapes.values(), reverse=True)
    for cut in (1, 5, 10, 20, 50, 100):
        covered = sum(c for c in counts[:cut])
        print("  top %3d shapes cover %5d of %d thunks (%.1f%%)"
              % (cut, covered, len(found), 100.0 * covered / max(len(found), 1)))
    singles = sum(1 for c in counts if c == 1)
    print("  shapes used exactly once: %d" % singles)
    return 0


if __name__ == "__main__":
    sys.exit(main())
