#!/usr/bin/env python3
"""The segment table: what the engine's formant tracks are made of, as data.

A formant track is a list of breakpoints and the frames are what it unfolds
into, exactly -- `tools/measure/breaks.py' proves that. A run of the
generator is one phoneme, and it is two gaps: a transition of fixed length
into the phoneme's first target and an interior that runs to its second and
absorbs whatever length the duration model gave it. `docs/authoring.md' says
how that was established.

So the values are a table and this writes it. The key is the phoneme, the
phoneme either side and the stress on its syllable, which `--reach' shows is
the whole of what a target depends on. What is not in it is any length that
varies: the interior span is the duration model's and is recorded as what it
happened to be, marked, rather than as a property of the segment.

The corpus is not made up. `test/samples/enus.words' already holds what this
engine says each of twenty-four thousand words is made of, stress marks and
all, so speaking those covers exactly the contexts the language produces
rather than a cross product most of which never occurs. A key seen twice
that disagrees with itself is the check that the key is long enough, and is
reported rather than averaged.

    tools/module/segments.py <probe> [<tag>] [--words N] [--jobs N]
                             [--out <path>]
"""

import collections
import os
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ProcessPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "measure"))

VOWELS = set("iIeEAauUocYWOXHx")

# Which parameters need a context table and which do not. Measured over
# fifteen hundred words: four never vary at all, fifteen more are decided by
# the phoneme alone to better than 88 per cent, and only these eight are
# genuinely context-dependent -- the second formant is predicted by its
# phoneme alone 25 per cent of the time, the third 31. Everything else goes
# in a file of its own, one line a phoneme, which is a thousand lines
# against three quarters of a million.
CONTEXTUAL = ("f1", "f2", "f3", "f4", "f5", "b3", "av", "af")

# Five phonemes get two runs rather than one, and it is exactly the two
# affricates and the three diphthongs: /C/ and /J/ are a stop and a
# fricative, /Y/, /W/ and /O/ two vowel targets. Measured rather than
# assumed -- over six hundred words, every word containing one of these has
# a run more than its phonemes and no other phoneme is above a third. It
# composes with the /h/ fold, so `hY' in `anaheim' is two runs and not one.
TWO_RUNS = set("CJOWY")
SKIP = ("step", "f0")
PROBE = None


def annotations(tag):
    """Each word's phonemes, as the engine itself reports them.

    Empty where the language has no word baseline, which is all of them but
    English: `test/words.sh record' makes one and wants a word list.
    """
    path = os.path.join(ROOT, "test", "samples", "%s.words" % tag)
    out = []
    if not os.path.exists(path):
        return out
    for line in open(path):
        if line.startswith("#") or "\t" not in line:
            continue
        word, ph = line.rstrip("\n").split("\t", 1)
        # The recorded field is the whole annotation inside square brackets
        # that words.sh puts round it, so what comes out already begins with
        # a backtick. Wrapping it again nests one annotation in another and
        # the engine says something else entirely.
        # One pronunciation annotation and nothing else. A handful of
        # entries are the engine reporting something else entirely -- one is
        # a whole warning with a path in it -- and they are not words.
        m = re.match(r'^\[(`\[[^\[\]]*\])\]$', ph)
        if m:
            out.append((word, m.group(1)))
    return out


def marked(body):
    """The phonemes of an annotation, each with the stress of its syllable.

    A mark stands before the syllable it marks and holds until the next one,
    so this is a walk rather than a lookup.
    """
    out = []
    stress = "0"
    i = 0
    while i < len(body):
        if body[i] == "." and i + 1 < len(body) and body[i + 1].isdigit():
            stress = body[i + 1]
            i += 2
            continue
        if body[i] in "`[]":
            i += 1
            continue
        out.append((body[i], stress))
        i += 1
    return out


def units(ph):
    """Phonemes grouped into the engine's own runs, with their contexts.

    /h/ has no run of its own -- it is the following vowel's shape excited
    by noise -- so it is folded into the vowel after it, which keeps the run
    count and the phoneme count in step.
    """
    got = []
    i = 0
    while i < len(ph):
        if (ph[i][0] == "h" and i + 1 < len(ph)
                and ph[i + 1][0] in VOWELS):
            got.append(("h" + ph[i + 1][0], ph[i + 1][1]))
            i += 2
        else:
            got.append(ph[i])
            i += 1
    out = []
    for k, (u, st) in enumerate(got):
        left = got[k - 1][0][-1] if k else "."
        right = got[k + 1][0][0] if k + 1 < len(got) else "."
        if u[-1] in TWO_RUNS:
            # Two runs, and they are halves of one phoneme: the first has
            # the real left neighbour and the second the real right one,
            # with the phoneme itself standing between them.
            out.append((u + "1", left, u[-1], st))
            out.append((u + "2", u[-1], right, st))
        else:
            out.append((u, left, right, st))
    return out


def de_bruijn(alpha, n):
    """A sequence over `alpha' containing every string of length `n' once.

    Built as an Eulerian circuit over the graph whose nodes are the
    (n-1)-grams and whose edges are the n-grams, which is easy to check: the
    sequence has to come out k to the n long and hold that many distinct
    n-grams, and it does for every small alphabet tried. The Lyndon-word
    construction is shorter to write and the version of it written from
    memory here gave 16,215 symbols and 131 distinct triples where there
    should have been 91,125 of each, silently.

    It is what makes filling the key space affordable: every phoneme between
    every pair of phonemes is 91,125 triples for a forty-five letter
    alphabet, and one sequence holds them all.
    """
    k = len(alpha)
    nodes = k ** (n - 1)
    nxt = [0] * nodes
    stack = [0]
    out = []
    while stack:
        v = stack[-1]
        if nxt[v] < k:
            e = nxt[v]
            nxt[v] += 1
            stack.append((v * k + e) % nodes)
        else:
            out.append(stack.pop() % k)
    out.reverse()
    seq = out[:nodes * k]
    # Cyclic, so the triples that wrap round want the head repeating.
    seq = seq + seq[:n - 1]
    return "".join(alpha[i] for i in seq)


def fill_corpus(tag, span=8, overlap=3):
    """Utterances covering every phoneme between every pair of phonemes.

    Three stresses, marked before every vowel so each one carries it, and
    the chunks overlap so a triple straddling a boundary is still said
    whole. The word edges are their own carriers, a word of running English
    having only so many phonemes at the start and end of it.

    Eight phonemes a chunk, measured. A long enough stretch of arbitrary
    phonemes stops parsing as an annotation and is spelled out instead, and
    the longer the chunk the likelier: twenty phonemes line up 73 times in a
    hundred, twelve 86, eight 94. A short chunk also loses less when it does
    fail.
    """
    # The alphabet from the words this engine says, where there is a word
    # baseline, and from the language's own phone statement where there is
    # not -- only English has the first. Single-letter names only: the
    # multi-letter ones are the module's internal symbols and an annotation
    # will not take them.
    alpha = sorted(set(
        p for _, body in annotations(tag) for p, _ in marked(body)))
    if not alpha:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import phonemes as P
        alpha = sorted(set(n for n in P.inventory(tag)
                           if len(n) == 1 and n not in "#?"))
    seq = de_bruijn(alpha, 3)
    out = []
    for stress in ("0", "1", "2"):
        i = 0
        while i < len(seq):
            chunk = seq[i:i + span]
            if len(chunk) < 3:
                break
            body = "".join(("." + stress + c) if c in VOWELS else c
                           for c in chunk)
            # An annotation wants a stress mark before its first syllable,
            # and a chunk that opens on consonants has none of its own.
            if not body.startswith("."):
                body = "." + stress + body
            out.append(("fill", "`[" + body + "]"))
            i += span - overlap
        for a in alpha:
            for b in alpha:
                out.append(("edge", "`[." + stress + a + b + "]"))
                out.append(("edge", "`[." + stress + b + a + "]"))
    return out


def tap(probe, text):
    """One utterance's runs and the gaps that start inside each."""
    with tempfile.TemporaryDirectory() as w:
        c = os.path.join(w, "c.txt")
        with open(c, "w") as f:
            f.write(text + "\n")
        env = dict(os.environ)
        env["EVV_ARRAY_TAP"] = os.path.join(w, "a.txt")
        subprocess.run([probe, "@" + c, os.path.join(w, "c.wav"), "a"],
                       capture_output=True, env=env)
        if not os.path.exists(env["EVV_ARRAY_TAP"]):
            return []
        runs = []
        gaps = []
        for line in open(env["EVV_ARRAY_TAP"]):
            f = line.split()
            if not f:
                continue
            if f[0] == "run":
                runs.append([int(f[2]), int(f[4]), {}])
            elif f[0] == "at":
                lo, hi = f[5].split("..")
                gaps.append((int(f[1]), int(f[4]), int(lo), int(hi), f[2]))
        # By the moment a gap covers, never by where the tap printed it: the
        # cursor advances only when a frame needs a value past its right
        # end, so a parameter holding still crosses late.
        for at, span, v0, v1, name in gaps:
            for r in runs:
                if r[0] <= at < r[1]:
                    # Whether the gap ends inside the run matters: a segment
                    # whose own target is unset places no breakpoint, so the
                    # line runs on to the next target that is set and ends
                    # somewhere in a later segment. That end is the later
                    # segment's number, not this one's -- schwa before /k/
                    # ends at 1650 in `academy' and 1200 in `acclaim', which
                    # is the vowel after the /k/ in each.
                    r[2].setdefault(name, []).append(
                        (at - r[0], span, v0, v1, at + span <= r[1]))
                    break
        for r in runs:
            for v in r[2].values():
                v.sort()
        return runs


def harvest(job):
    """One word's segments, as table entries.

    A gap's value where it starts is usually the one the gap before left
    behind, and where it is not, that start is the segment's own and has to
    be written down. It is not rare: of the gaps in four hundred words, 40
    per cent of the aspiration's start somewhere else, 38 per cent of the
    voicing's, 15 per cent of the frication's and 14 per cent of the third
    formant's. Treating every parameter as continuous is what made a word
    generated from these tables fluctuate in volume.
    """
    word, body = job
    ph = marked(body)
    if not ph:
        return []
    runs = tap(PROBE, body)
    got = units(ph)
    if len(runs) != len(got) + 1:
        return [(None, len(got), len(runs))]
    # The running value a parameter holds, across the whole utterance, so a
    # jump can be told from a continuation.
    running = {}
    out = []
    for i, (unit, left, right, stress) in enumerate(got):
        for name, gs in runs[i][2].items():
            if name in SKIP:
                continue
            items = []
            for _, _, v0, v1, ends in gs:
                if not ends:
                    # A stretch that runs past the segment still says where
                    # it begins, and that start is the segment's own. /m/'s
                    # nasal zero jumps to 350 and ramps on into the vowel
                    # after it; dropping the stretch outright dropped the
                    # 350, and a nasal without its murmur is a glide --
                    # `abandonment' came out as abandonwend.
                    # Always, not only where it differs from what came
                    # before. A segment that says nothing lets the stretch
                    # before it run past to whatever the segment after wants:
                    # in `abandonment' the /n/ before the /m/ ramped its
                    # nasal zero down to the default 200 because the /m/ had
                    # no entry to stop at, and the /m/ had no entry because
                    # its 350 matched the /n/'s and so read as no jump.
                    items.append((v0, None))
                    running[name] = v1
                    continue
                # Nothing has run before the first stretch of an utterance,
                # so its start is not a jump. Calling it one recorded a jump
                # for every parameter of every word's first segment, which
                # put 24,187 exceptions in the table for `fl' -- a
                # parameter that never leaves nought.
                jump = (None if name not in running or running[name] == v0
                        else v0)
                items.append((jump, v1))
                running[name] = v1
            out.append(((unit, left, right, stress, name),
                        (gs[0][2], tuple(items),
                         not gs[-1][4]),
                        word))
    return out


def truncates(short, full, start):
    """Whether `short' is `full' stopped part way through.

    Only the ends lie on the path: a target written as a start with no end
    of its own says where a run-through begins and not where it goes. Every
    end before the last has to be one of the full sequence's, in order, and
    the last has to lie between where the segment started and the ends it
    still had to reach -- a trajectory cut short drops the breakpoints it
    never got to and leaves only the value it stopped at. /E/ between /s/
    and /l/ reaches 1650 then 1500 with room and a single 1575 without.
    """
    a = tuple(v for _, v in short if v is not None)
    b = tuple(v for _, v in full if v is not None)
    if not a:
        return True
    if not b or len(a) > len(b):
        return False
    if a[:-1] != b[:len(a) - 1]:
        return False
    rest = (start,) + b[len(a) - 1:]
    return min(rest) <= a[-1] <= max(rest)


def setup(probe):
    global PROBE
    PROBE = probe


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    probe = os.path.abspath(argv[1])
    tag = argv[2] if len(argv) > 2 and not argv[2].startswith("-") else "enus"

    def opt(name, dflt):
        return argv[argv.index(name) + 1] if name in argv else dflt

    limit = int(opt("--words", "0"))
    jobs = int(opt("--jobs", str(os.cpu_count() or 4)))
    out = opt("--out", os.path.join(ROOT, "lang", tag, "%s.segments" % tag))

    load = opt("--load", None)
    save = opt("--save", None)
    corpus = opt("--corpus", "words")
    words = [] if corpus == "fill" else annotations(tag)
    if corpus in ("fill", "both"):
        words = words + fill_corpus(tag)
    if limit:
        words = words[:limit]
    sys.stderr.write("segments: %d words, %d jobs\n" % (len(words), jobs))

    shapes = collections.defaultdict(collections.Counter)
    held = collections.defaultdict(collections.Counter)
    holdout = float(opt("--holdout", "0"))
    cut = int(len(words) * (1 - holdout)) if holdout else len(words)
    example = {}
    seen = collections.Counter()
    skipped = 0
    done = 0
    if load:
        sys.stderr.write("segments: reading %s\n" % load)
        for line in open(load):
            f = line.rstrip("\n").split("\t")
            key = tuple(f[:5])
            sh = tuple(int(v) for v in f[5].split(",")) if f[5] else ()
            shapes[key][(sh, int(f[6]), f[7] == "1")] += int(f[8])
            seen[key] += int(f[7])
            example.setdefault(key, "?")
        words = []
    with ProcessPoolExecutor(jobs, initializer=setup,
                             initargs=(probe,)) as pool:
        for got in pool.map(harvest, words, chunksize=32):
            done += 1
            if done % 2000 == 0:
                sys.stderr.write("segments: %d words\n" % done)
            if got and got[0][0] is None:
                skipped += 1
                continue
            for key, gaps, word in got:
                seen[key] += 1
                # The targets alone. Every span is the duration model's,
                # not only the last: /n/ before /t/ after an /X/ reaches 350
                # then 200 over 63 and 49 milliseconds in one word and over
                # 43 and 29 in another, the same two targets either way.
                # A target repeated is a breakpoint that changes nothing:
                # the line between two equal values is the same line whether
                # it is drawn in one stretch or two, so collapsing them is
                # lossless and stops one word's extra breakpoint reading as
                # a different segment.
                start, targets, runson = gaps
                shape = []
                for item in targets:
                    if shape and shape[-1] == item and item[0] is None:
                        continue
                    shape.append(item)
                where = shapes if done <= cut else held
                where[key][(tuple(shape), start, runson)] += 1
                example.setdefault(key, word)

    # Where a key was seen with more than one shape, the longest is the
    # segment as the rules wrote it and the others are it cut short: a
    # duration model that shortens a segment stops the trajectory where the
    # segment now ends and writes the value it had got to, so /n/ before
    # /i/ after an /o/ reaches 1500 then 1750 when there is room and a
    # single 1625 when there is not, which is the midpoint of the two. A
    # shape that is a truncation of the longest is not a disagreement; one
    # that is not is.
    if save:
        with open(save, "w") as f:
            for key, obs in shapes.items():
                for (sh, start, runson), n in obs.items():
                    f.write("%s\t%s\t%d\t%d\t%d\n"
                            % ("\t".join(key),
                               ",".join(str(v) for v in sh), start,
                               1 if runson else 0, n))

    table = {}
    clipped = 0
    clash = collections.Counter()
    rejected = []
    for key, seenshapes in shapes.items():
        pick = max(seenshapes,
                   key=lambda ob: (len(ob[0]), seenshapes[ob]))
        best = pick[0]
        # Whether a segment's last stretch has a target of its own is
        # duration-dependent -- it depends on how many stretches there are --
        # so it is recorded as whichever way it usually goes and is not part
        # of what makes two observations disagree. Counting it took the
        # disagreements from 11,388 to 27,837 and said nothing new.
        votes = collections.Counter()
        for sh, start, ro in seenshapes:
            if sh == best:
                votes[ro] += seenshapes[(sh, start, ro)]
        table[key] = (best, votes.most_common(1)[0][0] if votes else False)
        for sh, start, ro in seenshapes:
            if sh == best:
                continue
            if truncates(sh, best, start):
                clipped += 1
            else:
                clash[key] += seenshapes[(sh, start, ro)]
                rejected.append((key, best, sh, seenshapes[(sh, start, ro)]))
    if opt("--clashes", None):
        with open(opt("--clashes", None), "w") as f:
            for key, best, sh, n in sorted(rejected):
                f.write("%s\tkept %s\tagainst %s\tin %d words\n"
                        % (" ".join(key), best, sh, n))

    # A base and its exceptions, not a line a context. Most of the cross
    # product says the same thing -- eleven parameters hold one value across
    # better than fifteen thousand of the seventeen thousand segments -- and
    # a flat dump buries what a neighbour actually changes under four
    # hundred thousand lines that repeat. This is the shape the rules
    # themselves have: a base locus and a handful of overrides.
    base = {}
    grouped = collections.defaultdict(list)
    for (unit, left, right, stress, name), value in table.items():
        grouped[(unit, stress, name)].append(((left, right), value))
    for k, rows in grouped.items():
        common = collections.Counter(t for _, t in rows)
        base[k] = common.most_common(1)[0][0]

    def spell(value):
        targets, runson = value
        out = []
        for j, v in targets:
            if v is None:
                out.append("%d:" % j)          # a start with no end of its own
            elif j is None:
                out.append(str(v))
            else:
                out.append("%d:%d" % (j, v))
        return " ".join(out) + (" >" if runson else "")

    # The parameters a context cannot move go in their own file, keyed on
    # the phoneme and its stress and nothing else.
    plain = opt("--phonemes",
                os.path.join(ROOT, "lang", tag, "%s.phonemes" % tag))
    with open(plain, "w") as f:
        f.write("# What each phoneme's parameters do, where no neighbour "
                "moves them.\n")
        f.write("#\n")
        f.write("# One line a phoneme, a stress and a parameter. The "
                "nineteen parameters\n")
        f.write("# here are the ones a context does not decide: four never "
                "vary at all and\n")
        f.write("# the rest are settled by the phoneme alone better than "
                "88 times in a\n")
        f.write("# hundred. The eight that a context does decide are in "
                "%s.segments.\n" % tag)
        f.write("#\n")
        f.write("# Written by tools/module/segments.py. See "
                "docs/authoring.md.\n")
        for k in sorted(grouped):
            if k[2] in CONTEXTUAL:
                continue
            f.write("%s %s %-4s %s\n" % (k[0], k[1], k[2], spell(base[k])))

    lines = 0
    with open(out, "w") as f:
        f.write("# What each segment of %s is made of, as targets.\n" % tag)
        f.write("#\n")
        f.write("# A `base' line is what a phoneme's parameter does at a "
                "stress, whatever\n")
        f.write("# is either side of it. Any other line names a SET of "
                "phonemes that may\n")
        f.write("# stand to the left and a set that may stand to the "
                "right, `.' for a\n")
        f.write("# word edge, and says what those contexts do instead. A "
                "set is written\n")
        f.write("# as the phonemes run together.\n")
        f.write("#\n")
        f.write("# The numbers are the targets the parameter reaches, in "
                "order. One\n")
        f.write("# written `from:to' starts somewhere other than where the "
                "stretch before\n")
        f.write("# left off, which happens for two fifths of the "
                "aspiration's stretches\n")
        f.write("# and a seventh of the third formant's. A\n")
        f.write("# trailing `>' means the last stretch has no target of its "
                "own: the\n")
        f.write("# engine draws no breakpoint there and the stretch joins "
                "the next\n")
        f.write("# segment's first. What it\n")
        f.write("# starts from is whatever the segment before left behind "
                "and is not here;\n")
        f.write("# how long each stretch takes is the duration model's and "
                "is not either.\n")
        f.write("#\n")
        f.write("# The base of a parameter a context cannot move is in "
                "%s.phonemes,\n" % tag)
        f.write("# one line a phoneme; only the eight it can are based "
                "here. Every\n")
        f.write("# parameter's exceptions are here either way.\n")
        f.write("#\n")
        f.write("# Harvested by tools/module/segments.py. See "
                "docs/authoring.md.\n")
        for k in sorted(grouped):
            unit, stress, name = k
            # The base of a parameter a context cannot move lives in the
            # phonemes file; its exceptions still live here, because
            # dropping them is not free. Doing so cost `money' its exactness
            # -- nought to 25 per cent of the signal -- for a file two
            # thirds the size, which is the wrong trade.
            if name in CONTEXTUAL:
                f.write("%s %s %-4s base %s\n"
                        % (unit, stress, name, spell(base[k])))
            lines += 1
            # One line a rectangle rather than a line a context. A value
            # holds over a set of left neighbours crossed with a set of
            # right ones far more often than not -- 7,785 of 13,592 value
            # blocks are a single such rectangle -- and where it does not,
            # a handful of rectangles covers it. Writing the key once for
            # each rather than once for each of three quarters of a million
            # contexts is what takes the file from fifteen megabytes to one.
            byvalue = collections.defaultdict(set)
            for (left, right), value in grouped[k]:
                if value != base[k]:
                    byvalue[value].add((left, right))
            # A value is a tuple of (jump or None, target) pairs and a
            # flag, so it does not order; sort by how it is written instead.
            for value in sorted(byvalue, key=spell):
                bycontext = {}
                for left, right in byvalue[value]:
                    bycontext.setdefault(left, set()).add(right)
                rects = collections.defaultdict(list)
                for left, rights in bycontext.items():
                    rects["".join(sorted(rights))].append(left)
                for rights in sorted(rects):
                    f.write("%s %s %-4s %s %s %s\n"
                            % (unit, stress, name,
                               "".join(sorted(rects[rights])), rights,
                               spell(value)))
                    lines += len(rects[rights]) * 0
                lines += 1

    if holdout:
        known = predicted = truncated = missing = wrong = 0
        for key, obs in held.items():
            for (sh, start), n in obs.items():
                if key not in table:
                    missing += n
                    continue
                known += n
                if sh == table[key]:
                    predicted += n
                elif truncates(sh, table[key], start):
                    truncated += n
                else:
                    wrong += n
        total = known + missing
        sys.stderr.write(
            "segments: held out %d words. %d segment parameters, %d exactly "
            "as the table says, %d the same cut short, %d wrong, %d in a "
            "context the table never saw\n"
            % (len(words) - cut, total, predicted, truncated, wrong,
               missing))

    kinds = len(set(k[:4] for k in table))
    sys.stderr.write("segments: %d written as a base and %d exceptions\n"
                     % (len(grouped), lines - len(grouped)))
    sys.stderr.write("segments: %d segments, %d lines, %d seen more than "
                     "once, %d shapes that were the same segment cut short, "
                     "%d disagreeing, %d words whose runs did not line up\n"
                     % (kinds, len(table),
                        sum(1 for k in seen if seen[k] > 1), clipped,
                        len(clash), skipped))
    if clash:
        byparm = collections.Counter()
        for key, n in clash.items():
            byparm[key[4]] += 1
        sys.stderr.write("segments: keys disagreeing, by parameter: %s\n"
                         % "  ".join("%s %d" % kv
                                     for kv in byparm.most_common()))
        for key, n in clash.most_common(6):
            sys.stderr.write("   %s disagreed %d times, first in %s\n"
                             % (" ".join(key), n, example[key]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
