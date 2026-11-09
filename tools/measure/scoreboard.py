#!/usr/bin/env python3
"""How close US English's rules are to needing no carried dictionary.

The rules and the module's own tables are what read a word nobody wrote an
entry for, a name out of a book or a word coined last year, and the carried
dictionary is where they fail. So the dictionary is the scoreboard: an entry
the rules already say is one they no longer need, and the work on the rules
is measured by how many of those there are.

Two numbers, and a third when an earlier run is given:

  root entries the rules say
      Each entry of root.dic is read in the forms running text has it in --
      the word, inside a sentence, starting one, its plural and its
      possessive -- once with the volume in and once with the root volume
      empty, and is counted where every form reads the same; starting a
      sentence it is compared without the accent, which any dictionary's
      answer loses there, IBM's engine's as well. Then the counted entries are taken out together
      and read again, since a word's plural or a compound of it can be
      another entry's word, and any that moved are not counted; until
      nothing moves.

  the rules against CMU
      A sample of CMU's dictionary, the same twenty thousand words every
      run, read with no dictionary at all but the module's own, and how many
      CMU agrees with on sounds and on stress as well. Any text files given
      with --text are read the same way, word by word, weighted by how often
      each word occurs in them.

  against an earlier run
      Which entries are newly said by the rules and which no longer are,
      and which sample words moved closer to CMU or further from it, each
      a file in the output directory.

Everything is read through the library named, which has to be built from the
tree as it stands, with the carried dictionary as volumes a caller loads and
eciBuiltInDict off, as tools/measure/published.py reads it: a caller cannot
take an entry out of the dictionary a language carries, and this has to.
Forms that only the carried dictionary reads -- a capital inside a word, a
word the cut divides -- are left out for that reason.

usage: tools/measure/scoreboard.py <libeci.so> <out-dir> [--against <dir>]
           [--text FILE]... [--jobs N]
"""

import collections
import multiprocessing
import os
import random
import re
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from evv import ROOT, sibling  # noqa: E402

referee = sibling('measure/referee')
published = sibling('measure/published')
built = sibling('module/builtindict')

LANG = os.path.join(ROOT, 'lang', 'enus')
WORD = re.compile(r'^[a-z]{2,}$')
SAMPLE = 20000


def forms(w):
    """The word as running text has it: alone, inside a sentence, starting
    one, with the plural and the possessive the root volume gives it, and,
    where it has no ending already, with -ing and -ed, the final e dropped
    as English drops it. A verb's endings on a noun are words nobody
    writes, and cost only an entry kept that could have gone."""
    cap = w[0].upper() + w[1:]
    plural = w + ('es' if re.search(r'(s|x|z|sh|ch)$', w) else 's')
    out = [w, 'Say %s now.' % w, '%s is here.' % cap, plural, w + "'s"]
    if not re.search(r'(s|ed|ing|er|ly|y)$', w):
        stem = w[:-1] if w.endswith('e') else w
        out += [stem + 'ing', stem + 'ed']
    return out


# The accent the rules give a capitalised first word is lost when a
# dictionary answers it, IBM's engine as well (docs/quirks.md), so a word
# starting a sentence is compared on its sounds and stresses alone.
ACCENT = re.compile(r'`\d+ ?')


def same(x, y):
    return x == y or ACCENT.sub('', x) == ACCENT.sub('', y)


def write_volume(path, entries):
    with open(path, 'wb') as f:
        for word, says in entries:
            f.write(word + b'\t' + says + b'\r\n')
        # The loader drops a file's last line.
        f.write(b'\r\n')


def volumes(tmp, root_without=None):
    """The carried dictionary as a caller's files, the root volume without
    the words given, or empty if None is given for the root."""
    vols = built.volumes(LANG)
    files = []
    for n, vol in ((0, 'main'), (1, 'root'), (2, 'abbr')):
        entries = vols[vol]
        if vol == 'root':
            if root_without is None:
                entries = []
            else:
                entries = [(w, s) for w, s in entries if w not in root_without]
        path = os.path.join(tmp, '%s-%d.dic' % (vol, len(entries)))
        write_volume(path, entries)
        files.append((n, path))
    return files


def read(lib, texts, files, jobs):
    return published.speak_all(lib, sorted(set(texts)), 0, files, jobs)


def said_by_rules(lib, tmp, jobs):
    """The root entries every form of which reads the same without them."""
    root = [w.decode('cp1252') for w, _s in built.volumes(LANG)['root']]
    words = sorted({w for w in root if WORD.match(w)})
    full = volumes(tmp, root_without=set())
    empty = volumes(tmp)
    a = read(lib, words, full, jobs)
    b = read(lib, words, empty, jobs)
    cand = {w for w in words if same(a[w], b[w])}
    texts = [t for w in cand for t in forms(w)]
    a = read(lib, texts, full, jobs)
    b = read(lib, texts, empty, jobs)
    cand = {w for w in cand if all(same(a[t], b[t]) for t in forms(w))}
    rounds = 0
    while True:
        rounds += 1
        less = volumes(tmp, root_without={w.encode('cp1252') for w in cand})
        texts = [t for w in cand for t in forms(w)]
        c = read(lib, texts, less, jobs)
        moved = {w for w in cand if any(not same(a[t], c[t]) for t in forms(w))}
        if not moved:
            break
        cand -= moved
    return words, cand, rounds


def against_cmu(lib, jobs, texts):
    cmu = referee.lexicon()
    alpha = sorted(w for w in cmu if re.fullmatch(r'[a-z]{2,}', w))
    sample = random.Random(1).sample(alpha, SAMPLE)
    counts = collections.Counter()
    for path in texts:
        for w in re.findall(r"[A-Za-z]+", open(path, encoding='utf-8', errors='replace').read()):
            if w.lower() in cmu:
                counts[w.lower()] += 1
    words = sorted(set(sample) | set(counts))
    said = read(lib, words, [], jobs)
    fit = {}
    for w in words:
        sounds, stress = referee.fits(referee.ours(said[w]), cmu[w])
        fit[w] = (sounds, sounds and stress)
    return sample, counts, said, fit


def pct(n, d):
    return 100.0 * n / d if d else 0.0


def main(argv):
    jobs = 40
    texts, against = [], None
    args = []
    i = 0
    while i < len(argv):
        if argv[i] == '--jobs':
            jobs = int(argv[i + 1])
            i += 2
        elif argv[i] == '--text':
            texts.append(argv[i + 1])
            i += 2
        elif argv[i] == '--against':
            against = argv[i + 1]
            i += 2
        else:
            args.append(argv[i])
            i += 1
    if len(args) != 2:
        sys.exit(__doc__)
    lib, out = os.path.abspath(args[0]), args[1]
    os.makedirs(out, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        words, said, rounds = said_by_rules(lib, tmp, jobs)
    sample, counts, reading, fit = against_cmu(lib, jobs, texts)

    with open(os.path.join(out, 'said-by-rules'), 'w', encoding='utf-8') as f:
        f.write(''.join(w + '\n' for w in sorted(said)))
    with open(os.path.join(out, 'rules'), 'w', encoding='utf-8') as f:
        for w in sorted(reading):
            f.write('%s\t%d\t%d\t%s\n' % (w, fit[w][0], fit[w][1], reading[w].strip()))

    print('scoreboard: %d of %d root entries are said by the rules in every form'
          ' (%d rounds)' % (len(said), len(words), rounds))
    s = sum(fit[w][0] for w in sample)
    st = sum(fit[w][1] for w in sample)
    print('scoreboard: the rules alone, %d CMU words: sounds %.1f%%, sounds and'
          ' stress %.1f%%' % (len(sample), pct(s, len(sample)), pct(st, len(sample))))
    if counts:
        tot = sum(counts.values())
        s = sum(n for w, n in counts.items() if fit[w][0])
        st = sum(n for w, n in counts.items() if fit[w][1])
        print('scoreboard: the rules alone, %d words of the texts CMU holds: sounds'
              ' %.1f%%, sounds and stress %.1f%%' % (tot, pct(s, tot), pct(st, tot)))

    if against:
        was = set(open(os.path.join(against, 'said-by-rules'), encoding='utf-8').read().split())
        before = {}
        for line in open(os.path.join(against, 'rules'), encoding='utf-8'):
            w, a, b, r = line.rstrip('\n').split('\t')
            before[w] = (int(a), int(b), r)
        gained, lost = sorted(said - was), sorted(was - said)
        better = sorted(w for w in fit if w in before and fit[w][1] > before[w][1])
        worse = sorted(w for w in fit if w in before and fit[w][1] < before[w][1])
        moved = sorted(w for w in fit if w in before and reading[w].strip() != before[w][2])
        for name, ws in (('gained', gained), ('lost', lost), ('better', better),
                         ('worse', worse), ('moved', moved)):
            with open(os.path.join(out, name), 'w', encoding='utf-8') as f:
                f.write(''.join(w + '\n' for w in ws))
        print('scoreboard: against %s: %d entries newly said by the rules, %d no'
              ' longer; %d words read otherwise, %d closer to CMU, %d further'
              % (against, len(gained), len(lost), len(moved), len(better), len(worse)))
    return 0


if __name__ == '__main__':
    multiprocessing.set_start_method('forkserver', force=True)
    sys.exit(main(sys.argv[1:]))
