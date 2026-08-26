#!/usr/bin/env bash
#
# The machine's primitives, ours against IBM's, one call at a time.
#
# test/suite.sh cannot see these. It proves the engine by speaking sentences,
# and a primitive that no rule in the nine languages IBM shipped ever calls is
# reached by no sentence -- which is why the arithmetic beyond addition, the
# string tests and the whole generate family were missing from this engine in
# the first place: the link never asked for them. So a primitive written for a
# rule of ours is proved here instead, by calling it directly on both sides.
#
# test/prims.c is the table of cases and is compiled twice: by the top-level
# `make prims' against our engine, and by `make -C reference prims' against
# IBM's own objects, which define these under plain C names. Both print the
# bytes each call leaves behind and this diffs them.
#
# It wants Wine and IBM's objects, like the suite and unlike test/hash.sh.
#
# usage: prims.sh

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/.." && pwd)

ours=$root/build/prims
theirs=$root/build/reference/prims.exe

make -C "$root" prims >/dev/null || exit 1
make -C "$root/reference" prims >/dev/null || exit 1

[ -x "$ours" ]   || { echo "prims: no binary at $ours" >&2; exit 2; }
[ -f "$theirs" ] || { echo "prims: no binary at $theirs" >&2; exit 2; }

case $(uname -s 2>/dev/null) in
MINGW*|MSYS*|CYGWIN*) PE= ;;
*)                    PE=wine ;;
esac

a=$(mktemp) || exit 1
b=$(mktemp) || exit 1
trap 'rm -f "$a" "$b"' EXIT

"$ours" > "$a" || { echo "prims: ours did not finish" >&2; exit 1; }

# IBM's is a Windows binary and writes its lines with a carriage return in
# front of every newline, which is the console's doing and not the engine's.
$PE "$theirs" 2>/dev/null | tr -d '\r' > "$b"
[ -s "$b" ] || { echo "prims: IBM's produced nothing" >&2; exit 1; }

n=$(wc -l < "$a")
if diff -q "$a" "$b" >/dev/null; then
    echo "prims: $n calls, identical"
    exit 0
fi

echo "prims: they differ" >&2
diff "$a" "$b" | head -40 >&2
echo "prims: $(diff "$a" "$b" | grep -c '^<') of $n lines differ" >&2
exit 1
