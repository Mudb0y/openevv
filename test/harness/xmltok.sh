#!/usr/bin/env bash
#
# The XML scanner, ours against IBM's.
#
# test/harness/xmltok.c is the list of documents and is compiled twice: by the
# top-level `make xmltok' against our engine, and by `make -C reference
# xmltok' against IBM's objects. Both drive the parser directly and print
# every call its three handlers get, and this diffs them.
#
# It wants Wine and IBM's objects, like test/harness/prims.sh and unlike
# test/matrix.sh.
#
# usage: test/harness/xmltok.sh

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)

ours=$root/build/xmltok
theirs=$root/build/reference/xmltok.exe

make -C "$root" xmltok >/dev/null || exit 1
make -C "$root/reference" xmltok >/dev/null || exit 1

[ -x "$ours" ]   || { echo "xmltok: no binary at $ours" >&2; exit 2; }
[ -f "$theirs" ] || { echo "xmltok: no binary at $theirs" >&2; exit 2; }

case $(uname -s 2>/dev/null) in
MINGW*|MSYS*|CYGWIN*) PE= ;;
*)                    PE=wine ;;
esac

a=$(mktemp) || exit 1
b=$(mktemp) || exit 1
trap 'rm -f "$a" "$b"' EXIT

"$ours" > "$a" || { echo "xmltok: ours did not finish" >&2; exit 1; }

# IBM's is a Windows binary and writes a carriage return in front of every
# newline, which is the console's doing and not the converter's.
$PE "$theirs" 2>/dev/null | tr -d '\r' > "$b"
[ -s "$b" ] || { echo "xmltok: IBM's produced nothing" >&2; exit 1; }

n=$(wc -l < "$a")
if diff -q "$a" "$b" >/dev/null; then
    echo "xmltok: $n lines, identical"
    exit 0
fi

echo "xmltok: they differ" >&2
diff "$a" "$b" | head -40 >&2
echo "xmltok: $(diff "$a" "$b" | grep -c '^<') of $n lines differ" >&2
exit 1
