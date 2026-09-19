#!/usr/bin/env python3
"""What a rule's named string actually says.

A rule names a run of bytes by a symbol -- `push sym string_30' -- and
`lang/<tag>/rules/symbols' says which store it falls in and how far in.  The
bytes themselves are in `lang/<tag>/<tag>.consts'.  Neither is readable by eye,
because the bytes are in the language's own two alphabets: a letter is the code
its input statement gives it and a phoneme the code its statement gives that,
and the same byte is a different thing in each.  Which one a string holds is
decided by what the rule does with it, so both are printed and the caller
chooses.

That is the whole of reading a letter-to-sound rule.  `b_rules' tests
`string_30', which is `bt', and inserts one of two phones -- and `debt' and
`subtle' are what that comes to.

    tools/rules/strings.py <tag> <symbol>...     what each one says
    tools/rules/strings.py <tag> --in <object>   every symbol of one object
    tools/rules/strings.py <tag> --rule <name>   one rule's calls, strings read
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def stores(tag):
    """Every store of bytes the language's rules name, by name."""
    out, cur = {}, None
    for line in open("%s/lang/%s/%s.consts" % (ROOT, tag, tag)):
        m = re.match(r"^store (\S+)", line)
        if m:
            cur = m.group(1)
            out[cur] = bytearray()
            continue
        if line.startswith("bytes ") and cur:
            out[cur] += bytes(int(b, 16) for b in line.split()[1:])
    return out


def symbols(tag):
    """Every symbol, as the object that names it, its store and its offset."""
    out = {}
    for line in open("%s/lang/%s/rules/symbols" % (ROOT, tag)):
        p = line.split()
        if len(p) == 5 and p[0] == "at":
            out.setdefault(p[2], []).append((p[1], p[3], int(p[4])))
    return out


def codes(tag, tool):
    """The numbered names one of the two tools prints, as a table.

    The two take their argument differently -- the alphabet wants a verb in
    front of the language and the phonemes do not -- which is worth saying
    here rather than discovering as an empty table."""
    out = {}
    args = ["show", tag] if tool == "alphabet.py" else [tag]
    said = subprocess.run(["python3", "%s/tools/module/%s" % (ROOT, tool)] + args,
                          capture_output=True, text=True).stdout
    for line in said.splitlines():
        p = line.split()
        if len(p) >= 2 and p[0].isdigit():
            out[int(p[0])] = p[1]
    return out


def say(tag, names, only=None):
    store, sym = stores(tag), symbols(tag)
    letters = codes(tag, "alphabet.py")
    phones = codes(tag, "phonemes.py")

    for n in names:
        for obj, where, off in sym.get(n, []):
            if only and obj != only:
                continue
            b = store[where]
            end = off
            while end < len(b) and b[end]:
                end += 1
            raw = bytes(b[off:end])
            print("%s in %s, %s+%d, %d bytes" % (n, obj, where, off, len(raw)))
            print("   bytes   %s" % " ".join("%02x" % c for c in raw))
            print("   letters %s" % "".join(letters.get(c, "<%d>" % c) for c in raw))
            print("   phones  %s" % " ".join(phones.get(c, "<%d>" % c) for c in raw))


def one_rule(tag, name):
    """A rule's calls with the strings it names read out beside them.

    A letter rule is a list of arms and an arm is a scan, a test and an
    insertion, so what it does is readable the moment the strings are: the
    calls alone say scan, test, save, insert, and the strings say which
    letters and which phones.
    """
    store, sym = stores(tag), symbols(tag)
    letters = codes(tag, "alphabet.py")
    phones = codes(tag, "phonemes.py")

    def read(name, obj):
        for at, where, off in sym.get(name, []):
            if at != obj and at != "glob.obj":
                continue
            b = store.get(where)
            if b is None:
                continue
            end = off
            while end < len(b) and b[end]:
                end += 1
            raw = bytes(b[off:end])
            return ("%s = letters %r, phones %r"
                    % (name,
                       "".join(letters.get(c, "<%d>" % c) for c in raw),
                       " ".join(phones.get(c, "<%d>" % c) for c in raw)))
        return name

    import glob as _glob
    inside = False
    obj = None
    for path in sorted(_glob.glob(os.path.join(ROOT, "lang", tag,
                                               "rules", "*.dr"))):
        for line in open(path):
            w = line.split()
            if w[:1] == ["rule"] and len(w) > 1:
                inside = w[1] == name
                if inside and len(w) > 3:
                    obj = w[3]
                continue
            if not inside:
                continue
            if line.rstrip() == "end":
                return True
            if w[:1] == ["label"]:
                print(line.rstrip())
            elif w[:1] == ["call"]:
                entry = w[1]
                said = ""
                m = re.search(r"(ZZstring\d+|string_\d+)$", entry)
                if m:
                    said = "   -- %s" % read(m.group(1), obj)
                print("  call %s%s" % (entry, said))
            elif w[:2] == ["push", "sym"]:
                print("  push sym %s   -- %s" % (w[2], read(w[2], obj)))
    return False


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip())
        return 2
    tag = argv[0]
    if argv[1] == "--rule":
        if not one_rule(tag, argv[2]):
            print("strings: no rule called %s" % argv[2])
            return 1
        return 0
    if argv[1] == "--in":
        obj = argv[2]
        sym = symbols(tag)
        names = [n for n, ws in sym.items() if any(w[0] == obj for w in ws)]
        say(tag, sorted(names), only=obj)
        return 0
    say(tag, argv[1:])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
