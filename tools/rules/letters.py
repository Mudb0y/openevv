#!/usr/bin/env python3
"""Letter-to-sound as a file a person can edit.

A language's letter rules are one rule to a letter -- `a_rules' through
`z_rules' in `lang/<tag>/rules/et_phone.dr' -- and each is a list of arms tried
in order. An arm sets the scan on the letters, tests what stands there, saves
where the scan got to, and spells the range between the two points as phones.
That is letter-to-sound and nothing else, so this is the file it wants to be
written in:

    letter b
      bt    says t        # debt
      b     says b

One block a letter, arms in order, the first that matches winning, and the last
arm of a block the bare letter with no test at all -- what the letter says when
nothing else applies. `says nothing' is an arm that swallows its letters and
lays no phones down.

The phones are the ETI phone letters, the same ones `lang/<tag>/<tag>.dict'
already uses, and the letters are the language's own characters.

What it writes is `lang/<tag>/rules/et_phone.up', which the build compiles over
the lifted text in the ordinary way, so a rule written here stands where IBM's
compiled one stood and `test/words.sh' says whether any of 24,318 words moved.

The strings an arm needs -- the letters it tests and the phones it lays down --
have to be bytes the module already carries. Every string IBM's own rules test
is one, which is why a transcription needs nothing new; an arm that wants a
string no rule has ever named says so and names the bytes, and the answer is a
line in `lang/<tag>/rules/constants' and a `make constants'.

usage: tools/rules/letters.py show  <tag>     what the file compiles to
       tools/rules/letters.py write <tag>     write rules/et_phone.up
       tools/rules/letters.py regenerate <tag>  hold the tree against the file
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from evv import ROOT, sibling                                  # noqa: E402

alphabet = sibling("module/alphabet")
phonemes = sibling("module/phonemes")


# The three variables every letter rule works through. 844 and 852 are the two
# ends of the range an arm spells: the rule that walks the word sets them
# around the letter before it calls us, and an arm that swallows more letters
# moves the second one along.
#
# 884 is the pointer that says the run an arm matched is one piece of the word
# rather than two, and it is not decoration. `b' before `t' says nothing in
# debt and in subtle, and says /b/ in ob-tain and in sub-tract, where the b
# ends a prefix and the t starts a root. Without this test those four words
# and four more come out wrong, which is how it was found.
LEFT, RIGHT, ONEPIECE = 844, 852, 884

# Which field is which. A field is a level of the spine: 1 is the letters the
# scan walks and 2 is the phones an arm lays down.
LETTERS, PHONES = 1, 2


class Trouble(Exception):
    pass


# ---- the language's own two alphabets ------------------------------------

def letter_codes(tag):
    """Every character of the language and the code it arrives as."""
    names = alphabet.read(tag)[3]
    return dict((name, n) for n, name in enumerate(names))


def phone_codes(tag):
    """Every phoneme the language declares and the code the rules index it by.

    The code is its place in the phone statement, and the statement's first
    value is the gap rather than a sound, so `b' is one and not nought -- the
    same numbering tools/module/phonemes.py prints.
    """
    return dict((name, n) for n, name in enumerate(phonemes.inventory(tag)))


# ---- the strings an arm names ------------------------------------------

# A rule names a string by a symbol, and a symbol belongs to the object the
# rule came out of -- except for a constant of the language's own, which
# tools/rules/consts.py records against no object and which any rule may name.
# So the strings here are minted rather than borrowed: a letter rule of ours
# cannot name the ones IBM's rules name, because those belong to glob.obj.


class Strings(object):
    """The byte strings the arms want, named and kept in the order they were
    first asked for, so that two runs of this tool write the same file."""

    def __init__(self):
        self.by_bytes = {}
        self.order = []

    def name(self, kind, data, spelled):
        if data in self.by_bytes:
            return self.by_bytes[data]
        if kind == "lts" and re.match(r"^[a-z0-9]+$", spelled):
            name = "lts_%s" % spelled
        else:
            name = "%s_%s" % (kind, "".join("%02x" % c for c in data))
        n, base = 1, name
        while name in [m for _d, m, _s in self.order]:
            n += 1
            name = "%s_%d" % (base, n)
        self.by_bytes[data] = name
        self.order.append((data, name, spelled))
        return name

    def text(self, tag):
        out = [
            "# The strings %s's letter rules name, written by" % tag,
            "# tools/rules/letters.py out of lang/%s/letters. Every line here" % tag,
            "# is one arm's letters or one arm's phones, in the language's own",
            "# codes rather than in ASCII.",
            "#",
            "# It is not hand-written and editing it changes nothing: the file",
            "# to edit is lang/%s/letters." % tag,
            "",
        ]
        for data, name, spelled in self.order:
            out.append("bytes %-18s %s  # %s"
                       % (name, " ".join("%02x" % c for c in data), spelled))
        return "\n".join(out) + "\n"


# ---- the file ------------------------------------------------------------

class Arm(object):
    def __init__(self, where, letters, phones, note):
        self.where = where
        self.letters = letters      # the whole run, the block's letter first
        self.phones = phones        # [] for an arm that says nothing
        self.note = note


def parse(path):
    """The file as blocks of arms, in the order they are written."""
    blocks = []
    cur = None
    for n, raw in enumerate(open(path), 1):
        where = "%s line %d" % (os.path.basename(path), n)
        note = ""
        if "#" in raw:
            raw, note = raw.split("#", 1)
            note = note.strip()
        line = raw.strip()
        if not line:
            continue
        w = line.split()
        if w[0] == "letter":
            if len(w) != 2:
                raise Trouble("%s: a block is `letter' and one character"
                              % where)
            cur = (w[1], [])
            blocks.append(cur)
            continue
        if cur is None:
            raise Trouble("%s: an arm before any letter block" % where)
        if "says" not in w:
            raise Trouble("%s: an arm is letters, `says', and phones" % where)
        at = w.index("says")
        if at != 1:
            raise Trouble("%s: an arm names one run of letters before `says'"
                          % where)
        letters, said = w[0], w[at + 1:]
        if not said:
            raise Trouble("%s: `says' what?" % where)
        phones = [] if said == ["nothing"] else said
        if not letters.startswith(cur[0]):
            raise Trouble("%s: this is the %s block, so an arm starts with %s"
                          % (where, cur[0], cur[0]))
        cur[1].append(Arm(where, letters, phones, note))
    return blocks


# ---- what it compiles to -------------------------------------------------

def rule_for(tag, letter, arms, known, lcode, pcode):
    """One letter's rule, as the upper form.

    The shape is IBM's own and the tags are its numbering: the arm to fall to
    is planted before an arm is tried, the scan pointer is saved under a tag of
    its own so that a failure inside an arm comes back to the arm rather than
    to the letter, and one tag stands for the rule having spelled something.
    """
    if not arms:
        raise Trouble("the %s block has no arms" % letter)
    if arms[-1].letters != letter:
        raise Trouble("the last arm of the %s block is what %s says when"
                      " nothing else applies, so it is bare %s"
                      % (letter, letter, letter))
    for a in arms[:-1]:
        if a.letters == letter:
            raise Trouble("%s: a bare %s matches everything, so nothing after"
                          " it can be reached" % (a.where, letter))

    def letters_of(run):
        try:
            return bytes(lcode[c] for c in run)
        except KeyError as e:
            raise Trouble("%s has no character %s" % (tag, e))

    def phones_of(said):
        try:
            return bytes(pcode[p] for p in said)
        except KeyError as e:
            raise Trouble("%s has no phoneme %s" % (tag, e))

    out = []
    w = out.append
    w("rule %s_rules takes 1 from et_phone.obj" % letter)
    w("  through wrappers")
    w("  afresh")
    w("  variable leftpoint word %d" % LEFT)
    w("  variable rightpoint word %d" % RIGHT)
    w("  variable onepiece word %d" % ONEPIECE)
    w("")
    w("  call ZZfenceZZstring376")

    # The tags. One a spelling arm, one for each arm that can be fallen to,
    # and the last for the rule having spelled something.
    tag_of = {}
    n = 0
    for i, a in enumerate(arms):
        if i:
            n += 1
            tag_of[("fall", i)] = n
        if a.letters != letter:
            n += 1
            tag_of[("body", i)] = n
    n += 1
    tag_of[("done",)] = n

    for i, a in enumerate(arms):
        nxt = "arm%d" % (i + 1)
        if a.letters != letter:
            w("")
            if a.note:
                w("# %s" % a.note)
            if i:
                w("place arm%d on %d" % (i, tag_of[("fall", i)]))
            w("  plant test %s as %d" % (nxt, tag_of[("fall", i + 1)]))
            w("  call lpta_loadp addr leftpoint")
            w("  call setscan_r %d" % LETTERS)
            w("  if answer is not 0")
            w("    go to %s" % nxt)
            w("  end")
            run = letters_of(a.letters)
            w("  call test_string_s %d %d sym %s"
              % (LETTERS, len(run),
                 known.name("lts", run, a.letters)))
            w("  if answer is not 0")
            w("    go to %s" % nxt)
            w("  end")
            w("place body%d on %d" % (i, tag_of[("body", i)]))
            w("  call savescptr %d addr rightpoint" % tag_of[("body", i)])
            w("  call lpta_loadp addr onepiece")
            w("  call test_ptr")
            w("  if answer is not 0")
            w("    backtrack")
            w("  end")
            w(spell(a, known, phones_of))
            w("  go to laid")
        else:
            w("")
            if a.note:
                w("# %s" % a.note)
            if i:
                w("place arm%d on %d" % (i, tag_of[("fall", i)]))
            w(spell(a, known, phones_of))

    w("")
    w("place laid")
    w("  if answer is not 0")
    w("    backtrack")
    w("  end")
    w("")
    w("place done on %d" % tag_of[("done",)])
    w("  match")
    w("end")
    return "\n".join(out)


def spell(arm, known, phones_of):
    """The one line that lays an arm's phones over the range it matched."""
    said = phones_of(arm.phones)
    if not said:
        # An arm that says nothing still has to empty the range, which is the
        # same call with nothing to put in it.
        return ("  call lpta_rpta_loadp addr leftpoint addr rightpoint\n"
                "  call empty_2pt 0")
    return ("  call lpta_rpta_loadp addr leftpoint addr rightpoint\n"
            "  call insert_2pt_s %d %d sym %s 0"
            % (PHONES, len(said),
               known.name("say", said, " ".join(arm.phones))))


def compile_tag(tag):
    path = os.path.join(ROOT, "lang", tag, "letters")
    if not os.path.exists(path):
        raise Trouble("there is no lang/%s/letters" % tag)
    known = Strings()
    lcode = letter_codes(tag)
    pcode = phone_codes(tag)
    out = [
        "# %s's letter rules, written by tools/rules/letters.py out of" % tag,
        "# lang/%s/letters, which is the file to edit. Every rule here stands"
        % tag,
        "# in for the one of the same name in et_phone.dr.",
        "#",
        "# An arm sets the scan on the letters, tests what stands there, saves",
        "# where the scan got to, and spells the range between the two points",
        "# as phones. The tags are the machine's dispatch: one to fall to the",
        "# next arm, one a spelling arm so a failure inside it comes back to",
        "# the arm rather than to the letter, and one for having spelled.",
    ]
    for letter, arms in parse(path):
        out.append("")
        out.append(rule_for(tag, letter, arms, known, lcode, pcode))
    return "\n".join(out) + "\n", known.text(tag)


def not_laid_down(tag, strings):
    """The names this minted that rules/symbols has nowhere for.

    A rule that names one of those builds into an index past the end of the
    symbol table, so the emitter stops; this says the same thing earlier and
    names the answer.
    """
    want = [line.split()[1] for line in strings.splitlines()
            if line.startswith("bytes ")]
    have = set()
    where = os.path.join(ROOT, "lang", tag, "rules", "symbols")
    if os.path.exists(where):
        for line in open(where):
            w = line.split()
            if len(w) == 5 and w[0] == "at":
                have.add(w[2])
    return [n for n in want if n not in have]


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip())
        return 2
    what, tag = argv
    try:
        text, strings = compile_tag(tag)
    except Trouble as e:
        print("letters: %s" % e)
        return 1
    rules = os.path.join(ROOT, "lang", tag, "rules", "et_phone.up")
    consts = os.path.join(ROOT, "lang", tag, "rules", "constants.letters")
    if what == "show":
        sys.stdout.write(text)
        sys.stdout.write("\n")
        sys.stdout.write(strings)
        return 0
    if what == "write":
        open(rules, "w").write(text)
        open(consts, "w").write(strings)
        missing = not_laid_down(tag, strings)
        if missing:
            # An ordinary build will stop on the first of these, with a
            # message from the emitter saying the symbol has nowhere
            # recorded. Say it here too, where the answer is: a string is
            # minted by this tool and laid down by tools/rules/consts.py,
            # and `make letters' is the two together.
            print("%s: %d string%s this names %s not laid down yet (%s)."
                  " Run make letters."
                  % (tag, len(missing), "" if len(missing) == 1 else "s",
                     "is" if len(missing) == 1 else "are",
                     ", ".join(missing)))
            return 1
        print("%s: rules/et_phone.up and rules/constants.letters written"
              % tag)
        return 0
    if what == "regenerate":
        ok = True
        for where, want in ((rules, text), (consts, strings)):
            have = open(where).read() if os.path.exists(where) else ""
            if have != want:
                print("%s: %s is not what lang/%s/letters says"
                      % (tag, os.path.basename(where), tag))
                ok = False
        if ok:
            print("%s: rules/et_phone.up and rules/constants.letters are what"
                  " lang/%s/letters says" % (tag, tag))
        return 0 if ok else 1
    print(__doc__.strip())
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
