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
the word does, which is `at start' and `at end'. And an arm may end by marking
the phones it laid down, `marked <field> <value>', in the phone statement's own
names: Italian's doubled consonants are `geminate yes'. Last of all it may say
which words it is for, `where the root begins <list>', `where the root is
<list>' or `where the root so far is <list>', naming one of the language's own
lookup sets: Italian's s is unvoiced between vowels where the root begins
VsV_pronounced_s, and its gi keeps the i where the root so far, up to the end
of the gi, is one of i_pronounced_i.
`the word' in place of `the root' matches from the word's own start, which
wants the line `a word runs from <start> to <end>'.

A silent letter `says nothing', and an arm that `is left alone' matches and
does nothing at all, leaving the letter to whatever the machine does with one
nobody spelled; Italian's h is the first everywhere but where it begins the
root, and the second there. `before' may join letters and kinds with `+' --
`tt+vowel' -- and `accented' asks for the letter's own accent flag.

After the phones, an arm may say what must not hold: `unless before <letters>',
with `at end' if it must end the root, `unless the root is <list>', `unless
the root begins <list>' and `unless the root so far is <list>', as many as it
needs. That is IBM's `not', a test that abandons the arm when it succeeds.
`where the word passes <rule>' asks one of the language's own rules of the
whole word -- Italian's o is open in a word one_ital_syllable passes -- and
`hands over to <rule>' in place of `says' gives the letter to one, as a
homograph is given to the rule that knows its part of speech.

The phones are the ETI phone letters, the same ones `lang/<tag>/<tag>.dict'
already uses, and the letters are the language's own characters.

Three lines may stand above the letters. `voiceless means p t k f s c h ś ć'
names a class, which an arm asks for as it would a kind -- `before voiceless'
-- and `and the end of the word' ending the line counts nothing at all
following, or anything that is not a letter, as one of the class: Polish
devoices a consonant before a voiceless one and at the end of a word, and
that is one condition. `unless voiced follows' after that takes the end of
the word back out of the class when the next word begins with one of the
class `voiced', and `before voiced word' on an arm asks that of the letter
itself: pod domem keeps its d, and jak dobrze is jag dobrze. A class that
does not count the end of the word may carry an `unless' as well, naming
one that does: `voiced means b d g z ź ż unless voiceless follows' counts a
voiced letter only where it keeps its voice, so liczba voices its cz and
liczb does not.
`ą ć ę ł ń ś ź ż come through q' says those letters
have no link of their own in the dispatcher and reach their rules through
q's, so their blocks are compiled as arms of one rule standing where q's
rule stood, with a test beside it for the dispatcher to ask. And
`phone sz is L' lets an arm name a phone by a spelling of its own where the
phone statement's name would mislead: Polish's hard sz took over Italian's
L, and `sz says L' reads as something it is not.

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
setsmod = sibling("module/sets")


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
# Where the two ends of the piece live is the language's own business and
# nothing in a rule says which is which, so a letters file says: English holds
# them at 876 and 884, Spanish at 704 and 872.

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
    called = None
    for line in open(where, encoding="utf-8"):
        w = line.split()
        if w[:1] == ["rule"]:
            want = len(w) > 1 and w[1] == name
        elif want and w[:1] == ["shape"]:
            for i, t in enumerate(w):
                if t == "params":
                    return int(w[i + 1])
        elif w[:2] == ["call", name] and len(w) > 3 and w[2] == "arity":
            called = int(w[3])
    # A rule of the language's own has no lifted body to read that from, and
    # the dispatcher that calls it says the same thing: its arity counts the
    # state, as a rule's params do.
    if called is not None:
        return called
    raise Trouble("%s has no rule called %s to stand in for, and nothing"
                  " calls one" % (os.path.basename(where), name))


def fields(tag):
    """The letters field, the phones field and the one a letter rule fences,
    as this language numbers them, which is the order it declares its
    statements in."""
    at = {}
    for line in open(os.path.join(ROOT, "lang", tag, "%s.statements" % tag), encoding="utf-8"):
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
    codes = dict((name, n) for n, name in enumerate(names))
    # A letter of the language's own is named in the alphabet by the byte it
    # was given, which read as Latin-1 is no letter at all: Polish's ś is 0x88.
    # The codepoints file says which character each of those bytes is, so the
    # file can be written in the letters themselves.
    where = os.path.join(ROOT, "lang", tag, "%s.codepoints" % tag)
    if os.path.exists(where):
        for line in open(where, encoding="utf-8"):
            w = line.split("#", 1)[0].split()
            if len(w) == 2:
                byte = bytes([int(w[1], 16)]).decode("latin-1")
                if byte in codes:
                    codes.setdefault(chr(int(w[0], 16)), codes[byte])
    return codes


def phone_fields(tag):
    """Every field of the phone statement, as its number and the names of its
    values in order, which is what a mark is written in terms of."""
    out = {}
    field = None
    inside = False
    for line in open(os.path.join(ROOT, "lang", tag, "%s.statements" % tag), encoding="utf-8"):
        if line.startswith("statement "):
            if inside:
                break
            inside = line.split()[1] == "phone"
            continue
        if not inside:
            continue
        if line.startswith("  field "):
            field = line.split()[1]
            out[field] = (len(out), [])
        elif line.startswith("    value ") and field is not None:
            out[field][1].append(line.split(None, 1)[1].strip())
    return out


def word_lists(tag):
    """The language's own lookup sets, by name, as the letter strings each
    holds.

    These are the lists IBM's letter rules consult: Italian's are named for
    what they decide, `VsV_pronounced_s' and `sci_pronounced_sci' among them,
    and hold the beginnings of the roots they decide it for. The text form
    lays a set's descriptor out with its count at 0x0c and its size in bytes
    at 0x10, which is not where the runtime reads them; the store holds the
    entries as the language's own letter codes, each ended by a nought."""
    import struct
    where = setsmod.text_path(tag)
    if not os.path.exists(where):
        return {}
    m = setsmod.read_text(where)
    out = {}
    for n, name, off in m["sets"]["at"]:
        desc = m["set_table"][n * 0x24:(n + 1) * 0x24]
        size = struct.unpack_from("<i", desc, 0x10)[0]
        words = [w for w in m["sets"]["store"][off:off + size].split(b"\0")
                 if w]
        short = name[:-len("_setentries")] if name.endswith("_setentries") \
            else name
        out[short] = words
    return out


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

CONDITIONS = ("at start", "at end", "at word start", "at word end")

# What a letter may be said to be, rather than which letter it is. These are
# the values of the input statement's `letter_type` field, which is the
# language's own answer and not ours: 1 vow, 2 con, 3 glid.
CLASSES = {"vowel": 1, "consonant": 2, "glide": 3}


class Arm(object):
    def __init__(self, where, letters, after, before, when, phones, note,
                 marked=None, listed=None):
        self.where = where
        self.letters = letters      # the whole run, the block's letter first
        self.after = after          # letters that must precede, not swallowed
        self.before = before        # letters that must follow, not swallowed
        self.when = when            # "", "at start" or "at end"
        self.phones = phones        # [] for an arm that says nothing
        self.note = note
        self.marked = marked        # (field, value) set on the phones laid
        self.listed = listed        # ("begins" or "is", list name)

    def tests(self, letter=None):
        """Whether this arm asks anything at all, which decides its shape.

        An arm that asks nothing is the block's last: the letter on its own,
        with no run to match, nothing either side of it and nowhere it has to
        fall. Anything else is tried and may fail, and reading the run alone
        to decide that was a fault worth naming -- an arm of one letter with
        a condition on it came out as a bare arm with the condition dropped,
        silently, and three letters were blamed on the machine for it."""
        return bool(len(self.letters) > 1 or self.after or self.before
                    or self.when or self.listed
                    or getattr(self, "accented", False)
                    or (letter is not None and self.letters[0] != letter))


class Header(object):
    """What the lines above the letters say besides where things live."""

    def __init__(self):
        self.classes = {}       # name -> (letters, whether the end counts)
        self.through = None     # (letters, the letter whose link they use)
        self.aliases = {}       # a spelling of ours -> the phone's own name
        self.obj = None
        self.pattern = None


def parse(path):
    """The file as blocks of arms, in the order they are written, and what
    the lines above them say."""
    blocks = []
    header = Header()
    wordrange = None
    cur = None
    obj = None
    pattern = None
    piece = None
    for n, raw in enumerate(open(path, encoding="utf-8"), 1):
        where = "%s line %d" % (os.path.basename(path), n)
        note = ""
        if "#" in raw:
            raw, note = raw.split("#", 1)
            note = note.strip()
        line = raw.strip()
        if not line:
            continue
        w = line.split()
        if w[:2] == ["a", "piece"]:
            # Where this language keeps the two ends of the piece being read,
            # which an `at start' or `at end' arm tests against. They are the
            # language's own: English holds them at 876 and 884 and Spanish
            # at 704 and 872, and nothing in a rule says which is which, so
            # the file says.
            if len(w) != 7 or w[2:4] != ["runs", "from"] or w[5] != "to":
                raise Trouble("%s: the line is `a piece runs from <start> to"
                              " <end>'" % where)
            piece = (int(w[4]), int(w[6]))
            continue
        if w[:2] == ["a", "word"]:
            # Where the whole word's two ends live, which a list is matched
            # from when it holds whole words or words with their prefixes:
            # Italian's z_pronounced_D has inzupp, whose root is zupp.
            if len(w) != 7 or w[2:4] != ["runs", "from"] or w[5] != "to":
                raise Trouble("%s: the line is `a word runs from <start> to"
                              " <end>'" % where)
            wordrange = (int(w[4]), int(w[6]))
            continue
        if len(w) > 2 and w[1] == "means" and cur is None:
            members = w[2:]
            unless = None
            if len(members) > 3 and members[-3] == "unless" \
                    and members[-1] == "follows":
                unless = members[-2]
                members = members[:-3]
            end = members[-6:] == ["and", "the", "end", "of", "the", "word"]
            if end:
                members = members[:-6]
            if not members or w[0] in CLASSES:
                raise Trouble("%s: a class is `<name> means <letters>', with"
                              " `and the end of the word' if nothing"
                              " following counts, `unless <class> follows'"
                              " after that if a word beginning with one of"
                              " that class does not, and its name is not one"
                              " of %s" % (where, ", ".join(CLASSES)))
            header.classes[w[0]] = (members, end, unless)
            continue
        if w[-3:-1] == ["come", "through"] and len(w) > 3 and cur is None:
            header.through = (w[:-3], w[-1])
            continue
        if w[0] == "phone" and len(w) == 4 and w[2] == "is" and cur is None:
            header.aliases[w[1]] = w[3]
            continue
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
            header.obj, header.pattern = obj, pattern
            continue
        if w[0] == "letter":
            if obj is None:
                raise Trouble("%s: say which object these stand in for before"
                              " the first letter" % where)
            if len(w) == 2:
                cur = (w[1], pattern % w[1], obj, [], (piece, wordrange))
            elif len(w) == 4 and w[2] == "as":
                cur = (w[1], pattern % w[3], obj, [], (piece, wordrange))
            else:
                raise Trouble("%s: a block is `letter <character>', with"
                              " `as <name>' where the rule is not spelled"
                              " after the character" % where)
            blocks.append(cur)
            continue
        if cur is None:
            raise Trouble("%s: an arm before any letter block" % where)
        # `is left alone' is the arm that matches and does nothing at all,
        # leaving the letter to whatever the machine does with one nobody
        # spelled. IBM's Italian h does that where it begins the root, and
        # silences it everywhere else, and the two sound different.
        alone = w[-3:] == ["is", "left", "alone"]
        if alone:
            w = w[:-3] + ["says", "alone"]
        # `hands over to <rule>' gives the letter to another of the
        # language's rules, as IBM's e does with a homograph: which e pesca
        # has is the part of speech's business, not the letters'.
        if "hands" in w and w[w.index("hands"):w.index("hands") + 3] == \
                ["hands", "over", "to"]:
            h = w.index("hands")
            w = w[:h] + ["says", "handed", w[h + 3]] + w[h + 4:]
        if "says" not in w:
            raise Trouble("%s: an arm is letters, `says', and phones, or"
                          " letters and `is left alone'" % where)
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
        before_word = False
        if rest[:1] == ["before"]:
            if len(rest) < 2:
                raise Trouble("%s: `before' what?" % where)
            before = rest[1]
            rest = rest[2:]
            # `before voiced word': the letter ends its word and the next
            # word begins with one of the class, which is how a consonant
            # takes its voice from the word after it.
            if rest[:1] == ["word"]:
                before_word = True
                rest = rest[1:]
        # `accented' is the letter's own accent flag, which is set for an
        # accented letter and for one the dictionary has marked stressed:
        # Italian's i says i in entropia, whose i is written plain.
        accented = False
        if rest[:1] == ["accented"]:
            accented = True
            rest = rest[1:]
        when = " ".join(rest)
        if when and when not in CONDITIONS:
            raise Trouble("%s: an arm may say %s and nothing else, not %r"
                          % (where, " or ".join("`%s'" % c
                                                for c in CONDITIONS), when))
        if not said:
            raise Trouble("%s: `says' what?" % where)
        # What must not hold, which is how IBM's rule says `not': a boa
        # choice around a test that, when it succeeds, abandons the arm.
        # `unless before <letters>' and `unless the root is <list>', as many
        # as the arm needs, last of all.
        unless = []
        while "unless" in said:
            u = len(said) - 1 - said[::-1].index("unless")
            tail = said[u + 1:]
            said = said[:u]
            if tail[:1] == ["before"] and len(tail) == 2:
                unless.insert(0, ("before", tail[1]))
            elif tail[:1] == ["before"] and len(tail) in (4, 5) and \
                    " ".join(tail[2:]) in ("at end", "at word end"):
                unless.insert(0, ("before", tail[1], " ".join(tail[2:])))
            elif len(tail) == 4 and tail[:2] == ["the", "root"] and \
                    tail[2] in ("is", "begins"):
                unless.insert(0, ("root " + tail[2], tail[3]))
            elif len(tail) == 6 and tail[:5] == ["the", "root", "so", "far",
                                                 "is"]:
                unless.insert(0, ("root so far", tail[5]))
            else:
                raise Trouble("%s: `unless before <letters>' or `unless the"
                              " root is <list>'" % where)
        # Which words the arm is for, where that is a list rather than a
        # spelling. Italian keeps an s unvoiced between vowels in asepsi and
        # dinosauro and voices it in casa, and nothing in the letters says
        # which: IBM's rule asks a list of root beginnings.
        listed = None
        if "where" in said:
            m = said.index("where")
            tail = said[m:]
            if len(tail) == 5 and tail[1:4] == ["the", "word", "passes"]:
                # Another of the language's rules asked of the whole word,
                # which is how IBM's o knows a word of one syllable: it calls
                # one_ital_syllable with the word's two ends.
                listed = ("passes", tail[4], "word")
            elif len(tail) == 7 and tail[1] == "the" \
                    and tail[2] in ("root", "word") \
                    and tail[3:6] == ["so", "far", "is"]:
                listed = ("so far", tail[6], tail[2])
            elif len(tail) == 5 and tail[1] == "the" \
                    and tail[2] in ("root", "word") \
                    and tail[3] in ("begins", "is"):
                listed = (tail[3], tail[4], tail[2])
            else:
                raise Trouble("%s: the clause is `where the root begins"
                              " <list>', `where the root is <list>' or"
                              " `where the root so far is <list>', or the"
                              " same of the word, and ends the arm" % where)
            said = said[:m]
        # What the phones are marked with once they are down. Italian's
        # doubled consonants are two phones and one long sound, and IBM says
        # the second half by setting the phone statement's `geminate' on both.
        marked = None
        if "marked" in said:
            m = said.index("marked")
            if len(said) != m + 3:
                raise Trouble("%s: `marked' is a field of the phone statement"
                              " and one of its values, and ends the arm"
                              % where)
            marked = tuple(said[m + 1:])
            said = said[:m]
            if not said:
                raise Trouble("%s: `says' what?" % where)
        if said[:1] == ["handed"]:
            phones = ("handed", said[1])
        else:
            phones = [] if said == ["nothing"] else None \
                if said == ["alone"] else said
        # An arm may begin with a letter the block is not named after. The
        # dispatcher hands one rule several characters -- Spanish's i rule is
        # entered for `i' and for `í' alike -- and IBM's own arms tell them
        # apart by asking what the character is, which is what such an arm
        # compiles to.
        arm = Arm(where, letters, after, before, when, phones, note, marked,
                  listed)
        arm.accented = accented
        arm.unless = unless
        arm.before_word = before_word
        cur[3].append(arm)
    return blocks, header


# ---- what it compiles to -------------------------------------------------

def follows_name(tag, cls):
    return "letters_%s_%s_follows" % (tag, cls)


def word_follows_name(tag, cls):
    return "letters_%s_%s_word_follows" % (tag, cls)


def word_follows_rule(tag, cls, members, known, lcode, letters_at, fence_at,
                      obj, step=False):
    """Whether the range's right end is the end of its word and the next
    word begins with one of a class: the scan meets a space and then that
    letter. Anything else there -- a comma, a full stop, another letter of
    the same word -- is not it.

    A letter rule fences its scan at the edge of the morph, which is also
    the edge of the word, so the fence is taken down to look and put back
    as the letter rule had it. And the space is asked for by its field
    rather than as a string: the string tests advance past what they match,
    and the step past a space is the one the fence refuses.
    """
    if " " not in lcode:
        raise Trouble("%s has no space in its alphabet" % tag)
    out = ["rule %s takes 2 from %s"
           % (word_follows_name(tag, cls) + ("_after" if step else ""), obj),
           "  bare",
           "  afresh",
           "  call ZZfence_null",
           "  call lpta_loadp arg 1",
           "  call setscan_nof_r %d" % letters_at,
           "  if answer is not 0",
           "    go to other",
           "  end"]
    if step:
        out += ["  call advance_tok",
                "  if answer is not 0",
                "    go to other",
                "  end"]
    out += ["  call testFldeq %d 0 %d" % (letters_at, lcode[" "]),
           "  if answer is not 0",
           "    go to other",
           "  end",
           "  call advance_tok",
           "  if answer is not 0",
           "    go to other",
           "  end"]
    for m in members:
        if len(m) != 1 or m not in lcode:
            raise Trouble("%s: a class a word begins with is of single"
                          " letters, not %s" % (cls, m))
        out += ["  call test_string_s %d 1 sym %s"
                % (letters_at, known.name("lts", bytes([lcode[m]]), m)),
                "  if answer is 0",
                "    go to one",
                "  end"]
    fence = "  call fence 1 sym %s" % known.name(
        "fence", bytes([fence_at]), "the morph statement")
    out += ["place other", fence, "  answer 1",
            "place one", fence, "  answer 0", "end"]
    return "\n".join(out)


def rule_for(tag, letter, name, obj, arms, known, lcode, pcode,
             letters_at, phones_at, fence_at, params, piece, pfields, lists,
             wordrange=None, header=None, through=False):
    """One letter's rule, as the upper form.

    The shape is IBM's own and the tags are its numbering: the arm to fall to
    is planted before an arm is tried, the scan pointer is saved under a tag of
    its own so that a failure inside an arm comes back to the arm rather than
    to the letter, and one tag stands for the rule having spelled something.

    A rule the `come through' letters share has no bare arm, since every arm
    is some other letter's: it asks each in turn and gives up when none is
    the letter it was handed.
    """
    header = header or Header()
    classes = header.classes
    if not arms:
        raise Trouble("the %s block has no arms" % letter)
    if through:
        for a in arms:
            if not a.tests(letter):
                raise Trouble("%s: an arm of a letter that comes through %s"
                              " asks nothing, so nothing after it could be"
                              " reached" % (a.where, letter))
    else:
        if arms[-1].tests(letter):
            raise Trouble("the last arm of the %s block is what %s says when"
                          " nothing else applies, so it asks nothing: bare %s"
                          % (letter, letter, letter))
        for a in arms[:-1]:
            if not a.tests(letter):
                raise Trouble("%s: a bare %s matches everything, so nothing"
                              " after it can be reached" % (a.where, letter))

    def letters_of(run):
        try:
            return bytes(lcode[c] for c in run)
        except KeyError as e:
            raise Trouble("%s has no character %s" % (tag, e))

    def phones_of(said):
        try:
            return bytes(pcode[header.aliases.get(p, p)] for p in said)
        except KeyError as e:
            raise Trouble("%s has no phoneme %s" % (tag, e))

    def ends(before, word=False):
        """Whether an arm's `before' is a class that nothing following
        counts as one of, or the first letter of the next word, either of
        which is asked of the range's right end by a rule of its own rather
        than read where the scan stands."""
        if word:
            if before not in classes or classes[before][1]:
                raise Trouble("`before %s word' wants a class of letters"
                              % before)
            return True
        items = before.split("+")
        hit = [k for k in items
               if k in classes and (classes[k][1] or classes[k][2])]
        if hit and len(items) > 1:
            raise Trouble("`before %s': a class the end of the word counts in,"
                          " or one with an `unless', stands alone" % before)
        return bool(hit)

    out = []
    w = out.append
    w("rule %s takes %d from %s" % (name, params, obj))
    w("  through wrappers")
    w("  afresh")
    if params == 1:
        w("  variable leftpoint word %d" % LEFT)
        w("  variable rightpoint word %d" % RIGHT)
    if piece is not None:
        w("  variable piecestart word %d" % piece[0])
        w("  variable pieceend word %d" % piece[1])
    if any((a.listed and a.listed[2] == "word")
           or (isinstance(a.when, str) and "word" in a.when) for a in arms):
        if wordrange is None:
            raise Trouble("a list matched against the word needs the line"
                          " `a word runs from <start> to <end>'")
        w("  variable wordstart word %d" % wordrange[0])
        w("  variable wordend word %d" % wordrange[1])
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
        if a.tests(letter):
            n += 1
            tag_of[("body", i)] = n
    if through:
        n += 1
        tag_of[("fall", len(arms))] = n
    n += 1
    tag_of[("done",)] = n

    for i, a in enumerate(arms):
        nxt = "arm%d" % (i + 1)
        if a.tests(letter):
            w("")
            if a.note:
                w("# %s" % a.note)
            if i:
                w("place arm%d on %d" % (i, tag_of[("fall", i)]))
            w("  plant test %s as %d" % (nxt, tag_of[("fall", i + 1)]))
            if a.listed and a.listed[0] == "passes":
                w("  call %s addr wordstart addr wordend" % a.listed[1])
                w("  if answer is not 0")
                w("    go to %s" % nxt)
                w("  end")
            if a.listed and a.listed[0] not in ("so far", "passes"):
                how, which, over = a.listed
                begin = "wordstart" if over == "word" else "piecestart"
                finish = "wordend" if over == "word" else "pieceend"
                if piece is None:
                    raise Trouble("%s: a list is matched against the root,"
                                  " which needs the line `a piece runs from"
                                  " <start> to <end>'" % a.where)
                if which not in lists:
                    raise Trouble("%s: %s has no list called %s"
                                  % (a.where, tag, which))
                # First, before the arm reads anything, since this moves the
                # scan and everything after it sets the scan afresh. One test
                # an entry, from the root's own first letter: the root begins
                # with the entry, or for `is', is it and ends there. That is
                # what IBM's rule asks by handing a growing stretch from the
                # root's start to setd_lookup, without a loop to write.
                spelled_as = dict((c, ch) for ch, c in lcode.items())
                for k, entry in enumerate(lists[which]):
                    miss = "list%d_%d" % (i, k + 1)
                    w("  call lpta_loadp addr %s" % begin)
                    w("  call setscan_r %d" % letters_at)
                    w("  if answer is not 0")
                    w("    go to %s" % miss)
                    w("  end")
                    w("  call test_string_s %d %d sym %s"
                      % (letters_at, len(entry),
                         known.name("lts", entry, "".join(
                             spelled_as.get(c, "?") for c in entry))))
                    w("  if answer is not 0")
                    w("    go to %s" % miss)
                    w("  end")
                    if how == "is":
                        w("  call lpta_loadp addr %s" % finish)
                        w("  call test_ptr")
                        w("  if answer is not 0")
                        w("    go to %s" % miss)
                        w("  end")
                    w("  go to listed%d" % i)
                    w("place %s" % miss)
                w("  go to %s" % nxt)
                w("place listed%d" % i)
            if a.after and a.after in classes:
                # Any one of a class to the left, each member read afresh
                # from the left end, since a member may be two letters and a
                # test of two that matches only the first has moved the scan.
                # A member is written as it is spelled and read nearest first,
                # so it is reversed: sz before a w is z and then s.
                found = "after%d" % i
                for m in classes[a.after][0]:
                    ctx = letters_of(m[::-1])
                    w("  call lpta_loadp %s" % left)
                    w("  call setscan_l %d" % letters_at)
                    w("  if answer is not 0")
                    w("    go to %s" % nxt)
                    w("  end")
                    w("  call test_string_s %d %d sym %s"
                      % (letters_at, len(ctx), known.name("lts", ctx, m)))
                    w("  if answer is 0")
                    w("    go to %s" % found)
                    w("  end")
                w("  go to %s" % nxt)
                w("place %s" % found)
            elif a.after:
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
            if a.when and "word" not in a.when and piece is None:
                raise Trouble("%s: `%s' needs the line `a piece runs from"
                              " <start> to <end>' at the top of the file,"
                              " since where those live is the language's own"
                              % (a.where, a.when))
            if a.when in ("at start", "at word start"):
                # The run has to begin where the piece does. Put the scan on
                # the letter walking left and ask whether it is already at the
                # piece's first node.
                w("  call lpta_loadp %s" % left)
                w("  call setscan_l %d" % letters_at)
                w("  if answer is not 0")
                w("    go to %s" % nxt)
                w("  end")
                w("  call lpta_loadp addr %s" % ("wordstart" if "word" in a.when
                                                  else "piecestart"))
                w("  call test_ptr")
                w("  if answer is not 0")
                w("    go to %s" % nxt)
                w("  end")
            if getattr(a, "accented", False):
                # The accent flag of the letter itself, field 5 of the input
                # statement, whose first value is yes.
                w("  call lpta_loadp %s" % left)
                w("  call setscan_r %d" % letters_at)
                w("  if answer is not 0")
                w("    go to %s" % nxt)
                w("  end")
                w("  call testFldeq %d 5 0" % letters_at)
                w("  if answer is not 0")
                w("    go to %s" % nxt)
                w("  end")
            if a.letters[0] != letter:
                # Which character this is, rather than what stands beside it.
                # testFldeq reads at the scan, so the scan has to be put on
                # this letter first -- from the left end of the range, which
                # is where it meets this one.
                w("  call lpta_loadp %s" % left)
                w("  call setscan_r %d" % letters_at)
                w("  if answer is not 0")
                w("    go to %s" % nxt)
                w("  end")
                w("  call testFldeq %d 0 %d"
                  % (letters_at, lcode[a.letters[0]]))
                w("  if answer is not 0")
                w("    go to %s" % nxt)
                w("  end")
            if len(a.letters) > 1 or (a.before and not ends(
                    a.before, getattr(a, "before_word", False))):
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
            #
            # Nor where it only looked at what follows. The scan set on the
            # right end and saved back into it looks like a no-op and is not:
            # what goes back is where the scan stands rather than the token
            # the caller handed over, and a phone laid over that range leaves
            # the letter's phon_form unset. Nothing heard shows it until a
            # later letter asks what this one was said as -- Italian's i does,
            # of the u before it -- and IBM's u saves nothing there.
            if len(a.letters) > 1:
                w("  call savescptr %d %s"
                  % (tag_of[("body", i)], right))
            if a.before and getattr(a, "before_word", False):
                w("  call %s %s" % (word_follows_name(tag, a.before), right))
                w("  if answer is not 0")
                w("    backtrack")
                w("  end")
            elif a.before:
                # What has to follow and is not swallowed: letters, a kind of
                # letter -- `vowel', `consonant' -- or a run of them joined by
                # `+', which is how IBM's e asks for -etta at the end of a word:
                # `tt+vowel'. A kind is read where the scan stands, so it is
                # stepped past when anything is still to be asked after it.
                items = a.before.split("+")
                later = a.when in ("at end", "at word end")
                for n, k in enumerate(items):
                    if k in CLASSES:
                        w("  call testFldeq %d 4 %d" % (letters_at, CLASSES[k]))
                        w("  if answer is not 0")
                        w("    backtrack")
                        w("  end")
                        if n < len(items) - 1 or later:
                            w("  call advance_tok")
                            w("  if answer is not 0")
                            w("    backtrack")
                            w("  end")
                    elif k in classes and (classes[k][1] or classes[k][2]):
                        if later:
                            raise Trouble("%s: a class the end of the word"
                                          " counts in says where the word"
                                          " ends already" % a.where)
                        w("  call %s %s" % (follows_name(tag, k), right))
                        w("  if answer is not 0")
                        w("    backtrack")
                        w("  end")
                    elif k in classes:
                        # One letter of the class, whichever: a test of one
                        # character that fails leaves the scan where it was,
                        # so they are asked in turn from the same place.
                        found = "class%d_%d" % (i, n)
                        if any(len(m) > 1 for m in classes[k][0]):
                            raise Trouble("%s: `before %s' reads each member"
                                          " from where the scan stands, so"
                                          " each has to be one letter"
                                          % (a.where, k))
                        for m in classes[k][0]:
                            ctx = letters_of(m)
                            w("  call test_string_s %d %d sym %s"
                              % (letters_at, len(ctx),
                                 known.name("lts", ctx, m)))
                            w("  if answer is 0")
                            w("    go to %s" % found)
                            w("  end")
                        w("  backtrack")
                        w("place %s" % found)
                    else:
                        ctx = letters_of(k)
                        w("  call test_string_s %d %d sym %s"
                          % (letters_at, len(ctx), known.name("lts", ctx, k)))
                        w("  if answer is not 0")
                        w("    backtrack")
                        w("  end")
            if a.when in ("at end", "at word end"):
                # And the run has to finish where the piece does. The scan is
                # past the last letter it matched, so this asks whether that
                # is the piece's last node. An arm of one letter with nothing
                # after it has not moved the scan at all, so it is put on the
                # letter and stepped past it first, which is how IBM's own u
                # asks whether it ends the word.
                if not (len(a.letters) > 1 or a.before):
                    w("  call lpta_loadp %s" % left)
                    w("  call setscan_r %d" % letters_at)
                    w("  if answer is not 0")
                    w("    backtrack")
                    w("  end")
                    w("  call advance_tok")
                    w("  if answer is not 0")
                    w("    backtrack")
                    w("  end")
                w("  call lpta_loadp addr %s" % ("wordend" if "word" in a.when
                                                  else "pieceend"))
                w("  call test_ptr")
                w("  if answer is not 0")
                w("    backtrack")
                w("  end")
            for u, clause in enumerate(getattr(a, "unless", [])):
                kind, what = clause[0], clause[1]
                ending = clause[2] if len(clause) > 2 else None
                clear = "clear%d_%d" % (i, u)
                if kind == "before":
                    # Read what follows afresh from the range's right end; if
                    # all of it is there, this arm is not the one.
                    w("  call lpta_loadp %s" % right)
                    w("  call setscan_r %d" % letters_at)
                    w("  if answer is not 0")
                    w("    go to %s" % clear)
                    w("  end")
                    for k in what.split("+"):
                        if k in CLASSES:
                            w("  call testFldeq %d 4 %d"
                              % (letters_at, CLASSES[k]))
                            w("  if answer is not 0")
                            w("    go to %s" % clear)
                            w("  end")
                            w("  call advance_tok")
                        else:
                            ctx = letters_of(k)
                            w("  call test_string_s %d %d sym %s"
                              % (letters_at, len(ctx),
                                 known.name("lts", ctx, k)))
                            w("  if answer is not 0")
                            w("    go to %s" % clear)
                            w("  end")
                    if ending:
                        w("  call lpta_loadp addr %s"
                          % ("wordend" if "word" in ending else "pieceend"))
                        w("  call test_ptr")
                        w("  if answer is not 0")
                        w("    go to %s" % clear)
                        w("  end")
                    w("  backtrack")
                else:
                    if piece is None or what not in lists:
                        raise Trouble("%s: `unless the root is %s' needs the"
                                      " piece line and that list"
                                      % (a.where, what))
                    spelled_as = dict((c, ch) for ch, c in lcode.items())
                    for k, entry in enumerate(lists[what]):
                        miss = "unl%d_%d_%d" % (i, u, k)
                        w("  call lpta_loadp addr piecestart")
                        w("  call setscan_r %d" % letters_at)
                        w("  if answer is not 0")
                        w("    go to %s" % miss)
                        w("  end")
                        w("  call test_string_s %d %d sym %s"
                          % (letters_at, len(entry), known.name(
                              "lts", entry, "".join(
                                  spelled_as.get(c, "?") for c in entry))))
                        w("  if answer is not 0")
                        w("    go to %s" % miss)
                        w("  end")
                        if kind in ("root is", "root so far"):
                            w("  call lpta_loadp %s"
                              % ("addr pieceend" if kind == "root is"
                                 else right))
                            w("  call test_ptr")
                            w("  if answer is not 0")
                            w("    go to %s" % miss)
                            w("  end")
                        w("  backtrack")
                        w("place %s" % miss)
                w("place %s" % clear)
            if a.listed and a.listed[0] == "so far":
                # The stretch from the root's first letter to the end of the
                # run this arm matched is one of the list's entries: IBM's g
                # hands exactly that stretch to setd_lookup, so giurista's gi
                # is not taken for the entry giuri that its root begins with.
                # Here, after the run, because it asks where the run ended.
                which = a.listed[1]
                begin = "wordstart" if a.listed[2] == "word" else "piecestart"
                if piece is None:
                    raise Trouble("%s: a list is matched against the root,"
                                  " which needs the line `a piece runs from"
                                  " <start> to <end>'" % a.where)
                if which not in lists:
                    raise Trouble("%s: %s has no list called %s"
                                  % (a.where, tag, which))
                spelled_as = dict((c, ch) for ch, c in lcode.items())
                for k, entry in enumerate(lists[which]):
                    miss = "sofar%d_%d" % (i, k + 1)
                    w("  call lpta_loadp addr %s" % begin)
                    w("  call setscan_r %d" % letters_at)
                    w("  if answer is not 0")
                    w("    go to %s" % miss)
                    w("  end")
                    w("  call test_string_s %d %d sym %s"
                      % (letters_at, len(entry),
                         known.name("lts", entry, "".join(
                             spelled_as.get(c, "?") for c in entry))))
                    w("  if answer is not 0")
                    w("    go to %s" % miss)
                    w("  end")
                    w("  call lpta_loadp %s" % right)
                    w("  call test_ptr")
                    w("  if answer is not 0")
                    w("    go to %s" % miss)
                    w("  end")
                    w("  go to sofar%d" % i)
                    w("place %s" % miss)
                w("  backtrack")
                w("place sofar%d" % i)
            w(spell(a, known, phones_of, phones_at, left, right, pfields))
            w("  go to laid")
        else:
            w("")
            if a.note:
                w("# %s" % a.note)
            if i:
                w("place arm%d on %d" % (i, tag_of[("fall", i)]))
            w(spell(a, known, phones_of, phones_at, left, right, pfields))

    if through:
        w("")
        w("place arm%d on %d" % (len(arms), tag_of[("fall", len(arms))]))
        w("  give up")

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


def letter_kinds(tag):
    """How many values the input statement's letter_type has past its first,
    which is what a character that is not a letter holds."""
    kinds = None
    inside = False
    for line in open(os.path.join(ROOT, "lang", tag, "%s.statements" % tag), encoding="utf-8"):
        if line.startswith("statement "):
            inside = line.split()[1] == "inp"
            continue
        if inside and line.startswith("  field "):
            kinds = 0 if line.split()[1] == "letter_type" else None
        elif inside and kinds is not None and line.startswith("    value "):
            kinds += 1
        elif inside and kinds is not None and line.strip() == "end":
            return kinds - 1
    raise Trouble("%s's input statement has no letter_type" % tag)


def follows_rule(tag, cls, members, known, lcode, letters_at, obj,
                 unless=None, step=False):
    """Whether what follows the range's right end is one of a class the end
    of the word counts in.

    The word ends there if the scan cannot be set past it -- the scan stops at
    the fence, so that is also the end of the piece -- or if what follows is
    no kind of letter at all, which catches a word before a space or a full
    stop. testFldeq cannot say which of those it is, since it answers no both
    when a letter follows and when nothing does, so the two halves are asked
    separately. A rule of its own, bare, because every arm that asks it wants
    it asked of a different range.
    """
    # The scan is set afresh before every question, since a member may be two
    # letters and a test of two that matches only the first has moved it.
    #
    # With `step' it is asked one letter further on, of what follows the
    # letter after the right end, which is how a voiced letter is asked
    # whether it keeps its voice.
    rescan = ["  call lpta_loadp arg 1",
              "  call setscan_nof_r %d" % letters_at]
    if step:
        rescan += ["  call advance_tok"]
    # The end of the word is one of the class unless the word after it
    # begins with one of `unless': Polish keeps a final consonant voiced
    # before a word beginning with a voiced one, pod domem.
    at_end = ["  answer 0"]
    if unless is not None:
        at_end = ["  call %s arg 1" % (word_follows_name(tag, unless)
                                       + ("_after" if step else "")),
                  "  if answer is 0",
                  "    answer 1",
                  "  end",
                  "  answer 0"]
    name = follows_name(tag, cls) + ("_after" if step else "")
    out = ["rule %s takes 2 from %s" % (name, obj),
           "  bare",
           "  afresh"]
    # Whether the scan could be set at all, and moved when it is to be, is
    # asked of each step on its own: a failure answered by the step after it
    # would be lost.
    out += rescan[:1]
    for line in rescan[1:]:
        out += [line,
                "  if answer is not 0"] + ["  " + x for x in at_end] + [
                "  end"]
    for m in members:
        try:
            run = bytes(lcode[c] for c in m)
        except KeyError as e:
            raise Trouble("%s has no character %s" % (tag, e))
        out += rescan + [
                "  call test_string_s %d %d sym %s"
                % (letters_at, len(run), known.name("lts", run, m)),
                "  if answer is 0",
                "    answer 0",
                "  end"]
    out += rescan
    for k in range(1, letter_kinds(tag) + 1):
        out += ["  call testFldeq %d 4 %d" % (letters_at, k),
                "  if answer is 0",
                "    answer 1",
                "  end"]
    out += at_end + ["end"]
    return "\n".join(out)


def keeps_rule(tag, cls, members, unless, known, lcode, letters_at, obj):
    """Whether the letter after the range's right end is one of a class and
    is not followed by one of `unless': a voiced letter that keeps its voice.
    liczba has a dż because its b is voiced, and liczb a cz because its b
    is not, being at the end of the word.
    """
    out = ["rule %s takes 2 from %s" % (follows_name(tag, cls), obj),
           "  bare",
           "  afresh",
           "  call lpta_loadp arg 1",
           "  call setscan_nof_r %d" % letters_at,
           "  if answer is not 0",
           "    answer 1",
           "  end"]
    for m in members:
        if len(m) != 1 or m not in lcode:
            raise Trouble("%s: a class with an `unless' of its own is of"
                          " single letters, not %s" % (cls, m))
        out += ["  call test_string_s %d 1 sym %s"
                % (letters_at, known.name("lts", bytes([lcode[m]]), m)),
                "  if answer is 0",
                "    go to one",
                "  end"]
    out += ["  answer 1",
            "place one",
            "  call %s_after arg 1" % follows_name(tag, unless),
            "  if answer is 0",
            "    answer 1",
            "  end",
            "  answer 0",
            "end"]
    return "\n".join(out)


def through_test(name, letters, known, lcode, letters_at, obj):
    """The question the dispatcher asks where it asked whether this is the
    letter whose link the others share. It is bare, being where a wrapper
    stood, and it leaves the scan on the letter when one matches, as the
    test it replaces did."""
    out = ["rule %s takes 1 from %s" % (name, obj), "  bare", "  afresh"]
    for c in letters:
        out += ["  call test_string_s %d 1 sym %s"
                % (letters_at, known.name("lts", bytes([lcode[c]]), c)),
                "  if answer is 0",
                "    answer 0",
                "  end"]
    out += ["  answer 1", "end"]
    return "\n".join(out)


def spell(arm, known, phones_of, phones_at, left, right, pfields=None):
    """The lines that lay an arm's phones over the range it matched, and mark
    them where the arm says to."""
    if arm.phones is None:
        return "# left alone: no call, the letter is the machine's"
    if isinstance(arm.phones, tuple):
        # Handed to another rule with what IBM's e hands its homograph rule:
        # the root's two ends and the letter's, in that order. What it
        # answers is its own business; the letter is spelled either way.
        return ("  call %s addr piecestart addr pieceend %s %s\n"
                "  go to done" % (arm.phones[1], left, right))
    said = phones_of(arm.phones)
    if not said:
        # A silent letter, as IBM's Italian h makes one: a default projection
        # of the phone field at the right end of the range and a deletion at
        # one point on the left. Each of the three things tried here before
        # -- emptying the range with delete_2pt, inserting nought phones, and
        # laying nothing down at all -- answered right and left the walk where
        # it was, so the letter was read again for ever.
        return ("  call lpta_loadp %s\n"
                "  call proj_def %d\n"
                "  call lpta_loadp %s\n"
                "  call delete_1pt %d"
                % (right, phones_at, left, phones_at))
    out = ("  call lpta_rpta_loadp %s %s\n"
           "  call insert_2pt_s %d %d sym %s 0"
           % (left, right, phones_at, len(said),
              known.name("say", said, " ".join(arm.phones))))
    if arm.marked:
        field, value = arm.marked
        if field not in pfields:
            raise Trouble("%s: the phone statement has no field %s"
                          % (arm.where, field))
        at, values = pfields[field]
        if value not in values:
            raise Trouble("%s: %s is one of %s, not %s"
                          % (arm.where, field, ", ".join(values), value))
        # IBM's own order: the insertion answers first, and a failed one
        # backtracks before anything is marked.
        out += ("\n  if answer is not 0\n    backtrack\n  end\n"
                "  call lpta_rpta_loadp %s %s\n"
                "  call mark_s %d %d %d 0"
                % (left, right, phones_at, at, values.index(value)))
    return out


def compile_tag(tag):
    path = os.path.join(ROOT, "lang", tag, "letters")
    if not os.path.exists(path):
        raise Trouble("there is no lang/%s/letters" % tag)
    known = Strings()
    letters_at, phones_at, fence_at = fields(tag)
    pfields = phone_fields(tag)
    lists = word_lists(tag)
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
    blocks, header = parse(path)
    for alias, phone in header.aliases.items():
        if alias in pcode:
            raise Trouble("`phone %s is %s': %s is a phone of %s's already"
                          % (alias, phone, alias, tag))
        if phone not in pcode:
            raise Trouble("`phone %s is %s': %s has no phone %s"
                          % (alias, phone, tag, phone))
    if header.pattern is None:
        raise Trouble("lang/%s/letters names no letter" % tag)
    stem = header.obj[:-4] if header.obj.endswith(".obj") else header.obj

    # The letters with no link of their own become arms of one rule, the
    # one standing where their shared letter's rule stood.
    shared = None
    if header.through:
        letters, via = header.through
        mine = [b for b in blocks if b[0] in letters]
        missing = [c for c in letters if c not in [b[0] for b in mine]]
        if missing:
            raise Trouble("%s come through %s but have no block: %s"
                          % (" ".join(letters), via, " ".join(missing)))
        if any(b[0] == via for b in blocks):
            raise Trouble("%s has a block of its own, and its link is the"
                          " one the letters that come through it use" % via)
        blocks = [b for b in blocks if b[0] not in letters]
        shared = (via, header.pattern % via, header.obj,
                  [a for b in mine for a in b[3]], mine[0][4])

    # A class that counts the end of the word takes its `unless' from one
    # that does not, asked of the next word; one that does not takes it from
    # one that does, asked a letter further on. Each wants the other's rules.
    classes = header.classes
    for cls, (members, end, unless) in classes.items():
        if unless is not None and (unless not in classes
                                   or classes[unless][1] == end):
            raise Trouble("`unless %s follows' on %s wants a class that %s"
                          " the end of the word" % (unless, cls, "does not"
                                                    " count" if end else
                                                    "counts"))
    stepped = set(u for (m, e, u) in classes.values() if u and not e)
    for cls, (members, end, unless) in classes.items():
        if not end:
            for step in (False, True):
                if step and not any(classes[c][2] == cls for c in stepped):
                    continue
                out.append("")
                out.append(word_follows_rule(tag, cls, members, known,
                                             lcode, letters_at, fence_at,
                                             header.obj, step))
    for cls, (members, end, unless) in classes.items():
        if end:
            for step in (False, True):
                if step and cls not in stepped:
                    continue
                out.append("")
                out.append(follows_rule(tag, cls, members, known, lcode,
                                        letters_at, header.obj, unless,
                                        step))
        elif unless is not None:
            out.append("")
            out.append(keeps_rule(tag, cls, members, unless, known, lcode,
                                  letters_at, header.obj))
    for letter, name, obj, arms, ranges in blocks:
        piece, wordrange = ranges
        out.append("")
        out.append(rule_for(tag, letter, name, obj, arms, known, lcode,
                            pcode, letters_at, phones_at, fence_at,
                            takes(tag, stem, name), piece, pfields, lists,
                            wordrange, header))
    if shared is not None:
        via, name, obj, arms, ranges = shared
        out.append("")
        out.append(through_test("%s_test" % name, header.through[0], known,
                                lcode, letters_at, obj))
        out.append("")
        out.append(rule_for(tag, via, name, obj, arms, known, lcode, pcode,
                            letters_at, phones_at, fence_at,
                            takes(tag, stem, name), ranges[0], pfields,
                            lists, ranges[1], header, through=True))
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
        for line in open(where, encoding="utf-8"):
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
        open(rules, "w", encoding="utf-8").write(text)
        open(consts, "w", encoding="utf-8").write(strings)
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
            have = open(where, encoding="utf-8").read() if os.path.exists(where) else ""
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
