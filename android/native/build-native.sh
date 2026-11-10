#!/usr/bin/env bash
# Builds libevvjni.so out of this tree's engine, one file per ABI.
#
# The engine comes out of the tree's own Makefile, which writes the language
# rules with Python and then compiles them; that work is per language rather
# than per ABI, so the first ABI pays for it and the rest do not. What this
# adds is the JNI bridge, linked with the engine and its published ECI names
# into one shared object so there is no soname for Android's packaging to trip
# over.

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
android=$(cd "$here/.." && pwd)
engine=$(cd "$android/.." && pwd)

ABIS=${ABIS:-"arm64-v8a armeabi-v7a x86_64"}
API=${API:-21}
RULES=${RULES:-c}
LANGS=${LANGS:-lang/enus lang/engb lang/eses lang/esus lang/frfr lang/frca lang/dede lang/itit lang/fifi lang/ptbr lang/plpl lang/jajp}
OUT=${OUT:-"$android/app/build/native/jniLibs"}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}

# The engine writes its rules with Python, and every language module's text is
# UTF-8. Python reads a file in the locale's encoding unless it is told
# otherwise, which on Windows is cp1252 and cannot read Polish's rules at all.
# This is what says UTF-8 whatever the machine's locale is.
export PYTHONUTF8=1

NDK=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
if [ -z "$NDK" ]; then
	echo "set ANDROID_NDK_HOME to an NDK r23 or later" >&2
	exit 1
fi
NDK=${NDK//\\//}
OUT=${OUT//\\//}

case "$(uname -s)" in
	Linux*)  hosttag=linux-x86_64 ;;
	Darwin*) hosttag=darwin-x86_64 ;;
	*)       hosttag=windows-x86_64 ;;
esac
ndk_prebuilt="$NDK/prebuilt/$hosttag/bin"
if command -v cygpath >/dev/null 2>&1; then
	ndk_prebuilt_posix=$(cygpath -u "$ndk_prebuilt")
	if [ -d "$ndk_prebuilt_posix" ]; then
		export PATH="$ndk_prebuilt_posix:$PATH"
	fi
elif [ -d "$ndk_prebuilt" ]; then
	export PATH="$ndk_prebuilt:$PATH"
fi
bin="$NDK/toolchains/llvm/prebuilt/$hosttag/bin"
exe=""
[ -x "$bin/clang" ] || exe=".exe"
clang="$bin/clang$exe"
strip="$bin/llvm-strip$exe"
make="$ndk_prebuilt/make$exe"
if [ ! -x "$make" ]; then
	make="make"
fi
if [ ! -x "$clang" ]; then
	echo "no clang at $clang" >&2
	exit 1
fi

triple_for() {
	case "$1" in
		arm64-v8a)   echo "aarch64-linux-android$API" ;;
		armeabi-v7a) echo "armv7a-linux-androideabi$API" ;;
		x86_64)      echo "x86_64-linux-android$API" ;;
		x86)         echo "i686-linux-android$API" ;;
		*)           echo "unknown ABI $1" >&2; exit 1 ;;
	esac
}

for abi in $ABIS; do
	triple=$(triple_for "$abi")
	build="build/android-$abi"
	echo "==> $abi ($triple)"
	# -fsigned-char, because a plain char is signed on x86 and unsigned on ARM,
	# and this engine is x86 code transcribed: it reads bytes out of its tables
	# and compares them as numbers, so where the original saw -114 an ARM build
	# would see 142 and the comparison would go the other way. Turning it on
	# changed no sample of the text tried, so it is the safer of the two rather
	# than the fix for anything heard.
	#
	# The Makefile's own library flags do the rest: position independent code,
	# only the published names visible, and with the rules compiled every
	# function in a section of its own, so that the link below can drop the
	# bytecode nothing reads.
	rsp=$("$make" -C "$engine" -s -j"$JOBS" \
		RULES="$RULES" \
		LANGS="$LANGS" \
		BUILD="$build" \
		CC="$clang --target=$triple" \
		CFLAGS="-fsigned-char" \
		android-objects | tee /dev/stderr | tail -n 1)
	mkdir -p "$OUT/$abi"
	( cd "$engine" && "$clang" --target="$triple" \
		-O2 -std=gnu99 -fPIC -fvisibility=hidden -fsigned-char \
		-Werror=int-conversion -Werror=incompatible-pointer-types \
		-Iinclude \
		-shared \
		"$android/app/src/main/cpp/evv_jni.c" \
		"@$rsp" \
		-Wl,--gc-sections \
		-Wl,-z,max-page-size=16384 \
		-lm -llog \
		-o "$OUT/$abi/libevvjni.so" )
	# The unstripped copy stays behind so a crash address out of logcat can be
	# turned back into a function with llvm-symbolizer.
	cp "$OUT/$abi/libevvjni.so" "$engine/$build/libevvjni.unstripped.so"
	"$strip" "$OUT/$abi/libevvjni.so"
	ls -l "$OUT/$abi/libevvjni.so"
done

echo "native libraries are under $OUT"
