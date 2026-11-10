#!/usr/bin/env python3
"""The words in each of ETI Eloquence 6.1's dictionaries, out of Apple's module.

tools/module/lexicon.py reads our own dictionaries out of the tables the
engine is built from. This reads 6.1's out of the module Apple ships, so that
the two can be set side by side word for word: which words one engine knows
by heart and the other only guesses from the spelling is most of what
separates them, and it cannot be heard in a word list without knowing it.

Each dictionary's `<name>_actentries' is laid out as IBM's are -- a count of
two-byte offsets, then the entries, each a nul-terminated key in the
language's letter codes and four bytes of value with the action number in the
upper half -- with one difference: 6.1 keeps the offsets in the order the
search wants and the entries in another, so the offsets do not rise. Nothing
beside the table says how many entries it has, so the count is found as the
one whose offsets, sorted, start at nought and each land exactly where the
entry before ends. Keys are written with our own alphabet for the input
statement, which every English dictionary of both engines is keyed in; a
module whose letters were numbered differently would read as nonsense rather
than as plausible wrong words, since the vowels come first.

The module has to be the Mach-O Apple ships, not the ELF converted from it:
the conversion keeps no symbols and the tables are found by name. A fat one is
thinned to its x86-64 slice first.

usage: tools/module/lexicon64.py <module.dylib>            a line a dictionary
       tools/module/lexicon64.py <module.dylib> <name>     that one's words
"""

import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lexicon

INPUT_STMT = 1


def thin(path):
    """The x86-64 slice, as a file of its own when the module is fat."""
    with open(path, 'rb') as f:
        magic = f.read(4)
    if magic not in (b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'):
        return path
    out = os.path.join(tempfile.mkdtemp(), os.path.basename(path))
    subprocess.run(['llvm-lipo', '-thin', 'x86_64', path, '-o', out],
                   check=True)
    return out


def entries(data):
    """A dictionary's (key codes, action) pairs in the order its index
    gives them, or None when no count makes the offsets chain."""
    for count in range(2, len(data) // 2):
        offs = struct.unpack_from('<%dH' % count, data, 0)
        order = sorted(set(offs))
        if order[0] != 0 or len(order) != count:
            continue
        area = 2 * count
        spans = {}
        for i, o in enumerate(order):
            at = area + o
            end = data.find(b'\0', at)
            if end < 0 or end + 5 > len(data):
                break
            if i + 1 < count and area + order[i + 1] != end + 5:
                break
            spans[o] = (list(data[at:end]),
                        struct.unpack_from('<H', data, end + 3)[0])
        else:
            return [spans[o] for o in offs]
    return None


def dictionaries(module):
    """Every dictionary in the module: its name, then its words with their
    action numbers, in index order."""
    path = thin(module)
    blob = open(path, 'rb').read()
    nm = subprocess.run(['llvm-nm', '-n', path], check=True,
                        capture_output=True, text=True).stdout
    syms = []
    for line in nm.splitlines():
        w = line.split()
        if len(w) == 3:
            syms.append((int(w[0], 16), w[2].lstrip('_')))

    alpha = lexicon.alphabets(open(lexicon.LINK_C, encoding='utf-8').read())
    table = alpha.get(INPUT_STMT, [])
    once = lexicon.unique_names(table)

    out = []
    for i, (addr, name) in enumerate(syms):
        if not name.endswith('_actentries'):
            continue
        end = syms[i + 1][0] if i + 1 < len(syms) else len(blob)
        got = entries(blob[addr:end])
        if got is None:
            raise SystemExit('lexicon64: %s does not read as a dictionary'
                             % name)
        out.append((name[:-len('_actentries')],
                    [(lexicon.word(k, table, once), act) for k, act in got]))
    return out


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__.strip().split('\n\n')[-1])
    want = sys.argv[2] if len(sys.argv) > 2 else None
    for name, words in dictionaries(sys.argv[1]):
        if want is None:
            print('%-32s %5d entries' % (name, len(words)))
        elif name == want:
            for w, act in sorted(words):
                print('%s %d' % (w, act))


if __name__ == '__main__':
    main()
