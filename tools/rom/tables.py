#!/usr/bin/env python3
"""Lift the romanizer's own tables out of the two objects that are all table.

What this is. Beside the static dictionary that tools/rom/dictionary.py takes, the
Japanese romanizer carries three objects that are mostly table: dictman.obj,
sixty thousand bytes of hash tables, penalty tables, number and reading tables
and the two substitution tables that turn English into romaji and romaji into
kana; unicodeconvt.obj, a hundred and twenty-nine thousand bytes of Shift-JIS
and Unicode conversion tables; and jpnutil.obj, whose two thousand bytes are
one row of kana to a name and the romaji each kana in it is spelled with.

Neither has a single relocation inside its data, so both are bytes and nothing
about their format has to be understood here. What is transcribed separately is
the code that reads them, which reads them exactly as the original does.

Each object comes out as one block of bytes with a pointer into it per table,
which is how the original had it. That matters rather than being tidiness: the
converter accepts lead bytes past the end of the table it looks them up in, and
a packed record can run on past the end of its own table, so a table laid out
on its own would answer with something the original never saw. A table's length
is the distance to the next one and includes whatever padding sat between.

Two of dictman's names are counts rather than arrays -- s_nEng2Roman and
s_nRoman2Kana -- and come out as the two-byte arrays that hold them.

usage: tools/rom/tables.py [objdir] [outdir]
"""

import os
import re
import subprocess
import sys

# tools/evv.py says where the tree is, so that no tool counts directories to
# find it. The one thing this line has to know is that the directory above a
# tool's group is tools itself.
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from evv import ROOT

# Which objects, which section of each holds the tables, and what the file
# says about it.
OBJECTS = [
    ("dictman.obj", ".rdata",
     "DictMan's tables: the dictionary hashes, the penalty and phrase\n"
     " * vectors, the number and reading tables, and the substitution tables\n"
     " * that make romaji out of English and kana out of romaji."),
    ("unicodeconvt.obj", ".rdata",
     "UnicodeConverter's tables: Shift-JIS to Unicode and back, the two\n"
     " * lead-byte tables that say which bytes begin a two-byte character,\n"
     " * and the kana and AI tables beside them."),
    ("jpnutil.obj", ".data",
     "JpnUtil's tables: one row of kana to a name, and the romaji each of\n"
     " * the five in that row is spelled with. These are what turn a kana\n"
     " * code into letters, five bytes to an entry."),
    ("userdict.obj", ".rdata",
     "RomUserDict's one table: three bytes for each of the four parts of\n"
     " * speech a user-dictionary entry may be given, which are the part of\n"
     " * speech and the two attribute bytes the candidate entry carries."),
    ("phrasebuf.obj", ".rdata",
     "PhraseBuf's one table: three hundred and eighty two-byte verbs, each\n"
     " * with a nought after it so that strcmp can be used, which are the\n"
     " * single-kanji verbs a doubled consonant may attach to."),
]

# And the objects whose tables are each in a COMDAT section of its own rather
# than together in one block. Neither the largest-section rule below nor the
# offsets `nm' prints can tell those apart: every one of them starts at
# nought of its own section. Such an object names its tables here instead,
# with the section each is in and how long each is, and the lift refuses
# unless exactly the expected number of blocks of that length is there and
# they all hold the same bytes.
PER_SECTION = [
    ("numread.obj",
     "NumRead's tables. m_sanTCodes is the twenty-six characters a digit or\n"
     " * an operator is written as, and SINDX is the run table that says\n"
     " * which entries of the reading table one of those codes names --\n"
     " * entry i runs from SINDX[i] to SINDX[i + 1] less one. IBM's compiler\n"
     " * put a copy of SINDX in a section of its own for each of the two\n"
     " * methods that reads it, and the two copies are identical.",
     [("m_sanTCodes", ".data", 0x20, 1),
      ("SINDX", ".data", 0x14, 2)]),
]

# And the objects that keep a named block in a section of a size rather than
# in the largest one of its name, plus the tables inside it. PCProsCtrl has
# its phoneme spellings in one small .rdata and two arrays of pointers in
# .data, so neither the largest-section rule nor the symbol offsets `nm'
# prints can be used on their own: the offsets are into two different
# sections and the largest .rdata is not the one wanted.
NAMED = [
    ("PCProsCtrl.obj", ".rdata", 0x87,
     "ProsCtrl's phoneme spellings, three bytes to an entry: thirty-two\n"
     " * consonants, eight vowels and five long vowels, which is the whole\n"
     " * Japanese inventory as the synthesiser spells it. The consonant\n"
     " * table is what IsBurstCons's twelve codes index, and reading the two\n"
     " * together is what says those twelve are the plosives and their\n"
     " * palatalised forms.",
     [("s_aszCname", 0x000, 0x60),
      ("s_aszVname", 0x060, 0x18),
      ("s_aszLVname", 0x078, 0x0f)]),
    ("MakeReadableJP.obj", ".rdata", 0x41,
     "MakeReadableJP's four currency names that are file statics rather\n"
     " * than string literals: the pesos of Argentina, Chile, Colombia and\n"
     " * Mexico. The dash in front of them is the same block and is named so\n"
     " * that the map still tiles.",
     [("szKANA_DASH", 0x00, 0x04),
      ("szKANA_ARS", 0x04, 0x14),
      ("szKANA_CLP", 0x18, 0x0c),
      ("szKANA_COP", 0x24, 0x10),
      ("szKANA_MXN", 0x34, 0x0d)]),
    ("MakeReadableJP_SPR.obj", ".rdata", 0x65f,
     "MakeReadableJP's kana table, five bytes to an entry and a hundred\n"
     " * and sixty-three entries in each half: j_phones is how the\n"
     " * synthesiser spells a sound and j_kana is the kana that sound is\n"
     " * written with. convertSPR reads the first to find a match and\n"
     " * writes the second. The pad byte at the end of the first half is\n"
     " * counted with it so that the map still tiles.",
     [("j_phones", 0x000, 0x330),
      ("j_kana", 0x330, 0x32f)]),
]

# And the objects with an array of pointers to string literals rather than an
# array of bytes. Those cannot be lifted as bytes at all: what is in the
# object is a relocation a slot, and the strings are each in a COMDAT section
# of their own. The lift follows the relocations, reads each literal out of
# its own section, and writes the array out as string literals -- so the
# strings are IBM's, taken rather than retyped, exactly as the byte tables
# are.
POINTER_TABLES = [
    ("PCProsCtrl.obj",
     "ProsCtrl's two arrays of names for a part of speech, which is what\n"
     " * GetGokiInfoToWrite puts in the output beside a word. The first is\n"
     " * indexed by the part of speech itself and the second by the negative\n"
     " * of it, which is how a punctuation mark is told from a word.",
     [("s_aszPosInfo", ".data", 0x500, 11),
      ("s_aszSpecialPosInfo", ".data", 0x52c, 7)]),
]

# How many entries each of those really has, which IBM keeps beside them as a
# plain int rather than leaving to be counted. The lift reads both and refuses
# if either disagrees with the count named above, so a table that grew or
# shrank is caught rather than silently truncated.
POINTER_COUNTS = [
    ("PCProsCtrl.obj", ".data",
     [("s_aszPosInfo", 0x548), ("s_aszSpecialPosInfo", 0x54c)]),
]

# And the objects with a table of pairs: a number and a pointer to a string,
# eight bytes an entry, ending on an entry whose pointer is nought. Neither
# the byte lifter nor the pointer lifter above can take those, because half of
# each entry is in the section and half is a relocation. The lift walks the
# entries, reads the number out of the section and the string out of its own
# COMDAT, and stops where the pointer stops.
SYMBOL_TABLES = [
    ("TextNormalizer.obj", ".data",
     "TextNormalizer's one table: the names an annotation may be written\n"
     " * with and the number each stands for, which is what says whether a\n"
     " * bracketed piece of text is a date, a time, a telephone number, an\n"
     " * amount of money, a cardinal, an ordinal or a truth value, and in\n"
     " * which of the ten orders a date is written. The terminating entry\n"
     " * holds minus one, which is what a name in none of them comes back\n"
     " * as.",
     ["aMakeReadableAnnos"]),
    ("MakeReadableJP.obj", ".data",
     "MakeReadableJP's twelve symbol tables, which are what its dozen\n"
     " * predicates ask. Each is a run of pairs -- what the symbol means and\n"
     " * how it is written -- and the writing is one or two bytes of\n"
     " * Shift-JIS, so a table holds a full-width form and a half-width form\n"
     " * of the same thing side by side.",
     ["aCURRENCY_SYMBOLS", "aCURRENCY_PUNCTS", "aDECIMAL_POINTS",
      "aPLUS_MINUS_SYMBOLS", "aRANGE_SYMBOLS", "aDATE_SEPARATORS",
      "aDAYOFWEEK_SYMBOLS", "aPARENTHESIS_SYMBOLS", "aTIME_DELIMS",
      "aTEL_SYMBOLS", "aBOOL_SYMBOLS"]),
]

# A static member of a class, as MSVC spells one: ?name@Class@@ and then the
# type. A global of no class, which is ?name@@ and the type. And a plain
# file-static, which carries only the C underscore.
MEMBER = re.compile(r"^\?([A-Za-z_]\w*)@(\w+)@@")
GLOBAL = re.compile(r"^\?([A-Za-z_]\w*)@@")
STATIC = re.compile(r"^_([A-Za-z_]\w*)$")

# Names that are not tables: the compiler's string literals and its own
# section symbols.
SKIP = re.compile(r"^\?\?_C@|^\.")


def run(*args):
    return subprocess.run(args, capture_output=True, text=True,
                          check=True).stdout


def sections(obj, section):
    """Every block of that section name in the object, in the order printed.

    An object may have several sections of one name: a two-byte COMDAT for a
    string literal beside the large one, or a table apiece where the compiler
    gave each its own."""
    text = run("i686-w64-mingw32-objdump", "-s", "-j", section, obj)
    blocks = []
    data = bytearray()
    for line in text.splitlines():
        if line.startswith("Contents of section"):
            if data:
                blocks.append(bytes(data))
            data = bytearray()
            continue
        m = re.match(r"^\s+([0-9a-f]+)\s+((?:[0-9a-f]{2,8}\s+){1,4})", line)
        if not m:
            continue
        at = int(m.group(1), 16)
        raw = bytes.fromhex(m.group(2).replace(" ", ""))
        if len(data) < at + len(raw):
            data.extend(bytes(at + len(raw) - len(data)))
        data[at:at + len(raw)] = raw
    if data:
        blocks.append(bytes(data))
    if not blocks:
        raise SystemExit("rom/tables: %s printed no data" % obj)
    return blocks


def rdata(obj, section):
    """And the one of those that holds the tables, which is the largest."""
    return max(sections(obj, section), key=len)


def one_table(obj, section, want, howmany):
    """One named table out of an object that keeps each in its own section.

    Picked by how long it is, since that is the only thing that tells the
    sections apart. Refuses unless the count is what was expected and every
    block of that length holds the same bytes, either of which failing means
    the object is not the one this was written against."""
    found = [b for b in sections(obj, section) if len(b) == want]
    if len(found) != howmany:
        raise SystemExit(
            "rom/tables: %s has %d sections of %d bytes in %s, wanted %d"
            % (obj, len(found), want, section, howmany))
    for b in found[1:]:
        if b != found[0]:
            raise SystemExit(
                "rom/tables: %s's sections of %d bytes in %s differ"
                % (obj, want, section))
    return found[0]


def headers(obj):
    """Every section of the object as (index, name, size, offset in file).

    The offset is what makes a COMDAT string literal readable: `objdump -t'
    gives a symbol's section number and nothing else, and several sections of
    one name and one size may hold different bytes, so the only exact way to
    the content is the file offset the header prints."""
    out = []
    for line in run("i686-w64-mingw32-objdump", "-h", obj).splitlines():
        m = re.match(r"^\s*(\d+)\s+(\S+)\s+([0-9a-f]+)\s+[0-9a-f]+\s+"
                     r"[0-9a-f]+\s+([0-9a-f]+)", line)
        if m:
            out.append((int(m.group(1)), m.group(2), int(m.group(3), 16),
                        int(m.group(4), 16)))
    if not out:
        raise SystemExit("rom/tables: %s printed no section headers" % obj)
    return out


def section_of(obj, symbol):
    """Which section a symbol is in, as an index into headers()."""
    for line in run("i686-w64-mingw32-objdump", "-t", obj).splitlines():
        if line.rstrip().endswith(symbol):
            m = re.search(r"\(sec\s+(-?\d+)\)", line)
            if m:
                return int(m.group(1)) - 1
    raise SystemExit("rom/tables: %s does not name %s" % (obj, symbol))


def literal(obj, symbol):
    """One string literal, read out of its own section by file offset."""
    idx = section_of(obj, symbol)
    for i, name, size, at in headers(obj):
        if i == idx:
            with open(obj, "rb") as f:
                f.seek(at)
                raw = f.read(size)
            if not raw.endswith(b"\0"):
                raise SystemExit(
                    "rom/tables: %s in %s is not terminated" % (symbol, obj))
            return raw[:-1]
    raise SystemExit("rom/tables: %s in %s is in no section" % (symbol, obj))


def relocs(obj, section):
    """Every relocation in that section, as offset -> symbol."""
    out = {}
    want = False
    for line in run("i686-w64-mingw32-objdump", "-r", obj).splitlines():
        if line.startswith("RELOCATION RECORDS FOR"):
            want = ("[%s]" % section) in line
            continue
        m = re.match(r"^([0-9a-f]+)\s+\S+\s+(\S+)$", line.strip())
        if want and m:
            out[int(m.group(1), 16)] = m.group(2)
    return out


def strings_at(obj, section, at, count):
    """An array of pointers to string literals, as the strings themselves."""
    rel = relocs(obj, section)
    out = []
    for i in range(count):
        where = at + i * 4
        if where not in rel:
            raise SystemExit(
                "rom/tables: %s has no relocation at 0x%x in %s"
                % (obj, where, section))
        name = rel[where]
        if not SKIP.match(name):
            raise SystemExit(
                "rom/tables: %s at 0x%x in %s is not a string literal"
                % (name, where, section))
        out.append(literal(obj, name))
    return out


def int32_at(obj, section, at):
    """One thirty-two bit number out of a section, little end first."""
    data = rdata(obj, section)
    if at + 4 > len(data):
        raise SystemExit("rom/tables: %s has no int at 0x%x in %s"
                         % (obj, at, section))
    return int.from_bytes(data[at:at + 4], "little", signed=True)


def symbol_pairs(obj, section, at, rel, data):
    """A run of number-and-string pairs, up to the one with no string.

    The terminating entry's number comes back too, as the last pair with None
    for its string. It is not always nought: the annotation table keeps minus
    one there, and the walk that looks a name up returns whatever the entry it
    stopped on holds."""
    out = []
    while True:
        where = at + len(out) * 8 + 4
        value = int.from_bytes(data[at + len(out) * 8:at + len(out) * 8 + 4],
                               "little", signed=True)
        if where not in rel:
            out.append((value, None))
            break
        out.append((value, literal(obj, rel[where])))
        if len(out) > 4096:
            raise SystemExit("rom/tables: %s at 0x%x does not end" % (obj, at))
    if len(out) < 2:
        raise SystemExit("rom/tables: %s at 0x%x is empty" % (obj, at))
    return out


def named_block(obj, section, want):
    """The one section of that name and that exact length."""
    found = [b for b in sections(obj, section) if len(b) == want]
    if len(found) != 1:
        raise SystemExit(
            "rom/tables: %s has %d sections of %d bytes in %s, wanted one"
            % (obj, len(found), want, section))
    return found[0]


def tables(obj):
    """Every table the object defines, as (name, offset), in order."""
    out = []
    for line in run("i686-w64-mingw32-nm", obj).splitlines():
        m = re.match(r"^([0-9a-f]+) ([DdRr]) (\S+)$", line.strip())
        if not m:
            continue
        raw = m.group(3)
        if SKIP.match(raw):
            continue
        name = MEMBER.match(raw) or GLOBAL.match(raw) or STATIC.match(raw)
        if not name:
            continue
        out.append((name.group(1), int(m.group(1), 16)))
    out.sort(key=lambda x: x[1])
    return out


def emit_block(f, name, block):
    """One object's whole read-only section, in one piece."""
    f.write("static const uint8_t %s[%d] __attribute__((aligned(8))) = {"
            % (name, len(block)))
    for i, b in enumerate(block):
        if i:
            f.write(",")
        if i % 16 == 0:
            f.write("\n    ")
        f.write("%d" % b)
    f.write("\n};\n\n")


def emit_header(out, tag, lines):
    """The declarations for what the block above defines, so that a
    transcription includes one file rather than repeating them."""
    guard = "ROM_TABLES_%s_H" % tag.upper()
    with open(out, "w") as f:
        f.write("/* What lang/%s/rom_tables_%s.c defines.\n"
                " *\n"
                " * Written by tools/rom/tables.py beside that file, so\n"
                " * that a table cannot be declared one way and defined\n"
                " * another. Each pointer is into its object's own block and\n"
                " * each length is that table's, in bytes.\n"
                " */\n\n"
                "#ifndef %s\n#define %s\n\n#include <stdint.h>\n\n"
                "/* What a symbol table's entries are: what the symbol\n"
                " * means and how it is written. */\n"
                "typedef struct { int32_t what; const char *how; }\n"
                "    %s_symbol;\n\n"
                % (tag, tag, guard, guard, tag))
        obj = None
        for one, name, n, kind in lines:
            if one != obj:
                f.write("%s/* %s */\n" % ("" if obj is None else "\n", one))
                obj = one
            if kind == "symbols":
                f.write("extern const %s_symbol %s_%s[];\n"
                        % (tag, tag, name))
            elif kind == "strings":
                f.write("extern const char *const %s_%s[];\n" % (tag, name))
            else:
                f.write("extern const uint8_t *const %s_%s;\n" % (tag, name))
            f.write("extern const int32_t %s_%s_n;\n" % (tag, name))
        f.write("\n#endif\n")


def emit_all(where, out, tag):
    total = 0
    lines = []
    # A constant the compiler put in more than one object comes out of each of
    # them, and the bytes are the same either way, so the first one wins and
    # the rest are passed over. Without this the file defines a name twice and
    # will not compile.
    seen = set()
    with open(out, "w") as f:
        f.write("/* The Japanese romanizer's tables.\n"
                " *\n"
                " * Lifted byte for byte out of IBM's objects by\n"
                " * tools/rom/tables.py rather than retyped, and the code\n"
                " * that reads them is transcribed separately and reads them\n"
                " * exactly as the original does.\n"
                " *\n"
                " * All but two of them are bytes and have no relocation\n"
                " * inside them, so they really are bytes. The two that are\n"
                " * not are arrays of pointers to string literals, and those\n"
                " * are lifted by following the relocations and reading each\n"
                " * literal out of the COMDAT section it has to itself, so\n"
                " * the strings are taken rather than retyped as well.\n"
                " *\n"
                " * Each object's tables come out as one block with a pointer\n"
                " * into it per table, rather than as an array each, because\n"
                " * they were one block in the original and its own code does\n"
                " * not always stay inside the table it started in: the two\n"
                " * lead-byte tables are shorter than the range of lead bytes\n"
                " * the converter accepts, and a packed record can run on\n"
                " * past the end of its table. Laid out this way, whatever\n"
                " * such a read finds is what IBM's found.\n"
                " */\n\n"
                "#include <stdint.h>\n\n"
                "typedef struct { int32_t what; const char *how; }\n"
                "    %s_symbol;\n\n" % tag)
        for obj, section, about in OBJECTS:
            path = os.path.join(where, obj)
            if not os.path.exists(path):
                raise SystemExit("rom/tables: no %s" % path)
            data = rdata(path, section)
            syms = tables(path)
            if not syms:
                raise SystemExit("rom/tables: %s names no tables" % obj)
            block = os.path.splitext(obj)[0] + "_tables"
            f.write("/* %s\n * %s\n */\n\n" % (obj, about))
            emit_block(f, block, data)
            for i, (name, at) in enumerate(syms):
                end = syms[i + 1][1] if i + 1 < len(syms) else len(data)
                if at >= end or end > len(data):
                    raise SystemExit(
                        "rom/tables: %s in %s runs from %d to %d of %d"
                        % (name, obj, at, end, len(data)))
                if name in seen:
                    continue
                seen.add(name)
                f.write("const uint8_t *const %s_%s = %s + 0x%x;\n"
                        % (tag, name, block, at))
                f.write("const int32_t %s_%s_n = %d;\n\n"
                        % (tag, name, end - at))
                total += end - at
                lines.append((obj, name, end - at, "bytes"))
            f.write("\n")

        for obj, about, wanted in PER_SECTION:
            path = os.path.join(where, obj)
            if not os.path.exists(path):
                raise SystemExit("rom/tables: no %s" % path)
            f.write("/* %s\n * %s\n */\n\n" % (obj, about))
            for name, section, want, howmany in wanted:
                block = os.path.splitext(obj)[0] + "_" + name
                emit_block(f, block, one_table(path, section, want, howmany))
                f.write("const uint8_t *const %s_%s = %s;\n"
                        % (tag, name, block))
                f.write("const int32_t %s_%s_n = %d;\n\n"
                        % (tag, name, want))
                total += want
                lines.append((obj, name, want, "bytes"))
            f.write("\n")

        for obj, section, size, about, wanted in NAMED:
            path = os.path.join(where, obj)
            if not os.path.exists(path):
                raise SystemExit("rom/tables: no %s" % path)
            data = named_block(path, section, size)
            block = os.path.splitext(obj)[0] + "_tables"
            f.write("/* %s\n * %s\n */\n\n" % (obj, about))
            emit_block(f, block, data)
            for name, at, n in wanted:
                if at + n > len(data):
                    raise SystemExit(
                        "rom/tables: %s in %s runs to %d of %d"
                        % (name, obj, at + n, len(data)))
                f.write("const uint8_t *const %s_%s = %s + 0x%x;\n"
                        % (tag, name, block, at))
                f.write("const int32_t %s_%s_n = %d;\n\n" % (tag, name, n))
                total += n
                lines.append((obj, name, n, "bytes"))
            f.write("\n")

        for obj, section, about, wanted in SYMBOL_TABLES:
            path = os.path.join(where, obj)
            if not os.path.exists(path):
                raise SystemExit("rom/tables: no %s" % path)
            data = rdata(path, section)
            rel = relocs(path, section)
            found = dict(tables(path))
            f.write("/* %s\n * %s\n */\n\n" % (obj, about))
            for name in wanted:
                if name not in found:
                    raise SystemExit("rom/tables: %s names no %s"
                                     % (obj, name))
                got = symbol_pairs(path, section, found[name], rel, data)
                f.write("const %s_symbol %s_%s[%d] = {\n"
                        % (tag, tag, name, len(got)))
                for value, one in got:
                    if one is None:
                        f.write("    { %d, 0 },\n" % value)
                    else:
                        f.write("    { %d, \"%s\" },\n"
                                % (value, "".join("\\x%02x" % b for b in one)))
                f.write("};\n")
                f.write("const int32_t %s_%s_n = %d;\n\n"
                        % (tag, name, len(got) - 1))
                total += sum(len(x) + 9 for _v, x in got if x is not None)
                lines.append((obj, name, len(got) - 1, "symbols"))
            f.write("\n")

        for obj, about, wanted in POINTER_TABLES:
            path = os.path.join(where, obj)
            if not os.path.exists(path):
                raise SystemExit("rom/tables: no %s" % path)
            f.write("/* %s\n * %s\n */\n\n" % (obj, about))
            for one, section, counts in POINTER_COUNTS:
                if one != obj:
                    continue
                named = dict((n, a) for n, a in counts)
                for name, sect, at, count in wanted:
                    if name not in named:
                        continue
                    said = int32_at(path, section, named[name])
                    if said != count:
                        raise SystemExit(
                            "rom/tables: %s says %s has %d entries, not %d"
                            % (obj, name, said, count))
            for name, section, at, count in wanted:
                got = strings_at(path, section, at, count)
                f.write("const char *const %s_%s[%d] = {\n" % (tag, name,
                                                               count))
                for one in got:
                    f.write("    \"%s\",\n" % one.decode("ascii"))
                f.write("};\n")
                f.write("const int32_t %s_%s_n = %d;\n\n"
                        % (tag, name, count))
                total += sum(len(x) + 1 for x in got)
                lines.append((obj, name, count, "strings"))
            f.write("\n")
    return lines, total


def main(argv):
    where = argv[0] if argv else os.path.join(ROOT, "analysis", "jajp")
    tag = os.path.basename(where.rstrip("/\\"))
    outdir = argv[1] if len(argv) > 1 else os.path.join(ROOT, "lang", tag)

    os.makedirs(outdir, exist_ok=True)
    out = os.path.join(outdir, "rom_tables_%s.c" % tag)
    lines, total = emit_all(where, out, tag)
    emit_header(os.path.join(outdir, "rom_tables_%s.h" % tag), tag, lines)

    by_obj = {}
    for obj, _, n, _kind in lines:
        by_obj[obj] = by_obj.get(obj, 0) + n
    print(", ".join("%s %d bytes" % (o, n) for o, n in sorted(by_obj.items())))
    print("%d tables, %d bytes" % (len(lines), total))
    print("written to %s and its header" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
