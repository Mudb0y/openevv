#!/usr/bin/env python3
"""A language's alphabet, and what each character of it is.

The alphabet is the value names of the input statement's first field, and
beside it in the same statement is a record for every one of those names: what
case the character is, whether it is a letter or a digit or punctuation,
whether it is a vowel or a consonant or a glide, whether it carries an accent,
and the phoneme it stands for on its own. Which is letter-to-sound at its
simplest, and it is data rather than code.

Both are in `lang/<tag>/<tag>.statements`, the alphabet as `value' lines and
the records as the `variants' bytes of the same statement -- one record of five
bytes per name, in the order the names are in. Reading those bytes by eye and
writing them by hand is how a letter quietly comes out as a digit, so this
reads and writes them by name.

    lang-alphabet.py show <tag>              every character and what it is
    lang-alphabet.py show <tag> <char>...    only the ones named
    lang-alphabet.py add <tag> <byte> <field>=<value>...

`add' puts a character at a byte value nothing in the alphabet claims yet, so
that no existing code changes meaning: the dictionaries are keyed by these
codes and moving one would move every word that used it. The byte is what the
engine will see for that character once it arrives, in hex.

    lang-alphabet.py add plpl b1 case=lower type=letter letter=vow \\
                              accent='~yes' phoneme=a

usage: as above; `show' with no character lists the lot
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Which statement holds the characters, and the five fields a record carries
# in the order they sit in it.
STATEMENT = "inp"
RECORD = ("case", "type", "letter", "accent", "phoneme")
# What each of those is called in the statement itself.
FIELD = {"case": "letcase", "type": "character_type", "letter": "letter_type",
         "accent": "accent", "phoneme": "phon_form"}


def path_of(tag):
    return os.path.join(ROOT, "lang", tag, "%s.statements" % tag)


def read(tag):
    """The statement's lines, its alphabet, its field values and its records.

    The file is kept as its lines so that writing it back changes only what
    was asked for: everything else, including every other statement, goes
    back exactly as it came.
    """
    lines = open(path_of(tag)).read().split("\n")
    first = last = None
    names = []
    values = {}
    field = None
    variants = bytearray()
    var_at = []

    for i, line in enumerate(lines):
        if line == "statement %s" % STATEMENT:
            first = i
            continue
        if first is not None and last is None:
            if line.startswith("statement ") or line == "end":
                last = i
                continue
            w = line.split()
            if line.startswith("  field "):
                field = w[1]
                values.setdefault(field, [])
            elif line.startswith("    value") and field:
                text = line[len("    value"):]
                text = text[1:] if text.startswith(" ") else text
                text = text.replace("\\s", " ").replace("\\\\", "\\")
                values[field].append(text)
                if field == "name":
                    names.append(text)
            elif w and w[0] == "variants":
                var_at.append(i)
                variants += bytes(int(x, 16) for x in w[1:])
    if first is None:
        raise SystemExit("lang-alphabet: %s has no %s statement"
                         % (tag, STATEMENT))
    return lines, first, last, names, values, bytes(variants), var_at


def named(values, field, v):
    table = values.get(FIELD[field], [])
    return table[v] if 0 <= v < len(table) else str(v)


def number(values, field, text):
    table = values.get(FIELD[field], [])
    if text in table:
        return table.index(text)
    raise SystemExit("lang-alphabet: %s has no %s called %r"
                     % (FIELD[field], field, text))


def show(tag, want):
    _l, _f, _t, names, values, variants, _at = read(tag)
    print("%s: %d characters, %d bytes of records"
          % (tag, len(names), len(variants)))
    for code, ch in enumerate(names):
        if want and ch not in want:
            continue
        r = variants[code * 5:code * 5 + 5]
        if len(r) < 5:
            print("%3d  %-4s no record" % (code, ch))
            continue
        print("%3d  %-4s %-6s %-6s %-5s %-5s says %s"
              % (code, ch if ch.strip() else "' '",
                 named(values, "case", r[0]), named(values, "type", r[1]),
                 named(values, "letter", r[2]), named(values, "accent", r[3]),
                 named(values, "phoneme", r[4])))
    return True


def add(tag, byte, args):
    lines, first, last, names, values, variants, var_at = read(tag)
    want = {}
    for a in args:
        if "=" not in a:
            raise SystemExit("lang-alphabet: %r is not field=value" % a)
        k, v = a.split("=", 1)
        if k not in RECORD:
            raise SystemExit("lang-alphabet: a record has no %r; it has %s"
                             % (k, ", ".join(RECORD)))
        want[k] = v
    for k in RECORD:
        if k not in want:
            raise SystemExit("lang-alphabet: say what its %s is" % k)

    ch = bytes([int(byte, 16)]).decode("latin-1")
    if ch in names:
        raise SystemExit("lang-alphabet: %s already has that character, as"
                         " code %d" % (tag, names.index(ch)))
    if len(variants) != len(names) * 5 + 5:
        raise SystemExit("lang-alphabet: %d names and %d bytes of records is"
                         " not one record each" % (len(names), len(variants)))

    record = bytes(number(values, k, want[k]) for k in RECORD)
    written = ch.replace("\\", "\\\\").replace(" ", "\\s")

    # The name goes after the last one of the field it belongs to, and the
    # record after the last of the records, so every code that exists keeps
    # the meaning it had.
    at_name = max(i for i in range(first, last)
                  if lines[i].startswith("    value")
                  and i < min([j for j in range(first, last)
                               if lines[j].startswith("  field ")
                               and lines[j].split()[1] != "name"]
                              or [last]))
    lines.insert(at_name + 1, "    value %s" % written)

    at_var = max(j for j in var_at) + 1        # the line after the last one
    lines.insert(at_var, "  variants %s"
                 % " ".join("%02x" % b for b in record))

    open(path_of(tag), "w").write("\n".join(lines))
    print("%s: %s is code %d now, %s"
          % (tag, ch, len(names),
             ", ".join("%s %s" % (k, want[k]) for k in RECORD)))
    return True


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip())
        return 2
    what, tag = argv[0], argv[1]
    if what == "show":
        return 0 if show(tag, set(argv[2:])) else 1
    if what == "add" and len(argv) > 2:
        return 0 if add(tag, argv[2], argv[3:]) else 1
    print(__doc__.strip())
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
