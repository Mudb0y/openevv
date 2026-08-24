#!/usr/bin/env python3
"""A rule as what it does, compiled into the notation the machine runs.

The lower notation in tools/delta-lower.py is one line to one operation, which
is what makes it exact and what makes it long: of the 322,890 operations in
English's 1,042 real rules, 202,000 are the argument stack being written out by
hand -- pushes, pops and the calls between them -- and 53,000 more are a
comparison setting the flags in one line and a branch reading them in the next.
None of that says anything about a language.

This is the form above it. It keeps every call exactly as it is, the same entry
with the same arguments in the same order, because a form that reworded what a
rule calls would be describing the rule rather than being it. What it takes
over is the machine: the argument stack, the flags, where in the frame a local
sits, the landing place, the entry and the two ways out, and the numbered
dispatch a rule backtracks through.

The frame. A rule hands the machine five places in its own frame and the
machine writes to all five, so their sizes are not ours to choose: the record
ventproc fills in is 92 bytes, the landing place it plants is 64, and the three
fence arrays are 12 each, which is room for a statement type per byte and ten
is all English declares. Those sit at the bottom, the locals a rule declares
sit above them, and the last word of the frame is the count backtrack_function
is handed. Every rule IBM compiled has that shape, measured over all 1,042.

The tags. A rule backtracks by planting a choice point that carries a small
number and then asking backtrack_function what number came back; the answer
says where to carry on, and -1 says the rule has run out of alternatives. So a
plant is a place in the rule plus a number, and getting the numbers to agree
with the dispatch is exactly the sort of bookkeeping that goes wrong quietly.
Here a plant names a place and the number is the compiler's business.

What it does not do is choose registers well or move anything it does not have
to. There is no reason to: the same rules run either as bytecode or as the C a
decompiler writes out of that bytecode, and neither cares which of the eight
registers held a value on the way. So r0 carries every answer, as the machine
already says it does, and r1 to r5 widen a narrow argument on the way to a
call.

What it does not attempt is a form for the rules as patterns. The census says
why: a median real rule is 28 calls over 15 blocks, only 4% merely test and
assign, and there are 1,058 distinct shapes between the 1,042. They are
programs, and this is a form for writing programs with the machine's
bookkeeping taken off.

usage: delta-upper.py lower <file>...    what it compiles to, as lower notation
       delta-upper.py rules <file>...    one line a rule: shape, blocks, ops
"""

import importlib.util
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name,
                                                  os.path.join(ROOT, path))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


dlo = load("delta_lower", "tools/delta-lower.py")
dgl = load("gen_globals", "tools/gen-globals.py")


# ---- the frame ----------------------------------------------------------
#
# Where each of the five places the machine writes to sits, measured up from
# the bottom of the frame, and how big it is. The order and the sizes are
# IBM's; what is ours is putting the locals above them rather than below,
# which some of its own rules do and some do not.

REC, REC_SIZE = 0, 92           # what ventproc saves so a backtrack can undo
JB, JB_SIZE = REC + REC_SIZE, 64          # the landing place, by its address
MARKS, MARKS_SIZE = JB + JB_SIZE, 12      # the three fence arrays, a byte
CHARS, CHARS_SIZE = MARKS + MARKS_SIZE, 12    # per statement type, so twelve
INDEX, INDEX_SIZE = CHARS + CHARS_SIZE, 12    # is room for the ten English
FIXED = INDEX + INDEX_SIZE                    # declares

# Which comes out as 0, 92, 156, 168, 180 and 192, and all 1,042 of IBM's
# rules lay the block out at exactly those offsets from wherever they put it.

# What a rule answers. Every one of the 1,042 hands vretproc 94 and answers 94
# where it gives up, and 0 where it has matched, and one rule tests a call's
# answer against 94, so the number is not free.
GAVE_UP = 94

WIDTH = {"word": 4, "half": 2, "byte": 1}
# The three parts of a cell, where each sits in it and how wide it is.
CELL = {"kind": (0, 2, True), "field": (2, 2, True), "value": (4, 4, False)}
MOVE = {4: "movl", 2: "movw", 1: "movb"}
WIDEN = {(2, False): "movzwl", (2, True): "movswl",
         (1, False): "movzbl", (1, True): "movsbl"}
CMP_KIND = {4: "cmpl", 2: "cmpw", 1: "cmpb"}
ALU_WORD = {"add": "addl", "subtract": "subl", "and": "andl", "or": "orl",
            "increment": "incl", "decrement": "decl", "negate": "negl",
            "shift left": "shll", "shift right": "sarl"}
ALU_HALF = {"add": "addw", "subtract": "subw", "and": "andw", "or": "orw",
            "increment": "incw", "decrement": "decw", "negate": "negw",
            "shift left": "shlw", "shift right": "sarw"}

# The conditions, and how a test of one is written the other way round, which
# is what an `if' needs: the branch it emits is the one that skips the body.
CONDITION = {
    "is": "e", "is not": "ne",
    "is less than": "l", "is at least": "ge",
    "is more than": "g", "is at most": "le",
    "is below": "b", "is not below": "ae",
    "is above": "a", "is not above": "be",
}
OTHERWISE = {"e": "ne", "ne": "e", "l": "ge", "ge": "l", "g": "le", "le": "g",
             "b": "ae", "ae": "b", "a": "be", "be": "a", "s": "ns", "ns": "s"}

# What a plant calls, and what the wrapper for it is called where the language
# already has one. A wrapper is a rule, so it is entered and says so; a rule
# re-expressed from IBM's own has to go through the same ones or a run of it
# says something different from a run of the original.
PLANTS = {
    "test": ("starttest", "ZZstarttest%d"),
    "test with boa": None,
    "choice": ("bspush_ca", "ZZbspush_ca__%d"),
    "choice with boa": ("bspush_ca_boa", "ZZbspush_ca_boa__%d"),
    "scan": ("bspush_ca_scan", "ZZbspush_ca_scan__%d"),
    "scan with boa": ("bspush_ca_scan_boa", "ZZbspush_ca_scan_boa__%d"),
}


# The three places every rule has, which a plant or a jump may name as well:
# backtracking to `matched' is a rule saying it has matched after all.
OURS = ("matched", "gave_up", "dispatch")


class Trouble(Exception):
    """Something the text says that cannot be compiled, with where it says it."""


def fault(where, what):
    raise Trouble("%s: %s" % (where, what))


# ---- reading the text ---------------------------------------------------


def lines_of(path):
    """Every line that says something, as words, with where it came from."""
    out = []
    for n, raw in enumerate(open(path), 1):
        line = raw.split("#")[0].strip()
        if line:
            out.append(("%s line %d" % (os.path.basename(path), n),
                        line.split()))
    return out


def number(where, text):
    try:
        return int(text, 0)
    except ValueError:
        fault(where, "%r is not a number" % text)


def kind_of(where, w):
    """A width, which is one word or two: `half', or `signed half'."""
    signed = False
    if w and w[0] == "signed":
        w.pop(0)
        signed = True
    if not w or w[0] not in WIDTH:
        fault(where, "expected a width and found %r" % (w[:1],))
    return WIDTH[w.pop(0)], signed


class Rule:
    def __init__(self, name, obj, params, where, named=None):
        self.name = name
        self.obj = obj
        self.params = params
        self.where = where
        self.locals = {}        # name -> (offset from the frame's bottom,
        self.variables = {}     #          width, signed)
        # What the language calls its own variables, out of <tag>.globals.
        # IBM's names are gone, so these are what we have worked the language
        # out to mean, and a rule may use one wherever it could name an
        # offset.
        for nm, (off, width) in (named or {}).items():
            self.variables[nm] = (off, width, False)
        self.wrappers = False
        # A rule of the shape the language's own wrappers have: it is entered
        # and it answers, and that is all. It plants no landing place, saves
        # no record and commits nothing, because a wrapper stands inside
        # somebody else's rule and the choice points around it are theirs.
        self.bare = False
        self.sizes = {}
        self.at = FIXED
        self.body = []

    # A local the machine is handed the address of has to be as big as the
    # machine writes: get_parm fills in a compiled location, which is eight
    # bytes, and a four byte local would take the next one with it. Nothing
    # here knows how much any entry writes, so `bytes\' is how the rule says.
    def add_local(self, where, name, size, width, signed):
        if name in self.locals or name in self.variables:
            fault(where, "there is already something called %r" % name)
        self.locals[name] = (self.at, width, signed)
        self.sizes[name] = size
        self.at += (size + 3) & ~3

    def frame(self):
        """The whole frame: the machine's places, the locals, and the count
        backtrack_function is handed, which is always the last word. A bare
        rule has none of the first and does not backtrack, so it has neither
        the block nor the count."""
        return self.at if self.bare else self.at + 4

    def unwind(self):
        if self.bare:
            fault(self.where, "%s is bare, so it does not backtrack" % self.name)
        return self.at


def parse(path, named=None):
    """Every rule in one file. A block ends at `end', so nothing depends on
    indentation and no line has to be read together with another."""
    words = lines_of(path)
    obj = os.path.basename(path).rsplit(".", 1)[0] + ".obj"
    rules = []
    i = 0
    while i < len(words):
        where, w = words[i]
        if w[0] != "rule":
            fault(where, "expected a rule and found %r" % w[0])
        if len(w) < 4 or w[2] != "takes":
            fault(where, "a rule says `rule <name> takes <n>'")
        r = Rule(w[1], w[5] if len(w) > 5 and w[4] == "from" else obj,
                 number(where, w[3]), where, named)
        i += 1
        i = declarations(words, i, r)
        r.body, i = statements(words, i, r, "end")
        if i >= len(words) or words[i][1][0] != "end":
            fault(where, "the rule %s has no end" % r.name)
        i += 1
        rules.append(r)
    return rules


def declarations(words, i, r):
    while i < len(words):
        where, w = words[i]
        head = w[0]
        if head == "local":
            w = list(w[1:])
            name = w.pop(0)
            size = 4
            width, signed = 4, False
            if w and w[0] == "bytes":
                w.pop(0)
                size = number(where, w.pop(0))
                width = 4 if size >= 4 else size
            elif w:
                width, signed = kind_of(where, w)
                size = width
            if w:
                fault(where, "%r is left over" % " ".join(w))
            r.add_local(where, name, size, width, signed)
        elif head == "variable":
            w = list(w[1:])
            name = w.pop(0)
            width, signed = kind_of(where, w)
            r.variables[name] = (number(where, w.pop(0)), width, signed)
        elif head == "through" and w[1:] == ["wrappers"]:
            r.wrappers = True
        elif head == "bare":
            if r.locals:
                fault(where, "say `bare' before the locals: it moves them")
            r.bare = True
            r.at = 0
        else:
            return i
        i += 1
    return i


BLOCKS = ("if", "while")


def statements(words, i, r, closer):
    """The statements of one block, up to its `end'."""
    out = []
    while True:
        if i >= len(words):
            fault(r.where, "the rule %s has no end" % r.name)
        where, w = words[i]
        head = w[0]
        if head == closer or (closer == "end" and head in ("end", "else")):
            return out, i
        if head in BLOCKS:
            body, i = statements(words, i + 1, r, "end")
            other = []
            if words[i][1][0] == "else":
                other, i = statements(words, i + 1, r, "end")
                if words[i][1][0] != "end":
                    fault(words[i][0], "expected an end")
            out.append((head, where, list(w[1:]), body, other))
        else:
            out.append(("say", where, list(w)))
        i += 1


# ---- writing the machine ------------------------------------------------


class Compiler:
    def __init__(self, r):
        self.r = r
        self.d = dlo.Written(r.name, r.obj, r.frame(), 8, r.params)
        self.cur = None
        self.n = 0
        self.loops = []
        self.at_tag = {}        # a planted number, and where it carries on
        self.dispatches = False
        self.places = set()
        self.wanted = {}        # a place named before it is written

    # -- blocks ----------------------------------------------------------

    def label(self, hint):
        self.n += 1
        return "%s%d" % (hint, self.n)

    def start(self, label):
        self.cur = self.d.block(label)

    def op(self, *rest):
        self.cur.append(tuple(rest))

    def branch(self, cond, target):
        # The notation names a branch as the instruction was named, with the
        # j on the front that the emitter takes off again.
        self.op("branch", "j" + cond, target)

    def ended(self):
        """Whether the block already leaves, so that nothing need be added."""
        return bool(self.cur) and self.cur[-1][0] in ("jump", "return")

    def slot(self, at):
        """A place in the frame, as the machine names it: from the top down."""
        return at - self.r.frame()

    # -- operands --------------------------------------------------------

    def value(self, where, w):
        """One value off the front of a line, and how wide it is: 4 for
        anything that stands for itself, narrower for a place that is."""
        if not w:
            fault(where, "expected a value")
        head = w.pop(0)
        if head == "answer":
            return ("reg", "%eax"), 4, False
        if head == "state":
            return ("state", 0), 4, False
        if head == "arg":
            n = number(where, w.pop(0))
            if n < 1 or n >= self.r.params:
                fault(where, "this rule is handed the state and %d more, so"
                      " there is no arg %d" % (self.r.params - 1, n))
            return ("param", n), 4, False
        if head == "addr":
            name = w.pop(0)
            if name == "unwind":
                return ("slotaddr", self.slot(self.r.unwind())), 4, False
            if name not in self.r.locals:
                fault(where, "%r is not a local of this rule" % name)
            return ("slotaddr", self.slot(self.r.locals[name][0])), 4, False
        if head == "cell":
            # A cell is what the machine writes where a rule hands it the
            # address of a local: a kind, a field and a value. Which of the
            # three a rule means has to be said, since the name on its own
            # is the first word, and that is the kind.
            name = w.pop(0) if w else ""
            if name not in self.r.locals:
                fault(where, "%r is not a local of this rule" % name)
            if self.r.sizes.get(name, 4) < 8:
                fault(where, "%r is %d bytes and a cell is eight"
                      % (name, self.r.sizes.get(name, 4)))
            part = w.pop(0) if w else ""
            if part not in CELL:
                fault(where, "a cell has a kind, a field and a value, and"
                      " nothing called %r" % part)
            off, width, signed = CELL[part]
            at, _cw, _cs = self.r.locals[name]
            return ("slot", self.slot(at + off)), width, signed
        if head == "sym":
            return ("sym", w.pop(0)), 4, False
        if head == "global":
            width, signed = kind_of(where, w)
            return ("statefld", number(where, w.pop(0))), width, signed
        if head == "unwind":
            return ("slot", self.slot(self.r.unwind())), 4, False
        if head in self.r.locals:
            at, width, signed = self.r.locals[head]
            return ("slot", self.slot(at)), width, signed
        if head in self.r.variables:
            off, width, signed = self.r.variables[head]
            return ("statefld", off), width, signed
        return ("imm", number(where, head)), 4, False

    def place_of(self, where, w):
        """A value used as somewhere to write, which is the same set of
        operands less the ones that only stand for themselves."""
        op, width, signed = self.value(where, w)
        if op[0] not in ("slot", "statefld"):
            fault(where, "%s is not somewhere a value can be put" % (op[0],))
        return op, width, signed

    # -- calls -----------------------------------------------------------

    def call(self, where, entry, words):
        """A call, with the state handed over as the machine hands it: the
        last thing pushed is the first argument, so the state goes last."""
        args = []
        w = list(words)
        while w:
            args.append(self.value(where, w))
        scratch = 0
        for op, width, signed in reversed(args):
            if width < 4:
                # Nothing reads a narrow place as a value: a push is four
                # bytes whatever is under it, so the value is widened into a
                # register first. r0 is never used for it, since the argument
                # before this one may be the answer still sitting there.
                scratch += 1
                if scratch > 5:
                    fault(where, "too many narrow arguments in one call")
                reg = "%" + ("ecx", "edx", "ebx", "esi", "edi")[scratch - 1]
                self.op("load", WIDEN[(width, signed)], op, reg)
                self.op("push", ("reg", reg))
            else:
                self.op("push", op)
        self.op("push", ("state", 0))
        self.op("call", entry, len(args) + 1, len(args) + 1)
        self.op("popn", len(args) + 1)

    # -- conditions ------------------------------------------------------

    def condition(self, where, w):
        """Set the flags from what the line compares, and answer the condition
        that is true when it holds."""
        left, lw, _ls = self.value(where, w)
        if not w:
            # A value on its own is the machine's own idiom for `not nothing'.
            self.op("cmp", CMP_KIND[lw], ("imm", 0), left)
            return "ne"
        for n in (3, 2, 1):
            if len(w) >= n and " ".join(w[:n]) in CONDITION:
                cond = CONDITION[" ".join(w[:n])]
                del w[:n]
                break
        else:
            fault(where, "no comparison called %r" % " ".join(w))
        right, rw, _rs = self.value(where, w)
        if w:
            fault(where, "%r is left over" % " ".join(w))
        width = min(lw, rw)
        # The machine subtracts the first operand from the second, so the
        # thing being tested goes second and the thing it is held against
        # first, and then the condition reads the way the line does.
        self.op("cmp", CMP_KIND[width], right, left)
        return cond

    # -- the statements --------------------------------------------------

    def block(self, body):
        for stmt in body:
            if stmt[0] == "if":
                _k, where, w, then, other = stmt
                cond = self.condition(where, list(w))
                after = self.label("after")
                if other:
                    els = self.label("else")
                    self.branch(OTHERWISE[cond], els)
                    self.block(then)
                    if not self.ended():
                        self.op("jump", after)
                    self.start(els)
                    self.block(other)
                else:
                    self.branch(OTHERWISE[cond], after)
                    self.block(then)
                self.start(after)
            elif stmt[0] == "while":
                _k, where, w, body_, _o = stmt
                top = self.label("round")
                after = self.label("done")
                self.op("jump", top)
                self.start(top)
                cond = self.condition(where, list(w))
                self.branch(OTHERWISE[cond], after)
                self.loops.append((top, after))
                self.block(body_)
                self.loops.pop()
                self.op("jump", top)
                self.start(after)
            else:
                self.say(stmt[1], list(stmt[2]))

    def say(self, where, w):
        head = w.pop(0)

        if head == "call":
            if not w:
                fault(where, "a call says what it calls")
            self.call(where, w.pop(0), w)

        elif head == "set":
            place, width, _s = self.place_of(where, w)
            if not w or w.pop(0) != "to":
                fault(where, "a set says `set <place> to <value>'")
            src, sw, ssigned = self.value(where, w)
            if sw < width:
                fault(where, "a %d byte value will not fill %d bytes"
                      % (sw, width))
            self.op("store", MOVE[width], src, place)

        elif head in ("add", "subtract", "and", "or", "shift"):
            if head == "shift":
                head = "shift " + w.pop(0)
            src, _sw, _ss = self.value(where, w)
            if not w or w.pop(0) not in ("to", "from", "into"):
                fault(where, "%s says what it works on" % head)
            place, width, _s = self.place_of(where, w)
            table = ALU_WORD if width == 4 else ALU_HALF
            if width == 1:
                fault(where, "the machine has no arithmetic on one byte")
            self.op("alu", table[head], src, place)

        elif head in ("increment", "decrement", "negate"):
            place, width, _s = self.place_of(where, w)
            if width == 1:
                fault(where, "the machine has no arithmetic on one byte")
            table = ALU_WORD if width == 4 else ALU_HALF
            self.op("alu", table[head], None, place)

        elif head == "put":
            # Writing through a pointer, which is how a rule answers
            # something to whatever called it. The machine cannot store from
            # one place in memory to another, so both ends go through a
            # register, and neither of them is the one the answer is in: a
            # rule commonly puts back what it has just been told.
            src, sw, ssigned = self.value(where, w)
            if not w or w.pop(0) != "into":
                fault(where, "a put says `put <value> into <value> at <n>'")
            ptr, pw, _ps = self.value(where, w)
            if pw != 4:
                fault(where, "a pointer is a whole word")
            off = 0
            if w[:1] == ["at"]:
                del w[:1]
                off = number(where, w.pop(0))
            if sw < 4:
                self.op("load", WIDEN[(sw, ssigned)], src, "%ecx")
            else:
                self.op("load", "movl", src, "%ecx")
            self.op("load", "movl", ptr, "%edi")
            self.op("store", "movl", ("reg", "%ecx"),
                    ("indirect", ("reg", "%edi"), off))

        elif head == "place":
            name = w.pop(0)
            if name in ("enter",) + OURS:
                fault(where, "%r is the compiler's own place" % name)
            if name in self.places:
                fault(where, "there are two places called %r" % name)
            self.places.add(name)
            self.start(name)
            # `on <n>' binds a place to a number nothing in the rule plants.
            # IBM's own rules have such numbers -- high_tone dispatches on 3
            # and plants 1, 2 and 4 -- so a rule re-expressed from one needs
            # to be able to say it.
            if w[:1] == ["on"]:
                self.bind(where, number(where, w[1]), name)

        elif head == "go":
            if not w or w.pop(0) != "to":
                fault(where, "a jump says `go to <place>\'")
            name = w.pop(0)
            self.wanted.setdefault(name, where)
            self.op("jump", name)

        elif head == "plant":
            self.plant(where, w)

        elif head == "backtrack":
            self.backtrack(where)

        elif head == "match":
            if self.r.bare:
                fault(where, "a bare rule answers rather than matching")
            self.op("jump", "matched")

        elif head == "give" and w[:1] == ["up"]:
            if self.r.bare:
                fault(where, "a bare rule answers rather than giving up")
            self.op("jump", "gave_up")

        elif head == "leave":
            if not self.loops:
                fault(where, "nothing to leave")
            self.op("jump", self.loops[-1][1])

        elif head == "again":
            if not self.loops:
                fault(where, "nothing to go round again")
            self.op("jump", self.loops[-1][0])

        elif head == "answer":
            value, _w, _s = self.value(where, w)
            self.op("return", value)

        elif head == "raw":
            # The way out for the operations that have no word here: the nine
            # rules that read a table, the little floating point, and anything
            # else too rare to be worth a form of its own. It is the lower
            # notation, so it says exactly what it does.
            self.op(*dlo.get_op(list(w)))

        else:
            fault(where, "no statement called %r" % head)

    def bind(self, where, tag, name):
        if tag < 1:
            fault(where, "a planted number starts at one")
        if tag in self.at_tag and self.at_tag[tag] != name:
            fault(where, "%d already carries on at %r"
                  % (tag, self.at_tag[tag]))
        self.at_tag[tag] = name
        self.wanted.setdefault(name, where)

    def plant(self, where, w):
        if self.r.bare:
            fault(where, "a bare rule plants nothing: the choice points"
                  " around it are its caller's")
        kind = w.pop(0)
        name = w.pop(0)
        if w[:2] == ["with", "boa"]:
            kind += " with boa"
            del w[:2]
        tag = None
        if w[:1] == ["as"]:
            # Stated rather than allocated, which is what re-expressing one of
            # IBM's rules needs: theirs are numbered as its compiler numbered
            # them and a rule may plant 4 without planting 3.
            tag = number(where, w[1])
            del w[:2]
        if w:
            fault(where, "%r is left over" % " ".join(w))
        if PLANTS.get(kind) is None:
            fault(where, "nothing is planted %r" % kind)
        if tag is None:
            tag = max(self.at_tag) + 1 if self.at_tag else 1
        self.bind(where, tag, name)
        entry, wrapper = PLANTS[kind]
        if self.r.wrappers:
            self.call(where, wrapper % tag, [])
        else:
            self.call(where, entry, [str(tag)])

    def backtrack(self, where):
        """Undo the last rule and carry on where the answer says.

        Where the answer is read is one place for the whole rule, and it has
        to be written after the body rather than here: a plant further down
        the text is a number this has to know about, and at this point in the
        reading it does not exist yet. So every backtrack goes to the one
        chain and the chain is laid down at the end.
        """
        self.call(where, "backtrack_function", ["addr", "unwind"])
        self.dispatches = True
        self.op("jump", "dispatch")

    def bare_rule(self):
        """A rule of the shape the language's own wrappers have.

        There is no entry and there are no tails: the body runs and the rule
        answers. That is what a rule standing where a wrapper stands has to
        be -- the caller planted the choice points around the call and a
        `succeed' of ours would commit them, which reads later as a machine
        whose stack says something that is not so.
        """
        r = self.r
        self.start("enter")
        self.block(r.body)
        # Falling off the end answers what the last call answered, which is
        # what every one of IBM's own wrappers does.
        if not self.ended():
            self.op("return", ("reg", "%eax"))
        missing = [n for n in self.wanted
                   if n not in self.places and n not in OURS]
        if missing:
            fault(self.wanted[missing[0]],
                  "there is no place called %r" % missing[0])
        for n in self.wanted:
            if n in OURS:
                fault(self.wanted[n], "%s is bare, so it has no %r" % (r.name, n))
        return self.d

    def dispatch(self):
        """Which place each planted number carries on at.

        The machine can only answer a number whose choice point is still
        standing, so a place that cannot be reached costs a comparison and
        nothing else. Anything the chain does not name -- and -1, which is
        what the rule's own marker answers -- is the rule out of
        alternatives. A decrement and a test of nothing is how the original
        reads such an answer, and it is one operation shorter than a
        comparison against each number.
        """
        self.start("dispatch")
        # One decrement a number, from one up to the highest planted, because
        # that is how the answer is read: a decrement leaves nothing when the
        # answer was the number of decrements so far. A number with nowhere to
        # carry on at costs the decrement and no branch.
        for tag in range(1, max(self.at_tag) + 1):
            self.op("alu", "decl", None, ("reg", "%eax"))
            if tag in self.at_tag:
                self.branch("e", self.at_tag[tag])
        self.op("jump", "gave_up")

    # -- the whole rule --------------------------------------------------

    def rule(self):
        r = self.r
        if r.bare:
            return self.bare_rule()
        self.start("enter")
        # The count backtrack_function is handed starts at nothing, and an
        # `and' with nothing is how the original writes that.
        self.op("alu", "andl", ("imm", 0), ("slot", self.slot(r.unwind())))
        # The landing place, planted here rather than in the runtime: a
        # backtrack has to come back into the rule and not into whatever
        # called it. The last thing pushed is the buffer, which is only a
        # name -- one place is kept per address, outside the frame.
        self.op("push", ("imm", 0))
        self.op("push", ("slotaddr", self.slot(JB)))
        self.op("call", "setjmp3", 2, 2)
        self.op("popn", 2)
        self.op("cmp", "testl", ("reg", "%eax"), ("reg", "%eax"))
        self.branch("ne", "gave_up")
        for at in (JB, MARKS, CHARS, INDEX, REC):
            self.op("push", ("slotaddr", self.slot(at)))
        self.op("push", ("state", 0))
        self.op("call", "ventproc", 6, 6)
        self.op("popn", 6)
        self.op("cmp", "testl", ("reg", "%eax"), ("reg", "%eax"))
        self.branch("ne", "gave_up")

        self.block(r.body)
        # Falling off the end of the body is giving up, which is what the
        # original does too: every way out of a rule is one of the two tails.
        if not self.ended():
            self.op("jump", "gave_up")

        if self.dispatches:
            if not self.at_tag:
                fault(r.where, "%s backtracks with nothing planted" % r.name)
            self.dispatch()

        self.start("gave_up")
        self.call(r.where, "vretproc", [str(GAVE_UP)])
        self.op("return", ("imm", GAVE_UP))

        self.start("matched")
        self.call(r.where, "succeed", [])
        self.op("return", ("imm", 0))

        missing = [n for n in self.wanted
                   if n not in self.places and n not in OURS]
        if missing:
            fault(self.wanted[missing[0]],
                  "there is no place called %r" % missing[0])
        return self.d


def compile_file(path, lang=None):
    """Every rule in one file, in the shape the emitter takes.

    Told which language, it lets a rule use the names that language gives its
    own variables; without one, a variable is named by its offset as before.
    """
    named = dgl.names_of(lang) if lang else {}
    out = []
    for r in parse(path, named):
        out.append((r.name, Compiler(r).rule(), r.obj))
    return out


# ---- what it compiled to ------------------------------------------------


def main():
    if len(sys.argv) < 3:
        print(__doc__.strip())
        return 2
    what = sys.argv[1]
    ok = True
    # Which language, so that a rule may use the names that language gives its
    # own variables. It is the same variable the rest of the pipeline reads.
    lang = os.environ.get("EVV_NOTATION_LANG")
    for path in sys.argv[2:]:
        try:
            rules = compile_file(path, lang)
        except Trouble as e:
            print("upper: %s" % e)
            ok = False
            continue
        if what == "lower":
            out = []
            for name, d, obj in rules:
                dlo.write_rule(name, obj, d, {}, out)
                out.append("")
            print("\n".join(out))
        elif what == "rules":
            for name, d, _obj in rules:
                print("%-32s frame %3d params %d, %d blocks, %d operations"
                      % (name, d.frame, d.params, len(d.blocks),
                         sum(len(b[2]) for b in d.blocks)))
        else:
            print(__doc__.strip())
            return 2
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
