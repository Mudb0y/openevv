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
# The rules go too, or the faithful form written here would be left sitting
# where the next build expects the ordinary one and would be newer than
# everything it is made from, so nothing would rewrite it. Removing them
# rather than rewriting them is enough: absent is not stale, and the next
# build that wants them writes them again.
trap 'rm -rf "$work"; rm -f "$here"/lang/enus/delta_rules_c[0-9][0-9]_enus.c' EXIT

[ $# -gt 0 ] || { echo "check: name some rules" >&2; exit 2; }

build() {
    rm -f "$here/build/probe"
    make -C "$here" RULES="$1" probe >/dev/null || exit 1
    cp "$here/build/probe" "$work/probe.$1"
}

# One sentence through one of the two, with what only the interpreter can say
# taken out and the references masked.
# What is left after the marked references are masked: a rule's own
# arguments. A rule takes a reference as the distance it is -- there is no
# crossing and so no mask to consult -- and two builds lay their regions out
# differently, so those distances differ legitimately. Masking every
# eight-digit value would swallow genuine integers with them, so each
# distinct one is replaced by the order it first appears in instead.
#
# Order of first appearance and not rank among the values: rank was tried and
# is worse, because the two runs meet slightly different sets of references
# and every rank after the first difference then shifts. Order of appearance
# survives that; what it does not survive is the two runs meeting the same
# pair in the opposite order, which is the whole of what still differs.
canon() {
    python3 -c '
import re, sys
seen = {}
pat = re.compile(r"(?<![0-9a-fA-F])[0-9a-f]{8}(?![0-9a-fA-F])")


def one(m):
    v = m.group(0)
    if v not in seen:
        seen[v] = len(seen) + 1
    return "REF%d" % seen[v]


for line in sys.stdin:
    sys.stdout.write(pat.sub(one, line))
'
}

speak() {
    DELTA_RULE_TRACE=200000 timeout 900 "$work/probe.$1" \
        "$2" "$work/$1.wav" 2>"$work/$1.raw" >/dev/null
    grep -v '^rules run:\|^# store \|in the area' "$work/$1.raw" \
        | sed -E 's/@[0-9a-f]{8}/ARENA/g' | canon > "$work/$1.trace"
}

echo "check: building both"
build bytecode
EVV_FAITHFUL=1 python3 "$tools/rules/decompile.py" "$@" || exit 1
build c

lines=0
n=0
while IFS= read -r sentence; do
    [ -n "$sentence" ] || continue
    n=$((n + 1))
    speak bytecode "$sentence"
    speak c "$sentence"

    if ! cmp -s "$work/bytecode.wav" "$work/c.wav"; then
        echo "check: sentence $n does not even sound the same" >&2
        exit 1
    fi
    if ! cmp -s "$work/bytecode.trace" "$work/c.trace"; then
        echo "check: sentence $n parts company" >&2
        diff "$work/bytecode.trace" "$work/c.trace" | head -20 >&2
        exit 1
    fi
    lines=$((lines + $(wc -l < "$work/bytecode.trace")))
    echo "check: sentence $n, the same"
done < "$here/test/cases/plain.txt"

echo "check: the same, call for call, over $lines lines of $n sentences"
