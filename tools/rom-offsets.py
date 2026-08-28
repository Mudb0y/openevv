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

usage: rom-offsets.py [textanalysis|dictsearch]
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


def defines(path):
    out = {}
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
        (d["TA_SPARE"], d["TA_SPARE_END"] - d["TA_SPARE"], "the spare region"),
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
        (d["DS_UNREAD_HEAD"], d["DS_UNREAD_HEAD_END"] - d["DS_UNREAD_HEAD"],
         "the working store nobody has read"),
        (d["DS_FZK"], d["DS_FZK_N"] * d["DS_FZK_SIZE"], "the function words"),
        (d["DS_REC"], d["DS_REC_N"] * d["DS_REC_SIZE"], "the three records"),
        (d["DS_COUNT"], 2, "the count"),
        (d["DS_UNREAD_MID"], d["DS_UNREAD_MID_END"] - d["DS_UNREAD_MID"],
         "the middle nobody has read"),
        (d["DS_TANKAN"], d["DS_TANKAN_N"] * d["DS_TANKAN_SIZE"],
         "the tankan table"),
        (d["DS_KANA"], d["DS_KANA_END"] - d["DS_KANA"], "the kana buffers"),
        (d["DS_WORK"], d["DS_WORK_END"] - d["DS_WORK"], "the working area"),
        (d["DS_W_8508"], 2, "a word"),
        (d["DS_W_850A"], 2, "a word"),
        (d["DS_W_850C"], 2, "a word"),
        (d["DS_W_850E"], 2, "a word"),
        (d["DS_W_8510"], 2, "a word"),
        (d["DS_W_8512"], 2, "a word"),
        (d["DS_INPUTCHAR"], 4, "the input reader"),
        (d["DS_UNREAD_TAIL"], d["DS_UNREAD_TAIL_END"] - d["DS_UNREAD_TAIL"],
         "the tail nobody has read"),
        (d["DS_L_8900"], 4, "a long"),
        (d["DS_L_8904"], 4, "a long"),
    ]
    return [(at, at + n - 1, name) for at, n, name in r]


# Which objects hold a class's own code -- a class may be spread over
# several, and DictSearch is spread over four -- the header that maps it, the
# region table, and the three names the checker needs out of that header: how
# big the object is, the offset below which a displacement tells us nothing,
# and the size of the widest thing that could be mistaken for it when sweeping
# the rest of the module. That last one is None where nothing else is close.
CLASSES = {
    "textanalysis": (["txtanal.obj"], "txtanal.h", regions, "TA_BYTES",
                     "TA_MARKS", "TA_PHRASEBUF_BYTES"),
    "dictsearch": (["dictsearch.obj", "dictapi.obj", "fdictapi.obj",
                    "kanastr.obj"], "dictsearch.h", regions_ds, "DS_BYTES",
                   "DS_FZK", None),
}


def main(argv):
    which = argv[0] if argv else "textanalysis"
    if which not in CLASSES:
        print("rom-offsets: no map of %s" % which)
        return 2
    objnames, headname, regionsOf, sizeName, floorName, wideName = \
        CLASSES[which]
    where = os.path.join(ROOT, "analysis", "jajp")
    objs = [os.path.join(where, n) for n in objnames]
    head = os.path.join(ROOT, "rom", "jajp", headname)
    d = defines(head)
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
