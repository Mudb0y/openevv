#!/usr/bin/env bash
#
# The romanizer's converters, ours against IBM's, one call at a time.
#
# test/romcan.sh cannot see these. It proves the engine below the romanizer by
# replaying what IBM's romanizer answered, so a class the romanizer reaches for
# itself is never called on that path -- the codeset conversion is exactly
# that. test/romprims.c is the sweep and is compiled twice, once against our
# romanizer and once against IBM's own objects, and this diffs what the two
# print.
#
# Every input there is: each single byte, each two-byte pair the converter
# accepts, and all sixty-five thousand code points in the other direction,
# twice. About a hundred and forty thousand lines a side.
#
# It wants Wine and IBM's objects, like the suite and unlike test/hash.sh.
#
# usage: romprims.sh

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/.." && pwd)

ours=$root/build/romprims-jajp
theirs=$root/build/reference-jajp/romprims.exe

make -C "$root" romprims LANGS=lang/jajp >/dev/null || exit 1
make -C "$root/reference" TAG=jajp BUILD=../build/reference-jajp romprims \
    >/dev/null || exit 1

[ -x "$ours" ]   || { echo "romprims: no binary at $ours" >&2; exit 2; }
[ -f "$theirs" ] || { echo "romprims: no binary at $theirs" >&2; exit 2; }

case $(uname -s 2>/dev/null) in
MINGW*|MSYS*|CYGWIN*) PE= ;;
*)                    PE=wine ;;
esac

a=$(mktemp) || exit 1
b=$(mktemp) || exit 1
trap 'rm -f "$a" "$b"' EXIT

"$ours" > "$a" 2>/dev/null
$PE "$theirs" 2>/dev/null | tr -d '\r' > "$b"

if diff -q "$b" "$a" >/dev/null; then
    echo "romprims: $(wc -l < "$a") calls, identical"
    exit 0
fi

echo "romprims: $(diff "$b" "$a" | grep -c '^<') lines differ"
diff "$b" "$a" | head -20
exit 1
