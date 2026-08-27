#!/usr/bin/env bash
#
# Run one set of sentences through the engine built for this machine and
# through IBM's own under Wine, and say whether the audio agrees.
#
# This is what proves the port: the same sentences, through code built for a
# different operating system and a different C library and threaded with
# pthreads, coming out as the same samples.
#
# It is not a promise to sound like Eloquence forever -- the language data is
# meant to be worked on -- but until a change is made deliberately, a sample
# that moved is a bug, and this is what says so.
#
# A case that times out is retried once on its own and then reported as hung:
# the reference hangs now and again on an index mark, and calling that a
# difference has cost false alarms.
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

# Both engines here may be Windows binaries. On Windows they run themselves;
# anywhere else they want Wine in front of them. The reference always is one,
# since it is IBM's own code built as IBM built it.
case $(uname -s 2>/dev/null) in
MINGW*|MSYS*|CYGWIN*) PE= ;;
*)                    PE=wine ;;
esac

# Which language is being compared. A build takes one language, and so does
# the reference built from that language's objects, so the two have to be
# named together or an English engine gets held against a German oracle and
# every case differs for no reason worth reading. English keeps the plain
# names because that is what everything already asks for.
LANG_TAG=${EVV_LANG:-enus}
case $LANG_TAG in
enus) SUF= ;;
*)    SUF=-$LANG_TAG ;;
esac

# And which language to ask the engine for, since a build may have more than
# one in it. These are IBM's own numbers, the ones its ini names each
# language section for; a language added to the tree adds a line here.
case $LANG_TAG in
enus) : ${EVV_LANGUAGE:=0x10000} ;;
dede) : ${EVV_LANGUAGE:=0x40000} ;;
engb) : ${EVV_LANGUAGE:=0x10001} ;;
eses) : ${EVV_LANGUAGE:=0x20000} ;;
esus) : ${EVV_LANGUAGE:=0x20001} ;;
itit) : ${EVV_LANGUAGE:=0x50000} ;;
frfr) : ${EVV_LANGUAGE:=0x30000} ;;
frca) : ${EVV_LANGUAGE:=0x30001} ;;
jajp) : ${EVV_LANGUAGE:=0x80000} ;;
esac
export EVV_LANGUAGE

REFDIR=${EVV_REFERENCE_DIR:-$BUILD/reference$SUF}
[ -x "$REFDIR/speak.exe" ] || { echo "compare: no reference binary" >&2; exit 2; }
# Which of ours to run. Both builds have to say the same thing, so either
# can be set against the reference; EVV_NATIVE names the other one.
OURS=${EVV_NATIVE:-$BUILD/probe$SUF}
[ -x "$OURS" ] || { echo "compare: no native binary" >&2; exit 2; }
# Ours may be the Windows build now, which runs the way the reference does.
case $OURS in
*.exe) OURS_RUN="$PE $OURS" ;;
*)     OURS_RUN="$OURS" ;;
esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$REFDIR"/*.exe "$work/" 2>/dev/null
cd "$work"

# One case through one binary. Answers 0 when it produced a file, 1 when it
# timed out.
one_run() {
    local who=$1 out=$2
    rm -f "$out"
    if [ "$who" = ref ]; then
        timeout "$LIMIT" $PE ./speak.exe @case.txt "$out" $mode > "$out.txt" 2>/dev/null
    else
        timeout "$LIMIT" $OURS_RUN @case.txt "$out" $mode > "$out.txt" 2>/dev/null
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
