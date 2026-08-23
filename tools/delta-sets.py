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
void %%(tag)s_set_dict_new(delta_state *d)
{
    delta_low_region(setent_store, sizeof setent_store);
    d->set_store = EVV_REF(delta_low_copy(setent_all, sizeof setent_all));
}

void %%(tag)s_set_dict_delete(delta_state *d)
{
    if (d != 0)
        d->set_store = EVV_REF(0);
}

void %%(tag)s_act_dict_new(delta_state *d)
{
    delta_low_region(actent_store, sizeof actent_store);
    d->act_store = EVV_REF(delta_low_copy(actent_all, sizeof actent_all));
}

void %%(tag)s_act_dict_delete(delta_state *d)
{
    if (d != 0)
        d->act_store = EVV_REF(0);
}

void %%(tag)s_link_new(delta_state *d)
{
    d->fence_room = %d;

    d->fence_chars = EVV_REF(malloc(%d));
    d->fence_chars_base = d->fence_chars;
    if (d->fence_chars == 0) { delta_delete(d); return; }
    d->fence_index = EVV_REF(malloc(%d));
    d->fence_index_base = d->fence_index;
    if (d->fence_index == 0) { delta_delete(d); return; }
    d->fence_marks = EVV_REF(malloc(%d));
    d->fence_marks_base = d->fence_marks;
    if (d->fence_marks == 0) { delta_delete(d); return; }

    d->nstmts = %d;
    d->lang_a = %d;
    d->lang_b = %d;
    d->lfnames = EVV_REF(delta_low_copy(lfnames, sizeof lfnames));
    d->nlfnames = %d;
    d->nsets = %d;
    d->dictfile = EVV_REF(delta_low_copy(dictfile, sizeof dictfile));
    d->nactions = %d;

    d->sets = EVV_REF(malloc(%d));
    if (EVV_AT(uint8_t *, d->sets) == 0) { delta_delete(d); return; }
    memcpy(EVV_AT(uint8_t *, d->sets), set_table, sizeof set_table);

    d->act_table = EVV_REF(malloc(%d));
    if (EVV_AT(uint8_t *, d->act_table) == 0) { delta_delete(d); return; }
    memcpy(EVV_AT(uint8_t *, d->act_table), act_table, sizeof act_table);
}

void %%(tag)s_link_delete(delta_state *d)
{
    if (d == 0)
        return;
    free(EVV_AT(uint8_t *, d->fence_index_base));
    d->fence_index_base = EVV_REF(0);
    free(EVV_AT(uint8_t *, d->fence_chars_base));
    d->fence_chars_base = EVV_REF(0);
    free(EVV_AT(uint8_t *, d->fence_marks_base));
    d->fence_marks_base = EVV_REF(0);
    free(EVV_AT(uint8_t *, d->sets));
    d->sets = EVV_REF(0);
    free(EVV_AT(uint8_t *, d->act_table));
    d->act_table = EVV_REF(0);
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

def store_model(o, func, suffix):
    """One store: the blob it all lies in, and where each set starts in it."""
    order = installed(o, func, suffix)
    if not order:
        raise ValueError("%s installs nothing" % func)
    section = o.symbol[order[0][1]][0]
    at = []
    for slot, who in sorted(order):
        s, v = o.symbol[who]
        if s != section:
            raise ValueError("%s is not in the store" % who)
        at.append((slot, who.lstrip("_"), v))
    return {"store": bytes(o.section[section]), "at": at}


def model_of(where):
    """Everything in the file as values, with nothing of the objects left."""
    link = dlk.Coff(os.path.join(where, "link.obj"))
    sets = dlk.Coff(os.path.join(where, "setentry.obj"))
    acts = dlk.Coff(os.path.join(where, "actentry.obj"))

    nsets = link.word(*link.symbol["_vsetdct_glob"])
    nacts = link.word(*link.symbol["_vactdct_glob"])

    def table(name, size):
        section, value = link.symbol[name]
        data = link.section[section][value:value + size]
        if len(data) != size:
            raise ValueError("%s is %d bytes, not %d"
                             % (name, len(data), size))
        return bytes(data)

    lsec, lval = link.symbol["_vlfnames_glob"]
    streams = []
    i = 0
    while True:
        who = link.reloc[lsec].get(lval + i * 4)
        if who is None:
            break
        streams.append(link.string(who))
        i += 1
    dsec, dval = link.symbol["_vdictfile_glob"]

    return {
        "set_table": table("_vsetdtbl_glob", nsets * 0x24),
        "act_table": table("_vactdtbl_glob", nacts * 0x28),
        "sets": store_model(sets, "_set_dict_new", "_setentries"),
        "acts": store_model(acts, "_act_dict_new", "_actentries"),
        "streams": streams,
        "dictfile": link.string(link.reloc[dsec][dval]),
    }


def emit(m, out_c, tag):
    """One writer, whether the model came out of the objects or out of text."""
    nsets = len(m["set_table"]) // 0x24
    nacts = len(m["act_table"]) // 0x28

    with open(out_c, "w") as f:
        f.write("/* Generated by tools/delta-sets.py. Do not edit.\n"
                "\n"
                "   The language's lookup sets and its dictionary's\n"
                "   actions: what each one is, and what is in it. The\n"
                "   contents are bytes and nothing else, so they are here\n"
                "   as they were; the machine is handed a copy of the two\n"
                "   tables rather than these, because it writes to them.\n"
                "\n"
                "   The three dictionary arrays below -- act_table,\n"
                "   actent_store and actent_all -- are laid down again by\n"
                "   tools/delta-dict.py out of lang/enus/enus.dict, which is\n"
                "   where a word is added or an entry changed. Running\n"
                "   this lifter again puts IBM's own back and loses\n"
                "   whatever that file said. */\n"
                "\n#include <stdlib.h>\n#include <string.h>\n"
                "\n#include \"delta.h\"\n"
                "#include \"delta_rules_c.h\"\n")

        f.write("\n/* What each set is: how many entries, how wide, and\n"
                "   where in its blob to start. */\n"
                "static const uint8_t set_table[] = {\n%s\n};\n"
                % bytes_as_c(m["set_table"]))
        f.write("\nstatic const uint8_t act_table[] = {\n%s\n};\n"
                % bytes_as_c(m["act_table"]))

        for which, name, what in (("sets", "setent", "sets"),
                                  ("acts", "actent", "actions")):
            one = m[which]
            f.write("\n/* The %s, as they lie. */\n"
                    "static const uint8_t %s_store[] = {\n%s\n};\n"
                    % (what, name, bytes_as_c(one["store"])))
            f.write("\n/* Where each one starts in it. */\n"
                    "static const uint8_t *const %s_all[] = {\n" % name)
            for _slot, who, off in one["at"]:
                f.write("    %s_store + %d,   /* %s */\n" % (name, off, who))
            f.write("};\n")

        f.write("\n/* The streams the language can open by name, and the\n"
                "   dictionary it looks its words up in. */\n"
                "static const char *const lfnames[] = {\n")
        for n in m["streams"]:
            f.write("    %s,\n" % dlk.c_string(n))
        f.write("};\n\nstatic const char dictfile[] = %s;\n"
                % dlk.c_string(m["dictfile"]))

        # The names in the tail carry the language, because a program may
        # have more than one module in it; the numbers above are put in
        # first, so this is a second pass rather than another placeholder.
        f.write((TAIL % (0x19, 10, 10, 11, 10, 1, 2, len(m["streams"]),
                         nsets, nacts, 0x50b8, 0x488)) % {"tag": tag})

    return nsets, nacts


# ---- the model back out of the C ---------------------------------------
#
# Not out of the objects, which is the one thing this file must not be lifted
# from again: the dictionary's three arrays in it are laid down by
# tools/delta-dict.py out of the words, so IBM's objects hold what the
# dictionary said before anything was added to it. The C in the tree is what
# the language is, so that is what the text is written from, and the text and
# the C then say the same thing whatever has been edited.


def numbers(s, name):
    m = re.search(r"static const uint8_t %s\[\] = \{(.*?)\};" % name, s,
                  re.S)
    if m is None:
        raise SystemExit("delta-sets: no %s in there" % name)
    return bytes(int(x) for x in m.group(1).replace("\n", "").split(",")
                 if x.strip())


def from_c_string(text):
    out = []
    i = 0
    while i < len(text):
        if text[i] == "\\":
            nxt = text[i + 1]
            if nxt == "\\" or nxt == '"':
                out.append(nxt)
                i += 2
            else:
                out.append(chr(int(text[i + 1:i + 4], 8)))
                i += 4
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def model_from_c(path):
    s = open(path).read()
    m = {"set_table": numbers(s, "set_table"),
         "act_table": numbers(s, "act_table"),
         "sets": {"store": numbers(s, "setent_store"), "at": []},
         "acts": {"store": numbers(s, "actent_store"), "at": []}}
    for which, name in (("sets", "setent"), ("acts", "actent")):
        body = re.search(r"static const uint8_t \*const %s_all\[\] = \{"
                         r"(.*?)\};" % name, s, re.S)
        if body is None:
            raise SystemExit("delta-sets: no %s_all in there" % name)
        found = re.findall(r"%s_store \+ (\d+),\s*/\* (\S+) \*/"
                           % name, body.group(1))
        for slot, (off, who) in enumerate(found):
            m[which]["at"].append((slot, who, int(off)))
    lf = re.search(r"static const char \*const lfnames\[\] = \{(.*?)\};",
                   s, re.S)
    m["streams"] = [from_c_string(x)
                    for x in re.findall(r'"((?:[^"\\]|\\.)*)"', lf.group(1))]
    df = re.search(r'static const char dictfile\[\] = "((?:[^"\\]|\\.)*)";',
                   s)
    m["dictfile"] = from_c_string(df.group(1))
    return m


# ---- the same sets as text ---------------------------------------------
#
# The contents of a set are bytes and nothing else -- one entry after another,
# a key and what the rules do with it -- so as text they are bytes too, at
# thirty-two to a line. What the text adds over the C is that the structure is
# separated from the contents: which sets there are, where each one starts, and
# which streams the language can open, each on a line of its own.
#
# The words in there are not edited here. They are in lang/<tag>/<tag>.dict
# through tools/delta-dict.py, which lays the dictionary's three arrays down
# again out of the words; this is the form for everything around them.

HEAD = """# The lookup sets %(tag)s declares and its dictionary's actions, as text.
#
# `table' is what each one is -- how many entries, how wide, where in its blob
# to start -- and `store' is the blob, both as bytes at thirty-two to a line.
# `at' says where a set begins in the store: the slot the language installs it
# in, the name the original's compiler gave it, and how far in it lies.
#
# `stream' is a stream the language can open by name, in order, and
# `dictionary' the file it looks its words up in.
#
# Written by tools/delta-sets.py. The words themselves are not in here: they
# are in %(tag)s.dict.
"""


def hexes(word, data, out):
    for i in range(0, len(data), 32):
        out.append("%s %s" % (word, " ".join("%02x" % b
                                             for b in data[i:i + 32])))


def write_text(m, path, tag):
    out = [HEAD % {"tag": tag}, ""]
    for n in m["streams"]:
        out.append("stream %s" % n)
    out.append("dictionary %s" % m["dictfile"])
    for which, word in (("sets", "set"), ("acts", "action")):
        out.append("")
        hexes("table %s" % word,
              m["set_table" if which == "sets" else "act_table"], out)
        out.append("")
        for slot, who, off in m[which]["at"]:
            out.append("at %s %d %s %d" % (word, slot, who, off))
        out.append("")
        hexes("store %s" % word, m[which]["store"], out)
    open(path, "w").write("\n".join(out) + "\n")
    return len(m["sets"]["at"]), len(m["acts"]["at"])


def read_text(path):
    m = {"set_table": b"", "act_table": b"", "streams": [], "dictfile": None,
         "sets": {"store": b"", "at": []}, "acts": {"store": b"", "at": []}}
    which = {"set": "sets", "action": "acts"}
    for n, raw in enumerate(open(path), 1):
        line = raw.split("#")[0].strip()
        if not line:
            continue
        w = line.split()
        where = "%s line %d" % (os.path.basename(path), n)
        if w[0] == "stream":
            m["streams"].append(" ".join(w[1:]))
        elif w[0] == "dictionary":
            m["dictfile"] = " ".join(w[1:])
        elif w[0] in ("table", "store", "at"):
            if w[1] not in which:
                raise SystemExit("%s: %r is neither the sets nor the actions"
                                 % (where, w[1]))
            one = which[w[1]]
            if w[0] == "table":
                key = "set_table" if one == "sets" else "act_table"
                m[key] += bytes(int(x, 16) for x in w[2:])
            elif w[0] == "store":
                m[one]["store"] += bytes(int(x, 16) for x in w[2:])
            else:
                m[one]["at"].append((int(w[2]), w[3], int(w[4])))
        else:
            raise SystemExit("%s: no line called %r" % (where, w[0]))
    if m["dictfile"] is None:
        raise SystemExit("the text names no dictionary")
    return m


def text_path(tag):
    return os.path.join(ROOT, "lang", tag, "%s.sets" % tag)


def out_path(tag):
    return os.path.join(ROOT, "lang", tag, "delta_sets_%s.c" % tag)


def dump(tag, where=None):
    m = model_of(where) if where else model_from_c(out_path(tag))
    nsets, nacts = write_text(m, text_path(tag), tag)
    print("%d sets and %d actions, %d and %d bytes of entries, in %s"
          % (nsets, nacts, len(m["sets"]["store"]), len(m["acts"]["store"]),
             os.path.relpath(text_path(tag), ROOT)))
    return True


def regenerate(tag, write=False):
    import tempfile
    m = read_text(text_path(tag))
    if write:
        emit(m, out_path(tag), tag)
        print("%s written" % os.path.relpath(out_path(tag), ROOT))
        return True
    with tempfile.TemporaryDirectory() as tmp:
        made = os.path.join(tmp, "sets.c")
        emit(m, made, tag)
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
    # As in delta-link.py: the objects' directory names the language, and
    # the written file is named for it. No arguments means US English.
    tag = argv[0] if argv else "enus"
    where = argv[1] if len(argv) > 1 else os.path.join(ROOT, "analysis", tag)
    out_c = out_path(tag)
    os.makedirs(os.path.dirname(out_c), exist_ok=True)

    m = model_of(where)
    nsets, nacts = emit(m, out_c, tag)

    print("sets: %d, %d bytes of entries"
          % (len(m["sets"]["at"]), len(m["sets"]["store"])))
    print("actions: %d, %d bytes of entries"
          % (len(m["acts"]["at"]), len(m["acts"]["store"])))
    print("tables: %d and %d bytes" % (nsets * 0x24, nacts * 0x28))
    print("written to %s" % os.path.relpath(out_c, ROOT))
    return 0


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "dump":
        sys.exit(0 if dump(*sys.argv[2:]) else 1)
    if len(sys.argv) == 3 and sys.argv[1] == "regenerate":
        sys.exit(0 if regenerate(sys.argv[2]) else 1)
    if len(sys.argv) == 3 and sys.argv[1] == "write":
        sys.exit(0 if regenerate(sys.argv[2], write=True) else 1)
    sys.exit(main(sys.argv[1:]))
