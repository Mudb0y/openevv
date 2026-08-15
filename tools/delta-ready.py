#!/usr/bin/env python3
"""Which Delta primitives are transcribable now: every dependency already done.

The done set is read from the harness, since a function only counts as done
once it is being compared against IBM's.
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import importlib
deps = importlib.import_module('delta-deps')


def done_set():
    src = open(os.path.join(ROOT, 'oracle', 'delta_diff.c')).read()
    return set(re.findall(r'\bibm_([A-Za-z_][A-Za-z0-9_]*)\s*\(', src))


def sizes(obj):
    """Instruction count per function, so the easy ones come first."""
    out = subprocess.run(['llvm-objdump', '-d', '--no-show-raw-insn', obj],
                         check=True, capture_output=True, text=True).stdout
    counts, name = {}, None
    for line in out.splitlines():
        if line.startswith('Disassembly of section'):
            name = None
            continue
        m = re.match(r'^[0-9a-f]+ <(.+)>:$', line)
        if m:
            if name is None:
                name = m.group(1).lstrip('_')
                counts.setdefault(name, 0)
            continue
        if name and re.match(r'^\s+[0-9a-f]+:', line):
            counts[name] += 1
    return counts


def main():
    roots = sys.argv[1:] or ['DeltaProc_main']
    everything, size = {}, {}
    for o in deps.OBJECTS:
        path = os.path.join(deps.ENUS, o + '.obj')
        if os.path.exists(path):
            for fn, calls in deps.scan(path).items():
                everything.setdefault(fn, (o, calls))
            size.update(sizes(path))

    order = deps.closure(everything, roots)
    have = done_set()
    ready = []
    for fn in order:
        if fn in have or fn not in everything:
            continue
        obj, calls = everything[fn]
        need = [c for c in calls
                if c not in deps.LIBC and c != fn and c not in have
                and c in everything]
        if not need:
            ready.append((size.get(fn, 0), fn, obj))

    todo = [f for f in order if f not in have and f in everything]
    print('closure %d, done %d, left %d, ready now %d'
          % (len(order), len(order) - len(todo), len(todo), len(ready)))
    print()
    for n, fn, obj in sorted(ready):
        print('  %-24s %-11s %d instructions' % (fn, obj, n))
    return 0


if __name__ == '__main__':
    sys.exit(main())
