#!/usr/bin/env python3
"""How much of the rules' control flow is a shape C has a word for.

Writing a rule as C faithfully needs nothing but labels and gotos, and that is
what tools/delta-decompile.py does. Getting back to something a person would
recognise as the rule language needs the loops and the conditionals named, and
that is only possible where the flow has a shape a structured language can say.

The question that decides it is asked of every edge that goes backwards: does
the block it lands on stand on every path to the block it left? Where it does,
the edge closes a loop, and the loop has one way in and can be written as one.
Where it does not, no arrangement of loops and conditionals says it: the code
it lands on is reached both from above and from below, and saying that in a
structured language means copying the code or adding a variable to dispatch on.

An earlier version of this asked instead whether the loops it had already found
had one way in, which they have by construction, and so answered that the shape
was good for every rule. Everything the answer turned on, it had put aside as a
forward edge. This counts what is actually there.
"""

import collections
import importlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
census = importlib.import_module('delta-census')

# The one that decides it, spelled once because it is long and is counted,
# printed and sorted on.
AWKWARD = 'edges back to a block that does not stand over them'



def graph(insns, length):
    """The rule cut into basic blocks, and where each one can go next. A
    dispatch is one block with as many ways out as it has arms, and the way it
    falls through when the answer is not one of them."""
    blocks = census.split(insns, length)
    at = {off: i for i, (off, _body) in enumerate(blocks)}
    n = len(blocks)
    succ = [[] for _ in range(n)]
    ends = [None] * n
    for i, (off, body) in enumerate(blocks):
        last = body[-1]
        shape, vals, ops, targets, size = insns[last]
        op = shape[0]
        nxt = at.get(last + size)
        ends[i] = op if op in ('jump', 'branch', 'switch', 'return') else 'fall'
        if op == 'jump':
            outs = [at[t] for t in targets]
        elif op == 'branch':
            outs = [at[targets[0]], nxt]
        elif op == 'switch':
            outs = [at[t] for t in targets] + [nxt]
        elif op == 'return':
            outs = []
        else:
            outs = [nxt]
        succ[i] = [x for x in outs if x is not None]
    return n, succ, ends


def rpo(n, succ):
    """The blocks in reverse postorder, which is the order a walk finishes
    them in, backwards. An edge to a block earlier in it goes back."""
    seen = [False] * n
    out = []
    stack = [(0, iter(succ[0]))]
    seen[0] = True
    while stack:
        u, it = stack[-1]
        for v in it:
            if not seen[v]:
                seen[v] = True
                stack.append((v, iter(succ[v])))
                break
        else:
            out.append(u)
            stack.pop()
    out.reverse()
    return out


def dominators(n, succ, order):
    """Which blocks stand on every path from the entry to each block, as a bit
    per block."""
    pred = [[] for _ in range(n)]
    for u in range(n):
        for v in succ[u]:
            pred[v].append(u)
    full = (1 << n) - 1
    dom = [full] * n
    dom[0] = 1
    again = True
    while again:
        again = False
        for u in order:
            if u == 0:
                continue
            new = full
            for p in pred[u]:
                new &= dom[p]
            new |= 1 << u
            if new != dom[u]:
                dom[u] = new
                again = True
    return dom, pred


def look(insns, length):
    n, succ, ends = graph(insns, length)
    order = rpo(n, succ)
    dom, pred = dominators(n, succ, order)
    place = {u: i for i, u in enumerate(order)}

    got = collections.Counter()
    got['blocks'] = n
    for u in range(n):
        if u not in place:
            got['blocks nothing reaches'] += 1
            continue
        outs = succ[u]
        for k, v in enumerate(outs):
            got['edges'] += 1
            if v not in place:
                continue
            if ends[u] == 'switch' and k < len(outs) - 1:
                got['arms of a dispatch'] += 1
            elif place[v] <= place[u]:
                if (dom[u] >> v) & 1:
                    got['edges closing a loop'] += 1
                else:
                    got[AWKWARD] += 1
            elif ends[u] == 'fall' or (ends[u] == 'branch' and k == 1):
                got['edges straight on'] += 1
            else:
                got['edges forward'] += 1
    return got


def main():
    c, rules = census.load()
    bodies = []
    for name, obj, start, length in rules:
        insns = c.decode(start, length)
        if any(insns[o][0] == ('call', 'ventproc') for o in insns):
            bodies.append((name, insns, length))

    tally = collections.Counter()
    sizes = []
    awkward = collections.Counter()
    worst = []
    for name, insns, length in bodies:
        got = look(insns, length)
        sizes.append(got['blocks'])
        tally.update(got)
        bad = got[AWKWARD]
        awkward[min(bad, 10)] += 1
        worst.append((bad, got['blocks'], name))

    print('rules with a body: %d' % len(bodies))
    print('blocks in one: %d at the median, %d at the most'
          % (sorted(sizes)[len(sizes) // 2], max(sizes)))
    print()
    for k in ('blocks', 'edges', 'edges straight on', 'edges forward',
              'arms of a dispatch', 'edges closing a loop',
              AWKWARD,
              'blocks nothing reaches'):
        print('  %-52s %6d' % (k, tally[k]))
    print()
    print('rules by how many edges go back to a block that does not stand'
          ' over them:')
    for k in sorted(awkward):
        print('  %s%2d  %4d rules' % ('at least ' if k == 10 else '         ',
                                      k, awkward[k]))
    worst.sort(reverse=True)
    print()
    print('the most awkward rules:')
    for bad, blocks, name in worst[:5]:
        print('  %-28s %4d of %4d blocks' % (name, bad, blocks))
    return 0


if __name__ == '__main__':
    sys.exit(main())
