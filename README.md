# openevv

A portable Eloquence. IBM's Embedded ViaVoice text-to-speech engine, taken out
of its 1999 Windows objects and rebuilt as C that compiles and speaks on a
machine it was never meant to run on.

It speaks, and it speaks IBM's own samples: the audio is byte for byte
identical to IBM's binary across all 81 test cases, from both a thirty-two and
a sixty-four bit build. Nothing is borrowed at build time. No DLL, no SDK, no
Wine.

    make
    ./build/evv -o hello.wav "Hello from Eloquence."

That wants a C compiler and about half a minute. On Linux nothing plays the
audio yet, so the engine writes a wave file; pipe it into a player to hear it at
once:

    ./build/evv "Hello from Eloquence." | aplay -q -

On Windows there is a speak window. Take `evvspeak.exe` from the latest release,
type something, pick one of the eight voices, and hear it; `evv.exe` beside it
is the same engine on the command line. One file each, nothing to install, and
`make win` builds both from here with mingw.

`eci.dll` is in the release too: the same engine exporting the names IBM
published, so a program written against IBM's library can load ours instead --
a screen reader add-on, for instance. It ships in both bitnesses, in folders
that say which is which: an add-on that loads the engine into the reader's own
process wants the reader's bitness, and the most used one hosts the engine in a
thirty-two bit process of its own whatever the reader is.

`./build/evv -h` says what the options are, and `./build/evv -l` says what each
of the eight voices is set to.

## What is here

`src` is the engine: hand-written C, one file per object in IBM's own module
decomposition, so that a file can be checked against the object it came from.

`lang/enus` is US English: the rules, the constants they read, the sets, the
link tables, the voice presets and the dictionary. This is the part lifted out
of IBM's objects rather than written, and it is in the tree so that the engine
builds without the SDK.

`cli/evv.c` is the command above and `win/speak.c` is the speak window.
`cli/probe.c` is the same engine behind a front that reports what it answered at
every step, which is what `test` sets against IBM's binary case for case.
`tools` holds the lifters, the decompiler and the analysers. `reference` builds
IBM's own binary under Wine, which is what the tests compare against.

## Documentation

`docs/building.md` is what you need, what to build, and what each variable
does.
`docs/status.md` is what works, what does not, and what has not been started.
`docs/tree.md` says what every directory is for.

## Licence and provenance

Our own work -- the engine, the two front ends, the tools, the tests and the
documents -- is under the MIT licence in LICENSE.

The language data under `lang/enus` is not ours. It is transcribed out of IBM's
Embedded ViaVoice objects, byte for byte where the engine's arithmetic depends
on it, and it is IBM's work. The MIT licence does not cover it and we are in no
position to license it to anyone. NOTICE says what it is, whose it is, and who
the rights in it may belong to today.

Nothing else of IBM's is here. The objects the port was read out of, and the
headers and symbol tables it was read with, are not in the tree and are not
needed to build. IBM still serves the SDK they came out of from its own
public download host, and `docs/building.md` says where it is and what to do
with it -- which is what anyone wanting to check this work against the original,
or to add one of the eight other languages, would start from.
