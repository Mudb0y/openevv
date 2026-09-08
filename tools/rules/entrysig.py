"""What each entry the machine can call takes, read from its declaration.

A rule pushes values and the machine hands them over; where a reference is an
address the two are the same thing and nothing needs saying. Where it is not
-- a distance into the region, which is what lets that region live anywhere --
each argument has to be converted or not according to whether the entry wants
a pointer, and the only place that is written down is the entry's own C
declaration. So this reads them.

The answer is a mask per entry: bit n set means argument n is a pointer, and
bit 31 means the entry answers with one. A module's own rules are not in here
at all -- they take references and keep them, so their mask is nought.
"""

import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

DECL = re.compile(
    r'(?:^|\n)[ \t]*(?:extern[ \t]+|static[ \t]+)?'
    r'((?:const[ \t]+)?(?:unsigned[ \t]+|signed[ \t]+)?'
    r'[A-Za-z_][A-Za-z_0-9]*\s*\**)\s*'
    r'(?:STDCALL[ \t]+|THISCALL[ \t]+)?'
    r'([A-Za-z_][A-Za-z_0-9]*)[ \t]*\(([^)]*)\)[ \t]*'
    r'(?:MANGLED\([^)]*\)\s*)?\s*[;{]')

# The two the C library provides, which have no declaration in this tree.
LIBC = {'memcpy': ('v', ['p', 'p', 'v']),
        'memset': ('v', ['p', 'v', 'v'])}

RETURNS_POINTER = 1 << 31

_cache = None


def _split(text):
    out, depth, cur = [], 0, ''
    for ch in text:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def _read():
    """Every function this tree declares, as a return kind and argument kinds.

    The first declaration of a name wins. That is not arbitrary: a header
    declares what callers see and a definition has to agree with it, so either
    answers the only question asked here, which is what is a pointer.
    """
    global _cache
    if _cache is not None:
        return _cache
    _cache = {}
    for base, _dirs, files in os.walk(os.path.join(ROOT, 'src')):
        for fn in sorted(files):
            if not fn.endswith(('.c', '.h')):
                continue
            text = open(os.path.join(base, fn)).read()
            text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
            for m in DECL.finditer(text):
                name = m.group(2)
                if name in _cache:
                    continue
                args = _split(m.group(3))
                if args in ([], ['void']):
                    args = []
                _cache[name] = (
                    'p' if '*' in m.group(1) else 'v',
                    ['p' if ('*' in a or '[]' in a) else 'v' for a in args])
    for name, sig in LIBC.items():
        _cache.setdefault(name, sig)
    return _cache


def mask(name):
    """Which of an entry's arguments are pointers, as a bit each.

    None where nothing in the tree declares it, which the caller has to treat
    as a refusal rather than as nought: a wrong nought would hand a primitive
    a distance where it wanted an address, and the fault would be a wild
    pointer a long way from here.
    """
    sig = _read().get(name)
    if sig is None:
        return None
    ret, args = sig
    m = RETURNS_POINTER if ret == 'p' else 0
    for i, k in enumerate(args):
        if k == 'p':
            if i >= 31:
                raise SystemExit('%s: an entry with more than thirty-one'
                                 ' arguments needs a wider mask' % name)
            m |= 1 << i
    return m
