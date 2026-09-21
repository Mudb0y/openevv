#!/usr/bin/env python3
"""The word lists test/words.sh holds the engine to.

The sentence gate has about a hundred cases a language in it. That is the
right size for asking whether the engine still says what it said, and far too
small for asking whether a rule change was a good idea: a change that mends
forty words and breaks four hundred passes it without a murmur. So there is a
second gate a level down, one word to a line, and this writes the list it
reads.

Where the words come from is the language's own spelling dictionary, which is
the only source that is both broad and honest about what the language really
spells. English takes SCOWL's `wamerican.35' and `wbritish.35', which are the
common half of the language before the long tail of proper nouns; the others
take a hunspell dictionary, whose `.dic' is one stem to a line with its affix
flags after a slash. A stride through the list sorted gives an even spread
across the alphabet rather than twenty thousand words beginning with A.

Words are lowercased and kept at three to twelve letters, letters of that
language's own alphabet only: an apostrophe or a capital is a different
question, and a word the engine cannot even read is not a test of its rules.

**And the list is written in the code set the engine reads, which is the
Windows Western set and not UTF-8.** `docs/api.md` says so -- the engine takes
single bytes in the language's own set, and does almost nothing between the
caller's bytes and the machine's characters. A list in UTF-8 hands it the two
bytes of an accented letter as two characters, and Spanish hangs outright on
one such pair: `cc', the two bytes of a UTF-8 `\u00f3', and `n'. That is worth a
line in the crashers corpus of its own, but it is not what a word list is for.

And then every word the language's own dictionaries hold, where the module
has them as text, which the sample mostly does not: 4,317 of English's 5,042
were missing on 6 September 2026. Those are exactly the words a change to a
dictionary can move, so a gate that cannot see them cannot answer the question
people will most often be asking it. Leaving them out cost a false pass the
same day -- `slugabed' was edited, the gate said every one as it was, and it
was saying nothing at all.

The list is written into the tree rather than read from the store at run time,
because a baseline is worth nothing if the thing it was recorded against can
move underneath it.

    nix shell nixpkgs#scowl
    tools/measure/wordlist.py enus > test/cases/words-enus.txt

    nix shell nixpkgs#hunspellDicts.it_IT
    tools/measure/wordlist.py itit > test/cases/words-itit.txt

usage: tools/measure/wordlist.py <tag> [count]
"""

import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
WANT = 20000

# Where each language's words come from, and what the source is called when it
# is not in the store. The letters are the language's own: a list filtered to
# ASCII would drop every German word with an umlaut in it, which is a fifth of
# the interesting ones.
SOURCES = {
    'enus': ('/nix/store/*-scowl-*/share/dict/wamerican.35',
             'nixpkgs#scowl', 'a-z'),
    'engb': ('/nix/store/*-scowl-*/share/dict/wbritish.35',
             'nixpkgs#scowl', 'a-z'),
    'dede': ('/nix/store/*-hunspell-dict-de-de-*/share/hunspell/de_DE.dic',
             'nixpkgs#hunspellDicts.de_DE', 'a-zäöüß'),
    'eses': ('/nix/store/*-hunspell-dict-es-es-*/share/hunspell/es_ES.dic',
             'nixpkgs#hunspellDicts.es_ES', 'a-záéíóúüñ'),
    # The two Spanishes spell the same and sound different, which is the
    # whole reason they are two modules, so one source serves both.
    'esus': ('/nix/store/*-hunspell-dict-es-es-*/share/hunspell/es_ES.dic',
             'nixpkgs#hunspellDicts.es_ES', 'a-záéíóúüñ'),
    'frfr': ('/nix/store/*-hunspell-dict-fr-*/share/hunspell/fr*.dic',
             'nixpkgs#hunspellDicts.fr-any', 'a-zàâçéèêëîïôùûüÿœæ'),
    # Dicollecte's list is every variant of French together, which is what
    # both French modules want: they differ in how a word is said, not in
    # which words there are.
    'frca': ('/nix/store/*-hunspell-dict-fr-*/share/hunspell/fr*.dic',
             'nixpkgs#hunspellDicts.fr-any', 'a-zàâçéèêëîïôùûüÿœæ'),
    'itit': ('/nix/store/*-hunspell-dict-it-it-*/share/hunspell/it_IT.dic',
             'nixpkgs#hunspellDicts.it_IT', 'a-zàèéìòù'),
}


def source_words(tag):
    """Every word of the language's spelling dictionary worth testing."""
    where, how, letters = SOURCES[tag]
    found = sorted(glob.glob(where))
    if not found:
        print("wordlist: no source for %s in the store; nix shell %s"
              % (tag, how), file=sys.stderr)
        return None, None
    keep = re.compile(r'[%s]{3,12}\Z' % letters)
    words = set()
    for line in open(found[0], encoding='utf-8', errors='replace'):
        # A hunspell .dic is a count, then one stem a line with its affix
        # flags after a slash; SCOWL is one word a line and has neither.
        w = line.split('/')[0].strip().lower()
        if keep.match(w):
            words.add(w)
    return sorted(words), os.path.basename(found[0])


def held(tag):
    """Every word the module's own dictionaries hold, where it has them as
    text. Only English does today, and the rest answer nothing rather than
    pretending to."""
    out = set()
    p = os.path.join(ROOT, 'lang', tag, '%s.dict' % tag)
    if not os.path.exists(p):
        return out
    for line in open(p, encoding='utf-8', errors='replace'):
        m = re.match(r'^  (\S+) ', line)
        if m and len(m.group(1)) <= 20:
            out.add(m.group(1))
    return out


def main(argv):
    if not argv or argv[0] not in SOURCES:
        print(__doc__.strip())
        print("\nwordlist: the languages with a source are %s"
              % ", ".join(sorted(SOURCES)), file=sys.stderr)
        return 2
    tag = argv[0]
    want = int(argv[1]) if len(argv) > 1 else WANT

    words, source = source_words(tag)
    if not words:
        return 2

    # Evenly spaced by fraction rather than by whole steps: asking for twenty
    # thousand of thirty-eight thousand with an integer stride gives a stride
    # of one, which is the first twenty thousand and stops at M.
    if want >= len(words):
        picked = words
    else:
        picked = [words[(i * len(words)) // want] for i in range(want)]

    sys.stdout.reconfigure(encoding="cp1252", errors="replace")
    print("# %d words of %s, spread evenly through the %d in %s."
          % (len(picked), tag, len(words), source))
    carried = held(tag)
    both = sorted(set(picked) | carried)
    if carried:
        print("# With every word the dictionaries hold folded in: %d of them,"
              % len(carried))
        print("# which are the words a change to a dictionary can move.")
    print("# Written by tools/measure/wordlist.py. See test/words.sh.")
    # In the code set the engine reads rather than in UTF-8, and a word with a
    # character that set has no room for is dropped rather than mangled.
    out = sys.stdout.buffer
    dropped = 0
    for w in both:
        try:
            out.write(w.encode("cp1252") + b"\n")
        except UnicodeEncodeError:
            dropped += 1
    if dropped:
        print("wordlist: %d words dropped, the Windows Western set having no"
              " room for them" % dropped, file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
