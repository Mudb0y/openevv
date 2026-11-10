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

A dictionary that says where a word is stressed says it in a slot of its own,
which the tail reads after the record is laid: nought leaves the stress to the
rules, and anything else is the pattern itself, one decimal digit a syllable,
1 for the main stress, 2 for a secondary one and 0 for none -- the same digits
a word's phonemes are written with, so 20100 is Appalachians. The digits count
back from the last syllable, so syllables before the first digit are
unstressed: 1 is Siobhan, stressed on the second of two. An arm that wants one
stores it before its jump. Which slot it is differs from rule to rule, and the
tail's test of it for nought is what says which.
"""

import os
import re


class CannotWrite(Exception):
    """Why this rule will not take an arm written this way."""


def whole(tree):
    """Every rule of the language, for asking whether a helper exists."""
    return ''.join(open(os.path.join(tree, f), encoding="utf-8").read()
                   for f in sorted(os.listdir(tree)) if f.endswith('.dr'))


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
    # The other shape: the record pushed and a helper called whose name ends
    # in the record's length, one helper for every length the language was
    # compiled with. A new arm can only lay a length one of them already does.
    # Other arms may jump in between the push and the call, which leaves a
    # label there.
    pat = re.compile(
        r'^label (L\d+) was (\S+)\n'
        r'  push sym (\S+)\n'
        r'(?:label \S+ was \S+\n)?'
        r'  push reg (r\d+)\n'
        r'  call (\S+_)(\d+) arity 2 depth (\d+)\n'
        r'  jump to (L\d+)$', re.M)
    for m in pat.finditer(body):
        return {'label': m.group(1), 'reg': m.group(4), 'helper': m.group(5),
                'depth': m.group(7), 'tail': m.group(8)}
    raise CannotWrite('no arm in this rule states its own record length, so '
                      'a new one has nothing to copy')


def stress_slot(body):
    """The slot a rule's tail reads a word's stress pattern from, or None."""
    m = re.search(r'^  cmp cmpw imm 0 slot (\d+)\n  branch je to L\d+\n'
                  r'  load movzwl slot \1 into r\d+$', body, re.M)
    return m.group(1) if m else None


def stress_value(pattern):
    """A pattern as the number an arm stores, which the tail reads as sixteen
    bits. Leading noughts say nothing the count from the end does not."""
    if not re.fullmatch(r'0*[12][012]*', pattern) or int(pattern) > 0xffff:
        raise CannotWrite('a stress pattern is a digit a syllable with a 1 or a '
                          '2 in it, no more than 65535; %s is not one'
                          % pattern)
    return int(pattern)


def add(tree, name, obj, blob, off, length, symbols_path, stress=None):
    """Write one arm. Answers the action number it was given."""
    dr = None
    for f in sorted(os.listdir(tree)):
        if not f.endswith('.dr'):
            continue
        path = os.path.join(tree, f)
        text = open(path, encoding="utf-8").read()
        if re.search(r'^rule %s ' % re.escape(name), text, re.M):
            dr = path
            break
    if dr is None:
        raise CannotWrite('no lower-form file holds the rule %s' % name)

    text = open(dr, encoding="utf-8").read()
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

    pattern = ''
    if stress is not None:
        slot = stress_slot(body)
        if slot is None:
            raise CannotWrite('%s keeps no stress pattern for a word' % name)
        pattern = '  store movl imm %d slot %s\n' % (stress_value(stress), slot)
    if 'helper' in shape:
        helper = '%s%d' % (shape['helper'], length)
        if not re.search(r'\bcall %s ' % re.escape(helper), whole(tree)):
            raise CannotWrite('%s lays a record of %d with no helper the '
                              'language was compiled with' % (name, length))
        arm = ('label %s was $arm$%s\n'
               '  push sym %s\n'
               '  push reg %s\n'
               '  call %s arity 2 depth %s\n'
               '  jump to %s\n'
               % (label, label, sym, shape['reg'], helper, shape['depth'],
                  shape['tail']))
    else:
        arm = ('label %s was $arm$%s\n'
               '  store %s imm %d slot %s\n'
               '  load movl sym %s into %s\n'
               '%s'
               '  jump to %s\n'
               % (label, label, shape['store'], length, shape['slot'],
                  sym, shape['reg'], pattern, shape['tail']))

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

    open(dr, 'w', encoding="utf-8").write(text[:span[0]] + new + text[span[1]:])

    with open(symbols_path, 'a', encoding="utf-8") as f:
        f.write('at %s %s %s %d\n' % (obj, sym, blob, off))

    return act + 1


def arm_block(body, label):
    """The three lines belonging to one arm, and where they sit."""
    m = re.search(r'^label %s was \S+\n((?:  .*\n)*)' % re.escape(label),
                  body, re.M)
    if not m:
        raise CannotWrite('no block for %s' % label)
    return m


def rewrite(tree, name, act, obj, blob, off, length, was, symbols_path):
    """Give an action that already exists a record of its own.

    The same three lines as a new arm, only found rather than written: the
    length it states and the symbol it names are replaced where they stand.
    An arm that names its record in code it shares with other arms is refused,
    because changing it there would change what those words say too -- which
    is the same rule the bytecode path applied, said in text.
    """
    dr = None
    for f in sorted(os.listdir(tree)):
        if not f.endswith('.dr'):
            continue
        path = os.path.join(tree, f)
        text = open(path, encoding="utf-8").read()
        if re.search(r'^rule %s ' % re.escape(name), text, re.M):
            dr = path
            break
    if dr is None:
        raise CannotWrite('no lower-form file holds the rule %s' % name)

    text = open(dr, encoding="utf-8").read()
    span = rule_span(text, name)
    body = text[span[0]:span[1]]

    switch = re.search(r'^  switch reg r\d+ to (.+)$', body, re.M)
    if not switch:
        raise CannotWrite('%s does not dispatch the way this can read' % name)
    labels = switch.group(1).split()
    if not 1 <= act <= len(labels):
        raise CannotWrite('%s has no action %d to change' % (name, act))

    m = arm_block(body, labels[act - 1])
    block = m.group(0)

    load = re.search(r'^  load movl sym (\S+) into (r\d+)$', block, re.M)
    if not load:
        raise CannotWrite('action %d names its record somewhere this cannot '
                          'reach on its own, so changing it here would change '
                          'what every word sharing that code says' % act)

    store = re.search(r'^  store (mov[bwl]) imm (\d+) slot (\d+)$', block, re.M)
    if store is None and length != was:
        raise CannotWrite('action %d does not state its own length -- the rule '
                          'says it another way -- so it can only be given a '
                          'record of the length it already lays down, and %d '
                          'is not %d' % (act, length, was))

    sym = 'string_%d' % (max(
        [int(x) for x in re.findall(r'\bstring_(\d+)\b', text)] or [0]) + 1)

    new = block
    if store is not None:
        new = new.replace(store.group(0), '  store %s imm %d slot %s'
                          % (store.group(1), length, store.group(3)), 1)
    new = new.replace(load.group(0), '  load movl sym %s into %s'
                      % (sym, load.group(2)), 1)

    body = body[:m.start()] + new + body[m.end():]
    open(dr, 'w', encoding="utf-8").write(text[:span[0]] + body + text[span[1]:])

    with open(symbols_path, 'a', encoding="utf-8") as f:
        f.write('at %s %s %s %d\n' % (obj, sym, blob, off))


def _rule_text(tree, name):
    for f in sorted(os.listdir(tree)):
        if not f.endswith('.dr'):
            continue
        path = os.path.join(tree, f)
        text = open(path, encoding="utf-8").read()
        if re.search(r'^rule %s ' % re.escape(name), text, re.M):
            return path, text
    raise CannotWrite('no lower-form file holds the rule %s' % name)


def _tail_sets(body, label, slot):
    """Whether the code an arm jumps to stores the stress slot itself before
    the tail reads it, which would undo what the arm stored."""
    lines = body.split('\n')
    at = {l.split()[1]: i for i, l in enumerate(lines)
          if l.startswith('label ')}
    seen = set()
    while label not in seen and label in at:
        seen.add(label)
        for line in lines[at[label] + 1:]:
            if line.startswith('label '):
                continue
            if re.match(r'  cmp cmpw imm 0 slot %s$' % slot, line):
                return False
            if re.match(r'  store \S+ \S+ \S+ slot %s$' % slot, line):
                return True
            m = re.match(r'  jump to (L\d+)$', line)
            if m:
                label = m.group(1)
                break
            if line.startswith('  return') or line == 'end':
                return False
    return False


def _bypass(body, label, slot, also):
    """Where an arm can jump instead of `label' to reach the tail that reads
    the stress slot without passing anything that writes it: the label that
    tail begins at, when everything on the way is only a store to the stress
    slot or to a slot in `also', which the arm stores itself. None when
    something else is in the way."""
    lines = body.split('\n')
    at = {l.split()[1]: i for i, l in enumerate(lines)
          if l.startswith('label ')}
    test = re.compile(r'  cmp cmpw imm 0 slot %s$' % slot)

    def reads_here(start):
        for line in lines[start + 1:]:
            if test.match(line):
                return True
            if line.startswith('label '):
                continue
            if line.startswith('  jump ') or line.startswith('  return'):
                return False
        return False

    seen = set()
    while label in at and label not in seen:
        seen.add(label)
        i = at[label]
        if reads_here(i):
            return label
        for line in lines[i + 1:]:
            if line.startswith('label '):
                if reads_here(at[line.split()[1]]):
                    return line.split()[1]
                continue
            m = re.match(r'  store \S+ \S+ \S+ slot (\d+)$', line)
            if m and (m.group(1) == slot or m.group(1) in also):
                continue
            m = re.match(r'  jump to (L\d+)$', line)
            if m:
                label = m.group(1)
                break
            return None
        else:
            return None
    return None


def stresses(tree, name):
    """What each action's own arm stores as the word's stress pattern, for
    the arms that store one."""
    _path, text = _rule_text(tree, name)
    span = rule_span(text, name)
    body = text[span[0]:span[1]]
    slot = stress_slot(body)
    switch = re.search(r'^  switch reg r\d+ to (.+)$', body, re.M)
    if slot is None or not switch:
        return {}
    out = {}
    for act, label in enumerate(switch.group(1).split(), 1):
        try:
            block = arm_block(body, label).group(0)
        except CannotWrite:
            continue
        m = re.search(r'^  store movl imm (\d+) slot %s$' % slot, block, re.M)
        if m:
            out[act] = m.group(1)
    return out


def set_stress(tree, name, act, stress):
    """Give one action's arm a stress pattern of its own, or with None take
    the one it stores away and leave the stress to the rules. Refused where
    the code the arm jumps to stores the pattern itself."""
    path, text = _rule_text(tree, name)
    span = rule_span(text, name)
    body = text[span[0]:span[1]]
    slot = stress_slot(body)
    if slot is None:
        raise CannotWrite('%s keeps no stress pattern for a word' % name)
    switch = re.search(r'^  switch reg r\d+ to (.+)$', body, re.M)
    labels = switch.group(1).split() if switch else []
    if not 1 <= act <= len(labels):
        raise CannotWrite('%s has no action %d' % (name, act))
    m = arm_block(body, labels[act - 1])
    block = m.group(0)
    have = re.search(r'^  store movl imm (\d+) slot %s$' % slot, block, re.M)
    if (have.group(1) if have else None) == (
            str(stress_value(stress)) if stress is not None else None):
        return
    jump = re.search(r'^  jump to (L\d+)$', block, re.M)
    if jump:
        onward = jump.group(1)
    else:
        # An arm without a jump runs on into the label after it.
        after = re.match(r'label (L\d+) ', body[m.end():])
        if not after:
            raise CannotWrite('%s action %d runs on into nothing this can '
                              'read' % (name, act))
        onward = after.group(1)
    target = onward
    if stress is not None and _tail_sets(body, onward, slot):
        # The code it jumps to stores a default of its own. That is only
        # passed by when it does nothing else the arm does not do itself.
        own = set(re.findall(r'^  store \S+ \S+ \S+ slot (\d+)$', block, re.M))
        target = _bypass(body, onward, slot, own - {slot})
        if target is None:
            raise CannotWrite('%s action %d has its stress stored by code '
                              'other words run through' % (name, act))
    elif stress is None and _tail_sets(body, onward, slot):
        raise CannotWrite('%s action %d has its stress stored by code other '
                          'words run through' % (name, act))
    new = re.sub(r'^  store movl imm \d+ slot %s\n' % slot, '', block,
                 flags=re.M)
    if stress is not None:
        line = '  store movl imm %d slot %s\n' % (stress_value(stress), slot)
        if jump:
            new = new.replace(jump.group(0),
                              line + '  jump to %s' % target, 1)
        else:
            new = new + line + '  jump to %s\n' % target
    body = body[:m.start()] + new + body[m.end():]
    open(path, 'w', encoding="utf-8").write(text[:span[0]] + body + text[span[1]:])
