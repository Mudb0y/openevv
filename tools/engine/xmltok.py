#!/usr/bin/env python3
"""The XML scanner's automaton, out of IBM's object.

`xmltok.obj' is not hand-written code: it is flex 2.5.4 output, and the
grammar it was generated from is not in the SDK. What is in the object is
the automaton flex built out of that grammar -- eight tables, three and a
half kilobytes of them -- and the skeleton that walks them, which is flex's
own and the same in every scanner it has ever generated.

So the skeleton is written out by hand in `src/eci/ssml/eci_xmltok.c', where it can
be read, and the tables are lifted here. Writing a scanner to the same
interface instead would have meant guessing the grammar from the tables and
then hoping every corner of it agreed; taking the automaton means the
tokenisation is the same by construction, on every input including the ones
nobody thought of.

The tables are the same in every one of the nine languages' object sets,
which was checked: this is engine code and not a language's data.

usage: lift-xmltok.py [--check] [object]

With no argument it reads analysis/enus/xmltok.obj and writes
src/eci/ssml/eci_xmltok_tables.c. With --check it writes nothing and answers whether
what is in the tree is what the object says.
"""

import os
import subprocess
import sys

# tools/evv.py says where the tree is, so that no tool counts directories to
# find it. The one thing this line has to know is that the directory above a
# tool's group is tools itself.
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from evv import ROOT

# Which of the object's sections the tables are in, and where each starts.
# The names are flex's own. A table of two-byte entries is a `short' in
# flex's output; the two of four-byte entries are declared `int32_t' there
# and only their low byte is ever read, which is what the skeleton does.
TABLES = [
    ('yy_acclist', 0x000, 2),
    ('yy_accept',  0x0d0, 2),
    ('yy_ec',      0x220, 4),
    ('yy_meta',    0x620, 4),
    ('yy_base',    0x688, 2),
    ('yy_def',     0x7f0, 2),
    ('yy_nxt',     0x958, 2),
    ('yy_chk',     0xb78, 2),
]

HEAD = '''/* The XML scanner's automaton, lifted out of IBM's object.
 *
 * Written by tools/engine/xmltok.py. Do not edit.
 *
 * These are flex 2.5.4's own tables for the grammar IBM wrote and did not
 * ship. src/eci/ssml/eci_xmltok.c is the skeleton that walks them, written out by
 * hand there because it is flex's and not IBM's; what is here is the
 * automaton, and it is IBM's. See NOTICE.
 *
 * Nine start conditions, forty-eight rules, twenty-six equivalence classes
 * and a hundred and sixty-six states.
 */

#include <stdint.h>
#include "eci_xmltok.h"

'''


def coff_sections(path):
    """Every section of a COFF object: name, size and its bytes.

    Read here rather than through llvm-objdump because the object has eight
    sections called .rdata and a dump groups them by name.
    """
    raw = open(path, 'rb').read()
    count = int.from_bytes(raw[2:4], 'little')
    opt = int.from_bytes(raw[16:18], 'little')
    base = 20 + opt
    out = []
    for i in range(count):
        h = raw[base + i * 40: base + (i + 1) * 40]
        name = h[0:8].rstrip(b'\0').decode('ascii', 'replace')
        size = int.from_bytes(h[16:20], 'little')
        where = int.from_bytes(h[20:24], 'little')
        out.append((name, size, raw[where:where + size] if where else b''))
    return out


def section_bytes(obj):
    """The one .rdata section the tables live in.

    It is the only section in the object as long as the tables are, and the
    length is not a coincidence: the eight of them tile it exactly, which is
    checked below."""
    want = TABLES[-1][1]
    for name, size, blob in coff_sections(obj):
        if name == '.rdata' and size > want:
            return blob
    raise SystemExit('engine/xmltok: no table section in %s' % obj)


def slice_tables(blob):
    ends = [start for _, start, _ in TABLES[1:]] + [len(blob)]
    out = []
    for (name, start, width), end in zip(TABLES, ends):
        chunk = blob[start:end]
        count = len(chunk) // width
        values = []
        for i in range(count):
            v = int.from_bytes(chunk[i * width:(i + 1) * width], 'little',
                               signed=(width == 2))
            values.append(v)
        out.append((name, width, values))
    return out


def emit(tables):
    lines = [HEAD]
    for name, width, values in tables:
        ctype = 'int16_t' if width == 2 else 'int32_t'
        lines.append('const %s %s[%d] = {' % (ctype, name, len(values)))
        per = 12 if width == 2 else 10
        for i in range(0, len(values), per):
            row = ', '.join('%6d' % v for v in values[i:i + per])
            lines.append('    ' + row + ',')
        lines.append('};')
        lines.append('')
    return '\n'.join(lines)


def main():
    args = [a for a in sys.argv[1:]]
    check = '--check' in args
    args = [a for a in args if a != '--check']
    obj = args[0] if args else os.path.join(ROOT, 'analysis', 'enus',
                                            'xmltok.obj')
    out = os.path.join(ROOT, 'src', 'eci_xmltok_tables.c')

    text = emit(slice_tables(section_bytes(obj)))

    if check:
        have = open(out).read() if os.path.exists(out) else ''
        if have == text:
            print('xmltok: the tables in the tree are what the object says')
            return 0
        print('xmltok: they differ', file=sys.stderr)
        return 1

    open(out, 'w').write(text)
    print('wrote %s' % out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
