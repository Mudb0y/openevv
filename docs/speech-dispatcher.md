# Speech Dispatcher

OpenEVV has a native Speech Dispatcher output module for Linux. It keeps one
engine instance alive, streams 16-bit mono 11025 Hz PCM back to Speech
Dispatcher for playback, and does not open an audio device itself.

## Build and automatic test

Install Speech Dispatcher's development headers and module helper library,
plus `pkg-config`. Package names vary; on Arch Linux the files are supplied by
`speech-dispatcher`, while Debian-family systems normally use
`libspeechd-dev`.

    make -j"$(nproc)" RULES=bytecode speechd-test

That builds `build/sd_openevv` and drives it through the output-module
protocol without playing sound. To build every available language into one
module and exercise each one:

    make -j"$(nproc)" RULES=bytecode speechd-test-all

The all-language executable is
`build/sd_openevv-enus-dede-engb-eses-esus-frfr-frca-itit`. Omit
`RULES=bytecode` for the faster-speaking compiled-rule build; generating it
takes substantially longer.

## Install

For a user-local compiled-rule installation with all eight languages, use
absolute paths appropriate to the account:

    make -j"$(nproc)" RULES=c LANGS="lang/enus lang/dede lang/engb lang/eses lang/esus lang/frfr lang/frca lang/itit" PREFIX=/home/Username/.local SPEECHD_CONFDIR=/home/Username/.config/speech-dispatcher/modules speechd-install

This installs `sd_openevv` below the chosen module directory and
`openevv.conf` below the chosen configuration directory. Distribution
packagers can set `DESTDIR`, `SPEECHD_MODULEDIR`, and `SPEECHD_CONFDIR`
directly. Speech Dispatcher does not discover module binaries automatically;
add the installed paths to the user's `speechd.conf`:

    AddModule "openevv" "/home/Username/.local/libexec/speech-dispatcher-modules/sd_openevv" "/home/Username/.config/speech-dispatcher/modules/openevv.conf"

## Try it without installing

Add the following to `~/.config/speech-dispatcher/speechd.conf`, replacing
`/absolute/path/openevv` with this checkout's absolute path:

    AddModule "openevv" "/absolute/path/openevv/build/sd_openevv" "/absolute/path/openevv/speechd/openevv.conf"

If testing the all-language build, use its suffixed executable name instead.
Stop the existing per-user daemon with `killall speech-dispatcher`; the next client
or screen reader connection will start it with the new configuration. Do not
replace a distribution module or system configuration while evaluating a
development build.

Confirm discovery before listening:

    spd-say -O
    spd-say -o openevv -L
    spd-say -o openevv -w "OpenEVV through Speech Dispatcher."

Useful coverage includes `-r`, `-p`, `-R`, and `-i` at negative, zero, and
positive values; `-m none|some|most|all`; `-s`; `-c`; `-k`; every `-t` voice
type; and exact voices such as `-y enus-elderly-male`. For a multilingual
build, try `-l en-US`, `de-DE`, `en-GB`, `es-ES`, `es-MX`, `fr-FR`, `fr-CA`,
and `it-IT`, including accented text. Listen to text containing symbols under
each `-m` mode: the direct protocol test cannot cover the server-side symbol
names or translations. Finally, test rapid interruption and language changes
in the actual screen reader, because that test also cannot establish audible
latency, playback routing, or application behavior.

## Supported behavior and limits

The module advertises eight voice presets per built language. It supports
Speech Dispatcher rate, pitch, pitch range, volume, voice type, exact voice,
language, spelling, character, key, sound-icon fallback, stop, pause at Speech
Dispatcher's next internal index mark, and index-mark events. Punctuation and
symbol names use Speech Dispatcher's language-aware server-side symbol
preprocessing. When the server inserts a symbol name and changes the module's
punctuation mode to `none`, the module suppresses the retained non-prosodic
symbol so names such as `dash-` and `left paren(` are not spoken twice. It
preserves sentence punctuation for pauses and apostrophes inside words.
UTF-8 input is converted to the Latin-1 input used by the eight current OpenEVV
languages. Left and right curly apostrophes are normalized to ASCII apostrophes;
other characters outside Latin-1 become `?`.

SSML support is deliberately small: `<mark name="...">` produces index
events, `<break>` inserts a pause, the five predefined XML entities are
decoded, and other tags are ignored while their text is spoken. Prosody,
phoneme, substitution, and audio elements are not implemented. Capital
recognition handles ordinary text, character, and key messages: `spell` says
“capital” before uppercase letters and `icon` emits Speech Dispatcher's
`capital` sound-icon event.

Set `Debug 1` in `speechd/openevv.conf` only while diagnosing the module.
Speech Dispatcher owns the log destination and audio backend.
