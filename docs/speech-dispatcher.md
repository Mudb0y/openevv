# Speech Dispatcher

OpenEVV has a native Speech Dispatcher output module for Linux. It keeps one
engine instance alive, streams 16-bit mono 11025 Hz PCM back to Speech
Dispatcher for playback, and does not open an audio device itself.

## Build and automatic test

Install Speech Dispatcher's development headers and module helper library,
plus `pkg-config`. Package names vary; on Arch Linux the files are supplied by
`speech-dispatcher`, while Debian-family systems normally use
`libspeechd-dev`. On a Nix machine `nix develop` has them: the flake's shell
carries `speechd`, `pkg-config` and `glib.dev`, the last because
`speech-dispatcher.pc` requires glib and pkg-config cannot answer for it
otherwise. None of that touches a running server -- the module hands its
samples back rather than opening a device, so what it needs is headers and a
library.

    make -j"$(nproc)" RULES=bytecode speechd-test

That builds `build/sd_openevv` and drives it through the output-module
protocol without playing sound. To build every available language into one
module and exercise each one:

    make -j"$(nproc)" RULES=bytecode speechd-test-all

The all-language executable is
`build/sd_openevv-enus-engb-dede-eses-esus-frfr-frca-itit-plpl-jajp`. Omit
`RULES=bytecode` for the faster-speaking compiled-rule build; generating it
takes substantially longer, and the suite passes in both forms.

Ten languages are linked and nine are offered. Japanese is deliberately left
out of the module's table: its text is Shift-JIS, EUC-JP or one of three
seven-bit JIS sets and its romanizer recodes whichever it was given, none of
which is UTF-8, and there is no converter here for them. Offering it would
hand a screen reader a language this module would mis-speak. `speechd-test-all`
therefore states the count it expects rather than counting `LANGS`, so a
language that quietly stopped being offered fails the check.

## Install

For a user-local compiled-rule installation with every language, use absolute
paths appropriate to the account:

    make -j"$(nproc)" RULES=c LANGS="lang/enus lang/engb lang/dede lang/eses lang/esus lang/frfr lang/frca lang/itit lang/plpl lang/jajp" PREFIX=/home/Username/.local SPEECHD_CONFDIR=/home/Username/.config/speech-dispatcher/modules speechd-install

This installs `sd_openevv` below the chosen module directory and
`openevv.conf` below the chosen configuration directory. Distribution
packagers can set `DESTDIR`, `SPEECHD_MODULEDIR`, and `SPEECHD_CONFDIR`
directly. Speech Dispatcher automatically discovers module binaries in its
system and user module directories when `speechd.conf` has no active
`AddModule` directives. Do not add an explicit OpenEVV registration to such a
configuration: Speech Dispatcher 0.12 skips automatic discovery as soon as any
module is explicitly registered, which can hide every other installed voice.

Older OpenEVV packages included a helper that added an explicit registration.
After upgrading, remove that registration from the current user's configuration
with:

    openevv-speechd-enable

If the helper was previously run with `sudo`, repair the system configuration
explicitly as root:

    sudo openevv-speechd-enable --system

The helper backs up a configuration before changing it. If OpenEVV is the only
explicit module written by the older package, the helper removes it and restores
automatic discovery. If the configuration already lists other modules
explicitly, the helper keeps or adds OpenEVV alongside them instead. Custom
OpenEVV registrations are left untouched. The helper does not restart Speech
Dispatcher itself. Restarting temporarily takes speech away, so do that only
from a session that can be recovered without hearing.

## Try it without installing

Add the following to `~/.config/speech-dispatcher/speechd.conf`, replacing
`/absolute/path/openevv` with this checkout's absolute path:

    AddModule "openevv" "/absolute/path/openevv/build/sd_openevv" "/absolute/path/openevv/speechd/openevv.conf"

This explicit development registration disables automatic module discovery;
the test daemon will load only modules that have their own active `AddModule`
lines. Do not leave it in the configuration used by a screen reader. If testing
the all-language build, use its suffixed executable name instead.
Stop the existing per-user daemon with `killall speech-dispatcher`; the next
client or screen reader connection will start it with the new configuration.

**That command stops speech.** On a machine where a screen reader is how its
user reads the screen, killing the daemon takes the speech away until
something reconnects, and a module that will not start leaves it away. So do
it in a session that can be recovered without hearing: a second machine, an
ssh login, or a terminal already speaking through another synthesiser. Never
replace a distribution module or a system configuration while evaluating a
development build, for the same reason -- the fallback has to stay working.

Confirm discovery before listening:

    spd-say -O
    spd-say -o openevv -L
    spd-say -o openevv -w "OpenEVV through Speech Dispatcher."

Useful coverage includes `-r`, `-p`, `-R`, and `-i` at negative, zero, and
positive values; `-m none|some|most|all`; `-s`; `-c`; `-k`; every `-t` voice
type; and exact voices such as `-y enus-elderly-male`. For a multilingual
build, try `-l en-US`, `de-DE`, `en-GB`, `es-ES`, `es-MX`, `fr-FR`, `fr-CA`,
`it-IT` and `pl-PL`, including accented text. Polish is the one worth listening
to closely: it is the only language whose text the engine converts from UTF-8
itself, so it is the only one where the module has to keep its hands off the
bytes. Listen to text containing symbols under
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
UTF-8 input is converted to the Latin-1 input used by the nine languages IBM
shipped. Left and right curly apostrophes are normalized to ASCII apostrophes;
other characters outside Latin-1 become `?`.

Polish is the exception and gets no conversion here at all. It declares code
points of its own, so the engine converts its text from UTF-8 itself, and
converting it twice would turn eight of its nine diacritics into question
marks -- 99,616 PCM bytes for the pangram where leaving it alone gives 36,762.
Two byte-wise walks had to learn the same thing, because the ranges collide
exactly: a UTF-8 lead byte of 0xc0 to 0xdf reads as a Latin-1 capital and a
continuation byte of 0xa1 to 0xbf reads as a symbol, so a lowercase Polish z
with a dot was announced as a capital and had its second byte replaced with a
space. Above 0x7f in UTF-8 the answer is that a byte is not a character to
judge, and one consequence is honest to state: capital recognition works on
Polish for the ASCII letters and does not announce the accented capitals.

SSML support is deliberately small: `<mark name="...">` produces index
events, `<break>` inserts a pause, the five predefined XML entities are
decoded, and other tags are ignored while their text is spoken. Prosody,
phoneme, substitution, and audio elements are not implemented. Capital
recognition handles ordinary text, character, and key messages: `spell` says
“capital” before uppercase letters and `icon` emits Speech Dispatcher's
`capital` sound-icon event.

Set `Debug 1` in `speechd/openevv.conf` only while diagnosing the module.
Speech Dispatcher owns the log destination and audio backend.
