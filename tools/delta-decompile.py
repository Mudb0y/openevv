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

Proving it: the engine says which rule it is entering and with what when
DELTA_RULE_TRACE is set, and a rule compiled from here is entered through the
same function and says the same. So the same text spoken twice, once with a
rule compiled and once without, either names the same rules in the same order
with the same arguments or the translation is wrong. tools/delta-check.sh does
that, with EVV_FAITHFUL set so that a wrapper is left as the call it was; the
comment at the top of it says what else that comparison needs and what would
make it finer. Both suites are the coarser check behind it, and the only one
for the inlining itself.

usage: delta-decompile.py                 the hundred smallest with a body
       delta-decompile.py <count>         the smallest that many
       delta-decompile.py all             every rule there is
       delta-decompile.py <rule>...       the ones named
"""

import collections
import importlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
census = importlib.import_module('delta-census')

# Beside the language it was read from, unless told otherwise. Writing it
# anywhere else puts one language's rules where another language's build
# will pick them up.
OUT_DIR = os.environ.get('EVV_OUT_DIR', census.LANG_DIR)

# How many files the rules are written into. One was seven minutes of compiler
# and could not be shared out; this many finish in about a minute on a machine
# with cores to spare, and the Makefile must be told the same number, since it
# names the files it expects rather than looking for whatever is there.
PARTS = int(os.environ.get('EVV_RULE_PARTS', '32'))

# The number goes before the language, not after it. Everything in a language
# module has to end in that module's name -- the Makefile refuses a file that
# does not, since that is how a stray from an earlier lift is caught.
def part_path(n):
    return os.path.join(OUT_DIR, 'delta_rules_c%02d_%s.c' % (n, census.LANG_TAG))

# Inlining a wrapper says what a rule does, and costs the only check that is
# finer than the audio. A wrapper written out no longer calls the wrapper rule,
# so what a run says it did differs from what the interpreter says, and
# tools/delta-check.sh has nothing to compare. Setting EVV_FAITHFUL leaves the
# wrappers as calls, which is the form that check is built from; nothing else
# here changes what a run says it did.
FAITHFUL = bool(os.environ.get('EVV_FAITHFUL'))

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
                1: 'LOW(r%d)' % n,
                2: 'BYTE0(r%d)' % n,
                3: 'BYTE1(r%d)' % n,
                }.get(code >> 4, 'r%d' % n)

    def reg_write(self, code, what):
        n = code & 7
        return {
            0: 'r%d = (%s);' % (n, what),
            1: 'SETLOW(r%d, %s);' % (n, what),
            2: 'SETBYTE0(r%d, %s);' % (n, what),
            3: 'SETBYTE1(r%d, %s);' % (n, what),
        }.get(code >> 4, 'r%d = (%s);' % (n, what))

    def value(self, kind, val, width=4, signed=True, where=None):
        """One operand as a C expression of type int32_t."""
        if kind == 'reg':
            return self.reg_read(self.raw(where))
        if kind == 'imm':
            v = self.c.imm[val]
            return '%d' % (v - 0x100000000 if v >= 0x80000000 else v)
        if kind == 'sym':
            return 'delta_sym_ref[%d]' % val
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
                body.append('      r0 = EVV_LAND_SAVE((intptr_t)buf);')
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
        elif op == 'ftol':
            # The little floating point the Frenches have. Worked out in long
            # double because that is the x87 register the original computes in,
            # and with the constant 0.4 the narrower type truncates
            # differently: see the comment on OP_FTOL in src/delta_rules.c.
            # Built up in parentheses rather than as one flat expression,
            # so it is worked out in the order the machine works it out in.
            # Written flat, C's precedence would read a + b * k as a + (b * k)
            # where the stack does (a + b) * k, and nothing in these two
            # languages happens to need the difference -- which is exactly the
            # kind of luck not to depend on.
            expr = None
            for step in shape[1]:
                if step[0] in (0, 1):
                    term = '(long double)(int32_t)(%s)' % v(ops[step[1]])
                else:
                    bits = ((rule.c.imm[step[2]] & 0xffffffff) << 32) \
                           | (rule.c.imm[step[1]] & 0xffffffff)
                    term = 'EVV_DBL(0x%016xULL)' % bits
                if expr is None:
                    expr = term
                else:
                    expr = '(%s %s %s)' % (expr, '*' if step[0] == 2 else '+',
                                           term)
            body.append('    ' + rule.reg_write(
                rule.raw(last), '(int32_t)(%s)' % expr))
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
            body.append('    RETURN(%s);' % v(ops[0]))
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

   This is one of %d such files. Together they are some thirteen megabytes,
   which is a quarter of an hour of compiler in one translation unit and about
   a minute spread over as many cores as the machine has. Which rules land in
   which file is settled by size, so that the files finish together; nothing
   else depends on where a rule is.

   delta_rule_native names the ones written down; the interpreter looks there
   first and runs a rule from here when it finds one. Each file carries its own
   piece of that table and the first gathers the pieces. */

#include <string.h>

#include "delta_rules_%s.h"
#include "delta_rules_c.h"
#include "evv_arena.h"

"""


def write(names):
    done, refused = [], []
    bodies = []

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
        # The rule while it is still one straight line of code with labels,
        # which is where anything that has to follow the machine's own control
        # flow has to look: the alternatives a dispatch chain names, and which
        # comparison a test of the flags is reading.
        flat = direct_tests(tail_returns(fold(body)))
        alts = dispatch_names(flat)
        # Taking the dead loads out first brings more calls up against
        # their arguments, which is why the joining goes last.
        named, saw = name_globals(
            join_calls(c, drop_pops(join_pops(drop_dead(
                leave_loops(structure(flat)))))))
        named = name_tails(
            name_alternatives(name_params(named, pbase, params), alts))
        USED.update(saw)

        plants = plants_landing(flat)
        bad = stale_registers(flat) if plants else set()

        # Whether this rule needs a frame out of the arena at all. It does
        # if it hands the machine the address of anything in one -- a value
        # is thirty-two bits, so such an address can only be the arena's --
        # or if it has working memory below its arguments, or if a backtrack
        # can land in it. None of that is true of a wrapper, and a wrapper is
        # 2,335 of the 3,377: those keep their few words on the stack like
        # any other C function, which is two calls and a clear a rule.
        loose = (frame == 0 and not plants
                 and not any('SLOT(' in l or 'PARAMAT(' in l for l in named))
        if loose:
            named = [l.replace('RETURN(', 'LEAVE(') for l in named]

        text = []
        text.append('/* %s, from %s */\n' % (name, rule.obj))
        text.append('static int32_t evv_%s(void *state, const int32_t *args,'
                    ' int nargs)\n{\n' % name)
        # The frame is not an ordinary local. A rule hands the machine the
        # address of it, and where a value is 32 bits and an address is not,
        # the only stack that can be named in one is the arena's.
        #
        # Every rule takes the same number of bytes, though most want far
        # fewer, because the address of a frame is the name a landing place
        # is filed under and frames of one size put a rule at a given depth
        # back where it was. What is cleared below is the rule's own, which
        # is a different question.
        room = frame + pbase + 4 * params
        if loose:
            # Words rather than bytes so that a four-byte read of it is
            # aligned; nothing in a rule reads its frame wider than that.
            text.append('    int32_t own[%d];\n' % ((room + 3) // 4))
            text.append('    unsigned char *base = (unsigned char *)own;\n')
            text.append('    unsigned char *param = base + %d;\n' % pbase)
        else:
            text.append('    unsigned char *frame = evv_frame_push('
                        'DELTA_RULE_FRAME_MAX);\n')
            text.append('    unsigned char *base = frame + %d;\n' % frame)
            text.append('    unsigned char *param = base + %d;\n' % pbase)
        text.append('    int32_t arg[%d];\n' % argument_depth(flat))
        # A landing from a backtrack comes back into the middle of the
        # function, and anything the compiler had chosen to keep in a machine
        # register would come back stale. The interpreter is safe because
        # everything it needs lives in a block whose address has escaped; here
        # the frame and the argument area are addressed, and the rest is said
        # to be volatile so that it is not kept anywhere else.
        #
        # A rule that plants no landing is never come back into, so nothing
        # about it can be stale. That is every wrapper. Of the ones that do
        # plant a landing, stale_registers says which registers a throw could
        # leave holding the wrong thing, and it is usually none of them: the
        # code at a landing puts back what it is about to read, because IBM's
        # own compiler had the same problem with setjmp and answered it the
        # same way.
        #
        # The argument depth is never one of them. The landing puts it back
        # from a local written before the save and never after, which is the
        # one thing C promises comes through a landing unharmed.
        # And every register, which is what this used to do and what the
        # claim above is held against: write the rules both ways, land on a
        # landing place on purpose and see whether the two ever differ.
        # docs/status.md says how that was done and what it answered.
        if os.environ.get('EVV_STALE_ALL') and plants:
            bad = set(range(8))
        keep = [r for r in range(8) if r not in bad]
        text.append('    int argn = 0;\n')
        if keep:
            text.append('    int32_t %s;\n'
                        % ', '.join('r%d = 0' % r for r in keep))
        if bad:
            text.append('    volatile int32_t %s;\n'
                        % ', '.join('r%d = 0' % r for r in sorted(bad)))
        text.append('    delta_flags fl;\n')
        text.append('    int i;\n\n')
        # The arena can run out, and then there is no frame. The interpreter
        # answers nought here rather than writing through the nought it was
        # given, and a rule written as C has to do the same or an engine that
        # would have gone quiet falls over instead.
        if not loose:
            text.append('    if (frame == 0)\n        return 0;\n\n')
        # Only as much of the frame as this rule has. The interpreter clears
        # the largest any rule asks for, since it has one piece of code for
        # all of them; here the shape is known, and the number below is the
        # same one delta-emit.py takes the largest of. A rule reaching past it
        # would be reaching outside its own frame either way.
        text.append('    memset(%s, 0, %d);\n'
                    % ('own' if loose else 'frame', room))
        text.append('    memset(arg, 0, sizeof arg);\n')
        text.append('    memset(&fl, 0, sizeof fl);\n')
        text.append('    for (i = 0; i < nargs && i < %d; i++)\n' % params)
        text.append('        memcpy(base + %d + 4 * i, &args[i], 4);\n\n'
                    % pbase)
        text.append('\n'.join(named))
        out = 'LEAVE' if loose else 'RETURN'
        tail = ('' if named and named[-1].strip().startswith(out + '(')
                else '\n    %s(r0);' % out)
        text.append('%s\n}\n\n' % tail)
        bodies.append((name, ''.join(text)))
        done.append(name)

    # Where each global the rules touch lands in the state. Every file gets
    # the whole list rather than the part of it that file uses: it is a few
    # thousand defines, the preprocessor does not care, and working out which
    # rule touched which would be one more thing that could be wrong.
    defs = ''
    if USED:
        where = {v: k for k, v in layout().items()}
        defs = ('/* Where each global the rules touch lands in the state. */\n'
                '%s\n\n'
                % '\n'.join('#define DG_%-6s %5d' % (v, where[v])
                            for v in sorted(USED, key=lambda x: where[x])))

    for n, part in enumerate(share_out(bodies)):
        text = [HEAD % (PARTS, census.LANG_TAG), defs]
        for _name, body in part:
            text.append(body)
        # The table carries the language, as every other name a module defines
        # does, because a program may have several in it, and the number of the
        # file besides, since each holds the rules that landed in it.
        text.append('const delta_rule_c %s_delta_rule_native_p%02d[] = {\n'
                    % (census.LANG_TAG, n))
        for name, _body in part:
            text.append('    { %d, evv_%s },\n' % (index_of(name), name))
        text.append('    { -1, 0 },\n};\n')
        if n == 0:
            # The pieces gathered, in the one file that is always written.
            # delta_run_rule walks these once and reads them into an index by
            # rule number, so what is in which piece never matters afterwards.
            text.append('\n')
            for k in range(PARTS):
                text.append('extern const delta_rule_c '
                            '%s_delta_rule_native_p%02d[];\n'
                            % (census.LANG_TAG, k))
            text.append('\nconst delta_rule_c *const %s_delta_rule_native[] '
                        '= {\n' % census.LANG_TAG)
            for k in range(PARTS):
                text.append('    %s_delta_rule_native_p%02d,\n'
                            % (census.LANG_TAG, k))
            text.append('    0,\n};\n')
        open(part_path(n), 'w').write(''.join(text))
    return done, refused


def plants_landing(body):
    """Whether a backtrack can come back into the middle of this rule.

    Every real rule plants one and no wrapper does, but that is a fact about
    the language rather than about the machine, so it is read off the rule in
    front of us. Either spelling counts: fold() writes LANDING where the whole
    of it is in one place and leaves the save itself where it is not."""
    return any('LANDING(' in l or 'EVV_LAND_SAVE' in l for l in body)


REG_RE = re.compile(r'\br([0-7])\b')
DEF_RE = re.compile(r'^\s*r([0-7]) = ')
POP_RE = re.compile(r'^\s*POP\(r[0-7]')
FLAT_LABEL = re.compile(r'^\s*(L\d+):;\s*$')
FLAT_GOTO = re.compile(r'^\s*goto (L\d+);\s*$')
FLAT_BRANCH = re.compile(r'^\s*if \((.*)\) goto (L\d+);\s*$')
FLAT_CASE = re.compile(r'^\s*case \d+: goto (L\d+);\s*$')
FLAT_SWITCH = re.compile(r'^\s*switch \(.*\) \{\s*$')
FLAT_CLOSE = re.compile(r'^\s*\}\s*$')


def flat_cfg(flat):
    """Where each line of the flat form can go next, or None if there is a
    line here this cannot read.

    Five shapes carry flow and they are all of them: a label, a jump, a jump
    on a condition, the return, and the switch a backtracking dispatch is
    written as -- which is a block of arms that each jump, so the switch goes
    to every one of them and each of those goes to its own label. Anything
    else in such a block, a default among them, is not a shape this has seen
    and the answer is to say so rather than to leave an edge out: an edge
    left out is a path an analysis never looks down."""
    at = {}
    for i, line in enumerate(flat):
        m = FLAT_LABEL.match(line)
        if m:
            at[m.group(1)] = i

    n = len(flat)
    succ = [[] for _ in range(n)]
    i = 0
    while i < n:
        line = flat[i]
        m = FLAT_GOTO.match(line)
        if m:
            succ[i] = [at[m.group(1)]]
            i += 1
            continue
        m = FLAT_BRANCH.match(line)
        if m:
            succ[i] = ([i + 1] if i + 1 < n else []) + [at[m.group(2)]]
            i += 1
            continue
        if FLAT_SWITCH.match(line):
            k = i + 1
            arms = []
            while k < n and not FLAT_CLOSE.match(flat[k]):
                m = FLAT_CASE.match(flat[k])
                if not m:
                    return None
                arms.append(k)
                succ[k] = [at[m.group(1)]]
                k += 1
            if k >= n:
                return None
            succ[i] = arms
            succ[k] = [k + 1] if k + 1 < n else []
            i = k + 1
            continue
        if line.strip().startswith('RETURN('):
            i += 1
            continue
        if i + 1 < n:
            succ[i] = [i + 1]
        i += 1
    return succ

STALE = [0, 0]


def _defuse(line):
    """What one line of the flat form writes and what it reads."""
    if 'LANDING(' in line or 'EVV_LAND_SAVE' in line:
        # What the save reads it reads before it, not after; what it leaves
        # behind is its answer in r0, and the argument depth, which the line
        # after it puts back.
        return {0}, set()
    if 'ENTER(' in line:
        return {0}, set()
    if POP_RE.match(line):
        # Not a write to be counted on. The machine takes nothing off an
        # empty argument area, and then the register keeps what it had.
        return set(), set()
    m = DEF_RE.match(line)
    if m:
        # SETLOW and the two byte writers keep the rest of the register, so
        # they do not match here and count as a read, which is right.
        return {int(m.group(1))}, {int(x)
                                   for x in REG_RE.findall(line[m.end():])}
    return set(), {int(x) for x in REG_RE.findall(line)}


def stale_registers(flat):
    """Which registers a backtrack could leave holding something stale.

    A landing place is come back into from a rule that threw, and only some of
    what the machine was holding comes back with it: the C compiler may have
    put a register anywhere, and the save puts back only what the two calling
    conventions agree belongs to the callee. So a register the rule writes
    after planting the landing, and reads after coming back to it without
    writing it first, has to be somewhere the save cannot lose -- which is
    what volatile is for.

    A register that is only ever written before the landing is not one of
    those: it holds the same thing on the way back as it did going in, and C
    says so of any local that is not written between the two.

    What is asked here is the ordinary question of which registers are live
    where the save returns to, over the rule as flat code with labels. It is
    asked of every path out of the landing rather than only the one a throw
    takes, because which of the two the test after the landing chooses is not
    worth working out: 877 of the 1,042 rules need no register kept either
    way, and only thirteen need two.
    """
    succ = flat_cfg(flat)
    if succ is None:
        return set(range(8))

    n = len(flat)
    df = [None] * n
    us = [None] * n
    for i, line in enumerate(flat):
        df[i], us[i] = _defuse(line)

    live = [set() for _ in range(n)]
    moved = True
    while moved:
        moved = False
        for i in range(n - 1, -1, -1):
            out = set()
            for k in succ[i]:
                out |= live[k]
            was = us[i] | (out - df[i])
            if was != live[i]:
                live[i] = was
                moved = True

    bad = set()
    for i, line in enumerate(flat):
        if 'LANDING(' in line or 'EVV_LAND_SAVE' in line:
            bad |= live[i]
    if bad:
        STALE[0] += 1
        STALE[1] += len(bad)
    return bad


ARG_ONE = re.compile(r'^\s*ARG\(.*\);\s*$')
DROP_N = re.compile(r'^\s*DROP\((\d+)\);\s*$')
POP_ONE = re.compile(r'^\s*POP\(r[0-7]\);\s*$')

DEEP = [0, 0]


def _depth_effect(line):
    """How far above the depth on entry a line reaches, and what it leaves the
    depth at, or None where this cannot read the line at all."""
    if ARG_ONE.match(line):
        return 1, 1
    m = DROP_N.match(line)
    if m:
        return 0, -int(m.group(1))
    if POP_ONE.match(line):
        return 0, -1
    if 'LANDING(' in line:
        return 2, 2
    # The same thing where fold() could not put it in one place: the two
    # pushes are on the first of its three lines and nothing else moves.
    if 'int32_t buf' in line:
        return 2, 2
    if 'EVV_LAND_SAVE' in line or 'argn = depth' in line:
        return 0, 0
    if 'ENTER(' in line:
        return 6, 0
    if 'ARG(' in line or 'POP(' in line or 'DROP(' in line or 'argn' in line:
        return None
    return 0, 0


def argument_depth(flat):
    """How deep this rule's argument area ever gets.

    The machine keeps one area per rule and the interpreter gives every rule
    the same 64 words of it, because it has one piece of code for all of them.
    Here the rule is in front of us, and 3,225 of the 3,377 never get past
    nine: writing out 64 words and clearing them cost every rule a quarter of
    a kilobyte of stack and a `rep stosq', on a thread that has sixty-four
    kilobytes in all and rules that nest deeply.

    What comes back is a bound rather than a guess, and 64 wherever there is
    any doubt -- a line this cannot read, a loop that pushes more than it lets
    go, anything at all that reaches the interpreter's own number. Sixty-four
    is what the code did before, so falling back to it changes nothing.
    """
    succ = flat_cfg(flat)
    if succ is None:
        return MAXARG

    n = len(flat)
    eff = []
    for line in flat:
        e = _depth_effect(line)
        if e is None:
            return MAXARG
        eff.append(e)

    into = [None] * n
    into[0] = 0
    peak = 0
    work = [0]
    while work:
        i = work.pop()
        v = into[i]
        p, ch = eff[i]
        peak = max(peak, v + p)
        if peak >= MAXARG:
            return MAXARG
        out = v + ch
        out = 0 if out < 0 else out
        if out >= MAXARG:
            return MAXARG
        for k in succ[i]:
            if into[k] is None or out > into[k]:
                into[k] = out
                work.append(k)
    peak = max(1, min(peak, MAXARG))
    DEEP[0] += 1
    DEEP[1] += peak
    return peak


def share_out(bodies):
    """The rules dealt into PARTS files so that the files are about the same
    size, since the slowest one is what a build waits for. Largest first into
    whichever file is smallest so far, which is close enough to even: the
    biggest rule is 30 kilobytes against a file of four hundred. Each file
    keeps the rules in the order they were read, so that writing them again
    gives the same files."""
    parts = [[] for _ in range(PARTS)]
    weight = [0] * PARTS
    for i, (name, body) in sorted(enumerate(bodies),
                                  key=lambda x: (-len(x[1][1]), x[0])):
        n = min(range(PARTS), key=lambda k: (weight[k], k))
        parts[n].append((i, name, body))
        weight[n] += len(body)
    return [[(name, body) for _i, name, body in sorted(p)] for p in parts]
SHAPES = {}


def c_rule_shape(name):
    if not SHAPES:
        import re
        text = open(census.RULES_C).read()
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

# And the same for a condition that has already been written as a comparison.
FLIP = {'==': '!=', '!=': '==', '<': '>=', '>=': '<', '<=': '>', '>': '<='}

LABEL_RE = re.compile(r'^\s*L(\d+):;$')
BRANCH_RE = re.compile(r'^(\s*)if \((.*)\) goto L(\d+);$')
GOTO_RE = re.compile(r'^(\s*)goto L(\d+);$')
JUMP_RE = re.compile(r'goto L(\d+);')


def opposite(cond):
    """The other half of a condition, however it is written. None where there
    is no way to say it, which is a branch that keeps the goto it had."""
    m = re.match(r'^IF\((\w+)\)$', cond)
    if m:
        was = m.group(1)
        return 'IF(%s)' % OPPOSITE[was] if was in OPPOSITE else None
    # A comparison is turned round by turning its operator round, which needs
    # the one that compares the two sides rather than any inside them.
    depth = 0
    found = None
    i = 0
    while i < len(cond):
        ch = cond[i]
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth -= 1
        elif depth == 0:
            for op in ('==', '!=', '<=', '>='):
                if cond.startswith(op, i):
                    if found:
                        return None
                    found = (i, op)
                    i += 1
                    break
            else:
                if ch in '<>':
                    if found:
                        return None
                    found = (i, ch)
        i += 1
    if not found:
        return None
    at, op = found
    return cond[:at] + FLIP[op] + cond[at + len(op):]


STRUCTURED = [0]
LOOPED = [0]


def tails_of(body):
    """The labels where a rule ends: the one it goes to when it gives up and
    the one it goes to when it has matched. Everything jumps to them, so they
    are the two places a reader looks for, and they stay where they can be
    found."""
    out = set()
    for i, line in enumerate(body):
        m = LABEL_RE.match(line)
        if not m:
            continue
        for k in range(i + 1, min(i + 8, len(body))):
            t = body[k].strip()
            if 'succeed,' in t or 'vretproc,' in t:
                out.add(int(m.group(1)))
                break
            if t.startswith('RETURN(') or LABEL_RE.match(body[k]):
                break
    return out


def structure(body):
    """Branches that skip a region, written as the if they are, and branches
    back over one, written as the loop.

    A branch forward to a label is an if around what it skips, under the
    opposite condition. A branch back to a label above it is a do-while around
    what lies between, and an unconditional jump back is the same thing with
    nothing to test.

    All three ask that what they enclose is a whole region -- that it opens and
    closes every block it mentions. Beyond that a loop asks nothing at all, and
    an if asks only that the region does not hold one of the two places the rule
    ends. Whether anything else jumps into a region does not otherwise matter,
    because C lets a goto enter a block and means by it exactly what the flat
    code meant: the rest of the block, and then whatever follows it. The label
    inside says so, and a reader who sees one knows there is another way in.

    Loops go first, and innermost first, so that what comes out nests. The order
    is not a preference: a branch out of a loop turned into an if straddles the
    loop's own test, and then the loop can never close.
    """
    tails = tails_of(body)
    while True:
        cut = _loop(body)
        if cut is None:
            cut = _once(body, tails)
            if cut is None:
                return body
            STRUCTURED[0] += 1
        else:
            LOOPED[0] += 1
        body = cut


def _loop(body):
    """A branch back to a label above it, written as the do-while it is.

    The label is the top of the loop and the branch is its test, so the lines
    between them are the loop's body and the branch is what repeats it. All it
    takes is that what lies between is a whole region: nothing else has to be
    true of it.

    The label stays where it is, inside the loop. It used to be taken away,
    which meant refusing every loop anything else jumped to -- and that was
    most of them, which is why 343 loops closed where the machine has 7,757.
    A goto at the top of a do is the same place as before it, so nothing that
    jumped there needs to know.
    """
    at = {}
    for i, line in enumerate(body):
        m = LABEL_RE.match(line)
        if m:
            at[int(m.group(1))] = i

    best = None
    for i, line in enumerate(body):
        m = BRANCH_RE.match(line)
        cond = m.group(2) if m else None
        if not m:
            m = GOTO_RE.match(line)
            if not m:
                continue
        pad, tgt = m.group(1), int(m.group(len(m.groups())))
        j = at.get(tgt)
        if j is None or j >= i:
            continue
        if not _whole(body[j + 1:i]):
            continue
        if best is None or i - j < best[0] - best[1]:
            best = (i, j, pad, cond)
    if best is None:
        return None

    i, j, pad, cond = best
    out = body[:j]
    out.append('%sdo {' % pad if cond else '%sfor (;;) {' % pad)
    out.append(body[j])
    out.extend('    ' + l if l.strip() else l for l in body[j + 1:i])
    out.append('%s} while (%s);' % (pad, cond) if cond else '%s}' % pad)
    out.extend(body[i + 1:])
    return out


WRAPPED = [0]
NAMED = [0]
USED = set()
LAYOUT = {}


def layout():
    """Where each of the language's global variables lands in the state.

    delta_new walks the declaration list once, aligning and numbering as it
    goes, and this walks it the same way. The proof that it walks it right is
    that the last variable ends exactly on the state's declared size, with
    nothing over and nothing short.
    """
    if LAYOUT:
        return LAYOUT
    path = os.path.join(census.LANG_DIR,
                        'delta_globals_%s.c' % census.LANG_TAG)
    if not os.path.exists(path):
        return LAYOUT
    text = open(path).read()
    kinds = re.findall(r'DG_(WORD|LONG|SHORT|COMPOUND)', text)
    sizes = [int(b) for _a, b in
             re.findall(r'\{\s*(\d+),\s*(\d+)\s*\}',
                        text[text.index('delta_compounds[]'):])]

    def up(n, a):
        return (n + a - 1) & ~(a - 1)

    at = 0xb0
    n = {'WORD': 0, 'LONG': 0, 'SHORT': 0, 'COMPOUND': 0}
    for k in kinds:
        if k in ('WORD', 'LONG'):
            at = up(at, 4)
            LAYOUT[at + 4] = '%s%d' % ('w' if k == 'WORD' else 'l', n[k])
            at += 8
        elif k == 'SHORT':
            at = up(at, 2)
            LAYOUT[at + 2] = 's%d' % n[k]
            at += 4
        else:
            at = up(at, 2)
            LAYOUT[at] = 'c%d' % n[k]
            at += 4 + up(sizes[n[k]] if n[k] < len(sizes) else 0, 2)
        n[k] += 1
    return LAYOUT


PARAMED = [0]
AT_RE = re.compile(r'AT\((u?int(?:8|16|32)_t), (-?\d+)\)')
SLOT_RE = re.compile(r'SLOT\((-?\d+)\)')


def name_params(body, pbase, params):
    """The arguments a rule was called with, under their numbers.

    The frame holds them at the bottom, one word each, in the order they were
    handed over. Everything else in the frame is the rule's own working room
    and keeps its offset, because nothing so far says what any of it is for.
    """
    if params <= 0:
        return body
    top = pbase + 4 * params

    def at(m):
        off = int(m.group(2))
        if not (pbase <= off < top) or (off - pbase) % 4:
            return m.group(0)
        PARAMED[0] += 1
        return 'PARAM(%s, %d)' % (m.group(1), (off - pbase) // 4)

    def slot(m):
        off = int(m.group(1))
        if not (pbase <= off < top) or (off - pbase) % 4:
            return m.group(0)
        PARAMED[0] += 1
        return 'PARAMAT(%d)' % ((off - pbase) // 4)

    return [SLOT_RE.sub(slot, AT_RE.sub(at, l)) for l in body]


POP_RE = re.compile(r'^(\s*)POP\((r\d)\);$')
POPPED = [0]
DROPPED = [0]

# What may sit on the right of a load without the load being worth keeping.
# Anything here either sets a flag or moves the argument stack, and taking it
# out would take that with it.
DIRTY = ('ALU(', 'CMP(', 'IF(', 'CALL(', 'CALLW(', 'ARG(', 'POP(',
         'ENTER(', 'LANDING(', '=')


def join_pops(body):
    """Several pops in a row into one, the way the machine let go of them."""
    out = []
    i = 0
    while i < len(body):
        m = POP_RE.match(body[i])
        if not m:
            out.append(body[i])
            i += 1
            continue
        j = i
        while j < len(body) and body[j] == body[i]:
            j += 1
        POPPED[0] += j - i - 1
        out.append('%sPOP(%s, %d);' % (m.group(1), m.group(2), j - i))
        i = j
    return out


def drop_dead(body):
    """Loads into r0 that nothing reads.

    The compiler loaded a value into the accumulator and then pushed the same
    value, or loaded one and immediately loaded another over it. The load is
    only worth keeping if something can see it, so the walk forward stops at
    anything that reads r0 and at anything that leaves straight-line code.

    A call in between is not a reason to keep it. A call can go back to a
    landing, but a landing is a setjmp and the first thing it does on the way
    back is put setjmp's answer in r0, so what the load left there cannot be
    read on that path either.
    """
    dead = set()
    for i, line in enumerate(body):
        m = re.match(r'^\s*r0 = \((.*)\);$', line)
        if not m or any(d in m.group(1) for d in DIRTY):
            continue
        for j in range(i + 1, len(body)):
            l = body[j]
            t = l.strip()
            if (t.startswith('goto ') or t.startswith('if (')
                    or t.endswith('{') or t.endswith('}') or t.endswith(':;')
                    or t.startswith('LANDING') or t.startswith('ENTER')):
                break
            w = re.match(r'^\s*r0 = ', l)
            if 'r0' in (l[l.index('=') + 1:] if w else l):
                break
            if w:
                dead.add(i)
                DROPPED[0] += 1
                break
    return [l for i, l in enumerate(body) if i not in dead]


REACH = re.compile(r'\(\*\((u?int(?:8|16|32)_t) \*\)'
                   r'\(\(unsigned char \*\)\(intptr_t\)\((r\d)\)'
                   r' \+ (\d+)\)\)')


def name_globals(body):
    """Reaches through the state written as the variables they are.

    Only through a register that was loaded with the state and never loaded
    with anything else, so that a name is put on a reach only where the thing
    reached through is known to be the state.
    """
    holds = set()
    other = set()
    for line in body:
        m = re.match(r'\s*(r\d) = (.*);$', line)
        if not m:
            continue
        (holds if m.group(2) == '(FIELD(0))' else other).add(m.group(1))
    holds -= other
    if not holds:
        return body, set()
    where = layout()
    seen = set()

    def sub(m):
        t, reg, off = m.group(1), m.group(2), int(m.group(3))
        if reg not in holds or off not in where:
            return m.group(0)
        seen.add(where[off])
        NAMED[0] += 1
        return 'GLOBAL(%s, %s, %s)' % (t, reg, where[off])

    return [REACH.sub(sub, l) for l in body], seen



SIMPLE = {}


def wrappers():
    """The wrapper rules, each as the primitive it stands for.

    Two rules in three are not rules. They live in glob.obj, they call one
    runtime primitive with a few numbers baked in, and their name spells the
    numbers -- ZZbspush_ca__1 is bspush_ca with sixty-two. A call to one says
    nothing; the primitive with its numbers says what the rule does.

    Only the plain ones are taken: one call, nothing but numbers, words and
    the caller's own arguments pushed for it, and as many pushes as the call
    wants. A wrapper that computes something, or calls twice, keeps its name.
    """
    if SIMPLE:
        return SIMPLE
    c, rules = all_rules()
    for name, _obj, start, length in rules:
        if not name.startswith('ZZ'):
            continue
        try:
            insns = c.decode(start, length)
        except Exception:
            continue
        pushes, calls, bad = [], [], False
        for off in sorted(insns):
            shape, vals, ops, _t, _sz = insns[off]
            if shape[0] == 'push':
                pushes.append(ops[0][:2])
            elif shape[0] == 'call':
                calls.append((shape[1], vals[0]))
            elif shape[0] not in ('return', 'popn', 'popreg', 'load', 'store',
                                  'cmp', 'setarg'):
                bad = True
        if bad or len(calls) != 1 or len(pushes) != calls[0][1]:
            continue
        if any(k not in ('imm', 'slot', 'sym') for k, _v in pushes):
            continue
        SIMPLE[name] = (calls[0][0], pushes)
    return SIMPLE


def inlined(c, who, args):
    """One call site written as the primitive the wrapper stood for.

    The site's arguments arrive in the order they were pushed, so the last of
    them is the primitive's first. The wrapper's own pushes read the same way,
    which is why both are turned round here and the result reads as a call.
    """
    table = wrappers()
    if who not in table:
        return None
    prim, pushes = table[who]
    slots = [v for k, v in pushes if k == 'slot']
    if not slots or (max(slots) - 8) // 4 + 1 != len(args):
        return None
    out = []
    for kind, val in pushes:
        if kind == 'imm':
            v = c.imm[val]
            out.append('%d' % (v - 0x100000000 if v >= 0x80000000 else v))
        elif kind == 'sym':
            out.append('delta_sym_ref[%d]' % val)
        else:
            out.append(args[len(args) - 1 - (val - 8) // 4])
    out.reverse()
    WRAPPED[0] += 1
    return '%s, CALLW(%s, %s)' % (', '.join('ARG(%s)' % a for a in args),
                                  prim, ', '.join(out))


CALL_RE = re.compile(r'^(\s*)r0 = CALL\((\w+), (\d+)\);$')
ARG_RE = re.compile(r'^\s*ARG\((.*)\);$')

JOINED = [0]


def join_calls(code, body):
    """A call and the pushes that feed it, on one line.

    Only where every one of them is on the lines immediately above, so that
    what is joined is what was already together; a push the compiler put
    somewhere else stays where it is.
    """
    out = []
    i = 0
    while i < len(body):
        m = CALL_RE.match(body[i])
        if m:
            pad, who, want = m.group(1), m.group(2), int(m.group(3))
            args = []
            k = i - 1
            while len(args) < want and k >= 0 and ARG_RE.match(body[k]):
                args.append(ARG_RE.match(body[k]).group(1))
                k -= 1
            if want and len(args) == want and len(out) >= want:
                del out[len(out) - want:]
                args.reverse()
                said = (inlined(code, who, args)
                        if who.startswith('ZZ') and not FAITHFUL else None)
                if said is None:
                    said = '%s, CALL(%s, %d)' % (
                        ', '.join('ARG(%s)' % a for a in args), who, want)
                out.append('%sr0 = (%s);' % (pad, said))
                JOINED[0] += 1
                i += 1
                continue
        out.append(body[i])
        i += 1
    return out


def _whole(region):
    """Whether a run of lines opens and closes every block it mentions."""
    depth = 0
    for line in region:
        depth += line.count('{') - line.count('}')
        if depth < 0:
            return False
    return depth == 0


def _once(body, tails):
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
        if opposite(cond) is None:
            continue
        j = at.get(tgt)
        if j is None or j <= i + 1:
            continue
        # A region may hold a label something outside jumps to: C lets a goto
        # enter a block and means by it what the flat code meant, and the label
        # is there to say so. The exception is the two places a rule ends. Those
        # are what everything jumps to, and folding one inside a conditional
        # puts the place a rule gives up three levels in from where a reader
        # looks for it.
        inside = [k for k, where in at.items() if i < where < j and k in tails]
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
    out.append('%sif (%s) {' % (pad, opposite(cond)))
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
    if 'EVV_LAND_SAVE' not in body[i + 4]:
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


# Each of the machine's comparisons: whether it subtracts or ands, and how
# wide it works. Anything narrower than the whole is masked to that width
# first, which is why the width has to be carried about.
CMP_KIND = {'testl': ('test', 4), 'testw': ('test', 2), 'testb': ('test', 1),
            'cmpl': ('cmp', 4), 'cmpw': ('cmp', 2), 'cmpb': ('cmp', 1)}

# What a condition says after a comparison. The machine takes the first operand
# from the second, so the second is the one on the left of what comes out, and
# whether the comparison is signed is which flags the condition reads rather
# than anything about the operands. The sign flag on its own is not a
# comparison at all -- it is the sign of a difference that may have overflowed
# -- so a condition that reads it alone is left as it was. None do.
CMP_SAYS = {
    'e': ('%s == %s', True), 'ne': ('%s != %s', True),
    'l': ('%s < %s', True), 'ge': ('%s >= %s', True),
    'le': ('%s <= %s', True), 'g': ('%s > %s', True),
    'b': ('%s < %s', False), 'ae': ('%s >= %s', False),
    'be': ('%s <= %s', False), 'a': ('%s > %s', False),
}

# And after an and, where the carry and the overflow are both clear, so every
# condition is that value against nought. Above-or-equal is then always true
# and below never is; those two are left alone rather than written as a
# constant, which would read worse than the flag did.
TEST_SAYS = {
    'e': '%s == 0', 'be': '%s == 0', 'ne': '%s != 0', 'a': '%s != 0',
    's': '%s < 0', 'l': '%s < 0', 'ns': '%s >= 0', 'ge': '%s >= 0',
    'g': '%s > 0', 'le': '%s <= 0',
}

CMP_RE = re.compile(r'^CMP\((\w+), (.*)\);$')
ENVELOPE_RE = re.compile(r'^(?:LANDING|ENTER)\(')
READS_RE = re.compile(r'IF\((\w+)\)')

# What an operand reads that something else could write.
READS_MEM = re.compile(r'\bAT\(|\bFLD\(|\bPARAM\(|\bGLOBAL\(|\*\(')

# The machine's four flags, and which of them each thing touches. Most
# operations write all four, and the exceptions are what make this worth
# writing down: an increment or a decrement keeps the carry it was given, a
# shift leaves the carry and the overflow alone, and a multiply says nothing.
FLAGS = ('zf', 'sf', 'cf', 'of')
ALL_FLAGS = frozenset(FLAGS)
WRITES_FLAGS = {
    'incl': frozenset(('zf', 'sf', 'of')),
    'incw': frozenset(('zf', 'sf', 'of')),
    'decl': frozenset(('zf', 'sf', 'of')),
    'decw': frozenset(('zf', 'sf', 'of')),
    'shll': frozenset(('zf', 'sf')),
    'shlw': frozenset(('zf', 'sf')),
    'sarl': frozenset(('zf', 'sf')),
    'sarw': frozenset(('zf', 'sf')),
    'imull': frozenset(),
    'imulw': frozenset(),
}

# And the one operation that reads a flag rather than only writing it.
READS_FLAGS = {'sbbl': frozenset(('cf',))}

# What each condition looks at, so that a comparison is only kept for the
# flags something actually asks about.
COND_FLAGS = {
    'e': ('zf',), 'ne': ('zf',),
    'a': ('cf', 'zf'), 'ae': ('cf',), 'b': ('cf',), 'be': ('cf', 'zf'),
    'g': ('zf', 'sf', 'of'), 'ge': ('sf', 'of'),
    'l': ('sf', 'of'), 'le': ('zf', 'sf', 'of'),
    's': ('sf',), 'ns': ('sf',),
}

TESTED = [0, 0]


def _writes(line, reg):
    """Whether a line puts something in a register. Every way the writer has
    of doing that is here; a way missed would let a comparison be written
    against a value that had moved on."""
    return bool(re.search(r'\b%s\b\s*=[^=]' % reg, line)
                or re.search(r'SET(?:LOW|BYTE0|BYTE1)\(%s\b' % reg, line)
                or re.search(r'POP\(%s\b' % reg, line))


def _flow(body):
    """Where each line can go next. Straight on unless it says otherwise, and
    both ways at a branch or an arm."""
    at = {}
    for i, line in enumerate(body):
        m = LABEL_RE.match(line)
        if m:
            at[m.group(1)] = i
    n = len(body)
    succ = []
    for i, line in enumerate(body):
        t = line.strip()
        on = [i + 1] if i + 1 < n else []
        m = re.match(r'^(?:if \(.*\)|case -?\d+:) goto L(\d+);$', t)
        if m:
            succ.append(on + [at[m.group(1)]])
            continue
        m = re.match(r'^goto L(\d+);$', t)
        if m:
            succ.append([at[m.group(1)]])
            continue
        if t.startswith('RETURN('):
            succ.append([])
            continue
        succ.append(on)
    return succ


def _operands(text):
    """The two operands of a comparison, split at the comma between them and
    not at any comma inside either of them."""
    depth = 0
    for i, ch in enumerate(text):
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth -= 1
        elif ch == ',' and depth == 0:
            return text[:i], text[i + 1:].lstrip()
    return None, None


NATURAL = (
    (re.compile(r'^(AT|FLD)\((u?int(?:8|16|32)_t),'), 2),
    (re.compile(r'^GLOBAL\((u?int(?:8|16|32)_t),'), 2),
    (re.compile(r'^PARAM\((u?int(?:8|16|32)_t),'), 2),
    (re.compile(r'^\(\*\((u?int(?:8|16|32)_t) \*\)'), 1),
    (re.compile(r'^(LOW)\(r\d\)$'), 0),
    (re.compile(r'^(BYTE[01])\(r\d\)$'), 0),
)


def _natural(expr):
    """What an operand already is, as a width and whether it is signed. Every
    operand the writer emits is an int32_t, but most of them got there by
    widening something narrower, and one that was widened the way the machine
    would have widened it needs nothing said about it."""
    while expr.startswith('(int32_t)'):
        expr = expr[len('(int32_t)'):]
    if re.match(r'^-?\d+$', expr):
        return 4, True, expr
    for pat, group in NATURAL:
        m = pat.match(expr)
        if not m:
            continue
        what = m.group(group) if group else m.group(1)
        if what == 'LOW':
            return 2, False, expr
        if what.startswith('BYTE'):
            return 1, False, expr
        return (int(what.strip('u').replace('int', '').replace('_t', '')) // 8,
                not what.startswith('u'), expr)
    return 4, True, expr


def _at_width(expr, w, signed):
    """One operand, said at the width the machine compared it at.

    The machine masks both operands to that width before it compares them, so
    an operand wider than the comparison has to be cut down to it here. One
    that is already the width, and already signed or unsigned the same way, is
    left as it is -- and a cast to a word around it can go, because casting to
    a word and then to a half is the same as casting to the half.
    """
    t = ('' if signed else 'u') + {1: 'int8_t', 2: 'int16_t',
                                   4: 'int32_t'}[w]
    was, wsigned, bare = _natural(expr)
    if re.match(r'^-?\d+$', bare):
        v = int(bare)
        lo, hi = ((-(1 << (8 * w - 1)), 1 << (8 * w - 1)) if signed
                  else (0, 1 << (8 * w)))
        return bare if lo <= v < hi else '(%s)%s' % (t, bare)
    if was == w and wsigned == signed:
        return bare
    if w == 4 and signed and was <= 4:
        # Nothing to say: the operand is already the whole of what the machine
        # compared, however it came to be.
        return expr
    return '(%s)(%s)' % (t, bare)


def _said(what, cond):
    """What a condition says about a comparison, as C. None where it cannot be
    said, which leaves the flags where they were."""
    kind, w, a, b = what
    if kind == 'test':
        if cond not in TEST_SAYS:
            return None
        if a == b:
            v = _at_width(a, w, True)
        elif w != 4:
            v = '(int%d_t)((%s) & (%s))' % (w * 8, a, b)
        else:
            # Parenthesised, because C binds an and looser than a comparison
            # and would read this as anding with the answer.
            v = '((%s) & (%s))' % (a, b)
        return TEST_SAYS[cond] % v
    if cond not in CMP_SAYS:
        return None
    form, signed = CMP_SAYS[cond]
    return form % (_at_width(b, w, signed), _at_width(a, w, signed))


def _kills(line, what):
    """Whether a line could have moved either operand out from under a
    comparison that has already been made. A register it writes, or a store
    where the operand reads memory -- and a call can store anywhere."""
    _kind, _w, a, b = what
    both = a + ' ' + b
    for reg in set(re.findall(r'\br\d\b', both)):
        if _writes(line, reg):
            return True
    if READS_MEM.search(both):
        if 'CALL(' in line or 'memcpy' in line or 'memset' in line:
            return True
        m = re.match(r'^\s*(.*?) =[^=]', line)
        if m and READS_MEM.search(m.group(1)):
            return True
    return False


def direct_tests(body):
    """Every test of the flags, written as the comparison the machine made.

    The machine has no way to ask a question except to set its flags and then
    read one of them, and a quarter of the lines in a rule were that. Here the
    flags are a variable like any other, so a test can say what it tests: what
    a call answered against nought, a length against a limit, a bit against a
    mask.

    It is done only where every reader of a flag is reached by one comparison
    and nothing has moved either of its operands since. Where anything else can
    reach a reader, or a call has been made that could have stored over what
    was compared, the flags stay and the comparison stays with them.

    Each of the four flags is followed on its own, because the operations do
    not all write all four: increment and decrement keep the carry they were
    given, the shifts leave the carry and the overflow alone, and a multiply
    says nothing at all. A comparison taken away because nothing read its zero
    flag would otherwise take with it a carry that something still read.
    """
    if not any(CMP_RE.match(l.strip()) or ENVELOPE_RE.match(l.strip())
               for l in body):
        return body

    succ = _flow(body)
    n = len(body)

    # Every line that leaves a flag saying something new: which flags it
    # writes, and what they then say -- a comparison of two operands, or
    # nothing, where they mean whatever the operation left behind. The
    # envelope's own test is a real one, but its line cannot be taken away
    # because the macro is what carries it.
    gen = {}
    hidden = {}
    for i, line in enumerate(body):
        t = line.strip()
        if ENVELOPE_RE.match(t):
            gen[i] = (ALL_FLAGS, ('test', 4, 'r0', 'r0'), False)
            continue
        m = CMP_RE.match(t)
        if m:
            k = CMP_KIND.get(m.group(1))
            a, b = _operands(m.group(2)) if k else (None, None)
            gen[i] = ((ALL_FLAGS, (k[0], k[1], a, b), True) if k and a
                      else (ALL_FLAGS, None, False))
            continue
        if 'CMP(' in t:
            gen[i] = (ALL_FLAGS, None, False)
            continue
        m = re.search(r'\bALU\((\w+),', t)
        if m:
            gen[i] = (WRITES_FLAGS.get(m.group(1), ALL_FLAGS), None, False)
            if m.group(1) in READS_FLAGS:
                hidden[i] = READS_FLAGS[m.group(1)]

    empty = (frozenset(),) * len(FLAGS)

    def through(i, coming):
        """What each flag says after a line, given what it said before it."""
        bits, what, _keep = gen.get(i, (frozenset(), None, False))
        made = frozenset([(i, what)])
        line = body[i]
        return tuple(made if b in bits else
                     frozenset((d, None) if w and _kills(line, w) else (d, w)
                               for d, w in was)
                     for b, was in zip(FLAGS, coming))

    # What reaches each line, grown until it stops growing. Every line is
    # looked at once whether or not anything reaches it, or a rule whose first
    # line happens not to touch the flags is never walked at all: nothing
    # would reach the first comparison, so nothing would carry it forward and
    # every comparison would look unread. A line nothing reaches keeps
    # nothing, which is a refusal rather than a licence.
    state = [empty for _ in range(n)]
    work = list(range(n - 1, -1, -1))
    queued = set(range(n))
    while work:
        i = work.pop()
        queued.discard(i)
        now = through(i, state[i])
        for j in succ[i]:
            fresh = tuple(a | b for a, b in zip(state[j], now))
            if fresh != state[j]:
                state[j] = fresh
                if j not in queued:
                    queued.add(j)
                    work.append(j)

    def reaching(i, flags):
        out = set()
        for b in flags:
            out |= state[i][FLAGS.index(b)]
        return out

    needed = set()
    said = list(body)
    for i, line in enumerate(body):
        conds = READS_RE.findall(line)
        if conds:
            want = set()
            for c in conds:
                want.update(COND_FLAGS.get(c, ALL_FLAGS))
            came = reaching(i, want)
            whats = set(w for _d, w in came)
            one = list(whats)[0] if len(whats) == 1 else None
            says = ([_said(one, c) for c in conds] if one else [])
            if came and one and all(says):
                for c, how in zip(conds, says):
                    said[i] = said[i].replace('IF(%s)' % c, how)
                    TESTED[0] += 1
            else:
                needed.update(d for d, _w in came)
        if i in hidden:
            needed.update(d for d, _w in reaching(i, hidden[i]))

    keep = []
    for i, line in enumerate(said):
        _bits, what, deletable = gen.get(i, (None, None, False))
        if deletable and what and i not in needed:
            TESTED[1] += 1
            continue
        keep.append(line)
    return keep


RETURNED = [0]


def tail_returns(body):
    """A jump at the return, written as the return itself.

    The compiler put a rule's one way out at the bottom and jumped to it from
    everywhere, so a rule ends by saying where it is going rather than what it
    answers. The label stays where something falls into it and goes where
    nothing does any more.
    """
    at = {}
    for i, line in enumerate(body):
        m = LABEL_RE.match(line)
        if m:
            at[m.group(1)] = i
    ret = {}
    for lab, i in at.items():
        if i + 1 < len(body) and body[i + 1].strip().startswith('RETURN('):
            ret[lab] = body[i + 1].strip()
    if not ret:
        return body

    out = []
    for line in body:
        m = re.match(r'^(\s*)goto L(\d+);$', line)
        if m and m.group(2) in ret:
            out.append('%s%s' % (m.group(1), ret[m.group(2)]))
            RETURNED[0] += 1
        else:
            out.append(line)

    live = set()
    for line in out:
        live.update(JUMP_RE.findall(line))

    def gone(line):
        m = LABEL_RE.match(line)
        return m is not None and m.group(1) not in live

    return [l for l in out if not gone(l)]


LEFT = [0, 0]


def _loop_spans(body):
    """Where each loop opens and closes, innermost last, with the label at its
    top and the label just past its end. Depth is counted in braces rather than
    in indentation, because a line can open and close its own."""
    spans = []
    open_at = []
    depth = 0
    for i, line in enumerate(body):
        t = line.strip()
        before = depth
        depth += line.count('{') - line.count('}')
        if t == 'do {' or t == 'for (;;) {':
            open_at.append(('do' if t.startswith('do') else 'for', before, i))
        elif open_at and depth == open_at[-1][1] and line.count('}'):
            kind, was, start = open_at.pop()
            top = None
            m = (LABEL_RE.match(body[start + 1])
                 if start + 1 < len(body) else None)
            if m:
                top = int(m.group(1))
            follow = None
            for k in range(i + 1, len(body)):
                if not body[k].strip():
                    continue
                m = LABEL_RE.match(body[k])
                if m:
                    follow = int(m.group(1))
                break
            spans.append((start, i, kind, top, follow))
    return spans


def leave_loops(body):
    """A jump out of a loop said as leaving it, and a jump to the top of one
    said as going round again.

    Only for the loop a line is actually inside, and only where the place
    jumped to is the one the loop leaves to, or its own top: a break lands
    after the loop and nowhere else, so a jump anywhere further on stays the
    jump it was.

    Going round again is said only in a loop with nothing to test. In a
    do-while, C's continue goes to the test rather than to the top, which is
    not what a jump to the top meant.
    """
    inner = {}
    for start, end, kind, top, follow in _loop_spans(body):
        for k in range(start + 1, end):
            inner.setdefault(k, (kind, top, follow))

    out = list(body)
    for k, (kind, top, follow) in inner.items():
        m = BRANCH_RE.match(out[k]) or GOTO_RE.match(out[k])
        if not m:
            continue
        tgt = int(m.group(len(m.groups())))
        cond = m.group(2) if m.re is BRANCH_RE else None
        if tgt == follow:
            what, which = 'break', 0
        elif tgt == top and kind == 'for':
            what, which = 'continue', 1
        else:
            continue
        out[k] = ('%sif (%s) %s;' % (m.group(1), cond, what) if cond
                  else '%s%s;' % (m.group(1), what))
        LEFT[which] += 1

    live = set()
    for line in out:
        live.update(int(x) for x in JUMP_RE.findall(line))
    return [l for l in out
            if not (LABEL_RE.match(l) and int(LABEL_RE.match(l).group(1))
                    not in live)]


DROPPED_POPS = [0]
POPREG_RE = re.compile(r'POP\((r\d)(?:, (\d+))?\);')


def drop_pops(body):
    """A pop into a register nothing reads, as the letting go it is.

    A rule pushes what a call is to take and moves the stack back afterwards,
    and the machine's way of moving it back is to pop into a register. Where
    the rule never looks at that register anywhere, nothing was read back and
    the line says only that the arguments are gone, which is what DROP says.
    """
    read = set()
    for line in body:
        rest = POPREG_RE.sub('', line)
        for m in re.finditer(r'\br\d\b', rest):
            if rest[m.end():m.end() + 3] == ' = ':
                continue
            read.add(m.group(0))

    def one(m):
        if m.group(1) in read:
            return m.group(0)
        DROPPED_POPS[0] += 1
        return 'DROP(%s);' % (m.group(2) or '1')

    return [POPREG_RE.sub(one, l) for l in body]


ALTED = [0, 0]


def _chain(body, i):
    """The arms of a dispatch the compiler wrote as a chain of decrements.

    With few alternatives to choose between it did not build a jump table: it
    took one off the answer and jumped if that left nought, took another off
    and jumped again. So the place jumped to after the first decrement is the
    rule's first alternative, which is the one a jump table reaches through
    case nought -- a table is preceded by the same decrement, to bring an
    answer of one down to the first arm of the table. Both are counted from
    nought here so that both say the same thing.
    """
    arms = []
    k = 0
    n = len(body)
    while i < n:
        t = body[i].strip()
        if t.startswith('switch ('):
            return None, i
        if (t.startswith('POP(') or t.startswith('DROP(')
                or t.startswith('ARG(')):
            i += 1
            continue
        m = re.match(r'^(r\d) = \(ALU\(dec[lw], 0, \1\)\);$', t)
        if m:
            k += 1
            i += 1
            continue
        m = re.match(r'^if \((?:IF\(e\)|r\d == 0)\) goto (L\d+);$', t)
        if m and k:
            arms.append((k - 1, m.group(1)))
            i += 1
            continue
        break
    return (arms or None), i


def dispatch_names(body):
    """Every arm of a rule's backtracking dispatch, under the alternative it
    is.

    A rule asks backtrack_function which alternative to try next and then goes
    to it, and its compiler wrote that two ways: a jump table where there were
    several arms, and a chain of decrements of the answer where there were few.
    Both are the same question and both are the order the language wrote the
    alternatives in, so both are named the same way.

    A place two arms claim, or one that two names would land on, keeps its
    number: that is not one alternative.
    """
    claimed = {}
    kind = {}
    site = 0
    i = 0
    n = len(body)
    while i < n:
        t = body[i].strip()
        if t.startswith('switch ('):
            site += 1
            j = i + 1
            while j < n:
                m = re.match(r'^case (-?\d+): goto (L\d+);$', body[j].strip())
                if not m:
                    break
                new = 'alt%d_%s' % (site, m.group(1))
                claimed.setdefault(m.group(2), set()).add(new)
                kind[new] = 0
                j += 1
            i = j
            continue
        if 'backtrack_function' in t:
            arms, j = _chain(body, i + 1)
            if arms:
                site += 1
                for k, tgt in arms:
                    new = 'alt%d_%d' % (site, k)
                    claimed.setdefault(tgt, set()).add(new)
                    kind[new] = 1
                i = j
                continue
        i += 1

    names = {t: list(s)[0] for t, s in claimed.items() if len(s) == 1}
    taken = {}
    for t, new in names.items():
        taken.setdefault(new, []).append(t)
    names = {t: new for t, new in names.items() if len(taken[new]) == 1}
    for new in names.values():
        ALTED[kind[new]] += 1
    return names


def name_alternatives(body, names):
    """The names worked out before the rule was structured, put on."""
    if not names:
        return body
    pat = re.compile(r'\b(%s)\b' % '|'.join(sorted(names, key=len,
                                                   reverse=True)))
    return [pat.sub(lambda m: names[m.group(1)], l) for l in body]


TAILED = [0, 0]


def name_tails(body):
    """The two places a rule ends, under what they are.

    A rule that has matched calls succeed and answers nought; a rule that has
    not calls vretproc with 94 and answers that. Both sit at the bottom and
    everything jumps to them, which is why most of a rule's gotos are neither
    a loop nor a conditional: they are the language saying this attempt is over.
    Named, they say it.

    A label the dispatch has already claimed keeps that name, because which
    alternative a place is says more than what it does.
    """
    at = {}
    for i, line in enumerate(body):
        m = LABEL_RE.match(line)
        if m:
            at[m.group(1)] = i

    found = {}
    for lab, i in at.items():
        for k in range(i + 1, min(i + 8, len(body))):
            t = body[k].strip()
            if 'succeed,' in t:
                found[lab] = 'matched'
                break
            if 'vretproc,' in t:
                found[lab] = 'failed'
                break
            if t.startswith('RETURN(') or LABEL_RE.match(body[k]):
                break
    if not found:
        return body

    n = collections.Counter(found.values())
    seen = collections.Counter()
    names = {}
    for lab in sorted(found, key=lambda x: at[x]):
        what = found[lab]
        seen[what] += 1
        names['L' + lab] = (what if n[what] == 1
                            else '%s%d' % (what, seen[what]))
        TAILED[0 if what == 'failed' else 1] += 1

    pat = re.compile(r'\b(%s)\b' % '|'.join(sorted(names, key=len,
                                                   reverse=True)))
    return [pat.sub(lambda m: names[m.group(1)], l) for l in body]


def main():
    if len(sys.argv) > 1 and sys.argv[1] == 'all':
        names = every()
    elif len(sys.argv) > 1 and not sys.argv[1].isdigit():
        names = sys.argv[1:]
    else:
        names = smallest(int(sys.argv[1]) if len(sys.argv) > 1 else 100)

    done, refused = write(names)
    print('calls joined to their arguments: %d' % JOINED[0])
    print('wrappers inlined to the primitive they stand for: %d' % WRAPPED[0])
    print('reaches through the state named as the variable they are: %d over %d variables' % (NAMED[0], len(USED)))
    print('arms named as the alternative they are: %d in a table, %d in a'
          ' chain of decrements' % (ALTED[0], ALTED[1]))
    print('reaches into the frame named as the argument they are: %d'
          % PARAMED[0])
    print('tests of the flags written as the comparison they are: %d,'
          ' comparisons no longer needed: %d' % (TESTED[0], TESTED[1]))
    print('pops into a register nothing reads, said as letting go: %d'
          % DROPPED_POPS[0])
    print('jumps at the return, written as the return: %d' % RETURNED[0])
    print('pops joined: %d, loads into r0 that nothing reads: %d'
          % (POPPED[0], DROPPED[0]))
    print('branches turned into an if: %d, loops closed: %d'
          % (STRUCTURED[0], LOOPED[0]))
    print('jumps out of a loop said as leaving it: %d, jumps to the top said'
          ' as going round again: %d' % (LEFT[0], LEFT[1]))
    print('tails named: %d where the rule gives up, %d where it has matched'
          % (TAILED[0], TAILED[1]))
    print('landing places folded: %d, entries folded: %d'
          % (FOLDED[0], FOLDED[1]))
    print('rules where a throw could leave a register stale: %d, registers'
          ' kept for them: %d' % (STALE[0], STALE[1]))
    print('rules whose argument area could be bounded: %d, words of it'
          ' between them: %d' % (DEEP[0], DEEP[1]))
    print('%d of %d rules written to %s, over %d files'
          % (len(done), len(names),
             os.path.relpath(part_path(0), ROOT).replace('c00_', 'cNN_'),
             PARTS))
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
