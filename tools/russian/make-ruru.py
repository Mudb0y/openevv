#!/usr/bin/env python3
"""Write lang/ruru out of lang/enus.

Russian is not a module openevv has, and nothing in the tree is near enough
to read Russian spelling for itself. What the US English module has is the
fullest phonology and prosody of the eleven, and an annotation that takes a
pronunciation in its own phoneme alphabet with the stress marked: `[.0x.1bWt]
is about. So Russian is said by the English module from phonemes:
src/eci/synth/eci_russian.c decides
each word's stress, reduces its unstressed vowels, assimilates its voicing,
softens its consonants with a glide and writes the result as one of those
annotations, and the module speaks it with an English voice and English
intonation. ы is the module's X, the reduced vowel of roses, which is the
same sound.

So the module is English's, copied and renamed, with a number and a name of
its own. A copy rather than English under a second name, because a language
is known to the engine by its module: Russian gets its own eight voices, its
own dictionaries in force and its own place in every list a screen reader
offers, and Latin text inside Russian is still read as English.

Russian is family eighteen, 0x120000. Three tables in the engine are indexed
by family and hold eighteen, counted from one; IBM used six, openevv gave
seventeen to Polish, and four more are families IBM's own code says have a
romanizer. Eighteen is the one that was left.

It writes two more things a build of Russian needs, out of the data beside
it: src/eci/synth/eci_russian_stress.h, the table of stress eci_russian.c is
compiled with, which every build wants since eci_russian.c is in every
build; and with --rulex, rulex.dat, RuLex's lexicon, which the engine reads
as a file rather than compiling in, since it is GPL. NOTICE here says whose
each is.

The Makefile runs this as it starts, with --module and --rulex when LANGS
names lang/ruru, so none of the three is in the tree. Run from anywhere.
Writes nothing that is already what it would write, so it costs nothing to
call every time.

usage: make-ruru.py [--engine DIR] [--module] [--rulex DIR]
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.dirname(os.path.dirname(HERE))

SOURCE = "enus"
TARGET = "ruru"

#: What changes besides the tag, file by file. Everything else is the template
#: as it stands.
EDITS = {
    "ruru.settings": [
        ("library Static Engine ENU", "library Static Engine RUS"),
        ("[1.0]", "[18.0]"),
    ],
    "delta_lang_ruru.c": [
        ('"US English"', '"Russian"'),
    ],
}

#: Rules of Russian's own, copied into lang/ruru/rules from tools/russian/rules.
RULES = os.path.join(HERE, "rules")

#: Lines of the template's rules changed for Russian: which file, inside
#: which rule, the line as it was and as it is.
#:
#: ga_ph_r and eng_ph_F ask ru_trill_Fv in tools/russian/rules/ru_val.up
#: for their place instead of English's retroflex r and its d, which is what
#: makes Russian р the tap Polish's is.
#:
#: ga_ph_o is English's o, the diphthong of boat: its first formant falls
#: from 550 to 400 and its second from 1100 to 850, which is the glide
#: towards u that makes it English. Russian о is a plain vowel, so it is held
#: near where it starts -- 500 to 470 and 950 to 900 -- which is between the
#: o English starts from and the bought vowel that sounded half way to а.
CALLS = [
    ("rules/es_gaval.dr", "ga_ph_r",
     "call eng_ret_Fv arity 1 depth 2", "call ru_trill_Fv arity 1 depth 2"),
    # And the flap, which is what Cyrillic р is said as: the module closes
    # it like a d, and in English it is placed like a d, so it sounded like
    # one. Placed at the trill's locus it is what Italian's -- and so
    # Polish's -- r is: one dip in the voicing at the formants of a trill.
    ("rules/es_val.dr", "eng_ph_F",
     "call eng_alv_Fv arity 1 depth 3", "call ru_trill_Fv arity 1 depth 3"),
    ("rules/es_gaval.dr", "ga_ph_o",
     "store movw imm 550 statefld 3078", "store movw imm 500 statefld 3078"),
    ("rules/es_gaval.dr", "ga_ph_o",
     "store movw imm 400 statefld 3082", "store movw imm 470 statefld 3082"),
    ("rules/es_gaval.dr", "ga_ph_o",
     "store movw imm 1100 statefld 3086", "store movw imm 950 statefld 3086"),
    ("rules/es_gaval.dr", "ga_ph_o",
     "store movw imm 850 statefld 3090", "store movw imm 900 statefld 3090"),
    # Russian х. A T in the phonemes eci_russian.c writes lays down the
    # module's velar fricative, Q, instead of English's th: see RU_X below.
    # Only phonemes handed in that way come through here, so a th in Latin
    # text, which the module's letter-to-sound lays down itself, is English's
    # th as it always was.
    ("rules/et_spr1.dr", "insert_eng_spr_phone",
     "call ZZlprp_load__insert_2pt_s_2_1_ZZstring34 arity 3 depth 3",
     "call ru_insert_x arity 3 depth 3"),
    # The arm before th's in the chain that says which rule speaks a
    # consonant goes to the one RU_X adds when the consonant is not v.
    ("rules/es_val.dr", "eng_con_vals",
     "  branch jne to L17\n", "  branch jne to L900\n"),
    # And х is timed as th is, wherever the timing and the syllables ask for
    # th by name: as th, it was timed so, and as a phone no rule knew the
    # vowel before it came out a tenth of a second longer.
    ("rules/es_cdur.dr", "eng_cons_durs",
     "call ZZtest_string_s_2_1_ZZstring34 arity 1 depth 1",
     "call ru_test_th_or_x arity 1 depth 1"),
    ("rules/es_tdur.dr", "get_eng_fric_trans_dur",
     "call ZZtest_string_s_2_1_ZZstring34 arity 1 depth 1",
     "call ru_test_th_or_x arity 1 depth 1"),
    ("rules/et_syll.dr", "assign_eng_syll_init_con",
     "call ZZtest_string_s_2_1_ZZstring34 arity 1 depth 2",
     "call ru_test_th_or_x arity 1 depth 2"),
]

#: Russian х is Q, a phoneme English declares -- a velar fricative, voiced --
#: and never says: no word of English's twenty thousand lays it down, no
#: Phoneme line gives it numbers and no rule speaks it. So it is Russian's to
#: take, given th's record, and spoken by ru_ph_x, which the arm RU_X_ARM
#: adds to eng_con_vals hands it to.
#:
#: The sound is English's sh said at the velar place, with every noise band
#: it sets moved down two, from where sh has its energy to where х has it.
#: English's own th was tried first, and its noise is so faint at the end of
#: a word that it was heard as f; three settings of sh went to the ear --
#: its own bands, one down and two down -- and two down was heard as х.
RU_X_CODE = 23

RU_X_ARM = """label L900 was ru_x
  push reg r6
  call ZZstarttest18 arity 1 depth 1
  push reg r6
  call ZZlpta_load_vvg__setscan_0106r__2 arity 1 depth 2
  cmp testl reg r0 reg r0
  popreg r1
  popreg r1
  branch jne to L17
  push reg r6
  call ru_test_x arity 1 depth 1
  cmp testl reg r0 reg r0
  popreg r1
  branch jne to L17
  push reg r6
  call ZZstarttest18 arity 1 depth 1
  push reg r6
  call ru_ph_x arity 1 depth 2
  jump to L1
"""


def _velar_fricative(body):
    def down(m):
        off = int(m.group(2))
        if 2926 <= off <= 2950 and (off - 2926) % 4 == 0:
            off = max(2926, off - 8)
        return "%s%d" % (m.group(1), off)
    body = body.replace("rule eng_ph_S from", "rule ru_ph_x from", 1)
    body = body.replace("was _eng_ph_S", "was _ru_ph_x", 1)
    body = body.replace("call eng_pal_Fv", "call eng_vel_Fv")
    return re.sub(r"(store movw imm \d+ ind reg r6 )(\d+)", down, body)


#: Written by settings.py out of ruru.settings rather than renamed, since it
#: holds the settings as bytes and the section name is in them.
REGENERATED = ["eci_ini_ruru.c"]

STAMP = ".made-from"


def is_build_output(name):
    """What a build of the template leaves behind rather than template: the rules as
    the engine runs them, written by every build out of the text, under the
    tag of the module they are in."""
    return name.startswith("delta_rules_") and name != "delta_rules_none_%s.c" % SOURCE


def fingerprint(src):
    """A digest of the template and of this script, so a change to either
    makes the copy again."""
    h = hashlib.sha256()
    for root, dirs, files in os.walk(src):
        dirs.sort()
        for name in sorted(files):
            if is_build_output(name):
                continue
            path = os.path.join(root, name)
            h.update(os.path.relpath(path, src).replace(os.sep, "/").encode())
            with open(path, "rb") as f:
                h.update(f.read())
    with open(os.path.abspath(__file__), "rb") as f:
        h.update(f.read())
    for name in sorted(os.listdir(RULES)):
        with open(os.path.join(RULES, name), "rb") as f:
            h.update(name.encode() + f.read())
    return h.hexdigest()


def is_text(data):
    return b"\0" not in data[:8192]


def stale(out, inputs):
    """Whether out is missing or older than anything it is made from."""
    if not os.path.isfile(out):
        return True
    made = os.path.getmtime(out)
    return any(os.path.getmtime(p) > made for p in inputs)


def stress_table(engine):
    """The table of Russian stress the engine's Russian is compiled with:
    half a million words, OpenRussian's as much as ours, so it is written
    rather than kept."""
    out = os.path.join(engine, "src", "eci", "synth", "eci_russian_stress.h")
    inputs = [os.path.join(HERE, n) for n in ("stress.py", "stress.txt", "openrussian.tsv.gz")]
    if not stale(out, inputs):
        return
    subprocess.run(
        [sys.executable, os.path.join(HERE, "stress.py"), "--out", out],
        env=dict(os.environ, PYTHONUTF8="1"),
        check=True,
        stdout=subprocess.DEVNULL,
    )


def rulex_lexicon(where):
    """RuLex's lexicon, rulex.dat, and its licence beside it, where
    nvda/build.py packs them beside the library. It is GPL, so it is a file
    the engine reads rather than a table in it."""
    os.makedirs(where, exist_ok=True)
    out = os.path.join(where, "rulex.dat")
    src = os.path.join(HERE, "rulex")
    inputs = [os.path.join(HERE, "rulex.py")] + [os.path.join(src, n) for n in os.listdir(src)]
    if stale(out, inputs):
        subprocess.run(
            [sys.executable, os.path.join(HERE, "rulex.py"), "--out", out],
            env=dict(os.environ, PYTHONUTF8="1"),
            check=True,
            stdout=subprocess.DEVNULL,
        )
    shutil.copy(os.path.join(src, "LICENSE.GPL-2"),
                os.path.join(where, "rulex-LICENSE.GPL-2.txt"))


def q_voiceless(path):
    """Q's record in the phone statement made th's: voiceless rather than
    voiced, and at th's place rather than the velar one, so that every rule
    that asks what its neighbour is answers as it did when х was th. Where
    it is spoken is ru_ph_x's to say, and that says velar. Done here rather
    than with tools/module/phonemes.py set, which writes the record and then
    fails printing it."""
    lines = open(path, encoding="utf-8").read().split("\n")
    start = lines.index("statement phone")
    rows = [i for i in range(start, len(lines))
            if lines[i].startswith("  variants ")]
    rows = rows[:next((k for k in range(1, len(rows)) if rows[k] != rows[k - 1] + 1), len(rows))]
    data = [b for i in rows for b in lines[i].split()[1:]]
    at = RU_X_CODE * 8 + 1
    if data[at] != "00" or data[at + 3] != "04":
        sys.exit("make-ruru: Q is no longer the voiced velar fricative it was")
    data[at] = "01"
    data[at + 3] = "01"
    for n, i in enumerate(rows):
        lines[i] = "  variants " + " ".join(data[n * 32:(n + 1) * 32])
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", default=ENGINE)
    ap.add_argument("--module", action="store_true", help="write lang/ruru")
    ap.add_argument("--rulex", metavar="DIR", help="write rulex.dat into DIR")
    args = ap.parse_args()

    engine = os.path.abspath(args.engine)
    stress_table(engine)
    if args.rulex:
        rulex_lexicon(os.path.join(engine, args.rulex))
    if args.module:
        return module(engine)
    return 0


def module(engine):
    src = os.path.join(engine, "lang", SOURCE)
    dst = os.path.join(engine, "lang", TARGET)
    if not os.path.isdir(src):
        sys.exit("make-ruru: no %s" % src)

    want = fingerprint(src)
    stamp = os.path.join(dst, STAMP)
    if os.path.isfile(stamp) and open(stamp).read().strip() == want:
        return 0

    if os.path.isdir(dst):
        shutil.rmtree(dst)

    for root, dirs, files in os.walk(src):
        rel = os.path.relpath(root, src)
        out_dir = os.path.normpath(os.path.join(dst, rel))
        os.makedirs(out_dir, exist_ok=True)
        for name in files:
            if is_build_output(name):
                continue
            with open(os.path.join(root, name), "rb") as f:
                data = f.read()
            new_name = name.replace(SOURCE, TARGET)
            if is_text(data):
                data = data.replace(SOURCE.encode(), TARGET.encode())
                for old, new in EDITS.get(new_name, []):
                    if old.encode() not in data:
                        sys.exit("make-ruru: %s no longer says %r" % (new_name, old))
                    data = data.replace(old.encode(), new.encode())
            with open(os.path.join(out_dir, new_name), "wb") as f:
                f.write(data)

    for name in sorted(os.listdir(RULES)):
        shutil.copy(os.path.join(RULES, name), os.path.join(dst, "rules", name))
    for path, rule, was, now in CALLS:
        full = os.path.join(dst, path)
        text = open(full, encoding="utf-8").read()
        start = text.index("rule %s from " % rule)
        end = text.find("\nrule ", start + 1)
        end = len(text) if end < 0 else end
        body = text[start:end]
        if was not in body:
            sys.exit("make-ruru: %s in %s no longer says %r" % (rule, path, was))
        text = text[:start] + body.replace(was, now) + text[end:]
        with open(full, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)

    # х: the arm that hands Q to its rule, before th's, and the rule itself,
    # made out of sh's, at the end of the same file.
    full = os.path.join(dst, "rules", "es_val.dr")
    text = open(full, encoding="utf-8").read()
    th_arm = "label L17 was $X18$99581\n"
    if text.count(th_arm) != 1:
        sys.exit("make-ruru: eng_con_vals no longer has th's arm where it was")
    text = text.replace(th_arm, RU_X_ARM + th_arm)
    start = text.index("rule eng_ph_S from ")
    end = text.find("\nrule ", start + 1) + 1
    text = text.rstrip("\n") + "\n\n" + _velar_fricative(text[start:end]).rstrip("\n") + "\n"
    with open(full, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)

    # Q made voiceless, and its code where the rules can name it.
    q_voiceless(os.path.join(dst, TARGET + ".statements"))
    subprocess.run(
        [sys.executable, "tools/module/link.py", "write", TARGET],
        cwd=engine,
        env=dict(os.environ, PYTHONUTF8="1"),
        check=True,
        stdout=subprocess.DEVNULL,
    )
    with open(os.path.join(dst, "rules", "constants"), "a", encoding="utf-8", newline="\n") as f:
        f.write("\n# Russian's own: the phone Q, which is х. See tools/russian/make-ruru.py.\n"
                "bytes ru_ph_x %02x\n" % RU_X_CODE)
    # The letter-to-sound strings first, as a build lays them down, so that
    # the store and its symbols hold every constant and not only ours.
    subprocess.run(
        [sys.executable, "tools/rules/letters.py", "write", TARGET],
        cwd=engine,
        env=dict(os.environ, PYTHONUTF8="1"),
        check=True,
        stdout=subprocess.DEVNULL,
    )
    subprocess.run(
        [sys.executable, "tools/rules/consts.py", TARGET],
        cwd=engine,
        env=dict(os.environ, PYTHONUTF8="1"),
        check=True,
        stdout=subprocess.DEVNULL,
    )

    # The settings as bytes, out of the text just edited.
    env = dict(os.environ, PYTHONUTF8="1")
    subprocess.run(
        [sys.executable, "tools/module/settings.py", "write", TARGET],
        cwd=engine,
        env=env,
        check=True,
        stdout=subprocess.DEVNULL,
    )
    for name in REGENERATED:
        text = open(os.path.join(dst, name), encoding="utf-8").read()
        if "0x120000" not in text:
            sys.exit("make-ruru: %s does not say family eighteen" % name)

    with open(stamp, "w") as f:
        f.write(want + "\n")
    print("make-ruru: wrote lang/%s from lang/%s" % (TARGET, SOURCE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
