#!/usr/bin/env python3
"""One of the language's rules as C, instead of as bytecode for the machine
IBM's compiler wrote it for.

The machine is small -- eight registers, four flags, a frame of bytes
addressed from a base, and calls out to the runtime -- and every one of the
1,164 plain things the rules call is a function we have already written. So
the translation is not a matter of working out what anything means; it is a
matter of writing the same operations in C and proving the result does the
same thing.

Faithful before pretty. What comes out keeps the machine's own shape: the
frame is a buffer, the registers are locals, and a call is the same call with
the same arguments in the same order. Where the bytecode jumps, this goes to a
label. Recovering the loops and the conditionals is a separate pass, and one
that cannot start until this one is known to be exact.

Nothing about the machine's arithmetic is written again here. The flags and
the operations that set them came out of the interpreter into delta_rule_alu,
delta_rule_cmp and delta_condition, which both it and this call, so neither
can drift from the other over what a comparison afterwards will say.

Proving it: the interpreter prints every call it makes with its arguments when
DELTA_RULE_TRACE is set high enough. A rule compiled from here calls through
the same helper and prints the same way, so a run with the rule as bytecode
and a run with it as C either produce the same trace or the translation is
wrong. tools/delta-check.sh does that; both suites are the coarser check
behind it.

usage: delta-decompile.py                 the hundred smallest with a body
       delta-decompile.py <count>         the smallest that many
       delta-decompile.py all             every rule there is
       delta-decompile.py <rule>...       the ones named
"""

import importlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_C = os.path.join(ROOT, 'src', 'delta_rules_c.c')

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
census = importlib.import_module('delta-census')

# What the interpreter keeps, and what a rule compiled from here keeps too.
MAXARG = 64

COND_C = {
    'e': '==', 'ne': '!=',
    'g': '>', 'ge': '>=', 'l': '<', 'le': '<=',
    'a': '>', 'ae': '>=', 'b': '<', 'be': '<=',
}
UNSIGNED = ('a', 'ae', 'b', 'be')


class Unhandled(Exception):
    """Something in a rule this cannot write down. Better said than guessed."""


LOADED = []


def all_rules():
    """The whole lot, read once. Reading them per rule meant parsing a
    megabyte and a half of bytecode a thousand times over."""
    if not LOADED:
        LOADED.extend(census.load())
    return LOADED[0], LOADED[1]


def load(name):
    c, rules = all_rules()
    for i, (n, obj, start, length) in enumerate(rules):
        if n == name:
            return c, i, rules[i], c.decode(start, length)
    raise Unhandled('no rule called %s' % name)


class Rule:
    def __init__(self, code, index, row, insns):
        self.c = code
        self.index = index
        self.name, self.obj, self.start, self.length = row
        self.insns = insns
        self.targets = set()
        for off in insns:
            self.targets.update(insns[off][3])

    # ---- operands --------------------------------------------------------

    def raw(self, where):
        """The byte a register operand is written as. The low three bits are
        which register and the high nibble is how much of it, which the shared
        decoder masks away because nothing else needs it."""
        return self.c.code[where]

    def reg_read(self, code):
        n = code & 7
        return {0: 'r%d' % n,
                1: '(int32_t)((uint32_t)r%d & 0xffffu)' % n,
                2: '(int32_t)((uint32_t)r%d & 0xffu)' % n,
                3: '(int32_t)(((uint32_t)r%d >> 8) & 0xffu)' % n,
                }.get(code >> 4, 'r%d' % n)

    def reg_write(self, code, what):
        n = code & 7
        return {
            0: 'r%d = (%s);' % (n, what),
            1: ('r%d = (int32_t)(((uint32_t)r%d & 0xffff0000u)'
                ' | ((uint32_t)(%s) & 0xffffu));' % (n, n, what)),
            2: ('r%d = (int32_t)(((uint32_t)r%d & 0xffffff00u)'
                ' | ((uint32_t)(%s) & 0xffu));' % (n, n, what)),
            3: ('r%d = (int32_t)(((uint32_t)r%d & 0xffff00ffu)'
                ' | (((uint32_t)(%s) & 0xffu) << 8));' % (n, n, what)),
        }.get(code >> 4, 'r%d = (%s);' % (n, what))

    def value(self, kind, val, width=4, signed=True, where=None):
        """One operand as a C expression of type int32_t."""
        if kind == 'reg':
            return self.reg_read(self.raw(where))
        if kind == 'imm':
            v = self.c.imm[val]
            return '%d' % (v - 0x100000000 if v >= 0x80000000 else v)
        if kind == 'sym':
            return '(int32_t)(intptr_t)delta_rule_sym[%d]' % val
        if kind == 'slotaddr':
            return 'SLOT(%d)' % val
        if kind == 'state':
            return 'FIELD(%d)' % val
        if kind == 'slot':
            return self.at('base + %d' % val, width, signed)
        if kind == 'statefld':
            return self.at('(unsigned char *)state + %d' % val, width, signed)
        if kind.startswith('ind('):
            inner, disp = val
            return self.at('(unsigned char *)(intptr_t)(%s) + %d'
                           % (self.value(kind[4:-1], inner[0] if
                                         isinstance(inner, tuple) else inner,
                                         where=where[0] if
                                         isinstance(where, tuple) else where),
                              disp),
                           width, signed)
        raise Unhandled('operand %s' % kind)

    def at(self, where, width, signed):
        t = {1: 'int8_t', 2: 'int16_t', 4: 'int32_t'}[width]
        if not signed:
            t = 'u' + t
        m = re.match(r'^base \+ (-?\d+)$', where)
        if m:
            return '(int32_t)AT(%s, %s)' % (t, m.group(1))
        m = re.match(r'^\(unsigned char \*\)state \+ (-?\d+)$', where)
        if m:
            return '(int32_t)FLD(%s, %s)' % (t, m.group(1))
        return '(int32_t)(*(%s *)(%s))' % (t, where)

    def put(self, kind, val, what, width=4, where=None):
        """Putting a value where the operand says, as a statement."""
        if kind == 'reg':
            return self.reg_write(self.raw(where), what)
        lv, _w = self.place(kind, val, width, where)
        return '%s = (%s);' % (lv, what)

    def place(self, kind, val, width=4, where=None):
        """Where a value is put, as something assignable."""
        t = {1: 'int8_t', 2: 'int16_t', 4: 'int32_t'}[width]
        if kind == 'slot':
            return 'AT(%s, %d)' % (t, val), width
        if kind == 'statefld':
            return 'FLD(%s, %d)' % (t, val), width
        if kind.startswith('ind('):
            inner, disp = val
            return ('(*(%s *)((unsigned char *)(intptr_t)(%s) + %d))'
                    % (t, self.value(kind[4:-1],
                                     inner[0] if isinstance(inner, tuple)
                                     else inner,
                                     where=where[0] if isinstance(where, tuple)
                                     else where), disp), width)
        raise Unhandled('cannot put a value in %s' % kind)


MOV_WIDTH = {'movl': (4, True), 'movw': (2, True), 'movb': (1, True),
             'movswl': (2, True), 'movzwl': (2, False),
             'movsbl': (1, True), 'movzbl': (1, False)}

ALU_WIDTH = {'l': 4, 'w': 2}

ALU_OP = {'add': '+', 'sub': '-', 'and': '&', 'or': '|',
          'shl': '<<', 'sar': '>>', 'imul': '*'}


def emit(rule):
    """The rule as C. Raises Unhandled for anything not written down here,
    which is the point: a rule half translated is worse than one not."""
    body = []
    pending = None      # a comparison waiting for the branch that reads it

    def v(o, width=4, signed=True):
        return rule.value(o[0], o[1], width, signed, o[2])

    for off in sorted(rule.insns):
        shape, vals, ops, targets, size = rule.insns[off]
        op = shape[0]
        last = rule.start + off + size - 1

        if off in rule.targets:
            body.append('L%d:;' % off)

        if op == 'load':
            width, signed = MOV_WIDTH[shape[1]]
            body.append('    ' + rule.reg_write(rule.raw(last),
                                                v(ops[0], width, signed)))
        elif op == 'store':
            width, signed = MOV_WIDTH[shape[1]]
            body.append('    ' + rule.put(ops[1][0], ops[1][1],
                                          v(ops[0], width, signed),
                                          width, ops[1][2]))
        elif op in ('alu2', 'alu1'):
            kind = shape[1]
            width = ALU_WIDTH[kind[-1]]
            n = census.ALUK.index(kind)
            if op == 'alu2':
                a = v(ops[0], width, True)
                dst = ops[1]
            else:
                a = '1' if kind[:-1] in ('shl', 'sar') else '0'
                dst = ops[0]
            was = v(dst, width, True)
            body.append('    ' + rule.put(dst[0], dst[1],
                                          'ALU(%s, %s, %s)'
                                          % (census.ALUK[n], a, was),
                                          width, dst[2]))
            pending = None
        elif op == 'cmp':
            n = census.CMPK.index(shape[1])
            width = {'l': 4, 'w': 2, 'b': 1}[shape[1][-1]]
            body.append('    CMP(%s, %s, %s);'
                        % (census.CMPK[n], v(ops[0], width, True),
                           v(ops[1], width, True)))
        elif op == 'branch':
            body.append('    if (IF(%s)) goto L%d;'
                        % (shape[1], targets[0]))
        elif op == 'jump':
            body.append('    goto L%d;' % targets[0])
            pending = None
        elif op == 'push':
            body.append('    ARG(%s);' % v(ops[0]))
        elif op == 'setarg':
            body.append('    if (argn - 1 - %d >= 0 && argn - 1 - %d < %d)'
                        ' arg[argn - 1 - %d] = %s;'
                        % (vals[0], vals[0], MAXARG, vals[0], v(ops[0])))
        elif op == 'popn':
            body.append('    DROP(%d);' % vals[0])
        elif op == 'popreg':
            code = rule.raw(rule.start + off + 1)
            if code >> 4 == 0:
                body.append('    POP(r%d);' % (code & 7))
            else:
                # A pop into part of a register keeps the rest of it, which
                # is more than one statement, so it stays written out.
                body.append('    if (argn > 0) { argn--; if (argn < %d) %s }'
                            % (MAXARG, rule.reg_write(code, 'arg[argn]')))
        elif op == 'call':
            if shape[1] == 'setjmp3':
                # The one call the interpreter makes for itself: a rule plants
                # its landing place here rather than in the runtime, or a
                # backtrack would land in the wrong function.
                body.append('    { int32_t buf = (argn > 0) ? arg[argn - 1]'
                            ' : 0; int depth = argn;')
                body.append('      r0 = setjmp(*(jmp_buf *)(intptr_t)buf);')
                body.append('      argn = depth; }')
            else:
                body.append('    r0 = CALL(%s, %d);' % (shape[1], vals[0]))
            pending = None
        elif op == 'addk':
            k = rule.c.imm[vals[0]]
            body.append('    ' + rule.reg_write(
                rule.raw(last),
                '(int32_t)(%s + (%d))' % (v(ops[0]),
                                          k - 0x100000000 if k >= 0x80000000
                                          else k)))
        elif op == 'widen':
            body.append('    r2 = r0 >> 31;')
        elif op == 'setcc':
            body.append('    ' + rule.reg_write(
                0x20 | (rule.raw(last) & 0x0f),
                'IF(%s) ? 1 : 0' % shape[1]))
        elif op == 'mul':
            width = 2 if shape[1] == 'imulw' else 4
            body.append('    ' + rule.reg_write(
                rule.raw(last),
                '(int32_t)((uint32_t)(%s) * (uint32_t)(%s))'
                % (v(ops[0], width, True), v(ops[1], width, True))))
        elif op == 'map':
            body.append('    ' + rule.reg_write(
                rule.raw(last),
                '(int32_t)delta_rule_map[%d + (%s)]' % (vals[0], v(ops[0]))))
        elif op == 'scale':
            k = rule.c.imm[vals[0]]
            body.append('    ' + rule.reg_write(
                rule.raw(last),
                '(int32_t)((%d) + (%s) + (%s) * (%d))'
                % (k - 0x100000000 if k >= 0x80000000 else k,
                   v(ops[0]), v(ops[1]), vals[1])))
        elif op == 'div':
            body.append('    { int32_t by = %s;' % v(ops[0]))
            body.append('      if (by != 0) {')
            body.append('        int64_t num = ((int64_t)r2 << 32)'
                        ' | (uint32_t)r0;')
            body.append('        r0 = (int32_t)(num / by);')
            body.append('        r2 = (int32_t)(num % by); } }')
        elif op == 'return':
            body.append('    { int32_t out = %s; evv_frame_pop(frame);'
                        ' return out; }' % v(ops[0]))
            pending = None
        elif op == 'switch':
            body.append('    switch (%s) {' % v(ops[0]))
            for i, t in enumerate(targets):
                body.append('    case %d: goto L%d;' % (i, t))
            body.append('    }')
            pending = None
        else:
            raise Unhandled('operation %s' % op)

    return body


HEAD = """\
/* Generated by tools/delta-decompile.py. Do not edit.

   Rules of the language written as C rather than run as bytecode. Each keeps
   the shape of the machine its compiler wrote it for -- the frame is a
   buffer, the registers are locals, and a call is the same call with the same
   arguments -- because being the same thing matters more here than reading
   well. Recovering the loops and the conditionals comes after this is known
   to be exact.

   delta_rule_native names the ones written down; the interpreter looks there
   first and runs a rule from here when it finds one. */

#include <setjmp.h>
#include <string.h>

#include "delta_rules.h"
#include "delta_rules_c.h"
#include "evv_arena.h"

"""


def write(names):
    done, refused = [], []
    text = [HEAD]

    for name in names:
        try:
            c, index, row, insns = load(name)
            rule = Rule(c, index, row, insns)
            body = emit(rule)
        except Unhandled as why:
            refused.append((name, str(why)))
            continue

        _n, _o, _s, _l = row
        frame, pbase, params = c_rule_shape(name)
        text.append('/* %s, from %s */\n' % (name, rule.obj))
        text.append('static int32_t evv_%s(void *state, const int32_t *args,'
                    ' int nargs)\n{\n' % name)
        # The frame is not an ordinary local. A rule hands the machine the
        # address of it, and where a value is 32 bits and an address is not,
        # the only stack that can be named in one is the arena's.
        text.append('    unsigned char *frame = evv_frame_push('
                    'DELTA_RULE_FRAME_MAX);\n')
        text.append('    unsigned char *base = frame + %d;\n' % frame)
        text.append('    int32_t arg[%d];\n' % MAXARG)
        # A landing from a backtrack comes back into the middle of the
        # function, and anything the compiler had chosen to keep in a machine
        # register would come back stale. The interpreter is safe because
        # everything it needs lives in a block whose address has escaped; here
        # the frame and the argument area are addressed, and the rest is said
        # to be volatile so that it is not kept anywhere else.
        text.append('    volatile int argn = 0;\n')
        text.append('    volatile int32_t r0 = 0, r1 = 0, r2 = 0, r3 = 0,'
                    ' r4 = 0, r5 = 0, r6 = 0, r7 = 0;\n')
        text.append('    delta_flags fl;\n')
        text.append('    int i;\n\n')
        text.append('    memset(frame, 0, DELTA_RULE_FRAME_MAX);\n')
        text.append('    memset(arg, 0, sizeof arg);\n')
        text.append('    memset(&fl, 0, sizeof fl);\n')
        text.append('    for (i = 0; i < nargs && i < %d; i++)\n' % params)
        text.append('        memcpy(base + %d + 4 * i, &args[i], 4);\n\n'
                    % pbase)
        text.append('\n'.join(structure(fold(body))))
        text.append('\n    evv_frame_pop(frame);\n    return r0;\n}\n\n')
        done.append(name)

    text.append('const delta_rule_c delta_rule_native[] = {\n')
    for name in done:
        text.append('    { %d, evv_%s },\n' % (index_of(name), name))
    text.append('    { -1, 0 },\n};\n')

    open(OUT_C, 'w').write(''.join(text))
    return done, refused


SHAPES = {}


def c_rule_shape(name):
    if not SHAPES:
        import re
        text = open(os.path.join(ROOT, 'src',
                                 'delta_rules_enus.c')).read()
        for m in re.finditer(r'\{\s*"([^"]*)",\s*"[^"]*",\s*-?\d+,\s*-?\d+,'
                             r'\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\}',
                             census.span(text, 'delta_rules[]')):
            SHAPES[m.group(1)] = (int(m.group(2)), int(m.group(3)),
                                  int(m.group(4)))
    return SHAPES[name]


INDEX = {}


def index_of(name):
    if not INDEX:
        c, rules = all_rules()
        for i, (n, _o, _s, _l) in enumerate(rules):
            INDEX.setdefault(n, i)
    return INDEX[name]


def smallest(n):
    """The n smallest rules that have a body of their own."""
    c, rules = all_rules()
    out = []
    for name, obj, start, length in rules:
        insns = c.decode(start, length)
        if any(insns[o][0] == ('call', 'ventproc') for o in insns):
            out.append((length, name))
    out.sort()
    return [name for _l, name in out[:n]]


def every():
    """Every rule there is, the compiler's own accessors included. Those have
    no body of their own -- they fetch or store one thing -- but they are
    rules all the same, and while any is left as bytecode the interpreter has
    to stay."""
    c, rules = all_rules()
    return [name for name, _o, _s, _l in rules]




# The other half of a condition, so that a branch which skips a region can be
# turned round into an if which enters it. Every pair below is an exact
# complement of the other in delta_condition, which is what makes the turn
# safe rather than merely plausible.
OPPOSITE = {'e': 'ne', 'ne': 'e', 'a': 'be', 'be': 'a', 'ae': 'b', 'b': 'ae',
            'g': 'le', 'le': 'g', 'ge': 'l', 'l': 'ge', 's': 'ns', 'ns': 's'}

LABEL_RE = re.compile(r'^\s*L(\d+):;$')
BRANCH_RE = re.compile(r'^(\s*)if \(IF\((\w+)\)\) goto L(\d+);$')
JUMP_RE = re.compile(r'goto L(\d+);')

STRUCTURED = [0]
LOOPED = [0]


def structure(body):
    """Branches that skip a region, written as the if they are.

    A branch forward to a label, over a region nothing else can jump into, is
    an if around that region under the opposite condition. Nothing else is
    touched: a backward branch is a loop and a region with another way in is
    not a region, and both keep the goto they had.

    Innermost first, so that what comes out nests.
    """
    while True:
        cut = _once(body)
        if cut is None:
            cut = _loop(body)
            if cut is None:
                return body
            LOOPED[0] += 1
        else:
            STRUCTURED[0] += 1
        body = cut


def _loop(body):
    """A branch back to a label above it, written as the do-while it is.

    The label is the top of the loop and the branch is its test. It only works
    where nothing else jumps to that label -- another way in is another loop
    -- and where what lies between is a whole region.
    """
    at = {}
    for i, line in enumerate(body):
        m = LABEL_RE.match(line)
        if m:
            at[int(m.group(1))] = i

    goes = {}
    for i, line in enumerate(body):
        for m in JUMP_RE.finditer(line):
            goes.setdefault(int(m.group(1)), []).append(i)

    for i, line in enumerate(body):
        m = BRANCH_RE.match(line)
        if not m:
            continue
        pad, cond, tgt = m.group(1), m.group(2), int(m.group(3))
        j = at.get(tgt)
        if j is None or j >= i:
            continue
        if goes.get(tgt, ()) != [i]:
            continue
        if not _whole(body[j + 1:i]):
            continue
        out = body[:j]
        out.append('%sdo {' % pad)
        out.extend('    ' + l if l.strip() else l for l in body[j + 1:i])
        out.append('%s} while (IF(%s));' % (pad, cond))
        out.extend(body[i + 1:])
        return out
    return None


def _whole(region):
    """Whether a run of lines opens and closes every block it mentions."""
    depth = 0
    for line in region:
        depth += line.count('{') - line.count('}')
        if depth < 0:
            return False
    return depth == 0


def _once(body):
    at = {}
    for i, line in enumerate(body):
        m = LABEL_RE.match(line)
        if m:
            at[int(m.group(1))] = i

    goes = {}
    for i, line in enumerate(body):
        for m in JUMP_RE.finditer(line):
            goes.setdefault(int(m.group(1)), []).append(i)

    best = None
    for i, line in enumerate(body):
        m = BRANCH_RE.match(line)
        if not m:
            continue
        pad, cond, tgt = m.group(1), m.group(2), int(m.group(3))
        if cond not in OPPOSITE:
            continue
        j = at.get(tgt)
        if j is None or j <= i + 1:
            continue
        inside = [k for k, where in at.items() if i < where < j]
        if any(any(not (i < f < j) for f in goes.get(k, ()))
               for k in inside):
            continue
        # The region has to be a region. A branch whose target lies outside
        # the block the branch is in would otherwise take the block's own
        # closing brace with it, which balances and compiles and means
        # something else entirely.
        if not _whole(body[i + 1:j]):
            continue
        if best is None or j - i < best[1] - best[0]:
            best = (i, j, pad, cond, tgt)
    if best is None:
        return None

    i, j, pad, cond, tgt = best
    out = body[:i]
    out.append('%sif (IF(%s)) {' % (pad, OPPOSITE[cond]))
    out.extend('    ' + line if line.strip() else line
               for line in body[i + 1:j])
    out.append('%s}' % pad)
    out.extend(body[j:])

    # The label the branch used may have no one left who needs it.
    if len(goes.get(tgt, ())) == 1:
        k = next(n for n, line in enumerate(out) if LABEL_RE.match(line)
                 and int(LABEL_RE.match(line).group(1)) == tgt)
        del out[k]
    return out


# The envelope every rule carries, folded back into the two things it is.
#
# Both are matched line for line and only where nothing can be jumped into the
# middle of them, so what the compiler sees is unchanged: the macros in
# delta_rules_c.h expand to exactly the lines taken away. A rule whose
# envelope the original scheduled differently keeps it written out.
FOLDED = [0, 0]


def fold(body):
    """The landing place and the entry, as one line each."""
    out = []
    i = 0
    while i < len(body):
        n = _landing(body, i) or _enter(body, i)
        if n:
            out.append(n[0])
            i += n[1]
            continue
        out.append(body[i])
        i += 1
    return out


def _slot(line, want):
    m = re.match(r'^    %s$' % want, line)
    return m.groups() if m else None


def _landing(body, i):
    if i + 6 >= len(body):
        return None
    a = _slot(body[i], r'r0 = \(SLOT\((-?\d+)\)\);')
    if not a:
        return None
    jb = a[0]
    want = ['    ARG(0);',
            '    ARG(SLOT(%s));' % jb]
    if body[i + 1:i + 3] != want:
        return None
    if 'setjmp' not in body[i + 4]:
        return None
    if body[i + 6] != '    CMP(testl, r0, r0);':
        return None
    FOLDED[0] += 1
    return ('    LANDING(%s);' % jb, 7)


def _enter(body, i):
    if i + 14 > len(body):
        return None
    slots = []
    at = i
    for _ in range(5):
        a = _slot(body[at], r'r0 = \(SLOT\((-?\d+)\)\);')
        if not a:
            return None
        if body[at + 1] != '    ARG(SLOT(%s));' % a[0]:
            return None
        slots.append(a[0])
        at += 2
    tail = ['    ARG(FIELD(0));',
            '    r0 = CALL(ventproc, 6);',
            '    DROP(6);',
            '    CMP(testl, r0, r0);']
    if body[at:at + 4] != tail:
        return None
    FOLDED[1] += 1
    return ('    ENTER(%s);' % ', '.join(slots), 14)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == 'all':
        names = every()
    elif len(sys.argv) > 1 and not sys.argv[1].isdigit():
        names = sys.argv[1:]
    else:
        names = smallest(int(sys.argv[1]) if len(sys.argv) > 1 else 100)

    done, refused = write(names)
    print('branches turned into an if: %d, loops closed: %d'
          % (STRUCTURED[0], LOOPED[0]))
    print('landing places folded: %d, entries folded: %d'
          % (FOLDED[0], FOLDED[1]))
    print('%d of %d rules written to %s'
          % (len(done), len(names), os.path.relpath(OUT_C, ROOT)))
    if refused:
        seen = {}
        for name, why in refused:
            seen.setdefault(why, []).append(name)
        for why in sorted(seen, key=lambda w: -len(seen[w])):
            print('  %3d refused: %s (%s%s)'
                  % (len(seen[why]), why, ', '.join(seen[why][:3]),
                     ', ...' if len(seen[why]) > 3 else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
