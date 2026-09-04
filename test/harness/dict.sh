#!/usr/bin/env bash
#
# The eight dictionary calls, held against what IBM's own engine answers.
#
# Both sides run the same driver and print the same lines, so a difference
# names the call and shows both answers rather than moving a hash. Every line
# is an answer: a return code, a key, a translation, a part of speech.
#
# What it can reach is the plain road, which is every language in this tree.
# The extended calls are for Chinese, Korean and Japanese; this SDK has no
# Chinese or Korean at all and Japanese does not build, so what is checked
# there is that asking for the extended volume answers that the volume is
# wrong -- which is the whole of what those roads do for anything here.
#
# It wants IBM's objects and, off Windows, Wine. `make dict' builds both.
#
# usage: test/harness/dict.sh

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)

ours=$root/build/dict
theirs=$root/build/reference/dicttry.exe

for f in "$ours" "$theirs"; do
    if [ ! -x "$f" ]; then
        echo "dict: $f is not built; run 'make dict'" >&2
        exit 2
    fi
done

run_theirs() {
    case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) "$theirs" ;;
    *)                    wine "$theirs" ;;
    esac
}

a=$(mktemp); b=$(mktemp)
# The reference build writes IBM's own trace log to standard output -- the
# RAL's semaphore complaints, which are Wine rather than the engine -- so both
# sides are cut to the lines that are an answer, and the reference's Windows
# line endings come off.
"$ours" 2>/dev/null | grep -vE '^log\[|^$' > "$a"
run_theirs 2>/dev/null | tr -d '\r' | grep -vE '^log\[|^$' > "$b"

n=$(wc -l < "$a")

if cmp -s "$a" "$b"; then
    echo "dict: $n answers, every one as IBM's"
    rm -f "$a" "$b"
    exit 0
fi

echo "dict: the two engines differ"
diff -u "$b" "$a" | sed -n '3,40p'
rm -f "$a" "$b"
exit 1
