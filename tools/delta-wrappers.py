#!/usr/bin/env python3
"""What each of the language's wrapper rules stands for.

Two thirds of the rules are not rules at all. They live in glob.obj, they have
no body worth the name, and each one exists to call a single runtime primitive
with a few numbers baked in -- ZZtestFldeq3_1_2 is testFldeq with three, one
and two. The compiler emitted one per distinct combination rather than pushing
the numbers at the call site.

That layer is the last thing standing between a decompiled rule and a
readable one: a call to ZZlprp_load_vvg__setd0090_0111 says nothing, and the
primitive with its arguments says what the rule does. This decodes them, and
says how many do not fit the shape, because a wrapper that does something else
is a wrapper that cannot be inlined.

The strings are read where they can be. A record's content is not ASCII: it is
one code per character in the alphabet its statement type declares, which is
the value-name table of that statement's first field, and delta-lexicon.py
already spells it. Wherever a call takes a statement, a length and a symbol in
that order, the symbol is spelled; elsewhere it keeps its number, because
guessing which alphabet a number means would put words in the language's mouth.
"""

import collections
import importlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
census = importlib.import_module('delta-census')
lexicon = importlib.import_module('delta-lexicon')

LINK_C = os.path.join(ROOT, 'lang', 'enus', 'delta_link_enus.c')


def decode(code, blobs, start, length):
    """What a wrapper stands for: the primitives it calls, each with what was
    pushed for it.

    The last thing pushed is the first argument, so a call's arguments come
    back reversed. Some wrappers call two primitives in a row, which their own
    names say by joining the two with a double underscore.
    """
    insns = code.decode(start, length)
    pushed = []
    calls = []
    for off in sorted(insns):
        shape, vals, ops, targets, size = insns[off]
        op = shape[0]
        if op == 'push':
            kind, val, _where = ops[0]
            if kind == 'imm':
                v = code.imm[val]
                pushed.append(v - 0x100000000 if v >= 0x80000000 else v)
            elif kind == 'slot':
                pushed.append('arg')
            elif kind == 'sym':
                pushed.append(text_of(code, blobs, val))
            else:
                pushed.append(kind)
        elif op == 'call':
            calls.append((shape[1], list(reversed(pushed[-vals[0]:]))
                          if vals[0] else []))
            pushed = pushed[:-vals[0]] if vals[0] else pushed
        elif op in ('return', 'popn', 'popreg', 'load', 'store', 'cmp',
                    'setarg'):
            continue
        else:
            return None, 'does %s as well' % op
    if not calls:
        return None, 'calls nothing'
    return calls, None


def spell(code, blobs, alpha, once, calls):
    """A record's content as the language wrote it.

    A statement, a length and a symbol together say where the bytes are and
    which alphabet reads them, so that is the shape looked for. Anything else
    keeps its number.
    """
    for _who, args in calls:
        for i in range(len(args) - 2):
            stmt, ln, sym = args[i], args[i + 1], args[i + 2]
            if not isinstance(stmt, int) or not isinstance(ln, int):
                continue
            if not isinstance(sym, str) or not sym.startswith('sym'):
                continue
            if stmt not in alpha or ln <= 0:
                continue
            val = int(sym[3:])
            if val >= len(code.syms):
                continue
            blob, at = code.syms[val]
            body = blobs.get(blob, b'')[at:at + ln]
            if len(body) == ln:
                args[i + 2] = '"%s"' % lexicon.word(body, alpha[stmt],
                                                    once[stmt])
    return calls


def text_of(code, blobs, val):
    """A string constant as the language wrote it, where it is one."""
    if val >= len(code.syms):
        return 'sym%d' % val
    blob, at = code.syms[val]
    body = blobs.get(blob, b'')[at:at + 20]
    if body[:1] and all(32 <= b < 127 or b == 0 for b in body):
        cut = body.split(b'\0')[0]
        if cut:
            return '"%s"' % cut.decode('ascii')
    return 'sym%d' % val


def main():
    code, rules = census.load()
    blobs = census.carve_blobs(open(census.CONSTS_C).read())
    alpha = lexicon.alphabets(open(LINK_C).read())
    once = {k: lexicon.unique_names(v) for k, v in alpha.items()}
    families = collections.Counter()
    refused = collections.Counter()
    good = 0
    spelled = 0
    show = []
    interesting = []

    for name, obj, start, length in rules:
        if not name.startswith('ZZ'):
            continue
        what, why = decode(code, blobs, start, length)
        if what is None:
            refused[why] += 1
            continue
        good += 1
        what = spell(code, blobs, alpha, once, what)
        said = any(isinstance(a, str) and a.startswith('"')
                   for _w, args in what for a in args)
        if said:
            spelled += 1
        for who, _args in what:
            families[who] += 1
        if said:
            interesting.append((name, what))

    print('wrapper rules: %d, of which one primitive with numbers: %d'
          % (good + sum(refused.values()), good))
    if refused:
        for why, n in refused.most_common():
            print('  %4d %s' % (n, why))

    print('carrying a word of the language: %d' % spelled)

    print('\nthe primitives they stand for, most used first:')
    for who, n in families.most_common(20):
        print('  %-22s %4d wrappers' % (who, n))

    print('\nas they read, taking every fortieth so the spread shows:')
    for name, calls in interesting[::max(1, len(interesting) // 16)][:16]:
        said = '; '.join('%s(%s)' % (who, ', '.join(str(a) for a in args))
                         for who, args in calls)
        print('  %-34s %s' % (name, said))
    return 0


if __name__ == '__main__':
    sys.exit(main())
