#!/usr/bin/env python3
"""Find text the engine cannot survive.

Given a few strings that kill it, try every one-letter change to each --
substitution, insertion, deletion, over the letters and the apostrophe -- and
keep the ones that kill it too. Then do the same to those. The engine is the
oracle: a run that neither answers nor gives the utterance up is a find.

This is how test/cases/crashers.txt was made. Point it at a build with no
guards in it -- git stash, or an older tag -- to find more of them, and at the
build in the tree to check that none of them is left.

    tools/crash-search.py build/evv test/cases/crashers.txt 2

The third argument is how many rounds. One round off three strings takes a few
seconds; two takes about six minutes and turned three into ten thousand; three
would be four million candidates and several hours.
"""

import os
import subprocess
import sys
import string
import tempfile

ALPHA = string.ascii_lowercase + "'"


def neighbours(w):
    out = set()
    for i in range(len(w)):
        out.add(w[:i] + w[i + 1:])
        for c in ALPHA:
            out.add(w[:i] + c + w[i + 1:])
    for i in range(len(w) + 1):
        for c in ALPHA:
            out.add(w[:i] + c + w[i:])
    out.discard(w)
    return {x for x in out if x}


def killers(binary, words, jobs, limit):
    """Which of words the engine does not come back from."""
    bad = []
    with tempfile.TemporaryDirectory() as d:
        running = []

        def reap(all_of_them):
            while running and (all_of_them or len(running) >= jobs):
                w, pr = running.pop(0)
                try:
                    rc = pr.wait(timeout=limit)
                except subprocess.TimeoutExpired:
                    pr.kill()
                    pr.wait()
                    rc = None
                # Nought is a word spoken and one is an utterance given up.
                # Anything else is a signal, and None is a walk going round.
                if rc not in (0, 1):
                    bad.append(w)

        for n, w in enumerate(words):
            out = os.path.join(d, "o%d.wav" % (n % jobs))
            running.append((w, subprocess.Popen(
                [binary, "-o", out, w],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)))
            reap(False)
        reap(True)
    return bad


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)

    binary = sys.argv[1]
    seedfile = sys.argv[2]
    rounds = int(sys.argv[3]) if len(sys.argv) > 3 else 2
    jobs = int(os.environ.get("EVV_CRASH_JOBS", os.cpu_count() or 4))
    limit = int(os.environ.get("EVV_CRASH_TIMEOUT", "30"))

    seeds = [l.strip() for l in open(seedfile) if l.strip()
             and not l.startswith("#")]
    if not seeds:
        sys.exit("crash-search: no seeds in " + seedfile)

    print("crash-search: %d seeds, %d rounds, %d at a time"
          % (len(seeds), rounds, jobs), file=sys.stderr)

    known = set(seeds)
    frontier = set(seeds)

    for r in range(rounds):
        candidates = set()
        for w in frontier:
            candidates |= neighbours(w)
        candidates -= known

        found = set(killers(binary, sorted(candidates), jobs, limit))
        print("crash-search: round %d, %d tried, %d new"
              % (r + 1, len(candidates), len(found)), file=sys.stderr)

        known |= found
        frontier = found
        if not found:
            break

    for w in sorted(known):
        print(w)


if __name__ == "__main__":
    main()
