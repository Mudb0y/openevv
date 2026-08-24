#!/usr/bin/env python3
"""A language's phonemes, from the three places they live.

A phoneme is in three places at once and none of them alone says what it is.

Its name is a value of the phone statement's first field, in
`lang/<tag>/<tag>.statements`, and that is the list the rules index by. Its
numbers are a `Phoneme` line in `lang/<tag>/<tag>.settings`: four bytes of name
and then eleven values, which is what a caller handing the engine phonemes
rather than text is read against. And what it sounds like is a rule --
`<x>_ph_<name>` -- which sets the source parameters and calls one locus rule
for the place it is articulated at, `<x>_<place>_Fv`, where the formant targets
are.

So this puts the three beside each other: every phoneme the language declares,
whether it has numbers, whether it has a rule, and which place that rule speaks
it at. What it is for is adding one, which means having somewhere to copy from
and knowing what a copy has to cover.

A phoneme also carries a record saying what kind of sound it is, in the same
place a letter's does: the `variants' bytes of its own statement, one record
per name, and the statement says how long one is. For the phone statement they
are its own fields -- class, voicing, sonority, manner, place, and the three a
vowel wants -- so `place' is where a language says that its sz is retroflex and
its s is not. Reading those by eye and writing them by hand is how a fricative
quietly becomes a lateral, so `set' writes them by name.

usage: lang-phonemes.py <tag>            every phoneme
       lang-phonemes.py <tag> <name>...  only the ones named
       lang-phonemes.py set <tag> <phoneme> <field>=<value>...
       lang-phonemes.py add <tag> <name> <field>=<value>...

`add' puts a phoneme on the end, which is the only place one can go: the codes
below it are what every rule and every dictionary entry already say. It is the
last resort rather than the first -- a code that has never existed is invisible
to every rule that asks what its neighbours are, so retaking one the language
cannot reach is the better move where there is one to retake.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def inventory(tag):
    """The names the phone statement declares, in order, which is the order
    the rules index them by."""
    p = os.path.join(ROOT, "lang", tag, "%s.statements" % tag)
    inside = False
    field = None
    out = []
    for line in open(p):
        if line.startswith("statement phone"):
            inside = True
            continue
        if line.startswith("statement ") and inside:
            break
        if not inside:
            continue
        if line.startswith("  field "):
            field = line.split()[1]
        elif line.startswith("    value") and field == "name":
            t = line[len("    value"):].rstrip("\n")
            out.append(t[1:] if t.startswith(" ") else t)
    return out


def statement(tag, want="phone"):
    """One statement as its lines, its value names, its field order, the
    stride of one variant record and where the records are written.

    The record covers the fields after the name, less `afterslash' where the
    statement has one, and there are as many of them as the stride says. That
    is not stated anywhere: it is what makes the arithmetic come out for all
    nine languages, whose strides run from three to eight.
    """
    p = os.path.join(ROOT, "lang", tag, "%s.statements" % tag)
    lines = open(p).read().split("\n")
    inside = False
    field = None
    fields = []
    values = {}
    names = []
    stride = None
    var = bytearray()
    var_at = []

    for i, line in enumerate(lines):
        if line == "statement %s" % want:
            inside = True
            continue
        if not inside:
            continue
        if line.startswith("statement ") or line == "end":
            break
        w = line.split()
        if line.startswith("  field "):
            field = w[1]
            fields.append(field)
            values.setdefault(field, [])
        elif line.startswith("    value") and field:
            t = line[len("    value"):]
            t = t[1:] if t.startswith(" ") else t
            t = t.replace("\\s", " ").replace("\\\\", "\\")
            values[field].append(t)
            if field == "name":
                names.append(t)
        elif w[:3] == ["at", "start", "stride"]:
            stride = int(w[3])
        elif w[:1] == ["variants"]:
            var_at.append(i)
            var += bytes(int(x, 16) for x in w[1:])

    record = [f for f in fields if f not in ("name", "afterslash")][:stride or 0]
    at = next(i for i, l in enumerate(lines) if l == "statement %s" % want)
    return {"path": p, "lines": lines, "names": names, "values": values,
            "fields": fields, "record": record, "stride": stride,
            "variants": bytes(var), "var_at": var_at, "at": at}


def add_phoneme(tag, name, args):
    """One more phoneme, on the end of the statement's names and of its
    records. Everything below keeps the code it had, which is what makes this
    safe to do at all."""
    st = statement(tag)
    if name in st["names"]:
        raise SystemExit("lang-phonemes: %s already has %r, as code %d"
                         % (tag, name, st["names"].index(name)))
    stride = st["stride"]
    if len(st["variants"]) != len(st["names"]) * stride:
        raise SystemExit("lang-phonemes: %d names and %d bytes of records is"
                         " not one record each"
                         % (len(st["names"]), len(st["variants"])))
    rec = bytearray(stride)
    for a in args:
        if "=" not in a:
            raise SystemExit("lang-phonemes: %r is not field=value" % a)
        k, v = a.split("=", 1)
        if k not in st["record"]:
            raise SystemExit("lang-phonemes: a record has no %r; it has %s"
                             % (k, ", ".join(st["record"])))
        table = st["values"].get(k, [])
        if v not in table:
            raise SystemExit("lang-phonemes: %s has no value called %r; it has"
                             " %s" % (k, v, ", ".join(table)))
        rec[st["record"].index(k)] = table.index(v)

    lines = st["lines"]
    # The name goes after the last value of the *name* field, which is not the
    # last value line in the statement: every field has its own list and they
    # follow one another. Putting it after the last of them all adds a value to
    # whatever field comes last instead, which is a silent way to break a
    # record's meaning.
    first = next(i for i, l in enumerate(lines)
                 if l.strip() == "field name" and i > st["at"])
    stop = next(i for i in range(first + 1, len(lines))
                if lines[i].startswith("  field "))
    at_name = max(i for i in range(first, stop)
                  if lines[i].startswith("    value"))
    # The records go back first and the name after, because every index here
    # was taken from the file as it was: the name's line sits above the
    # records, so writing the records cannot move it, where writing the name
    # first would move every record line by one.
    var = bytearray(st["variants"]) + rec
    out = ["  variants " + " ".join("%02x" % b for b in var[i:i + 32])
           for i in range(0, len(var), 32)]
    lines[min(st["var_at"]):max(st["var_at"]) + 1] = out
    lines.insert(at_name + 1, "    value %s" % name)
    open(st["path"], "w").write("\n".join(lines))
    print("%s: %s is code %d now, %s"
          % (tag, name, len(st["names"]),
             ", ".join("%s %s" % (f, st["values"][f][rec[i]])
                       for i, f in enumerate(st["record"]))))
    return True


def set_record(tag, phoneme, args):
    """Change named fields of one phoneme's record, and write the file back
    with everything else exactly as it came."""
    st = statement(tag)
    if phoneme not in st["names"]:
        raise SystemExit("lang-phonemes: %s has no phoneme called %r"
                         % (tag, phoneme))
    code = st["names"].index(phoneme)
    stride = st["stride"]
    if len(st["record"]) != stride:
        raise SystemExit("lang-phonemes: the statement says a record is %d"
                         " bytes and names %d fields for it"
                         % (stride, len(st["record"])))
    at = code * stride
    if at + stride > len(st["variants"]):
        raise SystemExit("lang-phonemes: %s has no record" % phoneme)
    rec = bytearray(st["variants"][at:at + stride])

    for a in args:
        if "=" not in a:
            raise SystemExit("lang-phonemes: %r is not field=value" % a)
        k, v = a.split("=", 1)
        if k not in st["record"]:
            raise SystemExit("lang-phonemes: a record has no %r; it has %s"
                             % (k, ", ".join(st["record"])))
        table = st["values"].get(k, [])
        if v not in table:
            raise SystemExit("lang-phonemes: %s has no value called %r; it"
                             " has %s" % (k, v, ", ".join(table)))
        rec[st["record"].index(k)] = table.index(v)

    var = bytearray(st["variants"])
    was = bytes(var[at:at + stride])
    var[at:at + stride] = rec
    if bytes(var[at:at + stride]) == was:
        print("%s: %s already says that" % (tag, phoneme))
        return True

    out = ["  variants " + " ".join("%02x" % b for b in var[i:i + 32])
           for i in range(0, len(var), 32)]
    lines = st["lines"]
    lines[min(st["var_at"]):max(st["var_at"]) + 1] = out
    open(st["path"], "w").write("\n".join(lines))
    print("%s: %s is now %s" % (tag, phoneme, ", ".join(
        "%s %s" % (f, st["values"][f][rec[i]])
        for i, f in enumerate(st["record"]))))
    return True


def declared(tag):
    """The numbers each phoneme is declared with in the settings: its name out
    of the first four bytes, and the eleven values after them."""
    p = os.path.join(ROOT, "lang", tag, "%s.settings" % tag)
    out = {}
    for line in open(p):
        m = re.match(r"Phoneme(\d+)=(.*)", line.strip())
        if not m:
            continue
        nums = [int(x) for x in m.group(2).split()]
        name = "".join(chr(c) for c in nums[:4] if c)
        out[name] = (int(m.group(1)), nums[4:])
    return out


def rules(tag):
    """Which rule speaks each phoneme, and the place it speaks it at.

    A rule for a phoneme is named for it, and the one call it makes to
    something ending in _Fv is the place: that rule holds the formant targets.
    """
    where = os.path.join(ROOT, "lang", tag, "rules")
    out = {}
    for f in sorted(os.listdir(where)):
        if not (f.endswith(".dr") or f.endswith(".up")) or f == "wrappers.up":
            continue
        name = None
        for raw in open(os.path.join(where, f)):
            line = raw.rstrip("\n")
            if line.startswith("rule "):
                name = line.split()[1]
                continue
            if name is None:
                continue
            m = re.match(r"\s+call (\S+_Fv)\b", line)
            if m:
                ph = re.match(r".*_ph_(.+?)(_dur)?$", name)
                if ph:
                    out.setdefault(ph.group(1), (name, set()))
                    out[ph.group(1)][1].add(m.group(1))
    return out


def show(tag, want):
    names = inventory(tag)
    nums = declared(tag)
    said = rules(tag)

    print("%s: %d phonemes declared in the statements, %d in the settings,"
          " %d with a rule" % (tag, len(names), len(nums), len(said)))
    for code, nm in enumerate(names):
        if want and nm not in want:
            continue
        if nm == "GAP":
            continue
        line = "%3d  %-5s" % (code, nm)
        if nm in nums:
            line += " numbers %-2d" % nums[nm][0]
        else:
            line += " no numbers"
        if nm in said:
            rule, places = said[nm]
            line += "  %-16s at %s" % (rule, ", ".join(sorted(places)))
        else:
            line += "  no rule of its own"
        print(line)
    missing = [nm for nm in nums if nm not in names]
    if missing:
        print("  and the settings declare %s, which the statements do not"
              % ", ".join(sorted(missing)))
    return True


def main(argv):
    if argv[:1] == ["add"]:
        if len(argv) < 4:
            print(__doc__.strip())
            return 2
        return 0 if add_phoneme(argv[1], argv[2], argv[3:]) else 1
    if argv[:1] == ["set"]:
        if len(argv) < 4:
            print(__doc__.strip())
            return 2
        return 0 if set_record(argv[1], argv[2], argv[3:]) else 1
    if not argv:
        print(__doc__.strip())
        return 2
    return 0 if show(argv[0], set(argv[1:])) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
