#!/usr/bin/env python3
"""Entries for US English's dictionary written from published pronunciations
that ask nothing of whoever copies them.

Every word Wiktionary has an American pronunciation for -- or failing that a
British one -- is spoken through the engine with its dictionary on and off.
A word is clearly wrong when its reading differs from every form Wiktionary
or CMU lists for it by two sounds or more, or at the vowel either side
stresses; a difference of one unstressed sound is left alone, being as often
a variant nobody listed as a mistake, and so is a main stress alone, since
on that Wiktionary and CMU disagree with Merriam-Webster as often as the
engine does. For each clearly wrong word:

  remove   the reading is an entry's and the rules' own reading is one
           Wiktionary lists, so the entry is taken out
  moby     the Moby Pronunciator II has a form Wiktionary lists, and an entry
           is written from it
  cmu      the same from CMU's dictionary of 1993

Wiktionary only judges here and nothing of it is copied: its text is CC
BY-SA, and these files are to carry no terms. The Moby Pronunciator was put
in the public domain by Grady Ward in 2001, and CMU said of its 1993 release
that use for any purpose is completely unrestricted. The current CMU, under
terms of its own, judges alongside Wiktionary and is never copied either.

It judges only the body of lang/enus/dictionary's volumes, the sorted
entries the tools write, and leaves alone every word of a block decided by
hand and every word lang/enus/dictionary/removed names. What it finds goes to
the report, a line each: entry, the volume, the word, what to say and which
source; or remove, the volume, the word, what the entry said and why. With
--write it also puts the entries into the body and moves the removals from
the body to removed, which git then shows as one change to listen to.

An entry is a root, as most of the dictionary's are, so that a plural comes with
it, which a whole-word entry does not give. But a root is also
the stem of longer words, and a part of a compound -- jete made Jeter
zhuh-TAY-er, frey made Godfrey god-FRY -- so every entry is read back
through the engine as a caller's dictionary over every word Wiktionary or
CMU has, and one is dropped if the engine does not say it as written or if
any word containing it moves further from what is published. An entry of
the body is taken out only where no word containing it leans on it, and is
read back the same way, the whole dictionary loaded as a caller's with the
built-in one off, since nothing a caller loads can take an entry out.

The library has to be built from the tree as it stands, which the read-back
checks before anything else. A run takes the whole machine for some minutes,
so give it an idle-class scope.

usage: tools/measure/published.py <libeci.so> <kaikki.jsonl.gz> <mpron.txt>
           <cmudict.0.3> <report.tsv> [--write] [--jobs N]

  kaikki.jsonl.gz  https://kaikki.org/dictionary/English/
                   kaikki.org-dictionary-English.jsonl.gz
  mpron.txt        https://www.gutenberg.org/files/3205/files/mpron.txt
  cmudict.0.3      https://www.gutenberg.org/files/3205/files/cmudict.txt
"""

import collections
import ctypes
import gzip
import json
import multiprocessing
import os
import re
import sys
import tempfile
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from evv import ROOT, sibling  # noqa: E402

referee = sibling('measure/referee')

LANG = os.path.join(ROOT, 'lang', 'enus')
WHY = "Wiktionary lists the rules' reading and not the entry's"
VOWELS = referee.VOWELS
WORD = re.compile(r"^[a-z][a-z']*[a-z]$")

# ---- the notations -------------------------------------------------------

# IPA, longest first. A syllabic consonant is a reduced vowel and the
# consonant.
IPA = [
    ('t͡ʃ', 'CH'), ('tʃ', 'CH'), ('d͡ʒ', 'JH'), ('dʒ', 'JH'), ('t͡s', 'T S'),
    ('d͡z', 'D Z'), ('ts', 'T S'),
    ('n̩', '@ N'), ('l̩', '@ L'), ('m̩', '@ M'), ('ɹ̩', 'ER'), ('r̩', 'ER'),
    ('ɑ˞', 'AA R'), ('ɔ˞', 'AO R'), ('ɛ˞', 'EH R'), ('ə˞', 'ER'), ('ɜ˞', 'ER'),
    ('eɪ', 'EY'), ('ɛɪ', 'EY'), ('aɪ', 'AY'), ('ɑɪ', 'AY'), ('ʌɪ', 'AY'),
    ('ɐɪ', 'AY'), ('aʊ', 'AW'), ('ɑʊ', 'AW'), ('æʊ', 'AW'),
    ('ɔɪ', 'OY'), ('oɪ', 'OY'), ('oʊ', 'OW'), ('əʊ', 'OW'), ('ɒʊ', 'OW'),
    ('ɔʊ', 'OW'), ('ɜʊ', 'OW'), ('eə', 'EH'), ('ɛə', 'EH'), ('ɪə', 'IH'),
    ('ʊə', 'UH'), ('ɪɚ', 'IH R'), ('ɛɚ', 'EH R'), ('ʊɚ', 'UH R'),
    ('ɝɹ', 'ER'), ('ɜɹ', 'ER'), ('ɝr', 'ER'), ('ɜr', 'ER'),
    ('ɝ', 'ER'), ('ɜ', 'ER'), ('ɚ', 'ER'),
    ('i', 'IY'), ('ɪ', 'IH'), ('e', 'EY'), ('ɛ', 'EH'), ('æ', 'AE'),
    ('a', 'AA'), ('ɑ', 'AA'), ('ɒ', 'AA'), ('ɔ', 'AO'),
    ('o', 'OW'), ('ʊ', 'UH'), ('u', 'UW'), ('ʌ', 'AH'), ('ə', 'AH'),
    ('ɐ', 'AH'), ('ᵻ', 'IH'), ('ɨ', 'IH'), ('ʉ', 'UW'), ('y', 'UW'),
    ('ʏ', 'UH'), ('ø', 'ER'), ('œ', 'ER'), ('ɵ', 'OW'), ('ɘ', 'AH'),
    ('ɤ', 'AH'), ('ɯ', 'UW'),
    ('p', 'P'), ('b', 'B'), ('t', 'T'), ('d', 'D'), ('k', 'K'), ('ɡ', 'G'),
    ('g', 'G'), ('f', 'F'), ('v', 'V'), ('θ', 'TH'), ('ð', 'DH'), ('s', 'S'),
    ('z', 'Z'), ('ʃ', 'SH'), ('ʒ', 'ZH'), ('m', 'M'), ('n', 'N'), ('ŋ', 'NG'),
    ('l', 'L'), ('ɫ', 'L'), ('ɹ', 'R'), ('r', 'R'), ('ɾ', 'T'), ('ɻ', 'R'),
    ('ʁ', 'R'), ('ʀ', 'R'), ('j', 'Y'), ('w', 'W'), ('h', 'HH'), ('ɦ', 'HH'),
    ('ʍ', 'W'), ('x', 'K'), ('χ', 'HH'), ('ç', 'HH'), ('ʔ', 'T'),
    ('ɲ', 'N Y'), ('ʎ', 'L Y'), ('ʝ', 'Y'), ('β', 'V'), ('ɣ', 'G'),
    ('ɱ', 'M'), ('ɕ', 'SH'), ('ʑ', 'ZH'), ('ʂ', 'SH'), ('ʐ', 'ZH'),
    ('ɟ', 'JH'), ('c', 'K'), ('ɬ', 'L'), ('ʋ', 'V'), ('ɰ', 'W'),
]


def _put(out, ph, pending):
    """Append one phone; a vowel takes the stress waiting for it."""
    if ph == '@':
        out.append(('AH', 0))
    elif ph in VOWELS:
        out.append((ph, pending))
        return 0
    else:
        out.append((ph, None))
    return pending


def _one_vowel_stressed(out):
    vs = [n for n, (ph, _st) in enumerate(out) if ph in VOWELS]
    if len(vs) == 1 and not out[vs[0]][1]:
        out[vs[0]] = (out[vs[0]][0], 1)
    return out


def ipa(text, uk=False):
    """One IPA transcription as (phone, stress) pairs."""
    text = unicodedata.normalize('NFD', text)
    # An optional sound is left out, except the r a British transcription
    # brackets, which American English says.
    text = re.sub(r'\((?![ɹr]\))[^)]*\)', '', text)
    text = re.sub(r'[/\[\]()]', '', text)
    # English says a nasal vowel as the vowel and an n: Poussin, poo-SAN.
    text = re.sub('̃(?![nmŋ])', 'n', text).replace('̃', '')
    text = text.replace('ː', '').replace('ˑ', '')
    out, pending, i, marked = [], 0, 0, False
    while i < len(text):
        ch = text[i]
        if ch in 'ˈ\'':
            pending, marked, i = 1, True, i + 1
            continue
        if ch == 'ˌ':
            pending, marked, i = 2, True, i + 1
            continue
        for k, v in IPA:
            if text.startswith(k, i):
                for ph in v.split():
                    if ph == 'EY' and k == 'e' and uk:
                        ph = 'EH'
                    pending = _put(out, ph, pending)
                i += len(k)
                break
        else:
            i += 1
    return out if marked else _one_vowel_stressed(out)


# Moby: a vowel, and a few consonants, between slashes; the rest bare. A
# slash doubled in the source leaves a vowel bare now and then, so vowels are
# read bare too.
MOBY_SLASHED = {
    '&': 'AE', '(@)': 'EH', 'A': 'AA', 'eI': 'EY', '@': 'AH', '-': '@', 'E': 'EH',
    'i': 'IY', 'I': 'IH', 'aI': 'AY', 'Oi': 'OY', 'AU': 'AW', 'O': 'AO', 'oU': 'OW',
    'u': 'UW', 'U': 'UH', '@r': 'ER', '[@]': 'ER', 'y': 'UW', 'Y': 'UW', 'x': 'K',
    'j': 'Y', 'hw': 'W', 'tS': 'CH', 'dZ': 'JH', 'S': 'SH', 'Z': 'ZH', 'T': 'TH',
    'D': 'DH', 'N': 'NG', 'R': 'R', 'ju': 'Y UW', 'Ou': 'OW', 'z': 'Z', '(@)f': 'EH F'}
MOBY_BARE = [
    ('[@]', 'ER'), ('(@)', 'EH'), ('@r', 'ER'), ('Oi', 'OY'), ('oU', 'OW'), ('eI', 'EY'),
    ('aI', 'AY'), ('AU', 'AW'), ('ou', 'AW'), ('@', 'AH'), ('&', 'AE'), ('-', '@'),
    ('E', 'EH'), ('I', 'IH'), ('O', 'AO'), ('U', 'UH'), ('i', 'IY'), ('u', 'UW'),
    ('o', 'OW'), ('a', 'AE'), ('e', 'EH'), ('c', 'K'), ('W', 'V'), ('V', 'V'),
    ('tS', 'CH'), ('dZ', 'JH'), ('hw', 'W'), ('S', 'SH'), ('Z', 'ZH'), ('T', 'TH'),
    ('D', 'DH'), ('N', 'NG'), ('j', 'Y'), ('x', 'K'), ('R', 'R'), ('A', 'AA'),
    ('Y', 'UW'), ('b', 'B'), ('d', 'D'), ('f', 'F'), ('g', 'G'), ('h', 'HH'),
    ('k', 'K'), ('l', 'L'), ('m', 'M'), ('n', 'N'), ('p', 'P'), ('r', 'R'),
    ('s', 'S'), ('t', 'T'), ('v', 'V'), ('w', 'W'), ('z', 'Z')]


def moby(text):
    """One Moby transcription as pairs, or None for a symbol not known."""
    out, pending, i = [], 0, 0
    while i < len(text):
        ch = text[i]
        if ch in "',":
            pending = 1 if ch == "'" else 2
            i += 1
            continue
        if ch in '_ ':
            i += 1
            continue
        if ch == '/':
            j = text.find('/', i + 1)
            if j < 0:
                return None
            sym, i = text[i + 1:j], j + 1
            if not sym:
                continue
            if sym not in MOBY_SLASHED:
                return None
            phones = MOBY_SLASHED[sym]
        else:
            for sym, phones in MOBY_BARE:
                if text.startswith(sym, i):
                    i += len(sym)
                    break
            else:
                return None
        for ph in phones.split():
            pending = _put(out, ph, pending)
        # The r after the vowel of burn is that vowel's own.
        if sym == '[@]' and text.startswith('r', i):
            i += 1
    return _one_vowel_stressed(out)


def cmu(phones):
    return referee.cmu(phones)


def engine(text):
    return referee.ours(text)


# ---- how far apart two readings are ------------------------------------

REDUCED = {'AH', 'IH', 'UH'}
BEFORE_R = {'EY': 'EH', 'AE': 'EH', 'OW': 'AO', 'IY': 'IH', 'UW': 'UH'}


def coarse(pairs):
    """The mergers of American English: a flap is a t, a reduced vowel and r
    are one r-coloured vowel, n before k or g is the ng it is said as, vowels
    before r fall together as in Mary, marry and merry, and cot and caught are
    one."""
    seq = [[ph, st] for ph, st in pairs]
    out, i = [], 0
    while i < len(seq):
        ph, st = seq[i]
        nxt = seq[i + 1][0] if i + 1 < len(seq) else None
        if ph == 'D':
            ph = 'T'
        # Only a schwa and r are the vowel of fur; the vowel of fear is not.
        if ph == 'AH' and st == 0 and nxt == 'R':
            out.append(['ER', 0])
            i += 2
            continue
        if ph == 'NG' and nxt in ('K', 'G'):
            ph = 'N'
        out.append([ph, st])
        i += 1
    for k in range(len(out)):
        before_r = k + 1 < len(out) and out[k + 1][0] == 'R'
        if before_r and out[k][0] in BEFORE_R:
            out[k][0] = BEFORE_R[out[k][0]]
        elif not before_r and out[k][0] == 'AO':
            out[k][0] = 'AA'
    return out


def _same(x, y):
    if x[0] == y[0]:
        return True
    # A vowel left unstressed may be said reduced; one with a secondary
    # stress may not: the bolt of thunderbolt is said in full.
    if x[0] in VOWELS and y[0] in VOWELS and x[1] in (0, None) and y[1] in (0, None):
        return x[0] in REDUCED or y[0] in REDUCED
    return False


def _table(sa, sb):
    n, m = len(sa), len(sb)
    d = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(n + 1):
        d[i][0] = i
    for j in range(m + 1):
        d[0][j] = j
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            sub = 0 if _same(sa[i - 1], sb[j - 1]) else 1
            d[i][j] = min(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + sub)
    return d


def distance(a, b):
    """Sounds that differ, and a half for the main stress elsewhere."""
    sa, sb = coarse(a), coarse(b)
    d = _table(sa, sb)
    ma, mb = referee.main_stress(a), referee.main_stress(b)
    return d[len(sa)][len(sb)] + (0.5 if mb >= 0 and ma != mb else 0)


def stressed_miss(a, b):
    """Whether the best alignment differs at a vowel either side stresses."""
    sa, sb = coarse(a), coarse(b)
    d = _table(sa, sb)
    i, j = len(sa), len(sb)
    while i > 0 and j > 0:
        sub = 0 if _same(sa[i - 1], sb[j - 1]) else 1
        if d[i][j] == d[i - 1][j - 1] + sub:
            if sub and 1 in (sa[i - 1][1], sb[j - 1][1]):
                return True
            i, j = i - 1, j - 1
        elif d[i][j] == d[i - 1][j] + 1:
            if sa[i - 1][1] == 1:
                return True
            i -= 1
        else:
            if sb[j - 1][1] == 1:
                return True
            j -= 1
    return any(x[1] == 1 for x in sa[:i]) or any(x[1] == 1 for x in sb[:j])


def clearly_wrong(reading, forms):
    """Two sounds or more from every form, or a miss at a stressed vowel of
    every one."""
    if not forms:
        return False
    if min(distance(reading, f) for f in forms) >= 2:
        return True
    return all(distance(reading, f) >= 1 and stressed_miss(reading, f) for f in forms)


def listed(reading, forms):
    return any(distance(reading, f) <= 0.5 for f in forms)


# ---- the engine's phonetic spelling ------------------------------------

SPELL = {'IY': 'i', 'IH': 'I', 'EY': 'e', 'EH': 'E', 'AE': 'A', 'AA': 'a', 'AO': 'c',
         'OW': 'o', 'UH': 'U', 'UW': 'u', 'ER': 'R', 'AH': 'H', 'AY': 'Y', 'AW': 'W',
         'OY': 'O', 'P': 'p', 'B': 'b', 'T': 't', 'D': 'd', 'K': 'k', 'G': 'g',
         'F': 'f', 'V': 'v', 'TH': 'T', 'DH': 'D', 'S': 's', 'Z': 'z', 'SH': 'S',
         'ZH': 'Z', 'CH': 'C', 'JH': 'J', 'M': 'm', 'N': 'n', 'NG': 'G', 'L': 'l',
         'R': 'r', 'Y': 'y', 'W': 'w', 'HH': 'h'}
ONSETS = {('P', 'L'), ('P', 'R'), ('B', 'L'), ('B', 'R'), ('K', 'L'), ('K', 'R'),
          ('G', 'L'), ('G', 'R'), ('F', 'L'), ('F', 'R'), ('T', 'R'), ('D', 'R'),
          ('TH', 'R'), ('SH', 'R'), ('S', 'P'), ('S', 'T'), ('S', 'K'), ('S', 'M'),
          ('S', 'N'), ('S', 'L'), ('S', 'W'), ('S', 'F'), ('T', 'W'), ('D', 'W'),
          ('K', 'W'), ('G', 'W'), ('TH', 'W'), ('HH', 'W'), ('S', 'P', 'R'),
          ('S', 'T', 'R'), ('S', 'K', 'R'), ('S', 'P', 'L'), ('S', 'K', 'W'),
          ('P', 'Y'), ('B', 'Y'), ('K', 'Y'), ('G', 'Y'), ('F', 'Y'), ('V', 'Y'),
          ('M', 'Y'), ('HH', 'Y'), ('S', 'K', 'Y'), ('S', 'P', 'Y')}


def _onset(cs):
    return not cs or (cs[0] != 'NG' if len(cs) == 1 else tuple(cs) in ONSETS)


def spell(pairs):
    """Pairs as one phonetic word, syllables divided by the longest onset
    English allows, the last of two main stresses the word's."""
    pairs = list(pairs)
    mains = [i for i, (_p, st) in enumerate(pairs) if st == 1]
    for i in mains[:-1]:
        pairs[i] = (pairs[i][0], 2)
    nuclei = [i for i, (ph, _st) in enumerate(pairs) if ph in VOWELS]
    if not nuclei or not mains:
        return None
    starts = [0]
    for a, b in zip(nuclei, nuclei[1:]):
        between = [pairs[i][0] for i in range(a + 1, b)]
        k = len(between)
        while k > 0 and not _onset(between[len(between) - k:]):
            k -= 1
        starts.append(b - k)
    out = ''
    for s, (lo, hi) in enumerate(zip(starts, starts[1:] + [len(pairs)])):
        out += '.%d' % (pairs[nuclei[s]][1] or 0)
        for ph, st in pairs[lo:hi]:
            if ph == 'AH' and not st:
                out += 'x'
            elif ph == 'IH' and not st:
                out += 'X'
            else:
                out += SPELL[ph]
    return '`[' + out + ']'


# ---- the sources ---------------------------------------------------------

BAD = {'obsolete', 'nonstandard', 'proscribed', 'dated', 'rare', 'archaic', 'humorous',
       'eye-dialect', 'dialectal', 'error', 'misspelling', 'hyperforeign', 'Hyperforeign'}
US = {'General-American', 'US', 'Canada', 'cot-caught-merger', 'GA', 'GenAm',
      'North-America'}
UK = {'Received-Pronunciation', 'UK', 'British', 'Standard', 'Southern'}


def wiktionary(path):
    """Each word's forms, American and untagged first, British only where
    there is nothing else."""
    us, uk = collections.defaultdict(list), collections.defaultdict(list)
    with gzip.open(path, 'rt', encoding='utf-8') as f:
        for line in f:
            e = json.loads(line)
            key = e.get('word', '').lower()
            if not WORD.match(key):
                continue
            for s in e.get('sounds', ()):
                one = s.get('ipa')
                tags = set(s.get('tags', [])) | set(s.get('raw_tags', []))
                if not one or tags & BAD:
                    continue
                for v in one.replace('~', ',').split(','):
                    v = v.strip()
                    bare = v.strip('/[]')
                    # A fragment such as /-t͡ʃɒv/ only varies the one before.
                    if not bare or bare.startswith('-') or bare.endswith('-'):
                        continue
                    if tags & US or not tags - {'stressed', 'unstressed'}:
                        us[key].append(v)
                    elif tags & UK:
                        uk[key].append(v)
    out = {}
    for key in set(us) | set(uk):
        forms = []
        for v in us.get(key) or uk[key]:
            for w in readings(v):
                forms.append(ipa(w, uk=not us.get(key)))
        out[key] = forms
    return out


def readings(v):
    """A transcription and the variants its notation leaves open: an optional
    sound said, and a bare a read as the vowel of cat, which is what a
    transcription in the British style means by it, as well as of father."""
    out = [v]
    if '(' in v:
        out.append(re.sub(r'[()]', '', v))
    if re.search('a(?![ɪʊ])', v) and 'æ' not in v:
        out += [re.sub('a(?![ɪʊ])', 'æ', w) for w in list(out)]
    return out


def moby_lexicon(path):
    out = collections.defaultdict(list)
    with open(path, 'rb') as f:
        for raw in f:
            line = raw.decode('latin-1').rstrip('\r\n')
            if ' ' not in line:
                continue
            word, pron = line.split(' ', 1)
            pairs = moby(pron.strip())
            if pairs is not None:
                out[word.split('/')[0].replace('_', ' ').lower()].append(pairs)
    return out


def cmu_lexicon(path):
    out = collections.defaultdict(list)
    with open(path, 'rb') as f:
        for raw in f:
            line = raw.decode('latin-1').rstrip('\r\n')
            if not line or line.startswith('#'):
                continue
            word, _, phones = line.partition('  ')
            out[re.sub(r'\(\d+\)$', '', word).lower()].append(cmu(phones.split()))
    return out


# ---- speaking ------------------------------------------------------------

def _instance(lib, files):
    v = ctypes.c_void_p
    lib.eciNewEx.restype = v
    lib.eciNewEx.argtypes = [ctypes.c_int]
    lib.eciNewDict.restype = v
    lib.eciNewDict.argtypes = [v]
    lib.eciLoadDict.argtypes = [v, v, ctypes.c_int, ctypes.c_char_p]
    lib.eciSetDict.argtypes = [v, v]
    lib.eciSetParam.argtypes = [v, ctypes.c_int, ctypes.c_int]
    lib.eciAddText.argtypes = [v, ctypes.c_char_p]
    lib.eciGeneratePhonemes.argtypes = [v, ctypes.c_int, ctypes.c_char_p]
    lib.eciDelete.argtypes = [v]
    h = lib.eciNewEx(0x10000)
    if files:
        d = lib.eciNewDict(h)
        for vol, path in files:
            lib.eciLoadDict(h, d, vol, path.encode())
        lib.eciSetDict(h, d)
    return h


def speak(job):
    """Each word as the engine reads it: built-in dictionary on or off, and
    with any dictionary files given loaded as the caller's."""
    lib_path, words, builtin, files = job
    lib = ctypes.CDLL(lib_path)
    callback = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int,
                                ctypes.c_long, ctypes.c_void_p)
    lib.eciRegisterCallback.argtypes = [ctypes.c_void_p, callback, ctypes.c_void_p]
    said = []
    buf = ctypes.create_string_buffer(8192)

    def on_message(h, msg, param, data):
        # Phonemes come a buffer at a time in message 1.
        if msg == 1 and (param & 0xffffffff) > 0:
            said.append(buf.raw[:param & 0xffffffff])
        return 1

    cb = callback(on_message)
    h = _instance(lib, files)
    lib.eciRegisterCallback(h, cb, None)
    # Phonemes come only with the synthesis mode that waits to be told.
    lib.eciSetParam(h, 0, 1)
    lib.eciSetParam(h, 7, 1)
    lib.eciSetParam(h, 34, builtin)
    out = {}
    for w in words:
        del said[:]
        lib.eciAddText(h, w)
        lib.eciGeneratePhonemes(h, len(buf), buf)
        out[w] = b''.join(said).decode('cp1252')
    lib.eciDelete(h)
    return out


def speak_all(lib, words, builtin, files, jobs):
    enc = [w.encode('cp1252') for w in words]
    with multiprocessing.Pool(jobs) as pool:
        parts = pool.map(speak, [(lib, enc[i::jobs], builtin, files) for i in range(jobs)])
    said = {}
    for p in parts:
        said.update(p)
    return {w: said[e] for w, e in zip(words, enc)}


def judge(job):
    """Which words of a share are clearly wrong as read."""
    readings, refs = job
    return {w for w, r in readings.items() if clearly_wrong(engine(r), refs[w])}


def _further(job):
    pairs, refs = job
    out = set()
    for w, (before, after) in pairs.items():
        if not refs[w]:
            continue
        a, b = engine(before), engine(after)
        if min(distance(b, f) for f in refs[w]) > min(distance(a, f) for f in refs[w]):
            out.add(w)
    return out


def further(pairs, refs, jobs):
    words = sorted(pairs)
    shares = [words[i::jobs] for i in range(jobs)]
    with multiprocessing.Pool(jobs) as pool:
        parts = pool.map(_further, [({w: pairs[w] for w in s}, {w: refs[w] for w in s})
                                    for s in shares])
    return set().union(*parts)


def wrong_words(readings, refs, jobs):
    words = sorted(readings)
    shares = [words[i::jobs] for i in range(jobs)]
    with multiprocessing.Pool(jobs) as pool:
        parts = pool.map(judge, [({w: readings[w] for w in s}, {w: refs[w] for w in s})
                                 for s in shares])
    return set().union(*parts)


# ---- what is written -----------------------------------------------------

def dictionary_words():
    """The volume each word of the body is read from, and the words decided
    by hand or taken out on purpose, which this leaves alone."""
    built = sibling('module/builtindict')
    where, mine = {}, set()
    for vol in ('root', 'main'):
        path = os.path.join(LANG, 'dictionary', vol + '.dic')
        for word in built.layout(path)[1]:
            where.setdefault(word.lower(), (vol, word))
        mine |= {w.lower() for w in built.hand_words(path)}
    mine |= {w.lower() for _vol, w in built.read_removed(LANG)}
    return where, mine


def as_built(entries, holds, tmp):
    """The dictionary the build would lay down with these entries in and
    these holds taken out, written out as a caller's files. Read with the
    built-in dictionary off it is read as the build would read it, which is
    how a hold is tried, since a caller's dictionary can add an entry to the
    built-in one but cannot take one out of it."""
    built = sibling('module/builtindict')
    held = {(vol, word.encode('cp1252')) for vol, word in holds}
    files = []
    for n, vol in ((0, 'main'), (1, 'root')):
        merged = dict(built.read_dic(os.path.join(LANG, 'dictionary', vol + '.dic')))
        for word in [w for w in merged if (vol, w) in held]:
            del merged[word]
        for word, says, _src, v in entries:
            if v == vol:
                merged[word.encode('cp1252')] = says.encode('cp1252')
        path = os.path.join(tmp, vol + '.dic')
        with open(path, 'wb') as f:
            for word, says in sorted(merged.items()):
                f.write(word + b'\t' + says + b'\r\n')
            # The loader drops a file's last line.
            f.write(b'\r\n')
        files.append((n, path))
    return files


def report(path, entries, holds, write):
    """The report, and with write the body and removed as well."""
    built = sibling('module/builtindict')
    files = {vol: os.path.join(LANG, 'dictionary', vol + '.dic') for vol in ('main', 'root')}
    laid = {vol: built.layout(p) for vol, p in files.items()}
    gone = [(vol, word, laid[vol][1][word], WHY) for vol, word in holds]
    with open(path, 'w', encoding='utf-8') as f:
        for word, says, src, vol in sorted(entries, key=lambda e: (e[3], e[0].lower(), e[0])):
            f.write('entry\t%s\t%s\t%s\t%s\n' % (vol, word, says, src))
        for row in sorted(gone, key=lambda g: (g[0], g[1].lower(), g[1])):
            f.write('remove\t%s\n' % '\t'.join(row))
    if not write:
        return
    for vol, (head, body, tail) in laid.items():
        for word, says, _src, v in entries:
            if v == vol:
                body[word] = says
        for v, word, _says, _why in gone:
            if v == vol:
                del body[word]
        built.write_layout(files[vol], head, body, tail)
    built.add_removed(LANG, gone)


def main(argv):
    jobs = 40
    if '--jobs' in argv:
        at = argv.index('--jobs')
        jobs = int(argv[at + 1])
        argv = argv[:at] + argv[at + 2:]
    write = '--write' in argv
    argv = [a for a in argv if a != '--write']
    if len(argv) != 5:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    lib, kaikki, mpron, cmu93, out = argv
    lib = os.path.abspath(lib)
    if referee.cmudict is None:
        raise SystemExit('published: no CMU dictionary here; the dev shell has it')
    print('published: reading the sources', flush=True)
    wk = wiktionary(kaikki)
    cur = referee.lexicon()
    mob = moby_lexicon(mpron)
    old = cmu_lexicon(cmu93)
    # Every word either has is spoken, so that an entry harming a name only
    # CMU knows is seen; only Wiktionary's words can be given an entry.
    words = sorted(set(wk) | {w for w in cur if WORD.match(w)})
    accepted = {w: wk.get(w, []) + [cmu(p) for p in cur.get(w, ())] for w in words}
    print('published: speaking %d words twice' % len(words), flush=True)
    on = speak_all(lib, words, 1, [], jobs)
    off = speak_all(lib, words, 0, [], jobs)
    wrong = wrong_words(on, accepted, jobs)
    where, mine = dictionary_words()
    by_start = collections.defaultdict(list)
    for u in words:
        for i in range(len(u) - 2):
            by_start[u[i:i + 3]].append(u)

    def leaning(w):
        return by_start.get(w[:3], ())

    entries, holds, why = [], [], collections.Counter()
    for w in sorted(wrong):
        if w in mine or w not in wk:
            continue
        vol, spelt = where.get(w, ('root', w))
        from_dict = w in where
        spelt = spelt if vol == 'main' else w
        if from_dict and listed(engine(off[w]), wk[w]):
            # A root is also the stem of longer words, and taken out it
            # takes their readings with it: havering went wrong when haver
            # was held. Take one out only where no word leans on it.
            if vol == 'root' and any(w in u and u != w and u not in wrong for u in leaning(w)):
                why['kept as a stem'] += 1
            else:
                holds.append((vol, spelt))
                why['remove'] += 1
                continue
        for name, lex in (('moby', mob), ('cmu', old)):
            forms = [p for p in lex.get(w, ()) if any(st == 1 for _ph, st in p)
                     and listed(p, wk[w])]
            if forms:
                says = spell(forms[0])
                if says:
                    entries.append((spelt, says, name, vol))
                    why[name] += 1
                    break
        else:
            why['nothing to write from'] += 1
    print('published: %d clearly wrong; %s' % (
        len(wrong), ', '.join('%s %d' % kv for kv in sorted(why.items()))), flush=True)

    # Read the build back with every entry and hold in it, and drop any
    # entry the engine does not say as written, and any entry or hold that
    # moves a word further from what is published. The build as it stands
    # is read the same way first, as the check that the way is faithful.
    tmp = tempfile.mkdtemp()
    now = speak_all(lib, words, 0, as_built([], [], tmp), jobs)
    unlike = [w for w in words if now[w] != on[w]]
    if unlike:
        raise SystemExit('published: the build read as a caller\'s dictionary says %d '
                         'words otherwise, %s among them' % (len(unlike), ', '.join(unlike[:5])))
    while True:
        now = speak_all(lib, words, 0, as_built(entries, holds, tmp), jobs)
        mine = {e[0].lower(): e for e in entries}
        kept = {word.lower() for _vol, word in holds}
        unsaid = {k for k, e in mine.items() if distance(engine(now[k]), engine(e[1])) > 0}
        # Worse is any word further from what is published than it was,
        # not only one that has become clearly wrong: questar going from
        # one wrong reading to a wronger one counts.
        moved = [w for w in words if w not in mine and now[w] != on[w]]
        worse = further({w: (on[w], now[w]) for w in moved}, accepted, jobs)
        culprits = set()
        for w in worse:
            # The longest entry or hold inside the word, at its start or
            # anywhere a compound puts it, or with its final e gone before
            # an ending, as tace is read in Tacy; a hold can be the word
            # itself.
            def within(k):
                return k in w or (k.endswith('e') and len(k) > 3 and k[:-1] in w)

            inside = [k for k in mine if within(k) and k != w] + [k for k in kept if within(k)]
            if inside:
                culprits.add(max(inside, key=len))
            else:
                print('published: %s is worse and nothing written is inside it' % w,
                      flush=True)
        drop = unsaid | culprits
        print('published: %d entries and %d to take out, %d not said as written, %d made %d '
              'words worse' % (len(entries), len(holds), len(unsaid), len(culprits),
                               len(worse)), flush=True)
        if not drop:
            break
        entries = [e for e in entries if e[0].lower() not in drop]
        holds = [h for h in holds if h[1].lower() not in drop]
    report(out, entries, holds, write)
    print('published: %d entries and %d to take out%s' % (
        len(entries), len(holds), ', written into the dictionary' if write else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
