#!/usr/bin/env bash
#
# The SSML reader, ours against IBM's.
#
# test/harness/ssml.c is the list of documents and is compiled twice: by the
# top-level `make ssml' against our engine, and by `make -C reference ssml'
# against IBM's objects. Both register the SSML filter through the published
# interface, turn it on, and print the annotations every document turns
# into, and this diffs them.
#
# It wants Wine and IBM's objects, like test/harness/ipa.sh and unlike
# test/matrix.sh.
#
# usage: ssml.sh [cases.txt]

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)

ours=$root/build/ssml
theirs=$root/build/reference/ssml.exe

make -C "$root" ssml >/dev/null || exit 1
make -C "$root/reference" ssml >/dev/null || exit 1

[ -x "$ours" ]   || { echo "ssml: no binary at $ours" >&2; exit 2; }
[ -f "$theirs" ] || { echo "ssml: no binary at $theirs" >&2; exit 2; }

case $(uname -s 2>/dev/null) in
MINGW*|MSYS*|CYGWIN*) PE= ;;
*)                    PE=wine ;;
esac

a=$(mktemp) || exit 1
b=$(mktemp) || exit 1
trap 'rm -f "$a" "$b"' EXIT

# IBM's engine complains to standard output about the semaphores the porting
# shim under Wine does not really provide, and those lines are the shim's
# rather than the reader's. They are dropped from both sides, along with the
# blank line each of them carries.
strip() { tr -d '\r' | grep -av '^log\[' | grep -av '^$'; }

"$ours" "$@" | strip > "$a" || { echo "ssml: ours did not finish" >&2; exit 1; }

# IBM's is a Windows binary and writes a carriage return in front of every
# newline, which is the console's doing and not the reader's.
$PE "$theirs" "$@" 2>/dev/null | strip > "$b"
[ -s "$b" ] || { echo "ssml: IBM's produced nothing" >&2; exit 1; }

n=$(grep -ac '^in ' < "$a")
if diff -q -a "$a" "$b" >/dev/null; then
    echo "ssml: $n documents, identical"
    exit 0
fi

echo "ssml: they differ" >&2
diff -a "$a" "$b" | head -60 >&2
echo "ssml: $(diff -a "$a" "$b" | grep -c '^<') of $(wc -l < "$a") lines differ" >&2
exit 1
