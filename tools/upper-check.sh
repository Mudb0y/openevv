#!/usr/bin/env bash
#
# Hold a rule written in the upper form against the rule IBM compiled.
#
# There is no byte comparison to be had here and there was never going to be:
# a compiler of ours would have to make the same register choices and put the
# same instructions in the same order as IBM's did, which is a study of their
# compiler rather than of this engine, and it would forbid us writing anything
# they never wrote. eng_ph_F_dur says it in one line -- theirs pushes the state
# register, does two stores and then calls succeed; anything straightforward
# does the stores and then the push.
#
# So the standard is what the engine can observe. With tracing on it says every
# rule it enters and every call it makes with the arguments, and that is what
# the audio is made of: a rule that enters the same rules and makes the same
# calls with the same values in the same order is the same rule, whatever the
# bytes look like. This speaks the seven plain cases through a build carrying
# the authored rule and through one carrying IBM's, and the traces have to
# match.
#
# Three things are left out of the comparison and all three are the harness.
# The interpreter prints every store it makes, and the authored rule may keep a
# value in a different place -- what the engine sees of a store is the call
# that reads the value afterwards. It remarks when the depth a call carries
# disagrees with the area's, which is a remark about IBM's compiler batching
# its pops and ours not. And addresses in the arena are masked, because a frame
# with different locals in it lands somewhere else and where it landed is not
# what this is checking.
#
# The stores are held against each other as well, and reported rather than
# required. Where the authored rule keeps its values where IBM's did, that
# comparison holds too and is worth knowing; where it does not, an extra or
# missing store line is the harness and not a difference in what the engine
# did. The address in such a line is in the arena and therefore masked, so what
# it says is the width, the value and how many there were.
#
# The audio is the third comparison and it is not the weakest. A rule whose
# whole effect is to write a variable is invisible to a trace of calls -- one
# store, nothing reads it out loud -- and the wave file is what says the value
# was right. That is not hypothetical: setting a duration to 21 where IBM sets
# 20 passes the trace on every sentence and changes the sound of the second.
#
# One sentence to a run, because tracing costs twenty times what the synthesis
# does and feeding it that slowly faults part way through several sentences in
# one run. That is delta-check.sh's finding and it holds here.
#
# The sentences are the suite's seven plain ones and test/cases/upper.txt
# beside them, which is this harness's own; EVV_UPPER_CASES names another list
# of files, which is how the workflow runs the short one. The seven were not
# enough and saying why is worth more than the fix: has_lex_prefix takes one
# alternative when the word carries the prefix "re" and another when it does
# not, not one of the seven has such a word in it, and so the value that
# alternative hands over could be changed to anything and every sentence still
# passed. A check has to speak what the rule it is checking reads, and the way
# to know it does is to trace one sentence and look for the value.
#
# And one thing it will do on request rather than by default. A rule that calls
# a primitive where IBM's called a wrapper for it does the same work, but the
# wrapper is a rule and a run says it was entered, so the traces differ by that
# line and by nothing else. -sound asks for the audio to be the standard and
# reports how far the traces are apart instead of stopping on it. That is what
# a constant of ours is proved with: the same bytes read from a store of ours
# rather than from IBM's, where nothing may change but the sound is the thing
# that would.
#
# usage: upper-check.sh            every .up rule file in the tree
#        upper-check.sh <file>...  the ones named
#        upper-check.sh -sound ... the audio is the standard, not the trace

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
# The language as a tag, which is what everything else here calls it:
# `EVV_LANG=dede' picks German, as it does for the suite. Only English has
# been run through this, because only English has a rule written in the upper
# form; the rest is here so that the day another does, this is not the thing
# that has to be worked out.
tag=${EVV_LANG:-enus}
lang=lang/$tag
suf=
[ "$tag" = enus ] || suf=-$tag
export EVV_NOTATION_LANG=$tag
rules="$here/$lang/delta_rules_$tag.c"
header="$here/$lang/delta_rules_$tag.h"
work=$(mktemp -d)

restore() {
    [ -f "$work/kept.c" ] && cp "$work/kept.c" "$rules"
    [ -f "$work/kept.h" ] && cp "$work/kept.h" "$header"
    rm -rf "$work"
}
trap restore EXIT

sound=0
if [ $# -gt 0 ] && [ "$1" = "-sound" ]; then
    sound=1
    shift
fi

files=("$@")
if [ ${#files[@]} -eq 0 ]; then
    mapfile -t files < <(ls "$here/$lang/rules"/*.up 2>/dev/null \
                         | grep -v /wrappers.up)
fi
[ ${#files[@]} -gt 0 ] || { echo "upper: no rule written in the upper form" >&2
                            exit 2; }

named=$(for f in "${files[@]}"; do awk '$1 == "rule" { print $2 }' "$f"; done)
[ -n "$named" ] || { echo "upper: those files name no rule" >&2; exit 2; }
echo "upper: $(echo "$named" | wc -w) rules: $(echo $named)"

build() {
    rm -f "$here/build/probe$suf"
    make -C "$here" EVVLANG="$lang" RULES=bytecode probe >/dev/null || exit 1
    cp "$here/build/probe$suf" "$work/probe.$1"
}

speak() {
    DELTA_RULE_TRACE=200000 timeout 900 "$work/probe.$1" \
        "$2" "$work/$1.wav" 2>"$work/$1.raw" >/dev/null
    sed -E 's/\b1[0-9a-f]{7}\b/ARENA/g' "$work/$1.raw" \
        | grep -v '^rules run:\|in the area' > "$work/$1.full"
    grep -v '^# store ' "$work/$1.full" > "$work/$1.trace"
    # The same again with the running count of rules entered taken off, for
    # saying how far two traces are apart. A trace that is short of one entry
    # differs in the count on every line after it, so the raw figure would be
    # the length of the trace rather than the size of the difference. Nothing
    # is lost by masking it here: two runs that enter the same rules in the
    # same order count them the same, so the strict comparison above is the
    # one that reads it.
    sed -E 's/^rule [0-9]+:/rule:/' "$work/$1.trace" > "$work/$1.plain"
}

# IBM's, as the tree stands, kept so that the tree is put back whatever
# happens next.
cp "$rules" "$work/kept.c"
cp "$header" "$work/kept.h"
echo "upper: building the rules as they are"
build ibm

echo "upper: compiling the upper form into the tree"
python3 "$here/tools/delta-notation.py" authored >/dev/null || exit 1
if cmp -s "$rules" "$work/kept.c"; then
    echo "upper: the rules did not change, so nothing here is being tested" >&2
    exit 1
fi
build ours

n=0
lines=0
stores=0
while IFS= read -r sentence; do
    [ -n "$sentence" ] || continue
    n=$((n + 1))
    speak ibm "$sentence"
    speak ours "$sentence"

    if ! cmp -s "$work/ibm.trace" "$work/ours.trace"; then
        apart=$(diff "$work/ibm.plain" "$work/ours.plain" \
                | grep -c '^[<>]')
        if [ "$sound" = 0 ]; then
            echo "upper: sentence $n parts company" >&2
            diff "$work/ibm.trace" "$work/ours.trace" | head -20 >&2
            exit 1
        fi
        echo "upper: sentence $n, $apart trace lines apart of" \
             "$(wc -l < "$work/ibm.trace")"
    fi
    if ! cmp -s "$work/ibm.wav" "$work/ours.wav"; then
        echo "upper: sentence $n sounds different" >&2
        exit 1
    fi
    lines=$((lines + $(wc -l < "$work/ibm.trace")))
    if cmp -s "$work/ibm.full" "$work/ours.full"; then
        stores=$((stores + 1))
        echo "upper: sentence $n, the same, stores and all"
    else
        echo "upper: sentence $n, the same"
    fi
done < <(cat ${EVV_UPPER_CASES:-"$here/test/cases/plain.txt" \
                                 "$here/test/cases/upper.txt"})

echo "upper: the same, call for call, over $lines lines of $n sentences"
echo "upper: and the same store for store in $stores of the $n"
