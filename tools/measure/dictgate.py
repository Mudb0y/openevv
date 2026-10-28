#!/usr/bin/env python3
"""Which entries of a language's built-in dictionary the CMU Pronouncing
Dictionary supports, against what the rules say without them.

The built-in dictionary of US English is mostly the community's
IBMTTSDictionaries, sixty-nine thousand entries written by people for this
engine. Most are words CMU does not have at all, but a few thousand it does,
and for those it is a referee that is neither the community nor our rules:
every word of the dictionary is spoken twice, with eciBuiltInDict off and
on, and each word the dictionary changes is given a verdict, on sounds as
tools/measure/referee.py judges them:

  unheld       CMU does not have the word
  dict         CMU supports the dictionary's reading and not the rules'
  both         CMU supports both, sounds and stress
  dict-stress  both sounds are CMU's, and only the dictionary's stress
  ours-stress  both sounds are CMU's, and only the rules' stress
  neither      CMU supports neither
  ours         CMU supports the rules' reading and not the dictionary's

The last is what lang/<tag>/dictionary/held is for: an entry CMU says makes
a word worse is held out of the build until it has been decided by the
published pronunciation, which loses nothing, since without it the word is
said the way CMU supports. CMU is the wrong referee for a name it anglicises
and for an all-capitals key the dictionary spells out as an acronym, so the
held list is a list of candidates rather than a list of mistakes.

The library has to be built from the tree as it stands, held list and all,
and an instance of it is made per worker: forty, as the machine allows.

usage: tools/measure/dictgate.py <libeci.so> <lang dir> <report.tsv>
           [--held] [--jobs N]

--held writes lang/<tag>/dictionary/held from the verdicts, keeping nothing
of what was there: run it on a library built with an empty held list.
"""

import collections
import ctypes
import importlib.util
import multiprocessing
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def _import(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def speak(job):
    """Every word of the list as the engine reads it, with the built-in
    dictionary on or off."""
    lib_path, words, on = job
    lib = ctypes.CDLL(lib_path)
    lib.eciNewEx.restype = ctypes.c_void_p
    lib.eciNewEx.argtypes = [ctypes.c_int]
    lib.eciSetParam.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
    lib.eciAddText.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.eciGeneratePhonemes.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                        ctypes.c_char_p]
    lib.eciDelete.argtypes = [ctypes.c_void_p]
    callback = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int,
                                ctypes.c_long, ctypes.c_void_p)
    lib.eciRegisterCallback.argtypes = [ctypes.c_void_p, callback,
                                        ctypes.c_void_p]
    said = []
    buf = ctypes.create_string_buffer(8192)

    def on_message(h, msg, param, data):
        # Phonemes come a buffer at a time in message 1.
        if msg == 1 and (param & 0xffffffff) > 0:
            said.append(buf.raw[:param & 0xffffffff])
        return 1

    cb = callback(on_message)
    h = lib.eciNewEx(0x10000)
    lib.eciRegisterCallback(h, cb, None)
    # Phonemes come only with the synthesis mode that waits to be told.
    lib.eciSetParam(h, 0, 1)
    lib.eciSetParam(h, 7, 1)
    lib.eciSetParam(h, 34, on)
    out = {}
    for w in words:
        del said[:]
        lib.eciAddText(h, w)
        lib.eciGeneratePhonemes(h, len(buf), buf)
        out[w] = b"".join(said).decode("cp1252")
    lib.eciDelete(h)
    return out


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    jobs = 40
    if "--jobs" in argv:
        jobs = int(argv[argv.index("--jobs") + 1])
        args.remove(str(jobs))
    if len(args) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    lib, lang, report = args
    lib = os.path.abspath(lib)
    built = _import("builtindict", os.path.join(ROOT, "tools", "module",
                                                "builtindict.py"))
    ref = _import("referee", os.path.join(HERE, "referee.py"))

    vols = built.volumes(lang)
    where = collections.defaultdict(list)
    for vol in ("main", "root"):
        for word, _says in vols[vol]:
            where[word].append(vol)
    words = sorted(where)
    pieces = [words[i::jobs] for i in range(jobs)]
    with multiprocessing.Pool(jobs) as pool:
        both = pool.map(speak, [(lib, p, on) for on in (0, 1)
                                for p in pieces])
    off, on = {}, {}
    for i, part in enumerate(both):
        (off if i < jobs else on).update(part)

    cmu = ref.lexicon()
    count = collections.Counter()
    rows = []
    for w in words:
        a, b = off[w], on[w]
        if a == b:
            count["unchanged"] += 1
            continue
        text = w.decode("cp1252")
        readings = cmu.get(text.lower())
        first = ""
        if not readings:
            v = "unheld"
        else:
            fa = ref.fits(ref.ours(a), readings)
            fb = ref.fits(ref.ours(b), readings)
            if fa[0] != fb[0]:
                v = "dict" if fb[0] else "ours"
            elif not fa[0]:
                v = "neither"
            elif fa[1] != fb[1]:
                v = "dict-stress" if fb[1] else "ours-stress"
            else:
                v = "both"
            first = " ".join(readings[0])
        count[v] += 1
        rows.append((v, ",".join(where[w]), text, a, b, first))

    with open(report, "w", encoding="utf-8") as f:
        for r in rows:
            f.write("\t".join(r) + "\n")
    for k, n in count.most_common():
        print("%-12s %6d" % (k, n))

    if "--held" in argv:
        path = os.path.join(lang, "dictionary", "held")
        with open(path, "w", encoding="utf-8") as f:
            f.write(HELD_HEAD)
            for v, vs, text, _a, _b, _c in rows:
                if v == "ours":
                    for vol in vs.split(","):
                        f.write("%s\t%s\tCMU supports the rules\n"
                                % (vol, text))
        print("wrote %s" % path)
    return 0


HELD_HEAD = """\
# Entries of the community snapshot held out of the build, one a line: the
# volume, a tab, the word, a tab, and why. Each says the word one way where
# the CMU Pronouncing Dictionary supports the way the rules already say it,
# so holding it loses nothing; tools/measure/dictgate.py found them, and
# each comes back if the published pronunciation is the entry's.

"""

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
