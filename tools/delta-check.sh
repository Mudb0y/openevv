#!/usr/bin/env bash
#
# Hold rules written as C against the same rules left as bytecode.
#
# The interpreter says what every call is, with its arguments, when
# DELTA_RULE_TRACE is set high enough, and a rule written as C calls through
# the same helper and says the same. So the same text spoken twice, once with
# the rules compiled and once without, either says exactly the same thing or
# the translation is wrong somewhere.
#
# usage: delta-check.sh <rule>...
#        delta-check.sh <count>          the smallest that many with a body

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

[ $# -gt 0 ] || { echo "check: name some rules" >&2; exit 2; }

cat "$here/test/cases/plain.txt" "$here/test/cases/utf8.txt" \
    "$here/test/cases/anno.txt" > "$work/text"

speak() {
    rm -f "$here/build/probe"
    make -C "$here" RULES="$2" probe >/dev/null || exit 1
    DELTA_RULE_TRACE=200000 timeout 600 "$here/build/probe" \
        "@$work/text" "$work/out.wav" 2>"$1" >/dev/null
    grep -v '^rules run:' "$1" > "$1.clean"
}

echo "check: with the rules as bytecode"
speak "$work/before" bytecode

echo "check: with them as C"
python3 "$here/tools/delta-decompile.py" "$@" || exit 1
speak "$work/after" c

if cmp -s "$work/before.clean" "$work/after.clean"; then
    echo "check: the same, call for call, over $(wc -l < "$work/before.clean") lines"
    exit 0
fi

echo "check: they part company" >&2
diff "$work/before.clean" "$work/after.clean" | head -20 >&2
exit 1
