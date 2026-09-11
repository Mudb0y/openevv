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
    """Each word's phonemes, as the engine itself reports them."""
    path = os.path.join(ROOT, "test", "samples", "%s.words" % tag)
    out = []
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
    alpha = sorted(set(
        p for _, body in annotations(tag) for p, _ in marked(body)))
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
    """One word's segments, as table entries."""
    word, body = job
    ph = marked(body)
    if not ph:
        return []
    runs = tap(PROBE, body)
    got = units(ph)
    # One run a phoneme and one more for the trailing silence. Anything else
    # means the runs and the phonemes are not the same list, and guessing
    # which is which would put every segment after the mismatch under the
    # wrong key.
    if len(runs) != len(got) + 1:
        return [(None, len(got), len(runs))]
    out = []
    for i, (unit, left, right, stress) in enumerate(got):
        for name, gs in runs[i][2].items():
            if name in SKIP:
                continue
            # The value the segment starts from comes with it. It is not
            # part of the key -- it is whatever the segment before left
            # behind -- but it is what a trajectory cut short in its first
            # stretch was heading away from, so the truncation test needs it.
            #
            # And whether the last gap runs on past the segment, which is
            # the one bit a generator cannot do without. A segment with
            # fewer targets than pieces either holds its last target -- /A/
            # in `abbey' is a piece of nought and one of 157 both ending at
            # 1650 -- or has no target for that piece at all, in which case
            # the engine draws no breakpoint and the piece joins the next
            # segment's first: schwa in `aback' is 62 milliseconds and the
            # /b/ after it 15, and the engine draws one gap of 77 ending at
            # the /b/'s locus. Collapsing repeated targets loses the
            # difference, so it is kept here.
            out.append(((unit, left, right, stress, name),
                        (gs[0][2],
                         tuple(v for _, _, _, v, ends in gs if ends),
                         not gs[-1][4]),
                        word))
    return out


def truncates(short, full, start):
    """Whether `short' is `full' stopped part way through.

    Every target before the last has to be one of the full sequence's, in
    order, and the last has to lie between the target it stopped after and
    the next one -- an interpolated endpoint being what a trajectory cut
    short writes down. Cut inside the first stretch, the value it stopped
    at lies between where the segment started and its first target, which
    is why the start is wanted here and nowhere else.
    """
    if len(short) > len(full):
        return False
    if not short:
        return True
    if short[:-1] != full[:len(short) - 1]:
        return False
    # Where along the rest of the path it stopped is not recoverable, and
    # need not be. Cutting a segment short can drop the breakpoints it had
    # not reached and leave only the value it had got to, so /E/ between
    # /s/ and /l/ reaches 1650 then 1500 with room and a single 1575 without
    # -- the midpoint, and neither of the two targets. What can be checked
    # is that it lies on the path: between where the segment started and the
    # targets it had still to reach.
    rest = (start,) + full[len(short) - 1:]
    return min(rest) <= short[-1] <= max(rest)


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
                for v in targets:
                    if not shape or shape[-1] != v:
                        shape.append(v)
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
        return " ".join(str(v) for v in targets) + (" >" if runson else "")

    lines = 0
    with open(out, "w") as f:
        f.write("# What each segment of %s is made of, as targets.\n" % tag)
        f.write("#\n")
        f.write("# A `base' line is what a phoneme's parameter does at a "
                "stress, whatever\n")
        f.write("# is either side of it. Any other line names the phoneme "
                "to the left and\n")
        f.write("# the one to the right, `.' for a word edge, and says what "
                "that context\n")
        f.write("# does instead.\n")
        f.write("#\n")
        f.write("# The numbers are the targets the parameter reaches, in "
                "order, and a\n")
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
        f.write("# Harvested by tools/module/segments.py. See "
                "docs/authoring.md.\n")
        for k in sorted(grouped):
            unit, stress, name = k
            f.write("%s %s %-4s base %s\n"
                    % (unit, stress, name, spell(base[k])))
            lines += 1
            for (left, right), value in sorted(grouped[k]):
                if value == base[k]:
                    continue
                f.write("%s %s %-4s %s %s %s\n"
                        % (unit, stress, name, left, right, spell(value)))
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
