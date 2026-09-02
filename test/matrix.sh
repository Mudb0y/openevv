#!/usr/bin/env bash
#
# Every case of every language, held against what this engine has said before.
#
# This is the gate. test/suite.sh proved the engine against IBM's own binary
# and that proof is done: eight of the nine languages came out byte for byte
# identical over every case there is for them, in every build the tree makes.
# What is wanted from here on is different -- the engine is being changed on
# purpose now, and the question is no longer whether it is IBM's but whether
# anything moved that nobody meant to move. So the answers are written down
# and held to, and a case that moves has to be explained rather than merely
# noticed.
#
# What is recorded, per case, is two hashes rather than one, because they fail
# in different ways. The samples say what came out of the synthesiser. The
# probe's own reported answers -- the parameters read back, the index marks,
# the phonemes, what the dictionary calls returned -- say what the interface
# did, and the audio cannot see that. A permanently silent instance once
# passed all 81 cases on both sides of the differential suite because nothing
# there changed a rate; an answer that stops being reported shows up here.
#
# Every number in test/samples was blessed by IBM's binary at the moment it
# was written: the whole differential suite was run for all eight lifted
# languages immediately before recording, so each is a value the original had
# just agreed with. Polish is the exception and its file says so -- IBM never
# shipped Polish, there is nothing to hold it to, and what is recorded is what
# this engine does, which is still worth having as the only way a change to
# Polish can be told from an accident.
#
# It wants neither Wine nor IBM's objects. That is the point of it.
#
# usage: matrix.sh check  [lang ...]     the default
#        matrix.sh record [lang ...]     write the baselines instead
#
# EVV_MATRIX_NATIVE names a binary to drive rather than building one, which is
# how the Windows build and the thirty-two bit build are checked against the
# same numbers.

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/.." && pwd)
cases=$here/cases
store=$here/samples

# Every language in the tree, and the number the interface knows each by.
# These are IBM's own, the ones its ini names each section for; a language
# added to the tree adds a line here and to test/compare.sh, which has the
# same list for the same reason.
ALL="enus engb dede eses esus frfr frca itit plpl"
language_of() {
    case $1 in
    enus) echo 0x10000 ;;
    engb) echo 0x10001 ;;
    eses) echo 0x20000 ;;
    esus) echo 0x20001 ;;
    frfr) echo 0x30000 ;;
    frca) echo 0x30001 ;;
    dede) echo 0x40000 ;;
    itit) echo 0x50000 ;;
    plpl) echo 0x110000 ;;
    *) echo "matrix: no such language: $1" >&2; exit 2 ;;
    esac
}

# The seven comparisons test/suite.sh makes, said here in the same order and
# with the same mode letters so that the two harnesses ask the same questions
# of the engine. That is deliberate and it is what makes the baselines worth
# anything: every one of them is a number the suite had just agreed with, and
# a category the suite could not run would be a number nothing had blessed.
#
# A category is a case file and the letters the probe is given: `anno' turns
# the annotation input type on, `a' and `r' ask for the settings to be read
# back in a person's units, `d' exercises the dictionary calls, and `t' says
# the text twice on one instance.
#
# That last is the one to explain. The engine's second utterance is not its
# first and that is faithful rather than random: the same sentence twice gives
# the same number of samples both times and most of them differ, because the
# machine's state has moved on. It is entirely deterministic -- three
# processes give the same first utterance and the same second one, and IBM's
# engine does it too, to the same samples -- so it is a thing that can be
# recorded, and nothing else in the tree records it. A change that quietly
# reset the state between utterances would move nothing else here.
CATEGORIES="plain utf8 anno anno3 realworld dict second"
file_of() {
    case $1 in
    plain|dict|second) echo "plain" ;;
    utf8)              echo "utf8" ;;
    *)                 echo "anno" ;;
    esac
}
mode_of() {
    case $1 in
    plain|utf8|anno) echo "" ;;
    anno3)           echo "anno" ;;
    realworld)       echo "ar" ;;
    dict)            echo "ard" ;;
    second)          echo "t" ;;
    esac
}

# A Windows binary runs itself on Windows and wants Wine in front of it
# anywhere else.
case $(uname -s 2>/dev/null) in
MINGW*|MSYS*|CYGWIN*) PE= ;;
*)                    PE=wine ;;
esac

what=${1:-check}
case $what in
check|record) shift || true ;;
*) what=check ;;
esac
want=${*:-$ALL}

for t in $want; do
    case " $ALL " in *" $t "*) ;;
    *) echo "matrix: no such language: $t" >&2; exit 2 ;; esac
done

# One binary with every language asked for in it, rather than one build a
# language. It is the same samples either way -- that is what the differential
# suite says, each language held against its own oracle out of a binary with
# all nine in it -- and it is one build rather than nine. It also means the
# gate walks the path a real caller walks, which is a library holding several
# languages and being asked for one.
native=${EVV_MATRIX_NATIVE:-}
if [ -z "$native" ]; then
    langs=""
    for t in $want; do langs="$langs lang/$t"; done
    langs=${langs# }
    suf=$(printf '%s' "$want" | tr ' ' '-')
    [ "$want" = "enus" ] && suf=""
    [ -n "$suf" ] && suf=-$suf
    echo "matrix: building a probe with $(printf '%s' "$want" | wc -w) languages in it"
    make -C "$root" -j"$(nproc)" RULES=bytecode probe LANGS="$langs" >/dev/null \
        || { echo "matrix: the build failed" >&2; exit 1; }
    native=$root/build/probe$suf
fi
[ -x "$native" ] || { echo "matrix: no binary at $native" >&2; exit 2; }
case $native in
*.exe) run_native() { $PE "$native" "$@"; } ;;
*)     run_native() { "$native" "$@"; } ;;
esac

work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT

# The reported answers are held verbatim, so anything in them that is not the
# engine's has to be constant. The only such thing is the path the samples
# were written to, which the probe prints, so the file is always called the
# same and always sits in the same directory.
out=$work/case.wav

mkdir -p "$store"
bad=0
total=0
moved=0

for t in $want; do
    export EVV_LANGUAGE=$(language_of "$t")
    baseline=$store/$t.sha256
    lines=""
    n_lang=0
    bad_lang=0

    if [ "$what" = check ] && [ ! -r "$baseline" ]; then
        echo "$t: no baseline in ${baseline#$root/}; record one" >&2
        bad=1
        continue
    fi

    for cat in $CATEGORIES; do
        stem=$(file_of "$cat")
        mode=$(mode_of "$cat")
        src=$cases/$stem-$t.txt
        [ "$t" = enus ] && src=$cases/$stem.txt
        [ -r "$src" ] || { echo "$t: no cases at ${src#$root/}" >&2; bad=1; continue; }

        i=0
        while IFS= read -r text; do
            [ -n "$text" ] || continue
            i=$((i + 1))
            n_lang=$((n_lang + 1))
            total=$((total + 1))
            printf '%s' "$text" > "$work/case.txt"
            rm -f "$out" "$out.again.wav"
            ( cd "$work" && run_native @case.txt case.wav $mode ) \
                > "$work/said.txt" 2>/dev/null

            # Both utterances where there are two, which is what the
            # `second' category is for: the probe writes the second beside the
            # first under a name of its own, and a hash of the first alone
            # would say nothing about it.
            if [ -s "$out" ] && [ -s "$out.again.wav" ]; then
                audio=$(cat "$out" "$out.again.wav" | sha256sum | cut -d' ' -f1)
            elif [ -s "$out" ]; then
                audio=$(sha256sum < "$out" | cut -d' ' -f1)
            else
                audio=nothing
            fi
            # The probe writes its lines the way the host does, and the
            # Windows build of it is one of the things checked here.
            #
            # Which languages the binary has in it is taken off first, and it
            # is the only thing that is. A baseline is a language's own, so it
            # has to be checkable out of a build with one language in it or
            # nine, and the inventory a probe prints at startup is a property
            # of the build rather than of the language. Everything else stays:
            # the index mark, the eight editable voices read back, every
            # general parameter, what the dictionary calls answered and how
            # many samples came out. That is the surface the audio cannot see
            # -- the loop that fills those voices can be turned off entirely
            # without a single sample moving, which is how a stale script got
            # such an edit past everything once.
            said=$(tr -d '\r' < "$work/said.txt" \
                   | grep -vE '^speak: [0-9]+ languages$|^speak:   language 0x' \
                   | sha256sum | cut -d' ' -f1)

            line="$cat $i ${audio:0:16} ${said:0:16} $text"
            if [ "$what" = record ]; then
                lines="$lines$line"$'\n'
                continue
            fi

            wanted=$(awk -v c="$cat" -v n="$i" \
                       '$1 == c && $2 == n { print $3, $4; exit }' "$baseline")
            [ -n "$wanted" ] || {
                echo "$t $cat case $i: not in the baseline: $text" >&2
                bad=1; bad_lang=$((bad_lang + 1)); continue; }
            set -- $wanted
            if [ "${audio:0:16}" != "$1" ] || [ "${said:0:16}" != "$2" ]; then
                which=""
                [ "${audio:0:16}" != "$1" ] && which="the samples"
                [ "${said:0:16}" != "$2" ] && \
                    which="${which:+$which and }what it answered"
                echo "$t $cat case $i: $which moved: $text" >&2
                bad=1; bad_lang=$((bad_lang + 1)); moved=$((moved + 1))
            fi
        done < "$src"
    done

    if [ "$what" = record ]; then
        {
            echo "# What this engine says, case by case, for $t."
            echo "#"
            if [ "$t" = plpl ]; then
                echo "# Polish is not IBM's and there is nothing to hold it"
                echo "# to: these are what this engine does, not what any"
                echo "# original did. They are here so that a change to"
                echo "# Polish can be told from an accident, which is the"
                echo "# only check Polish will ever have besides an ear."
            else
                echo "# Every line was blessed by IBM's own binary: the whole"
                echo "# differential suite was run for this language"
                echo "# immediately before these were written, and every case"
                echo "# matched. See docs/building.md."
            fi
            echo "#"
            echo "# Written by test/matrix.sh record. One line a case:"
            echo "# the category, which case in it, the first sixteen hex of"
            echo "# the samples' hash, the same of the reported answers', and"
            echo "# the sentence."
            echo ""
            printf '%s' "$lines"
        } > "$baseline"
        echo "$t: $n_lang cases written to ${baseline#$root/}"
    else
        # And that the baseline holds no case this run did not speak. A case
        # file that lost a line would otherwise pass in silence: every case
        # spoken would match and the missing one would simply never be
        # looked for, which is the one way a check like this can quietly
        # cover less than it did yesterday.
        held=$(awk '!/^#/ && NF' "$baseline" | wc -l)
        if [ "$held" != "$n_lang" ]; then
            echo "$t: the baseline holds $held cases and this run spoke" \
                 "$n_lang; a case file has gained or lost a line" >&2
            bad=1
            bad_lang=$((bad_lang + 1))
        fi
        if [ "$bad_lang" = 0 ]; then
            echo "$t: $n_lang cases, all as they were"
        else
            echo "$t: $n_lang cases, $bad_lang moved"
        fi
    fi
done

if [ "$what" = record ]; then
    echo "matrix: $total cases recorded"
    exit 0
fi

if [ "$bad" = 0 ]; then
    echo "matrix: $total cases, every one as it was"
    exit 0
fi

echo "matrix: $moved of $total cases moved" >&2
echo "If that was deliberate, say in the commit which cases moved and why," >&2
echo "then run test/matrix.sh record to write the new answers down." >&2
echo "test/suite.sh is still there if you want to ask what IBM's engine" >&2
echo "does with the same sentence." >&2
exit 1
