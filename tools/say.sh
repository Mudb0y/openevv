#!/usr/bin/env bash
#
# Speak some text with the engine as lang/enus/enus.dict currently has it, so
# that a change to a word can be heard rather than read about. Lays the tables
# and the rules down from the file, builds the engine, speaks, and plays what
# came out if this machine has anything to play it with.
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
make -C "$here" >/dev/null || exit 1

rm -f "$out"
"$here/build/evv" -f "$work/text" -o "$out" || exit 1
[ -s "$out" ] || { echo "say: the engine produced nothing" >&2; exit 1; }

# A player is asked for as a client and nothing is reconfigured, so the
# machine's own speech is untouched whatever happens here.
for player in pw-play paplay aplay; do
    if command -v "$player" >/dev/null 2>&1; then
        echo "say: $out, through $player"
        "$player" "$out" >/dev/null 2>&1
        exit 0
    fi
done

echo "say: $out, and nothing here to play it with"
