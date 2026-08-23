#!/usr/bin/env python3
"""How much of one language module is still another's.

A language IBM never shipped starts as a copy of one it did -- Polish as
Italian, because Italian's stress is penultimate as Polish's is and its
affricates are the ones Polish needs -- and then becomes itself a rule at a
time. The danger in that is not the copying, which is the only sane way to
start; it is losing track of what has been done, and hearing Italian
phonology come out of something labelled Polish without noticing.

So this counts. For every rule in the module, is its text still character for
character the template's? For every table, is it? The answer is a number that
starts at everything and falls, and what it names is the work that is left.

It reads the text forms and nothing else, so it wants no objects and no
compiler, and it is as true of a module built from text as of one lifted.

usage: lang-census.py <tag> <template>          the counts
       lang-census.py <tag> <template> rules    every rule, one to a line
"""

import difflib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def rules_of(tag):
    """Every rule of a module as its text, by name.

    A rule is taken from its first line to the `end' that closes it, and the
    `from <object>' on the head line is left out of the comparison: it says
    which of IBM's objects the rule was compiled in, which is provenance
    rather than the rule.
    """
    where = os.path.join(ROOT, "lang", tag, "rules")
    if not os.path.isdir(where):
        raise SystemExit("lang-census: %s has no rules as text" % tag)
    out = {}
    # The upper form counts as well, and last: a rule written as what it does
    # stands in for the lifted one of the same name, so a module that has
    # replaced one is not still the template's for that rule. wrappers.up is
    # the other upper form and holds no rule of its own.
    for f in (sorted(x for x in os.listdir(where) if x.endswith(".dr"))
              + sorted(x for x in os.listdir(where)
                       if x.endswith(".up") and x != "wrappers.up")):
        name = None
        body = []
        for raw in open(os.path.join(where, f)):
            line = raw.rstrip("\n")
            if line.startswith("rule "):
                name = line.split()[1]
                body = [" ".join(line.split()[:2])]
            elif name is not None:
                body.append(line)
                # An `end' at the left margin closes a rule in both forms; one
                # indented closes a block inside an upper rule.
                if line == "end":
                    out[name] = "\n".join(body)
                    name = None
    return out


def tables_of(tag):
    """Each text form of a module, with the tag itself taken out.

    The tag is in the head of every one of them and in the names of the
    stores, so a copy that has been renamed and not otherwise touched would
    read as different on every line. What is compared is what the file says
    once its own name is out of it.
    """
    out = {}
    for kind in ("globals", "settings", "statements", "sets", "consts"):
        p = os.path.join(ROOT, "lang", tag, "%s.%s" % (tag, kind))
        if os.path.exists(p):
            out[kind] = open(p).read().replace(tag, "<tag>")
    return out


def census(tag, template, listing=False):
    mine, theirs = rules_of(tag), rules_of(template)
    same = [n for n in mine if n in theirs and mine[n] == theirs[n]]
    changed = [n for n in mine if n in theirs and mine[n] != theirs[n]]
    only_mine = [n for n in mine if n not in theirs]
    only_theirs = [n for n in theirs if n not in mine]

    if listing:
        for n in sorted(mine):
            if n in same:
                what = "the template's"
            elif n in changed:
                what = "changed"
            else:
                what = "ours alone"
            print("%-40s %s" % (n, what))
        for n in sorted(only_theirs):
            print("%-40s %s" % (n, "gone"))
        return True

    total = len(mine)
    print("%s against %s" % (tag, template))
    print("  rules: %d, of which %d are still the template's, %d changed,"
          " %d ours alone" % (total, len(same), len(changed), len(only_mine)))
    if only_theirs:
        print("  and %d of the template's are gone" % len(only_theirs))
    if total:
        print("  so %d%% of the rules are still %s"
              % (100 * len(same) // total, template))

    a, b = tables_of(tag), tables_of(template)
    for kind in sorted(a):
        if kind not in b:
            print("  %-11s ours alone" % kind)
        elif a[kind] == b[kind]:
            print("  %-11s still the template's" % kind)
        else:
            # A real difference and not a positional one: inserting sixteen
            # lines in the middle of a table would otherwise read as every
            # line after them having changed, which is how a small edit comes
            # to look like a rewrite.
            mine_lines = a[kind].split("\n")
            their_lines = b[kind].split("\n")
            added = gone = 0
            for tag_, i1, i2, j1, j2 in difflib.SequenceMatcher(
                    None, their_lines, mine_lines).get_opcodes():
                if tag_ in ("insert", "replace"):
                    added += j2 - j1
                if tag_ in ("delete", "replace"):
                    gone += i2 - i1
            print("  %-11s %d lines of %d are ours, %d of the template's gone"
                  % (kind, added, len(mine_lines), gone))
    return True


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip())
        return 2
    listing = len(argv) > 2 and argv[2] == "rules"
    return 0 if census(argv[0], argv[1], listing) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
