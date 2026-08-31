#!/usr/bin/env python3
"""Hold the map of TextAnalysis against IBM's own object.

TextAnalysis is one lump of 946,216 bytes and every other class in the Japanese
analyser is handed a reference to it and reads its fields directly, so nothing
can be written until the record is known. rom/jajp/txtanal.h is what that record
was read as; this is what says the reading is still true.

What it does. It pulls every offset txtanal.obj uses on a pointer -- every
displacement in a memory operand, and every large immediate added to a register,
which is how the compiler forms the base of an inner array -- and asks whether
each falls inside a region txtanal.h names. An offset that does not is either a
field nobody has written down yet or a mistake in the header, and either way it
is worth knowing about.

It is deliberately blunt about one thing: an offset in that object may be on a
sub-object's pointer rather than on TextAnalysis itself -- InputChar's own
fields are read at 0x27ac and DictSearch's at 0x80ac -- so the header names
those too. Anything left over is printed.

What it reads. From txtanal.obj, every offset above the head, since a stack
displacement is negative and cannot be mistaken for one. From every other object
in the module, every offset larger than the widest thing the analyser allocates
besides this one -- past that, a field can only be TextAnalysis's.

usage: rom-offsets.py [textanalysis|dictsearch|inputchar|jpath|phrasebuf]
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# A displacement in a memory operand, and a base formed by adding an
# immediate. Both are how a field is reached.
OPERAND = re.compile(r"(?:^|[\s,])(0x[0-9a-f]+)\(%e")
IMMED = re.compile(r"(?:add|lea)l?\s+\$(0x[0-9a-f]+),\s*%e")

# What the header calls things, as name and value.
DEFINE = re.compile(r"^#define\s+(\w+)\s+(0x[0-9a-f]+|\d+)")


def run(*args):
    return subprocess.run(args, capture_output=True, text=True,
                          check=True).stdout


def defines(paths):
    """Every name a header gives a number to. More than one may be named: a
    header that maps one class may include the header of another it reads."""
    out = {}
    for path in paths:
        for line in open(path):
            m = DEFINE.match(line)
            if m:
                out[m.group(1)] = int(m.group(2), 0)
    return out

def regions(d):
    """Every named region as (first, last, name). A region is a single field,
    a run of a known count and stride, or a pair of bounds."""
    r = [
        (d["TA_VTABLE"], 4, "the vtable"),
        (d["TA_OWNER"], 4, "the romanizer"),
        (d["TA_FORMATTED"], 4, "the formatted text"),
        (d["TA_DONE"], 4, "done"),
        (d["TA_UNKNOWN_10"], 4, "the field nothing touches"),
        (d["TA_INPUTCHAR"], 4, "the input reader"),
        (d["TA_ANNOTATION"], 4, "the annotations"),
        (d["TA_DICTSEARCH"], 4, "the dictionary search"),
        (d["TA_JPATH"], 4, "the path search"),
        (d["TA_PHRASEBUF"], 4, "the phrase buffer"),
        (d["TA_PHRASETABLE"], 4, "the phrase table object"),
        (d["TA_MARKS"], d["TA_MARKS_END"] - d["TA_MARKS"], "the parse marks"),
        (d["TA_PERBUF"], d["TA_PERBUF_N"] * d["TA_PERBUF_SIZE"],
         "the three per-buffer records"),
        (d["TA_SPARE"], d["TA_LONGWORD"] - d["TA_SPARE"], "the spare region"),
        (d["TA_LONGWORD"], d["TA_LONGWORD_N"] * d["TA_LONGWORD_SIZE"],
         "the long readings"),
        (d["TA_LONGWORDS"], 1, "how many of them"),
        (d["TA_SPARE_8FF"], d["TA_SPARE_END"] - d["TA_SPARE_8FF"],
         "the byte after them"),
        (d["TA_BUFFERS"], d["TA_BUFFER_N"] * d["TA_BUFFER_SIZE"],
         "the three phrase buffers"),
        (d["TA_USED"], 6, "how much of each buffer is used"),
        (d["TA_COUNT"], 2, "the total"),
        (d["TA_WORK"], d["TA_WORK_END"] - d["TA_WORK"], "the working area"),
        (d["TA_LINK"], d["TA_LINK_N"] * d["TA_LINK_SIZE"], "the link chain"),
        (d["TA_PHRASE"], d["TA_PHRASE_N"] * d["TA_PHRASE_SIZE"],
         "the phrase table"),
        (d["TA_FIRST"], 2, "first"),
        (d["TA_LAST"], 2, "last"),
        (d["TA_SPARE_18"], 2, "the spare word"),
        (d["TA_TOP"], 2, "top"),
        (d["TA_RAW_LEN"], 4, "the raw length"),
        (d["TA_RAW"], 4, "the raw text"),
        (d["TA_NORMALIZER"], 4, "the normalizer"),
        (d["IC_AT_END"], 4, "InputChar's end flag"),
        (d["IC_SNLK_TABLE"], 4, "InputChar's table chain"),
        (d["IC_LENGTH"], 2, "InputChar's length"),
        (d["DS_COUNT"], 2, "DictSearch's count"),
        (d["PB_TAIL"], 12, "PhraseBuf's tail"),
    ]
    return [(at, at + n - 1, name) for at, n, name in r]


def regions_ds(d):
    """The same for DictSearch, which is mapped in part. Every unresolved span
    is a region too, so that the tiling still holds and says what is not
    known rather than passing over it."""
    r = [
        (d["DS_VTABLE"], 4, "the vtable"),
        (d["DS_OWNER"], 4, "the owner"),
        (d["DS_ENTRY"], d["DS_ENTRY_N"] * d["DS_ENTRY_SIZE"],
         "the candidate entries"),
        (d["DS_FZK"], d["DS_FZK_N"] * d["DS_FZK_SIZE"], "the function words"),
        (d["DS_REC"], d["DS_REC_N"] * d["DS_REC_SIZE"], "the three records"),
        (d["DS_COUNT"], 2, "the count"),
        (d["DS_W_80AE"], 2, "the two bytes nothing touches"),
        (d["DS_TEXT"], d["DS_UNREAD_MID"] - d["DS_TEXT"], "the text buffer"),
        (d["DS_UNREAD_MID"], d["DS_UNREAD_MID_END"] - d["DS_UNREAD_MID"],
         "the middle nobody has read"),
        (d["DS_READING"], d["DS_READING_N"] * d["DS_READING_SIZE"],
         "the readings"),
        (d["DS_MARK"], d["DS_CAND_N"], "the candidate marks"),
        (d["DS_CHARS"], d["DS_CAND_N"] * 2, "how many characters each has"),
        (d["DS_LEN"], d["DS_CAND_N"] * 2, "how many bytes each has"),
        (d["DS_TAKEN"], d["DS_CAND_N"] * 2, "which have been taken"),
        (d["DS_NCAND"], 2, "the candidate count"),
        (d["DS_KANA"], d["DS_KANA_N"] * d["DS_KANA_SIZE"],
         "one kanji's readings"),
        (d["DS_KANA_CHARS"], d["DS_KANA_N"], "how many characters each took"),
        (d["DS_KANA_LEN"], d["DS_KANA_N"], "and how many bytes each is"),
        (d["DS_CURSOR"], 2, "the entry cursor"),
        (d["DS_COPIED"], 2, "what GetTextBuf copied"),
        (d["DS_RUNS"], 2, "how many hiragana runs"),
        (d["DS_TOTAL"], 2, "the running total"),
        (d["DS_FROM"], 2, "where the lookup starts"),
        (d["DS_TO"], 2, "and where it stops"),
        (d["DS_INPUTCHAR"], 4, "the input reader"),
        (d["DS_UNREAD_TAIL"], d["DS_UNREAD_TAIL_END"] - d["DS_UNREAD_TAIL"],
         "the tail nobody has read"),
        (d["DS_USERDICT_MODE"], 4, "the user-dictionary mode"),
        (d["DS_USERDICT_WORD"], 4, "the word it must agree with"),
    ]
    return [(at, at + n - 1, name) for at, n, name in r]


def regions_ic(d):
    """And for InputChar, which is mapped whole. The three arrays are the
    record: everything else is one field or a span nobody has read."""
    r = [
        (d["IC_OWNER"], 4, "the analysis"),
        (d["IC_TEXT"], d["IC_TEXT_N"] * 2, "the characters"),
        (d["IC_SCRATCH"], d["IC_SCRATCH_N"] * 2, "the collecting scratch"),
        (d["IC_KIND"], d["IC_TEXT_N"] * 4, "what each character is"),
        (d["IC_OFFSET"], d["IC_TEXT_N"] * 2, "where each one starts"),
        (d["IC_RAWPOS"], 4, "how far into the caller's text"),
        (d["IC_MARK"], d["IC_TEXT_N"] * 4, "what a candidate carries away"),
        (d["IC_COUNT"], 4, "how many characters there are"),
        (d["IC_POS"], 4, "the byte reached"),
        (d["IC_ENDED"], 4, "whether it ended on something"),
        (d["IC_TEXTP"], 4, "the text"),
        (d["IC_RESUME"], 4, "whether to carry on"),
        (d["IC_ENGRUN"], 4, "a run of letters is open"),
        (d["IC_NUMRUN"], 4, "a run of digits is open"),
        (d["IC_NUMJOIN"], 4, "a number carries across"),
        (d["IC_BRACKET_AT"], 2, "the last closing bracket"),
        (d["IC_ENDMARK"], 4, "what it ended on"),
        (d["IC_UNREAD_27A2"], 2, "the half word nobody has read"),
        (d["IC_PAUSE"], 4, "the pauses added up"),
        (d["IC_AT_END"], 4, "the end flag"),
        (d["IC_SNLK_TABLE"], 4, "the table chain"),
        (d["IC_LENGTH"], 4, "how many characters came before"),
        (d["IC_MORE"], 4, "the buffer ran out"),
    ]
    return [(at, at + n - 1, name) for at, n, name in r]


def regions_jp(d):
    """And for JPath, which is mapped whole but for one span. The three arrays
    are almost the whole of it and their counts agree with each other and with
    DictSearch's own."""
    r = [
        (d["JP_VTABLE"], 4, "the vtable"),
        (d["JP_OWNER"], 4, "the owner"),
        (d["JP_PATH"], d["JP_PATH_N"] * d["JP_PATH_SIZE"], "the paths"),
        (d["JP_SUB"], d["JP_SUB_N"] * d["JP_SUB_SIZE"], "the sub-words"),
        (d["JP_PATH_COUNT"], 2, "how many paths"),
        (d["JP_UNREAD_7486"], d["JP_UNREAD_N"], "the span nobody has read"),
        (d["JP_INDEX"], d["JP_INDEX_N"] * 2, "entry to sub-word"),
        (d["JP_SEARCH"], 4, "the dictionary search"),
    ]
    return [(at, at + n - 1, name) for at, n, name in r]


def regions_pb(d):
    """And for PhraseBuf, which is almost all buffer: one copy of one of the
    owner's three, and four fields around it."""
    r = [
        (d["PB_VTABLE"], 4, "the vtable"),
        (d["PB_OWNER"], 4, "the owner"),
        (d["PB_BUFFER"], d["PB_BUFFER_SIZE"], "the working copy"),
        (d["PB_TAIL"], 4, "the four bytes nobody has read"),
        (d["PB_SEARCH"], 4, "the dictionary search"),
        (d["PB_JPATH"], 4, "the path search"),
    ]
    return [(at, at + n - 1, name) for at, n, name in r]


# Which objects hold a class's own code -- a class may be spread over
# several, and DictSearch is spread over four -- the header that maps it, the
# region table, and the three names the checker needs out of that header: how
# big the object is, the offset below which a displacement tells us nothing,
# and the size of the widest thing that could be mistaken for it when sweeping
# the rest of the module. That last one is None where nothing else is close.
CLASSES = {
    "textanalysis": (["txtanal.obj"], ["txtanal.h", "inputchar.h"],
                     regions, "TA_BYTES",
                     "TA_MARKS", "TA_PHRASEBUF_BYTES"),
    "dictsearch": (["dictsearch.obj", "dictapi.obj", "fdictapi.obj",
                    "kanastr.obj", "engread.obj", "numanal.obj",
                    "phrasetable.obj"], ["dictsearch.h", "inputchar.h"],
                   regions_ds,
                   "DS_BYTES", "DS_FZK", None),
    "inputchar": (["inputchar.obj"], ["inputchar.h"], regions_ic,
                  "IC_BYTES", "IC_OWNER", None),
    "jpath": (["jpath.obj"], ["jpath.h"], regions_jp,
              "JP_BYTES", "JP_PATH", None),
    "phrasebuf": (["phrasebuf.obj"], ["phrasebuf.h"], regions_pb,
                  "PB_BYTES", "PB_BUFFER", None),
}


def main(argv):
    which = argv[0] if argv else "textanalysis"
    if which not in CLASSES:
        print("rom-offsets: no map of %s" % which)
        return 2
    objnames, headnames, regionsOf, sizeName, floorName, wideName = \
        CLASSES[which]
    where = os.path.join(ROOT, "analysis", "jajp")
    objs = [os.path.join(where, n) for n in objnames]
    heads = [os.path.join(ROOT, "rom", "jajp", n) for n in headnames]
    d = defines(heads)
    named = regionsOf(d)

    def offsets(path):
        found = set()
        for line in run("llvm-objdump", "-d", "--no-show-raw-insn",
                        path).splitlines():
            for m in OPERAND.finditer(line):
                found.add(int(m.group(1), 16))
            for m in IMMED.finditer(line):
                found.add(int(m.group(1), 16))
        return found

    # From the class's own object, everything above the head. A stack
    # displacement is negative and the pattern above does not match one, so
    # what is left is a field of this class or of one of the six it holds.
    seen = set()
    for one in objs:
        seen |= set(x for x in offsets(one) if x >= d[floorName])

    # And from every other object in the module, everything too large to be
    # anything else: the widest object the analyser allocates besides this one
    # is PhraseBuf, so an offset past that can only be a TextAnalysis field.
    if wideName is not None:
        for f in sorted(os.listdir(where)):
            if not f.endswith(".obj") or f in objnames:
                continue
            try:
                found = offsets(os.path.join(where, f))
            except subprocess.CalledProcessError:
                continue
            seen |= set(x for x in found if x > d[wideName])

    # An offset past the end of the object is not one of its fields. The
    # dictionary blobs are data with no code in them, and a disassembler asked
    # to read data prints operands; this is what keeps those out.
    seen = sorted(x for x in seen if x < d[sizeName])

    inside, outside = 0, []
    for at in seen:
        for first, last, name in named:
            if first <= at <= last:
                inside += 1
                break
        else:
            outside.append(at)

    print("%d offsets, %d inside a named region" % (len(seen), inside))
    bad = 0
    if outside:
        print("%d not accounted for:" % len(outside))
        for at in outside:
            print("    0x%x" % at)
        bad = 1
    else:
        print("every one is accounted for")

    # And the map's own arithmetic: the regions of the class itself have to
    # tile the object from nought to its size, with no gap and no overlap.
    # This is what holds a count in place -- nothing indexes the phrase
    # buffers with a constant, so the only thing that says there are three of
    # them is that three of them reach exactly as far as the next field.
    mine = sorted((a, b, n) for a, b, n in named if b < d[sizeName]
                  and not n.startswith(("InputChar", "DictSearch",
                                        "PhraseBuf")))
    at = 0
    for first, last, name in mine:
        if first > at:
            print("a gap of %d bytes at 0x%x, before %s"
                  % (first - at, at, name))
            bad = 1
        elif first < at:
            print("%s overlaps what is in front of it by %d bytes"
                  % (name, at - first))
            bad = 1
        at = last + 1
    if at != d[sizeName]:
        print("the regions run to 0x%x and the object is 0x%x"
              % (at, d[sizeName]))
        bad = 1
    if not bad:
        print("and the regions tile the whole of it")
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
