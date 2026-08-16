#!/usr/bin/env python3
"""Lift the language's lookup sets and dictionary actions.

Two stores and two tables. The stores are one blob each, holding every
named set's entries end to end, with nothing in them but bytes: no
pointers, so they come out as they are. The tables say what each set is,
and the machine gets a copy of them to work in rather than the original,
because it writes to them.

Between the two sits an array of pointers, one per set, which the original
fills in by hand in a function several thousand instructions long. That
order is what this reads out of it.
"""

import importlib.util
import os
import re
import sys

TAIL = r"""
/* Hand the machine what it needs of all this.

   The two tables are copied rather than handed over, because the machine
   writes to them, and it is given room for more than the language declares,
   which is what the original allocates. The stores themselves are handed
   over as they are. */
void set_dict_new(delta_state *d)
{
    d->set_store = setent_all;
}

void set_dict_delete(delta_state *d)
{
    if (d != 0)
        d->set_store = 0;
}

void act_dict_new(delta_state *d)
{
    d->act_store = actent_all;
}

void act_dict_delete(delta_state *d)
{
    if (d != 0)
        d->act_store = 0;
}

void link_new(delta_state *d)
{
    d->fence_room = %d;

    d->fence_chars_base = d->fence_chars = malloc(%d);
    if (d->fence_chars == 0) { delta_delete(d); return; }
    d->fence_index_base = d->fence_index = malloc(%d);
    if (d->fence_index == 0) { delta_delete(d); return; }
    d->fence_marks_base = d->fence_marks = malloc(%d);
    if (d->fence_marks == 0) { delta_delete(d); return; }

    d->nstmts = %d;
    d->lang_a = %d;
    d->lang_b = %d;
    d->lfnames = lfnames;
    d->nlfnames = %d;
    d->nsets = %d;
    d->dictfile = dictfile;
    d->nactions = %d;

    d->sets = malloc(%d);
    if (d->sets == 0) { delta_delete(d); return; }
    memcpy(d->sets, set_table, sizeof set_table);

    d->act_table = malloc(%d);
    if (d->act_table == 0) { delta_delete(d); return; }
    memcpy(d->act_table, act_table, sizeof act_table);
}

void link_delete(delta_state *d)
{
    if (d == 0)
        return;
    free(d->fence_index_base);
    d->fence_index_base = 0;
    free(d->fence_chars_base);
    d->fence_chars_base = 0;
    free(d->fence_marks_base);
    d->fence_marks_base = 0;
    free(d->sets);
    d->sets = 0;
    free(d->act_table);
    d->act_table = 0;
}
"""

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

spec = importlib.util.spec_from_file_location(
    "delta_link", os.path.join(ROOT, "tools", "delta-link.py"))
dlk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dlk)


def installed(o, func, suffix):
    """(slot, name) for each pointer a *_dict_new writes into its array.

    The shape is always the same: fetch the array out of the machine, then
    store the address of one blob into one slot of it.
    """
    section, value = o.symbol[func]
    code = o.section[section]
    fixups = o.reloc[section]
    end = value + len(code)
    out = []
    i = value
    while i < len(code) - 6:
        if code[i] == 0xc7 and code[i + 1] == 0x00:        # movl $x, (%eax)
            slot, at = 0, i + 2
        elif code[i] == 0xc7 and code[i + 1] == 0x40:      # movl $x, d8(%eax)
            slot, at = code[i + 2], i + 3
        elif code[i] == 0xc7 and code[i + 1] == 0x80:      # movl $x, d32(%eax)
            slot = int.from_bytes(code[i + 2:i + 6], "little")
            at = i + 6
        else:
            i += 1
            continue
        who = fixups.get(at)
        if who is None or not who.endswith(suffix):
            i = at + 4
            continue
        out.append((slot // 4, who))
        i = at + 4
    return out


def bytes_as_c(data, per_line=16):
    lines = []
    for i in range(0, len(data), per_line):
        lines.append("    " + ",".join(str(b) for b in data[i:i + per_line]))
    return ",\n".join(lines)


def blob(o, name, size, per_line=16):
    section, value = o.symbol[name]
    size = max(size, 0)
    data = o.section[section][value:value + size]
    if len(data) != size:
        raise ValueError("%s is %d bytes, not %d" % (name, len(data), size))
    return bytes_as_c(data, per_line)


def store(o, tag, suffix, func, out, name):
    """One store: the blob it all lives in, and a pointer per set into it.

    The blob is emitted whole rather than cut into one array per set,
    because that is how the original lies in memory and there is nothing
    to say a set never reads past its own last entry.
    """
    order = installed(o, func, suffix)
    if not order:
        raise ValueError("%s installs nothing" % func)

    section = o.symbol[order[0][1]][0]
    data = o.section[section]

    out.write("\n/* The %s, as they lie. */\n"
              "static const uint8_t %s_store[] = {\n%s\n};\n"
              % (tag, name, bytes_as_c(data)))

    out.write("\n/* Where each one starts in it. */\n"
              "static const uint8_t *const %s_all[] = {\n" % name)
    for slot, who in sorted(order):
        s, v = o.symbol[who]
        if s != section:
            raise ValueError("%s is not in the store" % who)
        out.write("    %s_store + %d,   /* %s */\n"
                  % (name, v, who.lstrip("_")))
    out.write("};\n")
    return order, len(data)


def main():
    where = os.path.join(ROOT, "analysis", "enus")
    out_c = os.path.join(ROOT, "src", "delta_sets_enus.c")

    link = dlk.Coff(os.path.join(where, "link.obj"))
    sets = dlk.Coff(os.path.join(where, "setentry.obj"))
    acts = dlk.Coff(os.path.join(where, "actentry.obj"))

    nsets = link.word(*link.symbol["_vsetdct_glob"])
    nacts = link.word(*link.symbol["_vactdct_glob"])

    with open(out_c, "w") as f:
        f.write("/* Generated by tools/delta-sets.py. Do not edit.\n"
                "\n"
                "   The language's lookup sets and its dictionary's\n"
                "   actions: what each one is, and what is in it. The\n"
                "   contents are bytes and nothing else, so they are here\n"
                "   as they were; the machine is handed a copy of the two\n"
                "   tables rather than these, because it writes to them. */\n"
                "\n#include <stdlib.h>\n#include <string.h>\n"
                "\n#include \"delta.h\"\n")

        f.write("\n/* What each set is: how many entries, how wide, and\n"
                "   where in its blob to start. */\n"
                "static const uint8_t set_table[] = {\n%s\n};\n"
                % blob(link, "_vsetdtbl_glob", nsets * 0x24))
        f.write("\nstatic const uint8_t act_table[] = {\n%s\n};\n"
                % blob(link, "_vactdtbl_glob", nacts * 0x28))

        set_order, set_bytes = store(sets, "sets", "_setentries",
                                     "_set_dict_new", f, "setent")
        act_order, act_bytes = store(acts, "actions", "_actentries",
                                     "_act_dict_new", f, "actent")

        # The streams the language can open, and the dictionary it names.
        lsec, lval = link.symbol["_vlfnames_glob"]
        names = []
        i = 0
        while True:
            who = link.reloc[lsec].get(lval + i * 4)
            if who is None:
                break
            names.append(link.string(who))
            i += 1
        dsec, dval = link.symbol["_vdictfile_glob"]
        dict_name = link.string(link.reloc[dsec][dval])

        f.write("\n/* The streams the language can open by name, and the\n"
                "   dictionary it looks its words up in. */\n"
                "static const char *const lfnames[] = {\n")
        for n in names:
            f.write("    %s,\n" % dlk.c_string(n))
        f.write("};\n\nstatic const char dictfile[] = %s;\n"
                % dlk.c_string(dict_name))

        f.write(TAIL % (0x19, 10, 10, 11, 10, 1, 2, len(names), nsets, nacts,
                        0x50b8, 0x488))

    print("sets: %d, %d bytes of entries" % (len(set_order), set_bytes))
    print("actions: %d, %d bytes of entries" % (len(act_order), act_bytes))
    print("tables: %d and %d bytes" % (nsets * 0x24, nacts * 0x28))
    print("written to %s" % os.path.relpath(out_c, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
