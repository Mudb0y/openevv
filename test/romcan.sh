#!/usr/bin/env bash
#
# Everything below the romanizer, held against IBM's own engine.
#
# A language written in another script goes through a romanizer, and until that
# romanizer is transcribed there is no way to get a word of such a language as
# far as the engine. This gets round that. For each case it runs IBM's engine
# with reference/romtap.c recording every call its romanizer manager makes and
# every answer it gets, and then runs ours with test/romcan.c standing where the
# romanizer would stand and giving those recorded answers back.
#
# Two things then have to hold. The samples have to be identical, which says
# the whole engine below the seam is right. And the conversation has to be
# identical -- romcan compares every call it receives against the recording and
# fails on a difference -- which says our manager asks the same questions IBM's
# did, without which the answers would prove nothing.
#
# What it does not test is the romanizer, which is the point: it is what says
# that the romanizer is all that is left.
#
# usage: EVV_LANG=jajp test/romcan.sh [cases-file]
#
# Wants `make -C reference TAG=jajp BUILD=../build/reference-jajp romtap' and
# `make romcan LANGS=lang/jajp'.

set -u

LIMIT=${EVV_CASE_TIMEOUT:-120}
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD=$ROOT/build

LANG_TAG=${EVV_LANG:-jajp}
case $LANG_TAG in
enus) SUF= ;;
*)    SUF=-$LANG_TAG ;;
esac

case $LANG_TAG in
jajp) : "${EVV_LANGUAGE:=0x80000}" ;;
esac
export EVV_LANGUAGE

cases=${1:-$ROOT/test/cases/plain$SUF.txt}
case $cases in /*) ;; *) cases=$PWD/$cases ;; esac
[ -r "$cases" ] || { echo "romcan: cannot read $cases" >&2; exit 2; }

case $(uname -s 2>/dev/null) in
MINGW*|MSYS*|CYGWIN*) PE= ;;
*)                    PE=wine ;;
esac

REFDIR=${EVV_REFERENCE_DIR:-$BUILD/reference$SUF}
REF=$REFDIR/speak-romtap.exe
OURS=$BUILD/romcan$SUF
[ -x "$REF" ] || { echo "romcan: no tapped reference at $REF" >&2; exit 2; }
[ -x "$OURS" ] || { echo "romcan: no romcan binary at $OURS" >&2; exit 2; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$REFDIR"/*.exe "$work/" 2>/dev/null

n=0; same=0; diff=0; hung=0
while IFS= read -r text; do
    [ -n "$text" ] || continue
    n=$((n + 1))
    printf '%s' "$text" > "$work/case.txt"

    rm -f "$work/ref.wav" "$work/nat.wav" "$work/case.tap"
    (cd "$work" && EVV_ROMTAP=case.tap timeout "$LIMIT" $PE ./speak-romtap.exe \
        @case.txt ref.wav > ref.txt 2>/dev/null)
    if [ ! -s "$work/ref.wav" ]; then
        hung=$((hung + 1))
        echo "case $n: the reference said nothing"
        continue
    fi

    if ! EVV_ROMCAN=$work/case.tap EVV_ROMCAN_TRACE=$work/nat.trace \
         timeout "$LIMIT" "$OURS" "@$work/case.txt" "$work/nat.wav" \
         > "$work/nat.txt" 2>"$work/nat.err"; then
        diff=$((diff + 1))
        echo "case $n: the conversation differs: $text"
        sed 's/^/    /' "$work/nat.err" | head -5
        continue
    fi

    if cmp -s "$work/ref.wav" "$work/nat.wav"; then
        same=$((same + 1))
    else
        diff=$((diff + 1))
        echo "case $n: the samples differ: $text"
    fi
done < "$cases"

echo "TOTAL: $n cases, $same matched, $diff differed, $hung silent"
[ "$diff" = 0 ] && [ "$hung" = 0 ]
