#!/usr/bin/env bash
#
# Run one set of sentences through the engine built for this machine and
# through IBM's own under Wine, and say whether the audio agrees.
#
# This is the differential test's other half. That one proves our code
# behaves like the original when linked beside it; this one proves the same
# code, built for a different operating system and a different C library
# and threaded with pthreads, still produces the same samples.
#
# A case that times out is retried once on its own and then reported as
# hung, for the same reason as next door: the reference hangs now and again
# on an index mark, and calling that a difference has cost false alarms.
#
# usage: compare.sh <cases-file> [mode-letters] [text-pattern]

set -u

LIMIT=${EVV_CASE_TIMEOUT:-120}
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD=$ROOT/build

cases=${1:?usage: compare.sh <cases-file> [mode] [pattern]}
case $cases in /*) ;; *) cases=$PWD/$cases ;; esac
[ -r "$cases" ] || { echo "compare: cannot read $cases" >&2; exit 2; }
mode=${2:-}
pattern=${3:-}

[ -x "$BUILD/speak.exe" ]      || { echo "compare: no reference binary" >&2; exit 2; }
[ -x "$BUILD/native/speak" ]   || { echo "compare: no native binary" >&2; exit 2; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$BUILD"/*.exe "$work/" 2>/dev/null
cd "$work"

# One case through one binary. Answers 0 when it produced a file, 1 when it
# timed out.
one_run() {
    local who=$1 out=$2
    rm -f "$out"
    if [ "$who" = ref ]; then
        timeout "$LIMIT" wine ./speak.exe @case.txt "$out" $mode > "$out.txt" 2>/dev/null
    else
        timeout "$LIMIT" "$BUILD/native/speak" @case.txt "$out" $mode > "$out.txt" 2>/dev/null
    fi
    [ -s "$out" ]
}

n=0; same=0; diff=0; hung=0
while IFS= read -r text; do
    [ -n "$text" ] || continue
    n=$((n + 1))
    printf '%s' "$text" > case.txt

    if ! one_run ref ref.wav || ! one_run nat nat.wav; then
        if ! one_run ref ref.wav || ! one_run nat nat.wav; then
            hung=$((hung + 1))
            echo "case $n: hung"
            continue
        fi
    fi

    ok=yes
    cmp -s ref.wav nat.wav || ok=no
    # The reference writes its lines the way Windows does, so the carriage
    # returns come off before the two are set against each other.
    if [ -n "$pattern" ]; then
        tr -d '\r' < ref.wav.txt | grep -E "$pattern" > ref.f 2>/dev/null || true
        tr -d '\r' < nat.wav.txt | grep -E "$pattern" > nat.f 2>/dev/null || true
        cmp -s ref.f nat.f || ok=no
    fi

    if [ "$ok" = yes ]; then
        same=$((same + 1))
    else
        diff=$((diff + 1))
        echo "case $n: differs: $text"
    fi
done < "$cases"

echo "TOTAL: $n cases, $same matched, $diff differed, $hung hung"
[ "$diff" = 0 ] && [ "$hung" = 0 ]
