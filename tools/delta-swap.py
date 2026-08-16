#!/usr/bin/env python3
"""Work out which of the runtime's own definitions our transcription replaces.

Every primitive we have transcribed carries the same name as the one it was
transcribed from, so the two cannot be linked side by side. This writes one
rename file per object that defines any of them, standing the original
aside under an ibm_ name so that everything reaching it by name reaches
ours instead.

A call one of those objects makes to a name it defines itself is renamed
along with the definition and so still arrives at the original. That is not
a gap in the swap so much as the shape of the object file: there is no way
to redirect a call without redirecting the definition it names. The runtime
counts what actually ran, and the differential harness has already compared
every one of them against the original in any case.
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

OURS = ["delta.c", "delta_heap.c", "delta_sysmem.c", "delta_tables.c",
        "delta_trace.c", "klatt_fx.c", "klatt_state.c", "klatt_synth.c",
        "klatt_tables.c", "delta_link_enus.c", "delta_sets_enus.c", "delta_savefile.c"]


def defined_by(obj, kinds="TDBR"):
    """Every name an object defines, without the leading underscore."""
    text = subprocess.run(["llvm-nm", obj], capture_output=True,
                          text=True).stdout
    out = set()
    for line in text.splitlines():
        m = re.match(r"^[0-9a-f]+ ([TDBR]) _(\w+)$", line.strip())
        if m and m.group(1) in kinds:
            out.add(m.group(2))
    return out


def ours(where, cc, cflags):
    """Every name our own sources define, and which of them are code."""
    out = set()
    code = set()
    for name in OURS:
        obj = os.path.join(where, name[:-2] + ".swapcheck.o")
        subprocess.run(cc + cflags + ["-c", os.path.join(ROOT, "src", name),
                                      "-o", obj], check=True)
        out |= defined_by(obj)
        code |= defined_by(obj, "T")
        os.remove(obj)
    return out, code


def main():
    enus = os.path.join(ROOT, "analysis", "enus")
    out = os.path.join(ROOT, "src", "prim")
    cc = ["i686-w64-mingw32-gcc"]
    cflags = ["-O2", "-std=c99", "-w", "-I" + os.path.join(ROOT, "src")]

    if not os.path.isdir(out):
        os.makedirs(out)
    for stale in os.listdir(out):
        os.remove(os.path.join(out, stale))

    mine, mine_code = ours("/tmp", cc, cflags)
    objects = []
    taken = set()
    total = 0

    for obj in sorted(f for f in os.listdir(enus) if f.endswith(".obj")):
        shared = defined_by(os.path.join(enus, obj)) & mine
        if not shared:
            continue
        with open(os.path.join(out, obj[:-4] + ".ren"), "w") as f:
            for name in sorted(shared):
                f.write("_%s _ibm_%s\n" % (name, name))
        objects.append(obj[:-4])
        taken |= shared
        total += len(shared)

    with open(os.path.join(out, "objects"), "w") as f:
        for name in objects:
            f.write("%s\n" % name)

    # The same names again under one of our own, for the build that wants to
    # watch both sides at once: the original stands aside as ibm_, ours as
    # our_, and a wrapper in between takes the plain name and says what it
    # was asked before handing over to us.
    swapped = sorted(taken)
    with open(os.path.join(out, "names"), "w") as f:
        for name in swapped:
            if name in mine_code:
                f.write("%s\n" % name)
    with open(os.path.join(out, "ours.ren"), "w") as f:
        for name in swapped:
            # Only what is code: a wrapper can stand in front of a call, and
            # there is nothing to stand in front of for a table.
            if name in mine_code:
                f.write("_%s _our_%s\n" % (name, name))

    print("ours: %d names" % len(mine))
    print("stood aside: %d names over %d objects" % (total, len(objects)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
