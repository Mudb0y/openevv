#!/usr/bin/env bash
#
# The eight dictionary calls, held against what IBM's own engine answers.
#
# Both sides run the same driver and print the same lines, so a difference
# names the call and shows both answers rather than moving a hash. Every line
# is an answer: a return code, a key, a translation, a part of speech.
#
# What it reaches depends on the language. On the plain road -- every language
# whose dictionary the engine keeps itself -- the extended calls answer that
# the volume is wrong, and that is the whole of what those roads do. Japanese
# is the one language with a dictionary of its own under the romanizer, so
# there the extended calls go somewhere: through `RomInstance' and into
# `ConverterInterface', which nothing else in this tree exercises. Run it for
# Japanese as well as for English or that half is only ever compiled.
#
# It wants IBM's objects and, off Windows, Wine. `make dict' builds both, and
# `make dict LANGS=lang/jajp' does the same for Japanese.
#
# usage: EVV_LANG=<tag> test/harness/dict.sh

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)

# Which language, and so which pair of binaries. The names carry the language
# for the same reason the probes' do: holding one language's engine against
# another's reference is a difference that is not one.
lang=${EVV_LANG:-enus}
suf=""
[ "$lang" = enus ] || suf="-$lang"

ours=$root/build/dict$suf
theirs=$root/build/reference$suf/dicttry.exe

for f in "$ours" "$theirs"; do
    if [ ! -x "$f" ]; then
        echo "dict: $f is not built; run 'make dict LANGS=lang/$lang'" >&2
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
    echo "dict: $lang, $n answers, every one as IBM's"
    rm -f "$a" "$b"
    exit 0
fi

echo "dict: $lang, the two engines differ"
diff -u "$b" "$a" | sed -n '3,40p'
rm -f "$a" "$b"
exit 1
