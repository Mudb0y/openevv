#!/usr/bin/env python3
"""Which of two readings of an English word the CMU Pronouncing Dictionary
supports.

US English is being made out of two engines: IBM's 4.3, which is ours, and
ETI's 6.1, which Apple ships. They read about one word in ten differently, and
most of those differences are not a matter of taste -- one of the two is
simply wrong -- so they want settling by something that is neither engine.
The CMU dictionary is that: 134,000 words of American English, written down by
people rather than worked out by rules.

It settles sounds and not stress. On the words both engines already read the
same way, CMU's sounds agree with theirs about ninety-eight times in a hundred,
which is what makes a disagreement mean something; its stress disagrees with
both about one word in eight, mostly through its own inconsistency, which is
what makes a stress disagreement mean very little. So a verdict is given on
sounds alone:

  ours       our sounds are one of CMU's readings and 6.1's are not
  6.1        the other way about
  stress     both sounds are CMU's and only the stress differs
  same       both sounds are CMU's and so is the stress; what differs is
             where a syllable is divided, which changes no sound
  neither    neither engine's sounds are any of CMU's
  unheld     CMU does not have the word

Both engines and CMU are brought down to one coarse alphabet before they are
compared, so that the three ways of writing American English agree where the
sounds do: a flap or a glottal stop is a t; a syllabic n is a reduced vowel
and an n; a reduced vowel and an r are the one r-coloured vowel CMU writes; a
vowel before r is the one CMU writes there; an n before k or g is the ng it
is said as; and the vowels of cot and caught are one, as they are for much of
the country, except before r. A reduced vowel then stands for any vowel the
other side leaves weak -- unstressed, or the secondary stress CMU gives a
prefix an engine reduces -- since a vowel written full on one side and
reduced on the other is a difference of care rather than of sound. A vowel
with the main stress has to match exactly, and so does every consonant.

usage: tools/measure/referee.py compare <words.txt> <report.tsv>
           [--module enu.dylib] [--common wordlist] [--theirs answers]
       tools/measure/referee.py score <baseline.words> [<earlier.words>]

`compare' speaks the list through build/phonemes and through 6.1, and writes
every word the two read differently with its verdict, which of each engine's
dictionaries holds it, and whether it is in the common list if one is given.
6.1's dictionaries are read from the module named, which has to be the Mach-O
Apple ships (tools/module/lexicon64.py says why). 6.1 itself is found the way
test/harness/eti.sh finds it, and has to be run one process at a time, which
for a list the size of CMU's is most of ten minutes; --theirs names a file its
answers are kept in, read when it exists and written when it does not, so that
trying a change to ours costs only ours.

`score' says how many words of a recorded baseline CMU agrees with on sounds,
and how many it holds. Given an earlier baseline as well, it counts only the
words both have, and says how many of them CMU agreed with then: a list that
lost or gained words would otherwise move the number without anything having
been said differently. That is what test/words.sh holds when it records.
"""

import collections
import os
import re
import resource
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from evv import ROOT

try:
    import cmudict
except ImportError:
    cmudict = None


# Our phone letters, the longer first, and what CMU calls each. The unstressed
# vowels are reduced afterwards, where the stress is known.
PHONES = [('Xx', 'IH'), ('c@', 'AO'), ('a@', 'AA'), ('Aa', 'AE'), ('H@', 'AH'),
          ('i', 'IY'), ('I', 'IH'), ('e', 'EY'), ('E', 'EH'), ('A', 'AE'),
          ('a', 'AA'), ('c', 'AO'), ('o', 'OW'), ('U', 'UH'), ('u', 'UW'),
          ('R', 'ER'), ('x', 'AH'), ('X', 'IH'), ('H', 'AH'), ('Y', 'AY'),
          ('W', 'AW'), ('O', 'OY'), ('@', 'AE'),
          ('b', 'B'), ('p', 'P'), ('d', 'D'), ('t', 'T'), ('F', 'T'),
          ('?', 'T'), ('N', 'AH N'), ('k', 'K'), ('g', 'G'), ('D', 'DH'), ('T', 'TH'),
          ('v', 'V'), ('f', 'F'), ('z', 'Z'), ('s', 'S'), ('Z', 'ZH'),
          ('S', 'SH'), ('J', 'JH'), ('C', 'CH'), ('h', 'HH'), ('m', 'M'),
          ('n', 'N'), ('G', 'NG'), ('r', 'R'), ('l', 'L'), ('y', 'Y'),
          ('w', 'W'), ('L', 'L')]
VOWELS = {'IY', 'IH', 'EY', 'EH', 'AE', 'AA', 'AO', 'OW', 'UH', 'UW', 'ER',
          'AH', 'AY', 'AW', 'OY'}
# The vowels CMU itself writes for a reduced one when it is unstressed.
REDUCED = {'AH', 'IH', 'UH'}
# Before r the three of them write the vowel differently and mean one sound.
BEFORE_R = {'EY': 'EH', 'OW': 'AO', 'IY': 'IH', 'UW': 'UH'}


def ours(text):
    """Our phonemes as (phone, stress) pairs, the stress on vowels only."""
    text = re.sub(r'[`\[\] ]', '', text)
    out, stress, i = [], None, 0
    while i < len(text):
        ch = text[i]
        if ch == '.':
            if i + 1 < len(text) and text[i + 1].isdigit():
                stress = int(text[i + 1])
                i += 2
            else:
                i += 1
            continue
        if ch.isdigit():
            i += 1
            continue
        for k, v in PHONES:
            if text.startswith(k, i):
                for one in v.split():
                    # A syllabic consonant's vowel is never the stressed one.
                    st = (0 if len(v.split()) > 1 else stress)
                    out.append((one, st if one in VOWELS else None))
                i += len(k)
                break
        else:
            out.append(('?' + ch, None))
            i += 1
    return out


def cmu(pron):
    out = []
    for ph in pron:
        m = re.match(r'([A-Z]+)(\d)?', ph)
        out.append((m.group(1), int(m.group(2)) if m.group(2) else None))
    return out


def sounds(pairs):
    """The coarse sounds, each with whether it is a weak vowel."""
    out = []
    for ph, st in pairs:
        if ph in REDUCED and st == 0:
            ph = '@'
        if ph == 'D':
            ph = 'T'
        out.append([ph, ph in VOWELS | {'@'} and st in (0, 2)])
    merged = []
    for i, (ph, weak) in enumerate(out):
        nxt = out[i + 1][0] if i + 1 < len(out) else None
        if ph in ('@', 'AH') and nxt == 'R':
            merged.append(['ER', weak])
            out[i + 1][0] = None
        elif ph is None:
            continue
        elif ph == 'NG' and nxt in ('K', 'G'):
            merged.append(['N', weak])
        else:
            merged.append([ph, weak])
    for i in range(len(merged)):
        before_r = i + 1 < len(merged) and merged[i + 1][0] == 'R'
        if before_r and merged[i][0] in BEFORE_R:
            merged[i][0] = BEFORE_R[merged[i][0]]
        elif not before_r and merged[i][0] == 'AO':
            merged[i][0] = 'AA'
    return merged


def same_sounds(engine, cmu_pairs):
    a, b = sounds(engine), sounds(cmu_pairs)
    if len(a) != len(b):
        return False
    for x, y in zip(a, b):
        if x[0] == y[0] or {x[0], y[0]} == {'@', 'AH'}:
            continue
        if '@' in (x[0], y[0]) and (x[1] if y[0] == '@' else y[1]):
            continue
        return False
    return True


def main_stress(pairs):
    marks = [st for ph, st in pairs if ph in VOWELS]
    return marks.index(1) if 1 in marks else -1


def fits(pairs, readings):
    """(sounds, stress): whether these are CMU's sounds for one of its
    readings, and whether the main stress falls where that reading has it."""
    best = (False, False)
    for r in readings:
        c = cmu(r)
        if same_sounds(pairs, c):
            best = max(best, (True, main_stress(pairs) == main_stress(c)))
    return best


def verdict(a, b, readings):
    if not readings:
        return 'unheld'
    fa, fb = fits(ours(a), readings), fits(ours(b), readings)
    if fa[0] and not fb[0]:
        return 'ours'
    if fb[0] and not fa[0]:
        return '6.1'
    if not fa[0]:
        return 'neither'
    if main_stress(ours(a)) != main_stress(ours(b)):
        return 'stress'
    return 'same'


def lexicon():
    if cmudict is None:
        raise SystemExit('referee: no CMU dictionary here; the dev shell has '
                         'it, or pip install cmudict')
    return cmudict.dict()


# ---- the two engines ----------------------------------------------------

def eti():
    """Where 6.1 is, and the libraries it wants, as test/harness/eti.sh finds
    them."""
    where, libs = os.environ.get('EVV_ETI'), os.environ.get('EVV_ETI_LIBS')
    if not where or not libs:
        module = os.path.realpath(
            '/run/current-system/sw/lib/speech-dispatcher-modules/sd_eloquence')
        if os.path.exists(module):
            where = where or os.path.join(
                os.path.dirname(os.path.dirname(module)), 'eloquence')
            found = re.findall(r"LD_LIBRARY_PATH='([^']*)'",
                               open(module, errors='replace').read())
            libs = libs or ':'.join(found)
    if not where or not os.path.exists(os.path.join(where, 'eci.so')):
        raise SystemExit('referee: no ETI Eloquence 6.1 here; EVV_ETI names '
                         'its directory')
    return where, libs


def pieces(words, size):
    return [words[i:i + size] for i in range(0, len(words), size)]


# What an engine is put down as having said for a word it died on, so that one
# word does not cost the rest of the list.
DIED = '!'


def no_cores():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def speak(words, work, name, line, command, size, at_once, **how):
    """Every word through one engine, a piece of the list a process and so
    many processes at once. A process that stops short, or is still going
    after five minutes, died on the word after its last answer; that word is
    put down as DIED and the rest of its piece is started again."""
    def start(n, part):
        src = os.path.join(work, '%s.%06d' % (name, n))
        open(src, 'w', encoding='latin-1').write(''.join(line(w) for w in part))
        return subprocess.Popen(command(src), stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL,
                                preexec_fn=no_cores, **how)

    def finish(n, part, p):
        got = []
        while True:
            try:
                said = p.communicate(timeout=300)[0]
            except subprocess.TimeoutExpired:
                p.kill()
                said = p.communicate()[0]
            said = said.decode('latin-1').splitlines()[:len(part) - len(got)]
            got.extend(said)
            if len(got) == len(part):
                return got
            got.append(DIED)
            if len(got) == len(part):
                return got
            p = start(n * 100000 + len(got), part[len(got):])

    out = []
    batch = pieces(words, size)
    for first in range(0, len(batch), at_once):
        group = list(enumerate(batch[first:first + at_once], first))
        procs = [(n, part, start(n, part)) for n, part in group]
        for n, part, p in procs:
            out.extend(finish(n, part, p))
    return out


def speak_ours(words, work, jobs):
    native = os.environ.get('EVV_WORDS_NATIVE',
                            os.path.join(ROOT, 'build', 'phonemes'))
    return speak(words, work, 'ours', lambda w: w + '\n',
                 lambda src: [native, src],
                 max(1, (len(words) + jobs - 1) // jobs), jobs)


def speak_theirs(words, work):
    where, libs = eti()
    driver = os.path.join(ROOT, 'build', 'eti')
    harness = os.path.join(ROOT, 'test', 'harness', 'eti.c')
    if (not os.path.exists(driver)
            or os.path.getmtime(harness) > os.path.getmtime(driver)):
        subprocess.run([os.environ.get('CC', 'cc'), '-O1', '-o', driver,
                        harness, '-ldl'], check=True)
    # One at a time. Apple's engine converted to run here shares something
    # between processes: four of them at once corrupt each other's heaps and
    # leave one another's mutexes held, where one alone reads every word.
    said = speak(words, work, 'theirs', lambda w: '0x10000\t-1\t%s\n' % w,
                 lambda src: [driver, os.path.join(where, 'eci.so'), src],
                 2000, 1,
                 # 6.1 reads its eci.ini from where it is run.
                 cwd=where, env=dict(os.environ, LD_LIBRARY_PATH=libs))
    return [s.split('\t')[0] for s in said]


# ---- what each engine knows by heart --------------------------------------

def our_dictionaries():
    """Word to the first of our dictionaries holding it."""
    held, section = {}, None
    for line in open(os.path.join(ROOT, 'lang', 'enus', 'enus.dict'),
                     encoding='latin-1'):
        if line.startswith('dictionary '):
            section = line.split()[1]
        elif line.startswith('  ') and section:
            held.setdefault(line.split()[0].lower(), section)
    return held


def their_dictionaries(module):
    if not module:
        return {}
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'module'))
    import lexicon64
    held = {}
    for name, words in lexicon64.dictionaries(module):
        for w, _act in words:
            held.setdefault(w.lower(), name)
    return held


def compare(args):
    src, report = args[0], args[1]
    module = common = keep = None
    rest = args[2:]
    while rest:
        flag, value, rest = rest[0], rest[1], rest[2:]
        if flag == '--module':
            module = value
        elif flag == '--common':
            common = value
        elif flag == '--theirs':
            keep = value
        else:
            raise SystemExit('referee: what is %s?' % flag)

    words = [w.strip() for w in open(src, encoding='latin-1')
             if w.strip() and not w.startswith('#')]
    jobs = int(os.environ.get('EVV_WORDS_JOBS', os.cpu_count() or 4))
    with tempfile.TemporaryDirectory() as work:
        mine = speak_ours(words, work, jobs)
        if keep and os.path.exists(keep):
            theirs = open(keep, encoding='latin-1').read().splitlines()
            if len(theirs) != len(words):
                raise SystemExit('referee: %s holds %d answers for %d words'
                                 % (keep, len(theirs), len(words)))
        else:
            theirs = speak_theirs(words, work)
            if keep:
                open(keep, 'w', encoding='latin-1').write(
                    '\n'.join(theirs) + '\n')

    cmu_words = lexicon()
    ibm, e61 = our_dictionaries(), their_dictionaries(module)
    usual = set()
    if common:
        usual = {w.strip().lower() for w in open(common, encoding='latin-1')}

    tally = collections.Counter()
    with open(report, 'w', encoding='latin-1') as out:
        out.write('# word\tverdict\tcommon\tours holds\t6.1 holds\tours\t6.1'
                  '\tCMU\n')
        for w, a, b in zip(words, mine, theirs):
            a = re.sub(r'\s+', '', a.strip('[]'))
            b = re.sub(r'\s+', '', b.strip('[]'))
            if a == b:
                tally['agree'] += 1
                continue
            readings = cmu_words.get(w.lower(), [])
            if DIED in (a, b):
                v = 'ours died' if a == DIED else '6.1 died'
            else:
                v = verdict(a, b, readings)
            tally[v] += 1
            out.write('\t'.join([
                w, v, 'common' if w.lower() in usual else '-',
                ibm.get(w.lower(), '-'), e61.get(w.lower(), '-'),
                re.sub(r'[`\[\]]', '', a), re.sub(r'[`\[\]]', '', b),
                ' | '.join(' '.join(r) for r in readings) or '-']) + '\n')
    print('referee: %d words, %d read alike' % (len(words), tally['agree']))
    for v in ('ours', '6.1', 'stress', 'same', 'neither', 'unheld',
              'ours died', '6.1 died'):
        print('referee: %6d %s' % (tally[v], v))


def score(args):
    cmu_words = lexicon()

    def answers(path):
        out = {}
        for line in open(path, encoding='latin-1'):
            if line.startswith('#') or '\t' not in line:
                continue
            w, said = line.rstrip('\n').split('\t', 1)
            out[w] = said
        return out

    now = answers(args[0])
    then = answers(args[1]) if len(args) > 1 else None
    held = agree = agreed = 0
    for w, said in now.items():
        readings = cmu_words.get(w.lower())
        if not readings or (then is not None and w not in then):
            continue
        held += 1
        agree += fits(ours(said), readings)[0]
        if then is not None:
            agreed += fits(ours(then[w]), readings)[0]
    print(('%d %d %d' % (agree, held, agreed)) if then is not None
          else ('%d %d' % (agree, held)))


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ('compare', 'score'):
        raise SystemExit('usage: ' + __doc__.split('usage: ')[1]
                         .split('\n\n')[0])
    {'compare': compare, 'score': score}[sys.argv[1]](sys.argv[2:])


if __name__ == '__main__':
    main()
