#!/usr/bin/env bash
# Pull every language module out of the EVV 4.3 libraries, one directory each.
#
# This is for comparison, not for building: what a language module is, is the
# rules, the statement table, the sets and the dictionaries, and the only way
# to see which of those are the language's own is to hold several side by
# side. The symbol prefixing extract.sh does is not wanted here -- that was
# for standing our objects beside IBM's, which is finished.
set -euo pipefail

ARCHIVE="${EVV_ARCHIVE:-/mnt/storage/Software/eloquence-archive}"
LIBDIR="$ARCHIVE/ibm-public/embedded-viavoice-sdk/extracted/evv4.3/wxp/lib/NT/X86/COMMON"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# The formant modules. The C-suffixed libraries beside them are the
# concatenative build of the same language, which uses recorded speech rather
# than the synthesiser, so their rules are not what we are reading.
LANGS="enus engb dede eses esus frfr frca itit jajp"

echo "extract-langs: reading $LIBDIR"
[ -d "$LIBDIR" ] || { echo "extract-langs: no such directory" >&2; exit 1; }

for tag in $LANGS; do
  lib="$LIBDIR/eci$tag.lib"
  if [ ! -f "$lib" ]; then
    echo "extract-langs: $tag has no library"
    continue
  fi
  out="$ROOT/analysis/$tag"
  mkdir -p "$out"
  (cd "$out" && rm -f ./*.obj && llvm-ar x "$lib")
  echo "extract-langs: $tag  $(ls "$out"/*.obj | wc -l) objects"
done
