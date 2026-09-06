#!/usr/bin/env bash
#
# What the heteronym filter writes, held against what it wrote before.
#
# The filter's whole job is to put an annotation where the engine's own test
# would miss one, so the text it produces is the thing to check: a wrong
# answer names itself here, where in a wave file it would be a hash that
# moved. It wants neither Wine nor IBM's objects.
#
# The cases include words the engine already turns correctly, because leaving
# those alone matters as much: an annotation written where the engine was
# right is as wrong as one missing where it was not.
#
# usage: test/harness/hetero.sh [check | record]

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
root=$(cd "$here/.." && pwd)
bin=${EVV_HETERO:-$root/build/hetero}
cases=$here/cases/hetero.txt
want=$here/samples/hetero.txt

[ -x "$bin" ] || { echo "hetero: no $bin; make hetero builds it" >&2; exit 2; }

got=$("$bin" "$cases") || exit 1
n=$(printf '%s\n' "$got" | wc -l)

if [ "${1:-check}" = record ]; then
    {
        echo "# What the heteronym filter writes for test/cases/hetero.txt."
        echo "# Written by test/harness/hetero.sh record."
        printf '%s\n' "$got"
    } > "$want"
    echo "hetero: $n cases written to ${want#$root/}"
    exit 0
fi

[ -r "$want" ] || { echo "hetero: no baseline; record one" >&2; exit 2; }

if diff -q <(grep -v '^#' "$want") <(printf '%s\n' "$got") >/dev/null; then
    echo "hetero: $n cases, every one as it was"
    exit 0
fi
echo "hetero: what the filter writes has moved" >&2
diff <(grep -v '^#' "$want") <(printf '%s\n' "$got") | head -20 >&2
exit 1
