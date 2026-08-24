#!/usr/bin/env python3
"""The language's dictionaries as a file a person can edit, and back again.

Which language is `EVV_LANG_DIR', the same variable the decompiler and the
census read, and English without one.

`dump' writes lang/<tag>/<tag>.dict out of the tables and the rules.
`build' reads that file and lays both back down: the entries, the index in
front of each dictionary, where each dictionary begins, the count in each
table entry, and -- where a pronunciation has been changed -- a record of its
own in the constant blob with the rule's switch pointed at it.
`where' says which two words a new one belongs between, since the order is
the engine's and not the alphabet's, and `find' says which dictionaries hold a
word at all, since there are twenty-eight of them and a word can be in
several.

A word written with `new' where its action number goes is one being added.
It is given a spare arm of its rule if there is one going, and otherwise an
arm of its own, and the number it was given is written back into the file.

The file is in the order the engine searches, which is by the language's own
character codes and not by letter, because that order is the one thing about
the layout that is observable. Six dictionaries hold the same word twice with
different actions, so which of the two the search lands on decides what is
said, and keeping the order keeps the answer. Where the entries then lie in
the store is not observable and is not kept.

What a word says is a property of its action and not of the word. Two words on
the same action therefore say the same thing, and a word given something else
to say is moved onto an action of its own rather than being refused; whichever
of them still wants what the action already says keeps it. The same happens
when a pronunciation cannot be changed where it stands. Both are changes in
what the dictionary means, so both are reported.

A word laid down in more than one piece has its pieces written out in order
with `then' between them, and a word with two readings has both with `or'
between them. Pieces can be changed one at a time; readings cannot, since
which of the two is taken is decided in shared code. An entry with nothing
after its action says nothing this can read faithfully and is left alone
rather than half-reported.

Building checks itself: it reads its own work back and holds every word, value
and pronunciation against what was there before.
"""

import collections
import importlib
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Which language. EVV_LANG_DIR is the same variable the decompiler and the
# census read, and English without one, so nothing that used to work needs
# saying differently.
LANG_DIR = os.environ.get('EVV_LANG_DIR', os.path.join(ROOT, 'lang', 'enus'))
TAG = os.path.basename(os.path.normpath(LANG_DIR))
SETS_C = os.path.join(LANG_DIR, 'delta_sets_%s.c' % TAG)
DICT_FILE = os.path.join(LANG_DIR, '%s.dict' % TAG)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
lex = importlib.import_module('delta-lexicon')
arms_mod = importlib.import_module('delta-arms')

ENTRY_BYTES = 0x28
VALUE_BYTES = 4
SOUND_STMT = 2
CHAR_STMT = 1

HEADER = """\
# The language's dictionaries. Written by tools/delta-dict.py from the tables
# and the rules, and read back by it to lay them down again.
#
# One section per dictionary, and inside it one entry per line, indented: the
# word, then the two counts the rule steps the spine by, then the action
# number it dispatches on, then what it says.
#
# What it says is written in ETI's phone letters where the section says sound,
# spaced one phone apart, and in the language's own characters where it says
# characters. A word laid down in more than one piece has its pieces written
# out in order with `then' between them -- the currencies spell an
# abbreviation, then a space, then a name -- and any piece the rule names
# itself rather than the word is shared with every word using it and cannot be
# changed for one of them, and neither can one the rule that lays it names for
# itself, which is how a suffix that always says the same thing is written.
# A word read one way before one thing and another
# way before another has both written out with `or' between them; those are
# shown as they are and cannot be changed, since which of the two is taken is
# decided in code the other words using that test run through. A phone or a character that no name picks out on its own is
# written as a hash and its number. An entry with nothing after its action
# lays no record down, or lays one down a way this cannot yet read.
#
# To add a word, put a line in with the word `new' where its action number
# goes and say what it is to sound like. Building gives it an action of its
# own and writes the number back here. `delta-dict.py where <dictionary>
# <word>' says which two words it belongs between, and `delta-dict.py find
# <word>' says which dictionaries hold it and what each says it sounds like.
#
# What a word says belongs to its action rather than to the word, so two words
# sharing an action say the same thing. Give one of them something else to say
# and it is moved onto an action of its own, and building says so.
#
# The order is the one the engine searches in, which is by the language's own
# character codes rather than by letter, so the vowels come first. A word put
# in the wrong place will not be found; building says so rather than laying
# down a dictionary that cannot be read.
#
# A word is written as its characters. A character the alphabet does not
# spell as one ordinary letter is written in brackets, by name where that
# name belongs to one code alone and by number where two codes share it.
"""


def bytes_as_c(data, per_line=16):
    lines = []
    for i in range(0, len(data), per_line):
        lines.append('    ' + ','.join(str(b) for b in data[i:i + per_line]))
    return ',\n'.join(lines)


def collated(keys):
    return list(keys) == sorted(keys)


def frozen(segments):
    """The pieces of what an action says, in a shape that can be compared."""
    return tuple((tuple(codes), joiner) for codes, joiner in segments)


def say(segments, kind, alpha):
    return lex.render(segments, kind, alpha)


def unsay(text, kind, alpha):
    return lex.parse(text, kind, alpha)


def read_tables():
    """Everything both halves need: the dictionaries, the alphabets, and what
    each action lays down."""
    alpha, act_table, store, dicts = lex.dictionaries()
    laid, kind = {}, {}
    for d in dicts:
        recs = lex.actions(d['name'])
        laid[d['name']] = recs
        kind[d['name']] = lex.choose(list(recs.values()), alpha)
    return alpha, act_table, store, dicts, laid, kind


def dump():
    alpha, act_table, store, dicts, laid, kind = read_tables()

    lines = [HEADER]
    loose = []
    for d in dicts:
        name = d['name']
        table = alpha.get(d['stmt'], [])
        once = lex.unique_names(table)
        recs, k = laid[name], kind[name]

        lines.append('dictionary %s statement %d width %d records %s'
                     % (name, d['stmt'], d['width'],
                        'sound' if k == SOUND_STMT else 'characters'))
        for _off, key, value in d['entries']:
            act = struct.unpack_from('<H', value, 2)[0]
            text = say(recs[act], k, alpha) if act in recs else ''
            lines.append('  %s %d %d %d%s'
                         % (lex.word(key, table, once), value[0], value[1],
                            act, (' says ' + text) if text else ''))
        lines.append('')

        if not collated([k2 for _o, k2, _v in d['entries']]):
            loose.append(name)

    os.makedirs(os.path.dirname(DICT_FILE), exist_ok=True)
    with open(DICT_FILE, 'w') as f:
        f.write('\n'.join(lines))

    print('%d dictionaries, %d entries, %d with a pronunciation, written to %s'
          % (len(dicts), sum(d['count'] for d in dicts),
             sum(1 for d in dicts for _o, _k, v in d['entries']
                 if struct.unpack_from('<H', v, 2)[0] in laid[d['name']]),
             os.path.relpath(DICT_FILE, ROOT)))
    if loose:
        print('not in the engine\'s own order: %s' % ', '.join(loose))
    return 0


def read_file():
    dicts = []
    for line in open(DICT_FILE):
        line = line.rstrip('\n')
        if not line.strip() or line.startswith('#'):
            continue
        if line.startswith('dictionary '):
            f = line.split()
            dicts.append({'name': f[1], 'stmt': int(f[3]), 'width': int(f[5]),
                          'kind': SOUND_STMT if f[7] == 'sound' else CHAR_STMT,
                          'entries': []})
            continue
        f = line[2:].split(' ')
        word, left, right = f[0], int(f[1]), int(f[2])
        # A word written with `new' where its action number goes is one being
        # added, and building gives it an action of its own.
        action = None if f[3] == 'new' else int(f[3])
        text = ' '.join(f[5:]) if len(f) > 4 and f[4] == 'says' else None
        dicts[-1]['entries'].append((word, left, right, action, text))
    return dicts


def lay_down(want, alpha):
    """The store, and where each dictionary starts in it."""
    out = bytearray()
    starts = []

    for d in want:
        letters = alpha.get(d['stmt'], [])
        once = lex.unique_names(letters)
        entries = [(lex.codes_of(w, letters, once), l, r, a)
                   for w, l, r, a, _t in d['entries']]

        base = len(out)
        starts.append(base)
        out.extend(b'\0' * (2 * len(entries)))

        for slot, (key, left, right, action) in enumerate(entries):
            where = len(out) - base - 2 * len(entries)
            # The index holds an offset in two signed bytes, so a dictionary
            # grown past that would wrap and be searched in the wrong place.
            if where > 0x7fff:
                raise ValueError('%s has outgrown its index at entry %d'
                                 % (d['name'], slot))
            struct.pack_into('<h', out, base + 2 * slot, where)
            out.extend(key)
            out.append(0)
            out.extend((left, right))
            out.extend(struct.pack('<H', action))

    return out, starts


def lay(arms, act, segments):
    """Give one action what it is to say, in however many pieces."""
    datas = [bytes(codes) for codes, _j in segments]
    if len(datas) == 1:
        arms.rewrite(act, datas[0])
    elif segments[1][1] == lex.THEN:
        arms.rewrite_parts(act, datas)
    else:
        # Both readings are chosen in code the other words using that test run
        # through, in every one of the 191 actions that has two, so there is
        # nothing here that could be changed for one word alone.
        raise ValueError('a word with two readings is shown as it is but '
                         'cannot be changed: which of the two is taken is '
                         'decided in code other words run through')


def mint(arms, used, codes, why):
    """An action of this word's own: a spare arm of the rule if one is going,
    and otherwise an arm added to it."""
    if len(codes) > 1:
        raise ValueError('nor can it be given an action of its own, %s '
                         'saying things in more than one piece' % why)
    codes = codes[0][0]
    act = arms.spare(used)
    if act is not None:
        try:
            arms.rewrite(act, bytes(codes))
            used.add(act)
            return act
        except ValueError:
            # It looked spare but cannot be written; do not offer it again.
            used.add(act)
    act = arms.add_arm(bytes(codes))
    used.add(act)
    return act


def work_out(d, alpha, laid):
    """What has to happen to one dictionary's pronunciations.

    An action belongs to every word that dispatches on it, so the first word
    to state one keeps the action and the others, if they want something else,
    are given actions of their own rather than being refused. That is a change
    in what the dictionary means and it is reported, not done quietly.
    """
    groups = {}
    for i, (_w, _l, _r, act, _t) in enumerate(d['entries']):
        if act is not None:
            groups.setdefault(act, []).append(i)

    change, split = [], []
    for act, idxs in groups.items():
        now = laid.get(act)
        now = frozen(now) if now is not None else None

        texts = []
        for i in idxs:
            text = d['entries'][i][4]
            if text is None:
                continue
            try:
                codes = frozen(unsay(text, d['kind'], alpha))
            except ValueError as why:
                raise ValueError('%s, %s: %s'
                                 % (d['name'], d['entries'][i][0], why))
            for seen, where in texts:
                if seen == codes:
                    where.append(i)
                    break
            else:
                texts.append((codes, [i]))

        if not texts:
            continue
        # Whichever wants what the action already says keeps it, so that a
        # word nobody asked to change is not moved off it.
        for n, (codes, _where) in enumerate(texts):
            if codes == now:
                texts.insert(0, texts.pop(n))
                break
        first, rest = texts[0], texts[1:]
        if first[0] != now:
            change.append((act, first[0], first[1]))
        for codes, where in rest:
            split.append((act, codes, where))
    return change, split


def build():
    alpha, act_table, store, was, laid, kind = read_tables()
    want = read_file()

    if [d['name'] for d in was] != [d['name'] for d in want]:
        print('the file names different dictionaries than the tables do',
              file=sys.stderr)
        return 1

    bad = 0
    for d in want:
        letters = alpha.get(d['stmt'], [])
        once = lex.unique_names(letters)
        try:
            keys = [lex.codes_of(w, letters, once) for w, _l, _r, _a, _t in
                    d['entries']]
        except ValueError as why:
            print('%s: %s' % (d['name'], why), file=sys.stderr)
            bad = 1
            continue
        if not collated(keys):
            print('%s is not in the order the engine searches, and words in '
                  'it would not be found' % d['name'], file=sys.stderr)
            bad = 1
    if bad:
        return 1

    rules = lex.rules()
    added = changed = split_off = 0
    touched = False

    for d in want:
        fresh = [i for i, e in enumerate(d['entries']) if e[3] is None]
        try:
            change, split = work_out(d, alpha, laid[d['name']])
        except ValueError as why:
            print(why, file=sys.stderr)
            bad = 1
            continue
        if not fresh and not change and not split:
            continue

        if any(d['entries'][i][4] is None for i in fresh):
            print('%s: a new word has to say something' % d['name'],
                  file=sys.stderr)
            bad = 1
            continue

        arms = arms_mod.Arms(rules, d['name'])
        if not arms.ok:
            print('%s lays its records down a way this cannot write, so the '
                  '%d change(s) asked of it were refused'
                  % (d['name'], len(fresh) + len(change) + len(split)),
                  file=sys.stderr)
            bad = 1
            continue

        used = set(e[3] for e in d['entries'] if e[3] is not None)
        touched = True

        def point(where, act):
            for i in where:
                e = d['entries'][i]
                d['entries'][i] = (e[0], e[1], e[2], act, e[4])

        try:
            # What one word wants and another on the same action does not.
            for act, codes, where in split:
                new = mint(arms, used, codes, d['name'])
                point(where, new)
                split_off += 1
                print('%s: %s now has an action of its own, %d, because it '
                      'says something other than the words it shared %d with'
                      % (d['name'], d['entries'][where[0]][0], new, act))

            # What the action itself is to say.
            for act, codes, where in change:
                try:
                    lay(arms, act, codes)
                    changed += 1
                except ValueError as why:
                    try:
                        new = mint(arms, used, codes, d['name'])
                    except ValueError as also:
                        raise ValueError('%s, and %s' % (why, also))
                    point(where, new)
                    split_off += 1
                    print('%s: %s now has an action of its own, %d, because '
                          'what it said could not be changed where it stood -- '
                          '%s' % (d['name'], d['entries'][where[0]][0], new,
                                  why))

            # Words being added.
            for i in fresh:
                codes = unsay(d['entries'][i][4], d['kind'], alpha)
                new = mint(arms, used, codes, d['name'])
                point([i], new)
                added += 1
        except ValueError as why:
            print(why, file=sys.stderr)
            bad = 1
            continue

    if bad:
        return 1

    if touched:
        rename(want)
    if rules.touched:
        rules.save()

    out, starts = lay_down(want, alpha)
    table = bytearray(act_table)
    for i, d in enumerate(want):
        struct.pack_into('<i', table, i * ENTRY_BYTES + 0x0c, len(d['entries']))

    text = open(SETS_C).read()
    text = splice(text, 'act_table[]', bytes_as_c(table))
    text = splice(text, 'actent_store[]', bytes_as_c(out))
    text = splice(text, 'actent_all[]',
                  '\n'.join('    actent_store + %d,   /* %s_actentries */'
                            % (s, d['name'])
                            for s, d in zip(starts, want)))
    open(SETS_C, 'w').write(text)

    print('%d dictionaries, %d entries, %d bytes of store, %d rewritten, '
          '%d added, %d split off'
          % (len(want), sum(len(d['entries']) for d in want), len(out),
             changed, added, split_off))
    return compare()


def compare():
    """Read it all back and hold it against what the file asked for. The store
    is laid out afresh, so the bytes are not the same bytes; what has to be the
    same is every dictionary's words, values and pronunciations."""
    alpha, _table, _store, now, laid, _kind = read_tables()
    want = read_file()

    bad = 0
    for asked, got in zip(want, now):
        letters = alpha.get(asked['stmt'], [])
        once = lex.unique_names(letters)
        mine = [(tuple(lex.codes_of(w, letters, once)), l, r, a)
                for w, l, r, a, _t in asked['entries']]
        theirs = [(tuple(k), v[0], v[1], struct.unpack_from('<H', v, 2)[0])
                  for _o, k, v in got['entries']]
        if mine != theirs:
            bad += 1
            print('%s reads back differently' % asked['name'],
                  file=sys.stderr)
            for i, (x, y) in enumerate(zip(mine, theirs)):
                if x != y:
                    print('  first at entry %d' % i, file=sys.stderr)
                    break

        for _w, _l, _r, act, text in asked['entries']:
            if text is None or act is None:
                continue
            said = laid[asked['name']].get(act)
            if said is None:
                bad += 1
                print('%s action %d lays nothing down, though it was told to '
                      'say something' % (asked['name'], act), file=sys.stderr)
            elif said != unsay(text, asked['kind'], alpha):
                bad += 1
                print('%s action %d says something other than it was told to'
                      % (asked['name'], act), file=sys.stderr)

    if bad:
        return 1
    print('reads back word for word, value for value and sound for sound')
    return 0


def rename(want):
    """Write the action numbers back into the file. A word given one, whether
    because it was being added or because it was split off from an action it
    shared, keeps it rather than being given another next time."""
    out, which, entry = [], -1, 0
    for line in open(DICT_FILE):
        if line.startswith('dictionary '):
            which += 1
            entry = 0
        elif line.startswith('  ') and which >= 0:
            f = line[2:].rstrip('\n').split(' ')
            act = want[which]['entries'][entry][3]
            if f[3] != str(act):
                f[3] = str(act)
                line = '  ' + ' '.join(f) + '\n'
            entry += 1
        out.append(line)
    open(DICT_FILE, 'w').writelines(out)


def splice(text, name, body):
    at = text.index(name)
    open_at = text.index('{', at)
    close_at = text.index('\n};', open_at)
    return text[:open_at + 1] + '\n' + body + text[close_at:]


def where():
    """Which two words a new one belongs between. The order is by the
    language's own character codes, so the vowels come first and no ordinary
    sense of alphabetical says where a word goes."""
    name, want = sys.argv[2], sys.argv[3]
    alpha, _t, _s, dicts = lex.dictionaries()
    for d in dicts:
        if d['name'] != name:
            continue
        table = alpha.get(d['stmt'], [])
        once = lex.unique_names(table)
        codes = lex.codes_of(want, table, once)
        keys = [k for _o, k, _v in d['entries']]
        at = 0
        for i, k in enumerate(keys):
            if k < codes:
                at = i + 1
        print('%s goes after %s and before %s'
              % (want,
                 lex.word(keys[at - 1], table, once) if at else '(the first)',
                 lex.word(keys[at], table, once) if at < len(keys)
                 else '(the last)'))
        return 0
    print('no dictionary called %s' % name, file=sys.stderr)
    return 1


def find():
    """Every dictionary holding a word, and what each says it sounds like. A
    word can be in several, and what any one of them does with it depends on
    where the engine is when it looks."""
    want = sys.argv[2]
    alpha, _t, _s, dicts, laid, kind = read_tables()
    found = 0
    for d in dicts:
        table = alpha.get(d['stmt'], [])
        once = lex.unique_names(table)
        for _off, key, value in d['entries']:
            if lex.word(key, table, once) != want:
                continue
            act = struct.unpack_from('<H', value, 2)[0]
            said = laid[d['name']].get(act)
            print('%-28s action %-4d %s'
                  % (d['name'], act,
                     say(said, kind[d['name']], alpha) if said
                     else '(nothing this can read)'))
            found += 1
    if not found:
        print('no dictionary holds %s' % want)
    return 0


if __name__ == '__main__':
    mode = sys.argv[1] if len(sys.argv) > 1 else 'dump'
    how = {'dump': dump, 'build': build, 'where': where, 'find': find}
    if mode not in how:
        print('usage: delta-dict.py [dump | build | where <dictionary> <word> '
              '| find <word>]', file=sys.stderr)
        sys.exit(2)
    sys.exit(how[mode]())
