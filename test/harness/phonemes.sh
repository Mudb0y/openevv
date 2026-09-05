#!/usr/bin/env bash
#
# What the language decided a word was made of, held against what IBM's own
# engine decided.
#
# This is the oracle for eciGeneratePhonemes and for the Delta runtime's
# printing layer underneath it. Both sides run the same driver over the same
# case file and print one line of phonemes per case, so a difference names
# the case and shows both answers rather than being a hash that moved.
#
# It wants IBM's objects and, off Windows, Wine -- the same as test/suite.sh
# and for the same reason. `make phonemes' builds both sides.
#
# usage: EVV_LANG=<tag> test/harness/phonemes.sh [cases.txt ...]
#
# The language says which pair of binaries to run, and the names carry it for
# the reason the probes' do: one language's engine held against another's
# reference is a difference that is not one. With no case file it takes that
# language's plain file, and `make phonemes' hands it the plain, annotation
# and long files a language has.

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)

lang=${EVV_LANG:-enus}
suf=""
[ "$lang" = enus ] || suf="-$lang"

ours=$root/build/phonemes$suf
theirs=$root/build/reference$suf/phontry.exe

for f in "$ours" "$theirs"; do
    if [ ! -x "$f" ]; then
        echo "phonemes: $f is not built; run 'make phonemes LANGS=lang/$lang'" >&2
        exit 2
    fi
done

# On anything but Windows the reference binary is a PE and wants Wine in
# front of it. The scripts work that out rather than being told.
run_theirs() {
    case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) "$theirs" "$1" ;;
    *)                    wine "$theirs" "$1" ;;
    esac
}

cases=("$@")
[ ${#cases[@]} -eq 0 ] && cases=("$root/test/cases/plain$suf.txt")

total=0
moved=0

for file in "${cases[@]}"; do
    a=$(mktemp); b=$(mktemp)
    # Only the answers. The reference build writes IBM's own trace log to
    # standard output -- the RAL's semaphore complaints, which are Wine
    # rather than the engine -- so both sides are cut to the lines that are
    # an answer, and the reference's Windows line endings come off.
    "$ours" "$file" 2>/dev/null | grep '^\[' > "$a"
    run_theirs "$file" 2>/dev/null | tr -d '\r' | grep '^\[' > "$b"

    n=$(wc -l < "$a")
    total=$((total + n))

    if cmp -s "$a" "$b"; then
        echo "$(basename "$file"): $n cases, all as IBM's"
    else
        i=0
        while IFS= read -r mine && IFS= read -r ibm <&3; do
            i=$((i + 1))
            [ "$mine" = "$ibm" ] && continue
            moved=$((moved + 1))
            echo "$(basename "$file") case $i differs"
            echo "  ours:  $mine"
            echo "  IBM's: $ibm"
        done < "$a" 3< "$b"
    fi
    rm -f "$a" "$b"
done

if [ "$moved" -eq 0 ]; then
    echo "phonemes: $lang, $total cases, every one as IBM's"
    exit 0
fi
echo "phonemes: $lang, $total cases, $moved different"
exit 1
