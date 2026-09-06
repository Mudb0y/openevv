#!/usr/bin/env bash
# Stands in for openevv's probe so that its own gate can drive a build running
# on an attached Android device.
#
#   EVV_MATRIX_NATIVE=native/tools/probe-on-device.sh \
#     bash test/matrix.sh check enus
#
# test/matrix.sh runs the probe from a working directory of its own with the
# case in case.txt and expects case.wav beside it, so that is all this moves:
# the case goes over, the engine runs there, and the wave files and the lines
# the probe reported come back. Push the probe to $EVV_DEVICE_DIR first.
#
# adb exec-out rather than adb shell, because a shell allocates a pty and a pty
# turns every LF in the reported lines, and every byte of the wave file that
# happens to be one, into CRLF.

set -e
export MSYS_NO_PATHCONV=1

adb=${ADB:-adb}
dev=${EVV_DEVICE_DIR:-/data/local/tmp/evvmatrix}
serial=${ANDROID_SERIAL:+-s $ANDROID_SERIAL}

$adb $serial exec-out "rm -f $dev/case.txt $dev/case.wav $dev/case.wav.again.wav" >/dev/null 2>&1 || true
$adb $serial push case.txt "$dev/case.txt" >/dev/null
$adb $serial exec-out "cd $dev && EVV_LANGUAGE='${EVV_LANGUAGE:-}' ./probe $* 2>/dev/null"

for f in case.wav case.wav.again.wav; do
	$adb $serial exec-out "cat $dev/$f 2>/dev/null" > "$f" || true
	[ -s "$f" ] || rm -f "$f"
done
