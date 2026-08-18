#!/usr/bin/env bash
#
# The same six comparisons the differential build runs, with the engine
# built for this machine standing where our Windows build stands next door.
# Answers non-zero if anything differed or hung.
#
# usage: suite.sh [name ...]     with no names, runs all of them

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cases=$here/../oracle/cases

TEXT='^speak: (voice )?param|^speak: index'
DICT='^speak: (voice )?param|^speak: index|^speak: (new|set|get|load|delete)Dict'

run() {
    local name=$1; shift
    printf '%-10s ' "$name"
    "$here/compare.sh" "$@" | tail -1
    return "${PIPESTATUS[0]}"
}

bad=0
want=${*:-plain utf8 anno anno3 realworld dict}

for one in $want; do
    case $one in
    plain)     run plain     "$cases/plain.txt" ""    ""      || bad=1 ;;
    utf8)      run utf8      "$cases/utf8.txt"  ""    ""      || bad=1 ;;
    anno)      run anno      "$cases/anno.txt"  ""    ""      || bad=1 ;;
    anno3)     run anno3     "$cases/anno.txt"  anno  "$TEXT" || bad=1 ;;
    realworld) run realworld "$cases/anno.txt"  ar    "$TEXT" || bad=1 ;;
    dict)      run dict      "$cases/plain.txt" ard   "$DICT" || bad=1 ;;
    *) echo "suite: no such comparison: $one" >&2; bad=1 ;;
    esac
done

exit $bad
