#!/usr/bin/env python3
"""Package the NVDA add-on.

An .nvda-addon file is a zip with the manifest at its root, so the packing
itself is four lines. The rest of this is the check worth having: every entry
point the driver asks the library for by name is looked up in the library's own
export table first, and a name that is not there stops the build.

That check exists because of how the failure would otherwise look. ctypes
resolves a name the moment it is used, not when the library is loaded, so a
misspelling or an entry point that was never wrapped shows up as speech simply
not happening, in a screen reader, with the log the only way to find out why.
Here it is a message before anything is shipped.

usage: build.py [--version V] [--tested V] [--out DIR]
"""

import argparse
import ast
import os
import re
import struct
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ADDON = os.path.join(HERE, "addon")

#: Where the built libraries are, and what they are called inside the add-on.
LIBRARIES = [
    (os.path.join(ROOT, "build", "eci.dll"), "eci.dll"),
    (os.path.join(ROOT, "build", "eci32.dll"), "eci32.dll"),
]

#: Where the libraries go inside the add-on. Not "openevv", because that is
#: the driver module's name and a directory of the same name beside it would be
#: a package competing with it.
ENGINE_DIR = "synthDrivers/openevv_engine"

DEFAULT_TESTED = "2026.3.0"


def say(what):
    print("build: " + what)


def die(what):
    sys.stderr.write("build: " + what + "\n")
    raise SystemExit(1)


# ---- reading a library's export table --------------------------------------


def _rva_to_offset(sections, rva):
    for start, size, raw in sections:
        if start <= rva < start + size:
            return raw + (rva - start)
    return None


def exported_names(path):
    """Every name a PE exports, read out of the file itself.

    Done here rather than with objdump so that packaging needs nothing but
    Python; the add-on is built on machines that have no cross toolchain.
    """
    with open(path, "rb") as f:
        image = f.read()

    if image[:2] != b"MZ":
        die("%s is not a PE file at all" % path)
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    if image[pe : pe + 4] != b"PE\0\0":
        die("%s has no PE header" % path)

    sections_count = struct.unpack_from("<H", image, pe + 6)[0]
    optional_size = struct.unpack_from("<H", image, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from("<H", image, optional)[0]
    if magic == 0x10B:
        directories = optional + 96
    elif magic == 0x20B:
        directories = optional + 112
    else:
        die("%s has an optional header this does not know (%#x)" % (path, magic))

    export_rva, export_size = struct.unpack_from("<II", image, directories)
    if export_rva == 0 or export_size == 0:
        die("%s exports nothing" % path)

    sections = []
    at = optional + optional_size
    for _ in range(sections_count):
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from(
            "<IIII", image, at + 8
        )
        # A section whose raw size is short still covers its virtual size in
        # memory, so take whichever is larger for the lookup.
        sections.append((virtual_address, max(virtual_size, raw_size), raw_pointer))
        at += 40

    table = _rva_to_offset(sections, export_rva)
    if table is None:
        die("%s puts its export table outside every section" % path)

    count = struct.unpack_from("<I", image, table + 0x18)[0]
    names_rva = struct.unpack_from("<I", image, table + 0x20)[0]
    names = _rva_to_offset(sections, names_rva)
    if names is None:
        die("%s puts its name table outside every section" % path)

    out = set()
    for i in range(count):
        one = struct.unpack_from("<I", image, names + i * 4)[0]
        where = _rva_to_offset(sections, one)
        if where is None:
            continue
        end = image.index(b"\0", where)
        out.add(image[where:end].decode("ascii", "replace"))
    return out


# ---- what the driver asks for ----------------------------------------------


def wanted_names():
    """Every eci entry point the engine layer names."""
    source = open(os.path.join(ADDON, "synthDrivers", "_openevv.py")).read()
    return set(re.findall(r"\bdll\.(eci\w+)", source))


# ---- the version -----------------------------------------------------------


def described():
    try:
        out = subprocess.run(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "0.0"
    return out[1:] if out.startswith("v") else out


# ---- translations ----------------------------------------------------------


def _po_entries(path):
    """The msgid and msgstr pairs of a .po file, plurals and contexts aside,
    which the add-on has none of."""
    entries = {}
    msgid = msgstr = None
    which = None

    def done():
        if msgid is not None and msgstr:
            entries[msgid] = msgstr

    def unquote(text):
        # A .po string is quoted and escaped the way a Python one is.
        return ast.literal_eval(text.strip())

    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if line.startswith("#") or not line:
            continue
        if line.startswith("msgid "):
            done()
            msgid, msgstr, which = unquote(line[6:]), None, "id"
        elif line.startswith("msgstr "):
            msgstr, which = unquote(line[7:]), "str"
        elif line.startswith('"'):
            if which == "id":
                msgid += unquote(line)
            else:
                msgstr += unquote(line)
    done()
    return entries


def compile_mo(path):
    """A .po file as the .mo gettext reads, written here so that packing
    needs nothing but Python."""
    entries = sorted(_po_entries(path).items())
    ids = b"".join(k.encode("utf-8") + b"\0" for k, _v in entries)
    strs = b"".join(v.encode("utf-8") + b"\0" for _k, v in entries)
    n = len(entries)
    start = 7 * 4 + 16 * n
    offsets = []
    at = 0
    for k, _v in entries:
        offsets.append((len(k.encode("utf-8")), start + at))
        at += len(k.encode("utf-8")) + 1
    at = 0
    soffsets = []
    for _k, v in entries:
        soffsets.append((len(v.encode("utf-8")), start + len(ids) + at))
        at += len(v.encode("utf-8")) + 1
    head = struct.pack("<7I", 0x950412DE, 0, n, 28, 28 + 8 * n, 0, 0)
    table = b"".join(struct.pack("<2I", *o) for o in offsets)
    table += b"".join(struct.pack("<2I", *o) for o in soffsets)
    return head + table + ids + strs


def translations():
    """Every file the add-on's locale folder contributes, as zip names and
    bytes: each manifest as it is and each catalog compiled."""
    out = []
    base = os.path.join(ADDON, "locale")
    if not os.path.isdir(base):
        return out
    for root, _dirs, files in os.walk(base):
        for name in sorted(files):
            path = os.path.join(root, name)
            rel = os.path.relpath(path, ADDON).replace(os.sep, "/")
            if name.endswith(".po"):
                out.append((rel[:-3] + ".mo", compile_mo(path)))
            elif name == "manifest.ini":
                out.append((rel, open(path, "rb").read()))
    return out


# ---- packing ---------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version")
    ap.add_argument("--tested", default=DEFAULT_TESTED)
    ap.add_argument("--out", default=os.path.join(ROOT, "build"))
    args = ap.parse_args()

    version = args.version or described()

    for path, _name in LIBRARIES:
        if not os.path.isfile(path):
            die(
                "there is no %s; `make win' and `make win32' build both"
                % os.path.relpath(path, ROOT)
            )

    wants = wanted_names()
    if not wants:
        die("the engine layer names no entry points, which cannot be right")
    for path, name in LIBRARIES:
        has = exported_names(path)
        missing = sorted(wants - has)
        if missing:
            die(
                "%s does not export %s, which the driver calls"
                % (name, ", ".join(missing))
            )
        say("%s exports all %d names the driver calls" % (name, len(wants)))

    manifest = open(os.path.join(HERE, "manifest.ini.in")).read()
    manifest = manifest.replace("@VERSION@", version).replace("@TESTED@", args.tested)

    os.makedirs(args.out, exist_ok=True)
    target = os.path.join(args.out, "openevv-%s.nvda-addon" % version)

    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("manifest.ini", manifest)
        for leaf in ("openevv.py", "_openevv.py", "_openevv_dictionaries.py"):
            z.write(os.path.join(ADDON, "synthDrivers", leaf), "synthDrivers/" + leaf)
        for path, name in LIBRARIES:
            z.write(path, ENGINE_DIR + "/" + name)
        # RuLex's lexicon for Russian, a file the engine reads from beside
        # the library rather than a table in it: it is GPL, and its licence
        # goes with it. Written by tools/russian/make-ruru.py.
        for name in ("rulex.dat", "rulex-LICENSE.GPL-2.txt"):
            extra = os.path.join(ROOT, "build", name)
            if os.path.isfile(extra):
                z.write(extra, ENGINE_DIR + "/" + name)
                say("packed %s" % name)
        for name, data in translations():
            z.writestr(name, data)

    say("wrote %s, %d bytes" % (target, os.path.getsize(target)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
