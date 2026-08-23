#!/usr/bin/env python3
"""Lift the Japanese romanizer's static dictionary out of its objects.

What this is. Japanese is written in a script the engine cannot speak, so its
module carries a romanizer: the thing that is jpnrom.dll in stock Eloquence.
Most of that romanizer by volume is a dictionary -- kanji and their readings,
single-kanji forms, normalisations, an English table, and the substitution
tables that make romaji -- and it is held as forty-eight objects of packed
records with one class, StaticDict, holding pointers into them.

None of the record format has to be understood to lift it. The records are
bytes, the way lang/<lang> is bytes, and the code that reads them is
transcribed separately and reads them exactly as the original does. What does
have to be understood is which pointer goes where, and StaticDict::Initialize
says that: six and a half thousand unrolled stores, each one an array, an index
and a string, all three named by relocations. So the arrays are read off rather
than guessed at, the same way tools/gen-globals.py reads the variable area off
delta_new.

usage: lift-rom.py [objdir] [outdir]
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# A static member of StaticDict holding one blob of records, and the arrays of
# pointers into them that Initialize fills in.
BLOB = re.compile(r"\?(s_sz[A-Za-z]+\d+)@StaticDict@@")
ARRAY = re.compile(r"\?(s_apsz[A-Za-z]+)@StaticDict@@")

# One store of Initialize: an immediate into a displacement, with the array
# named by the first relocation and the string by the second.
STORE = re.compile(r"^\s+([0-9a-f]+):\s+movl\s+\$0x0,0x([0-9a-f]+)\s*$")
RELOC = re.compile(r"^\s+([0-9a-f]+):\s+dir32\s+(\S+)\s*$")


def run(*args):
    return subprocess.run(args, capture_output=True, text=True,
                          check=True).stdout


def symbols(obj):
    """Every StaticDict blob this object defines, as (name, section, offset)."""
    out = []
    for line in run("i686-w64-mingw32-nm", obj).splitlines():
        m = re.match(r"^([0-9a-f]+) ([RD]) (\S+)$", line.strip())
        if not m:
            continue
        b = BLOB.search(m.group(3))
        if b:
            out.append((b.group(1), int(m.group(1), 16)))
    out.sort(key=lambda x: x[1])
    return out


def section_bytes(obj):
    """The read-only data of an object, as one block of bytes.

    Both .rdata and .data are looked at because the objects are not consistent
    about which one they use, and no object here has both."""
    for name in (".rdata", ".data"):
        try:
            text = run("i686-w64-mingw32-objdump", "-s", "-j", name, obj)
        except subprocess.CalledProcessError:
            continue
        data = bytearray()
        for line in text.splitlines():
            m = re.match(r"^\s+([0-9a-f]+)\s+((?:[0-9a-f]{2,8}\s+){1,4})", line)
            if not m:
                continue
            at = int(m.group(1), 16)
            raw = bytes.fromhex(m.group(2).replace(" ", ""))
            if len(data) < at + len(raw):
                data.extend(bytes(at + len(raw) - len(data)))
            data[at:at + len(raw)] = raw
        if data:
            return bytes(data)
    return b""


def blobs(where):
    """Every blob in the module, by name, as its bytes."""
    out = {}
    for f in sorted(os.listdir(where)):
        if not f.endswith(".obj"):
            continue
        obj = os.path.join(where, f)
        syms = symbols(obj)
        if not syms:
            continue
        data = section_bytes(obj)
        if not data:
            raise SystemExit("lift-rom: %s names blobs and holds no data" % f)
        for i, (name, at) in enumerate(syms):
            end = syms[i + 1][1] if i + 1 < len(syms) else len(data)
            if at >= end or end > len(data):
                raise SystemExit("lift-rom: %s in %s runs from %d to %d of %d"
                                 % (name, f, at, end, len(data)))
            out[name] = data[at:end]
    return out


def arrays(obj):
    """What Initialize puts where: {array: {index: blob name}}."""
    text = run("i686-w64-mingw32-objdump", "-d", "-r",
               "--no-show-raw-insn", obj)
    inside = False
    out = {}
    at = None
    seen = []

    def flush():
        if at is None or len(seen) != 2:
            return
        a = ARRAY.search(seen[0])
        b = BLOB.search(seen[1])
        if not a or not b:
            raise SystemExit("lift-rom: a store names %r and %r" % tuple(seen))
        if at % 4:
            raise SystemExit("lift-rom: an index at byte %d is not a slot" % at)
        out.setdefault(a.group(1), {})[at // 4] = b.group(1)

    for line in text.splitlines():
        if "<?Initialize@StaticDict@@SAXXZ>:" in line:
            inside = True
            continue
        if not inside:
            continue
        if re.match(r"^[0-9a-f]+ <", line):
            break
        m = STORE.match(line)
        if m:
            flush()
            at = int(m.group(2), 16)
            seen = []
            continue
        r = RELOC.match(line)
        if r and at is not None:
            seen.append(r.group(2))
            continue
        if line.strip() and not RELOC.match(line):
            # Anything that is not a store or its relocations ends the run:
            # the prologue and the return are the only such lines, and a third
            # kind would mean this is not the shape it is being read as.
            flush()
            at = None
            seen = []
    flush()
    return out


def emit(data, table, out, tag):
    n = sum(len(v) for v in data.values())
    with open(out, "w") as f:
        f.write("/* The Japanese romanizer's static dictionary.\n"
                " *\n"
                " * Kanji and their readings, single-kanji forms,"
                " normalisations, an English\n"
                " * table and the substitution tables that make romaji."
                " Packed records, lifted\n"
                " * byte for byte out of IBM's objects by tools/lift-rom.py"
                " rather than retyped:\n"
                " * the code that reads them is transcribed separately and"
                " reads them exactly as\n"
                " * the original does, so nothing here has to be understood"
                " to be right.\n"
                " *\n"
                " * The arrays at the end are what StaticDict::Initialize"
                " builds, read off its\n"
                " * own relocations rather than assumed.\n"
                " */\n\n")
        f.write("#include <stdint.h>\n\n")

        for name in sorted(data):
            b = data[name]
            f.write("static const uint8_t %s_%s[%d] = {\n" % (tag, name,
                                                              len(b)))
            for i in range(0, len(b), 16):
                f.write("    " + ",".join("%d" % c for c in b[i:i + 16])
                        + ",\n")
            f.write("};\n\n")

        for arr in sorted(table):
            slots = table[arr]
            top = max(slots) + 1
            f.write("const uint8_t *const %s_%s[%d] = {\n" % (tag, arr, top))
            for i in range(top):
                nm = slots.get(i)
                f.write("    %s,\n" % (("%s_%s" % (tag, nm)) if nm else "0"))
            f.write("};\n")
            f.write("const int32_t %s_%s_n = %d;\n\n" % (tag, arr, top))

    print("%d blobs, %d bytes; %d arrays: %s"
          % (len(data), n, len(table),
             ", ".join("%s[%d]" % (a, max(table[a]) + 1)
                       for a in sorted(table))))
    print("written to %s" % os.path.relpath(out, ROOT))


def main(argv):
    where = argv[0] if argv else os.path.join(ROOT, "analysis", "jajp")
    tag = os.path.basename(where.rstrip("/\\"))
    outdir = argv[1] if len(argv) > 1 else os.path.join(ROOT, "lang", tag)

    data = blobs(where)
    if not data:
        raise SystemExit("lift-rom: no static dictionary in %s" % where)
    table = arrays(os.path.join(where, "jpnsdict.obj"))
    if not table:
        raise SystemExit("lift-rom: Initialize said nothing")

    missing = sorted({n for slots in table.values() for n in slots.values()}
                     - set(data))
    if missing:
        raise SystemExit("lift-rom: %d blobs named and not found, first %s"
                         % (len(missing), missing[0]))

    os.makedirs(outdir, exist_ok=True)
    emit(data, table, os.path.join(outdir, "rom_dict_%s.c" % tag), tag)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
