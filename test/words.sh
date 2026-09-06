#!/usr/bin/env bash
#
# The gate a level below test/matrix.sh: one word to a line rather than one
# sentence, and what the engine says it is made of rather than what it sounds
# like.
#
# Why both. The sentence gate has 98 English cases in it, which is the right
# size for asking whether the engine still says what it always said, and far
# too small for asking whether a change to a rule or a dictionary was a good
# idea. A change that mends forty words and breaks four hundred passes it
# without a murmur. Twenty thousand words will not.
#
# It reads what `eciGeneratePhonemes' answers rather than the audio, which is
# what makes it affordable: no wave file, no hashing, nothing to compare but
# text, and a moved word names itself instead of being a hash that changed.
# It wants neither Wine nor IBM's objects.
#
# A word that moves is a question, exactly as in the sentence gate. Say in the
# commit which moved and why, then `test/words.sh record' writes them down.
#
# usage: test/words.sh check  [lang ...]     the default
#        test/words.sh record [lang ...]
#
# EVV_WORDS_NATIVE names a phonemes binary to drive rather than building one.

set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/.." && pwd)
cases=$here/cases
store=$here/samples

what=${1:-check}
case $what in
check|record) shift || true ;;
*)            what=check ;;
esac

want=${*:-enus}

bad=0
for tag in $want; do
    src=$cases/words-$tag.txt
    baseline=$store/$tag.words
    [ -r "$src" ] || { echo "words: no list at ${src#$root/}" >&2; bad=1; continue; }

    native=${EVV_WORDS_NATIVE:-}
    if [ -z "$native" ]; then
        make -C "$root" -j"$(nproc)" phonemes-bin EVVLANG="lang/$tag" \
            >/dev/null 2>&1 || true
        native=$root/build/phonemes
        [ "$tag" = enus ] || native=$root/build/phonemes-$tag
    fi
    # Spoken from a directory of its own, so a relative name would not be
    # found once there -- the same trap test/matrix.sh has a guard for.
    case $native in
    /*) ;;
    *)  native=$PWD/$native ;;
    esac
    [ -x "$native" ] || { echo "words: no binary at $native" >&2; bad=1; continue; }

    work=$(mktemp -d) || exit 1
    trap 'rm -rf "$work"' EXIT

    grep -v '^#' "$src" | grep -v '^$' > "$work/words.txt"
    total=$(wc -l < "$work/words.txt")

    # Every word is its own instance -- test/harness/phonemes.c makes and
    # deletes one a line -- so nothing carries from one to the next and the
    # list can be cut into as many pieces as there are cores.
    jobs=$(nproc)
    split -n l/"$jobs" -d -a 3 "$work/words.txt" "$work/part."
    for p in "$work"/part.*; do
        ( "$native" "$p" > "$p.said" 2>/dev/null ) &
    done
    wait
    for p in "$work"/part.*; do
        case $p in *.said) continue ;; esac
        cat "$p.said"
    done > "$work/said.txt"

    # A word the engine answers nothing for is its own kind of failure and a
    # worse one than a moved pronunciation: it means a change has made the
    # engine give up rather than differ. It also destroys the alignment
    # between the list and the answers, so nothing may be recorded or
    # compared until it is accounted for. Finding which words needs one
    # process a word, which is slow -- so it happens only when the count is
    # already known to be short.
    said=$(wc -l < "$work/said.txt")
    if [ "$said" != "$total" ]; then
        echo "words: $tag answered $said of $total; finding which" >&2
        : > "$work/silent.txt"
        for p in "$work"/part.*; do
            case $p in *.said) continue ;; esac
            [ "$(wc -l < "$p")" = "$(wc -l < "$p.said")" ] && continue
            while IFS= read -r w; do
                printf '%s\n' "$w" > "$work/one.txt"
                if [ "$("$native" "$work/one.txt" 2>/dev/null | wc -l)" = 0 ]; then
                    printf '%s\n' "$w" >> "$work/silent.txt"
                fi
            done < "$p"
        done
        echo "words: $tag, $(wc -l < "$work/silent.txt") of them say nothing at all:" >&2
        head -20 "$work/silent.txt" | sed 's/^/   /' >&2
        bad=1
        continue
    fi
    paste -d'	' "$work/words.txt" "$work/said.txt" > "$work/now.txt"

    if [ "$what" = record ]; then
        {
            echo "# What this engine says each word is made of, for $tag."
            echo "#"
            echo "# One word a line and its phonemes beside it, written by"
            echo "# test/words.sh record. The list is test/cases/words-$tag.txt."
            echo "# These are what this engine does, not what IBM's did: the"
            echo "# differential suite speaks sentences, not word lists."
            cat "$work/now.txt"
        } > "$baseline"
        echo "words: $tag, $total words written to ${baseline#$root/}"
        continue
    fi

    [ -r "$baseline" ] || {
        echo "words: no baseline in ${baseline#$root/}; record one" >&2
        bad=1; continue; }

    grep -v '^#' "$baseline" > "$work/was.txt"
    moved=$(diff --unchanged-line-format= --old-line-format='%L' \
                 --new-line-format= "$work/was.txt" "$work/now.txt" \
            | wc -l)
    if [ "$moved" = 0 ]; then
        echo "words: $tag, $total words, every one as it was"
    else
        echo "words: $tag, $total words, $moved moved" >&2
        diff "$work/was.txt" "$work/now.txt" | grep '^[<>]' | head -40 >&2
        [ "$moved" -gt 20 ] && echo "  ... $moved in all" >&2
        bad=1
    fi
    rm -rf "$work"
    trap - EXIT
done

if [ "$bad" != 0 ] && [ "$what" = check ]; then
    echo "If that was deliberate, say in the commit which words moved and why," >&2
    echo "then run test/words.sh record to write the new answers down." >&2
fi
exit $bad
