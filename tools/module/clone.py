#!/usr/bin/env python3
"""Make a language module out of another one and a recipe.

lang/plpl was made by copying lang/itit's text forms and renaming them, and
everything Polish about it was then written on top. This is that first step
as a tool, for a module that is kept as what it differs by rather than as a
copy: accents/<tag> holds a recipe and the rules written for it, and
lang/<tag> is made out of the template and those whenever it is wanted.

Nothing of the template's is kept twice that way, and a change to the
template reaches every module made from it. What is made is the template's
own data with our rules over the top, so NOTICE governs it exactly as it
governs the template: it is IBM's until it has been replaced, and
`make EVVLANG=lang/<tag> census' says how far that has got.

A recipe is lines of a word and what follows it:

    template dede                     the module it is made from
    name International German         what a person is shown
    library Static Engine INX         the name in the settings
    section 4.0                       the language number, family and dialect

Beside it, all optional:

    rules/*.up          rules of ours, in the upper form, built over the top
    rules/constants     bytes those rules name
    edits               changes to the template's own text, a line each:
                            <file> | <text to find> | <text to put>
                        where the text to find has to occur exactly once
    <tag>.codepoints    the language's own characters

usage: tools/module/clone.py <tag> [...]
       tools/module/clone.py --list
"""

import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from evv import ROOT

ACCENTS = os.path.join(ROOT, "accents")
TEXTS = ("consts", "globals", "sets", "settings", "statements", "dict",
         "codepoints")


def fail(message):
    sys.stderr.write("clone: %s\n" % message)
    sys.exit(1)


def read_recipe(tag):
    path = os.path.join(ACCENTS, tag, "recipe")
    if not os.path.exists(path):
        fail("no recipe at %s" % path)
    recipe = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            key, _, value = line.partition(" ")
            recipe[key] = value.strip()
    for key in ("template", "name", "library", "section"):
        if key not in recipe:
            fail("%s says no %s" % (path, key))
    return recipe


def retag(text, old, new):
    return text.replace(old, new)


def copy_text(src, dst, old, new):
    with open(src, encoding="latin-1", newline="") as f:
        text = f.read()
    text = text.replace("\r\n", "\n")
    with open(dst, "w", encoding="latin-1", newline="\n") as f:
        f.write(retag(text, old, new))


def settings_for(path, recipe, old, new):
    """The library's name and the section that says which language it is."""
    with open(path, encoding="latin-1", newline="") as f:
        lines = f.read().split("\n")
    out = []
    for line in lines:
        if line.startswith("library "):
            line = "library " + recipe["library"]
        elif line.startswith("\\n") and line.rstrip().endswith("]") and "[" in line:
            head = line[:line.index("[")]
            line = "%s[%s]" % (head, recipe["section"])
        out.append(line)
    with open(path, "w", encoding="latin-1", newline="\n") as f:
        f.write("\n".join(out))


def apply_edits(tag, recipe_dir, lang_dir):
    path = os.path.join(recipe_dir, "edits")
    if not os.path.exists(path):
        return 0
    n = 0
    with open(path, encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = [p.strip() for p in line.split("|")]
            if len(parts) != 3:
                fail("%s:%d: wants file | find | put" % (path, number))
            name, find, put = parts
            target = os.path.join(lang_dir, name)
            if not os.path.exists(target):
                fail("%s:%d: no file %s" % (path, number, name))
            with open(target, encoding="latin-1", newline="") as g:
                text = g.read()
            count = text.count(find)
            if count != 1:
                fail("%s:%d: `%s' occurs %d times in %s, not once"
                     % (path, number, find, count, name))
            text = text.replace(find, put.replace("\\n", "\n"))
            with open(target, "w", encoding="latin-1", newline="\n") as g:
                g.write(text)
            n += 1
    return n


def run(*args):
    done = subprocess.run([sys.executable] + list(args), cwd=ROOT)
    if done.returncode != 0:
        fail("%s failed" % " ".join(args))


def clone(tag):
    recipe = read_recipe(tag)
    old = recipe["template"]
    if len(tag) != len(old):
        fail("%s and %s are not the same length, and a name's length is "
             "stated in places this does not reach" % (tag, old))
    src = os.path.join(ROOT, "lang", old)
    dst = os.path.join(ROOT, "lang", tag)
    recipe_dir = os.path.join(ACCENTS, tag)
    if not os.path.isdir(src):
        fail("no module %s to make it from" % old)

    if os.path.isdir(dst):
        shutil.rmtree(dst)
    os.makedirs(os.path.join(dst, "rules"))

    for ext in TEXTS:
        s = os.path.join(src, "%s.%s" % (old, ext))
        if os.path.exists(s):
            copy_text(s, os.path.join(dst, "%s.%s" % (tag, ext)), old, tag)
    if os.path.exists(os.path.join(src, "letters")):
        copy_text(os.path.join(src, "letters"), os.path.join(dst, "letters"),
                  old, tag)
    rules = os.path.join(src, "rules")
    for name in sorted(os.listdir(rules)):
        s = os.path.join(rules, name)
        if os.path.isfile(s):
            copy_text(s, os.path.join(dst, "rules", name), old, tag)

    settings_for(os.path.join(dst, "%s.settings" % tag), recipe, old, tag)

    # What is ours goes over the top.
    ours = os.path.join(recipe_dir, "rules")
    n_rules = 0
    if os.path.isdir(ours):
        for name in sorted(os.listdir(ours)):
            s = os.path.join(ours, name)
            d = os.path.join(dst, "rules", name)
            if name == "constants" and os.path.exists(d):
                with open(s, encoding="utf-8") as f:
                    more = f.read()
                with open(d, "a", encoding="latin-1", newline="\n") as f:
                    f.write("\n" + more)
            elif name.endswith(".up") and os.path.exists(d):
                with open(s, encoding="utf-8") as f:
                    more = f.read()
                with open(d, "a", encoding="latin-1", newline="\n") as f:
                    f.write("\n" + more)
                n_rules += 1
            else:
                shutil.copyfile(s, d)
                n_rules += name.endswith(".up")
    points = os.path.join(recipe_dir, "%s.codepoints" % tag)
    if os.path.exists(points):
        shutil.copyfile(points, os.path.join(dst, "%s.codepoints" % tag))

    n_edits = apply_edits(tag, recipe_dir, dst)

    run("tools/module/gather.py", tag)
    env_tag = tag
    for tool in ("tools/module/globals.py", "tools/module/settings.py",
                 "tools/module/link.py", "tools/module/sets.py",
                 "tools/rules/consts.py"):
        run(tool, "write", env_tag)
    run("tools/rules/consts.py", tag)
    run("tools/module/codepoints.py", tag)
    print("clone: lang/%s made from lang/%s, %d rule files of ours, %d edits"
          % (tag, old, n_rules, n_edits))


def main():
    if len(sys.argv) < 2:
        sys.stderr.write(__doc__)
        sys.exit(2)
    if sys.argv[1] == "--list":
        if os.path.isdir(ACCENTS):
            for name in sorted(os.listdir(ACCENTS)):
                if os.path.exists(os.path.join(ACCENTS, name, "recipe")):
                    print(name)
        return
    for tag in sys.argv[1:]:
        clone(tag)


if __name__ == "__main__":
    main()
