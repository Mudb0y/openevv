#!/usr/bin/env python3
"""Turn OpenRussian's dictionary into tools/russian/openrussian.tsv.gz.

OpenRussian (https://en.openrussian.org) publishes its dictionary at
https://github.com/Badestrand/russian-dictionary under CC BY-SA 4.0: every
noun, adjective and verb it has, with each inflected form written with an
apostrophe after the stressed vowel. What this keeps is each form and which
of its vowels is stressed, which is all eci_russian.c wants to know, and
what it writes is therefore CC BY-SA 4.0 as well -- tools/russian/NOTICE
says so.

The rows come in order of how common the word is, so where two words share a
spelling and differ in stress -- замок and замок, стоит and стоит -- the
commoner one is kept. A form of one syllable is left out, since it has
nowhere else to put the stress.

usage: openrussian.py DIR    where DIR holds nouns.csv, adjectives.csv,
                             verbs.csv and others.csv
"""

import csv
import gzip
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TARGET = os.path.join(HERE, "openrussian.tsv.gz")

VOWELS = "аеёиоуыэюя"
LETTERS = "абвгдеёжзийклмнопрстуфхцчшщъыьэюя"

#: Columns that are not forms of the word.
NOT_FORMS = {
    "bare", "accented", "translations_en", "translations_de", "gender",
    "partner", "animate", "indeclinable", "sg_only", "pl_only", "aspect",
}


def forms(path):
    rows = csv.reader(open(path, encoding="utf-8"), delimiter="\t")
    head = next(rows)
    for row in rows:
        cells = [row[1]] if len(row) > 1 else []
        cells += [c for h, c in zip(head, row) if h not in NOT_FORMS]
        for cell in cells:
            for one in cell.replace(";", ",").split(","):
                one = one.strip().lower()
                if one and " " not in one and "-" not in one:
                    yield one


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    where = sys.argv[1]
    stress = {}
    for name in ("others", "nouns", "adjectives", "verbs"):
        for form in forms(os.path.join(where, name + ".csv")):
            bare = form.replace("'", "")
            if not bare or any(c not in LETTERS for c in bare):
                continue
            if sum(c in VOWELS for c in bare) < 2:
                continue
            if "'" in form:
                at = form.index("'")
                vowel = sum(c in VOWELS for c in form[:at]) - 1
            elif "ё" in bare:
                vowel = sum(c in VOWELS for c in bare[: bare.index("ё")])
            else:
                continue
            if vowel < 0:
                continue
            stress.setdefault(bare, vowel)
    with gzip.open(TARGET, "wt", encoding="utf-8", newline="\n") as out:
        for word in sorted(stress):
            out.write("%s\t%d\n" % (word, stress[word]))
    print("openrussian: %d forms into %s" % (len(stress), os.path.relpath(TARGET, HERE)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
