#!/usr/bin/env bash
#
# Speak some text with the engine as lang/enus.dict currently has it, so that
# a change to a word can be heard rather than read about. Lays the tables and
# the rules down from the file, builds the engine for this machine, and writes
# a wave file.
#
# usage: say.sh <text>...
#        say.sh -f <file>

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out=${EVV_SAY_OUT:-$here/build/say.wav}

if [ $# -eq 0 ]; then
    echo "say: nothing to say" >&2
    exit 2
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [ "$1" = "-f" ]; then
    [ $# -eq 2 ] || { echo "say: -f takes one file" >&2; exit 2; }
    [ -r "$2" ] || { echo "say: cannot read $2" >&2; exit 2; }
    cp "$2" "$work/text"
else
    printf '%s\n' "$*" > "$work/text"
fi

echo "say: laying the dictionaries down"
python3 "$here/tools/delta-dict.py" build >/dev/null || exit 1

echo "say: building the engine"
make -C "$here/native" speak >/dev/null || exit 1

rm -f "$out"
"$here/build/native/speak" "@$work/text" "$out" >/dev/null 2>&1

[ -s "$out" ] || { echo "say: the engine produced nothing" >&2; exit 1; }
echo "say: $out"
