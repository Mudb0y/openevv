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
nothing else applies. An arm may also say what must stand to its left and right
without swallowing it -- `after', `before', and `vowel', `consonant' or `glide'
in either place -- and whether the run has to begin or end where the piece of
the word does, which is `at start' and `at end'.

The phones are the ETI phone letters, the same ones `lang/<tag>/<tag>.dict'
already uses, and the letters are the language's own characters.

What it writes is `lang/<tag>/rules/et_phone.up', which the build compiles over
the lifted text in the ordinary way, so a rule written here stands where IBM's
compiled one stood and `test/words.sh' says whether any of 24,318 words moved.

The strings an arm needs -- the letters it tests and the phones it lays down --
are minted here and written to `lang/<tag>/rules/constants.letters', because a
symbol belongs to the object its rule came out of and a letter rule of ours
cannot name the ones IBM's name. `tools/rules/consts.py' lays them down, and
`make letters' is the two in that order.

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


# The two ends of the range an arm spells, and the two ends of the piece of the
# word being read -- the run the engine's earlier passes decided is one root or
# one prefix. The rule that walks the word sets the first pair around the
# letter before it calls us, and an arm that swallows more letters moves the
# second of them along.
#
# An arm's condition is a test that the scan has arrived at one end of the
# second pair, which is what `test_ptr' answers, and it is not decoration:
# `b' before `t' says nothing in debt and says /b/ in ob-tain and sub-tract,
# where the b ends a prefix and the t starts a root. Without it those and six
# more come out wrong, which is how the condition was found.
LEFT, RIGHT = 844, 852
PIECE_START, PIECE_END = 876, 884

# Which field is which. A field is a level of the spine -- the letters the scan
# walks, the phones an arm lays down -- and the number each has is the
# language's own business rather than a constant. Every module but Spanish
# declares its phone statement third and Spanish declares it fifth, so an arm
# written with English's numbers would lay Spanish's phones into its words.


def takes(tag, stem, name):
    """How many arguments the rule this one stands in for is handed.

    It decides where the range to spell comes from, and the two families
    differ: English's letter rules take the state alone and read the two ends
    out of variables 106 and 107, and Spanish's are handed them. A rule of
    ours that read the variables where the caller passes the range spells
    nothing at all, which is what the whole of Spanish did until this was
    noticed -- every b and d and f simply gone from the words.
    """
    where = os.path.join(ROOT, "lang", tag, "rules", stem + ".dr")
    want = False
    for line in open(where):
        w = line.split()
        if w[:1] == ["rule"]:
            want = len(w) > 1 and w[1] == name
        elif want and w[:1] == ["shape"]:
            for i, t in enumerate(w):
                if t == "params":
                    return int(w[i + 1])
    raise Trouble("%s has no rule called %s to stand in for"
                  % (os.path.basename(where), name))


def fields(tag):
    """The letters field, the phones field and the one a letter rule fences,
    as this language numbers them, which is the order it declares its
    statements in."""
    at = {}
    for line in open(os.path.join(ROOT, "lang", tag, "%s.statements" % tag)):
        if line.startswith("statement "):
            at.setdefault(line.split()[1], len(at))
    for want in ("inp", "phone", "morph"):
        if want not in at:
            raise Trouble("%s declares no %s statement" % (tag, want))
    return at["inp"], at["phone"], at["morph"]


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

CONDITIONS = ("at start", "at end")

# What a letter may be said to be, rather than which letter it is. These are
# the values of the input statement's `letter_type` field, which is the
# language's own answer and not ours: 1 vow, 2 con, 3 glid.
CLASSES = {"vowel": 1, "consonant": 2, "glide": 3}


class Arm(object):
    def __init__(self, where, letters, after, before, when, phones, note):
        self.where = where
        self.letters = letters      # the whole run, the block's letter first
        self.after = after          # letters that must precede, not swallowed
        self.before = before        # letters that must follow, not swallowed
        self.when = when            # "", "at start" or "at end"
        self.phones = phones        # [] for an arm that says nothing
        self.note = note

    def tests(self):
        """Whether this arm asks anything at all, which decides its shape.

        An arm that asks nothing is the block's last: the letter on its own,
        with no run to match, nothing either side of it and nowhere it has to
        fall. Anything else is tried and may fail, and reading the run alone
        to decide that was a fault worth naming -- an arm of one letter with
        a condition on it came out as a bare arm with the condition dropped,
        silently, and three letters were blamed on the machine for it."""
        return bool(len(self.letters) > 1 or self.after or self.before
                    or self.when)


def parse(path):
    """The file as blocks of arms, in the order they are written."""
    blocks = []
    cur = None
    obj = None
    pattern = None
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
        if w[0] == "rules":
            if len(w) != 5 or w[1] != "in" or w[3] != "named":
                raise Trouble("%s: the header is `rules in <object> named"
                              " <pattern>', the pattern saying how a letter's"
                              " rule is spelled with %%s where the letter is"
                              % where)
            obj, pattern = w[2], w[4]
            if "%s" not in pattern:
                raise Trouble("%s: the pattern says where the letter goes,"
                              " with %%s" % where)
            continue
        if w[0] == "letter":
            if obj is None:
                raise Trouble("%s: say which object these stand in for before"
                              " the first letter" % where)
            if len(w) == 2:
                cur = (w[1], pattern % w[1], obj, [])
            elif len(w) == 4 and w[2] == "as":
                cur = (w[1], pattern % w[3], obj, [])
            else:
                raise Trouble("%s: a block is `letter <character>', with"
                              " `as <name>' where the rule is not spelled"
                              " after the character" % where)
            blocks.append(cur)
            continue
        if cur is None:
            raise Trouble("%s: an arm before any letter block" % where)
        if "says" not in w:
            raise Trouble("%s: an arm is letters, `says', and phones" % where)
        at = w.index("says")
        letters, said = w[0], w[at + 1:]
        rest = w[1:at]
        after = ""
        if rest[:1] == ["after"]:
            if len(rest) < 2:
                raise Trouble("%s: `after' what?" % where)
            after = rest[1]
            rest = rest[2:]
        before = ""
        if rest[:1] == ["before"]:
            if len(rest) < 2:
                raise Trouble("%s: `before' what?" % where)
            before = rest[1]
            rest = rest[2:]
        when = " ".join(rest)
        if when and when not in CONDITIONS:
            raise Trouble("%s: an arm may say %s and nothing else, not %r"
                          % (where, " or ".join("`%s'" % c
                                                for c in CONDITIONS), when))
        if not said:
            raise Trouble("%s: `says' what?" % where)
        phones = [] if said == ["nothing"] else said
        if not letters.startswith(cur[0]):
            raise Trouble("%s: this is the %s block, so an arm starts with %s"
                          % (where, cur[0], cur[0]))
        cur[3].append(Arm(where, letters, after, before, when,
                          phones, note))
    return blocks


# ---- what it compiles to -------------------------------------------------

def rule_for(tag, letter, name, obj, arms, known, lcode, pcode,
             letters_at, phones_at, fence_at, params):
    """One letter's rule, as the upper form.

    The shape is IBM's own and the tags are its numbering: the arm to fall to
    is planted before an arm is tried, the scan pointer is saved under a tag of
    its own so that a failure inside an arm comes back to the arm rather than
    to the letter, and one tag stands for the rule having spelled something.
    """
    if not arms:
        raise Trouble("the %s block has no arms" % letter)
    if arms[-1].tests():
        raise Trouble("the last arm of the %s block is what %s says when"
                      " nothing else applies, so it asks nothing: bare %s"
                      % (letter, letter, letter))
    for a in arms[:-1]:
        if not a.tests():
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
    w("rule %s takes %d from %s" % (name, params, obj))
    w("  through wrappers")
    w("  afresh")
    if params == 1:
        w("  variable leftpoint word %d" % LEFT)
        w("  variable rightpoint word %d" % RIGHT)
    w("  variable piecestart word %d" % PIECE_START)
    w("  variable pieceend word %d" % PIECE_END)
    # Where the two ends of the range live, which is the whole of the
    # difference between the two families of letter rule.
    left = "addr leftpoint" if params == 1 else "arg 1"
    right = "addr rightpoint" if params == 1 else "arg 2"
    w("")
    # The fence is one statement type, and the wrapper IBM's own rule calls
    # is named for a string of that language's own. Ours is minted instead,
    # since a wrapper named for English's string does not exist in Spanish's
    # module and the build says so rather than guessing.
    w("  call fence 1 sym %s"
      % known.name("fence", bytes([fence_at]), "the morph statement"))

    # The tags. One a spelling arm, one for each arm that can be fallen to,
    # and the last for the rule having spelled something.
    tag_of = {}
    n = 0
    for i, a in enumerate(arms):
        if i:
            n += 1
            tag_of[("fall", i)] = n
        if a.tests():
            n += 1
            tag_of[("body", i)] = n
    n += 1
    tag_of[("done",)] = n

    for i, a in enumerate(arms):
        nxt = "arm%d" % (i + 1)
        if a.tests():
            w("")
            if a.note:
                w("# %s" % a.note)
            if i:
                w("place arm%d on %d" % (i, tag_of[("fall", i)]))
            w("  plant test %s as %d" % (nxt, tag_of[("fall", i + 1)]))
            if a.after:
                # What stands to the left. The two ends of the range the rule
                # was given sit outside the letter, so a scan set on the left
                # one and told to read leftwards meets the letter before this
                # one first -- the mirror of the rightward scan below, which
                # is set on the same end and meets this letter first.
                w("  call lpta_loadp %s" % left)
                w("  call setscan_l %d" % letters_at)
                w("  if answer is not 0")
                w("    go to %s" % nxt)
                w("  end")
                if a.after in CLASSES:
                    w("  call testFldeq %d 4 %d" % (letters_at, CLASSES[a.after]))
                else:
                    ctx = letters_of(a.after)
                    w("  call test_string_s %d %d sym %s"
                      % (letters_at, len(ctx),
                         known.name("lts", ctx, a.after)))
                w("  if answer is not 0")
                w("    go to %s" % nxt)
                w("  end")
            if a.when == "at start":
                # The run has to begin where the piece does. Put the scan on
                # the letter walking left and ask whether it is already at the
                # piece's first node.
                w("  call lpta_loadp %s" % left)
                w("  call setscan_l %d" % letters_at)
                w("  if answer is not 0")
                w("    go to %s" % nxt)
                w("  end")
                w("  call lpta_loadp addr piecestart")
                w("  call test_ptr")
                w("  if answer is not 0")
                w("    go to %s" % nxt)
                w("  end")
            if len(a.letters) > 1 or a.before:
                # From the left end where there is a run to match, since the
                # scan meets this letter first there; from the right end
                # where the arm only looks at what follows, since then the
                # first thing the scan should meet is the letter after this
                # one. Spanish's s does the second -- `s before m says z' is
                # abismo -- and reading from the left there tests the s
                # against an m and never matches.
                w("  call lpta_loadp %s"
                  % (left if len(a.letters) > 1 else right))
                w("  call setscan_r %d" % letters_at)
                w("  if answer is not 0")
                w("    go to %s" % nxt)
                w("  end")
            if len(a.letters) > 1:
                run = letters_of(a.letters)
                w("  call test_string_s %d %d sym %s"
                  % (letters_at, len(run),
                     known.name("lts", run, a.letters)))
                w("  if answer is not 0")
                w("    go to %s" % nxt)
                w("  end")
            w("place body%d on %d" % (i, tag_of[("body", i)]))
            # Where the scan got to is the end of the range to spell -- but
            # only where the scan was set rightwards and moved. An arm whose
            # run is the letter alone has read nothing to the right, so the
            # range is the one the caller handed over and saving here would
            # store whatever the left-context scan was left pointing at. That
            # is what hung the engine on `languorous': `r after ou' matched,
            # saved a leftward scan as the right end of the range, and the
            # rule read the same letter for ever after.
            if len(a.letters) > 1 or a.before:
                w("  call savescptr %d %s"
                  % (tag_of[("body", i)], right))
            if a.before in CLASSES:
                # What follows has to be of a kind rather than a letter, which
                # is how a rule says `a vowel' without naming twenty of them.
                w("  call testFldeq %d 4 %d" % (letters_at, CLASSES[a.before]))
                w("  if answer is not 0")
                w("    backtrack")
                w("  end")
            elif a.before:
                # Letters that have to follow and are not swallowed. The scan
                # is where the run ended and the range to spell is already
                # saved, so this only reads on.
                ctx = letters_of(a.before)
                w("  call test_string_s %d %d sym %s"
                  % (letters_at, len(ctx),
                     known.name("lts", ctx, a.before)))
                w("  if answer is not 0")
                w("    backtrack")
                w("  end")
            if a.when == "at end":
                # And the run has to finish where the piece does. The scan is
                # past the last letter it matched, so this asks whether that
                # is the piece's last node.
                w("  call lpta_loadp addr pieceend")
                w("  call test_ptr")
                w("  if answer is not 0")
                w("    backtrack")
                w("  end")
            w(spell(a, known, phones_of, phones_at, left, right))
            w("  go to laid")
        else:
            w("")
            if a.note:
                w("# %s" % a.note)
            if i:
                w("place arm%d on %d" % (i, tag_of[("fall", i)]))
            w(spell(a, known, phones_of, phones_at, left, right))

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


def spell(arm, known, phones_of, phones_at, left, right):
    """The one line that lays an arm's phones over the range it matched."""
    said = phones_of(arm.phones)
    if not said:
        # An arm that says nothing empties the phones over the range it
        # matched rather than filling them. That is how Spanish makes its h
        # silent -- `apply_span_h_rules' loads the two points and calls
        # delete_2pt on the phone field, and there is no insertion in the
        # whole rule.
        #
        # It was written down here as impossible once, because it hung the
        # engine. The deletion was not the reason: the arm that tried it was
        # a bare run with only a left context, and such an arm was saving a
        # leftward scan as the right end of its range, so the range it
        # emptied was nonsense. Reading another language's rules is what
        # settled it.
        raise Trouble(
            "%s: `says nothing' is not ready. Emptying the phones over the"
            " range is what a silent letter is -- Spanish's h does exactly"
            " that -- and an arm of ours that does only it answers correctly"
            " and leaves something behind every time it fires, so the engine"
            " slows to a crawl: Spanish went from thirteen milliseconds a"
            " word to over a second. IBM's rule does more than the one call"
            " and which part the engine needs has not been read yet."
            " Swallow the letter with the one beside it instead, the way"
            " `bt says t' does." % arm.where)
    return ("  call lpta_rpta_loadp %s %s\n"
            "  call insert_2pt_s %d %d sym %s 0"
            % (left, right, phones_at, len(said),
               known.name("say", said, " ".join(arm.phones))))


def compile_tag(tag):
    path = os.path.join(ROOT, "lang", tag, "letters")
    if not os.path.exists(path):
        raise Trouble("there is no lang/%s/letters" % tag)
    known = Strings()
    letters_at, phones_at, fence_at = fields(tag)
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
    stem = None
    for letter, name, obj, arms in parse(path):
        stem = obj[:-4] if obj.endswith(".obj") else obj
        out.append("")
        out.append(rule_for(tag, letter, name, obj, arms, known, lcode,
                            pcode, letters_at, phones_at, fence_at,
                            takes(tag, stem, name)))
    if stem is None:
        raise Trouble("lang/%s/letters names no letter" % tag)
    return "\n".join(out) + "\n", known.text(tag), stem


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
        text, strings, stem = compile_tag(tag)
    except Trouble as e:
        print("letters: %s" % e)
        return 1
    rules = os.path.join(ROOT, "lang", tag, "rules", stem + ".up")
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
            # Said rather than refused: `make letters' is this tool and
            # tools/rules/consts.py in that order, so failing here would stop
            # the run that was about to lay them down. A build that goes on
            # to compile a rule naming one of these stops by itself, with the
            # emitter saying the symbol has nowhere recorded.
            print("%s: %d string%s this names %s not laid down yet (%s)."
                  " Run make letters."
                  % (tag, len(missing), "" if len(missing) == 1 else "s",
                     "is" if len(missing) == 1 else "are",
                     ", ".join(missing)))
        print("%s: rules/%s.up and rules/constants.letters written"
              % (tag, stem))
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
            print("%s: rules/%s.up and rules/constants.letters are what"
                  " lang/%s/letters says" % (tag, stem, tag))
        return 0 if ok else 1
    print(__doc__.strip())
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
