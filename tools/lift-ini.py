#!/usr/bin/env python3
"""Lift the settings the engine carries in its own image.

Every language module holds one of these: sections in square brackets, key
equals value lines under them, and a byte of 0xff on the end. Lines are
separated by a nought rather than a newline, which is why the reader in
src/eci_iniread.c stops on either, and it is why this is lifted byte for
byte rather than retyped -- the reader's arithmetic depends on the exact
separators.

It matters more than its size suggests. The section name is the language
written as numbers, which is what src/eci_getlangs.c answers
eciGetAvailableLanguages2 out of and what src/eci_state.c settles on when
the caller asks for no language in particular. Under it are the eight voice
presets and every phoneme the language declares, which src/eci_phonemes.c
reads at startup.

usage: lift-ini.py <tag> [objdir]        lift the blob and write the C
       lift-ini.py dump <tag> [objdir]   write the text out of the objects
       lift-ini.py regenerate <tag>      the C from the text against the tree
       lift-ini.py write <tag>           the C from the text, for real
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The names the original's compiler gave the two. Ours do not go under
# them any more: a program may have several language modules in it, and
# src/delta_lang.c joins their blobs into the one the reader opens.
INI = "?eciIni@@3QBDB"
SIZE = "?eciIniSize@@3HB"

# Two more things out of the same object, because they are the language's
# and not the runtime's. getLibraryName answers with a name only when it is
# asked about the one language this build has in it, and the name and the
# number it compares against are both spelled per language: English says
# "Static Engine ENU" and 0x10000, German "Static Engine DEU" and 0x40000.
# The function is otherwise the same code in every module.
LIBNAME = "?getLibraryName@EngineArray@@AAEPBDQBVLangIdentifier@@@Z"
LIBNAME_PREFIX = b"Static Engine "


def section_data(obj, want):
    """The bytes of the section holding a symbol, and where it sits."""
    text = subprocess.run(["llvm-readobj", "--sections", "--section-data",
                           "--symbols", obj],
                          check=True, capture_output=True, text=True).stdout

    # Symbols first: which section number, and the offset into it.
    name = value = section = None
    where = {}
    for line in text.splitlines():
        m = re.match(r"\s+Name: (\S+)", line)
        if m:
            name = m.group(1)
            continue
        m = re.match(r"\s+Value: (\d+)", line)
        if m:
            value = int(m.group(1))
            continue
        m = re.match(r"\s+Section: \S+ \((-?\d+)\)", line)
        if m:
            section = int(m.group(1))
            if name is not None and section > 0:
                where.setdefault(name, (section, value))
            name = None

    if want not in where:
        raise SystemExit("lift-ini: %s is not in %s" % (want, obj))
    wanted, at = where[want]

    # Then the bytes of that section.
    number = None
    data = bytearray()
    inside = False
    for line in text.splitlines():
        m = re.match(r"\s+Number: (\d+)", line)
        if m:
            if number == wanted and data:
                break
            number = int(m.group(1))
            data = bytearray()
            inside = False
            continue
        if "SectionData (" in line:
            inside = number == wanted
            continue
        if inside:
            m = re.match(r"\s+([0-9A-F]{4,}): ((?:[0-9A-F ]{4}\s?)+)\|", line)
            if m:
                off = int(m.group(1), 16)
                raw = bytes.fromhex(m.group(2).replace(" ", ""))
                if len(data) < off + len(raw):
                    data.extend(bytes(off + len(raw) - len(data)))
                data[off:off + len(raw)] = raw
            else:
                inside = False
    return bytes(data), at, where


def library(obj):
    """The name, and every language number getLibraryName answers for.

    Eight of the nine modules ask the one question: is the whole packed word
    this. Japanese asks two, because it is the only one with more than one
    dialect in a family -- family eight, dialects nought, four and eight, all
    three the same library. So the answer is a set, and one entry is the
    ordinary case rather than a special one."""
    text = subprocess.run(["llvm-objdump", "-d", "--no-show-raw-insn",
                           "--disassemble-symbols=" + LIBNAME, obj],
                          check=True, capture_output=True, text=True).stdout
    whole = re.findall(r"cmpl\s+\$(0x[0-9a-f]+), \(%eax\)", text)
    if len(whole) == 1:
        packed = {int(whole[0], 16)}
    else:
        family = re.findall(r"cmpl\s+\$(0x[0-9a-f]+), %eax", text)
        dialects = re.findall(r"cmpb\s+\$(0x[0-9a-f]+), -0x10\(%ebp\)", text)
        if len(family) != 1 or not dialects:
            raise SystemExit("lift-ini: getLibraryName asks a question this"
                             " does not know how to read")
        packed = {(int(family[0], 16) << 16) | int(d, 16) for d in dialects}

    raw = open(obj, "rb").read()
    at = raw.find(LIBNAME_PREFIX)
    if at < 0 or raw.find(LIBNAME_PREFIX, at + 1) >= 0:
        raise SystemExit("lift-ini: the library name is not in there once")
    name = raw[at:raw.index(b"\0", at)].decode("latin-1")
    return name, packed


def language_of(blob):
    """The language the settings declare, as the packed word."""
    for line in blob.replace(b"\0", b"\n").split(b"\n"):
        m = re.match(rb"^\[(\d+)\.(\d+)\]$", line)
        if m:
            return (int(m.group(1)) << 16) | int(m.group(2))
    raise SystemExit("lift-ini: no section names a language")


def main(argv):
    if not argv:
        raise SystemExit(__doc__.strip().splitlines()[-1])
    tag = argv[0]
    where = argv[1] if len(argv) > 1 else os.path.join(ROOT, "analysis", tag)
    obj = os.path.join(where, "engarray.obj")
    out = os.path.join(ROOT, "lang", tag, "eci_ini_%s.c" % tag)

    data, at, syms = section_data(obj, INI)
    if at != 0:
        raise SystemExit("lift-ini: the blob does not start its section")
    size_at = syms[SIZE][1]
    size = int.from_bytes(data[size_at:size_at + 4], "little")
    if not 0 < size <= size_at:
        raise SystemExit("lift-ini: %d is not a size this blob could have"
                         % size)
    blob = data[:size]
    if blob[-1] != 0xff and 0xff not in blob[-2:]:
        raise SystemExit("lift-ini: the blob does not end the way one does")

    # The two the engine array wants, and the check that they agree with the
    # settings: the number the code compares against has to be the language
    # the sections name, or one of the two was read wrong.
    name, packed = library(obj)
    declared = language_of(blob)
    if declared not in packed:
        raise SystemExit("lift-ini: the code answers for %s and the sections"
                         " name 0x%x"
                         % (", ".join("0x%x" % p for p in sorted(packed)),
                            declared))

    os.makedirs(os.path.dirname(out), exist_ok=True)
    emit(tag, blob, declared, name, out)

    print("%s: %d bytes, language 0x%x, %s"
          % (tag, size, declared, name))
    print("written to %s" % os.path.relpath(out, ROOT))
    return 0


def emit(tag, blob, declared, name, out):
    size = len(blob)
    with open(out, "w") as f:
        f.write("/* The engine's settings, built into the image.\n"
                " *\n"
                " * Sections in square brackets, key equals value lines under"
                " them, and a byte\n"
                " * of 0xff on the end. Lines are separated by a nought rather"
                " than a newline,\n"
                " * which is why the reader in eci_iniread.c stops on either.\n"
                " *\n"
                " * Lifted byte for byte out of the original by"
                " tools/lift-ini.py rather than\n"
                " * retyped, because the reader's arithmetic depends on the"
                " exact separators.\n"
                " */\n\n")
        f.write("#include <stdint.h>\n")
        f.write('#include "eci_synththread.h"\n')
        f.write('#include "evv_abi.h"\n\n')
        f.write("const char %s_eciIni[%d] = {\n" % (tag, size))
        for i in range(0, size, 16):
            f.write("    " + ", ".join("%d" % b for b in blob[i:i + 16])
                    + ",\n")
        f.write("};\n\n")
        f.write("const int32_t %s_eciIniSize = %d;\n\n" % (tag, size))

        f.write("/* Which language this build has in it, and what the engine\n"
                "   array calls the copy linked into the image. The original\n"
                "   spells both into getLibraryName, which answers with the\n"
                "   name only when it is asked about this language; here they\n"
                "   are data, so that src/eci_engarray.c is the same code\n"
                "   whichever language is built beside it. */\n")
        f.write("const int32_t %s_eci_library_lang = 0x%x;\n"
                % (tag, declared))
        f.write('const char %s_eci_library_name[] = "%s";\n' % (tag, name))



# ---- the same settings as text -----------------------------------------
#
# The blob is lines of text with a nought between them rather than a newline,
# so as text it is one line to a record. Three bytes in it are not text and
# each has a form of its own: a newline inside a record is written `\\n\',
# because IBM puts two of them in front of the section that names the
# language and the reader's arithmetic depends on them being there; a
# backslash is doubled, since the voice datasets hold Windows paths; and the
# nought, 0xff, nought the blob ends with is not in the text at all, because
# every one of the nine ends that way and the writer puts it back.
#
# The language number is not in the text either. The section that names it is,
# and the number is that section read as a family and a dialect, which is one
# source rather than two that can disagree.

TERMINATOR = b"\x00\xff\x00"


def text_path(tag):
    return os.path.join(ROOT, "lang", tag, "%s.settings" % tag)


def out_path(tag):
    return os.path.join(ROOT, "lang", tag, "eci_ini_%s.c" % tag)


def escape(rec):
    return (rec.decode("latin-1").replace("\\", "\\\\")
            .replace("\n", "\\n"))


def unescape(line):
    out = bytearray()
    i = 0
    while i < len(line):
        c = line[i]
        if c == "\\" and i + 1 < len(line):
            nxt = line[i + 1]
            if nxt == "n":
                out.append(0x0a)
            elif nxt == "\\":
                out.append(0x5c)
            else:
                raise SystemExit("lift-ini: \\%s is not an escape" % nxt)
            i += 2
            continue
        out.append(ord(c) & 0xff)
        i += 1
    return bytes(out)


def write_text(tag, blob, name, path):
    if not blob.endswith(TERMINATOR):
        raise SystemExit("lift-ini: the blob does not end the way one does")
    out = ["# The engine's settings for %s, as text." % tag,
           "#",
           "# One line to a record, which in the blob is a run of bytes with a",
           "# nought after it. A newline inside a record is `\\n\' and a",
           "# backslash is doubled; the three bytes the blob ends with are the",
           "# writer's and are not in here. Under the section that names the",
           "# language -- the family and the dialect, which is where the number",
           "# comes from -- are the eight voice presets and every phoneme the",
           "# language declares.",
           "#",
           "# Written by tools/lift-ini.py.",
           "",
           "library %s" % name,
           ""]
    for rec in blob[:-3].split(b"\x00"):
        out.append(escape(rec))
    open(path, "w").write("\n".join(out) + "\n")
    return len(blob[:-3].split(b"\x00"))


def read_text(path):
    """The blob and the library name back out of the text."""
    name = None
    recs = []
    started = False
    for raw in open(path):
        line = raw.rstrip("\n")
        if not started:
            if line.startswith("#") or not line.strip():
                continue
            if line.startswith("library "):
                name = line[len("library "):]
                started = True
                continue
            started = True
        if not started or not line:
            continue
        recs.append(unescape(line))
    if name is None:
        raise SystemExit("lift-ini: the text does not say the library name")
    return b"\x00".join(recs) + TERMINATOR, name


def dump(tag, where=None):
    where = where or os.path.join(ROOT, "analysis", tag)
    obj = os.path.join(where, "engarray.obj")
    data, at, syms = section_data(obj, INI)
    size = int.from_bytes(data[syms[SIZE][1]:syms[SIZE][1] + 4], "little")
    blob = data[:size]
    name, _packed = library(obj)
    n = write_text(tag, blob, name, text_path(tag))
    print("%d records and %d bytes in %s"
          % (n, size, os.path.relpath(text_path(tag), ROOT)))
    return True


def regenerate(tag, write=False):
    import tempfile
    blob, name = read_text(text_path(tag))
    declared = language_of(blob)
    if write:
        emit(tag, blob, declared, name, out_path(tag))
        print("%s written" % os.path.relpath(out_path(tag), ROOT))
        return True
    with tempfile.TemporaryDirectory() as tmp:
        made = os.path.join(tmp, "ini.c")
        emit(tag, blob, declared, name, made)
        got = open(made, "rb").read()
    want = open(out_path(tag), "rb").read()
    if got == want:
        print("%s: %d bytes, the same as the tree's, language 0x%x"
              % (os.path.basename(out_path(tag)), len(got), declared))
        return True
    print("%s differs: %d bytes against %d"
          % (os.path.basename(out_path(tag)), len(got), len(want)))
    a, b = got.split(b"\n"), want.split(b"\n")
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            print("  first line that differs is %d" % (i + 1))
            print("  made: %s" % a[i][:70])
            print("  tree: %s" % b[i][:70])
            break
    return False


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "dump":
        sys.exit(0 if dump(*sys.argv[2:]) else 1)
    if len(sys.argv) == 3 and sys.argv[1] == "regenerate":
        sys.exit(0 if regenerate(sys.argv[2]) else 1)
    if len(sys.argv) == 3 and sys.argv[1] == "write":
        sys.exit(0 if regenerate(sys.argv[2], write=True) else 1)
    sys.exit(main(sys.argv[1:]))
