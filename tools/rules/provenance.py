#!/usr/bin/env python3
"""What each rule was addressing, read out of the census.

src/delta/delta_prov.c is the other half and says why this exists. The short
of it: a rule reaches into memory through a register and a byte offset, and
nothing written down says what the register was holding. Until that is known,
an offset cannot become a field name, a layout cannot move, and the rules
cannot be raised above the machine they were compiled for.

The census answers it by asking the allocator at run time, over every case the
gate speaks. This reads what it left and says how far it got:

    tools/rules/provenance.py provenance.bin lang/enus/provenance-sites-enus.txt \
        [build/probe]

The third argument is the binary the census came out of, and is worth giving:
most of what allocates in the engine is a static function, which the running
process cannot name because a local symbol is not a dynamic one. nm can see
them, and the census records where the image was loaded, so the two together
turn an allocation site into a name.

A site of one kind is one whose object is known. A site of more than one is
polymorphic -- the same instruction addressing two different sorts of thing,
which is real and has to stay arithmetic until something finer separates the
paths. A site nothing reached is the one that matters most: it is where a
change to a layout would break something with no test to say so.

What a kind is: a block of the arena, the frame stack, one of the language's
own data stores copied out low, the C stack, or nowhere the engine owns. That
is as fine as the allocator can answer, and the footnote in the report says
why it cannot answer any finer.
"""

import collections
import os
import struct
import sys

KINDS = ('null', 'store', 'block', 'stack', 'elsewhere', 'outside')

HEAD = struct.Struct('<4IQ')
SITE = struct.Struct('<6Q4I')
NAME = struct.Struct('<I48s')

MAGIC = 0x32565250


def read_census(path):
    """The counts a run left, by site number, and the allocation site names."""
    with open(path, 'rb') as f:
        blob = f.read()

    magic, count, names, _spare, base = HEAD.unpack_from(blob, 0)
    if magic != MAGIC:
        raise SystemExit('provenance: %s is not a census' % path)

    at = HEAD.size
    seen = {}
    for i in range(count):
        row = SITE.unpack_from(blob, at + i * SITE.size)
        counts = dict(zip(KINDS, row[:6]))
        whence, many, nbytes, many_bytes = row[6], row[7], row[8], row[9]
        if any(counts.values()):
            seen[i] = (counts, whence, bool(many), nbytes, bool(many_bytes))

    at += count * SITE.size
    named = {}
    for i in range(names):
        whence, raw = NAME.unpack_from(blob, at + i * NAME.size)
        named[whence] = raw.split(b'\0', 1)[0].decode('ascii', 'replace')

    return count, seen, named, base


def symbols(binary):
    """Every defined symbol of the binary, by address, local ones included."""
    import subprocess

    out = []
    try:
        text = subprocess.run(['nm', '--defined-only', '-n', binary],
                              capture_output=True, text=True,
                              check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return out
    for line in text.splitlines():
        bits = line.split()
        if len(bits) >= 3 and bits[1] in 'tTwW':
            try:
                out.append((int(bits[0], 16), bits[2]))
            except ValueError:
                pass
    out.sort()
    return out


def name_of(whence, base, syms):
    """The function a truncated return address fell in.

    The full address agrees with the recorded one in its low thirty-two bits
    and lies in this image, which leaves three candidates and one of them
    right. Then it is an offset into the image, and nm is in those terms.
    """
    import bisect

    if not syms or base == 0:
        return None
    for step in (0, 1, -1):
        at = (base & ~0xffffffff) + whence + step * (1 << 32)
        if at < base:
            continue
        off = at - base
        i = bisect.bisect_right(syms, (off, '\xff')) - 1
        if 0 <= i < len(syms):
            # Only when it is inside the image at all; a candidate that lands
            # past the last symbol by a long way is the wrong one.
            if off - syms[i][0] < 0x10000:
                return syms[i][1]
    return None


def read_sites(path):
    """The site table the decompiler wrote: which rule each site is in."""
    out = {}
    with open(path) as f:
        for line in f:
            if line.startswith('#') or not line.strip():
                continue
            n, rule, off, kind = line.split()
            out[int(n)] = (rule, int(off), kind)
    return out


def main():
    if len(sys.argv) not in (3, 4):
        raise SystemExit('usage: provenance.py <census.bin> <sites.txt>'
                         ' [binary]')

    highest, seen, named, base = read_census(sys.argv[1])
    sites = read_sites(sys.argv[2])
    syms = symbols(sys.argv[3]) if len(sys.argv) == 4 else []

    total = len(sites)
    one, many, unreached = [], [], []
    by_kind = collections.Counter()
    by_whence = collections.Counter()
    pooled = [0]

    for n in sorted(sites):
        if n not in seen:
            unreached.append(n)
            continue
        counts, whence, several, nbytes, many_bytes = seen[n]
        kinds = [k for k in KINDS if counts[k]]
        # Over kinds alone. The allocator and the length were recorded too and
        # decide nothing, for the reason the footnote gives.
        if len(kinds) == 1:
            one.append((n, kinds[0], whence, nbytes))
            by_kind[kinds[0]] += 1
            if kinds[0] == 'block':
                who = named.get(whence) or name_of(whence, base, syms)
                by_whence[(who or '0x%08x' % whence, nbytes)] += 1
                if several or many_bytes:
                    pooled[0] += 1
        else:
            many.append((n, kinds, several, many_bytes))

    say = print
    say('sites the decompiler numbered: %d' % total)
    if highest > total:
        say('the census carries %d, which is more than the site table names;'
            ' the two are out of step' % highest)
    say('reached by some case:           %d' % (len(one) + len(many)))
    say('  addressing one sort of thing: %d' % len(one))
    say('  addressing more than one:     %d' % len(many))
    say('never reached at all:           %d' % len(unreached))
    say('')

    say('Of the sites whose object is known, what it was:')
    for kind, n in by_kind.most_common():
        say('  %-10s %d' % (kind, n))

    if by_whence:
        say('')
        say('For the ones in the arena, who allocated the block and how long'
            ' it is. Read this as a footnote and not as an answer:')
        for (who, nbytes), n in by_whence.most_common(25):
            say('  %-34s %8d bytes  %d sites' % (who, nbytes, n))
        say('')
        say('The machine sub-allocates every record it keeps out of pools --'
            ' one block for')
        say('the language heap and one for the frame stack -- so the allocator'
            ' names')
        say('whichever call happened to grow the pool, not what the object is,'
            ' and the')
        say('length is the pool\'s. %d sites saw more than one such name or'
            ' length, which' % pooled[0])
        say('says the same thing. Naming the record wants the 162 places a'
            ' reference is')
        say('made, where the pointer still has a C type, rather than the'
            ' allocator.')

    if many:
        say('')
        say('The polymorphic ones, which cannot be named until the paths are'
            ' separated:')
        for n, kinds, several, many_bytes in many[:20]:
            rule, off, what = sites[n]
            say('  %-28s %s at %-6d %s'
                % (rule, what, off, ' and '.join(kinds)))
        if len(many) > 20:
            say('  ... and %d more' % (len(many) - 20))

    if unreached:
        say('')
        rules = collections.Counter(sites[n][0] for n in unreached)
        say('The unreached sites fall in %d rules. The ten with most:'
            % len(rules))
        for rule, n in rules.most_common(10):
            say('  %-28s %d' % (rule, n))

    return 0


if __name__ == '__main__':
    sys.exit(main())
