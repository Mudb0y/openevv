#!/usr/bin/env python3
"""How much of the rules' control flow is a shape C has a word for.

Writing a rule as C faithfully needs nothing but labels and gotos, and that is
what tools/delta-decompile.py does. Getting back to something a person would
recognise as the rule language needs the loops and the conditionals named, and
that is only possible where the flow has the shape a structured language can
say: every loop entered at one place, every branch rejoining at one place.

This counts that, and nothing else, so that the size of the next pass is known
before it is started rather than after.
"""

import collections
import importlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
census = importlib.import_module('delta-census')


def blocks(insns, length):
    """The rule cut into basic blocks, and where each can go next."""
    leaders = {0}
    for off, (shape, vals, ops, targets, size) in insns.items():
        leaders.update(targets)
        if shape[0] in ('jump', 'branch', 'switch', 'return'):
            if off + size < length:
                leaders.add(off + size)

    starts = sorted(l for l in leaders if l in insns)
    where = {}
    for i, s in enumerate(starts):
        end = starts[i + 1] if i + 1 < len(starts) else length
        where[s] = [o for o in sorted(insns) if s <= o < end]

    nxt = {}
    for s, body in where.items():
        if not body:
            nxt[s] = []
            continue
        last = body[-1]
        shape, vals, ops, targets, size = insns[last]
        out = []
        if shape[0] == 'return':
            pass
        elif shape[0] == 'jump':
            out = list(targets)
        elif shape[0] == 'switch':
            out = list(targets)
        elif shape[0] == 'branch':
            out = list(targets)
            if last + size < length:
                out.append(last + size)
        elif last + size < length:
            out.append(last + size)
        nxt[s] = [t for t in out if t in where]
    return starts, nxt


def dominators(starts, nxt):
    """Which blocks every way in has to pass through to reach each one."""
    pred = collections.defaultdict(set)
    for s in starts:
        for t in nxt[s]:
            pred[t].add(s)

    entry = starts[0]
    dom = {s: set(starts) for s in starts}
    dom[entry] = {entry}
    changed = True
    while changed:
        changed = False
        for s in starts:
            if s == entry:
                continue
            ins = [dom[p] for p in pred[s] if p in dom]
            new = set.intersection(*ins) | {s} if ins else {s}
            if new != dom[s]:
                dom[s] = new
                changed = True
    return dom, pred


def look(name, insns, length):
    starts, nxt = blocks(insns, length)
    if not starts:
        return None
    dom, pred = dominators(starts, nxt)

    back, forward = [], []
    for s in starts:
        for t in nxt[s]:
            (back if t in dom[s] else forward).append((s, t))

    # A loop a structured language can say is one entered only at its head.
    loops, awkward = [], 0
    for tail, head in back:
        body = {head}
        stack = [tail]
        while stack:
            b = stack.pop()
            if b in body:
                continue
            body.add(b)
            stack.extend(pred[b])
        outside = [p for b in body for p in pred[b]
                   if p not in body and b != head]
        if outside:
            awkward += 1
        loops.append(len(body))

    # A branch a structured language can say is one whose two ways meet again
    # at a block both of them have to reach.
    twoway = rejoin = 0
    for s in starts:
        if len(nxt[s]) != 2:
            continue
        twoway += 1
        a, b = nxt[s]
        after = [t for t in starts if s in dom[t] and t != s
                 and (a in dom[t] or b in dom[t]) is False]
        if any(s in dom[t] and t not in (a, b) for t in starts):
            rejoin += 1

    return {'blocks': len(starts), 'back': len(back), 'loops': loops,
            'awkward': awkward, 'twoway': twoway, 'rejoin': rejoin}


def main():
    c, rules = census.load()
    bodies = []
    for name, obj, start, length in rules:
        insns = c.decode(start, length)
        if any(insns[o][0] == ('call', 'ventproc') for o in insns):
            bodies.append((name, insns, length))

    tally = collections.Counter()
    sizes = []
    for name, insns, length in bodies:
        got = look(name, insns, length)
        if got is None:
            tally['nothing to look at'] += 1
            continue
        sizes.append(got['blocks'])
        if got['back'] == 0:
            tally['no loop at all'] += 1
        elif got['awkward'] == 0:
            tally['every loop entered at one place'] += 1
        else:
            tally['a loop entered at more than one place'] += 1

    print('rules with a body: %d' % len(bodies))
    print('blocks in one: %d at the median, %d at the most'
          % (sorted(sizes)[len(sizes) // 2], max(sizes)))
    print()
    for k, v in tally.most_common():
        print('  %-42s %4d' % (k, v))
    return 0


if __name__ == '__main__':
    sys.exit(main())
