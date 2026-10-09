#!/usr/bin/env bash
#
# Hold rules written as C against the same rules left as bytecode.
#
# The engine says which rule it is entering and with what, and every call it
# makes with its arguments, and a rule written as C goes through the same two
# functions and says the same. So the same sentence spoken twice, once with the
# rules compiled and once without, either says exactly the same thing or the
# translation is wrong somewhere. The audio is the coarser check behind this
# one: a rule can go wrong in a way that changes what runs and not what is
# heard.
#
# Four things about the comparison are deliberate, all of them the harness
# rather than the translation, and all of them found by this failing:
#
# One sentence at a time, in its own run of the engine. Tracing costs twenty
# times what the synthesis does, and while any one of the seven cases traces
# through to the end, seven in one run faults part way with less audio written
# -- so the traces stop in different places and nothing lines up. Each of the
# seven on its own is fine, and the fault is in feeding the synthesis far
# slower than it expects rather than in either form of the rules.
#
# Two kinds of line come out, both printed by the interpreter alone. It prints
# every store it makes, and a rule written as C makes its own and has nothing
# to print. And it says when the count a call carries for how deep the argument
# area should be disagrees with how deep it is, which is a remark about the
# compiled code rather than about either form of it.
#
# The rules are written out with EVV_FAITHFUL set, which leaves a wrapper rule
# as a call to that rule. Without it the decompiler writes out the primitive
# the wrapper stood for, so the wrapper rule is never entered and cannot appear
# in a trace at all. That is the inlining working, but it leaves nothing to
# compare. Every other pass is in either way, and the suites are the only check
# on the inlining itself.
#
# Addresses in the arena are masked. A rule written as C deliberately takes a
# smaller frame than the interpreter's, so the two land in different places,
# and where a frame landed is not what this is checking.
#
# usage: tools/rules/check-c.sh <rule>...
#        tools/rules/check-c.sh <count>          the smallest that many with a body

set -u
tools=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
here=$(dirname "$tools")
work=$(mktemp -d)
# How much may run at once, the builds' jobs and the sentences spoken side by
# side alike, six unless told, as for test/matrix.sh.
jobs=${EVV_JOBS:-6}
# The rules go too, or the faithful form written here would be left sitting
# where the next build expects the ordinary one and would be newer than
# everything it is made from, so nothing would rewrite it. What was there
# before is put back with the dates it had, which make then finds as it left
# them; removing them instead was safe, since absent is not stale, but sent
# the next build to decompile English again for two and a half minutes.
mkdir "$work/kept"
cp -p "$here"/lang/enus/delta_rules_c[0-9][0-9]_enus.c "$work/kept/" 2>/dev/null
trap 'rm -f "$here"/lang/enus/delta_rules_c[0-9][0-9]_enus.c
      cp -p "$work"/kept/* "$here/lang/enus/" 2>/dev/null
      rm -rf "$work"' EXIT

[ $# -gt 0 ] || { echo "check: name some rules" >&2; exit 2; }

# The side's own name, then the form of the rules to build it with. The two
# differ for the C side: it is built as `both', which is the C rules with the
# interpreter still in, because only a handful of rules were written as C here
# and a plain `c' build has nothing left to run the rest.
build() {
    rm -f "$here/build/probe"
    make -C "$here" -j"$jobs" RULES="$2" probe >/dev/null || exit 1
    cp "$here/build/probe" "$work/probe.$1"
}

# One sentence through one of the two, with what only the interpreter can say
# taken out and the references masked.
# What is left after the marked references are masked: values a rule passes
# that no declaration describes, which are mostly distances into the region.
# A rule written as C deliberately takes a smaller frame than the
# interpreter's, so the two sides land in different places by design and those
# distances differ for that reason alone.
#
# Masked by size rather than numbered, for the reasons written out at length
# in check-upper.sh: numbering each distinct value by when it first appeared
# cannot work, because the slots are reused and no relabelling reconciles
# them, and it swallows every immediate as well. Below the threshold a value
# is an immediate and is compared exactly; at or above it, it is a distance
# and is not.
mask() {
    python3 -c '
import re, sys
pat = re.compile(r"(?<![0-9a-fA-F])[0-9a-f]{8}(?![0-9a-fA-F])")


def one(m):
    v = int(m.group(0), 16)
    if v >= 0x80000000:
        v -= 0x100000000
    return "VAL" if abs(v) >= 0x10000 else m.group(0)


for line in sys.stdin:
    sys.stdout.write(pat.sub(one, line))
'
}

speak() {
    DELTA_RULE_TRACE=200000 EVV_PROBE_WAIT=900 timeout 900 "$work/probe.$1" \
        "$2" "$3/$1.wav" 2>"$3/$1.raw" >/dev/null
    grep -v '^rules run:\|^# store \|in the area' "$3/$1.raw" \
        | sed -E 's/@[0-9a-f]{8}/ARENA/g' | mask > "$3/$1.trace"
}

# One sentence through both sides, in a directory of its own, with what the
# reading below needs written down before the traces are let go: every
# sentence is spoken at once.
judge() {
    local d=$1 sounds=1 same=1
    speak bytecode "$2" "$d" &
    speak c "$2" "$d" &
    wait
    cmp -s "$d/bytecode.wav" "$d/c.wav" || sounds=0
    if ! cmp -s "$d/bytecode.trace" "$d/c.trace"; then
        same=0
        diff "$d/bytecode.trace" "$d/c.trace" | head -20 > "$d/head"
    fi
    echo "$sounds $same $(wc -l < "$d/bytecode.trace")" > "$d/result"
    rm -f "$d"/*.raw "$d"/*.trace "$d"/*.wav
}

echo "check: building both"
build bytecode bytecode
EVV_FAITHFUL=1 python3 "$tools/rules/decompile.py" "$@" || exit 1
build c both

n=0
running=0
while IFS= read -r sentence; do
    [ -n "$sentence" ] || continue
    n=$((n + 1))
    mkdir "$work/s$n"
    judge "$work/s$n" "$sentence" < /dev/null &
    running=$((running + 1))
    if [ "$running" -ge "$jobs" ]; then
        wait -n
        running=$((running - 1))
    fi
done < "$here/test/cases/plain.txt"
wait

lines=0
for k in $(seq 1 "$n"); do
    read -r sounds same traced < "$work/s$k/result" || {
        echo "check: sentence $k was not spoken" >&2
        exit 1
    }
    if [ "$sounds" = 0 ]; then
        echo "check: sentence $k does not even sound the same" >&2
        exit 1
    fi
    if [ "$same" = 0 ]; then
        echo "check: sentence $k parts company" >&2
        cat "$work/s$k/head" >&2
        exit 1
    fi
    lines=$((lines + traced))
    echo "check: sentence $k, the same"
done

echo "check: the same, call for call, over $lines lines of $n sentences"
