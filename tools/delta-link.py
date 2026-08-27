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
        size = 0
        data = bytearray()
        inside = False

        def close():
            # A section that holds nothing but zeros carries no bytes at
            # all, so its length is the one it declares, not the one the
            # dump shows.
            if len(data) < size:
                data.extend(bytes(size - len(data)))
            self.section[number] = bytes(data)
            self.secname[number] = name

        for line in text.splitlines():
            m = re.match(r"\s+Number: (\d+)", line)
            if m:
                if number is not None:
                    close()
                number = int(m.group(1))
                data = bytearray()
                size = 0
                inside = False
                continue
            m = re.match(r"\s+RawDataSize: (\d+)", line)
            if m:
                size = int(m.group(1))
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
            close()

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
            0x30: "whole_token", 0x38: "gen_sel", 0x3c: "unknown_3c"}


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

NSTMT = 10


def model_of(o):
    """The statement table as data, with nothing of the object left in it.

    Everything the table points at comes out as a value here -- a name as its
    text, an accessor as the offset it adds, a fresh record as its bytes -- so
    that the writer below can be handed either this or the same thing read out
    of text, and cannot tell which.
    """
    sec, base = o.at("_vstmtbl")

    # How far a run of bytes reaches: to the next thing named in the same
    # section, or the end of it.
    bounds = collections.defaultdict(list)
    for _nm, (sc, vl) in o.symbol.items():
        bounds[sc].append(vl)
    for sc in bounds:
        bounds[sc] = sorted(set(bounds[sc]))

    def extent(sym):
        sc, vl = o.symbol[sym]
        after = [v for v in bounds[sc] if v > vl]
        return (after[0] if after else len(o.section[sc])) - vl

    def blob(sym, addend, size):
        """A run of bytes a symbol names. A run that was never written to has
        no bytes in the file at all, so anything the section is short of is
        nought."""
        section, value = o.symbol[sym]
        data = o.section[section][value + addend:value + addend + size]
        return data + bytes(size - len(data))

    stmts = []
    for i in range(NSTMT):
        at = base + i * 0x40
        e = {}
        name_sym, _ = o.points_to(sec, at + 0x00)
        e["name"] = o.string(name_sym)
        fields = o.points_to(sec, at + 0x04)
        get = o.points_to(sec, at + 0x08)
        put = o.points_to(sec, at + 0x0c)
        variants = o.points_to(sec, at + 0x10)
        deflt = o.points_to(sec, at + 0x14)
        for k, off in (("u18", 0x18), ("u1c", 0x1c), ("nfields", 0x20),
                       ("length", 0x24), ("stride", 0x28), ("varlen", 0x2c),
                       ("whole", 0x30), ("u38", 0x38), ("u3c", 0x3c)):
            e[k] = o.word(sec, at + off)
        e["marks"] = [o.byte(sec, at + 0x34), o.byte(sec, at + 0x35),
                      o.byte(sec, at + 0x36), o.byte(sec, at + 0x37)]

        fsec, fbase = o.symbol[fields[0]]
        fbase += fields[1]
        gsec, gbase = o.symbol[get[0]]
        gbase += get[1]
        psec, pbase = o.symbol[put[0]]
        pbase += put[1]

        e["field"] = []
        for k in range(e["nfields"]):
            fat = fbase + k * 0x18
            f = {}
            nm, _ = o.points_to(fsec, fat + 0x00)
            fmt, _ = o.points_to(fsec, fat + 0x04)
            values = o.points_to(fsec, fat + 0x08)
            f["name"] = o.string(nm) if nm else None
            f["format"] = o.string(fmt) if fmt else None
            f["u0c"] = o.word(fsec, fat + 0x0c)
            nvalues = o.half(fsec, fat + 0x10)
            f["kind"] = o.half(fsec, fat + 0x12)
            f["flag"] = o.byte(fsec, fat + 0x14)

            gsym = o.points_to(gsec, gbase + k * 4)[0]
            n = accessor_offset(o, gsym)
            if n is None:
                raise ValueError("%s is not a plain accessor" % gsym)
            f["read"] = n
            psym = o.points_to(psec, pbase + k * 4)[0]
            if psym:
                shape = setter_shape(o, psym)
                if shape is None:
                    raise ValueError("%s is not a plain writer" % psym)
                f["write"] = (shape[1], shape[0])       # offset, width
            else:
                f["write"] = None

            if values[0]:
                vsec, vbase = o.symbol[values[0]]
                vbase += values[1]
                names = []
                for j in range(nvalues):
                    who, _ = o.points_to(vsec, vbase + j * 4)
                    names.append(o.string(who) if who else None)
                f["values"] = names
            else:
                f["values"] = None
                if nvalues:
                    f["nvalues"] = nvalues
            e["field"].append(f)

        e["fresh"] = blob(deflt[0], deflt[1], e["length"])
        e["variants"] = (blob(variants[0], variants[1],
                              extent(variants[0]) - variants[1])
                         if variants[0] else None)
        stmts.append(e)

    return stmts, variant_sizes(o)


# ---- writing the C ------------------------------------------------------


def emit(stmts, sizes, out, tag):
    """One writer, whether the table came out of an object or out of text.

    The names are numbered in the order they are met and the readers and
    writers in the order the fields are, which is how the original's own
    compiler numbered them -- `vfg0000' upwards, one per field, no two fields
    sharing one across all 58 of English's. So nothing about the naming has to
    be carried anywhere: it follows from the order.
    """
    strings = {}

    def string_name(text):
        if text not in strings:
            strings[text] = "s%d" % len(strings)
        return strings[text]

    # The order the original met them in: each field's name, then its format,
    # then the names its values may take, and a statement's own name after its
    # fields rather than before them.
    for e in stmts:
        for f in e["field"]:
            if f["name"]:
                string_name(f["name"])
            if f["format"]:
                string_name(f["format"])
            for v in f["values"] or ():
                if v is not None:
                    string_name(v)
        string_name(e["name"])

    slot = 0
    for e in stmts:
        for f in e["field"]:
            f["_read"] = "vfg%04d" % slot
            f["_write"] = "vfp%04d" % slot if f["write"] else None
            slot += 1

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
        for text, nm in strings.items():
            f.write("static const char %s[] = %s;\n" % (nm, c_string(text)))

        f.write("\n/* One reader per field: where it sits in the record. */\n")
        for e in stmts:
            for fd in e["field"]:
                f.write("static void *g_%s(void *p)"
                        " { return (char *)p + %d; }\n"
                        % (fd["_read"], fd["read"]))

        f.write("\n/* And one writer, which is the same with a width. */\n")
        for e in stmts:
            for fd in e["field"]:
                if not fd["_write"]:
                    continue
                off, width = fd["write"]
                f.write("static void p_%s(void *p, const void *v)\n"
                        "{ memcpy((char *)p + %d, v, %d); }\n"
                        % (fd["_write"], off, width))

        for i, e in enumerate(stmts):
            f.write("\n/* %s */\n" % e["name"])
            for k, fd in enumerate(e["field"]):
                if fd["values"] is None:
                    continue
                f.write("static const char *const v%d_%d[] = { %s };\n"
                        % (i, k, ", ".join(strings[x] if x is not None else "0"
                                           for x in fd["values"])))
            f.write("static const delta_fielddesc f%d[] = {\n" % i)
            for k, fd in enumerate(e["field"]):
                f.write("    { %s, %s, %s, %d, %d, %d, %d, { 0, 0, 0 } },\n"
                        % (strings[fd["name"]] if fd["name"] else "0",
                           strings[fd["format"]] if fd["format"] else "0",
                           ("v%d_%d" % (i, k)) if fd["values"] is not None
                           else "0",
                           fd["u0c"],
                           len(fd["values"]) if fd["values"] is not None
                           else fd.get("nvalues", 0),
                           fd["kind"], fd["flag"]))
            f.write("};\n")
            f.write("static void *(*const gt%d[])(void *) = { %s };\n"
                    % (i, ", ".join("g_" + fd["_read"] for fd in e["field"])))
            f.write("static void (*const pt%d[])(void *, const void *)"
                    " = { %s };\n"
                    % (i, ", ".join(("p_" + fd["_write"]) if fd["_write"]
                                    else "0" for fd in e["field"])))
            f.write("static const uint8_t d%d[] = { %s };\n"
                    % (i, ", ".join(str(b) for b in e["fresh"])))
            if e["variants"] is not None:
                f.write("static const uint8_t n%d[] = { %s };\n"
                        % (i, ", ".join(str(b) for b in e["variants"])))

        f.write("\n/* Not const: the runtime writes two of the words in\n"
                "   each entry. The name carries the language, because a"
                " program\n"
                "   may have more than one module in it. */\n"
                "delta_stmt %s_vstmtbl[] = {\n" % tag)
        for i, e in enumerate(stmts):
            f.write("    { %s, f%d, gt%d, pt%d, %s, d%d,\n"
                    "      %d, %d, %d, %d, %d, %d, %d, { %d, %d }, %d, 0,"
                    " %d, %d },\n"
                    % (strings[e["name"]], i, i, i,
                       ("n%d" % i) if e["variants"] is not None else "0", i,
                       e["u18"], e["u1c"], len(e["field"]), e["length"],
                       e["stride"], e["varlen"], e["whole"],
                       e["marks"][0], e["marks"][1], e["marks"][2],
                       e["u38"], e["u3c"]))
        f.write("};\n")

        f.write("\n/* Two of the numbers are not in the table as the file\n"
                "   holds it: how big one variant of a statement type is.\n"
                "   The language sets them when it starts, and a table\n"
                "   without them lays down a statement with no variant in\n"
                "   it at all. */\n"
                "void %s_viasizes(void)\n{\n" % tag)
        for i, off, v in sizes:
            f.write("    %s_vstmtbl[%d].%s = %d;\n"
                    % (tag, i, FIELD_AT.get(off, "unknown_%02x" % off), v))
        f.write("}\n")

    return strings


# ---- the same table as text --------------------------------------------
#
# One thing to a line, verb first, and a block ends at a bare `end'. What is
# not in here is anything that follows from the order: the names are numbered
# as they are met and a field's reader and writer as the fields are, which is
# how the original's compiler numbered them, so a text that says the same
# things in the same order gets the same C.

HEAD = """# The statement table %(tag)s declares, as text.
#
# Ten types, each naming itself, the fields it has, where each field sits in a
# record, how a fresh record starts, and the names a field's values may be
# written as. The machine is parameterised by this rather than owning it.
#
# `numbers' is the four words in an entry nobody has named yet, in the order
# they sit: 0x18, 0x1c, 0x38, 0x3c. `at start' is what the language writes into
# the table when it starts, which is the one thing in there that is not in the
# file. A field's `where' is the offset its reader adds, then the offset and
# the width its writer copies with, and a width of nought is a field with no
# writer.
#
# A field's value names are one to a line, in order, because they are what the
# alphabets are made of and one of English's is a space and another a
# backslash: those two are written `\\s' and `\\\\', and a line saying `hole'
# is an entry the table leaves empty rather than a name.
#
# Written by tools/delta-link.py.
"""

BY_NAME = dict((v, k) for k, v in FIELD_AT.items())


def write_text(stmts, sizes, path, tag):
    out = [HEAD % {"tag": tag}]
    at_start = collections.defaultdict(list)
    for i, off, v in sizes:
        at_start[i].append((off, v))

    def hexes(word, data, out):
        # Thirty-two bytes to a line, and a line of its own for each run of
        # them, because a variant table is a few hundred bytes and one line
        # of that is not a line anybody can read or hear.
        for i in range(0, len(data), 32):
            out.append("  %s %s"
                       % (word, " ".join("%02x" % b for b in data[i:i + 32])))

    for i, e in enumerate(stmts):
        out.append("")
        out.append("statement %s" % e["name"])
        out.append("  length %d" % e["length"])
        out.append("  stride %d" % e["stride"])
        out.append("  varlen %d" % e["varlen"])
        out.append("  whole %d" % e["whole"])
        out.append("  marks %d %d %d %d" % tuple(e["marks"]))
        out.append("  numbers %d %d %d %d"
                   % (e["u18"], e["u1c"], e["u38"], e["u3c"]))
        for off, v in at_start[i]:
            out.append("  at start %s %d"
                       % (FIELD_AT.get(off, "unknown_%02x" % off), v))
        if e["fresh"]:
            hexes("fresh", e["fresh"], out)
        if e["variants"] is not None:
            if not e["variants"]:
                out.append("  variants")
            hexes("variants", e["variants"], out)
        for fd in e["field"]:
            out.append("  field %s" % (fd["name"] or "-"))
            off, width = fd["write"] or (0, 0)
            out.append("    where %d %d %d" % (fd["read"], off, width))
            out.append("    what %d %d %d"
                       % (fd["kind"], fd["flag"], fd["u0c"]))
            if fd["format"]:
                out.append("    format %s" % fd["format"])
            if fd["values"] is not None:
                for v in fd["values"]:
                    if v is None:
                        out.append("    hole")
                    else:
                        out.append("    value %s"
                                   % v.replace("\\", "\\\\")
                                   .replace(" ", "\\s"))
            elif fd.get("nvalues"):
                out.append("    count %d" % fd["nvalues"])
            out.append("    end")
        out.append("end")
    open(path, "w").write("\n".join(out) + "\n")
    return sum(len(e["field"]) for e in stmts)


def read_text(path):
    """The table and what the language writes into it at startup."""
    stmts = []
    sizes = []
    e = None
    fd = None
    for n, raw in enumerate(open(path), 1):
        # A value's own text is whatever follows the word, so a line of one
        # keeps every character it has: an alphabet holds a hash as readily
        # as anything else, and a comment cannot start inside one.
        line = raw.rstrip("\r\n")
        if line.strip().split(" ")[:1] == ["value"]:
            line = line.strip("\t ")
        else:
            line = line.split("#")[0].strip()
        if not line:
            continue
        w = line.split()
        where = "%s line %d" % (os.path.basename(path), n)
        head = w[0]

        if head == "statement":
            e = {"name": " ".join(w[1:]), "field": [], "variants": None,
                 "fresh": b"", "u18": 0, "u1c": 0, "u38": 0, "u3c": 0,
                 "length": 0, "stride": 0, "varlen": 0, "whole": 0,
                 "marks": [0, 0, 0, 0]}
            stmts.append(e)
        elif head == "end":
            if fd is not None:
                fd = None
            else:
                e = None
        elif e is None:
            raise SystemExit("%s: %r is outside a statement" % (where, head))
        elif fd is not None:
            if head == "where":
                fd["read"] = int(w[1])
                fd["write"] = (int(w[2]), int(w[3])) if int(w[3]) else None
            elif head == "what":
                fd["kind"], fd["flag"], fd["u0c"] = (int(w[1]), int(w[2]),
                                                     int(w[3]))
            elif head == "format":
                fd["format"] = " ".join(w[1:])
            elif head in ("value", "hole"):
                if fd["values"] is None:
                    fd["values"] = []
                if head == "hole":
                    fd["values"].append(None)
                else:
                    text = line[len("value"):]
                    fd["values"].append(
                        (text[1:] if text.startswith(" ") else text)
                        .replace("\\s", " ").replace("\\\\", "\\"))
            elif head == "count":
                fd["nvalues"] = int(w[1])
            else:
                raise SystemExit("%s: no field line called %r"
                                 % (where, head))
        elif head == "field":
            fd = {"name": None if w[1] == "-" else " ".join(w[1:]),
                  "format": None, "values": None, "u0c": 0, "kind": 0,
                  "flag": 0, "read": 0, "write": None}
            e["field"].append(fd)
        elif head in ("length", "stride", "varlen", "whole"):
            e[head] = int(w[1])
        elif head == "marks":
            e["marks"] = [int(x) for x in w[1:5]]
        elif head == "numbers":
            e["u18"], e["u1c"], e["u38"], e["u3c"] = (int(w[1]), int(w[2]),
                                                      int(w[3]), int(w[4]))
        elif head == "at" and w[1:2] == ["start"]:
            if w[2] not in BY_NAME:
                raise SystemExit("%s: nothing in an entry is called %r"
                                 % (where, w[2]))
            sizes.append((len(stmts) - 1, BY_NAME[w[2]], int(w[3])))
        elif head == "fresh":
            e["fresh"] += bytes(int(x, 16) for x in w[1:])
        elif head == "variants":
            if e["variants"] is None:
                e["variants"] = b""
            e["variants"] += bytes(int(x, 16) for x in w[1:])
        else:
            raise SystemExit("%s: no statement line called %r"
                             % (where, head))
    return stmts, sizes


def text_path(tag):
    return os.path.join(ROOT, "lang", tag, "%s.statements" % tag)


def out_path(tag):
    return os.path.join(ROOT, "lang", tag, "delta_link_%s.c" % tag)


def dump(tag, where=None):
    where = where or os.path.join(ROOT, "analysis", tag)
    stmts, sizes = model_of(Coff(os.path.join(where, "link.obj")))
    n = write_text(stmts, sizes, text_path(tag), tag)
    print("%d statement types and %d fields in %s"
          % (len(stmts), n, os.path.relpath(text_path(tag), ROOT)))
    return True


def regenerate(tag, write=False):
    import tempfile
    stmts, sizes = read_text(text_path(tag))
    if write:
        emit(stmts, sizes, out_path(tag), tag)
        print("%s written" % os.path.relpath(out_path(tag), ROOT))
        return True
    with tempfile.TemporaryDirectory() as tmp:
        made = os.path.join(tmp, "link.c")
        emit(stmts, sizes, made, tag)
        got = open(made, "rb").read()
    want = open(out_path(tag), "rb").read()
    if got == want:
        print("%s: %d bytes, the same as the tree's"
              % (os.path.basename(out_path(tag)), len(got)))
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


def main(argv=()):
    # One language to a library, so the directory the objects were unpacked
    # into is what says which language this is, and the written file is
    # named for it. With no arguments it is US English, as it always was.
    tag = argv[0] if argv else "enus"
    where = argv[1] if len(argv) > 1 else os.path.join(ROOT, "analysis", tag)
    out = out_path(tag)
    os.makedirs(os.path.dirname(out), exist_ok=True)

    stmts, sizes = model_of(Coff(os.path.join(where, "link.obj")))
    strings = emit(stmts, sizes, out, tag)

    print("statement types: %d" % len(stmts))
    print("fields: %d" % sum(len(e["field"]) for e in stmts))
    print("readers: %d, writers: %d"
          % (sum(len(e["field"]) for e in stmts),
             sum(1 for e in stmts for f in e["field"] if f["write"])))
    print("strings: %d, value names: %d"
          % (len(strings),
             sum(len(f["values"] or ()) for e in stmts for f in e["field"])))
    print("written to %s" % os.path.relpath(out, ROOT))
    return 0


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "dump":
        sys.exit(0 if dump(*sys.argv[2:]) else 1)
    if len(sys.argv) == 3 and sys.argv[1] == "regenerate":
        sys.exit(0 if regenerate(sys.argv[2]) else 1)
    if len(sys.argv) == 3 and sys.argv[1] == "write":
        sys.exit(0 if regenerate(sys.argv[2], write=True) else 1)
    sys.exit(main(sys.argv[1:]))
