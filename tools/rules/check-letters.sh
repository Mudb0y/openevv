#!/usr/bin/env bash
#
# What one word's letter rules do differently from IBM's.
#
# tools/rules/check-upper.sh holds an authored rule to entering the same rules
# and making the same calls as the one it stands in for, over whole sentences.
# A letter rule written in lang/<tag>/letters is not that: it says `afresh',
# so that check leaves it alone deliberately, and what is wanted here is the
# opposite -- the two traces will differ, and the question is where.
#
# So this speaks one word through a build carrying IBM's letter rules and
# through one carrying ours, with tracing on, and prints the first place they
# part company. That is the difference between reading a rule's call list and
# guessing what it means, which has been wrong three times, and being told
# which call differs and with what arguments.
#
# The masking is check-upper.sh's and for its reasons: a value of 0x10000 or
# more is a distance into the region, which the two builds are entitled to
# place differently, and comparing those compares the layout rather than the
# behaviour. Everything below that is compared exactly, which is every
# immediate a rule carries.
#
# What is compared is what the rules do to the word, not every call they make.
# A rule of ours legitimately does less work than IBM's -- it tests the run it
# wants where IBM tests one letter at a time and backtracks -- so the calls are
# hundreds of lines apart even where the answer is identical, and that noise
# hides the one line that matters. So the comparison is over the calls that
# change the spine: what phones went in, what came out, and what was marked.
# EVV_LETTERS_ALL=1 compares every call instead, which is what to reach for
# when the two agree on every insertion and the sound still differs.
#
# And --sound asks the other question: not which call differs but whether
# anything can be heard to. It speaks every word of a list through both builds
# and names each word whose samples differ. That is what the two gates cannot
# say about a letter rule. test/words.sh reads the phoneme report, which prints
# a phone's name and nothing else it carries, so an n laid down where IBM lays
# down a velar nasal marked to be written as n reads the same there and sounds
# different; and test/matrix.sh hears only what its sentences happen to hold,
# which in Italian was no n before a hard c, g or q at all.
#
# usage: tools/rules/check-letters.sh <tag> <word>...
#        tools/rules/check-letters.sh <tag> -f <file>   a word to a line
#        tools/rules/check-letters.sh <tag> --sound [<file>]
#                                         the language's word list by default
#
# EVV_LETTERS_LINES says how many differing lines to print, twenty by default,
# and EVV_LETTERS_JOBS how many words --sound speaks at once, eight by default.

set -u
tools=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
here=$(dirname "$tools")
cd "$here"

[ $# -ge 2 ] || { echo "usage: check-letters.sh <tag> <word>..." >&2; exit 2; }
tag=$1; shift
lang=lang/$tag
suf=
[ "$tag" = enus ] || suf=-$tag
export EVV_NOTATION_LANG=$tag
rules="$here/$lang/delta_rules_$tag.c"
work=$(mktemp -d) || exit 1

words=()
sound=
if [ "${1:-}" = "--sound" ]; then
    sound=${2:-test/cases/words-$tag.txt}
    [ -r "$sound" ] || { echo "no such file: $sound" >&2; exit 2; }
elif [ "${1:-}" = "-f" ]; then
    [ -r "${2:-}" ] || { echo "no such file: ${2:-}" >&2; exit 2; }
    while IFS= read -r w; do
        case $w in ''|\#*) continue ;; esac
        words+=("$w")
    done < "$2"
else
    words=("$@")
fi

# The three files a build compiles are written out of the text rather than
# kept, so whatever was written last is what is left behind. Put the module's
# own back however this ends.
restore() {
    python3 "$tools/rules/notation.py" build >/dev/null 2>&1
    [ -n "${EVV_LETTERS_KEEP:-}" ] && echo "kept $work" || rm -rf "$work"
}
trap restore EXIT

build() {
    rm -f "$here/build/probe$suf"
    nice -n 15 make -C "$here" -j3 EVVLANG="$lang" RULES=bytecode probe \
        >/dev/null || exit 1
    cp "$here/build/probe$suf" "$work/probe.$1"
}

mask() {
    python3 -c '
import re, sys
pat = re.compile(r"(?<![0-9a-fA-F])[0-9a-f]{8}(?![0-9a-fA-F])")


def one(m):
    v = int(m.group(0), 16)
    if v >= 0x80000000:
        v -= 0x100000000
    return "VAL" if abs(v) >= 0x10000 else m.group(0)


for line in sys.stdin:
    sys.stdout.write(pat.sub(one, line))
'
}

speak() {
    DELTA_RULE_TRACE=2000000 timeout 900 "$work/probe.$1" \
        "$2" "$work/$1.wav" 2>"$work/$1.raw" >/dev/null
    sed -E 's/@[0-9a-f]{8}/ARENA/g' "$work/$1.raw" \
        | grep -v '^rules run:\|in the area\|^# store ' \
        | sed -E 's/^rule [0-9]+:/rule:/' | mask > "$work/$1.full"
    if [ -n "${EVV_LETTERS_ALL:-}" ]; then
        cp "$work/$1.full" "$work/$1.trace"
    else
        grep -E '^  (insert_2pt|ins_tokens|delete_2pt|vdel_2pt|mark_s)' \
            "$work/$1.full" > "$work/$1.trace"
    fi
}

echo "letters: writing IBM's rules out of the lifted text"
python3 "$tools/rules/notation.py" rewrite >/dev/null || exit 1
cp "$rules" "$work/kept.c"
build ibm

echo "letters: compiling lang/$tag/letters in"
python3 "$tools/rules/notation.py" build >/dev/null || exit 1
# The rules a build compiles, not the binary: the binary of the first build is
# still sitting where the second one will go.
if cmp -s "$rules" "$work/kept.c"; then
    echo "letters: the rules did not change, so nothing of ours is in" >&2
    exit 1
fi
build ours

lines=${EVV_LETTERS_LINES:-20}

if [ -n "$sound" ]; then
    # One process a word and a word a line, as test/words.sh speaks them, in
    # the engine's own code set rather than UTF-8 and so under LC_ALL=C. Each
    # side writes afresh into a file of its own, and a word either side said
    # nothing for is reported as that rather than compared: a stale file from
    # the word before would otherwise read as a difference, or hide one.
    LC_ALL=C grep -v '^#' "$sound" | LC_ALL=C grep -v '^$' > "$work/list"
    export work
    one() {
        n=$1; w=$2
        for side in ibm ours; do
            rm -f "$work/s.$n.$side.wav"
            timeout 60 "$work/probe.$side" "$w" "$work/s.$n.$side.wav" \
                >/dev/null 2>&1
            [ -s "$work/s.$n.$side.wav" ] || { printf 'silent\t%s\t%s\n' \
                "$side" "$w"; rm -f "$work"/s.$n.*; return; }
        done
        cmp -s "$work/s.$n.ibm.wav" "$work/s.$n.ours.wav" \
            || printf 'differs\t%s\n' "$w"
        rm -f "$work"/s.$n.*
    }
    export -f one
    total=$(wc -l < "$work/list")
    echo "letters: speaking $total words through both"
    LC_ALL=C awk '{ printf "%d\0%s\0", NR, $0 }' "$work/list" \
        | nice -n 15 xargs -0 -n 2 -P "${EVV_LETTERS_JOBS:-8}" \
            bash -c 'one "$0" "$1"' > "$work/heard"
    moved=$(grep -c '^differs' "$work/heard")
    silent=$(grep -c '^silent' "$work/heard")
    LC_ALL=C sort "$work/heard" | head -"$lines"
    echo "letters: $moved of $total words sound different, $silent said nothing"
    [ "$moved" = 0 ] && [ "$silent" = 0 ]
    exit
fi

bad=0
for w in "${words[@]}"; do
    speak ibm "$w"
    speak ours "$w"
    if cmp -s "$work/ibm.trace" "$work/ours.trace"; then
        echo "letters: $w, the same call for call"
        continue
    fi
    apart=$(diff "$work/ibm.trace" "$work/ours.trace" | grep -c '^[<>]')
    echo "letters: $w parts company, $apart lines of $(wc -l < "$work/ibm.trace")"
    diff "$work/ibm.trace" "$work/ours.trace" | head -"$lines"
    bad=1
done
exit $bad
