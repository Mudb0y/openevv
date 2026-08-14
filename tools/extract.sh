#!/usr/bin/env bash
# Pull the objects we transcribe out of the EVV 4.3 static libraries.
set -euo pipefail

ARCHIVE="${EVV_ARCHIVE:-/mnt/storage/Software/eloquence-archive}"
LIBDIR="$ARCHIVE/ibm-public/embedded-viavoice-sdk/extracted/evv4.3/wxp/lib/NT/X86/COMMON"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/analysis/obj"

echo "extract: reading $LIBDIR"

if [ ! -d "$LIBDIR" ]; then
  echo "extract: error: SDK library directory not found: $LIBDIR" >&2
  exit 1
fi

mkdir -p "$OUT"
cd "$OUT"

# clsyn.obj is compiled identically into every language library; ecienus is
# simply the one we happen to read it from.
llvm-ar x "$LIBDIR/ecienus.lib" clsyn.obj
llvm-ar x "$LIBDIR/ecienus.lib" runklatt.obj

for f in clsyn.obj runklatt.obj; do
  echo "extract: $f $(stat -c %s "$f") bytes"
done

# GNU as cannot parse '?' in a symbol reference, and we want IBM's functions
# and ours in one binary anyway, so give every original symbol an ibm_ prefix.
# Every symbol the object defines gets an ibm_ prefix, functions and tables
# alike, so the original and the rewrite can sit in one binary. i386 PE
# prefixes C identifiers with an underscore, so the new names carry one even
# though the MSVC C++ manglings they replace do not. String literals (??_C@)
# and compiler bookkeeping are left alone.
llvm-objdump --syms clsyn.obj \
  | awk '$0 ~ /\(sec  *[1-9]/ {
           n = $NF
           if (n ~ /^\?\?_C@/) next
           if (n ~ /^[.@]/) next
           if (n ~ /^\?[A-Za-z_]/) { s = n; sub(/@@.*/, "", s); sub(/^\?/, "", s) }
           else if (n ~ /^_[A-Za-z]/) { s = substr(n, 2) }
           else next
           print n " _ibm_" s
         }' | sort -u > rename.txt

# llvm-objcopy refuses symbol surgery on COFF, so the binutils one does it.
# .drectve carries MSVC /DEFAULTLIB directives that GNU ld cannot parse.
OBJCOPY=i686-w64-mingw32-objcopy

$OBJCOPY --redefine-syms=rename.txt --remove-section=.drectve \
  clsyn.obj clsyn-ibm.obj

# Several of these are static in the original and so invisible to a linker.
# Nothing else in the harness shares the ibm_ prefix, so widening them is safe.
llvm-objdump --syms clsyn-ibm.obj \
  | awk '/\(scl   3\)/ && /_ibm_/ { print $NF }' | sort -u > globalize.txt

$OBJCOPY --globalize-symbols=globalize.txt clsyn-ibm.obj
rm -f test-glob.obj
echo "extract: renamed $(wc -l < rename.txt) symbols, widened $(wc -l < globalize.txt) statics"
