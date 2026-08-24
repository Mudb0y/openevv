#!/usr/bin/env python3
"""The switch arms of a dictionary rule: what each action lays down, and how
to make one lay down something else.

A dictionary says what to do by an action number and the rule of the same name
dispatches on it with one switch, arms numbered from one. An arm names a
record in a constant blob and settles how many phones long it is, then joins
the shared tail that inserts it. It may do either inline or by jumping to a
block it shares with other arms, so an arm is followed rather than read.

How the length is settled turned out to be three things, so it is worked out
rather than recognised: enough of the machine is run over the arm -- eight
registers, the frame slots, and the constants -- to see what the insert call
is actually handed. The third value pushed to it is the length. That covers a
length stored into a frame slot, one packed into a byte of a constant and
shifted out, and one written into the name of the wrapper rule the arm calls,
without having to tell them apart.

An arm may also lay a record into a statement other than the one the word is
said in, which is a mark the rule leaves on its way rather than anything
spoken. The statement is the fourth thing handed to the call, and which one a
rule speaks in is settled by a vote over the records its arms name for
themselves -- a record a wrapper names could be either. A mark counted as a
reading had the suffix `ance' saying "Xx n s or d".

An insert this cannot read at all is stepped over rather than reported, which
also means whatever is found after it is not the whole of what the word says.

Two shapes answer nothing rather than guess. An arm may test something and
take one of two ways -- an abbreviation read one way before a name and another
before a number -- and both ways are followed, an answer given only when they
agree. And an arm may lay a word down in pieces, as the currencies do, which
is not one record and is not reported as one.

A wrapper may name its own record rather than take one, which is how a suffix
that always says the same thing is written. Those read, but there is nothing
of the word's to change and they say so.

Writing appends: the record goes at the end of its blob and the pools grow, so
nothing that names the old bytes is disturbed. Three ways in, in order of how
little they touch. A record of the same length needs only the arm's own naming
of it repointed. A rule that pushes the length out of a frame slot gets a
block of its own at the end, setting the length and naming the record, with
the switch pointed at it. A rule that says the length by which wrapper it
calls has the call moved to the wrapper for the length wanted, and one that
packs it into a constant has the constant replaced with the other byte kept.

Nothing shared is ever patched: an instruction more than one arm can run
through is refused, because changing it would change what some other word
says.

A word needing a pronunciation nobody has yet gets an action of its own. A
switch cannot be widened where it stands without pushing everything after it
along, so a wider copy goes at the end of the rule and the old one is jumped
over, with what is left of it filled with instructions that decode but are
never reached -- execution would not care, but anything reading the rule from
its first byte would otherwise lose its place. The rule also checks the action
against how many arms it has before it dispatches, and that check is raised
too, or the new action is thrown out before the switch ever sees it.

Where a rule states the length itself the new arm states it too. Where it
states it by which of its own it calls, the new arm names its record and is
then sent to whichever block already lays a record of that length down, so the
lengths it can be given are the ones the language once compiled.
"""

import importlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Which language. EVV_LANG_DIR is the same variable the decompiler and the
# census read, and English without one, so nothing that used to work needs
# saying differently.
LANG_DIR = os.environ.get('EVV_LANG_DIR', os.path.join(ROOT, 'lang', 'enus'))
TAG = os.path.basename(os.path.normpath(LANG_DIR))
RULES_C = os.path.join(LANG_DIR, 'delta_rules_%s.c' % TAG)
CONSTS_C = os.path.join(LANG_DIR, 'delta_consts_%s.c' % TAG)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
census = importlib.import_module('delta-census')

OP_STORE = census.OPS.index('store')
OP_JUMP = census.OPS.index('jump')
K_IMM = census.KINDS.index('imm')
K_SLOT = census.KINDS.index('slot')

# How far a rule-relative target reaches, the field being two signed bytes.
TARGET_MAX = 0x7fff

# A wrapper rule that lays a record down with the length written into its own
# name, so that asking for another length means calling a different one.
WRAPPER = re.compile(r'^(.*insert_2pt_s[a-z0-9_]*?_1_)([0-9]+)$')

# The other family of them, which lays a piece of a word rather than a word.
LAYER = re.compile(r'^(.*insert_l[a-z0-9_]*?_1_)([0-9]+)$')


def carve_rules_full(text):
    """Every field of the rule table, since the offsets have to be written
    back after a rule grows."""
    out = []
    for m in re.finditer(r'\{\s*"([^"]*)",\s*"([^"]*)",\s*(-?\d+),\s*(-?\d+),'
                         r'\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\}',
                         census.span(text, 'delta_rules[]')):
        out.append([m.group(1), m.group(2)] + [int(m.group(i))
                                               for i in range(3, 8)])
    return out


class Record:
    """Where one action's record lies and what the arm does to name it."""

    def __init__(self, blob, off, length, how, sym_insn, count_insn, cont,
                 arm_slot):
        self.blob = blob
        self.off = off
        self.length = length
        self.how = how           # where the length was settled
        self.sym_insn = sym_insn      # (at, size, where the index lies)
        self.count_insn = count_insn  # (at, size, movk) or None
        self.cont = cont              # where the arm's setup gives out
        self.arm_slot = arm_slot      # where the switch keeps this arm


class Part:
    """One record an action lays down. A fixed one is named by the wrapper
    that lays it rather than by the arm, so it is shared with everything else
    that calls that wrapper and is not one word's to change."""

    def __init__(self, blob, off, length, sym_at, sym_where, call_at, fixed):
        self.blob = blob
        self.off = off
        self.length = length
        self.sym_at = sym_at
        self.sym_where = sym_where
        self.call_at = call_at
        self.fixed = fixed


class Rules:
    """The lifted rules, read so that they can be written back."""

    def __init__(self):
        text = open(RULES_C).read()
        self.code = bytearray(census.carve_bytes(text, 'delta_rule_code[]'))
        self.imm = census.carve_bytes(text, 'delta_rule_imm[]')
        self.syms = census.carve_syms(text)
        self.entries = census.carve_strings(text, 'delta_rule_entry_name[]')
        self.rules = carve_rules_full(text)
        self.blobs = {k: bytearray(v) for k, v in
                      census.carve_blobs(open(CONSTS_C).read()).items()}
        self.c = census.Code(self.code, self.entries, self.imm, self.syms)
        self.touched = False
        # Which statement each rule's records go into, worked out once: it
        # costs a walk of every arm and nothing about it changes as records
        # are rewritten.
        self.fields = {}

    def index_of(self, name):
        for i, r in enumerate(self.rules):
            if r[0] == name:
                return i
        return None

    # ---- growing the pools ----------------------------------------------

    def add_imm(self, value):
        v = value & 0xffffffff
        for i, x in enumerate(self.imm):
            if (x & 0xffffffff) == v:
                return i
        self.imm.append(value)
        self.touched = True
        return len(self.imm) - 1

    def add_sym(self, blob, off):
        for i, (b, o) in enumerate(self.syms):
            if b == blob and o == off:
                return i
        self.syms.append((blob, off))
        self.touched = True
        return len(self.syms) - 1

    def add_record(self, blob, data):
        """A record of its own at the end of the blob. Records are never
        edited where they lie, because several arms can name the same bytes
        and one can be the tail of another."""
        body = self.blobs[blob]
        # Eight-aligned, which is how they already sit, and a gap after so a
        # reader that trusts the terminator rather than the count still stops.
        while len(body) % 8:
            body.append(0)
        at = len(body)
        body.extend(data)
        body.append(0)
        self.touched = True
        return at

    def set16(self, at, value):
        self.code[at] = value & 0xff
        self.code[at + 1] = (value >> 8) & 0xff
        self.touched = True

    def append_block(self, rule, data):
        """Bytes at the end of one rule. Everything after it in the code array
        shifts, so the rule table moves with it; nothing inside any rule does,
        since a target is counted from its own rule's start."""
        row = self.rules[rule]
        at = row[2] + row[3]
        where = row[3]
        if where + len(data) > TARGET_MAX:
            raise ValueError('%s has grown past what a target can reach'
                             % row[0])
        self.code[at:at] = data
        row[3] += len(data)
        for i, other in enumerate(self.rules):
            if i != rule and other[2] >= at:
                other[2] += len(data)
        self.touched = True
        return where

    # ---- writing back ----------------------------------------------------

    def save(self):
        text = open(RULES_C).read()
        text = splice(text, 'delta_rule_code[]',
                      as_c(self.code, 16))
        text = splice(text, 'delta_rule_imm[]',
                      as_c(self.imm, 8))
        text = splice(text, 'delta_rule_sym[]',
                      '\n'.join('    %s + %d,' % (b, o) for b, o in self.syms))
        text = splice(text, 'delta_rules[]',
                      '\n'.join('    { "%s", "%s", %d, %d, %d, %d, %d },' % tuple(r)
                                for r in self.rules))
        open(RULES_C, 'w').write(text)

        consts = open(CONSTS_C).read()
        for name, body in self.blobs.items():
            consts = re.sub(r'uint8_t %s\[\d+\]' % re.escape(name),
                            'uint8_t %s[%d]' % (name, len(body)), consts)
            consts = splice(consts, '%s[' % name, as_c(body, 16))
        open(CONSTS_C, 'w').write(consts)


# The three caller-saved registers. A call leaves the others alone, which is
# what the compiled code relies on when it works a value out before a call and
# pushes it after.
CLOBBERED = (0, 1, 2)

# How a value was got at, and therefore how to change it. 'id' means the
# constant is the value; 'low8' and 'high8' mean it is one byte of a constant
# that carries something else in the other.
SHAPES = ('id', 'low8', 'high8')


class Value:
    """A number the arm works out, and where it came from: the instruction
    that introduced the constant, where that constant's operand lies, what
    the constant was whole, and which part of it this is."""

    def __init__(self, n, at=None, where=None, orig=None, shape='id'):
        self.n = n
        self.at = at
        self.where = where
        self.orig = orig
        self.shape = shape

    def repack(self, n):
        """The whole constant with this part of it changed."""
        if self.shape == 'id':
            return n
        if self.shape == 'low8':
            return (self.orig & ~0xff) | (n & 0xff)
        if self.shape == 'high8':
            return (self.orig & ~0xff00) | ((n & 0xff) << 8)
        return None

    def __repr__(self):
        return 'Value(%r, %r, %r)' % (self.n, self.at, self.shape)


def derive(v, shape):
    """The same constant seen through a mask or a shift."""
    if v is None or v.n is None:
        return None
    keep = v.shape == 'id'
    if shape == 'low8':
        return Value(v.n & 0xff, v.at, v.where, v.orig,
                     'low8' if keep else None)
    if shape == 'high8':
        return Value((v.n >> 8) & 0xff, v.at, v.where, v.orig,
                     'high8' if keep else None)
    return v


class Arms:
    """One dictionary rule's dispatch."""

    def __init__(self, rules, name):
        self.rules = rules
        self.name = name
        self.index = rules.index_of(name)
        self.ok = False
        if self.index is None:
            return

        row = rules.rules[self.index]
        self.start, self.length = row[2], row[3]
        self.insns = rules.c.decode(self.start, self.length)

        self.arms, self.switch_at = self._switch()
        if not self.arms:
            return
        # A rule can hold the length in a frame slot, in a byte of a packed
        # immediate, or in the name of the wrapper it calls. Only the first
        # needs a slot, so not finding one is not a failure.
        self.slot = self._count_slot()
        self.default = self._default() if self.slot is not None else None
        self.template = self._template() if self.slot is not None else 'movb'
        self._owners = None
        # Worked out with nothing filtered, since the filtering depends on it.
        self.field = None
        if name not in rules.fields:
            rules.fields[name] = self._field()
        self.field = rules.fields[name]
        self.ok = True

    # ---- reading ---------------------------------------------------------

    def _switch(self):
        arms, at = None, None
        for off in sorted(self.insns):
            shape, vals, ops, targets, size = self.insns[off]
            if shape[0] == 'switch' and (arms is None or
                                         len(targets) > len(arms)):
                arms, at = targets, off
        return arms, at

    def arm_slots(self):
        """Where the switch keeps each arm's target, so one can be repointed."""
        p = self.start + self.switch_at
        _name, _val, _where, q = self.rules.c.operand(p + 1)
        return [q + 2 + 2 * i for i in range(len(self.arms))]

    def _count_slot(self):
        """Which frame slot the length is pushed from, where it is pushed
        from one at all. Only the writer for that shape needs it."""
        run = []
        for off in sorted(self.insns):
            shape, vals, ops, targets, size = self.insns[off]
            if shape[0] == 'push':
                run.append(ops[0])
            elif shape[0] == 'call' and shape[1].startswith('insert_'):
                if len(run) >= 3 and run[2][0] == 'slot':
                    return run[2][1]
                return None
            else:
                run = []
        return None

    # ---- working out what an arm hands the insert ------------------------

    def _load(self, op, state, movk='movl', at=None):
        kind, val, where = op
        regs, slots = state
        if kind == 'imm':
            v = (Value(self.rules.imm[val], at, where, self.rules.imm[val])
                 if val < len(self.rules.imm) else None)
        elif kind == 'reg':
            v = regs[val]
        elif kind == 'slot':
            v = slots.get(val)
        else:
            v = None
        if movk in ('movb', 'movsbl', 'movzbl'):
            v = derive(v, 'low8')
        elif movk in ('movw', 'movswl', 'movzwl'):
            v = (Value(v.n & 0xffff, v.at, v.where, v.orig, None)
                 if v and v.n is not None else None)
        return v

    def _step(self, off, state, pushes):
        """One instruction's effect on the registers and slots. Anything not
        worked out becomes unknown, which is safe: it only means a length
        cannot be read rather than that a wrong one is."""
        regs, slots = state
        shape, vals, ops, targets, size = self.insns[off]
        op = shape[0]

        if op == 'load':
            v = self._load(ops[0], state, shape[1], off)
            regs[vals[0]] = v
        elif op == 'store':
            v = self._load(ops[0], state, shape[1], off)
            if ops[1][0] == 'slot':
                slots[ops[1][1]] = v
            elif ops[1][0] == 'reg':
                regs[ops[1][1]] = v
        elif op == 'alu2':
            a = self._load(ops[0], state, 'movl', off)
            b = self._load(ops[1], state, 'movl', off)
            sub = shape[1]
            out = None
            if a is not None and a.n is not None and b is not None \
                    and b.n is not None:
                if sub.startswith('and') and a.n == 0xff:
                    out = derive(b, 'low8')
                elif sub in ('sarw', 'sarl') and a.n == 8:
                    out = derive(b, 'high8')
                else:
                    n = {'addl': b.n + a.n, 'addw': b.n + a.n,
                         'subl': b.n - a.n, 'subw': b.n - a.n,
                         'andl': b.n & a.n, 'andw': b.n & a.n,
                         'orl': b.n | a.n, 'orw': b.n | a.n,
                         'shll': b.n << a.n, 'shlw': b.n << a.n,
                         'sarl': b.n >> a.n, 'sarw': b.n >> a.n,
                         'imull': b.n * a.n}.get(sub)
                    out = Value(n, None, None, None, None) if n is not None else None
            if ops[1][0] == 'reg':
                regs[ops[1][1]] = out
            elif ops[1][0] == 'slot':
                slots[ops[1][1]] = out
        elif op == 'push':
            pushes.append(self._load(ops[0], state, 'movl', off))
        elif op in ('alu1', 'setcc', 'widen', 'map', 'scale', 'addk', 'mul',
                    'div', 'popreg'):
            for kind, val, _w in ops:
                if kind == 'reg':
                    regs[val] = None
                elif kind == 'slot':
                    slots[val] = None
            if op in ('load', 'map', 'scale', 'addk', 'mul', 'popreg') and vals:
                if isinstance(vals[0], int) and vals[0] < len(regs):
                    regs[vals[0]] = None

    def _state0(self):
        """What the prologue leaves behind, which is where every arm starts.
        The prologue is everything before the first jump target."""
        stop = self._first_landing()
        state = ([None] * 8, {})
        pushes = []
        for off in sorted(self.insns):
            if off >= stop:
                break
            shape = self.insns[off][0]
            if shape[0] == 'call':
                for r in CLOBBERED:
                    state[0][r] = None
                pushes = []
                continue
            self._step(off, state, pushes)
        return state

    def _run(self, at):
        """What one arm hands the call that lays its record down: the record,
        the length, and where the length came from.

        An arm may test something and take one of two ways -- an abbreviation
        read one way before a name and another before a number. Both ways are
        followed, and an answer is only given when they agree or when only one
        of them lays a record down at all. Anything less certain than that
        answers nothing, since a wrong pronunciation written back would look
        exactly like a right one."""
        regs, slots = self._state0()
        found = []
        self._walk(at, (list(regs), dict(slots)), [], None, set(), 0, found,
                   False)
        if not found:
            return None, None, None

        def key(f):
            sym, length, where, field = f
            return (sym[0] if sym else None,
                    length.n if length else None)

        first = key(found[0])
        if any(key(f) != first for f in found[1:]):
            return None, None, None
        return found[0][:3]

    def _walk(self, at, state, pushes, sym, seen, depth, found, pieces=False):
        """One way through an arm, collecting what it hands over. A branch
        splits into two ways, each explored on its own copy of the machine."""
        p = at
        steps = 0

        while p in self.insns and p not in seen and steps < 200:
            seen.add(p)
            steps += 1
            shape, vals, ops, targets, size = self.insns[p]

            for kind, val, _w in ops:
                # The record is whichever was named last before the call, not
                # the first named: an arm that lays several down names each
                # just before the call that takes it.
                if kind == 'sym' and val < len(self.rules.syms):
                    sym = (self.rules.syms[val], p, _w)

            if shape[0] == 'call':
                name = shape[1]
                if 'insert' in name:
                    length = field = None
                    got = None
                    if name.startswith('insert_'):
                        length = pushes[2] if len(pushes) > 2 else None
                        field = (pushes[3].n if len(pushes) > 3 and pushes[3]
                                 else None)
                    elif self.rules.index_of(name) is not None:
                        got = self._wrapper(name)
                        if got:
                            field = got[2]
                        length = Arms(self.rules, name)._pushed()

                    if length is None or length.n is None:
                        # An insert this cannot read. Skipping it is the only
                        # honest thing, and it means whatever is found later
                        # is not the whole of what the word says.
                        pieces = True
                        for r in CLOBBERED:
                            state[0][r] = None
                        pushes = []
                        p += size
                        continue

                    if (self.field is not None and field is not None
                            and field != self.field):
                        # Laid into another statement, so it is a mark the
                        # rule leaves on its way, not what the word says.
                        for r in CLOBBERED:
                            state[0][r] = None
                        pushes = []
                        p += size
                        continue

                    # An arm that lays a word down in pieces -- the currencies
                    # spell out an abbreviation, then a space, then a name --
                    # is not one record and is not reported as one.
                    if pieces:
                        return
                    # A wrapper may name the record itself rather than take
                    # one, which is how a suffix that always says the same
                    # thing is written; there is then nothing in the arm
                    # naming it, and nothing of the word's to change.
                    if sym is None and got and got[0] is not None:
                        sym = (got[0], None, None)
                    found.append((sym, length,
                                  ('here', p) if name.startswith('insert_')
                                  else ('wrapper', p, name), field))
                    return
                for r in CLOBBERED:
                    state[0][r] = None
                pushes = []
                p += size
                continue

            if shape[0] == 'jump':
                p = targets[0]
                continue
            if shape[0] in ('return', 'switch'):
                return
            if shape[0] == 'branch':
                if depth < 6:
                    self._walk(targets[0], (list(state[0]), dict(state[1])),
                               list(pushes), sym, set(seen), depth + 1, found,
                               pieces)
                p += size
                continue

            self._step(p, state, pushes)
            p += size

    def _field(self):
        """Which statement this rule's records go into. A rule may also leave
        a mark in another statement as it goes, and a mark is not what the word
        says. The vote is over the records the arm names for itself, which are
        certainly the word's own; a wrapper naming its own record could be
        either."""
        votes = {}
        for at in self.arms:
            found = []
            regs, slots = self._state0()
            self._walk(at, (list(regs), dict(slots)), [], None, set(), 0,
                       found, False)
            for sym, length, where, field in found:
                if field is None or sym is None or sym[1] is None:
                    continue
                votes[field] = votes.get(field, 0) + 1
        return max(votes, key=votes.get) if votes else None

    def _wrapper(self, name):
        """What one of the rule's own laying-down wrappers does: the record it
        names itself, if it names one, and how long a record it lays. A
        wrapper that names its own is a fixed piece -- a space between two
        words, say -- shared by everything that calls it."""
        if self.rules.index_of(name) is None:
            return None
        inner = Arms(self.rules, name)
        state = ([None] * 8, {})
        pushes, fixed = [], None
        for off in sorted(inner.insns):
            shape, vals, ops, targets, size = inner.insns[off]
            for kind, val, _w in ops:
                if kind == 'sym' and val < len(self.rules.syms):
                    fixed = self.rules.syms[val]
            if shape[0] == 'call':
                if 'insert' in shape[1]:
                    length = pushes[2] if len(pushes) > 2 else None
                    field = pushes[3] if len(pushes) > 3 else None
                    return (fixed, (length.n if length else None),
                            (field.n if field else None))
                pushes = []
                continue
            if shape[0] in ('jump', 'branch', 'return', 'switch'):
                return None
            inner._step(off, state, pushes)
        return None

    def ways(self, act):
        """The readings an action has where it tests something and takes one of
        two paths -- an abbreviation read one way before a name and another
        before a number. Each is a record of its own and can be changed on its
        own. The straight way through comes first, which is the order they are
        written in and read back."""
        if not self.ok or not 1 <= act <= len(self.arms):
            return None
        regs, slots = self._state0()
        found = []
        self._walk(self.arms[act - 1], (list(regs), dict(slots)), [], None,
                   set(), 0, found, False)

        out, seen = [], set()
        for sym, length, where, field in reversed(found):
            if sym is None or length is None or length.n is None:
                return None
            key = (sym[0], length.n)
            if key in seen:
                continue
            seen.add(key)
            out.append(Part(sym[0][0], sym[0][1], length.n, sym[1], sym[2],
                            where[1], False))
        return out or None

    def parts(self, act):
        """What one action lays down, in order. Most lay down one thing; the
        currencies spell an abbreviation, then a space, then a name, and that
        is three. Answers nothing where any of it cannot be read."""
        if not self.ok or not 1 <= act <= len(self.arms):
            return None
        regs, slots = self._state0()
        state = (list(regs), dict(slots))
        pushes, out = [], []
        sym = None
        seen = set()
        p = self.arms[act - 1]
        steps = 0

        while p in self.insns and p not in seen and steps < 300:
            seen.add(p)
            steps += 1
            shape, vals, ops, targets, size = self.insns[p]

            for kind, val, _w in ops:
                if kind == 'sym' and val < len(self.rules.syms):
                    sym = (self.rules.syms[val], p, _w)

            if shape[0] == 'call':
                name = shape[1]
                if 'insert' in name:
                    if name.startswith('insert_'):
                        length = pushes[2] if len(pushes) > 2 else None
                        field = pushes[3] if len(pushes) > 3 else None
                        here = field.n if field else None
                        if out and here is not None and here != out[0][1]:
                            for r in CLOBBERED:
                                state[0][r] = None
                            pushes = []
                            p += size
                            continue
                        if sym is None or length is None or length.n is None:
                            return None
                        out.append((Part(sym[0][0], sym[0][1], length.n,
                                         sym[1], sym[2], p, False), here))
                    else:
                        got = self._wrapper(name)
                        if got is None:
                            return None
                        fixed, length, field = got
                        if length is None:
                            return None
                        if out and field is not None and field != out[0][1]:
                            # A record laid into another statement is a mark
                            # the rule leaves, not what the word says.
                            for r in CLOBBERED:
                                state[0][r] = None
                            pushes = []
                            p += size
                            continue
                        if fixed is not None:
                            out.append((Part(fixed[0], fixed[1], length,
                                             None, None, p, True), field))
                        elif sym is not None:
                            out.append((Part(sym[0][0], sym[0][1], length,
                                             sym[1], sym[2], p, False), field))
                        else:
                            return None
                    sym = None
                for r in CLOBBERED:
                    state[0][r] = None
                pushes = []
                p += size
                continue

            if shape[0] == 'jump':
                p = targets[0]
                continue
            if shape[0] in ('return', 'switch'):
                break
            if shape[0] == 'branch':
                # One way only, and only once the pieces are all named.
                break

            self._step(p, state, pushes)
            p += size

        return [part for part, _field in out] or None

    def _reach(self, at, limit=200):
        """Every instruction one arm can run through on its way to laying its
        record down, both ways at a branch."""
        seen, stack = set(), [at]
        while stack and len(seen) < limit:
            p = stack.pop()
            if p in seen or p not in self.insns:
                continue
            seen.add(p)
            shape, vals, ops, targets, size = self.insns[p]
            if shape[0] == 'call' and 'insert_2pt_s' in shape[1]:
                continue
            if shape[0] in ('return', 'switch'):
                continue
            if shape[0] == 'jump':
                stack.append(targets[0])
                continue
            if shape[0] == 'branch':
                stack.append(targets[0])
            stack.append(p + size)
        return seen

    def owners(self):
        """How many arms can run through each instruction. Only what one arm
        alone can reach may be patched, since anything else would change what
        another word says."""
        if self._owners is None:
            count = {}
            for at in self.arms:
                for p in self._reach(at):
                    count[p] = count.get(p, 0) + 1
            self._owners = count
        return self._owners

    def _pushed(self):
        """A wrapper rule's own third argument, which is a constant."""
        state = ([None] * 8, {})
        pushes = []
        for off in sorted(self.insns):
            shape, vals, ops, targets, size = self.insns[off]
            if shape[0] == 'call':
                if 'insert_2pt_s' in shape[1]:
                    return pushes[2] if len(pushes) > 2 else None
                pushes = []
                continue
            if shape[0] in ('jump', 'branch', 'return', 'switch'):
                return None
            self._step(off, state, pushes)
        return None

    def _first_landing(self):
        landing = set()
        for off in self.insns:
            landing.update(self.insns[off][3])
        return min(landing) if landing else max(self.insns)

    def _default(self):
        """What the prologue leaves the length at, for the arms that say
        nothing about it."""
        stop = self._first_landing()
        value = None
        for off in sorted(self.insns):
            if off >= stop:
                break
            v = self._count_store(off)
            if v is not None and v[0] == 'imm':
                value = v[1]
        return value

    def _template(self):
        """An arm that sets the length to an immediate, to copy the shape of
        when writing one that did not."""
        for at in self.arms:
            for off in self._flow(at):
                v = self._count_store(off)
                if v is not None and v[0] == 'imm':
                    return self.insns[off][0][1]
        return 'movb'

    def _count_store(self, off):
        """(kind, value) when this instruction writes the length slot."""
        shape, vals, ops, targets, size = self.insns[off]
        if shape[0] != 'store' or len(ops) != 2:
            return None
        if ops[1][0] != 'slot' or ops[1][1] != self.slot:
            return None
        if ops[0][0] == 'imm' and ops[0][1] < len(self.rules.imm):
            return ('imm', self.rules.imm[ops[0][1]])
        return (ops[0][0], None)

    def _flow(self, at, limit=40):
        """The instructions an arm runs through before the work begins,
        following the jumps it shares with other arms."""
        seen, out, p = set(), [], at
        while p in self.insns and p not in seen and len(out) < limit:
            seen.add(p)
            shape = self.insns[p][0]
            out.append(p)
            if shape[0] == 'jump':
                p = self.insns[p][3][0]
                continue
            if shape[0] in ('branch', 'return', 'switch', 'call'):
                break
            p += self.insns[p][4]
        return out

    def _continuation(self, at):
        """Where an arm's own setup gives out and the shared work begins,
        which is what a replacement block has to jump to.

        It has to come after the record is named. An arm whose target lands in
        the middle of a shared block does its naming further along, and a
        block that jumped back to the start of it would have the original
        undo everything the replacement had just done, so that answers
        nothing and the arm is left alone."""
        named = False
        for p in self._flow(at):
            shape, vals, ops, targets, size = self.insns[p]
            if any(o[0] == 'sym' for o in ops):
                named = True
                continue
            if self._count_store(p) is not None:
                continue
            if shape[0] == 'jump':
                continue
            return p if named else None
        return None

    def read(self, act):
        """What action number `act' lays down, or None."""
        if not self.ok or not 1 <= act <= len(self.arms):
            return None
        at = self.arms[act - 1]
        sym, length, where = self._run(at)
        if sym is None or length is None or length.n is None:
            return None

        (blob, off), sym_at, sym_where = sym
        if sym_at is None:
            # Named by the rule that lays it, so there is no instruction of
            # this word's to point somewhere else.
            return Record(blob, off, length.n, 'fixed', (None, None, None),
                          (length, where), self._continuation(at),
                          self.arm_slots()[act - 1])
        size = self.insns[sym_at][4]
        how = {'here': 'slot', 'wrapper': 'wrapper'}.get(where[0], '?')
        if where[0] == 'here' and length.at is not None:
            how = 'packed'
        return Record(blob, off, length.n, how,
                      (sym_at, size, sym_where), (length, where),
                      self._continuation(at), self.arm_slots()[act - 1])

    def records(self):
        """Every action's record. An arm whose length cannot be worked out is
        left out rather than guessed at, since a wrong length would be written
        back as though it were what the engine says."""
        out = {}
        for act in range(1, len(self.arms) + 1):
            r = self.read(act)
            if r is not None:
                out[act] = r
        return out

    # ---- writing ---------------------------------------------------------

    def _alone(self, at):
        """Whether only this arm can run through an instruction, which is the
        condition for patching it: anything shared would change what some
        other word says."""
        return self.owners().get(at, 2) == 1

    def _bound(self):
        """Where the rule keeps the largest action it will dispatch. It is
        the immediate of the comparison just before the switch, and it counts
        from zero, so it is one less than the number of arms."""
        want = len(self.arms) - 1
        # Once a switch has been widened it sits at the end of the rule and
        # is reached by a jump from where it used to be, so the check is
        # before that jump rather than before the switch itself.
        stop = self.switch_at
        for off in sorted(self.insns):
            shape = self.insns[off][0]
            if shape[0] == 'jump' and self.insns[off][3][0] == self.switch_at:
                stop = min(stop, off)
        best = None
        for off in sorted(self.insns):
            if off >= stop:
                break
            shape, vals, ops, targets, size = self.insns[off]
            if shape[0] != 'cmp' or not ops or ops[0][0] != 'imm':
                continue
            if ops[0][1] < len(self.rules.imm) \
                    and self.rules.imm[ops[0][1]] == want:
                best = ops[0][2]
        return best

    def spare(self, used):
        """An arm the dictionary never dispatches to, which a new word can be
        given without touching anything. Only one that already lays a record
        down is any use, since that is what tells us how this rule does it."""
        got = self.records()
        for i in range(1, len(self.arms) + 1):
            if (i not in used and i in got and got[i].cont is not None
                    and got[i].sym_insn[0] is not None):
                return i
        return None

    def routes(self):
        """Where an arm can be sent for each length this rule already lays a
        record down in. A rule that says the length by which wrapper it calls
        has one of these per length, and a new arm reaches a length by being
        pointed at the one that already does it."""
        out = {}
        for act, r in self.records().items():
            if r.cont is not None and r.sym_insn[0] is not None:
                out.setdefault(r.length, r)
        return out

    def add_arm(self, data):
        """A new action, laying down a record of its own.

        The switch cannot be grown where it stands without pushing everything
        after it along, so a wider copy of it goes at the end of the rule and
        the old one is replaced by a jump to it. The bytes of the old switch
        after that jump are never reached again. Answers the new action
        number.
        """
        by_length = self.routes()
        if not by_length:
            raise ValueError('%s has no arm to copy the shape of' % self.name)

        # Found before anything is written, since a rule whose check cannot be
        # raised must be left exactly as it was.
        bound = self._bound()
        if bound is None:
            raise ValueError('%s does not check its action against a number '
                             'this recognises, so a new arm cannot be reached'
                             % self.name)

        if self.slot is not None:
            # The arm can state the length itself, so any arm will do to copy.
            r = by_length[min(by_length)]
        elif len(data) in by_length:
            # It cannot, so it has to be sent where that length is already
            # laid down, and only lengths the language once compiled exist.
            r = by_length[len(data)]
        else:
            raise ValueError('%s says how long a record is by which of its '
                             'own it calls, so a new one has to be a length '
                             'it already lays down, and %d is not one of %s'
                             % (self.name, len(data),
                                ', '.join(str(n) for n in
                                          sorted(by_length))))

        off = self.rules.add_record(r.blob, data)
        sym = self.rules.add_sym(r.blob, off)

        arm = bytearray()
        if self.slot is not None:
            imm = self.rules.add_imm(len(data))
            arm += bytes([OP_STORE, census.MOVK.index(self.template),
                          K_IMM, imm & 0xff, (imm >> 8) & 0xff,
                          K_SLOT, self.slot & 0xff, (self.slot >> 8) & 0xff])
        at, size, where = r.sym_insn
        copy = bytearray(self.rules.code[self.start + at:
                                         self.start + at + size])
        inside = where - (self.start + at)
        copy[inside] = sym & 0xff
        copy[inside + 1] = (sym >> 8) & 0xff
        arm += copy
        arm += bytes([OP_JUMP, r.cont & 0xff, (r.cont >> 8) & 0xff])
        where_arm = self.rules.append_block(self.index, arm)

        # The rule checks the action against the number of arms before it
        # dispatches, so the check has to be raised too or the new one is
        # thrown out before the switch ever sees it.
        self.rules.set16(bound, self.rules.add_imm(len(self.arms)))

        # The switch again, one arm wider, reading the same operand.
        head = self.start + self.switch_at
        _name, _val, _w, after = self.rules.c.operand(head + 1)
        table = bytearray()
        table.append(census.OPS.index('switch'))
        table += self.rules.code[head + 1:after]
        n = len(self.arms) + 1
        table += bytes([n & 0xff, (n >> 8) & 0xff])
        for t in self.arms:
            table += bytes([t & 0xff, (t >> 8) & 0xff])
        table += bytes([where_arm & 0xff, (where_arm >> 8) & 0xff])
        where_table = self.rules.append_block(self.index, table)

        # The old switch is jumped over rather than removed, and what is left
        # of it is filled with instructions that decode but are never reached.
        # Execution would not care, but anything reading the rule from its
        # first byte would lose its place in the middle of an instruction.
        spare = self.insns[self.switch_at][4] - 3
        filler = bytearray()
        if spare % 2:
            filler += bytes([OP_JUMP, 0, 0])
            spare -= 3
        if spare < 0:
            raise ValueError('%s has no room to jump past its switch'
                             % self.name)
        filler += bytes([census.OPS.index('widen'), 0]) * (spare // 2)

        self.rules.code[head] = OP_JUMP
        self.rules.set16(head + 1, where_table)
        self.rules.code[head + 3:head + 3 + len(filler)] = filler

        act = n
        self.__init__(self.rules, self.name)
        return act

    def rewrite_parts(self, act, datas):
        """Change what an action laid down in pieces. Each piece is its own
        record and is changed on its own; a piece the wrapper names rather
        than the arm belongs to every word that uses that wrapper and is not
        one word's to change."""
        got = self.parts(act)
        if got is None:
            raise ValueError('%s action %d lays down nothing this can read'
                             % (self.name, act))
        if len(got) != len(datas):
            raise ValueError('%s action %d lays down %d piece(s), not %d; a '
                             'piece cannot be added or taken away'
                             % (self.name, act, len(got), len(datas)))

        for part, data in zip(got, datas):
            body = self.rules.blobs.get(part.blob, b'')
            if bytes(body[part.off:part.off + part.length]) == data:
                continue
            if part.fixed:
                raise ValueError('%s action %d: that piece is named by the '
                                 'rule that lays it and is shared with every '
                                 'word using it, so it is not this word\'s to '
                                 'change' % (self.name, act))
            if not self._alone(part.sym_at):
                raise ValueError('%s action %d names one of its pieces in code '
                                 'other words run through'
                                 % (self.name, act))

            off = self.rules.add_record(part.blob, data)
            sym = self.rules.add_sym(part.blob, off)

            if len(data) != part.length:
                name = self.insns[part.call_at][0][1]
                m = WRAPPER.match(name) or LAYER.match(name)
                if not (m and self._alone(part.call_at)):
                    raise ValueError('%s action %d cannot have that piece made '
                                     'a different length' % (self.name, act))
                wanted = '%s%d' % (m.group(1), len(data))
                if wanted not in self.rules.entries:
                    raise ValueError('%s action %d would need %s, which the '
                                     'language never compiled'
                                     % (self.name, act, wanted))
                self.rules.set16(self.start + part.call_at + 1,
                                 self.rules.entries.index(wanted))
            self.rules.set16(part.sym_where, sym)

    def rewrite(self, act, data):
        """Give one action a record of its own, and the length to go with it.

        The record is always appended rather than written over, so nothing
        that names the old bytes is disturbed. The length is set whichever way
        this rule expresses one, and if none of them can be reached without
        touching code another arm runs through, nothing is written and the
        reason is raised."""
        r = self.read(act)
        if r is None:
            raise ValueError('%s action %d lays down no record'
                             % (self.name, act))

        sym_at, sym_size, sym_where = r.sym_insn
        length, where = r.count_insn
        if sym_at is None:
            raise ValueError('%s action %d has its record named by the rule '
                             'that lays it rather than by the word, so there '
                             'is nothing of this word\'s to change'
                             % (self.name, act))

        off = self.rules.add_record(r.blob, data)
        sym = self.rules.add_sym(r.blob, off)

        # Nothing to say about the length: just name the new record.
        if len(data) == r.length:
            if not self._alone(sym_at):
                raise ValueError(
                    '%s action %d names its record in code other words run '
                    'through' % (self.name, act))
            self.rules.set16(sym_where, sym)
            return

        # A rule that pushes the length out of a frame slot can have the whole
        # arm replaced, which is the cleanest way in and needs nothing shared.
        if self.slot is not None and r.cont is not None:
            imm = self.rules.add_imm(len(data))
            block = bytearray()
            block += bytes([OP_STORE, census.MOVK.index(self.template),
                            K_IMM, imm & 0xff, (imm >> 8) & 0xff,
                            K_SLOT, self.slot & 0xff, (self.slot >> 8) & 0xff])
            copy = bytearray(self.rules.code[self.start + sym_at:
                                             self.start + sym_at + sym_size])
            inside = sym_where - (self.start + sym_at)
            copy[inside] = sym & 0xff
            copy[inside + 1] = (sym >> 8) & 0xff
            block += copy
            block += bytes([OP_JUMP, r.cont & 0xff, (r.cont >> 8) & 0xff])
            at_block = self.rules.append_block(self.index, block)
            self.rules.set16(r.arm_slot, at_block)
            return

        if not self._alone(sym_at):
            raise ValueError('%s action %d names its record in code other '
                             'words run through' % (self.name, act))

        # A rule that says the length by which wrapper it calls can be moved
        # to the wrapper for the length wanted, if one was ever compiled.
        if where and where[0] == 'wrapper':
            call_at = where[1]
            m = WRAPPER.match(where[2])
            if m and self._alone(call_at):
                wanted = '%s%d' % (m.group(1), len(data))
                if wanted in self.rules.entries:
                    idx = self.rules.entries.index(wanted)
                    self.rules.set16(self.start + call_at + 1, idx)
                    self.rules.set16(sym_where, sym)
                    return
                raise ValueError(
                    '%s action %d would need %s, which the language never '
                    'compiled' % (self.name, act, wanted))

        # A rule that packs the length into a byte of a constant can have the
        # constant replaced, the other byte kept as it was.
        if length is not None and length.shape in SHAPES \
                and length.at is not None and self._alone(length.at):
            whole = length.repack(len(data))
            if whole is not None:
                self.rules.set16(length.where, self.rules.add_imm(whole))
                self.rules.set16(sym_where, sym)
                return

        raise ValueError('%s action %d states its length a way that cannot be '
                         'changed without touching code other words run '
                         'through' % (self.name, act))


def as_c(data, per_line):
    lines = []
    for i in range(0, len(data), per_line):
        lines.append('    ' + ','.join(str(b) for b in data[i:i + per_line]))
    # Every line ends in a comma, the last one included, which is how the
    # generator writes them.
    return ',\n'.join(lines) + ','


def splice(text, name, body):
    at = text.index(name)
    open_at = text.index('{', at)
    close_at = text.index('\n};', open_at)
    return text[:open_at + 1] + '\n' + body + text[close_at:]
