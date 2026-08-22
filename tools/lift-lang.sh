#!/usr/bin/env bash
#
# Everything a language module is, out of IBM's objects and into lang/<code>.
#
# One directory a language, named after it, holding the same set of files
# lang/enus holds. What each of the five steps writes is in docs/building.md;
# the order matters only in that the rules have to be emitted before the sets,
# which name them.
#
# It wants IBM's objects in analysis/<code>, which tools/extract-langs.sh puts
# there out of the SDK. Nothing here is needed to build a language that has
# already been lifted.
#
# A language already in lang/ is left alone: lifting it again would lose any
# pronunciation edited into it. Say --force to mean it anyway.
#
# usage: lift-lang.sh [--force] <code> [...]   e.g. lift-lang.sh dede itit
#        lift-lang.sh all
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ALL="enus engb dede eses esus frfr frca itit jajp"

want=("$@")
if [ "${#want[@]}" -eq 0 ]; then
    echo "usage: lift-lang.sh <code> [...] | all" >&2
    echo "       one of: $ALL" >&2
    exit 2
fi
force=no
if [ "${want[0]}" = "--force" ]; then
    force=yes
    want=("${want[@]:1}")
fi

if [ "${want[0]}" = "all" ]; then
    read -r -a want <<< "$ALL"
fi

# Lifting a language that is already in the tree throws away anything that was
# edited into it. tools/delta-sets.py is the sharp one: it puts IBM's own
# dictionary tables back and loses every pronunciation added through
# tools/delta-dict.py, which is why CLAUDE.md says not to run it to
# "regenerate" that file. So a directory that exists is left alone unless
# whoever is asking says they mean it.
for L in "${want[@]}"; do
    if [ ! -d "analysis/$L" ]; then
        echo "lift-lang: no objects in analysis/$L" >&2
        exit 1
    fi
    if [ -d "lang/$L" ] && [ "$force" = no ]; then
        echo "lift-lang: lang/$L is already there, leaving it alone."
        echo "lift-lang:   Lifting it again would put IBM's own dictionary"
        echo "lift-lang:   tables back and lose anything delta-dict.py added."
        echo "lift-lang:   Say --force if that is what you want."
        continue
    fi
    echo "lift-lang: $L"
    mkdir -p "lang/$L"

    # The rules as bytecode, the constants they name, the header saying how
    # big a frame is, and the stand-ins for rules that were not emitted.
    python3 tools/delta-emit.py "analysis/$L" "lang/$L"

    # The lookup sets and the dictionary's actions.
    python3 tools/delta-sets.py "$L"

    # The statement table: what each kind of record is and how to reach it.
    python3 tools/delta-link.py "$L"

    # Where the language's own variables sit in the machine's state.
    python3 tools/gen-globals.py "analysis/$L/glob.obj" \
        "lang/$L/delta_globals_$L.c"

    # The settings the engine runs on, including this language's eight voices
    # and its phoneme durations.
    python3 tools/gen-ini.py "analysis/$L" "lang/$L/eci_ini_$L.c"

    echo "lift-lang: $L done -- $(ls lang/$L | wc -l) files"
done
