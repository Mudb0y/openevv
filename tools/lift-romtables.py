#!/usr/bin/env python3
"""Lift the romanizer's own tables out of the two objects that are all table.

What this is. Beside the static dictionary that tools/lift-rom.py takes, the
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

usage: lift-romtables.py [objdir] [outdir]
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

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


def rdata(obj, section):
    """The object's table section, as one block of bytes.

    Only the section that holds the tables is wanted. An object may have
    several sections of one name -- a two-byte COMDAT for a string literal
    beside the large one -- and objdump prints them all under that name, so
    the largest is the one meant."""
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
        raise SystemExit("lift-romtables: %s printed no data" % obj)
    return max(blocks, key=len)


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
                " * Written by tools/lift-romtables.py beside that file, so\n"
                " * that a table cannot be declared one way and defined\n"
                " * another. Each pointer is into its object's own block and\n"
                " * each length is that table's, in bytes.\n"
                " */\n\n"
                "#ifndef %s\n#define %s\n\n#include <stdint.h>\n\n"
                % (tag, tag, guard, guard))
        obj = None
        for one, name, n in lines:
            if one != obj:
                f.write("%s/* %s */\n" % ("" if obj is None else "\n", one))
                obj = one
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
                " * tools/lift-romtables.py rather than retyped. Neither\n"
                " * object has a relocation inside its data, so these really\n"
                " * are bytes, and the code that reads them is transcribed\n"
                " * separately and reads them exactly as the original does.\n"
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
                "#include <stdint.h>\n\n")
        for obj, section, about in OBJECTS:
            path = os.path.join(where, obj)
            if not os.path.exists(path):
                raise SystemExit("lift-romtables: no %s" % path)
            data = rdata(path, section)
            syms = tables(path)
            if not syms:
                raise SystemExit("lift-romtables: %s names no tables" % obj)
            block = os.path.splitext(obj)[0] + "_tables"
            f.write("/* %s\n * %s\n */\n\n" % (obj, about))
            emit_block(f, block, data)
            for i, (name, at) in enumerate(syms):
                end = syms[i + 1][1] if i + 1 < len(syms) else len(data)
                if at >= end or end > len(data):
                    raise SystemExit(
                        "lift-romtables: %s in %s runs from %d to %d of %d"
                        % (name, obj, at, end, len(data)))
                if name in seen:
                    continue
                seen.add(name)
                f.write("const uint8_t *const %s_%s = %s + 0x%x;\n"
                        % (tag, name, block, at))
                f.write("const int32_t %s_%s_n = %d;\n\n"
                        % (tag, name, end - at))
                total += end - at
                lines.append((obj, name, end - at))
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
    for obj, _, n in lines:
        by_obj[obj] = by_obj.get(obj, 0) + n
    print(", ".join("%s %d bytes" % (o, n) for o, n in sorted(by_obj.items())))
    print("%d tables, %d bytes" % (len(lines), total))
    print("written to %s and its header" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
