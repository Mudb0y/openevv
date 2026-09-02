#!/usr/bin/env bash
#
# Speak one fixed sentence and check the samples against what they have always
# been.
#
# One sentence, so this is the smoke test rather than the gate: it answers in
# two seconds and it is what to run between edits. test/matrix.sh is the gate
# and asks the same question of 791 cases over nine languages, with the
# interface's own answers held as well as the audio.
#
# The samples do not depend on the compiler: gcc 15 and clang 21 agree byte for
# byte, which is what you would hope from an engine whose arithmetic is all
# integer. So a hash in the tree is a fair thing to hold a build to.
#
# usage: hash.sh [binary]        default build/evv

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
bin=${1:-$here/../build/evv}
text="The quick brown fox jumps over the lazy dog."

[ -x "$bin" ] || { echo "hash: no binary at $bin" >&2; exit 2; }

out=$(mktemp) || exit 1
trap 'rm -f "$out"' EXIT

# A Windows binary runs itself on Windows and wants Wine in front of it
# anywhere else.
case $(uname -s 2>/dev/null) in
MINGW*|MSYS*|CYGWIN*) PE= ;;
*)                    PE=wine ;;
esac

case $bin in
*.exe) $PE "$bin" -o "$out" "$text" >/dev/null 2>&1 ;;
*)     "$bin" -o "$out" "$text" >/dev/null 2>&1 ;;
esac

[ -s "$out" ] || { echo "hash: it produced nothing" >&2; exit 1; }

have=$(sha256sum < "$out" | cut -d' ' -f1)
want=$(cut -d' ' -f1 < "$here/samples.sha256")

if [ "$have" = "$want" ]; then
    echo "hash: the samples are what they have always been"
    exit 0
fi

echo "hash: the samples have moved" >&2
echo "  wanted $want" >&2
echo "  got    $have" >&2
echo "If that was deliberate, run test/matrix.sh to see everything else that" >&2
echo "moved with it, then test/matrix.sh record and put the new hash in" >&2
echo "test/samples.sha256." >&2
exit 1
