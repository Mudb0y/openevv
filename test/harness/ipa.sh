#!/usr/bin/env bash
#
# The IPA converters, ours against IBM's.
#
# test/harness/ipa.c is the table of questions and is compiled twice: by the
# top-level `make ipa' against our engine, and by `make -C reference ipa'
# against IBM's objects. Both print an answer for every code point in six
# languages, for a dozen whole strings in fifteen, and for the code-set
# conversions underneath, and this diffs them.
#
# It wants Wine and IBM's objects, like test/harness/prims.sh and unlike
# test/matrix.sh.
#
# usage: test/harness/ipa.sh

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)

ours=$root/build/ipa
theirs=$root/build/reference/ipa.exe

make -C "$root" ipa >/dev/null || exit 1
make -C "$root/reference" ipa >/dev/null || exit 1

[ -x "$ours" ]   || { echo "ipa: no binary at $ours" >&2; exit 2; }
[ -f "$theirs" ] || { echo "ipa: no binary at $theirs" >&2; exit 2; }

case $(uname -s 2>/dev/null) in
MINGW*|MSYS*|CYGWIN*) PE= ;;
*)                    PE=wine ;;
esac

a=$(mktemp) || exit 1
b=$(mktemp) || exit 1
trap 'rm -f "$a" "$b"' EXIT

"$ours" > "$a" || { echo "ipa: ours did not finish" >&2; exit 1; }

# IBM's is a Windows binary and writes a carriage return in front of every
# newline, which is the console's doing and not the converter's.
$PE "$theirs" 2>/dev/null | tr -d '\r' > "$b"
[ -s "$b" ] || { echo "ipa: IBM's produced nothing" >&2; exit 1; }

n=$(wc -l < "$a")
if diff -q "$a" "$b" >/dev/null; then
    echo "ipa: $n answers, identical"
    exit 0
fi

echo "ipa: they differ" >&2
diff "$a" "$b" | head -40 >&2
echo "ipa: $(diff "$a" "$b" | grep -c '^<') of $n lines differ" >&2
exit 1
