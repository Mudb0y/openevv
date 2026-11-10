#!/usr/bin/env bash
# Stands in for the engine's probe so that the recorded answers can be held
# against a build running on an attached Android device or emulator:
#
#   EVV_MATRIX_NATIVE=$PWD/android/native/tools/probe-on-device.sh \
#     test/matrix.sh check
#
# test/matrix.sh runs the probe from a working directory of its own with the
# case in case.txt and expects case.wav beside it, so that is all this moves:
# the case goes over, the engine runs there, and the wave files and the lines
# the probe reported come back. The matrix speaks several cases at once, so
# each gets a directory of its own on the device too, named for its own here.
# The probe has to be pushed to $EVV_DEVICE_DIR first; docs/android.md says
# how to build one.
#
# adb exec-out rather than adb shell, because a shell allocates a pty and a pty
# turns every LF in the reported lines, and every byte of the wave file that
# happens to be one, into CRLF.

set -e
export MSYS_NO_PATHCONV=1

adb=${ADB:-adb}
base=${EVV_DEVICE_DIR:-/data/local/tmp/evvmatrix}
dev=$base/$(basename "$PWD")
serial=${ANDROID_SERIAL:+-s $ANDROID_SERIAL}

$adb $serial exec-out "mkdir -p $dev" >/dev/null
$adb $serial push case.txt "$dev/case.txt" >/dev/null
$adb $serial exec-out "cd $dev && EVV_LANGUAGE='${EVV_LANGUAGE:-}' $base/probe $* 2>/dev/null"

for f in case.wav case.wav.again.wav; do
	$adb $serial exec-out "cat $dev/$f 2>/dev/null" > "$f" || true
	[ -s "$f" ] || rm -f "$f"
done
$adb $serial exec-out "rm -rf $dev" >/dev/null 2>&1 || true
