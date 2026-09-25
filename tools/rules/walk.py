#!/usr/bin/env python3
"""One of IBM's letter rules, a line a step, for reading.

A letter rule of any size is a backtracking program: each ALT and each choice
plants a tag, a failed test backtracks to the newest one, and the dispatch
line says where each tag resumes. What this prints is that and nothing else,
with the strings read out in the language's letters or phones, the slots named
as the letter's LEFT and RIGHT ends, the variables by offset -- 864 and 872
are a root's ends in Italian, 704 and 712 the word's -- and each lookup named
for the set it asks. Italian's e and o, a thousand lines of the lower form
each, were decoded from this and nothing else.

Three things to know when reading it. A `choice N (scan)' before a test is an
alternation: if the test fails, the scan is put back and tag N tries the next
letter at the same place. A `boa' choice is a `not': the test inside it
succeeding sets a depth and backtracks past its own branch. And a string a
rule hands over before jumping to a shared test is printed at the test's
first appearance only, so a `test' reached by a jump may be reading a string
set at the jump -- read the lower form there.

usage: tools/rules/walk.py <tag> <rule>
"""
import glob, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import strings as st                                           # noqa: E402

ROOT = st.ROOT
tag, name = sys.argv[1], sys.argv[2]
store, sym = st.stores(tag), st.symbols(tag)
letters = st.codes(tag, "alphabet.py")
phones = st.codes(tag, "phonemes.py")
setnames = {}
for line in open("%s/lang/%s/%s.sets" % (ROOT, tag, tag)):
    w = line.split()
    if w[:2] == ["at", "set"]:
        setnames[int(w[2])] = w[3].replace("_setentries", "")

lines, obj = [], None
for path in sorted(glob.glob(os.path.join(ROOT, "lang", tag, "rules", "*.dr"))):
    inside = False
    for line in open(path):
        w = line.split()
        if w[:1] == ["rule"] and len(w) > 1:
            inside = w[1] == name
            if inside:
                obj = w[3]
            continue
        if inside:
            if line.rstrip() == "end":
                break
            lines.append(line.rstrip())
    if lines:
        break


def raw(symname, n=None):
    for at, where, off in sym.get(symname, []):
        if at != obj and at != "glob.obj":
            continue
        b = store.get(where)
        end = off
        while end < len(b) and b[end]:
            end += 1
        r = bytes(b[off:end])
        return r[:n] if n else r
    return b""


def L(b):
    return "".join(letters.get(c, "<%d>" % c) for c in b)


def P(b):
    return " ".join(phones.get(c, "<%d>" % c) for c in b)


slotname = {}
stack = []          # what was pushed since the last call
r7 = None
out = []
for line in lines:
    w = line.split()
    if not w:
        continue
    if w[0] == "label":
        out.append(w[1] + ":")
        continue
    if w[:2] == ["push", "slotaddr"]:
        stack.append(("slot", int(w[2])))
    elif w[:2] == ["push", "imm"]:
        stack.append(("imm", int(w[2])))
    elif w[:2] == ["push", "sym"]:
        stack.append(("sym", w[2]))
    elif w[0] == "setarg" and "sym" in w:
        stack.append(("sym", w[w.index("sym") + 1]))
    elif w[:2] == ["push", "param"]:
        stack.append(("param", int(w[2])))
    elif w[:2] == ["push", "state"]:
        stack.append(("state", int(w[2])))
    elif w[0] == "addk" and w[3:] == ["reg", "r6", "into", "r0"] or \
            (w[0] == "addk" and "r6" in w):
        stack.append(("var", int(w[1])))
    elif w[:2] == ["push", "reg"]:
        if stack and stack[-1][0] == "var":
            pass
        else:
            stack.append(("reg", w[2]))
    elif w[0] == "load" and w[-2:] == ["into", "r7"] and "imm" in w:
        r7 = int(w[w.index("imm") + 1])
    elif w[0] == "alu" and "incl" in w and "r7" in w:
        r7 = (r7 or 0) + 1
    elif w[0] == "store" and "r7" in w and "slot" in w:
        out.append("    CUT to tag %s" % r7)
    elif w[0] == "call":
        f = w[1]
        args = [a for a in stack if a[0] != "state" and a[0] != "reg"]
        stack = []

        def nm(a):
            if a[0] == "slot":
                return slotname.get(a[1], "s%d" % a[1])
            if a[0] == "var":
                return "v%d" % a[1]
            return str(a[1])
        say = None
        m = re.match(r"ZZtest_string_s_1_(\d+)(?:_(ZZstring\d+|string_\d+))?$", f)
        if f == "ZZget_parm_ptr2":
            s = [a for a in args if a[0] == "slot"]
            # pushed right to left: the last slot pushed is the first argument
            slotname[s[-1][1]] = "LEFT"
            slotname[s[0][1]] = "RIGHT"
            say = "range LEFT..RIGHT"
        elif re.match(r"ZZlpta_load__setscan_(nof_)?(r|l)__\d+$", f):
            d = "->" if "_r__" in f else "<-"
            say = "scan %s from %s%s" % (d, nm(args[-1]) if args else "?",
                                         " nofence" if "nof" in f else "")
        elif f.startswith("ZZlpta_load_vvg__setscan_0107r"):
            say = "scan -> from v107"
        elif m:
            n = int(m.group(1))
            s = m.group(2) or next((a[1] for a in args if a[0] == "sym"), None)
            say = "test %r" % L(raw(s, n))
        elif f == "ZZtest_string_s_2_1" or re.match(r"ZZtest_string_s_2_", f):
            say = "test phone %s" % f
        elif re.match(r"ZZtestFldeq1_4_(\d)$", f):
            say = "is %s" % {"1": "vowel", "2": "consonant", "3": "glide"}[f[-1]]
        elif re.match(r"ZZtestFldeq1_0_(\d+)$", f):
            say = "is letter %r" % letters.get(int(f.split("_")[-1]), "?")
        elif re.match(r"ZZtestFldeq1_5_(\d+)$", f):
            say = "field5 == %s" % f.split("_")[-1]
        elif f == "advance_tok":
            say = "step"
        elif f == "ZZtwo_advance_tok":
            say = "step step"
        elif re.match(r"ZZsavescptr\d+$", f):
            say = "save scan into %s  [choice %s]" % (nm(args[-1]), f[11:])
        elif f == "savescptr":
            say = "save scan into %s [choice %s]" % (nm(args[-1]) if args else "?", args[0][1] if args else "?")
        elif re.match(r"ZZbspush_ca_scan__\d+$", f):
            say = "choice %s (scan)" % f.split("__")[1]
        elif re.match(r"ZZbspush_ca_scan_boa__\d+$", f):
            say = "choice %s (scan, boa)" % f.split("__")[1]
        elif re.match(r"ZZbspush_ca_boa__\d+$", f):
            say = "choice %s (boa)" % f.split("__")[1]
        elif re.match(r"ZZbspush_ca__\d+$", f):
            say = "choice %s" % f.split("__")[1]
        elif f in ("bspush_ca", "bspush_ca_boa", "bspush_ca_scan", "bspush_ca_scan_boa"):
            say = "choice %s (%s)" % (args[0][1] if args else "?", f)
        elif f == "bspop_boa":
            say = "pop boa"
        elif re.match(r"ZZstarttest\d+$", f):
            say = "ALT %s" % f[11:]
        elif f == "starttest":
            say = "ALT %s" % (args[0][1] if args else "?")
        elif f == "ZZlpta_loadp__test_ptr":
            say = "scan at %s?" % nm(args[-1])
        elif f == "ZZlprp_loadpn__comp":
            say = "compare %s with %s" % (nm(args[-1]), nm(args[0]))
        elif f in ("testeq", "testneq"):
            say = f == "testeq" and "  ...equal?" or "  ...unequal?"
        elif f == "ZZlprp_load__setd":
            setn = [a for a in args if a[0] == "imm"]
            ptrs = [a for a in args if a[0] != "imm"]
            say = "lookup %s over %s..%s" % (
                setnames.get(setn[0][1], setn) if setn else "?",
                nm(ptrs[-1]), nm(ptrs[0]))
        elif f.startswith("ZZlprp_load_vvg__setd"):
            setn = [a for a in args if a[0] == "imm"]
            rng = f.split("setd")[1]
            say = "lookup %s over v%s" % (setnames.get(setn[0][1], setn) if setn else "?", rng)
        elif re.match(r"ZZlprp_load__insert_2pt_s_2_(\d+)(?:_(ZZstring\d+))?$", f):
            mm = re.match(r"ZZlprp_load__insert_2pt_s_2_(\d+)(?:_(ZZstring\d+))?$", f)
            n = int(mm.group(1))
            s = mm.group(2) or next((a[1] for a in args if a[0] == "sym"), None)
            ptrs = [a for a in args if a[0] == "slot"]
            say = "SAY %s over %s..%s" % (P(raw(s, n)), nm(ptrs[-1]) if ptrs else "?", nm(ptrs[0]) if ptrs else "?")
        elif f.startswith("ZZlprp_load__mark_s"):
            say = "MARK %s" % f
        elif f == "backtrack_function":
            say = "BACKTRACK"
        elif f in ("setjmp3", "ventproc", "vretproc", "ZZfence_null",
                   "memset") or f.startswith("ZZfence") or f.startswith("ZZpush_ptr_init") \
                or f == "push_ptr_init":
            continue
        elif f == "succeed":
            say = "SUCCEED"
        else:
            say = "CALL %s(%s)" % (f, ", ".join(nm(a) for a in args))
        out.append("    " + say)
    elif w[0] == "branch":
        cond = {"jne": "fail", "je": "ok", "ja": "above"}.get(w[1], w[1])
        out.append("      %s -> %s" % (cond, w[3]))
    elif w[0] == "jump":
        out.append("      goto %s" % w[2])
    elif w[0] == "switch":
        out.append("      dispatch: " + " ".join("%d:%s" % (i + 1, t) for i, t in enumerate(w[4:])))
    elif w[0] == "return":
        out.append("    RETURN")
print("\n".join(out))
