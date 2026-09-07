#!/usr/bin/env bash

set -euo pipefail

repoDir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sayCommand=$repoDir/build/openevv-say
realEvv=$repoDir/build/evv
testDir=$(mktemp -d)
trap 'rm -rf "$testDir"' EXIT

cat >"$testDir/evv" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" >"$TEST_EVV_ARGS"
if [[ " $* " == *" -l "* ]]; then
    printf 'voice list\n'
elif [[ " $* " == *" -L list "* ]]; then
    printf 'language list\n'
elif [[ " $* " == *" -o - "* ]]; then
    printf 'fake wave'
else
    output=
    while (($#)); do
        if [[ $1 == -o ]]; then
            output=$2
            break
        fi
        shift
    done
    printf 'fake wave' >"$output"
fi
EOF

cat >"$testDir/player" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" >"$TEST_PLAYER_ARGS"
cat >"$TEST_PLAYER_INPUT"
EOF
chmod +x "$testDir/evv" "$testDir/player"

export TEST_EVV_ARGS=$testDir/evv.args
export TEST_PLAYER_ARGS=$testDir/player.args
export TEST_PLAYER_INPUT=$testDir/player.input
export OPENEVV_EVV=$testDir/evv
export OPENEVV_PLAYER=$testDir/player

"$sayCommand" "Hello world"
printf '%s\n' "Hello world" -o - >"$testDir/expected-evv.args"
printf '%s\n' -q - >"$testDir/expected-player.args"
cmp "$testDir/expected-evv.args" "$TEST_EVV_ARGS"
cmp "$testDir/expected-player.args" "$TEST_PLAYER_ARGS"
[[ $(<"$TEST_PLAYER_INPUT") == "fake wave" ]]

rm -f "$TEST_PLAYER_ARGS"
"$sayCommand" -w "$testDir/out.wav" -v 3 -s 70 -p 60 -P 40 -a 80 \
    -R 22050 -L 0x10000 -r "Saved speech"
printf '%s\n' -v 3 -s 70 -p 60 -P 40 -V 80 -R 22050 -L 0x10000 -r \
    "Saved speech" -o "$testDir/out.wav" >"$testDir/expected-evv.args"
cmp "$testDir/expected-evv.args" "$TEST_EVV_ARGS"
[[ $(<"$testDir/out.wav") == "fake wave" ]]
[[ ! -e $TEST_PLAYER_ARGS ]]

[[ $("$sayCommand" --voices) == "voice list" ]]
[[ $("$sayCommand" --languages) == "language list" ]]

if "$sayCommand" -w "$testDir/out.wav" -d default text >/dev/null 2>&1; then
    printf '%s\n' 'openevv-say accepted -d with -w' >&2
    exit 1
fi

OPENEVV_PLAYER=$testDir/missing-player
export OPENEVV_PLAYER
if "$sayCommand" text >/dev/null 2>&1; then
    printf '%s\n' 'openevv-say accepted a missing player' >&2
    exit 1
fi

unset OPENEVV_EVV OPENEVV_PLAYER
"$realEvv" -o "$testDir/default.wav" "Pitch range test."
"$realEvv" -P 100 -o "$testDir/range.wav" "Pitch range test."
if cmp -s "$testDir/default.wav" "$testDir/range.wav"; then
    printf '%s\n' 'evv -P did not change the generated speech' >&2
    exit 1
fi

printf '%s\n' 'openevv-say tests passed'
