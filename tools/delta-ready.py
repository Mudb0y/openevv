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
    """Transcribed counts as done. Almost everything is also compared against
    IBM's, but a few cannot be: vseqbad returns an indeterminate value, and
    the two heap calls that reach the system allocator are the layer this port
    supplies itself."""
    done = set()
    for name in ('delta.h',):
        for m in re.finditer(r'^\s*(?:[A-Za-z_][A-Za-z0-9_ *]*?)\b'
                             r'([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\);\s*$',
                             open(os.path.join(ROOT, 'src', name)).read(),
                             re.M):
            done.add(m.group(1))
    return done


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


def rule_roots():
    """Everything the generated rules call straight, minus the rules
    themselves and the thunks. That is the real remaining surface, so it
    beats guessing a root by hand."""
    lift = importlib.import_module('delta-lift')
    roots = set()
    rules = set()
    calls = set()
    for obj in sorted(f for f in os.listdir(deps.ENUS) if f.endswith('.obj')):
        for name, items in lift.read_functions(os.path.join(deps.ENUS, obj)):
            if not lift.is_rule(items):
                continue
            rules.add(name)
            data, tables = lift.find_data(items)
            d = lift.Decoder(name, items, data, tables).run()
            for _label, _start, block in d.blocks:
                for op in block:
                    if op[0] == 'call' and not op[1].startswith('ZZ'):
                        calls.add(op[1])
    for c in calls:
        if c not in rules:
            roots.add(c)
    return sorted(roots)


def main():
    if sys.argv[1:2] == ['--rules']:
        roots = rule_roots()
    else:
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
