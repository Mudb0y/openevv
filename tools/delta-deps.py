#!/usr/bin/env python3
"""What each Delta primitive calls, so they can be transcribed bottom up.

Each function sits in its own COFF section, but the compiler also leaves its
own internal labels in the symbol table, so a scan that stops at the next
symbol stops in the middle of anything with a branch in it. Sections are the
only reliable boundary.
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENUS = os.path.join(ROOT, "analysis", "enus")

OBJECTS = """assign debug delta dfault extra for io misc optimize pointer
             stack test init gener access ctxt ddelta deltaimp deltio dttime
             dutil heap mem prdelta prdeltao rectbuf""".split()

# Things every C program calls; not worth reporting as a dependency.
LIBC = {"memcpy", "memset", "memmove", "strcpy", "strlen", "strcmp", "malloc",
        "calloc", "free", "abs", "atoi", "atol", "isdigit", "fread", "fwrite",
        "_errno", "sprintf", "strncmp", "toupper", "tolower"}


def scan(obj):
    """Section by section: the first symbol names it, the relocations say
    what it calls."""
    out = subprocess.run(
        ["llvm-objdump", "-d", "-r", "--no-show-raw-insn", obj],
        check=True, capture_output=True, text=True).stdout

    functions = {}
    name = None
    calls = None

    for line in out.splitlines():
        if line.startswith("Disassembly of section"):
            name, calls = None, set()
            continue
        m = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
        if m and name is None:
            name = m.group(1)
            if name.startswith("_") and not name.startswith("$"):
                functions[name[1:]] = calls
            continue
        m = re.search(r"IMAGE_REL_I386_REL32\s+_?(\S+)$", line)
        if m and calls is not None:
            calls.add(m.group(1))

    return functions


def closure(everything, roots):
    """Everything the roots reach, in an order that puts callees first."""
    order, seen, stack = [], set(), []

    def visit(fn):
        if fn in seen:
            return
        seen.add(fn)
        stack.append(fn)
        if fn in everything:
            for c in sorted(everything[fn][1]):
                if c not in LIBC:
                    visit(c)
        stack.pop()
        order.append(fn)

    for r in roots:
        visit(r)
    return order


def main():
    if sys.argv[1:2] == ['--closure']:
        roots = sys.argv[2:]
        everything = {}
        for o in OBJECTS:
            path = os.path.join(ENUS, o + '.obj')
            if os.path.exists(path):
                for fn, calls in scan(path).items():
                    everything.setdefault(fn, (o, calls))
        order = closure(everything, roots)
        missing = [f for f in order if f not in everything]
        print('reachable: %d functions, %d of them outside the runtime'
              % (len(order), len(missing)))
        print()
        for fn in order:
            if fn in everything:
                obj, calls = everything[fn]
                deps = sorted(c for c in calls if c not in LIBC and c != fn)
                print('  %-24s %-11s %s' % (fn, obj, ' '.join(deps)))
            else:
                print('  %-24s %-11s (not in the runtime objects)' % (fn, ''))
        return 0

    wanted = set(sys.argv[1:])
    everything = {}

    for o in OBJECTS:
        path = os.path.join(ENUS, o + ".obj")
        if os.path.exists(path):
            for fn, calls in scan(path).items():
                everything.setdefault(fn, (o, calls))

    names = sorted(wanted) if wanted else sorted(everything)
    leaves = 0

    for fn in names:
        if fn not in everything:
            print("  %-22s (not found)" % fn)
            continue
        obj, calls = everything[fn]
        interesting = sorted(c for c in calls if c not in LIBC and c != fn)
        if not interesting:
            leaves += 1
        print("  %-22s %-12s %s"
              % (fn, obj, " ".join(interesting) if interesting else "(leaf)"))

    print()
    print("%d of %d are leaves once the C library is discounted"
          % (leaves, len(names)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
