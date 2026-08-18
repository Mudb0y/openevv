#!/usr/bin/env bash
#
# Run one set of sentences through both binaries and say whether they agree.
#
# The reference binary hangs now and again on annotation input carrying a
# user index mark, and when it does it takes the rest of the run with it: the
# case that hung produces no answer, and every case after it can inherit a
# half-written file. So a case that times out is retried once on its own, and
# if it hangs again it is reported as hung rather than as a difference. A
# hung case is the original's problem, not ours, and calling it a difference
# has cost several false alarms.
#
# The audio-marker sentence in the annotation set is the one that does this,
# and it is kept deliberately rather than removed: it is the only coverage
# there is of that path, it now labels itself, and a part of the engine the
# original cannot reproduce is one worth keeping an eye on.
#
# Each run works in its own directory, so two of these can be going at once
# without treading on each other's output.
#
# usage: compare.sh <cases-file> [mode-letters] [text-pattern]
#
#   mode-letters   passed to speak as its third argument, if given
#   text-pattern   an extended regex; when given, the filtered standard
#                  output is compared as well as the audio

set -u

LIMIT=${EVV_CASE_TIMEOUT:-120}
BUILD=$(cd "$(dirname "${BASH_SOURCE[0]}")/../build" && pwd)

cases=${1:?usage: compare.sh <cases-file> [mode] [pattern]}
# Resolved now, because the run moves into a directory of its own below.
case $cases in /*) ;; *) cases=$PWD/$cases ;; esac
[ -r "$cases" ] || { echo "compare: cannot read $cases" >&2; exit 2; }
mode=${2:-}
pattern=${3:-}

[ -x "$BUILD/speak.exe" ] || { echo "compare: no reference binary" >&2; exit 2; }
[ -x "$BUILD/speak_prim.exe" ] || { echo "compare: no ported binary" >&2; exit 2; }

work=$(mktemp -d "$BUILD/run.XXXXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 2

# One binary, one sentence. Answers 0 if it ran, 1 if it timed out or left
# nothing behind.
speak_once() {
    local exe=$1 line=$2 wav=$3 txt=$4 rc

    rm -f "$wav" "$txt" "$txt.raw"
    if [ -n "$mode" ]; then
        timeout "$LIMIT" wine "../$exe" "$line" "$wav" "$mode" >"$txt.raw" 2>/dev/null
    else
        timeout "$LIMIT" wine "../$exe" "$line" "$wav" >"$txt.raw" 2>/dev/null
    fi
    rc=$?

    if [ -n "$pattern" ]; then
        grep -E "$pattern" "$txt.raw" >"$txt" 2>/dev/null || : >"$txt"
    fi

    # Only a timeout counts as a hang. A run that finished but produced no
    # audio is a difference, and saying so keeps a crash in our own build
    # from being reported as the original's fault.
    [ "$rc" = 124 ] && return 1
    return 0
}

# Both binaries on one sentence. Answers 0 both ran, 1 the reference hung,
# 2 ours hung.
attempt() {
    local line=$1
    speak_once speak.exe      "$line" x.wav x.txt || return 1
    speak_once speak_prim.exe "$line" y.wav y.txt || return 2
    return 0
}

n=0; matched=0; differed=0; hung=0; unstable=0

while IFS= read -r line; do
    n=$((n + 1))

    attempt "$line"
    rc=$?
    if [ "$rc" != 0 ]; then
        # Give it one clean go on its own before believing it.
        attempt "$line"
        rc=$?
    fi

    if [ "$rc" = 1 ]; then
        echo "$n HUNG (reference)"
        hung=$((hung + 1))
        continue
    fi
    if [ "$rc" = 2 ]; then
        echo "$n HUNG (ours)"
        hung=$((hung + 1))
        continue
    fi

    if [ ! -s x.wav ]; then
        echo "$n DIFFER (reference produced no audio)"
        differed=$((differed + 1))
    elif [ ! -s y.wav ]; then
        echo "$n DIFFER (ours produced no audio)"
        differed=$((differed + 1))
    elif cmp -s x.wav y.wav \
         && { [ -z "$pattern" ] || cmp -s x.txt y.txt; }; then
        echo "$n same"
        matched=$((matched + 1))
    else
        # Before believing a difference, ask whether the original agrees
        # with itself. It does not always: an audio marker goes through the
        # sound path, and under Wine that does not come out the same twice.
        # A case the original cannot reproduce cannot tell us anything.
        cp x.wav ref-first.wav
        [ -n "$pattern" ] && cp x.txt ref-first.txt
        if speak_once speak.exe "$line" x.wav x.txt \
           && cmp -s ref-first.wav x.wav \
           && { [ -z "$pattern" ] || cmp -s ref-first.txt x.txt; }; then
            echo "$n DIFFER"
            differed=$((differed + 1))
        else
            echo "$n UNSTABLE (the original differs from itself)"
            unstable=$((unstable + 1))
        fi
    fi
done < "$cases"

echo "TOTAL: $n cases, $matched matched, $differed differed," \
     "$hung hung, $unstable unstable"

# Unstable cases are the original's business, not ours, so they do not fail
# the run -- but they are counted and named so they cannot be mistaken for
# agreement.
[ "$differed" = 0 ] && [ "$hung" = 0 ]
