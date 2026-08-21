# Building openevv

## What you need

A C compiler. That is the whole of it for an ordinary build: the language data
is in the tree, so there is no IBM SDK to find and nothing is downloaded.

Three things are wanted only for particular jobs. Python 3 writes the rules out
as C, which `make RULES=c` asks for and an ordinary build does not. A
thirty-two bit compiler builds the thirty-two bit engine. Wine and IBM's own
objects run the comparison tests, which is the only automatic check that the
audio is right.

On this machine all of those come from the flake, and `nix develop` puts them
on the path.

## Building

    make

That builds `build/libevv.a` and `build/evv`, which speaks. From nothing, that
is about half a minute on one core and under twenty seconds with `make -j8`.

    make probe

That builds `build/probe` instead: the same engine behind the front the tests
drive. It prints what the engine answered at every step so those answers can be
set against IBM's, which is why it is not the thing to run by hand.

    make evv32
    make probe32

The same two, thirty-two bit. That build is a check rather than a target: a
difference between the word sizes is a layout mistake, and this is what makes
one show up early. It needs a thirty-two bit compiler, which is `CC32`.

On a Nix machine `nix build` makes the same binary at `result/bin/evv`, and
`nix run . -- -o hello.wav "text"` runs it without installing anything.
`nix develop` is the shell the rest of this assumes: the thirty-two bit
compiler, Wine and Python on the path.

`make install` copies the binary to `/usr/local/bin/evv`, or wherever `PREFIX`
and `DESTDIR` say. There is nothing else to install: it reads no file of its own
at run time and wants no library but the C one, libm and pthreads. `make clean`
takes the objects and the binaries away and leaves the generated C alone.

## The variables

`CC` is the compiler for this machine, `cc` by default. `CC32` is the
thirty-two bit one, which on this machine is the cross compiler the flake
provides, `i686-unknown-linux-gnu-gcc`, and elsewhere is usually the host
compiler with a flag: `make evv32 CC32="gcc -m32"`. `NM` is used by `make
missing`. `OPT` is the optimisation level, `-O2`. `CFLAGS` is added to both
builds after everything else, so it can override.

`RULES` chooses which form of the language's rules gets linked, and is
explained next.

## The rules, twice

The language's rules exist in the tree as bytecode, and the engine has an
interpreter for them. They also exist as C: `tools/delta-decompile.py` writes
all 3,377 of them out of that same bytecode into `lang/enus/delta_rules_c.c`,
and the interpreter prefers a rule written as C wherever it finds one.

Both speak the same samples, so an ordinary build links an empty table and runs
every rule as bytecode. The C is thirteen megabytes in one file, which is seven
minutes of compiler and Python to write it first, and it is asked for by name:

    make RULES=c

That is the form to build when the decompiler itself is what is being worked
on, since it is the C that a change to it changes. `make rules` writes the file
without building anything. It is not kept in the tree, because every change to
the decompiler rewrites the whole of it.

## Running

    ./build/evv -o hello.wav "Hello from Eloquence."
    ./build/evv -f speech.txt -o speech.wav
    ./build/evv "Hello from Eloquence." | aplay -q -
    echo "Hello from Eloquence." | ./build/evv | pw-play -

With no `-o` it writes the wave to standard output, unless that is a terminal,
in which case it says so rather than filling the terminal with samples. With no
text it reads standard input.

`-v` picks one of the eight voices, `-s` the speed, `-p` the pitch and `-V` the
volume. Those numbers are the engine's own; `-r` makes them a person's instead,
so speed is words per minute and pitch is hertz. `-l` prints what each voice is
set to, in whichever units are in force.

## Windows

    make win

That cross-compiles two binaries with mingw: `build/evvspeak.exe`, the speak
window, and `build/evv.exe`, the same console driver as on this machine. Both
are static, so each is one file that wants nothing installed, and both are
sixty-four bit. `make win-probe` builds the test driver as `build/probe.exe`,
which `EVV_NATIVE=$PWD/build/probe.exe test/suite.sh` will run against IBM's
binary case for case, under the same Wine.

The speak window is the only front end anywhere in this tree that plays what it
makes. It types into a multiline box, picks one of the eight voices, takes the
rate in words a minute and the pitch in hertz, saves a wave file if asked, and
plays through waveOut, which every Windows since 1995 has. Control and Enter
speaks, Escape stops, and Escape again closes. Everything in it is a control
Windows ships, so a screen reader reads it without being told anything.
`evvspeak.exe /say "some text"` speaks at once and is how the sound gets tested
without a mouse.

Two things about the Windows build are worth knowing. `src/port_win32.c` stands
in for `src/port_posix.c`, which is the whole of the platform layer. And the
arena takes its region from VirtualAlloc at the same low addresses mmap gets on
Linux. The image itself is an ordinary PE at whatever base mingw chooses, with
ASLR on: nothing needs it low any more.

### The library

`make win` also builds `build/eci.dll`, which is the same engine with the names
IBM published on the outside: `eciNew`, `eciAddText`, `eciSynthesize` and the
rest, fifty-two of them, exported under those spellings from a sixty-four bit
library that wants nothing but the system's own DLLs. `win/eci_api.c` is the
whole of it, one wrapper per name.

The point of it is that a program written against IBM's `eci.dll` can load ours
instead. That program is usually a screen reader add-on: NVDA is a sixty-four
bit process now, and the add-on most people have loads the library with ctypes'
`windll`, calls seventeen of these, and hands in a callback made with
`WINFUNCTYPE`. `build/eci.ini` is copied out beside the library because add-ons
look for one and rewrite a path inside it; nothing here reads it, since the
engine carries its own settings in the image.

None of the calling convention trouble that a thirty-two bit build would bring
applies: on x86-64 `__stdcall` and `__cdecl` are the same thing, a stdcall name
carries no `@N` to strip, and a stdcall callback is callable as anything.

`make win32` builds the same library thirty-two bit, as `build/eci32.dll`, and
that one is not optional. The most used screen reader driver --
davidacm/NVDA-IBMTTS-Driver -- does not load the engine into the reader's own
process at all: it launches `rundll32.exe` from `SysWOW64` and hosts the engine
there, talking to it over a named pipe, so the library it loads is thirty-two
bit whatever the reader is. The other kind of add-on loads it in process and
therefore wants the bitness the reader has. Both are shipped, in folders that
say which is which, because dropping the wrong one in is the mistake to design
out.

Thirty-two bit is the easier build of the two: a pointer is a value there, so
there is no arena at all. It does want `--kill-at`, because stdcall decorates a
name with `@N` on x86 and a caller asking by name wants it plain, and it is
where a wrong signature shows up -- the argument size is part of the decorated
name, so a declaration that does not match the engine fails to link. Three of
mine did not, and the sixty-four bit linker had accepted them silently.

`win/eci.rc` gives both libraries a version resource, which is not decoration
either. That driver reads `ProductName` out of it to decide which engine it is
talking to: `IBMECI` turns on IBMTTS-specific text fixes, a different pause
style and a 22 kHz sample rate. This is the Eloquence engine at 11 kHz, so it
says `openevv` and gets treated accordingly. NVDA's own reader also raises
rather than loading a file with no version information at all, so without the
resource that driver would refuse us before it ever called anything.

One caveat about mixing toolchains, learned by tripping over it. The libraries
in a release are built by one mingw and tested with harnesses built by the same
one. A caller built by a *different* mingw, with a different thread runtime --
nixpkgs' uses mcfgthreads where Debian's uses winpthreads -- can fault on the
crossing, and one direction of that pairing does. It does not matter for the
callers that exist: Python's ctypes and a screen reader's host DLL are MSVC
built, with no mingw runtime in them at all, and CI checks both of those
crossings on Windows itself. But do not conclude from a fault in a hand-mixed
pair that the shipped library is broken; check a matched pair first.

Two ways to check it, and both are worth having. `make win-dlltest` builds
`build/dlltest.exe`, which links against nothing, loads `eci.dll` by name, asks
for each entry point by name and speaks; `test/hash.sh build/dlltest.exe` then
holds what comes out of the library against what comes out of everything else.
`test/dll.py` does the same through ctypes, which is a different question --
ctypes has its own ideas about handles, and a handle is sixty-four bits -- and
CI runs it on Windows itself. `make win32` builds `build/dlltest32.exe` for the
thirty-two bit library; that one is checked from C, since a sixty-four bit
Python cannot load a thirty-two bit library at all. Both harnesses also read
the version resource and fail if it is missing.

What the library does not export: the filter interface, which the engine does
not implement, and `eciGeneratePhonemes` and the dictionary find, lookup and
update calls, which exist inside the engine with no public wrapper yet. A
caller asking for one of those gets nothing rather than something wrong.

### The NVDA add-on

    make nvda

That builds both libraries and packs `build/openevv-<version>.nvda-addon`,
which is the engine as a synthesiser for NVDA. `nvda` is the whole of it:
`addon/synthDrivers/openevv.py` is what the reader talks to, and
`_openevv.py` beside it is the library, the thread that owns it and the audio.

It loads the engine into the reader's own process, which is why both libraries
are in the archive and the driver picks one by the bitness of the Python it
finds itself in. The other kind of add-on -- davidacm's IBMTTS driver -- hosts
the engine in a thirty-two bit `rundll32` of its own and talks to it over a
pipe, so it always wants `eci32.dll`; this one wants whichever matches.

Four things in it are decisions rather than detail. Every call into the library
happens on one thread, because the calls that queue work are not written to be
entered twice at once. Prosody inside a sentence is said as a `` `vb ``,
`` `vs `` or `` `vv `` annotation in the text rather than by setting a
parameter, because a parameter applies to everything queued behind it and an
annotation applies where it sits. Samples are held back until an index mark
arrives and then handed over with the mark attached, which puts a mark on the
sample it belongs to instead of a buffer later; the engine flushes a short
buffer of its own just before reporting a mark, which is what makes that line
up exactly.

And nothing ever interrupts the engine, which wants explaining because it is
not what the interface says to do.

Both of the ways the interface offers for cutting an utterance short used to
fault. Answering `eciDataAbort` from the callback died on a null indirect call
in `vinitloc_new`, which is what crashed NVDA the first time the add-on was
asked for silence; calling `eciStop` while synthesis was running died on a null
read of its own. e0cb1f8 fixed that, and the fix is confirmed on three
platforms: `make interrupt` faults without it and passes with it on Linux, and
the same test cross-compiled faults on turn one without it and survives twelve
turns with it both under Wine and on a real Windows machine. For the stop door
the before-and-after is a rate rather than a certainty, because it is a race:
without the guard 12 of 12 runs faulted under Wine and 11 of 12 on real
Windows, and with it 0 of 12 on real Windows -- but still 8 of 12 under Wine,
whose scheduling evidently exposes something the real one does not reach.

That is why the add-on still does not interrupt, and the reason has changed
rather than gone away. Interrupting no longer crashes; it goes mute. From the
second interruption onwards the engine accepts text, answers no error, and
produces nothing at all, for ever. `make interrupt` shows it plainly: turn one
says 19,371 samples and every turn after it says nought. So the interrupt path
is still not something a screen reader can be built on, and would not be even
if the silence were fixed tomorrow -- see below for what the traced evidence
says about leaving the engine alone instead.

So the add-on stops by throwing the samples away: the callback goes on
answering `eciDataProcessed` and simply drops what it is handed, the utterance
finishes synthesising into nothing, and no engine state is touched. Stopping
NVDA's wave player is what actually silences it. What that costs is the
synthesis time of audio nobody hears, and synthesis runs some eighty times
faster than speech, so throwing away eleven seconds of a sentence measures at
about a seventh of a second under Wine. The next utterance is byte-identical to
one spoken with no interruption at all, which is what says the engine was left
alone.

There is a second piece of evidence for that, taken with `DELTA_RULE_TRACE`
set so the interpreter reports an argument area whose depth is not what the
compiled code expected. Ten interruptions on one instance produce 154,253 such
remarks across 520 different rules -- that diagnostic is ordinary background
noise, which is why `tools/delta-check.sh` filters it out -- and *none at all*
on `callInternalSynthesizer`, `callSynthesizeArray`, `sendArrayParameters` or
`stopSynthesizing`. All 1,085 dispatches of the synthesiser rule ended the way
an uninterrupted one does. A real abort put a bad depth on exactly those rules,
so their silence here is the thing worth checking if this ever has to be
revisited. It is not a routine check: tracing that run writes 269 MB.

That differential is also what found the fault. Ten interruptions with those
rules untouched said the damage was not in interrupting but in the stop call
itself, which is what narrowed e0cb1f8 down to one guard.

    make nvda-test

Two checks that need nothing: no Windows, no library, no sound.
`nvda/test/sequence.py` is a speech sequence in and the calls it becomes out,
with NVDA's own modules stood in for, and it catches a misspelled annotation, an
index left inside a stretch of text, and a rate that maps to the wrong number.
`nvda/test/engine.py` goes a layer down and runs the engine layer itself against
a library and a wave player that are stood in for, which is what reaches
`_start`, the ctypes prototypes, the callback and the shutdown.

`nvda/build.py` adds one more before it packs anything: every entry point the
driver names is looked up in both libraries' export tables, and a name that is
not there stops the build. That matters because ctypes resolves a name when it
is used rather than when the library is loaded, so the failure it prevents is
speech quietly not happening inside a screen reader.

    python nvda/test/windows.py [addon directory]

That one runs on Windows, against the real library, with only NVDA stood in for,
and it is where the add-on's faults have actually been found. It wants a Python
on the target machine and the add-on's own directory; with no argument it drives
the installed one under the roaming profile. It speaks the same fixed sentence
the rest of `test` uses and holds it to the same 38,423 samples, checks that a
mark lands where it should, interrupts ten times over on one instance and
requires the utterance after each to come out unchanged, and then shuts down.

Three faults have shipped in this add-on and every one of them was invisible to
the checks that ran on the build host. A renamed function with one call site
left behind, which only `_start` reached. A crash on being asked for silence,
which needed the real engine. And a shutdown that raised while NVDA was
switching synthesiser away, because `Engine` subclassed `threading.Thread` and
kept the engine's instance handle in `self._handle` -- which is the name Python
3.13's own `Thread` keeps its thread handle in, so joining the thread looked up
`join` on an ECI handle. `Engine` holds a thread now rather than being one, and
`engine.py` checks outright that no attribute of it collides with one of
`Thread`'s, because on this host that fault does not even raise: Python 3.14's
`join` returns early for a thread that has already stopped, so only the
structural check sees it.

Installing it is the ordinary way -- NVDA's add-on store, "Install from
external source" -- and it appears as "Eloquence (openevv)" in the synthesiser
list.

One known limit, and it is the engine's rather than the add-on's. The engine
leaks a few megabytes per instance, so a caller that makes and throws away
enough of them runs the arena out and is then answered without complaint and
without audio. `make instances` is what shows it. The add-on makes one instance
and keeps it for as long as the driver lives, so it does not meet this.

## Getting IBM's objects

None of this is needed to build. It is needed for two things: the comparison
tests, which speak every case through IBM's own binary as well as ours, and the
lifters, which is how the language data in `lang` was made and how another
language would be.

Everything comes out of IBM's Embedded ViaVoice 4.3 SDK for Windows, which IBM
still serves from its public download host:

    https://public.dhe.ibm.com/software/pervasive/tools/viavoice/sdk/evvWXP.exe

114,984,719 bytes, dated 30 November 2004, sha256
47182a6b16bd8a5335944a1a03058ce52cba83b03de9da700e97fea68be0c29f. Despite the
.exe it is an ordinary Microsoft cabinet, so it unpacks on any machine, with
`nix shell nixpkgs#p7zip` first if that is how the machine gets its tools:

    7z x evvWXP.exe

That gives `evv4.3/wxp`, with the libraries, the headers, IBM's documentation
and its own sample applications under it. What the tools here read is the
static libraries in `evv4.3/wxp/lib/NT/X86/COMMON`. `ecienus.lib` is US English
and is the one this engine was made from; `eciengb`, `ecidede`, `ecieses`,
`eciesus`, `ecifrfr`, `ecifrca`, `eciitit` and `ecijajp` are the other eight
formant languages, and a `C`-suffixed library beside one of them is the
concatenative build of that language, which uses recorded speech rather than
the synthesiser and is not what any of this reads.

Point `EVV_LIBDIR` at that directory and run the extractors:

    EVV_LIBDIR=/somewhere/evv4.3/wxp/lib/NT/X86/COMMON tools/extract.sh
    EVV_LIBDIR=/somewhere/evv4.3/wxp/lib/NT/X86/COMMON tools/extract-langs.sh

`extract.sh` fills `analysis/enus` with the 207 objects of the English module,
which is what `make -C reference` links and what every lifter reads. It also
writes `analysis/obj` and `analysis/delta-ibm`, which carry the same objects
with IBM's symbols renamed out of the way; that was for standing our code
beside IBM's in one binary, and that harness is retired.

`extract-langs.sh` puts each of the other eight languages in `analysis/<tag>`,
which is for comparison rather than for building. Both extractors want
`llvm-ar`, `llvm-objdump` and the mingw `objcopy`, so both run inside `nix
develop`.

IBM's public host carries more than the SDK: the AIX packages of the same
engine, whose headers are how the interface across four generations was read,
and the Pocket PC runtimes, are under `/software/` beside it. None of it is
needed here.

## Testing

    make probe
    make -C reference
    nix develop --command test/suite.sh

The suite speaks each case through our engine and through IBM's and compares
the samples. It needs Wine, and it needs IBM's objects in `analysis/enus`,
which `tools/extract.sh` puts there out of the SDK above. Building the
reference binary writes it to `build/reference/speak.exe`.

Six categories run by default: plain text, UTF-8, annotations, annotations with
the annotation input type on, real-world text with the parameters read back in a
person's units, and the user dictionary. A seventh, `long`, is paragraphs rather
than sentences and is left out of the default set because under Wine it takes
minutes. Name any of them to run only those: `test/suite.sh plain long`.

`EVV_NATIVE=$PWD/build/probe32 test/suite.sh` runs the same cases through the
thirty-two bit build. Both word sizes have to pass, and so does `RULES=c`.

Without Wine there is no automatic check that the audio is right.
`tools/say.sh` speaks a sentence and plays it, laying the dictionaries down
first, so a change to the language data can be heard.

`tools/delta-check.sh` is the other check. It holds named rules written as C
against the same rules left as bytecode: it speaks each of the seven plain cases
twice, once each way, with the engine saying which rule it is entering and every
call it makes, arguments and all, and the two accounts have to be identical.
That is finer than the audio, because a rule can go wrong in a way that changes
what runs and not what is heard.

Four things about it are deliberate, and the comment at the top of the script
says why at length. One sentence at a time in its own run, because tracing costs
twenty times what the synthesis does and seven of them in one run faults part
way with less audio written; the wave files are compared first for that reason.
The stores are left out, because the interpreter prints the ones it makes and a
rule written as C makes its own. So is the interpreter's remark about the
argument area being a different depth than the compiled code expected, which is
about the compiled code rather than either form of it. The rules are written out
with `EVV_FAITHFUL` set, which leaves a wrapper rule as a call to that rule
rather than writing out the primitive it stood for, since an inlined wrapper is
never entered and so cannot appear in a trace at all. And addresses in the arena
are masked, because a rule written as C takes a smaller frame on purpose and the
two land in different places.

The check deletes the generated C when it finishes, so the next build writes the
ordinary form again rather than finding the faithful one sitting there newer
than everything it is made from.

`test/hash.sh` is the check that needs nothing at all: it speaks one fixed
sentence and holds the samples against a hash in `test/samples.sha256`. That
does not prove the engine right -- only IBM's binary can -- but it proves it
unchanged, which is what catches a careless edit, and it is what the workflow in
`.github` runs on every push. The samples do not depend on the compiler: gcc 15
and clang 21 agree byte for byte, which is what an engine with no floating point
in it should do.

## The sixty-four bit build

The Delta machine keeps addresses in thirty-two bit values, so on a wider host
everything it can point at has to live somewhere such a value can still name.
`src/evv_arena.c` maps a region low in memory and everything the machine holds
comes out of it. That includes the language's own data: the rules name their
constants by address, and the set and action tables hand over an address per
entry, so `src/delta_low.c` copies those stores out of the program at startup
and translates an address into its copy at the few places where one becomes a
value. A pointer from anywhere else says so and stops.

Which is why there is no `-no-pie` and no fixed image base any more. The
program can be loaded wherever the loader fancies, ASLR and all, which is what
makes a shared library possible: a library does not get to choose where it
goes. The Makefile asks the compiler how wide a pointer is and leaves the arena
out altogether when the host is thirty-two bit, where a pointer is a value
already.

`-Werror=int-conversion` and `-Werror=incompatible-pointer-types` are on for
both builds. A narrowed field assigned from a pointer, or the other way about,
was the whole of what went wrong in the sixty-four bit port, so it is an error
rather than a warning nobody reads. The rest of the warnings are off: this is
transcribed code and it is loud.
