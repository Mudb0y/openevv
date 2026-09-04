#!/usr/bin/env python3
"""Every function of ours that does nothing, beside what IBM's does.

There are three ways a name can be absent from this port and only two of
them are visible.

`make missing' asks what our code calls that nothing of ours defines, which
the linker can answer. `make objects' asks the reverse: which of IBM's
functions nothing here has written, which the linker never can, since a
function no rule calls is a function the link never asks for.

Neither can see the third: a function that exists, is called, and does
nothing. The symbol is there, so the link is content, and the census counts
it as answered. That is where the Delta runtime's printing layer sat for
months -- `print_lit', `print_var' and `print_stream' were empty, with a
comment above them explaining that printing belonged to the Delta debugger
and no target here wants a debugger. True of the trace, false of the whole:
printing is also how a language says what it decided, and the rules call
those three seventy-eight times.

So this finds them by shape rather than by comment -- a body that is only
argument casts and at most a return of a constant -- and then asks IBM's
object what it holds under the same name. Three piles come out.

Standing in front of something, which is the pile to read: ours does
nothing where IBM's has a body. Each of these is a place where a premise is
invisible, and each wants a reason written beside it.

Faithful: IBM's does nothing either. Seven instructions is the line, because
a thiscall method that only returns nought is seven -- push, move, push,
move, xor, leave, ret -- and a plain C one is four to six.

And ours alone: no counterpart in IBM's objects at all, which is the
porting layer and the shims that stand in for Windows.

It will not catch the next `print_lit' by itself: that one would appear
under `standing' with its comment saying why it was fine. What it does is
keep that pile short enough that a person can re-read every reason in an
afternoon, rather than leaving them scattered through comments nobody
revisits.

    tools/engine/stubs.py            the three piles
    tools/engine/stubs.py --all      every one, with its size

It wants IBM's objects in `analysis/<tag>'. The instruction counts come out
of the same index `census.py' builds, and the two share its cache.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import census                                             # noqa: E402

# What IBM's side has to hold before it counts as doing something. A thiscall
# method returning nought is seven instructions; a plain C one is fewer.
DOES_NOTHING = 7

# A literal a do-nothing function is allowed to answer with.
LITERAL = re.compile(r'^(?:-?\d+|0x[0-9a-fA-F]+|NULL|0L)$')


def strip_comments(s):
    return re.sub(r'/\*.*?\*/', ' ', s, flags=re.S)


def do_nothing():
    """Every function under src whose body does nothing at all."""
    found = []
    for root, _, files in os.walk(os.path.join(ROOT, 'src')):
        for fn in sorted(files):
            if not fn.endswith('.c'):
                continue
            path = os.path.join(root, fn)
            src = open(path, encoding='utf-8', errors='replace').read()
            for m in re.finditer(
                    r'^([A-Za-z_][\w \*\t]*?[\* ])(\w+)\(([^;{]*?)\)\s*\{',
                    src, re.M):
                name = m.group(2)
                if name in ('if', 'for', 'while', 'switch', 'return',
                            'sizeof'):
                    continue
                i = src.index('{', m.end() - 1)
                depth, j = 0, i
                while j < len(src):
                    if src[j] == '{':
                        depth += 1
                    elif src[j] == '}':
                        depth -= 1
                        if depth == 0:
                            break
                    j += 1
                body = strip_comments(src[i + 1:j])
                answer = 'void'
                empty = True
                for st in (x.strip() for x in body.split(';') if x.strip()):
                    if re.fullmatch(r'\(void\)\s*[\w\[\]\.\->]+', st):
                        continue
                    if st.startswith('return'):
                        v = st[len('return'):].strip()
                        if v == '' or LITERAL.match(v):
                            answer = v or 'void'
                            continue
                    empty = False
                    break
                if empty:
                    found.append((os.path.relpath(path, ROOT), name, answer))
    return found


def aliases():
    """Ours to IBM's, out of the ALIAS and MANGLED lines the sources carry.

    A mangled name may be split across two lines as a pair of C string
    literals, so the pieces are joined back up before they are believed."""
    out = {}
    for top in ('src', 'lang', 'rom'):
        for root, _, files in os.walk(os.path.join(ROOT, top)):
            for fn in files:
                if not fn.endswith(('.c', '.h')):
                    continue
                s = open(os.path.join(root, fn), encoding='utf-8',
                         errors='replace').read()
                for m in re.finditer(
                        r'ALIAS(?:_N)?\(\s*((?:"[^"]*"\s*)+),\s*\n?\s*"(\w+)"',
                        s):
                    ibm = "".join(re.findall(r'"([^"]*)"', m.group(1)))
                    out.setdefault(m.group(2), ibm)
                for m in re.finditer(
                        r'\b(\w+)\s*\([^;]*?\)\s*\n?\s*MANGLED\(\s*'
                        r'((?:"[^"]*"\s*)+)\)', s):
                    ibm = "".join(re.findall(r'"([^"]*)"', m.group(2)))
                    out.setdefault(m.group(1), ibm)
    return out


def main(argv):
    show_all = '--all' in argv

    tag = os.environ.get('EVV_CENSUS_TAG', 'enus')
    import glob
    objs = sorted(glob.glob(os.path.join(ROOT, 'analysis', tag, '*.obj')))
    if not objs:
        print("stubs: no objects in analysis/%s; docs/building.md says where"
              " IBM's SDK is" % tag, file=sys.stderr)
        return 2

    sizes = census.ibm_sizes(objs)
    alias = aliases()

    standing, faithful, ourown = [], [], []
    for path, name, answer in do_nothing():
        found = None
        for cand in (alias.get(name), '_' + name, name):
            if cand and cand in sizes:
                found = (cand, sizes[cand])
                break
            if cand and '_' + cand.lstrip('_') in sizes:
                key = '_' + cand.lstrip('_')
                found = (key, sizes[key])
                break
        if found is None:
            ourown.append((path, name, answer))
        elif found[1] > DOES_NOTHING:
            standing.append((path, name, answer, found[0], found[1]))
        else:
            faithful.append((path, name, answer, found[0], found[1]))

    print("Ours does nothing and IBM's does something. Read every one of"
          " these:\n")
    for path, name, answer, key, n in sorted(standing, key=lambda x: -x[4]):
        print("  %-38s %-30s %4d instructions in %s"
              % (path, name, n, key[:40]))

    print("\nOurs does nothing and so does IBM's: %d." % len(faithful))
    print("Ours alone, with no counterpart in IBM's objects: %d."
          % len(ourown))
    if show_all:
        print("\nFaithful:")
        for path, name, answer, key, n in sorted(faithful):
            print("  %-38s %-30s %d" % (path, name, n))
        print("\nOurs alone:")
        for path, name, answer in sorted(ourown):
            print("  %-38s %-30s -> %s" % (path, name, answer))

    print("\n%d functions in src do nothing; %d of them stand in front of"
          % (len(standing) + len(faithful) + len(ourown), len(standing)))
    print("something IBM wrote.")
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
