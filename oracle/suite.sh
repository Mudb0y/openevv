#!/usr/bin/env bash
#
# The comparisons, one after another. long is not in the default set:
# it is minutes rather than seconds, and it is there for the queue between
# the text side and the synthesis thread, which one sentence never fills. Answers non-zero if anything
# differed or hung, so it can be used as a check rather than read.
#
# usage: suite.sh [name ...]     with no names, runs all but long

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

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
    plain)     run plain     "$here/cases/plain.txt" ""    ""      || bad=1 ;;
    utf8)      run utf8      "$here/cases/utf8.txt"  ""    ""      || bad=1 ;;
    anno)      run anno      "$here/cases/anno.txt"  ""    ""      || bad=1 ;;
    anno3)     run anno3     "$here/cases/anno.txt"  anno  "$TEXT" || bad=1 ;;
    realworld) run realworld "$here/cases/anno.txt"  ar    "$TEXT" || bad=1 ;;
    long)      run long      "$here/cases/long.txt" ""    ""      || bad=1 ;;
    dict)      run dict      "$here/cases/plain.txt" ard   "$DICT" || bad=1 ;;
    *) echo "suite: no such comparison: $one" >&2; bad=1 ;;
    esac
done

exit $bad
