#!/usr/bin/env bash
#
# What our engine reads a sentence as, held against ETI Eloquence 6.1.
#
# 6.1 is what the rules lifted by tools/rules/lift64.py came out of, so it is
# the oracle for them the way IBM's binary under Wine is for the rest: both
# sides run test/harness/eti.c over the same case file and a difference names
# the case and shows both answers. Phonemes are compared and not samples,
# because the two synthesisers are separate programs.
#
# The 6.1 is Apple's, converted to run here by apple-eloquence-elf. It is not
# in the tree and not on a runner, so this runs where it is installed and
# nowhere else. EVV_ETI names the directory with eci.so and eci.ini in it, and
# EVV_ETI_LIBS where its libc++ is; on a machine whose Speech Dispatcher runs
# Eloquence through that package both are read from its module.
#
# usage: test/harness/eti.sh [cases.txt ...]

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)

if [ -z "${EVV_ETI:-}" ] || [ -z "${EVV_ETI_LIBS:-}" ]; then
    module=$(readlink -f /run/current-system/sw/lib/speech-dispatcher-modules/sd_eloquence 2>/dev/null)
    if [ -n "$module" ]; then
        : "${EVV_ETI:=$(dirname "$(dirname "$module")")/eloquence}"
        : "${EVV_ETI_LIBS:=$(grep -o "LD_LIBRARY_PATH='[^']*'" "$module" \
                             | sed "s/LD_LIBRARY_PATH='//; s/'$//" | paste -sd:)}"
    fi
fi
if [ ! -r "${EVV_ETI:-}/eci.so" ]; then
    echo "eti: no ETI Eloquence 6.1 here; EVV_ETI names its directory" >&2
    exit 2
fi

ours=${EVV_ECI_LIB:-$root/build/libeci.so}
if [ ! -r "$ours" ]; then
    echo "eti: $ours is not built; run 'make so'" >&2
    exit 2
fi
driver=$root/build/eti
if [ ! -x "$driver" ] || [ "$here/eti.c" -nt "$driver" ]; then
    ${CC:-cc} -O1 -o "$driver" "$here/eti.c" -ldl || exit 2
fi

cases=("$@")
[ ${#cases[@]} -eq 0 ] && cases=("$root/test/cases/eti-punct.txt")

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

n=0; same=0
for f in "${cases[@]}"; do
    case $f in /*) ;; *) f=$PWD/$f ;; esac
    # 6.1 reads its eci.ini from the directory it is run in, and the one it
    # ships names its languages relative to that.
    ( cd "$EVV_ETI" && LD_LIBRARY_PATH=$EVV_ETI_LIBS "$driver" "$EVV_ETI/eci.so" "$f" ) \
        > "$work/theirs" || { echo "eti: 6.1 did not finish $f" >&2; exit 1; }
    timeout 600 "$driver" "$ours" "$f" ours > "$work/ours" \
        || { echo "eti: ours did not finish $f" >&2; exit 1; }
    # The one difference that is not one: 6.1 puts a space between words in
    # what it reports and ours does not, so spaces are taken out of both.
    while IFS=$'\t' read -r a text <&3 && IFS=$'\t' read -r b _ <&4; do
        n=$((n + 1))
        if [ "${a// /}" = "${b// /}" ]; then
            same=$((same + 1))
        else
            echo "eti: differs: $text"
            echo "  6.1  ${a// /}"
            echo "  ours ${b// /}"
        fi
    done 3< "$work/theirs" 4< "$work/ours"
done

echo "eti: $n cases, $same read as 6.1 reads them, $((n - same)) differ"
[ "$same" = "$n" ]
