"""What type the pointer at one EVV_REF has, read out of the source.

A birth is a file and a line, and the line is where the pointer still has a C
type. Recovering that type is a small job rather than a general one, because
only four shapes occur: a plain identifier, a member of one, the address of
either, and a call. Each is a backward scan or a lookup away. Nothing here
parses C -- it reads declarations of the shapes that matter and says so when
it cannot.
"""

import os
import re

INNER = re.compile(r'EVV_REF\(')
IDENT = re.compile(r'^\s*&?\s*([A-Za-z_][A-Za-z0-9_]*)\s*$')
MEMBER = re.compile(r'^\s*&?\s*([A-Za-z_][A-Za-z0-9_]*)\s*'
                    r'(?:->|\.)\s*([A-Za-z_][A-Za-z0-9_]*)\s*$')
CALL = re.compile(r'^\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(')
DECL = re.compile(r'\b(?:const\s+|static\s+|extern\s+)*'
                  r'([A-Za-z_][A-Za-z0-9_]*)\s*\*\s*'
                  r'(?:const\s*)?([A-Za-z_][A-Za-z0-9_]*)\b')

# Words that look like a type in front of a star and are not one. `return *p'
# and `sizeof *p' both match the shape of a declaration and neither declares
# anything.
NOT_A_TYPE = frozenset((
    'return', 'sizeof', 'case', 'goto', 'if', 'while', 'for', 'do', 'else',
    'switch', 'break', 'continue', 'typedef', 'struct', 'union', 'enum',
))
FIELD = re.compile(r'^\s*(?:const\s+)?([A-Za-z_][A-Za-z0-9_]*)\s+\**'
                   r'([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\])?\s*;')
RET = re.compile(r'^\s*(?:static\s+|extern\s+)*'
                 r'([A-Za-z_][A-Za-z0-9_]*)\s*\*?\s*%s\s*\(')
TOP = re.compile(r'^[A-Za-z_][A-Za-z0-9_ \t*]*\([^;]*$')

_files = {}
_structs = None


def _read(root, name):
    if name not in _files:
        path = None
        for base, _dirs, files in os.walk(root):
            if name in files and '/build' not in base:
                path = os.path.join(base, name)
                break
        _files[name] = (open(path, errors='replace').read().splitlines()
                        if path else None)
    return _files[name]


def _balanced(text, at):
    """What is inside the EVV_REF that starts at *at*."""
    depth = 0
    for i in range(at, len(text)):
        if text[i] == '(':
            depth += 1
        elif text[i] == ')':
            depth -= 1
            if depth == 0:
                return text[at + 1:i]
    return None


def structs(root):
    """Every struct in the headers, as a field name to type map."""
    global _structs
    if _structs is not None:
        return _structs
    _structs = {}
    for base, _dirs, files in os.walk(root):
        if '/build' in base:
            continue
        for name in files:
            if not name.endswith('.h'):
                continue
            lines = open(os.path.join(base, name), errors='replace').read()
            for body, tag in re.findall(
                    r'(?:typedef\s+)?struct(?:\s+\w+)?\s*\{(.*?)\}\s*(\w+)\s*;',
                    lines, re.S):
                fields = {}
                for line in body.splitlines():
                    m = FIELD.match(line)
                    if m:
                        fields[m.group(2)] = m.group(1)
                _structs.setdefault(tag, {}).update(fields)
    return _structs


def _declared(text, line, want):
    """The type of an identifier, from the nearest declaration above."""
    for i in range(line - 1, -1, -1):
        for d in DECL.finditer(text[i]):
            if d.group(2) == want and d.group(1) not in NOT_A_TYPE:
                return d.group(1)
        if TOP.match(text[i]) and i < line - 1:
            break
    return None


def type_at(root, file, line):
    """The type of the pointer the EVV_REF on that line was given."""
    text = _read(root, file)
    if text is None:
        return None, 'no such file'
    if not 1 <= line <= len(text):
        return None, 'no such line'

    here = text[line - 1]
    m = INNER.search(here)
    if not m:
        return None, 'no EVV_REF on that line'
    inner = _balanced(here, m.end() - 1)
    if inner is None:
        return None, 'the EVV_REF runs past the line'

    m = IDENT.match(inner)
    if m:
        t = _declared(text, line, m.group(1))
        return (t, None) if t else (None, 'no declaration of ' + m.group(1))

    m = MEMBER.match(inner)
    if m:
        outer = _declared(text, line, m.group(1))
        if outer is None:
            return None, 'no declaration of ' + m.group(1)
        fields = structs(root).get(outer)
        if not fields:
            return None, 'no struct ' + outer
        t = fields.get(m.group(2))
        return (t, None) if t else (None, '%s has no %s' % (outer,
                                                            m.group(2)))

    m = CALL.match(inner)
    if m and m.group(1) == 'EVV_AT':
        # EVV_AT is the crossing back the other way and carries the type it
        # is producing as its first argument, so there is nothing to look up.
        args = inner[inner.find('(') + 1:]
        first = args.split(',', 1)[0].strip()
        base = first.replace('*', ' ').split()
        base = [w for w in base if w not in ('const', 'volatile')]
        return (base[0], None) if base else (None, 'EVV_AT with no type')

    if m:
        want = m.group(1)
        pat = re.compile(RET.pattern % re.escape(want))
        for name in (file, 'delta.h', 'delta.c', 'evv_arena.h'):
            lines = _read(root, name)
            if lines is None:
                continue
            for one in lines:
                got = pat.match(one)
                if got:
                    return got.group(1), None
        return None, 'no declaration of ' + want + '()'

    return None, 'shape not read: ' + inner.strip()[:44]
