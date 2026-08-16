#!/usr/bin/env python3
"""Lift the language's statement table out of its generated link file.

The Delta machine is parameterised by this table rather than owning it: ten
entries, one per statement type the language declares, each naming the
type, the fields it has, how to reach each field in a record, what a fresh
one holds, and the names each field's values may be written as. Everything
in it is reached by pointer, so a copy of the bytes would be meaningless;
this walks it and writes it out as C.

The accessors are the one part that is code rather than data, and they are
all the same shape: an offset added to the record. They come out as one
function each.
"""

import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class Coff:
    """Just enough of an object file to walk a table in it."""

    def __init__(self, path):
        self.path = path
        self.section = {}     # number -> bytes
        self.secname = {}     # number -> name
        self.symbol = {}      # name -> (section, value)
        self.reloc = collections.defaultdict(dict)   # section -> off -> name
        self._sections()
        self._symbols()
        self._relocs()

    def _run(self, *args):
        return subprocess.run(["llvm-readobj"] + list(args) + [self.path],
                              check=True, capture_output=True,
                              text=True).stdout

    def _sections(self):
        text = self._run("--sections", "--section-data")
        number = None
        name = None
        data = bytearray()
        inside = False
        for line in text.splitlines():
            m = re.match(r"\s+Number: (\d+)", line)
            if m:
                if number is not None:
                    self.section[number] = bytes(data)
                    self.secname[number] = name
                number = int(m.group(1))
                data = bytearray()
                inside = False
                continue
            m = re.match(r"\s+Name: (\S+)", line)
            if m and number is not None and name != m.group(1):
                name = m.group(1)
            if "SectionData (" in line:
                inside = True
                continue
            if inside:
                # The offset is as wide as the section needs, so it is read
                # rather than counted on, and the bytes are placed by it.
                m = re.match(r"\s+([0-9A-F]{4,}): ((?:[0-9A-F ]{4}\s?)+)\|", line)
                if m:
                    at = int(m.group(1), 16)
                    raw = bytes.fromhex(m.group(2).replace(" ", ""))
                    if len(data) < at + len(raw):
                        data.extend(bytes(at + len(raw) - len(data)))
                    data[at:at + len(raw)] = raw
                else:
                    inside = False
        if number is not None:
            self.section[number] = bytes(data)
            self.secname[number] = name

    def _symbols(self):
        text = self._run("--symbols")
        name = value = section = None
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
                    self.symbol.setdefault(name, (section, value))
                name = None

    def _relocs(self):
        text = self._run("--relocations")
        section = None
        for line in text.splitlines():
            m = re.match(r"\s+Section \((\d+)\)", line)
            if m:
                section = int(m.group(1))
                continue
            m = re.match(r"\s+0x([0-9A-F]+) IMAGE_REL_I386_DIR32 (\S+)", line)
            if m and section is not None:
                self.reloc[section][int(m.group(1), 16)] = m.group(2)

    # ---- reading ------------------------------------------------------

    def at(self, name):
        return self.symbol[name]

    def word(self, section, off):
        b = self.section[section]
        return int.from_bytes(b[off:off + 4], "little", signed=True)

    def half(self, section, off):
        b = self.section[section]
        return int.from_bytes(b[off:off + 2], "little", signed=True)

    def byte(self, section, off):
        return self.section[section][off]

    def points_to(self, section, off):
        """The symbol a pointer-sized slot names, and what it adds to it."""
        who = self.reloc[section].get(off)
        if who is None:
            return None, 0
        return who, self.word(section, off)

    def string(self, name):
        section, value = self.symbol[name]
        data = self.section[section]
        end = data.index(b"\0", value)
        return data[value:end].decode("latin-1")


def c_string(s):
    out = []
    for ch in s:
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif 32 <= ord(ch) < 127:
            out.append(ch)
        else:
            out.append("\\%03o" % ord(ch))
    return '"%s"' % "".join(out)


def setter_shape(o, name):
    """(width, offset) for a writer, which copies one field into a record."""
    section, value = o.symbol[name]
    code = o.section[section][value:value + 24]
    if code[:4] != bytes((0x8b, 0x44, 0x24, 0x08)):     # movl 0x8(%esp), %eax
        return None
    i = 4
    if code[i:i + 3] == bytes((0x66, 0x8b, 0x00)):      # movw (%eax), %ax
        width, i = 2, i + 3
    elif code[i:i + 2] == bytes((0x8a, 0x00)):          # movb (%eax), %al
        width, i = 1, i + 2
    elif code[i:i + 2] == bytes((0x8b, 0x00)):          # movl (%eax), %eax
        width, i = 4, i + 2
    else:
        return None
    if code[i:i + 4] != bytes((0x8b, 0x4c, 0x24, 0x04)):  # movl 0x4(%esp),%ecx
        return None
    i += 4
    if width == 2 and code[i:i + 2] == bytes((0x66, 0x89)):
        i += 2
    elif width == 1 and code[i:i + 1] == bytes((0x88,)):
        i += 1
    elif width == 4 and code[i:i + 1] == bytes((0x89,)):
        i += 1
    else:
        return None
    modrm = code[i]
    i += 1
    if modrm in (0x01, 0x41, 0x81):        # (%ecx) / disp8 / disp32
        if modrm == 0x01:
            return width, 0
        if modrm == 0x41:
            return width, code[i]
        return width, int.from_bytes(code[i:i + 4], "little")
    return None


def accessor_offset(o, name):
    """What an accessor adds to the record it is given, or None."""
    section, value = o.symbol[name]
    code = o.section[section][value:value + 16]
    # movl 0x4(%esp), %eax
    if code[:4] != bytes((0x8b, 0x44, 0x24, 0x04)):
        return None
    rest = code[4:]
    if rest[:1] == b"\xc3":
        return 0
    if rest[:1] == b"\x40":                    # incl %eax
        return 1
    if rest[:2] == bytes((0x83, 0xc0)):        # addl $imm8, %eax
        return rest[2]
    if rest[:1] == b"\x05":                    # addl $imm32, %eax
        return int.from_bytes(rest[1:5], "little")
    return None


FIELD_AT = {0x18: "unknown_18", 0x1c: "unknown_1c", 0x20: "nfields",
            0x24: "length", 0x28: "stride", 0x2c: "varlen",
            0x30: "whole_token", 0x38: "unknown_38", 0x3c: "unknown_3c"}


def variant_sizes(o):
    """What viasizes writes into the table when the language starts.

    Two of the numbers in the table are not in the file at all: how big one
    variant of a statement type is. The language sets them itself, and a
    copy of the table that does not have them lays down a statement with no
    variant in it at all. """
    section, value = o.symbol["_viasizes"]
    code = o.section[section][value:value + 64]
    fixups = o.reloc[section]
    out = []
    acc = None
    held = []
    i = 0
    while i < len(code):
        if code[i] == 0x6a:                       # pushl $imm8
            held.append(code[i + 1])
            i += 2
        elif code[i] == 0x58:                     # popl %eax
            acc = held.pop() if held else None
            i += 1
        elif code[i] == 0xa3:                     # movl %eax, disp32
            who = fixups.get(value + i + 1)
            if who != "_vstmtbl" or acc is None:
                raise ValueError("viasizes writes somewhere unexpected")
            off = int.from_bytes(code[i + 1:i + 5], "little")
            out.append((off // 0x40, off % 0x40, acc))
            i += 5
        elif code[i] == 0xc3:                     # retl
            break
        else:
            raise ValueError("viasizes has a shape this cannot read")
    return out


def main():
    where = os.path.join(ROOT, "analysis", "enus")
    obj = os.path.join(where, "link.obj")
    out = os.path.join(ROOT, "src", "delta_link_enus.c")

    o = Coff(obj)
    sec, base = o.at("_vstmtbl")

    strings = {}          # symbol -> C name
    offsets = {}          # accessor symbol -> offset added
    accessors = []        # in the order they were met

    def string_name(sym):
        if sym not in strings:
            strings[sym] = "s%d" % len(strings)
        return strings[sym]

    def accessor(sym):
        if sym not in offsets:
            n = accessor_offset(o, sym)
            if n is None:
                raise ValueError("%s is not a plain accessor" % sym)
            offsets[sym] = n
            accessors.append(sym)
        return sym

    NSTMT = 10
    stmts = []
    for i in range(NSTMT):
        at = base + i * 0x40
        e = {}
        e["name"], _ = o.points_to(sec, at + 0x00)
        e["fields"] = o.points_to(sec, at + 0x04)
        e["get"] = o.points_to(sec, at + 0x08)
        e["put"] = o.points_to(sec, at + 0x0c)
        e["variants"] = o.points_to(sec, at + 0x10)
        e["deflt"] = o.points_to(sec, at + 0x14)
        for k, off in (("u18", 0x18), ("u1c", 0x1c), ("nfields", 0x20),
                       ("length", 0x24), ("stride", 0x28), ("varlen", 0x2c),
                       ("whole", 0x30), ("u38", 0x38), ("u3c", 0x3c)):
            e[k] = o.word(sec, at + off)
        e["marks"] = (o.byte(sec, at + 0x34), o.byte(sec, at + 0x35),
                      o.byte(sec, at + 0x36), o.byte(sec, at + 0x37))
        stmts.append(e)

    # The fields, the accessors and the names each field's values take.
    getters = {}
    setters = {}
    lines = []

    def blob(sym, addend, size):
        """A run of bytes a symbol names, as C initialisers.

        A run that was never written to has no bytes in the file at all, so
        anything the section is short of is nought."""
        section, value = o.symbol[sym]
        data = o.section[section][value + addend:value + addend + size]
        data = data + bytes(size - len(data))
        return ", ".join(str(b) for b in data)

    for i, e in enumerate(stmts):
        n = e["nfields"]
        fsec, fbase = o.symbol[e["fields"][0]]
        fbase += e["fields"][1]
        gsec, gbase = o.symbol[e["get"][0]]
        gbase += e["get"][1]
        psec, pbase = o.symbol[e["put"][0]]
        pbase += e["put"][1]

        e["field"] = []
        for k in range(n):
            at = fbase + k * 0x18
            f = {}
            f["name"], _ = o.points_to(fsec, at + 0x00)
            f["format"], _ = o.points_to(fsec, at + 0x04)
            f["values"] = o.points_to(fsec, at + 0x08)
            f["u0c"] = o.word(fsec, at + 0x0c)
            f["nvalues"] = o.half(fsec, at + 0x10)
            f["kind"] = o.half(fsec, at + 0x12)
            f["flag"] = o.byte(fsec, at + 0x14)
            f["get"] = o.points_to(gsec, gbase + k * 4)[0]
            f["put"] = o.points_to(psec, pbase + k * 4)[0]
            accessor(f["get"])
            if f["put"]:
                shape = setter_shape(o, f["put"])
                if shape is None:
                    raise ValueError("%s is not a plain writer" % f["put"])
                setters[f["put"]] = shape
            if f["name"]:
                string_name(f["name"])
            if f["format"]:
                string_name(f["format"])
            if f["values"][0]:
                vsec, vbase = o.symbol[f["values"][0]]
                vbase += f["values"][1]
                f["names"] = []
                for j in range(f["nvalues"]):
                    who, _ = o.points_to(vsec, vbase + j * 4)
                    f["names"].append(who)
                    if who:
                        string_name(who)
            else:
                f["names"] = None
            e["field"].append(f)
        string_name(e["name"])

    for sym in offsets:
        getters[sym] = offsets[sym]

    # How far a run of bytes reaches: to the next thing named in the same
    # section, or the end of it.
    bounds = collections.defaultdict(list)
    for nm, (sc, vl) in o.symbol.items():
        bounds[sc].append(vl)
    for sc in bounds:
        bounds[sc] = sorted(set(bounds[sc]))

    def extent(sym):
        sc, vl = o.symbol[sym]
        after = [v for v in bounds[sc] if v > vl]
        end = after[0] if after else len(o.section[sc])
        return end - vl

    with open(out, "w") as f:
        f.write("/* Generated by tools/delta-link.py. Do not edit.\n"
                "\n"
                "   The statement table the language declares, and\n"
                "   everything it points at: what each type is called, the\n"
                "   fields it has, how to reach one in a record, what a\n"
                "   fresh record holds, and the names each field's values\n"
                "   may be written as.\n"
                "\n"
                "   The readers and writers are the one part that was code.\n"
                "   Each is an offset into the record, so each comes out as\n"
                "   one function here. */\n\n")
        f.write("#include <string.h>\n\n#include \"delta.h\"\n\n")

        f.write("/* The names, in the order they were met. */\n")
        for sym, nm in strings.items():
            f.write("static const char %s[] = %s;\n"
                    % (nm, c_string(o.string(sym))))

        f.write("\n/* One reader per field: where it sits in the record. */\n")
        for sym in accessors:
            f.write("static void *g_%s(void *p) { return (char *)p + %d; }\n"
                    % (sym.lstrip("_"), offsets[sym]))

        f.write("\n/* And one writer, which is the same with a width. */\n")
        for sym, (width, off) in sorted(setters.items()):
            f.write("static void p_%s(void *p, const void *v)\n"
                    "{ memcpy((char *)p + %d, v, %d); }\n"
                    % (sym.lstrip("_"), off, width))

        for i, e in enumerate(stmts):
            f.write("\n/* %s */\n" % o.string(e["name"]))
            for k, fd in enumerate(e["field"]):
                if fd["names"] is None:
                    continue
                f.write("static const char *const v%d_%d[] = { %s };\n"
                        % (i, k, ", ".join(strings[x] if x else "0"
                                           for x in fd["names"])))
            f.write("static const delta_fielddesc f%d[] = {\n" % i)
            for fd in e["field"]:
                k = e["field"].index(fd)
                f.write("    { %s, %s, %s, %d, %d, %d, %d, { 0, 0, 0 } },\n"
                        % (strings[fd["name"]] if fd["name"] else "0",
                           strings[fd["format"]] if fd["format"] else "0",
                           ("v%d_%d" % (i, k)) if fd["names"] is not None
                           else "0",
                           fd["u0c"], fd["nvalues"], fd["kind"], fd["flag"]))
            f.write("};\n")
            f.write("static void *(*const gt%d[])(void *) = { %s };\n"
                    % (i, ", ".join("g_" + fd["get"].lstrip("_")
                                    for fd in e["field"])))
            f.write("static void (*const pt%d[])(void *, const void *)"
                    " = { %s };\n"
                    % (i, ", ".join("p_" + fd["put"].lstrip("_")
                                    for fd in e["field"])))
            f.write("static const uint8_t d%d[] = { %s };\n"
                    % (i, blob(e["deflt"][0], e["deflt"][1], e["length"])))
            if e["variants"][0]:
                n = extent(e["variants"][0]) - e["variants"][1]
                f.write("static const uint8_t n%d[] = { %s };\n"
                        % (i, blob(e["variants"][0], e["variants"][1], n)))

        f.write("\n/* Not const: the runtime writes two of the words in\n"
        "   each entry. */\n"
        "delta_stmt vstmtbl[] = {\n")
        for i, e in enumerate(stmts):
            f.write("    { %s, f%d, gt%d, pt%d, %s, d%d,\n"
                    "      %d, %d, %d, %d, %d, %d, %d, { %d, %d }, %d, 0,"
                    " %d, %d },\n"
                    % (strings[e["name"]], i, i, i,
                       ("n%d" % i) if e["variants"][0] else "0", i,
                       e["u18"], e["u1c"], e["nfields"], e["length"],
                       e["stride"], e["varlen"], e["whole"],
                       e["marks"][0], e["marks"][1], e["marks"][2],
                       e["u38"], e["u3c"]))
        f.write("};\n")

        writes = variant_sizes(o)
        f.write("\n/* Two of the numbers are not in the table as the file\n"
                "   holds it: how big one variant of a statement type is.\n"
                "   The language sets them when it starts, and a table\n"
                "   without them lays down a statement with no variant in\n"
                "   it at all. */\n"
                "void viasizes(void)\n{\n")
        for i, off, v in writes:
            f.write("    vstmtbl[%d].%s = %d;\n"
                    % (i, FIELD_AT.get(off, "unknown_%02x" % off), v))
        f.write("}\n")

    print("statement types: %d" % len(stmts))
    print("fields: %d" % sum(e["nfields"] for e in stmts))
    print("readers: %d, writers: %d" % (len(getters), len(setters)))
    print("strings: %d, value names: %d"
          % (len(strings),
             sum(len(f["names"] or ()) for e in stmts for f in e["field"])))
    print("written to %s" % os.path.relpath(out, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
