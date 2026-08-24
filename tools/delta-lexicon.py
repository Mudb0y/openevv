#!/usr/bin/env python3
"""The language's dictionaries as words, out of the tables the engine reads.

A dictionary is an index of two-byte offsets followed by its entries laid end
to end, each a nul-terminated key and then the value the rules act on. The key
is not text: it is one code per character in the alphabet the statement type
declares, and that alphabet is the value-name table of the statement's first
field, which the statement table already carries.

Named with a dictionary it prints that one, otherwise a line per dictionary
saying how many entries it holds and showing the first few.

Which language is `EVV_LANG_DIR', as everywhere else, and English without
one.
"""

import importlib
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Which language. EVV_LANG_DIR is the same variable the decompiler and the
# census read, and English without one, so nothing that used to work needs
# saying differently.
LANG_DIR = os.environ.get('EVV_LANG_DIR', os.path.join(ROOT, 'lang', 'enus'))
TAG = os.path.basename(os.path.normpath(LANG_DIR))
SETS_C = os.path.join(LANG_DIR, 'delta_sets_%s.c' % TAG)
LINK_C = os.path.join(LANG_DIR, 'delta_link_%s.c' % TAG)
CONSTS_C = os.path.join(LANG_DIR, 'delta_consts_%s.c' % TAG)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
census = importlib.import_module('delta-census')
arms_mod = importlib.import_module('delta-arms')

ENTRY_BYTES = 0x28

# A dictionary says what to do by an action number, and the rule of the same
# name dispatches on it with one switch, whose arms are numbered from one.
# What the arm then lays down is a record of one statement type or another --
# a pronunciation for most of them, but a run of characters for the ones that
# spell a thing out, such as the currencies and the abbreviations. Which it is
# shows in the codes: a record only fits the alphabet it was written in.
RECORD_STMTS = (2, 1)


def span(text, name):
    at = text.index(name)
    open_at = text.index('{', at)
    close_at = text.index('\n};', open_at)
    return text[open_at + 1:close_at]


def carve_bytes(text, name):
    body = re.sub(r'/\*.*?\*/', '', span(text, name), flags=re.S)
    return bytes(int(v) for v in body.replace('\n', '').split(',') if v.strip())


def carve_starts(text, name):
    """Where each dictionary begins in the store, and what it is called. The
    name is only in the comment beside it, which is the only place the
    generated table keeps it."""
    out = []
    for line in span(text, name).splitlines():
        m = re.search(r'\+\s*(\d+),\s*/\*\s*(\S+?)_actentries', line)
        if m:
            out.append((int(m.group(1)), m.group(2)))
        else:
            m = re.search(r'\+\s*(\d+),', line)
            if m:
                out.append((int(m.group(1)), '?'))
    return out


ESCAPES = {'n': '\n', 't': '\t', 'r': '\r', '"': '"', "'": "'",
           '\\': '\\', '0': '\0', 'a': '\a', 'b': '\b', 'f': '\f',
           'v': '\v'}


def unescape(s):
    """A C string literal's own text. The alphabet has characters in it that
    only a literal can spell, so leaving these as they were written would put
    a backslash into a word."""
    out, i = [], 0
    while i < len(s):
        if s[i] != '\\':
            out.append(s[i])
            i += 1
            continue
        i += 1
        if i < len(s) and s[i] in '01234567':
            j = i
            while j < len(s) and j < i + 3 and s[j] in '01234567':
                j += 1
            out.append(chr(int(s[i:j], 8)))
            i = j
        elif i < len(s):
            out.append(ESCAPES.get(s[i], s[i]))
            i += 1
    return ''.join(out)


def alphabets(text):
    """One code-to-name table per statement type, taken from the first field
    of each, which is the field a dictionary key is written in."""
    strings = {k: unescape(v) for k, v in
               re.findall(r'static const char (s\d+)\[\] = "((?:[^"\\]|\\.)*)"',
                          text)}
    out = {}
    for m in re.finditer(r'static const char \*const v(\d+)_0\[\] = \{([^}]*)\}',
                         text):
        names = [n.strip() for n in m.group(2).split(',') if n.strip()]
        out[int(m.group(1))] = [strings.get(n, n) for n in names]
    return out


def plain(name):
    """Whether a code can be written as itself in a word."""
    return len(name) == 1 and 33 <= ord(name) < 127 and name not in '[]'


def word(codes, alpha, once=None):
    """A key read back as text, in a form that reads back the same way. A code
    that spells one ordinary character is that character; anything else is
    bracketed, by name where the name picks it out on its own and by number
    where two codes share a name."""
    parts = []
    for c in codes:
        name = alpha[c] if c < len(alpha) else ''
        if plain(name) and (once is None or once.get(name) == c):
            parts.append(name)
        elif (name and '[' not in name and ']' not in name
              and once is not None and once.get(name) == c):
            parts.append('[%s]' % name)
        else:
            parts.append('[#%d]' % c)
    return ''.join(parts)


def unique_names(alpha):
    """Name to code, for the names only one code answers to."""
    seen = {}
    for c, name in enumerate(alpha):
        seen.setdefault(name, []).append(c)
    return {n: cs[0] for n, cs in seen.items() if len(cs) == 1}


def codes_of(text, alpha, once):
    """A written key back to the codes it stands for."""
    out, i = [], 0
    while i < len(text):
        if text[i] == '[':
            end = text.find(']', i)
            if end < 0:
                raise ValueError('a bracket is opened and not closed in %r'
                                 % text)
            body = text[i + 1:end]
            if body.startswith('#'):
                out.append(int(body[1:]))
            elif body in once:
                out.append(once[body])
            else:
                raise ValueError('the language has no character called %r'
                                 % body)
            i = end + 1
        else:
            if text[i] not in once:
                raise ValueError('the language has no character %r; one it '
                                 'does not spell as an ordinary letter goes '
                                 'in brackets' % text[i])
            out.append(once[text[i]])
            i += 1
    return out


LOADED = []


def rules():
    """The lifted rules, read once and shared."""
    if not LOADED:
        LOADED.append(arms_mod.Rules())
    return LOADED[0]


# How the pieces of what a word says are strung together when it is written
# out: `then' between the pieces of one word laid down in several, `or' between
# two readings a word has depending on what follows it.
THEN = ' then '
OR = ' or '


def actions(rule_name):
    """What each action of a dictionary lays down, as the pieces it is written
    out in: a list of (codes, what goes in front of them). Most actions lay one
    record and so have one piece with nothing in front."""
    a = arms_mod.Arms(rules(), rule_name)
    if not a.ok:
        return {}

    def codes(part):
        body = rules().blobs.get(part.blob, b'')
        return list(body[part.off:part.off + part.length])

    out = {}
    for act, r in a.records().items():
        out[act] = [(list(rules().blobs.get(r.blob, b'')
                          [r.off:r.off + r.length]), '')]

    for act in range(1, len(a.arms) + 1):
        if act in out:
            continue
        # A word laid down in pieces, then a word with two readings. Both are
        # second tries: one record found either way came off a path the first
        # try refused for a reason, and taking it would undo that.
        for how, joiner in ((a.parts, THEN), (a.ways, OR)):
            got = how(act)
            if got and len(got) > 1:
                out[act] = [(codes(p), '' if n == 0 else joiner)
                            for n, p in enumerate(got)]
                break
    return out


def render(segments, kind, alpha):
    """What an action says, written out."""
    table = alpha.get(kind, [])
    once = unique_names(table)
    out = []
    for codes, joiner in segments:
        out.append(joiner)
        out.append(sound_text(codes, table, once) if kind == 2
                   else word(codes, table, once))
    return ''.join(out)


def parse(text, kind, alpha):
    """And back into the pieces it was written out in."""
    table = alpha.get(kind, [])
    once = unique_names(table)
    out, rest, joiner = [], text, ''
    while True:
        at_then = rest.find(THEN)
        at_or = rest.find(OR)
        cuts = [x for x in (at_then, at_or) if x >= 0]
        if not cuts:
            head, nxt, rest = rest, None, ''
        else:
            cut = min(cuts)
            nxt = THEN if cut == at_then else OR
            head, rest = rest[:cut], rest[cut + len(nxt):]
        out.append((sound_codes(head, once) if kind == 2
                    else codes_of(head, table, once), joiner))
        if nxt is None:
            return out
        joiner = nxt


def sound_text(codes, alpha, once):
    """A pronunciation as ETI's own phone letters, spaced. A code no name
    picks out on its own is written by number."""
    parts = []
    for c in codes:
        name = alpha[c] if c < len(alpha) else ''
        parts.append(name if name and once.get(name) == c else '#%d' % c)
    return ' '.join(parts)


def sound_codes(text, once):
    out = []
    for t in text.split():
        if t.startswith('#'):
            out.append(int(t[1:]))
        elif t in once:
            out.append(once[t])
        else:
            raise ValueError('the language has no phone called %r' % t)
    return out


def choose(records, alphas):
    """Which statement type a dictionary's records are written in. One
    dictionary lays down one kind of record, so this is settled for the whole
    of it.

    The two alphabets cannot be weighed against each other by how many
    records they fit, because the character one has four times the codes and
    so fits almost anything. A dictionary counts as pronunciations unless a
    real share of its records will not sit in the phone alphabet at all."""
    real = [codes for group in records for codes, _j in group if codes]
    if not real:
        return RECORD_STMTS[0]
    phone = alphas.get(2, [])
    fits = sum(1 for r in real if all(c < len(phone) for c in r))
    return 2 if fits >= 0.9 * len(real) else 1


def dictionaries():
    """Every dictionary as it lies: what its table entry says, where it
    begins in the store, and its entries in the order the index puts them,
    each with where it sits in the store and the value behind its key."""
    sets = open(SETS_C).read()
    alpha = alphabets(open(LINK_C).read())
    act_table = carve_bytes(sets, 'act_table[]')
    store = carve_bytes(sets, 'actent_store[]')
    starts = carve_starts(sets, 'actent_all[]')

    out = []
    for i, (base, name) in enumerate(starts):
        e = act_table[i * ENTRY_BYTES:(i + 1) * ENTRY_BYTES]
        if len(e) < ENTRY_BYTES:
            break
        count = struct.unpack_from('<i', e, 0x0c)[0]
        area = base + 2 * count
        offsets = [o & 0xffff for o in
                   struct.unpack_from('<%dh' % count, store, base)]

        entries = []
        for off in offsets:
            at = area + off
            end = store.index(b'\0', at)
            entries.append((off, list(store[at:end]), store[end + 1:end + 5]))

        out.append({'name': name, 'base': base, 'stmt': e[0x08],
                    'width': e[0x18], 'count': count, 'area': area,
                    'entries': entries})
    return alpha, act_table, store, out


def main():
    alpha, act_table, store, dicts = dictionaries()

    want = sys.argv[1] if len(sys.argv) > 1 else None
    total = 0

    for d in dicts:
        name, count = d['name'], d['count']
        table = alpha.get(d['stmt'], [])
        once = unique_names(table)
        entries = [(word(k, table, once), v) for _off, k, v in d['entries']]

        total += count
        if want and name != want:
            continue

        laid = actions(name)

        said = []
        for key, value in sorted(entries):
            act = struct.unpack_from('<H', value, 2)[0]
            said.append((key, act, laid.get(act, [])))

        holds = choose([r for _k, _a, r in said], alpha)

        if not want:
            got = sum(1 for _k, _a, r in said if r)
            print('%-32s %5d entries, %5d with a record, written as %s'
                  % (name, count, got,
                     'sound' if holds == 2 else 'characters'))
            continue

        print('%-32s statement %d, width %d, %5d entries'
              % (name, d['stmt'], d['width'], count))
        for key, act, run in said:
            print('    %-28s %4d  %s'
                  % (key, act, render(run, holds, alpha) if run else ''))

    if not want:
        print()
        print('%d dictionaries, %d entries' % (len(dicts), total))


if __name__ == '__main__':
    main()
