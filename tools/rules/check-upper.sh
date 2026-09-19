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
# one run. That is tools/rules/check-c.sh's finding and it holds here.
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
# usage: tools/rules/check-upper.sh            every .up rule file in the tree
#        tools/rules/check-upper.sh <file>...  the ones named
#        tools/rules/check-upper.sh -sound ... the audio is the standard, not the trace

set -u
tools=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
here=$(dirname "$tools")
# The language as a tag, which is what everything else here calls it:
# `EVV_LANG=dede' picks German, as it does for the suite. English is the one
# this is for, and the only one it means anything on: what it compares is a
# rule of ours against the rule it stands in for, and English's four are the
# only upper-form rules that stand in for anything. Polish's are its own --
# there is no Polish rule of IBM's to hold them against -- so running this on
# Polish would compare Polish against the Italian it replaced and report the
# difference it was written to make.
tag=${EVV_LANG:-enus}
lang=lang/$tag
suf=
[ "$tag" = enus ] || suf=-$tag
export EVV_NOTATION_LANG=$tag
rules="$here/$lang/delta_rules_$tag.c"
work=$(mktemp -d)

# The three files a build compiles are written out of the text rather than
# kept in the tree, so what is left behind here is whatever was written last.
# An ordinary build wants the module as it means itself, which is what `build'
# writes, so that is what this puts back however it ends. Nothing needs
# copying aside: the text is the source and every form of it can be written
# again from that.
restore() {
    python3 "$tools/rules/notation.py" build >/dev/null 2>&1
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

# The rules that say `afresh'. A rule of ours that stands in for one of IBM's
# has to enter the same rules and make the same calls; one written anew has
# nothing to be held against, since it calls a primitive where IBM's called a
# wrapper for it, numbers its plants its own way, and will say what IBM never
# said. So its own trace is dropped from both sides and what holds it here is
# the audio, with test/words.sh and test/matrix.sh outside.
afresh=$(for f in "${files[@]}"; do
    awk '$1 == "rule" { name = $2 } $1 == "afresh" { print name }' "$f"
done)
[ -z "$afresh" ] || echo "upper: written afresh, so held by the sound alone:" \
                         "$(echo $afresh)"

build() {
    rm -f "$here/build/probe$suf"
    make -C "$here" EVVLANG="$lang" RULES=bytecode probe >/dev/null || exit 1
    cp "$here/build/probe$suf" "$work/probe.$1"
}

# What is left after the marked references are masked: values a rule passes
# that no declaration describes. Most are distances into the region -- a
# frame, a cell, a scan position -- and the two builds are entitled to place
# those where they like, so comparing them compares the layout rather than the
# behaviour.
#
# Numbering each distinct one by when it first appeared was tried first and
# cannot work. The slots are reused, so the same slot in one build is a
# different slot in the other at a different moment: over sentence one, 728
# references had to be remapped and 3,297 of those assignments contradicted
# each other. No relabelling exists. Rank among the values is worse again,
# because the two runs meet slightly different sets and every rank after the
# first difference shifts.
#
# Comparing them literally is worse than either -- 57,383 lines apart, since
# every frame offset then counts.
#
# So they are masked by size, which works because the two kinds are nowhere
# near each other. Measured over the whole of sentence one, 548,529 lines:
# every value that differs between the builds is at least 0x4a0f04, and every
# value below 0x10000 is identical in both. The threshold sits in that gap
# with two orders of magnitude either side, and four values in ten stay under
# exact comparison -- every immediate a rule carries, negatives included,
# which the numbering above swallowed whole.
#
# What this gives up is a large immediate, which is masked with the distances.
# If that is ever worth having back, the way is to make the bytecode's push
# sites say which argument is a reference, and mask by the mark alone.
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
    DELTA_RULE_TRACE=200000 timeout 900 "$work/probe.$1" \
        "$2" "$work/$1.wav" 2>"$work/$1.raw" >/dev/null
    sed -E 's/@[0-9a-f]{8}/ARENA/g' "$work/$1.raw" \
        | grep -v '^rules run:\|in the area' | mask > "$work/$1.full"
    grep -v '^# store ' "$work/$1.full" > "$work/$1.trace"
    # The same again with the running count of rules entered taken off, for
    # saying how far two traces are apart. A trace that is short of one entry
    # differs in the count on every line after it, so the raw figure would be
    # the length of the trace rather than the size of the difference. Nothing
    # is lost by masking it here: two runs that enter the same rules in the
    # same order count them the same, so the strict comparison above is the
    # one that reads it.
    sed -E 's/^rule [0-9]+:/rule:/' "$work/$1.trace" > "$work/$1.plain"
    # And what the two sides are actually compared on. With nothing written
    # afresh that is the trace as it stands, counter and all, which is what
    # this has always compared. With something written afresh, its own lines
    # come out of both sides -- and the running count has to go with them,
    # since a masked rule legitimately enters a different number of rules and
    # every count after it would differ. Nothing is lost: two runs that enter
    # the same rules in the same order count them the same, so the count only
    # ever restates what the lines already say.
    if [ -z "$afresh" ]; then
        cp "$work/$1.trace" "$work/$1.cmp"
    else
        drop "$work/$1.plain" > "$work/$1.cmp"
    fi
}

# Every line from entering one of those rules to leaving it, taken out. A
# trace says the call, then the rule it entered, then everything the rule did,
# then what it left with, so the span is from the call to the leaving.
drop() {
    python3 -c '
import re, sys

names = set(sys.argv[2:])
if not names:
    sys.stdout.write(open(sys.argv[1]).read())
    raise SystemExit
call = re.compile(r"^\s*([A-Za-z_][A-Za-z_0-9]*)\(")
entered = re.compile(r"^rule [0-9]*:\s*([A-Za-z_][A-Za-z_0-9]*)\(")
left = re.compile(r"^# ([A-Za-z_][A-Za-z_0-9]*) left with")

inside = None
depth = 0
for line in open(sys.argv[1]):
    if inside is None:
        m = call.match(line) or entered.match(line)
        if m and m.group(1) in names:
            inside = m.group(1)
            depth = 1
            continue
        sys.stdout.write(line)
        continue
    m = call.match(line)
    if m and m.group(1) == inside:
        depth += 1
    m = left.match(line)
    if m and m.group(1) == inside:
        depth -= 1
        if depth == 0:
            inside = None
' "$1" $afresh
}

# IBM's rules and nothing of ours, which is the side an authored rule has to
# be held against. `rewrite' is that: the lifted text alone, with every
# upper-form file left out whether the module claims it or not.
echo "upper: writing IBM's rules out of the lifted text"
python3 "$tools/rules/notation.py" rewrite >/dev/null || exit 1
cp "$rules" "$work/kept.c"
build ibm

# And the same with every upper-form file compiled in, the module's own and
# the ones lang/<tag>/rules/trials says are not. English's four are trials,
# and they are the whole point here.
echo "upper: compiling the upper form in"
python3 "$tools/rules/notation.py" authored >/dev/null || exit 1
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

    if ! cmp -s "$work/ibm.cmp" "$work/ours.cmp"; then
        apart=$(diff "$work/ibm.cmp" "$work/ours.cmp" \
                | grep -c '^[<>]')
        if [ "$sound" = 0 ]; then
            # How far apart, before the first twenty lines of it. Reading the
            # head alone once cost a week: it showed thirteen differing lines
            # and the traces were thirty-six thousand apart, so a fix was
            # believed finished when it had barely moved.
            echo "upper: sentence $n parts company, $apart lines of" \
                 "$(wc -l < "$work/ibm.cmp")" >&2
            diff "$work/ibm.cmp" "$work/ours.cmp" | head -20 >&2
            exit 1
        fi
        echo "upper: sentence $n, $apart trace lines apart of" \
             "$(wc -l < "$work/ibm.cmp")"
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
