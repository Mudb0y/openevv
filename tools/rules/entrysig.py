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

# What cannot be a return type, however much it looks like one. Without this
# the pattern below reads `else\n    memcpy(place, &out, 4);' as a declaration
# of memcpy taking three values, and the mask that follows hands memcpy two
# distances where it wanted addresses. It went unnoticed because under
# absolute addressing both arms of the crossing agree, so nothing could fail
# until the day a reference stopped being an address -- which is the whole
# reason the mask exists. A wrong mask is worse than a refused one.
NOT_A_TYPE = frozenset((
    'if', 'else', 'while', 'do', 'for', 'switch', 'case', 'default',
    'break', 'continue', 'goto', 'return', 'sizeof', 'typedef', 'struct',
    'union', 'enum'))

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
            text = open(os.path.join(base, fn), encoding="utf-8").read()
            text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
            for m in DECL.finditer(text):
                name = m.group(2)
                if name in _cache:
                    continue
                if m.group(1).strip().rstrip('*').strip() in NOT_A_TYPE:
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


# A module's rules are written with the masks above built into them, so a
# declaration that changes whether an argument is a pointer reaches a rule
# only when the rule code is written again. Depending on every file under src
# would rewrite every module, and the rules as C after them, on any edit to the
# machine. So the build asks instead, whenever src changes -- a fifth of a
# second -- whether the masks the written rule code holds are still what the
# declarations say, and only a module where one is not is written again. What
# is asked about is the rule code as it stands, not a record of how it was
# written, because more than one thing writes it: the upper-form check writes
# IBM's rules alone and builds them, and a record kept by the build would read
# that as stale and write the module's own over what was to be checked.

def _table(text, what):
    m = re.search(r'\b[a-z]+_%s\[\] = \{\n(.*?)\n\};' % what, text, re.S)
    return [] if m is None else [w.strip().rstrip(',')
                                 for w in m.group(1).splitlines()]


def _entries(rule_code):
    """The names a module's written rules call, the masks they hold, and
    which are the module's own rules: those are written with nought and
    called by the module's own name for them, never the bare one."""
    with open(rule_code, encoding='utf-8', errors='replace') as f:
        text = f.read()
    names = [n.strip('"') for n in _table(text, 'delta_rule_entry_name')]
    masks = [m.split('/*')[0].strip() for m in _table(text, 'delta_rule_argmask')]
    fns = [f.split('/*')[0].strip() for f in _table(text, 'delta_rule_entry')]
    own = [not f.endswith(')' + n) for f, n in zip(fns, names)]
    return names, masks, own


def disagreements(rule_code):
    """Every entry whose mask in the written rule code is not what its
    declaration says now."""
    names, masks, own = _entries(rule_code)
    wrong = []
    for n, held, mine in zip(names, masks, own):
        m = None if mine else mask(n)
        if m is not None and int(held.rstrip('u'), 0) != m:
            wrong.append('%s holds %s and its declaration says 0x%08x'
                         % (n, held, m))
    return wrong


def record(rule_code, path):
    """The timestamp the rule-code step depends on, and sets to the rule
    code's own once it has written it: made new only when a mask the rule
    code holds has moved, which is what sends the module round again."""
    if not os.path.exists(rule_code):
        return
    wrong = disagreements(rule_code)
    if wrong:
        with open(path, 'w', encoding='utf-8') as f:
            f.write(''.join(w + '\n' for w in wrong))
    elif not os.path.exists(path):
        open(path, 'w', encoding='utf-8').close()
        st = os.stat(rule_code)
        os.utime(path, ns=(st.st_atime_ns, st.st_mtime_ns))


def check(rule_code):
    """For a module whose rule code is kept by hand rather than written: the
    same question, and a failure rather than a rewrite, since nothing will
    write it again when a declaration changes."""
    wrong = disagreements(rule_code)
    for w in wrong:
        print('%s: %s' % (rule_code, w))
    return not wrong


if __name__ == '__main__':
    import sys
    if len(sys.argv) != 3 or sys.argv[1] not in ('record', 'check'):
        sys.exit('usage: entrysig.py record|check lang/<tag>')
    module = sys.argv[2].rstrip('/')
    rule_code = os.path.join(module,
                             'delta_rules_%s.c' % os.path.basename(module))
    if sys.argv[1] == 'record':
        record(rule_code, os.path.join(module, 'rules', '.declared'))
    else:
        sys.exit(0 if check(rule_code) else 1)
