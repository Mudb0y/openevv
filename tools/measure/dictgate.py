#!/usr/bin/env python3
"""Taking the community's later changes into US English's dictionary.

US English's dictionary began on 14 October 2026 as the community's
IBMTTSDictionaries, and lang/enus/dictionary/upstream names the last commit
of theirs whose changes it has taken. Given a clone of theirs and a newer
commit, this reads what they changed between the two -- an entry added, a
reading changed, an entry dropped -- and judges only that, each against what
the dictionary says now:

  take      the dictionary still says what they said before, or has no
            entry where they had none: their change is taken
  ours      the word is in a block decided by hand, or the body says what
            they never said, which published.py or a person wrote: ours
            stays
  removed   the word was taken out on purpose and removed says why: it
            stays out
  cmu       the CMU Pronouncing Dictionary supports the way the engine says
            the word now and not the way it would with their change: not
            taken, and an entry they added goes to removed with that as the
            reason, to be decided by the published pronunciation
  unsayable a character of theirs the engine cannot name

The CMU check is the one the snapshot was taken through. Each word a change
touches is spoken as the dictionary has it now and as it would have it, the
second by loading the whole dictionary with the change in as a caller's,
with the built-in one off, and tools/measure/referee.py judges both on
sounds. CMU is the wrong referee for a name it anglicises, so what it stops
is reported for a person to decide rather than thrown away. The abbreviation
volume is taken without it.

Every change goes to the report, a line each: the verdict, what they did,
the volume, the word, what the dictionary says now, what they said before
and what they say now. With --take the changes taken go into the body of
each volume, the entries CMU stopped into removed, and the commit into
upstream, which git then shows as one change to listen to.

The library has to be built from the tree as it stands. The clone is read
with git and nothing in it is run.

usage: tools/measure/dictgate.py <libeci.so> <IBMTTSDictionaries clone>
           <commit> <report.tsv> [--take] [--jobs N]
"""

import collections
import ctypes
import multiprocessing
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from evv import ROOT, sibling  # noqa: E402

built = sibling('module/builtindict')
LANG = os.path.join(ROOT, 'lang', 'enus')
UPSTREAM = os.path.join(LANG, 'dictionary', 'upstream')
FILES = {'main': 'ENUmain.dic', 'root': 'ENURoot.dic', 'abbr': 'ENUabbr.dic'}
COMMIT = re.compile(r'^[0-9a-f]{40}$')


def last_taken():
    commits = [l.strip() for l in open(UPSTREAM, encoding='utf-8')
               if COMMIT.match(l.strip())]
    if len(commits) != 1:
        raise SystemExit('dictgate: %s names no one commit' % UPSTREAM)
    return commits[0]


def theirs(clone, commit, vol):
    """A volume of theirs at a commit, word to reading, as text. Their files
    have been the Windows Western set; one that reads as UTF-8 is taken as
    that, since the Western set's accented letters almost never do."""
    got = subprocess.run(['git', '-C', clone, 'show', '%s:%s' % (commit, FILES[vol])],
                         capture_output=True)
    if got.returncode:
        raise SystemExit('dictgate: git show %s:%s: %s'
                         % (commit, FILES[vol], got.stderr.decode(errors='replace').strip()))
    raw = got.stdout.removeprefix(b'\xef\xbb\xbf')
    try:
        raw.decode('utf-8')
        code = 'utf-8'
    except UnicodeDecodeError:
        code = 'cp1252'
    out = {}
    for line in raw.split(b'\n'):
        entry = built.parse(line)
        if entry:
            out[entry[0].decode(code)] = entry[1].decode(code)
    return out


def sayable(*texts):
    try:
        for t in texts:
            if t is not None:
                t.encode('cp1252')
        return True
    except UnicodeEncodeError:
        return False


def speak(job):
    """Each word as the engine reads it, with the built-in dictionary on or
    off and any dictionary files given loaded as the caller's. Here and not
    borrowed from published.py: a pool's worker starts afresh and finds a
    function by its module's name, which a tool loaded by path has not."""
    lib_path, words, builtin, files = job
    lib = ctypes.CDLL(lib_path)
    v = ctypes.c_void_p
    lib.eciNewEx.restype = v
    lib.eciNewEx.argtypes = [ctypes.c_int]
    lib.eciNewDict.restype = v
    lib.eciNewDict.argtypes = [v]
    lib.eciLoadDict.argtypes = [v, v, ctypes.c_int, ctypes.c_char_p]
    lib.eciSetDict.argtypes = [v, v]
    lib.eciSetParam.argtypes = [v, ctypes.c_int, ctypes.c_int]
    lib.eciAddText.argtypes = [v, ctypes.c_char_p]
    lib.eciGeneratePhonemes.argtypes = [v, ctypes.c_int, ctypes.c_char_p]
    lib.eciDelete.argtypes = [v]
    callback = ctypes.CFUNCTYPE(ctypes.c_int, v, ctypes.c_int, ctypes.c_long, v)
    lib.eciRegisterCallback.argtypes = [v, callback, v]
    said = []
    buf = ctypes.create_string_buffer(8192)

    def on_message(h, msg, param, data):
        # Phonemes come a buffer at a time in message 1.
        if msg == 1 and (param & 0xffffffff) > 0:
            said.append(buf.raw[:param & 0xffffffff])
        return 1

    cb = callback(on_message)
    h = lib.eciNewEx(0x10000)
    if files:
        d = lib.eciNewDict(h)
        for vol, path in files:
            lib.eciLoadDict(h, d, vol, path.encode())
        lib.eciSetDict(h, d)
    lib.eciRegisterCallback(h, cb, None)
    # Phonemes come only with the synthesis mode that waits to be told.
    lib.eciSetParam(h, 0, 1)
    lib.eciSetParam(h, 7, 1)
    lib.eciSetParam(h, 34, builtin)
    out = {}
    for w in words:
        del said[:]
        lib.eciAddText(h, w.encode('cp1252'))
        lib.eciGeneratePhonemes(h, len(buf), buf)
        out[w] = b''.join(said).decode('cp1252')
    lib.eciDelete(h)
    return out


def speak_all(lib, words, builtin, files, jobs):
    jobs = max(1, min(jobs, len(words)))
    with multiprocessing.Pool(jobs) as pool:
        parts = pool.map(speak, [(lib, words[i::jobs], builtin, files) for i in range(jobs)])
    out = {}
    for p in parts:
        out.update(p)
    return out


def as_changed(changes, tmp):
    """The main and root volumes as the build would lay them down with these
    changes made, as a caller's files."""
    files = []
    for n, vol in ((0, 'main'), (1, 'root')):
        merged = dict(built.read_dic(os.path.join(LANG, 'dictionary', vol + '.dic')))
        for v, word, after in changes:
            if v != vol:
                continue
            if after is None:
                merged.pop(word.encode('cp1252'), None)
            else:
                merged[word.encode('cp1252')] = after.encode('cp1252')
        path = os.path.join(tmp, vol + '.dic')
        with open(path, 'wb') as f:
            for word, says in sorted(merged.items()):
                f.write(word + b'\t' + says + b'\r\n')
            # The loader drops a file's last line.
            f.write(b'\r\n')
        files.append((n, path))
    return files


def main(argv):
    jobs = 40
    if '--jobs' in argv:
        at = argv.index('--jobs')
        jobs = int(argv[at + 1])
        argv = argv[:at] + argv[at + 2:]
    take = '--take' in argv
    argv = [a for a in argv if a != '--take']
    if len(argv) != 4:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    lib, clone, commit, out = argv
    lib = os.path.abspath(lib)
    got = subprocess.run(['git', '-C', clone, 'rev-parse', '--verify', commit + '^{commit}'],
                         capture_output=True, text=True)
    if got.returncode:
        raise SystemExit('dictgate: %s is not a commit of %s' % (commit, clone))
    commit = got.stdout.strip()
    last = last_taken()
    removed = built.read_removed(LANG)

    rows, laid = [], {}
    for vol in built.VOLUMES:
        path = os.path.join(LANG, 'dictionary', vol + '.dic')
        laid[vol] = built.layout(path)
        body, hand = laid[vol][1], built.hand_words(path)
        before, after = theirs(clone, last, vol), theirs(clone, commit, vol)
        for word in sorted(set(before) | set(after), key=lambda w: (w.lower(), w)):
            a, b = before.get(word), after.get(word)
            if a == b:
                continue
            did = 'added' if a is None else 'dropped' if b is None else 'changed'
            now = body.get(word)
            if word in hand:
                v = 'ours'
            elif (vol, word) in removed:
                v = 'removed'
            elif now != a:
                v = 'ours'
            elif not sayable(word, b):
                v = 'unsayable'
            else:
                v = 'take'
            rows.append([v, did, vol, word, now, a, b])

    # The CMU check, on every change still to be taken in the volumes it
    # reads, spoken now and as it would be with all of them made.
    asked = [r for r in rows if r[0] == 'take' and r[2] != 'abbr']
    if asked:
        referee = sibling('measure/referee')
        if referee.cmudict is None:
            raise SystemExit('dictgate: no CMU dictionary here; the dev shell has it')
        words = sorted({r[3] for r in asked})
        tmp = tempfile.mkdtemp()
        print('dictgate: speaking %d words twice' % len(words), flush=True)
        now = speak_all(lib, words, 1, [], jobs)
        then = speak_all(lib, words, 0,
                         as_changed([(r[2], r[3], r[6]) for r in asked], tmp), jobs)
        cmu = referee.lexicon()
        for r in asked:
            readings = cmu.get(r[3].lower())
            if not readings or now[r[3]] == then[r[3]]:
                continue
            fa = referee.fits(referee.ours(now[r[3]]), readings)
            fb = referee.fits(referee.ours(then[r[3]]), readings)
            if fa[0] and not fb[0]:
                r[0] = 'cmu'

    with open(out, 'w', encoding='utf-8') as f:
        for r in rows:
            f.write('\t'.join('' if x is None else x for x in r) + '\n')
    count = collections.Counter((r[0], r[1]) for r in rows)
    print('dictgate: %d changes from %s to %s%s' % (
        len(rows), last[:12], commit[:12],
        ''.join('; %s %s %d' % (v, d, n) for (v, d), n in sorted(count.items()))))
    if not take:
        return 0

    for vol, (head, body, tail) in laid.items():
        for v, did, rv, word, _now, _a, b in rows:
            if rv != vol or v != 'take':
                continue
            if b is None:
                del body[word]
            else:
                body[word] = b
        built.write_layout(os.path.join(LANG, 'dictionary', vol + '.dic'), head, body, tail)
    built.add_removed(LANG, [(r[2], r[3], r[6], 'CMU supports the rules') for r in rows
                             if r[0] == 'cmu' and r[1] == 'added'])
    text = open(UPSTREAM, encoding='utf-8').read()
    with open(UPSTREAM, 'w', encoding='utf-8') as f:
        f.write(text.replace(last, commit))
    print('dictgate: took %d, and upstream is %s' % (
        sum(1 for r in rows if r[0] == 'take'), commit[:12]))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
