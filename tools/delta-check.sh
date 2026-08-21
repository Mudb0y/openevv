#!/usr/bin/env bash
#
# Hold rules written as C against the same rules left as bytecode.
#
# The interpreter says which rule it is entering and with what, and a rule
# written as C is entered through the same function and says the same. So the
# same text spoken twice, once with the rules compiled and once without, either
# names the same rules in the same order with the same arguments or the
# translation is wrong somewhere. The audio is the coarser check behind this
# one: a rule can go wrong in a way that changes what runs and not what is
# heard.
#
# Four things about that comparison had to be settled, all of them the harness
# rather than the translation, and all of them found by this failing over the
# whole corpus:
#
# The text is short on purpose. Tracing costs more than the synthesis does, and
# over the whole case set the run is cut short -- with less audio written and
# the trace stopping in a different place each time, so nothing lines up. Seven
# sentences both sides finish, and then the traces are identical and so is the
# wave file.
#
# DELTA_RULE_TRACE is 1 and not more. Above a hundred thousand the interpreter
# also prints every call with its arguments, which would be the better check,
# and cannot be compared for two reasons: it prints the stores it makes, which
# a rule written as C makes for itself and cannot print, and the two disagree
# about how many arguments a call takes. A call is written with two counts, how
# many the entry takes and how deep the stack should be; the interpreter passes
# the first and the decompiler emits the second. Nothing reads past what it
# wants, so it changes nothing that is heard, but it means the printed argument
# lists differ. Emitting the first is a change to make on its own.
#
# The rules are written out with EVV_FAITHFUL set, which leaves a wrapper rule
# as a call to that rule. Without it the decompiler writes out the primitive
# the wrapper stood for, so the wrapper rule is never entered and cannot appear
# in a trace at all. That is the inlining working, but it leaves nothing to
# compare. Every other pass is in either way.
#
# Addresses in the arena are masked. A rule written as C deliberately takes a
# smaller frame than the interpreter's, so the two land in different places,
# and where a frame landed is not what this is checking.
#
# usage: delta-check.sh <rule>...
#        delta-check.sh <count>          the smallest that many with a body

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d)
# The rules go too, or the faithful form written here would be left sitting
# where the next build expects the ordinary one and would be newer than
# everything it is made from, so nothing would rewrite it.
trap 'rm -rf "$work" "$here/lang/enus/delta_rules_c.c"' EXIT

[ $# -gt 0 ] || { echo "check: name some rules" >&2; exit 2; }

speak() {
    rm -f "$here/build/probe"
    make -C "$here" RULES="$2" probe >/dev/null || exit 1
    DELTA_RULE_TRACE=1 timeout 900 "$here/build/probe" \
        "@$here/test/cases/plain.txt" "$work/$2.wav" 2>"$1" >/dev/null
    grep -v '^rules run:\|in the area' "$1" \
        | sed -E 's/\b1[0-9a-f]{7}\b/ARENA/g' > "$1.clean"
}

echo "check: with the rules as bytecode"
speak "$work/before" bytecode

echo "check: with them as C"
EVV_FAITHFUL=1 python3 "$here/tools/delta-decompile.py" "$@" || exit 1
speak "$work/after" c

if ! cmp -s "$work/bytecode.wav" "$work/c.wav"; then
    echo "check: the two do not even sound the same" >&2
    exit 1
fi

if cmp -s "$work/before.clean" "$work/after.clean"; then
    echo "check: the same, rule for rule, over" \
         "$(wc -l < "$work/before.clean") lines"
    exit 0
fi

echo "check: they part company" >&2
diff "$work/before.clean" "$work/after.clean" | head -20 >&2
exit 1
