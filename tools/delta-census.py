#!/usr/bin/env python3
"""How repetitive the lifted rules are, and therefore how big a decompiler is.

The rules are IBM's machine code re-encoded, but that code was written by
IBM's own Delta compiler rather than by a person, so it should be made of a
small number of templates. This counts them three ways, because the three
answer different questions:

  by block, an exact instruction sequence, which is what a naive matcher
  would have to recognise and is the pessimistic figure;

  by instruction, which says how small the vocabulary is;

  by call idiom, the sequence of runtime calls a block makes with the
  register shuffling between them ignored, which is the optimistic figure
  and the one a decompiler that understands the primitives would see.

With a rule named it dumps that rule instead, resolving symbols to the bytes
they point at, which is how one finds out whether the lexicons come back as
words.
"""

import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Which language's transcription to read. English lives in src because it is
# the one the engine is built from; the rest are lifted alongside it, and
# pointing the tools at one of those is how the machine model gets held
# against a language it was not worked out on.
LANG_DIR = os.environ.get('EVV_LANG_DIR',
                           os.path.join(ROOT, 'lang', 'enus'))
# The files are named for their language, and the directory is named for it
# too, so which language this is comes from the directory.
LANG_TAG = os.path.basename(LANG_DIR.rstrip('/\\'))
RULES_C = os.path.join(LANG_DIR, 'delta_rules_%s.c' % LANG_TAG)
CONSTS_C = os.path.join(LANG_DIR, 'delta_consts_%s.c' % LANG_TAG)

OPS = ['call', 'jump', 'branch', 'cmp', 'alu2', 'alu1', 'load',
       'store', 'switch', 'map', 'return', 'scale', 'addk', 'mul',
       'div', 'widen', 'setcc', 'push', 'setarg', 'popn', 'popreg',
       'ftol']

KINDS = ['none', 'imm', 'sym', 'slot', 'slotaddr', 'state', 'statefld',
         'reg', 'ind']

COND = ['e', 'ne', 'a', 'ae', 'b', 'be', 'g', 'ge', 'l', 'le', 's', 'ns']

CMPK = ['testl', 'testw', 'testb', 'cmpl', 'cmpw', 'cmpb']

ALUK = ['addl', 'addw', 'subl', 'subw', 'andl', 'andw', 'orl', 'orw',
        'incl', 'incw', 'decl', 'decw', 'shll', 'shlw', 'sarl', 'sarw',
        'negl', 'negw', 'sbbl', 'imull', 'imulw']

MOVK = ['movl', 'movw', 'movb', 'movswl', 'movzwl', 'movsbl', 'movzbl']

REGS = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi']


def span(text, name):
    at = text.index(name)
    open_at = text.index('{', at)
    close_at = text.index('\n};', open_at)
    return text[open_at + 1:close_at]


def carve_bytes(text, name):
    """The generated arrays are megabytes of decimal, so this splits the
    whole span at once rather than matching per element."""
    return [int(v) for v in span(text, name).replace('\n', '').split(',')
            if v.strip()]


def carve_strings(text, name):
    return re.findall(r'"((?:[^"\\]|\\.)*)"', span(text, name))


def carve_syms(text):
    """Each entry is a blob and an offset into it, written as an addition."""
    out = []
    for line in span(text, 'delta_rule_sym[]').split(','):
        line = line.strip()
        if not line:
            continue
        m = re.match(r'([A-Za-z_][A-Za-z0-9_]*)\s*(?:\+\s*(\d+))?$', line)
        out.append((m.group(1), int(m.group(2) or 0)) if m else (line, 0))
    return out


def carve_rules(text):
    out = []
    for m in re.finditer(r'\{\s*"([^"]*)",\s*"([^"]*)",\s*(-?\d+),\s*(-?\d+),'
                         r'\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\}',
                         span(text, 'delta_rules[]')):
        out.append((m.group(1), m.group(2), int(m.group(3)), int(m.group(4))))
    return out


def carve_blobs(text):
    out = {}
    # The language goes in front of every name in a module, so that two of
    # them can be linked into one program, and what the rules name a blob by is
    # that whole name. Written without the prefix once, and a blob named
    # `enus_evv_...' then matched nothing at all: the pronunciations went out
    # of tools/delta-dict.py's reach and stayed there for two days.
    for m in re.finditer(r'uint8_t ([A-Za-z0-9_]*evv_[A-Za-z0-9_]*)'
                         r'\[\d+\]\s*=\s*\{', text):
        name = m.group(1)
        close_at = text.index('\n};', m.end())
        body = text[m.end():close_at].replace('\n', '')
        out[name] = bytes(int(v) for v in body.split(',') if v.strip())
    return out


class Code:
    def __init__(self, code, entries, imm, syms):
        self.code = code
        self.entries = entries
        self.imm = imm
        self.syms = syms

    def u16(self, p):
        return self.code[p] | (self.code[p + 1] << 8)

    def s16(self, p):
        v = self.u16(p)
        return v - 0x10000 if v & 0x8000 else v

    def operand(self, p):
        """(shape, value, where, next). The shape is the kind alone, except
        that an indirection carries what it is an indirection through; the
        value is kept apart so a shape can ignore it and a dump can print it,
        and where it lies is kept so that it can be patched."""
        k = self.code[p]
        name = KINDS[k]
        if k == 0:
            return name, None, None, p + 1
        if k == 7:
            return name, self.code[p + 1] & 0x0f, p + 1, p + 2
        if k == 8:
            inner, val, at, q = self.operand(p + 1)
            return 'ind(%s)' % inner, (val, self.s16(q)), (at, q), q + 2
        # An immediate and a symbol name a pool entry, so they count from
        # zero; everything else is an offset either side of a base.
        if k in (1, 2):
            return name, self.u16(p + 1), p + 1, p + 3
        return name, self.s16(p + 1), p + 1, p + 3

    def insn(self, p):
        """(shape, values, operands, targets, next). An operand is a triple of
        its kind, its value and where in the code its value lies, kept
        separate from the shape so that one can be found and patched without
        guessing which value belongs to which."""
        op = OPS[self.code[p]]
        q = p + 1
        ops = []

        def one(at):
            name, val, where, nxt = self.operand(at)
            ops.append((name, val, where))
            return name, val, nxt

        def two(subs, trail=0):
            sub = subs[self.code[q]]
            a, _av, q2 = one(q + 1)
            b, _bv, q3 = one(q2)
            return (op, sub, a, b), (), ops, [], q3 + trail

        if op == 'call':
            # Two counts follow the entry: how many arguments it takes, and
            # how deep the argument area should be here. The first is what the
            # entry gets -- constant for 3,495 of the 3,500 entries called,
            # which is what says it is the arity -- and the second grows
            # through a rule, because a call does not pop what it was given.
            which = self.u16(q)
            return (('call', self.entries[which]),
                    (self.code[q + 2], self.code[q + 3]), ops, [], q + 4)
        if op == 'jump':
            return ('jump',), (), ops, [self.u16(q)], q + 2
        if op == 'branch':
            return (('branch', COND[self.code[q]]), (), ops,
                    [self.u16(q + 1)], q + 3)
        if op == 'cmp':
            return two(CMPK)
        if op == 'alu2':
            return two(ALUK)
        if op == 'store':
            return two(MOVK)
        if op == 'mul':
            return two(ALUK, 1)
        if op == 'alu1':
            sub = ALUK[self.code[q]]
            a, _av, q2 = one(q + 1)
            return (op, sub, a), (), ops, [], q2
        if op == 'load':
            sub = MOVK[self.code[q]]
            a, _av, q2 = one(q + 1)
            return (op, sub, a), (self.code[q2] & 0x0f,), ops, [], q2 + 1
        if op == 'switch':
            a, _av, q2 = one(q)
            n = self.u16(q2)
            q2 += 2
            targets = [self.u16(q2 + 2 * i) for i in range(n)]
            return ('switch', a), (n,), ops, targets, q2 + 2 * n
        if op == 'map':
            a, _av, q2 = one(q + 2)
            return ('map', a), (self.u16(q),), ops, [], q2 + 1
        if op == 'return':
            a, _av, q2 = one(q)
            return ('return', a), (), ops, [], q2
        if op == 'scale':
            a, _av, q2 = one(q + 2)
            b, _bv, q3 = one(q2)
            return ('scale', a, b), (self.u16(q), self.code[q3]), ops, [], q3 + 2
        if op == 'addk':
            a, _av, q2 = one(q + 2)
            return ('addk', a), (self.u16(q),), ops, [], q2 + 1
        if op == 'div':
            a, _av, q2 = one(q + 1)
            return ('div',), (), ops, [], q2
        if op == 'widen':
            return ('widen',), (), ops, [], q + 1
        if op == 'setcc':
            return ('setcc', COND[self.code[q]]), (), ops, [], q + 2
        if op == 'push':
            a, _av, q2 = one(q)
            return ('push', a), (), ops, [], q2
        if op == 'setarg':
            a, _av, q2 = one(q + 1)
            return ('setarg', a), (self.code[q],), ops, [], q2
        if op == 'ftol':
            # A little floating point: a count, then that many steps, then the
            # register the truncated answer goes in. A step is a kind byte and
            # then either an operand to read an integer through or the two
            # halves of a double constant in the ordinary constant pool.
            n = self.code[q]
            q2 = q + 1
            steps = []
            for _ in range(n):
                what = self.code[q2]
                q2 += 1
                if what in (0, 1):
                    _nm, _v, q2 = one(q2)
                    steps.append((what, len(ops) - 1))
                else:
                    steps.append((what, self.u16(q2), self.u16(q2 + 2)))
                    q2 += 4
            return ('ftol', tuple(steps)), (self.code[q2],), ops, [], q2 + 1

        if op in ('popn', 'popreg'):
            return (op,), (self.code[q],), ops, [], q + 1

        raise ValueError('operation %r at %d' % (op, p))

    def decode(self, start, length):
        """Every instruction of one rule, keyed by its rule-relative offset.
        Operand positions are kept as they lie in the whole code array, since
        that is where a patch has to go."""
        out = {}
        p = 0
        while p < length:
            shape, vals, ops, targets, nxt = self.insn(start + p)
            out[p] = (shape, vals, ops, targets, nxt - start - p)
            p = nxt - start
        return out


def split(insns, length):
    """Basic blocks. A leader is the rule's first byte, anything jumped to,
    and whatever follows a transfer of control."""
    leaders = {0}
    for off, (shape, vals, ops, targets, size) in insns.items():
        leaders.update(targets)
        if shape[0] in ('jump', 'branch', 'switch', 'return'):
            if off + size < length:
                leaders.add(off + size)

    blocks, cur = [], None
    for off in sorted(insns):
        if off in leaders:
            cur = []
            blocks.append((off, cur))
        cur.append(off)
    return blocks


def render(shape):
    return '; '.join(' '.join(str(x) for x in insn) for insn in shape)


def load():
    text = open(RULES_C).read()
    c = Code(carve_bytes(text, 'delta_rule_code[]'),
             carve_strings(text, 'delta_rule_entry_name[]'),
             carve_bytes(text, 'delta_rule_imm[]'),
             carve_syms(text))
    return c, carve_rules(text)


# ---- the census ---------------------------------------------------------

def census():
    c, rules = load()

    total = 0
    blk_n, blk_b = collections.Counter(), collections.Counter()
    ins_n, ins_b = collections.Counter(), collections.Counter()
    idi_n, idi_b = collections.Counter(), collections.Counter()
    per_object = collections.defaultdict(
        lambda: [0, 0, collections.Counter(), collections.Counter()])
    calls = collections.Counter()
    rule_shapes = collections.Counter()
    per_rule = []

    for name, obj, start, length in rules:
        insns = c.decode(start, length)
        blocks = split(insns, length)
        total += length

        shapes = []
        for off, block in blocks:
            shape = tuple(insns[o][0] for o in block)
            size = sum(insns[o][4] for o in block)
            idiom = tuple(i[1] for i in shape if i[0] == 'call')
            blk_n[shape] += 1
            blk_b[shape] += size
            idi_n[idiom] += 1
            idi_b[idiom] += size
            per_object[obj][2][shape] += size
            per_object[obj][3][idiom] += size
            shapes.append(shape)

        for off, (shape, vals, ops, targets, size) in insns.items():
            ins_n[shape] += 1
            ins_b[shape] += size
            if shape[0] == 'call':
                calls[shape[1]] += 1

        rule_shapes[tuple(shapes)] += 1
        per_object[obj][0] += 1
        per_object[obj][1] += length
        per_rule.append((name, obj, length, shapes))

    def coverage(counter, label, marks):
        print('  by %s: %d distinct' % (label, len(counter)))
        running = 0
        for i, (s, n) in enumerate(counter.most_common(), 1):
            running += n
            if i in marks:
                print('    top %5d cover %5.1f%% of bytes'
                      % (i, 100.0 * running / total))

    print('rules %d, objects %d, bytecode %d bytes, blocks %d'
          % (len(rules), len(per_object), total, sum(blk_n.values())))
    print()
    print('how much of the bytecode the commonest shapes account for')
    coverage(ins_b, 'instruction', [10, 20, 50, 100, 200, 500])
    coverage(idi_b, 'call idiom', [10, 20, 50, 100, 200, 500, 1000])
    coverage(blk_b, 'whole block', [10, 50, 100, 500, 1000, 2000, 5000])
    print()

    for counter, bytes_, label in ((blk_n, blk_b, 'block shapes'),
                                   (idi_n, idi_b, 'call idioms')):
        once = [s for s in counter if counter[s] == 1]
        many = [s for s in counter if counter[s] >= 10]
        print('%s seen once: %d, holding %.1f%% of the bytes; seen ten times '
              'or more: %d, holding %.1f%%'
              % (label, len(once),
                 100.0 * sum(bytes_[s] for s in once) / total,
                 len(many),
                 100.0 * sum(bytes_[s] for s in many) / total))
    print()

    dup = sum(n for s, n in rule_shapes.items() if n > 1)
    print('whole rules identical in shape to another: %d of %d'
          % (dup, len(rules)))
    print('rules with a body, counted by their entry call: %d'
          % calls['ventproc'])
    print()

    print('by object: rules, bytes, then the share of bytes in block shapes '
          'and in call idioms seen ten times or more anywhere')
    for obj, (n, b, shapes, idioms) in sorted(per_object.items(),
                                              key=lambda kv: -kv[1][1]):
        cs = sum(v for s, v in shapes.items() if blk_n[s] >= 10)
        ci = sum(v for s, v in idioms.items() if idi_n[s] >= 10)
        print('  %-16s %5d rules %9d bytes  block %5.1f%%  idiom %5.1f%%'
              % (obj, n, b, 100.0 * cs / b, 100.0 * ci / b))
    print()

    print('the fifteen commonest call idioms')
    for s, b in idi_b.most_common(15):
        print('  %6d blocks %8d bytes  %s'
              % (idi_n[s], b, ', '.join(s) if s else '(no call)'))
    print()

    print('the thirty commonest calls')
    for name, n in calls.most_common(30):
        print('  %6d  %s' % (n, name))
    print()

    print('the fifteen largest rules, and how repetitive each is')
    for name, obj, length, shapes in sorted(per_rule, key=lambda r: -r[2])[:15]:
        top = collections.Counter(shapes).most_common(1)[0][1]
        print('  %-26s %-14s %7d bytes %5d blocks %5d shapes, commonest %d'
              % (name, obj, length, len(shapes), len(set(shapes)), top))


# ---- one rule -----------------------------------------------------------

def printable(b):
    return ''.join(chr(x) if 32 <= x < 127 else '.' for x in b)


def dump(want):
    c, rules = load()
    blobs = carve_blobs(open(CONSTS_C).read())

    for name, obj, start, length in rules:
        if name != want:
            continue
        insns = c.decode(start, length)
        print('%s in %s, %d bytes' % (name, obj, length))
        targets = set()
        for off in insns:
            targets.update(insns[off][3])

        for off in sorted(insns):
            shape, vals, ops, tgts, size = insns[off]
            line = ' '.join(str(part) for part in shape)
            notes = []
            if shape[0] == 'call':
                notes.append('%d args, %d in the area' % (vals[0], vals[1]))
            # A symbol and an immediate are what a lexicon is written in, so
            # they are resolved; the rest is left as it lies.
            for part, val, _where in ops:
                if not isinstance(val, int):
                    continue
                if part == 'sym' and val < len(c.syms):
                    blob, at = c.syms[val]
                    body = blobs.get(blob, b'')[at:at + 24]
                    notes.append('%s+%d "%s"' % (blob, at, printable(body)))
                elif part == 'imm' and val < len(c.imm):
                    v = c.imm[val]
                    notes.append('= %d' % (v - 0x100000000
                                           if v >= 0x80000000 else v))
                elif part in ('slot', 'slotaddr', 'state', 'statefld'):
                    notes.append('%s %+d' % (part, val))
            if tgts:
                notes.append('to ' + ' '.join(str(t) for t in tgts))
            print('%5d:%s %-52s %s'
                  % (off, '*' if off in targets else ' ', line,
                     '  '.join(notes)))
        return

    print('no rule called %s' % want, file=sys.stderr)
    sys.exit(1)


if __name__ == '__main__':
    if len(sys.argv) > 1:
        dump(sys.argv[1])
    else:
        census()
