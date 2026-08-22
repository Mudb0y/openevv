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

usage: lift-ini.py <tag> [objdir]
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
    """The name and the language number getLibraryName answers for."""
    text = subprocess.run(["llvm-objdump", "-d", "--no-show-raw-insn",
                           "--disassemble-symbols=" + LIBNAME, obj],
                          check=True, capture_output=True, text=True).stdout
    found = re.findall(r"cmpl\s+\$(0x[0-9a-f]+), \(%eax\)", text)
    if len(found) != 1:
        raise SystemExit("lift-ini: getLibraryName does not compare once")
    packed = int(found[0], 16)

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
    if packed != declared:
        raise SystemExit("lift-ini: the code says 0x%x and the sections 0x%x"
                         % (packed, declared))

    os.makedirs(os.path.dirname(out), exist_ok=True)
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
                % (tag, packed))
        f.write('const char %s_eci_library_name[] = "%s";\n' % (tag, name))

    print("%s: %d bytes, language 0x%x, %s"
          % (tag, size, packed, name))
    print("written to %s" % os.path.relpath(out, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
