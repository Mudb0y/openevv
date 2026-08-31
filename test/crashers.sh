#!/usr/bin/env bash
#
# Speak every string in cases/crashers.txt and answer non-zero if the engine
# died on one or would not finish.
#
# These are the words IBM's engine cannot say. Each of them takes an unhandled
# page fault in the original -- the same fault, on a node reference of nought
# -- so there is no reference audio to hold ours against and this is not part
# of the differential suite. What it checks is only that ours answers: either
# it speaks the word or it gives the utterance up, and in both cases the
# process is still there afterwards. That is the whole point of the guards in
# src/delta.c, and this is what says they are still in place.
#
# It wants neither Wine nor IBM's objects, so it runs anywhere the engine
# builds. Like test/hash.sh it is one of the two quick ones.
#
# usage: crashers.sh [binary]

set -u

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/.." && pwd)
cases=$here/cases/crashers.txt

bin=${1:-${EVV_NATIVE:-$root/build/evv}}
[ -x "$bin" ] || { echo "crashers: no engine at $bin" >&2; exit 2; }
[ -r "$cases" ] || { echo "crashers: cannot read $cases" >&2; exit 2; }

# The Windows build runs the way the reference does everywhere but Windows.
case $(uname -s 2>/dev/null) in
MINGW*|MSYS*|CYGWIN*) pe= ;;
*)                    pe=wine ;;
esac
case $bin in
*.exe) run="$pe $bin" ;;
*)     run="$bin" ;;
esac

# A word that takes longer than this is not slow, it is going round.
limit=${EVV_CRASH_TIMEOUT:-30}
jobs=${EVV_CRASH_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# One word, in a subshell of its own so that the shell's report of a fault
# goes to the same place as everything else rather than to whoever is
# watching. A word that fails is written out as hex: several of these end a
# screen reader that is asked to read them, and the person running this may
# well be listening through one.
say() {
    local w=$1 rc
    ( timeout "$limit" $run -o "$work/$$.wav" "$w" >/dev/null 2>&1 ) 2>/dev/null
    rc=$?
    case $rc in
    0|1) return 0 ;;
    124) printf 'did not finish: %s\n' "$(printf '%s' "$w" | od -An -tx1 | tr -d ' \n')" ;;
    *)   printf 'died (%d): %s\n' "$rc" "$(printf '%s' "$w" | od -An -tx1 | tr -d ' \n')" ;;
    esac
    return 1
}
export -f say
export run work limit

n=0
while IFS= read -r w; do
    case $w in ''|\#*) continue ;; esac
    n=$((n + 1))
    printf '%s\n' "$w"
done < "$cases" > "$work/list"

xargs -a "$work/list" -d '\n' -P "$jobs" -I{} bash -c 'say "$@"' _ {} \
    > "$work/bad" 2>/dev/null
bad=$(wc -l < "$work/bad")

cat "$work/bad"
printf 'crashers: %d strings, %d survived, %d did not\n' \
    "$n" "$((n - bad))" "$bad"
[ "$bad" = 0 ]
