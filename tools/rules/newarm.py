#!/usr/bin/env python3
"""Add an arm to a dictionary rule by writing the lower form, not the bytecode.

The dictionary writer used to patch the rule's compiled image and splice it
into lang/<tag>/delta_rules_<tag>.c. That file is generated: `make rulecode'
writes it out of the text in lang/<tag>/rules at the head of every build, so
the rule half of a dictionary change was thrown away while the table half was
kept, and the engine faulted on the word. docs/status.md has the whole of it.

So write the text instead, which is where rules live now, and let the compiler
do what it is for. The lower form makes this far easier than patching bytecode
ever was: it names labels rather than offsets, so a block can be appended and a
switch widened with nothing to relocate.

An arm of the kind a dictionary needs is three lines --

    label L714 was $X99$100000
      store movb imm 9 slot 104
      load movl sym string_9001 into r6
      jump to L88

-- being the record's length into the slot the rule reads it from, the
record's address into the register the tail expects, and a jump to the tail
every arm of that shape shares. Adding one means those three lines, the new
label on the end of the switch, and the bound above the switch raised by one,
since the rule throws out an action past its arm count before dispatching.

The record's bytes go in the constants blob and its address in `symbols',
both of which are tracked and both of which survive a build untouched.
"""

import os
import re


class CannotWrite(Exception):
    """Why this rule will not take an arm written this way."""


def rule_span(text, name):
    """Where a rule begins and ends in a lower-form file."""
    start = None
    for m in re.finditer(r'^rule (\S+) ', text, re.M):
        if m.group(1) == name:
            start = m.start()
            continue
        if start is not None:
            return start, m.start()
    if start is None:
        return None
    return start, len(text)


def template(body):
    """An arm to copy the shape of: one that states its own length.

    Only that kind will do. A rule that says how long a record is by which of
    its own helpers an arm calls cannot be given a length it has never laid
    down, and one that packs the length into an immediate has nowhere to put
    a new one. Those are refused rather than guessed at.
    """
    pat = re.compile(
        r'^label (L\d+) was (\S+)\n'
        r'  store (mov[bwl]) imm (\d+) slot (\d+)\n'
        r'  load movl sym (\S+) into (r\d+)\n'
        r'  jump to (L\d+)$', re.M)
    for m in pat.finditer(body):
        return {'label': m.group(1), 'store': m.group(3), 'slot': m.group(5),
                'reg': m.group(7), 'tail': m.group(8)}
    raise CannotWrite('no arm in this rule states its own record length, so '
                      'a new one has nothing to copy')


def add(tree, name, obj, blob, off, length, symbols_path):
    """Write one arm. Answers the action number it was given."""
    dr = None
    for f in sorted(os.listdir(tree)):
        if not f.endswith('.dr'):
            continue
        path = os.path.join(tree, f)
        text = open(path).read()
        if re.search(r'^rule %s ' % re.escape(name), text, re.M):
            dr = path
            break
    if dr is None:
        raise CannotWrite('no lower-form file holds the rule %s' % name)

    text = open(dr).read()
    span = rule_span(text, name)
    body = text[span[0]:span[1]]
    shape = template(body)

    bound = re.search(r'^  cmp cmpl imm (\d+) reg (r\d+)$', body, re.M)
    switch = re.search(r'^  switch reg (r\d+) to (.+)$', body, re.M)
    if not bound or not switch:
        raise CannotWrite('%s does not dispatch the way this can widen' % name)

    arms = switch.group(2).split()
    act = len(arms)                       # the new one goes on the end
    if int(bound.group(1)) != act - 1:
        raise CannotWrite('%s checks against %s with %d arms, which is not '
                          'the shape this knows'
                          % (name, bound.group(1), len(arms)))

    used = set(int(m) for m in re.findall(r'^label L(\d+) ', body, re.M))
    label = 'L%d' % (max(used) + 1)
    sym = 'string_%d' % (max(
        [int(m) for m in re.findall(r'\bstring_(\d+)\b', text)] or [0]) + 1)

    arm = ('label %s was $arm$%s\n'
           '  store %s imm %d slot %s\n'
           '  load movl sym %s into %s\n'
           '  jump to %s\n'
           % (label, label, shape['store'], length, shape['slot'],
              sym, shape['reg'], shape['tail']))

    new = body
    new = new.replace(bound.group(0), '  cmp cmpl imm %d reg %s'
                      % (act, bound.group(2)), 1)
    new = new.replace(switch.group(0), '  switch reg %s to %s %s'
                      % (switch.group(1), switch.group(2), label), 1)
    # Before the rule's `end', not after it: the reader takes that line as the
    # end of the rule and everything past it belongs to no rule at all, which
    # it reports as an operation before any label.
    tail = re.search(r'^end$', new, re.M)
    if not tail:
        raise CannotWrite('%s does not end where this can write' % name)
    new = new[:tail.start()] + arm + new[tail.start():]

    open(dr, 'w').write(text[:span[0]] + new + text[span[1]:])

    with open(symbols_path, 'a') as f:
        f.write('at %s %s %s %d\n' % (obj, sym, blob, off))

    return act + 1
