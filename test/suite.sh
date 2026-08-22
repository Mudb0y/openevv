#!/usr/bin/env bash
#
# Every case spoken twice, once by the engine built for this machine and once
# by IBM's own under Wine, and the samples compared.
# Answers non-zero if anything differed or hung.
#
# usage: suite.sh [name ...]     with no names, runs all but long

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cases=$here/cases

# Which language to speak in. It picks the cases, and compare.sh picks the
# two binaries to match: `EVV_LANG=dede test/suite.sh' wants build/probe-dede
# and build/reference-dede, which is what `make LANG=lang/dede probe' and
# `make -C reference TAG=dede BUILD=../build/reference-dede' build.
lang=${EVV_LANG:-enus}
case $lang in
enus) suf= ;;
*)    suf=-$lang ;;
esac
export EVV_LANG=$lang

# Wine starts its debugger when something faults, which on a desktop is a dialog
# box and a process left sitting in front of whoever is at the machine. This
# runs Wine hundreds of times and the reference does fault now and again, so the
# debugger is turned off in this prefix first. Wine still says what faulted.
# On Windows there is no Wine and nothing to turn off.
case $(uname -s 2>/dev/null) in
MINGW*|MSYS*|CYGWIN*) ;;
*)
    wine reg add \
        "HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AeDebug" \
        /v Debugger /t REG_SZ /d "" /f >/dev/null 2>&1
    ;;
esac

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
    plain)     run plain     "$cases/plain$suf.txt" ""   ""      || bad=1 ;;
    utf8)      run utf8      "$cases/utf8$suf.txt"  ""   ""      || bad=1 ;;
    anno)      run anno      "$cases/anno$suf.txt"  ""   ""      || bad=1 ;;
    anno3)     run anno3     "$cases/anno$suf.txt"  anno "$TEXT" || bad=1 ;;
    realworld) run realworld "$cases/anno$suf.txt"  ar   "$TEXT" || bad=1 ;;
    long)      run long      "$cases/long$suf.txt"  ""   ""      || bad=1 ;;
    dict)      run dict      "$cases/plain$suf.txt" ard  "$DICT" || bad=1 ;;
    *) echo "suite: no such comparison: $one" >&2; bad=1 ;;
    esac
done

exit $bad
