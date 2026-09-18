#!/usr/bin/env python3
"""Lift a language's statement table out of an Eloquence-family .so
(ETI/Code Factory or Apple), where the machine's tables survive as
data even though the rules were compiled to native code.

The table pointers export with the same names the IBM objects' did
(vstmtbl, viasizes, and the getter and putter arrays), so the walk in
tools/module/link.py reads them unchanged. What differs from the COFF
case is only the object behind it: an ELF, with pointers already
resolved, and accessors compiled to ARM rather than x86.

Usage:
    tools/module/elflink.py dump <lib.so> [tag]
    tools/module/elflink.py write <lib.so> [tag]

`dump` prints the model read out of the library; `write` writes the
same generated link file link.py would, named for the tag given
(default: the library's own language name, or `enus`). Importers may
call model_of/lift directly for the raw model.
"""

import collections
import os
import re
import struct
import subprocess
import sys

from capstone import (Cs, CS_ARCH_ARM, CS_ARCH_ARM64,
                      CS_MODE_ARM, CS_MODE_THUMB)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, ROOT)

import tools.module.link as link  # noqa: E402


class Elf:
    """Just enough of an ELF to walk a table in it, in link.py's voice.

    The COFF loader read relocations to find what a pointer slot named.
    A linked ELF has no relocations to read: the slot holds the target's
    address already, absolute or (in a position-independent library) as a
    RELATIVE relocation addend. So a symbol here is an address, and a
    "section" is the run of bytes that address lives in.

    Both the ARM32 (ETI/Code Factory) and AArch64 (Apple) builds work.
    The two keep their tables to different layouts -- pointer width, the
    stride of a record, and where in it each part sits -- which link.py
    names as LAYOUT_COFF and LAYOUT_APPLE64; an Elf declares its pointer
    width through _is64 and model_of picks the layout to match.
    """

    def __init__(self, path):
        self.path = path
        self.section = {}       # name -> bytes
        self.sections = []      # (name, va, size) in order
        self.symbol = {}        # name -> (secname, offset-in-run)
        self.rel = {}           # va -> target va (RELATIVE relocs)
        self._is64 = False
        self._rela = False
        self._load()

    def _run(self, *args):
        return subprocess.run(["readelf", *args, self.path],
                              capture_output=True, text=True).stdout

    def _load(self):
        # ELF class (32 vs 64) decides pointer width and relocation shape.
        with open(self.path, "rb") as f:
            ident = f.read(5)
            self._is64 = ident[4:5] == b"\x02"
        ptr = 8 if self._is64 else 4

        # Section headers: names, addresses, sizes, file offsets.
        out = self._run("-SW")
        sec = {}
        for m in re.finditer(
                r"\[\s*(\d+)\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)"
                r"\s+([0-9a-f]+)", out):
            _, name, va, off, size = m.groups()
            if name in (".text", ".rodata", ".data.rel.ro",
                        ".data.rel.ro.local", ".data", ".bss",
                        ".m2e_text", ".m2e_data", ".m2e_data_const",
                        ".m2e_cstring", ".m2e_bss", ".m2e_common",
                        ".m2e_text_const"):
                va_i, off_i, size_i = int(va, 16), int(off, 16), int(size, 16)
                self.sections.append((name, va_i, size_i))
                if name in (".bss", ".m2e_bss", ".m2e_common"):
                    self.section[name] = b"\0" * size_i
                else:
                    with open(self.path, "rb") as f:
                        f.seek(off_i)
                        self.section[name] = f.read(size_i)

        # RELATIVE relocations: at va, the value is base + addend, so the
        # addend IS the target address for a base of zero. REL (ARM32)
        # carries the addend in the slot itself; RELA (AArch64) carries
        # it in the record.
        out = self._run("-rW")
        # The RELATIVE lines begin flush left; the table's header and the
        # section lines ('.rela.dyn at offset...') start with a letter and
        # fall out of `[0-9a-f]+`.
        rel_line = re.compile(
            r"^\s*([0-9a-f]+)\s+[0-9a-f]*\s*R_(?:ARM|AARCH64)_RELATIVE"
            r"(?:\s+([0-9a-f-]+))?\s*$")
        for line in out.splitlines():
            m = rel_line.match(line)
            if not m:
                continue
            rva = int(m.group(1), 16)
            if m.group(2):
                self.rel[rva] = int(m.group(2), 16)
            else:
                self.rel[rva] = None  # REL: the slot itself holds the addend
        self._rela = "R_AARCH64_RELATIVE" in out

        # Dynamic symbols: name -> address.
        out = self._run("-sW")
        for line in out.splitlines():
            f = line.split()
            if len(f) < 8 or f[5] == "UND" or f[1] == "Value":
                continue
            va = int(f[1], 16)
            name = f[7].split("@")[0]
            if not name or not name[0].isalpha():
                continue
            try:
                self.symbol.setdefault(name, self._loc(va))
            except ValueError:
                pass  # addresses outside the data sections are code

        # The walker looks for the underscore-prefixed names IBM's
        # objects carried. Alias the stripped versions to them.
        for stem in ("vstmtbl", "viasizes"):
            if "_" + stem not in self.symbol and stem in self.symbol:
                self.symbol["_" + stem] = self.symbol[stem]

        # Where the strings live, to tell a real pointer from one into
        # the value-name pool that follows the table.
        self._str_secs = frozenset(n for n in self.section if "cstr" in n)

        # viasizes may be a function, not a table; keep its address.
        self._viasizes_va = None
        for stem in ("viasizes", "_viasizes"):
            if stem in self.symbol:
                sec, off = self.symbol[stem]
                self._viasizes_va = self._sec_va(sec) + off

    # ---- address helpers ------------------------------------------------

    def _sec_va(self, name):
        for nm, va, _ in self.sections:
            if nm == name:
                return va
        return None

    def _loc(self, va):
        """The (section, offset) an address lives in."""
        for name, sec_va, size in self.sections:
            if sec_va <= va < sec_va + size:
                return name, va - sec_va
        raise ValueError("address %08x in no section" % va)

    def va_of(self, sec, off):
        return self._sec_va(sec) + off

    def _read_ptr(self, sec, off):
        """Read a pointer-sized slot: 4 or 8 bytes per the ELF class."""
        b = self.section[sec][off:off + (8 if self._is64 else 4)]
        return int.from_bytes(b, "little", signed=True)

    # ---- link.py's object interface ------------------------------------

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
        """What a pointer-sized slot names: the target, and the addend.

        The slot is either a RELATIVE relocation (addend = target) or a
        plain stored address. Either way the target is an address; we
        return it under a name that string() can find, plus the addend
        (which for a resolved ELF is zero -- the address is whole).
        """
        here = self.va_of(section, off)
        raw = self._read_ptr(section, off)
        if here in self.rel:
            resolved = self.rel[here]
            raw = raw if resolved is None else resolved
        if raw == 0:
            return None, 0
        try:
            sec, roff = self._loc(raw)
        except ValueError:
            return None, 0
        name = "va_%08x" % raw
        self.symbol.setdefault(name, (sec, roff))
        return name, 0

    def string(self, name):
        if name is None:
            return None
        section, value = self.symbol[name]
        data = self.section[section]
        end = data.index(b"\0", value)
        return data[value:end].decode("latin-1")

    # ---- ARM accessors --------------------------------------------------

    def _dis(self, va, n=32):
        """Instructions at an address, as (mnemonic, operands)."""
        if self.path in getattr(self, "_dc", {}):
            return self._dc[self.path].get(va, [])
        md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        md.detail = False
        out = {}
        with open(self.path, "rb") as f:
            f.seek(va)
            code = f.read(n)
        for ins in md.disasm(code, va):
            out.setdefault(ins.address, []).append((ins.mnemonic, ins.op_str))
        return out

    def getter_off(self, va):
        """What `add r0, r0, #imm; bx lr` adds to the record."""
        ins = self._dis(va)[va] if hasattr(self, "_dc") else self._dis2(va)
        return ins

    def _dis2(self, va, n=32):
        # AArch64's mode is nought, the same constant as ARM's.
        md = Cs(CS_ARCH_ARM64 if self._is64 else CS_ARCH_ARM, CS_MODE_ARM)
        md.detail = False
        out = []
        with open(self.path, "rb") as f:
            f.seek(va)
            code = f.read(n)
        for ins in md.disasm(code, va):
            out.append((ins.address, ins.mnemonic, ins.op_str))
        return out


# ---- ARM accessor decoders (replace link.py's x86 ones) -----------------

def arm_accessor_offset(o, name):
    """What an accessor adds to the record it is given, or None.

    The ARM32 shape is `add r0, r0, #imm; bx lr`, or a bare `bx lr` for
    the field at offset zero; AArch64 writes x0 and ends in `ret`. Either
    way a slot may be a one-instruction thunk (`b <shared accessor>`),
    which we follow to the code it names.
    """
    section, value = o.symbol[name]
    va = o.va_of(section, value)
    for _ in range(8):
        for addr, mnem, ops in o._dis2(va):
            if mnem == "b" and "#" in ops:
                va = int(ops.split("#")[1], 0)
                break
            if mnem in ("ret", "bx"):
                return 0
            if mnem == "add" and ops.startswith(("r0, r0, #", "x0, x0, #")):
                return int(ops.split("#")[1], 0)
            if mnem == "add" and ops.startswith(("r0, #", "x0, #")):
                return int(ops.split("#")[1], 0)
            if mnem in ("bl", "ldr", "push"):
                return None
        else:
            return None
    return None


def arm_setter_shape(o, name):
    """(width, offset) for a writer.

    The ARM shape is `ldr{b,h} rX, [r1]; str{b,h} rX, [r0, #imm]` (or
    with the same register for both loads and stores), then `bx lr`;
    AArch64 uses x1/x0, ends in `ret`, and may read or write the loads
    with w or x registers (an `ldr x` reads eight bytes). A slot may be
    a `b` thunk to a shared writer. A writer that is only `ret`/`bx lr`
    writes nothing: that is width nought, not an error.
    """
    section, value = o.symbol[name]
    va = o.va_of(section, value)
    width = None
    for _ in range(8):
        for addr, mnem, ops in o._dis2(va):
            if mnem == "b" and "#" in ops:
                va = int(ops.split("#")[1], 0)
                break
            if mnem.startswith("ldr") and "[" in ops:
                width = (8 if mnem == "ldr" and ops.lstrip().startswith("x")
                         else 4 if mnem == "ldr"
                         else 2 if mnem == "ldrh" else 1)
                continue
            if mnem.startswith("str") and width and \
                    any("[" + r in ops for r in ("r0", "x0", "w0")):
                m = re.search(r"#([0-9a-fx]+)\]", ops)
                return width, int(m.group(1), 0) if m else 0
            if mnem in ("ret", "bx"):
                return (0, 0) if width is None else None
            return None
        else:
            return None
    return None


def arm_variant_sizes(o):
    """What viasizes writes into the table when the language starts.

    The x86 form pushed immediates and stored them at vstmtbl offsets;
    ARM32 builds load the size with `movw` (or from a literal pool) and
    store it to [vstmtbl + #off]. AArch64 builds materialise the table
    base with `adrp; add`, the size with `movi v0.2s, #n`, and store the
    eight bytes {n, n} at [base + #off]; the pair is one variant's stride
    and the part of it that is copied (the store lands at the record's
    variant word +0x50, which the C table keeps as its stride and varlen).
    We decode the mov/str pairs.
    """
    va = o._viasizes_va
    if va is None:
        return []
    out = []
    if not o._is64:
        reg_val = {}
        for addr, mnem, ops in o._dis2(va, 64):
            if mnem == "bx":
                break
            if mnem in ("movw", "mov") and ", #" in ops:
                r, imm = ops.split(", #")
                reg_val[r.strip()] = int(imm, 0)
            elif mnem == "str" and ops.startswith("r") and "[" in ops:
                r = ops.split(",")[0].strip()
                if r in reg_val:
                    m = re.search(r"#([0-9a-fx]+)\]", ops)
                    off = int(m.group(1), 0) if m else 0
                    out.append((off // 0x40, off % 0x40, reg_val[r]))
                    reg_val.pop(r, None)
        return out

    # AArch64.
    sec, off0 = o.symbol["_vstmtbl"]
    rec0, _ = link._statement_base(o, sec, off0, link.LAYOUT_APPLE64)
    rec0_va = o.va_of(sec, rec0)
    base = None
    val = None
    for addr, mnem, ops in o._dis2(va, 96):
        if mnem == "ret":
            break
        if mnem == "adrp" and ", #" in ops:
            base = int(ops.split(", #")[1], 0)
        elif mnem == "add" and base is not None and ", #" in ops \
                and ops.startswith("x"):
            base += int(ops.split(", #")[1], 0)
        elif mnem.startswith("movi") and ", #" in ops:
            val = int(ops.split(", #")[1], 0)
        elif mnem == "str" and "[" in ops and val is not None \
                and base is not None:
            m = re.search(r"#([0-9a-fx]+)\]", ops)
            at = base + (int(m.group(1), 0) if m else 0)
            d = at - rec0_va
            if d >= 0 and d % 0x60 == 0x50:
                out.append((d // 0x60, 0x28, val))   # stride
                out.append((d // 0x60, 0x2c, val))   # varlen
            val = None
    return out


# ---- entry points -------------------------------------------------------

def model_of(o, tag):
    """link.py's model_of, with the ARM decoders standing in."""
    link.setter_shape = arm_setter_shape
    link.accessor_offset = arm_accessor_offset
    link.variant_sizes = arm_variant_sizes
    # variant_sizes is called by name inside model_of -- it imports the
    # module-level function, so rebinding the module attribute works.
    link.NSTMT = model_count(o)
    return link.model_of(o)


def dump(path, tag="enus"):
    o = Elf(path)
    stmts, sizes = model_of(o, tag)
    print("language %s: %d statement types, %d fields" %
          (tag, len(stmts), sum(len(e["field"]) for e in stmts)))
    for e in stmts:
        print("  %-14s nfields=%d length=%s stride=%s" %
              (e["name"], e["nfields"], e["length"], e["stride"]))
        for f in e["field"]:
            print("      %-16s fmt=%s read=%s write=%s" %
                  (f["name"], f["format"], f["read"], f["write"]))
    return o


def lift(path, tag):
    o = Elf(path)
    stmts, sizes = model_of(o, tag)
    return o, stmts, sizes


def model_count(o):
    """How many statement types the table really declares.

    IBM's objects declared ten; the ETI builds declare one fewer and
    the Apple builds keep the full ten, the last of them (pgmin)
    declaring no fields at all. A record carries a name, and the Apple
    ones carry a marker word worth one before it -- all but the first,
    which reads it as nought but points at real tables. The region that
    follows the table is values that read as neither, so walking until
    the name stops being a plausible identifier, or the record stops
    being marked as one, gives the true count.
    """
    sec, base = o.symbol["_vstmtbl"]
    layout = link.LAYOUT_APPLE64 if o._is64 else link.LAYOUT_COFF
    base, _ = link._statement_base(o, sec, base, layout)
    marker = layout.get("marker")
    strs = getattr(o, "_str_secs", ())
    n = 0
    while n < 16:
        at = base + n * layout["stmt"]
        namesym, _ = o.points_to(sec, at + layout["name"])
        name = o.string(namesym) if namesym else None
        if not name or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
            break
        if marker and o.word(sec, at + marker[0]) != marker[1]:
            # Apple's first record reads its marker as nought; it is a
            # real record all the same, and points at real tables. What
            # follows the table points into the strings instead.
            fsym, _ = o.points_to(sec, at + layout["fields"])
            if not fsym or o.symbol[fsym][0] in strs:
                break
        n += 1
    return n


def main(argv=()):
    import tools.module.link as L
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd, path = argv[0], argv[1]
    tag = argv[2] if len(argv) > 2 else "enus"
    link.setter_shape = arm_setter_shape
    link.accessor_offset = arm_accessor_offset
    link.variant_sizes = lambda o2: arm_variant_sizes(o2) \
        if hasattr(o2, "_viasizes_va") else []
    o = Elf(path)
    link.NSTMT = model_count(o)
    if cmd == "dump":
        stmts, sizes = link.model_of(o)
        print("language %s: %d statement types, %d fields" %
              (tag, len(stmts), sum(len(e["field"]) for e in stmts)))
        for e in stmts:
            print("  %-14s nfields=%d length=%s stride=%s" %
                  (e["name"], e["nfields"], e["length"], e["stride"]))
            for f in e["field"]:
                print("      %-16s fmt=%s read=%s write=%s" %
                      (f["name"], f["format"], f["read"], f["write"]))
        return 0
    if cmd == "write":
        stmts, sizes = link.model_of(o)
        out = link.out_path(tag)
        link.emit(stmts, sizes, out, tag)
        print("wrote %s" % out)
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))