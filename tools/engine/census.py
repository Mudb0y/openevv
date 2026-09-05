#!/usr/bin/env python3
"""What of IBM's objects has an answer in this tree, and what has none.

`make missing' answers the question the linker can answer: what our code
asks for that nothing of ours defines. It is silent about the other
direction -- a function of IBM's that nothing here has written, and that no
rule in the nine shipped languages happens to call, so the link never asks.
That is where the Delta runtime's printing layer hid for months.

This asks the other direction. Every object in `analysis/<tag>' is asked what
it defines, and each name is looked for here three ways.

By alias, which is exact: the `ALIAS' and `MANGLED' lines in the sources
claim IBM's own spelling for a function of ours, so a name that appears in
one is answered and there is nothing to argue about.

By name, which is a judgement: our sources prefix a function with the object
or class it came out of -- `es_engsynReadPhonemes' for `engsynReadPhonemes',
`fm_getINIValue' for `FilterManager::getINIValue' -- so the bare name matches
after the prefix comes off. Most of the tree is answered this way, because a
file-static needs no alias and never had one.

A bare name is only allowed to answer when it is IBM's alone. Eleven classes
in the object set have a method called `run' and four have a `dump', so one
function of ours called `th_run' would otherwise answer for all eleven and
the count would flatter itself.

Where a name is shared, the prefix has to agree as well, and which prefix
means which class is learned from the tree rather than written down here:
every `ALIAS' line pairs one of IBM's mangled names with one of ours, so
`?getINIValue@FilterManager@@' beside `fm_getINIValue' teaches that `fm_' is
FilterManager, and `th_run' is then ETIThread's `run' and nobody else's. The
aliases the tree already carries settle nearly every prefix; a shared name
under a prefix nothing has taught is left as no answer, which is the
conservative direction. Those matches are printed under `--matched' to be
looked over rather than trusted blindly.

And not at all, which is the answer this exists to produce.

Four kinds of name are dropped before any of that, each for its own reason.
A language's rules and tables are lifted as data rather than ported, so the
objects holding them are not code to answer for. The compiler's own
destructor and constructor helpers -- ??_G, ??_E, ??_H, ??_I -- belong to
vtables ours does not have. String constants and vtables are data. And a
local label is not a function.

    tools/engine/census.py                 the count, and what has no answer
    tools/engine/census.py --matched       what was answered by name, to check
    tools/engine/census.py --object eci    one object, in full

It reads the objects with binutils `nm', which is safe: it is instruction
decoding that GNU tools get wrong on these objects, not the symbol table.
Sizes come from `llvm-objdump', which is not.
"""

import glob
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
NM = os.environ.get("EVV_NM", "x86_64-w64-mingw32-nm")
OBJDUMP = os.environ.get("EVV_OBJDUMP", "llvm-objdump")

# The objects that are a language rather than the engine. `make rulecode' and
# the module tools own these; nothing in src answers for them.
DATA = re.compile(r"^(ea_|e_|es_|et_|u_|us_|ut_|ed_|glob|link|clsyn)")

# Where our own code is. lang and rom are in it because a language module
# carries its own set builders -- `enus_set_dict_new' answers `set_dict_new'
# -- and the romanizer is ours as well. lib is in it because the published
# names are wrappers there, and two of them exist nowhere else: the filter
# queries, which are empty in IBM's object and are answered by being empty
# here. Leaving lib out reported those two as missing.
SOURCES = ("src", "lang", "rom", "lib")

# A prefix our sources put in front of a name they took from an object or a
# class. Two to four lower-case letters and an underscore covers every one
# the tree uses, and taking it off is what lets a bare name match.
PREFIX = re.compile(r"^[a-z][a-z0-9]{0,3}_")


def shared_bare_names(objs):
    """Bare names more than one of IBM's classes uses, which therefore cannot
    answer on their own."""
    seen = {}
    for o in objs:
        out = subprocess.run([NM, "--defined-only", o],
                             capture_output=True, text=True).stdout
        for line in out.splitlines():
            if not re.search(r"\s[Tt]\s", line):
                continue
            sym = line.split()[-1]
            m = re.match(r"^\?\??[014]?(\w+)@(\w+)@@", sym)
            if not m:
                continue
            seen.setdefault(m.group(1), set()).add(m.group(2))
    return {n for n, classes in seen.items() if len(classes) > 1}


def bare_names(symbol):
    """Every name IBM's symbol could be answered under, or None to skip it."""
    s = symbol

    # Data and the compiler's own helpers, which are not functions to write.
    if s.startswith("??_C@") or s.startswith("??_7") or s.startswith("??_8"):
        return None
    if re.match(r"^\?\?_[GEHI]", s):
        return None
    # A local label, a section, or one of the static initialisers Microsoft's
    # runtime walked -- every one of those is a local `$E' in a `.text$yc'
    # COMDAT, and src/port/port_ctors.c answers the lot of them by hand. The
    # underscore the object format adds comes off first, since it is on some
    # of them and not others.
    if re.match(r"^_?(\$L|\$E|\.text)", s):
        return None
    if s in ("@comp.id", "@feat.00", ".drectve", ".rdata", ".data", ".bss"):
        return None

    # A constructor or a destructor of a class. The bare kind on its own --
    # `ctor', `dtor' -- is deliberately not a candidate: our own names come
    # out as that once the prefix is off, so accepting it would answer every
    # destructor in the object set with the first one written here.
    m = re.match(r"^\?\?([014])(\w+)@@", s)
    if m:
        kind = {"0": "ctor", "1": "dtor", "4": "assign"}[m.group(1)]
        return {m.group(2), m.group(2) + "_" + kind}

    # An ordinary method: ?name@Class@@signature.
    m = re.match(r"^\?(\w+)@(\w+)@@", s)
    if m:
        return {m.group(1), m.group(2) + "_" + m.group(1)}

    # A free C++ function: ?name@@signature.
    m = re.match(r"^\?(\w+)@@", s)
    if m:
        return {m.group(1)}

    # Plain C, with the leading underscore the object format adds, an @N if
    # it is stdcall, and a leading @ if it is fastcall. A C name belongs to no
    # class, so the ambiguity rule below has nothing to say about it and the
    # trailing dot marks it as exempt.
    s = s.lstrip("@").lstrip("_")
    s = s.split("@")[0]
    return {s, s + "."} if s else None


def ours():
    """Every name this tree defines, every name it claims by alias, and what
    each of our prefixes turns out to mean."""
    defined, claimed, prefixed = set(), set(), set()
    for top in SOURCES:
        for root, _, files in os.walk(os.path.join(ROOT, top)):
            for fn in files:
                if not fn.endswith((".c", ".h")):
                    continue
                path = os.path.join(root, fn)
                s = open(path, encoding="utf-8", errors="replace").read()
                # An ALIAS or MANGLED name may be split across lines as two
                # C string literals, so the pieces are joined back up.
                for m in re.finditer(r'(?:ALIAS(?:_N)?|MANGLED)\(\s*((?:"[^"]*"\s*)+)', s):
                    claimed.add("".join(re.findall(r'"([^"]*)"', m.group(1))))
                for m in re.finditer(
                        r"^[A-Za-z_][\w \*\t]*?[\* ](\w+)\([^;{]*?\)\s*\{", s, re.M):
                    defined.add(m.group(1))

    # Which of our prefixes stands for which of IBM's classes, learned from
    # the pairs the ALIAS lines already state.
    means = {}
    for c in claimed:
        m = re.match(r"^\?\??[014]?(\w+)@(\w+)@@", c)
        if not m:
            continue
        method, klass = m.group(1), m.group(2)
        for n in defined:
            p = PREFIX.match(n)
            if p and n[p.end():] == method:
                means.setdefault(n[:p.end()], {}).setdefault(klass, 0)
                means[n[:p.end()]][klass] += 1
    # A prefix means whichever class it was paired with most often.
    prefix_class = {p: max(v, key=v.get) for p, v in means.items()}

    # A defined name answers for itself, for whatever it is prefixed from,
    # and -- where the prefix is known -- for that class's method by name.
    for n in list(defined):
        p = PREFIX.match(n)
        if not p:
            continue
        stripped = n[p.end():]
        if not stripped:
            continue
        defined.add(stripped)
        klass = prefix_class.get(n[:p.end()])
        if klass:
            defined.add(klass + "_" + stripped)
    return defined, claimed


def ibm_sizes(objs):
    """How many instructions each name holds, for sizing what is left.

    The largest copy rather than the sum of them. A name can be defined in
    several objects -- MSVC emits an inline or a template body into every
    object that uses it, each in its own COMDAT -- and adding those up sizes
    a function at a multiple of itself. It read as a body where there was
    none: `translateMessage' is six instructions and does nothing, and two
    copies of it came to twelve, which is over the line `make stubs' draws
    at seven for a method that only returns. Same for `getSamples' at five
    and five. Copies of one COMDAT are identical, so the largest is the
    size."""
    cache = os.path.join(ROOT, "build", "census-sizes.json")
    if os.path.exists(cache):
        try:
            return json.load(open(cache))
        except Exception:
            pass
    sizes = {}
    for o in objs:
        try:
            out = subprocess.run([OBJDUMP, "-d", "--no-show-raw-insn", o],
                                 capture_output=True, text=True,
                                 timeout=180).stdout
        except Exception:
            continue
        cur = None
        here = {}
        for line in out.splitlines():
            m = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
            if m:
                cur = m.group(1)
                here.setdefault(cur, 0)
            elif cur and re.match(r"^\s+[0-9a-f]+:\s", line):
                here[cur] += 1
        for name, n in here.items():
            if n > sizes.get(name, 0):
                sizes[name] = n
    os.makedirs(os.path.dirname(cache), exist_ok=True)
    json.dump(sizes, open(cache, "w"))
    return sizes


def main(argv):
    show_matched = "--matched" in argv
    only = None
    if "--object" in argv:
        only = argv[argv.index("--object") + 1]

    tag = os.environ.get("EVV_CENSUS_TAG", "enus")
    objs = sorted(glob.glob(os.path.join(ROOT, "analysis", tag, "*.obj")))
    if not objs:
        print("census: no objects in analysis/%s; docs/building.md says where"
              " IBM's SDK is" % tag, file=sys.stderr)
        return 2

    defined, claimed = ours()
    sizes = ibm_sizes(objs)
    shared = shared_bare_names(objs)

    rows = []
    by_alias = by_name = 0
    matched_by_name = []

    for o in objs:
        base = os.path.basename(o)[:-4]
        if DATA.match(base) or (only and base != only):
            continue
        out = subprocess.run([NM, "--defined-only", o],
                             capture_output=True, text=True).stdout
        syms = [l.split()[-1] for l in out.splitlines()
                if re.search(r"\s[Tt]\s", l)]

        missing, total = [], 0
        for sym in syms:
            names = bare_names(sym)
            if names is None:
                continue
            total += 1
            if sym in claimed:
                by_alias += 1
                continue
            # A name IBM gives to more than one class answers only in its
            # qualified form. A plain C name is exempt, which the marker in
            # bare_names says; it is taken off again here.
            if any(n.endswith(".") for n in names):
                candidates = {n.rstrip(".") for n in names}
            else:
                candidates = {n for n in names if "_" in n or n not in shared}
            hit = candidates & defined
            if hit:
                by_name += 1
                matched_by_name.append((base, sym, sorted(hit)[0]))
                continue
            missing.append((sizes.get(sym, 0), sym))
        if missing:
            missing.sort(reverse=True)
            rows.append((sum(m[0] for m in missing), missing, total, base))

    if show_matched:
        print("Answered by name rather than by alias, %d of them. Each line is"
              % len(matched_by_name))
        print("IBM's spelling and the name here that answers it.\n")
        for base, sym, hit in sorted(matched_by_name):
            print("  %-16s %-58s %s" % (base, sym[:58], hit))
        return 0

    rows.sort(reverse=True)
    print("Objects with names that have no answer here, largest first.")
    print("Instructions is how much code is in them.\n")
    print("  %-20s %7s  %s" % ("object", "instrs", "names"))
    for ins, missing, total, base in rows:
        print("  %-20s %7d  %d of %d" % (base, ins, len(missing), total))
        if only:
            for c, sym in missing:
                print("      %6d  %s" % (c, sym))

    left = sum(len(r[1]) for r in rows)
    ins = sum(r[0] for r in rows)
    print("\n%d names answered by alias, %d by name, %d with no answer,"
          % (by_alias, by_name, left))
    print("in %d instructions across %d objects." % (ins, len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
