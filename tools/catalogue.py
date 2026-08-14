#!/usr/bin/env python3
"""Break a COFF object into per-function disassembly plus its data tables.

MSVC compiled clsyn.cpp with function-level linking, so every function sits in
its own .text section starting at address zero. Reading them as one dump is
useless; this splits them apart and pairs each with its relocations.
"""

import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def undname(name):
    if not name.startswith("?"):
        return name
    out = run("llvm-undname", "--no-access-specifier", name).strip()
    return out or name


def parse_sections(obj):
    """Read the COFF section table directly.

    objdump numbers sections from zero while COFF symbols reference them from
    one, and this object carries several sections all named .rdata, so going
    through the file header is the only unambiguous route to the raw bytes.
    """
    with open(obj, "rb") as fh:
        data = fh.read()

    nsections, _, _, opt_size = struct.unpack_from("<HIII", data, 2)[:4]
    nsections = struct.unpack_from("<H", data, 2)[0]
    opt_size = struct.unpack_from("<H", data, 16)[0]
    table = 20 + opt_size

    sections = []
    for i in range(nsections):
        off = table + i * 40
        name = data[off:off + 8].rstrip(b"\0").decode("ascii", "replace")
        size, _vaddr, raw_size, raw_ptr = struct.unpack_from("<IIII", data, off + 8)
        length = raw_size or size
        sections.append({
            "number": i + 1,
            "name": name,
            "size": length,
            "raw": data[raw_ptr:raw_ptr + length] if raw_ptr else b"",
        })
    return sections


def parse_symbols(obj):
    """(section_number, offset, name) for every non-auxiliary symbol."""
    syms = []
    for line in run("llvm-objdump", "--syms", obj).splitlines():
        m = re.match(r"\[\s*\d+\]\(sec\s+(-?\d+)\).*?0x([0-9a-f]+)\s+(\S.*)$", line)
        if m:
            syms.append((int(m.group(1)), int(m.group(2), 16), m.group(3).strip()))
    return syms


def split_functions(obj, outdir):
    """One .asm file per function, named after the demangled function."""
    dump = run("llvm-objdump", "-d", "-r", "--no-show-raw-insn", obj)
    blocks = {}
    current = None
    for line in dump.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
        if m:
            current = m.group(1)
            blocks[current] = []
            continue
        if current is not None:
            blocks[current].append(line)

    os.makedirs(outdir, exist_ok=True)
    written = []
    for mangled, body in blocks.items():
        pretty = undname(mangled)
        stem = mangled.lstrip("?_").split("@")[0]
        path = os.path.join(outdir, stem + ".asm")
        text = "\n".join(l.rstrip() for l in body).strip("\n")
        with open(path, "w") as fh:
            fh.write("; %s\n; %s\n\n%s\n" % (mangled, pretty, text))
        insns = sum(1 for l in body if re.match(r"^\s+[0-9a-f]+:", l))
        written.append((stem, pretty, insns))
    return written


def data_tables(sections, syms, outdir):
    """Slice the large .rdata into its named lookup tables, one file each."""
    rdata = [s for s in sections if s["name"] == ".rdata"]
    if not rdata:
        return []
    big = max(rdata, key=lambda s: s["size"])
    raw = big["raw"]

    named = sorted(
        [(off, name) for sec, off, name in syms
         if sec == big["number"] and not name.startswith("?") and off < big["size"]]
    )

    os.makedirs(outdir, exist_ok=True)
    out = []
    for i, (off, name) in enumerate(named):
        end = named[i + 1][0] if i + 1 < len(named) else big["size"]
        blob = raw[off:end]
        path = os.path.join(outdir, name.lstrip("_") + ".bin")
        with open(path, "wb") as fh:
            fh.write(blob)
        out.append((name.lstrip("_"), off, len(blob)))
    return out


def parameter_names(obj):
    """Recover the Klatt parameter list, in frame order, from .data relocs."""
    text = run("llvm-objdump", "-r", "--section=.data", obj)
    entries = []
    for line in text.splitlines():
        m = re.match(r"^([0-9a-f]{8})\s+IMAGE_REL_I386_DIR32\s+(\S+)$", line)
        if m:
            offset = int(m.group(1), 16)
            demangled = undname(m.group(2))
            lit = re.search(r'"(.*)"', demangled)
            entries.append((offset // 4, lit.group(1) if lit else m.group(2)))
    entries.sort()
    return entries


def main():
    obj = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "analysis/obj/clsyn.obj")
    stem = os.path.splitext(os.path.basename(obj))[0]
    base = os.path.join(ROOT, "analysis", stem)

    print("catalogue: reading %s" % obj)
    if not os.path.exists(obj):
        print("catalogue: error: object not found; run tools/extract.sh first", file=sys.stderr)
        return 1

    sections = parse_sections(obj)
    syms = parse_symbols(obj)

    funcs = split_functions(obj, os.path.join(base, "functions"))
    tables = data_tables(sections, syms, os.path.join(base, "tables"))
    params = parameter_names(obj)
    undefined = sorted(name for sec, _, name in syms if sec == 0)

    os.makedirs(base, exist_ok=True)

    with open(os.path.join(base, "functions.txt"), "w") as fh:
        for stem_, pretty, insns in sorted(funcs, key=lambda f: -f[2]):
            fh.write("%-24s %5d  %s\n" % (stem_, insns, pretty))

    with open(os.path.join(base, "tables.txt"), "w") as fh:
        for name, off, size in tables:
            fh.write("%-16s offset 0x%06x  %6d bytes\n" % (name, off, size))

    with open(os.path.join(base, "parameters.txt"), "w") as fh:
        for idx, name in params:
            fh.write("%3d %s\n" % (idx, name))

    with open(os.path.join(base, "undefined.txt"), "w") as fh:
        for name in undefined:
            fh.write("%s\n" % name)

    print("catalogue: %d functions, %d data tables, %d parameters, %d undefined symbols"
          % (len(funcs), len(tables), len(params), len(undefined)))
    print("catalogue: wrote %s" % base)
    return 0


if __name__ == "__main__":
    sys.exit(main())
