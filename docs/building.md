# Building openevv

## What you need

A C compiler and Python 3. The language data is in the tree, so there is no IBM SDK to find and nothing is downloaded; Python is wanted because an ordinary build now writes the rules out as C first, which is what `RULES` below is about. `make RULES=bytecode` needs the C compiler alone.

Two more things are wanted only for particular jobs. A thirty-two bit compiler builds the thirty-two bit engine. Wine and IBM's own objects run the comparison tests, which is the only automatic check that the audio is right.

On this machine all of those come from the flake, and `nix develop` puts them on the path.

## Building

    make

That builds `build/libevv.a` and `build/evv`, which speaks. From nothing, that is about a quarter of an hour: seven minutes for Python to write the rules out as C and about as long again to compile the thirteen megabytes of it. Once that file exists it is not written again unless the decompiler or the bytecode changes.

    make RULES=bytecode

That is the small, quick build -- half a minute on one core, under twenty seconds with `make -j8`, and a C compiler is all it wants. It speaks the same samples and it is the one to use while working on anything but the rules. What it costs is speed, which the next section puts numbers to.

    make probe

That builds `build/probe` instead: the same engine behind the front the tests drive. It prints what the engine answered at every step so those answers can be set against IBM's, which is why it is not the thing to run by hand.

    make evv32
    make probe32

The same two, thirty-two bit. That build is a check rather than a target: a difference between the word sizes is a layout mistake, and this is what makes one show up early. It needs a thirty-two bit compiler, which is `CC32`.

On a Nix machine `nix build` makes the same binary at `result/bin/evv`, and `nix run . -- -o hello.wav "text"` runs it without installing anything. `nix develop` is the shell the rest of this assumes: the thirty-two bit compiler, Wine and Python on the path.

`make install` copies the binary to `/usr/local/bin/evv`, or wherever `PREFIX` and `DESTDIR` say. There is nothing else to install: it reads no file of its own at run time and wants no library but the C one, libm and pthreads. `make clean` takes the objects and the binaries away and leaves the generated C alone.

## The variables

`CC` is the compiler for this machine, `cc` by default. `CC32` is the thirty-two bit one, which on this machine is the cross compiler the flake provides, `i686-unknown-linux-gnu-gcc`, and elsewhere is usually the host compiler with a flag: `make evv32 CC32="gcc -m32"`. `NM` is used by `make missing`. `OPT` is the optimisation level, `-O2`. `CFLAGS` is added to both builds after everything else, so it can override.

`RULES` chooses which form of the language's rules gets linked, and is explained next.

## The rules as text

`lang/enus/rules` holds all 3,377 rules as text, one file to an object, written
by `tools/delta-notation.py`. This is the form to read a rule in, and it is
meant to become the form to *change* one in.

    rule eng_ph_Z_dur from es_cdur.obj
    shape frame 196 argbase 8 params 1
    label L0 was _eng_ph_Z_dur
      alu andl imm 0 slot -4
      push slotaddr -104
      call setjmp3 arity 2 depth 2
      cmp testl reg r0 reg r0
      load movl state 0 into r6
      branch jne to L1

One operation to a line, the verb first, and an operand is one or two words --
so a line can be read straight through with nothing to keep track of. Nothing
is carried by indentation and nothing needs punctuation counted. Registers are
the machine's eight, `r0` to `r7`, with `w`, `b` or `h` for how much of one is
meant. Blocks are numbered; `was ...` on the label line is what the block was
called in IBM's object, which is only useful while rules are still being lifted
and is ignored when the text is read.

It is one to one with what the machine does, which is the point: it holds
registers, the argument stack and the backtracking as they are rather than
tidying them into loops and conditionals. The readable C that
`delta-decompile.py` writes is the other form, for reading rather than for
round-tripping, and inverting that exactly would be hard.

    make notation

writes the text out of IBM's objects again, and

    make notation-check

holds what is in the tree against those objects rule by rule: each is emitted
twice, once from the text and once from a fresh lift, and the bytecode has to
match. That is what says which rules have been changed on purpose -- an unedited
rule matches and an edited one is named, which is what somebody changing a rule
needs to be told.

    make notation-prove

is the stronger check and the one to believe. It emits every rule out of the
text into one stream and holds that against `delta_rule_code` as it stands in
`lang/enus/delta_rules_enus.c` -- the bytecode the engine actually runs. The
pools the rules draw on, the constants and strings and entry points and tag
maps, are shared across the whole language and numbered in the order the rules
are taken, so reproducing the stream byte for byte says the text carries every
rule, in order, with nothing added and nothing left out. A rule-by-rule
comparison cannot say that. All 1,496,807 bytes match.

Both want IBM's objects, so they are in the same class as the suite:
obtainable, and not needed to build.

    make notation-regenerate

is the one that says the text is the source rather than a second copy. It reads
`lang/enus/rules`, opens no object at all, and writes what the engine compiles
-- `delta_rules_enus.c` and `delta_rules.h` -- into a directory of its own, then
holds both against the files in the tree. Both match byte for byte: 4,932,041
bytes and 168,178.

What made that possible was one small table. A rule names a constant by a
symbol; the bytes behind it are a whole data section of the object it was
compiled into, and what the rule gets is an offset into that section. The bytes
were already in the tree, in `delta_consts_enus.c`. The mapping -- which store
and how far in -- was not, and it was the last thing the emitter needed the
objects for. It is now `lang/enus/rules/symbols`: 75 stores and 6,718
addresses, written by `make notation-symbols`.

So the rules can be rebuilt from text a person can read and change, and IBM's
objects are wanted for the comparison suite and for nothing else.

## What a rule stands for

`lang/enus/rules/wrappers.up` is the beginning of the upper layer: a rule as
what it means rather than as what the machine does to arrive at it.

    wrapper ZZbspush_ca__12 takes 1
      bspush_ca 12
    wrapper ZZget_parm_ptr2 takes 5
      get_parm arg 1 arg 2 -6
      get_parm arg 3 arg 4 -6
    wrapper ZZlprp_load__insert_2pt_i_7_2_ZZstring2 takes 3 answering truth
      lpta_rpta_loadp arg 1 arg 2
      insert_2pt_i 7 2 ZZstring2 0

Every one takes the machine's state as its first argument, so that is not
written; `arg n` is the wrapper's own nth, `as byte` or `as half` widens one
before it is handed over, and `answering truth` is the three operations that
turn whatever came back into nought or one. The name of a wrapper already
spells its arguments -- `ZZtest_string_s_2_1_ZZstring480` -- so this only says
out loud what the name is spelling.

    make upper
    make upper-prove

`upper` writes it and `upper-prove` checks it: each is compiled back down and
the bytecode has to match the lower form byte for byte. 1,954 of the 2,335
wrappers are there and all 1,954 match.

**It writes only what it can reproduce exactly.** 381 wrappers are left in the
lower form, of which 371 do not fit the shape at all and ten do fit but widen
an argument, and the original compiler put that load where it suited it rather
than always in one place. Where this cannot reproduce the placement, an upper
form would be a description that is not the rule, so the rule stays as it is.
That is the whole discipline of the thing: byte-identity is not a nicety here,
it is what makes a re-description of an existing rule worth having.

What is deliberately not attempted is the 1,042 real rules. Those are programs:
a median of 28 calls over 15 blocks, 1,058 distinct shapes between them, and
only 12% fitting even a loose template of tests and ordinary actions. Only 4%
merely test and assign. For those the readable form is the C
`delta-decompile.py` writes, and the naming it already does -- which primitive a
wrapper stands for, which variable a reach touches, which alternative an arm is
-- is the win. A declarative form would not fit them and pretending otherwise
would cost the exactness that makes any of this checkable.

The other use of an upper layer is the one that has nothing to be identical to:
writing rules that do not exist yet, which is what Polish needs. There the check
is the suite and an ear, not a byte comparison, so the constraint above does not
bind.

## The rules, twice

The language's rules exist in the tree as bytecode, and the engine has an interpreter for them. They also exist as C: `tools/delta-decompile.py` writes all 3,377 of them out of that same bytecode into `lang/enus/delta_rules_c_enus.c`, and the interpreter prefers a rule written as C wherever it finds one. It writes beside whichever language it was pointed at, so `make LANG=lang/dede rules` writes German into `lang/dede`.

Both speak the same samples. That is not a hope: `test/suite.sh` holds each form against IBM's binary over all 81 cases, and the two forms are set against each other call by call by `tools/delta-check.sh`. So which one is linked is a trade of build time and size against speed, and nothing else.

C is the default, because the speed is the part a person waiting for speech feels. Measured on one machine, the same long sentence, bytecode against C: the whole utterance synthesises in 138 ms against 63; the wait before the first samples of an utterance is 38 ms against 12; and interrupting an utterance and asking for another costs 124 ms against 39. That last one matters most and is the least obvious: the engine cannot abandon an utterance it has been told to stop -- see the interrupting section of `docs/status.md` for why not -- so what a cancel costs is whatever is left of the work, and compiled rules do that leftover work in a third of the time.

What it costs is the build. The C is thirteen megabytes in one file: seven minutes of Python to write and about as long to compile, where the bytecode build wants half a minute and no Python at all. The binaries are some four times the size -- `build/probe` is 15.6 MB against 3.7 -- because that is what a machine's worth of lifted code looks like written out as C, with nothing kept in a register because a backtrack may land in the middle of any of it.

    make RULES=bytecode

is therefore the one to build while working on anything but the rules, and

    make RULES=c

says the default out loud, which is worth doing in a script. `make rules` writes the file without building anything. It is not kept in the tree, because every change to the decompiler rewrites the whole of it.

## Languages

`LANGS` says which languages go in. One:

    make LANG=lang/dede probe

or several, in one binary:

    make LANGS="lang/enus lang/dede" probe

`LANG` is the name for one of them and is what everything already says; `LANGS` takes a list, and the first one named is what a caller gets when it asks for no language in particular.

A build of English alone keeps the plain names -- `build/probe`, `build/libevv.a`. Anything else carries what it has in it: `build/probe-dede`, `build/probe-enus-dede`, and the archives to match. That is not tidiness. An archive is built out of one set of objects, and those already sit in directories of their own, so building German and then English again would leave an archive newer than every English object: make would not rebuild it, and the English probe would be linked against the German engine.

How several fit in one program is in `src/delta_lang.h`. The short of it: every module names its own tables after itself -- `enus_vstmtbl`, `dede_vstmtbl` -- because IBM gave them the same names in every language, and the engine reaches whichever is in force rather than linking to one by name. A machine remembers the language it was made for, the engine keeps one engine per language as the original does, and `eciGetAvailableLanguages` answers with all of them.

## Testing another language

The oracle has to be built from that language's own objects, and goes somewhere of its own:

    make -C reference TAG=dede BUILD=../build/reference-dede

Both have to be given. The default output directory is the English one, because that is where `test/compare.sh` looks when nothing says otherwise.

Then `EVV_LANG` runs the suite against it:

    EVV_LANG=dede test/suite.sh

which picks `build/probe-dede`, `build/reference-dede` and the cases named for that language -- `test/cases/plain-dede.txt` and the rest. Naming the language is what keeps an English engine from being held against a German oracle, which differs on every case and says nothing.

A binary with several languages in it is driven the same way, with `EVV_NATIVE` naming it:

    EVV_NATIVE=$PWD/build/probe-enus-dede test/suite.sh
    EVV_LANG=dede EVV_NATIVE=$PWD/build/probe-enus-dede test/suite.sh

`compare.sh` sets `EVV_LANGUAGE` from the language it was asked for, and the probe asks the engine for that one rather than whichever is first. Those are IBM's own numbers, the ones its ini names each language section for; a language added to the tree adds a line to that table.

German passes the cases there are for it, both on its own and beside English in one binary. `docs/status.md` says in which configurations, and what is not covered.

## Running

    ./build/evv -o hello.wav "Hello from Eloquence."
    ./build/evv -f speech.txt -o speech.wav
    ./build/evv "Hello from Eloquence." | aplay -q -
    echo "Hello from Eloquence." | ./build/evv | pw-play -

With no `-o` it writes the wave to standard output, unless that is a terminal, in which case it says so rather than filling the terminal with samples. With no text it reads standard input.

`-v` picks one of the eight voices, `-s` the speed, `-p` the pitch and `-V` the volume. Those numbers are the engine's own; `-r` makes them a person's instead, so speed is words per minute and pitch is hertz. `-l` prints what each voice is set to, in whichever units are in force.

## Windows

    make win

That cross-compiles two binaries with mingw: `build/evvspeak.exe`, the speak window, and `build/evv.exe`, the same console driver as on this machine. Both are static, so each is one file that wants nothing installed, and both are sixty-four bit. `make win-probe` builds the test driver as `build/probe.exe`, which `EVV_NATIVE=$PWD/build/probe.exe test/suite.sh` will run against IBM's binary case for case, under the same Wine.

The speak window is the only front end anywhere in this tree that plays what it makes. It types into a multiline box, picks one of the eight voices, picks the language when the build has more than one, takes the rate in words a minute and the pitch in hertz, saves a wave file if asked, and plays through waveOut, which every Windows since 1995 has. Control and Enter speaks, Escape stops, and Escape again closes. Everything in it is a control Windows ships, so a screen reader reads it without being told anything.

The language list is what `eciGetAvailableLanguages` answers, under the names the language modules give themselves, and choosing one sets it on the instance already there rather than building another. With one language in the build the list holds that one and is left disabled, so the window is the same shape either way; it will not change language while something is being said, because the engine is spoken to from one thread and the worker is holding it.

`evvspeak.exe /say "some text"` speaks at once and is how the sound gets tested without a mouse. `/lang` in front of it picks the language to start in -- `evvspeak.exe /lang dede /say "Hallo."` -- and takes the tag, the name or the number: `dede`, `German` and `0x40000` all mean the same one. A language the build does not have is ignored and the window opens in whichever the engine picked.

Two things about the Windows build are worth knowing. `src/port_win32.c` stands in for `src/port_posix.c`, which is the whole of the platform layer. And the arena takes its region from VirtualAlloc at the same low addresses mmap gets on Linux. The image itself is an ordinary PE at whatever base mingw chooses, with ASLR on: nothing needs it low any more.

### The library

`make win` also builds `build/eci.dll`, which is the same engine with the names IBM published on the outside: `eciNew`, `eciAddText`, `eciSynthesize` and the rest, fifty-two of them, exported under those spellings from a sixty-four bit library that wants nothing but the system's own DLLs. `win/eci_api.c` is the whole of it, one wrapper per name.

The point of it is that a program written against IBM's `eci.dll` can load ours instead. That program is usually a screen reader add-on: NVDA is a sixty-four bit process now, and the add-on most people have loads the library with ctypes' `windll`, calls seventeen of these, and hands in a callback made with `WINFUNCTYPE`. `build/eci.ini` is copied out beside the library because add-ons look for one and rewrite a path inside it; nothing here reads it, since the engine carries its own settings in the image.

None of the calling convention trouble that a thirty-two bit build would bring applies: on x86-64 `__stdcall` and `__cdecl` are the same thing, a stdcall name carries no `@N` to strip, and a stdcall callback is callable as anything.

`make win32` builds the same library thirty-two bit, as `build/eci32.dll`, and that one is not optional. The most used screen reader driver -- davidacm/NVDA-IBMTTS-Driver -- does not load the engine into the reader's own process at all: it launches `rundll32.exe` from `SysWOW64` and hosts the engine there, talking to it over a named pipe, so the library it loads is thirty-two bit whatever the reader is. The other kind of add-on loads it in process and therefore wants the bitness the reader has. Both are shipped, in folders that say which is which, because dropping the wrong one in is the mistake to design out.

Thirty-two bit is the easier build of the two: a pointer is a value there, so there is no arena at all. It does want `--kill-at`, because stdcall decorates a name with `@N` on x86 and a caller asking by name wants it plain, and it is where a wrong signature shows up -- the argument size is part of the decorated name, so a declaration that does not match the engine fails to link. Three of mine did not, and the sixty-four bit linker had accepted them silently.

`win/eci.rc` gives both libraries a version resource, which is not decoration either. That driver reads `ProductName` out of it to decide which engine it is talking to: `IBMECI` turns on IBMTTS-specific text fixes, a different pause style and a 22 kHz sample rate. This is the Eloquence engine at 11 kHz, so it says `openevv` and gets treated accordingly. NVDA's own reader also raises rather than loading a file with no version information at all, so without the resource that driver would refuse us before it ever called anything.

One caveat about mixing toolchains, learned by tripping over it. The libraries in a release are built by one mingw and tested with harnesses built by the same one. A caller built by a *different* mingw, with a different thread runtime -- nixpkgs' uses mcfgthreads where Debian's uses winpthreads -- can fault on the crossing, and one direction of that pairing does. It does not matter for the callers that exist: Python's ctypes and a screen reader's host DLL are MSVC built, with no mingw runtime in them at all, and CI checks both of those crossings on Windows itself. But do not conclude from a fault in a hand-mixed pair that the shipped library is broken; check a matched pair first.

Two ways to check it, and both are worth having. `make win-dlltest` builds `build/dlltest.exe`, which links against nothing, loads `eci.dll` by name, asks for each entry point by name and speaks; `test/hash.sh build/dlltest.exe` then holds what comes out of the library against what comes out of everything else. `test/dll.py` does the same through ctypes, which is a different question -- ctypes has its own ideas about handles, and a handle is sixty-four bits -- and CI runs it on Windows itself. `make win32` builds `build/dlltest32.exe` for the thirty-two bit library; that one is checked from C, since a sixty-four bit Python cannot load a thirty-two bit library at all. Both harnesses also read the version resource and fail if it is missing.

What the library does not export: the filter interface, which the engine does not implement, and `eciGeneratePhonemes` and the dictionary find, lookup and update calls, which exist inside the engine with no public wrapper yet. A caller asking for one of those gets nothing rather than something wrong.

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

It is not in a release, on purpose, and that is not an oversight to be tidied
up. It is not stable enough to hand anyone yet: three faults have already
shipped from here to one VM, and none of them was visible to the checks that
run on the build host. The workflow still builds it and uploads it as a build
artifact, which is how a version to try is got, and the release job throws it
away. Putting it back in a release is a decision to be made when it has been
lived with, not when the checks pass.

One known limit, and it is the engine's rather than the add-on's. The engine
leaks a few megabytes per instance, so a caller that makes and throws away
enough of them runs the arena out and is then answered without complaint and
without audio. `make instances` is what shows it. The add-on makes one instance
and keeps it for as long as the driver lives, so it does not meet this.

## Getting IBM's objects

None of this is needed to build. It is needed for two things: the comparison tests, which speak every case through IBM's own binary as well as ours, and the lifters, which is how the language data in `lang` was made and how another language would be.

Everything comes out of IBM's Embedded ViaVoice 4.3 SDK for Windows, which IBM still serves from its public download host:

    https://public.dhe.ibm.com/software/pervasive/tools/viavoice/sdk/evvWXP.exe

114,984,719 bytes, dated 30 November 2004, sha256 47182a6b16bd8a5335944a1a03058ce52cba83b03de9da700e97fea68be0c29f. Despite the .exe it is an ordinary Microsoft cabinet, so it unpacks on any machine, with `nix shell nixpkgs#p7zip` first if that is how the machine gets its tools:

    7z x evvWXP.exe

That gives `evv4.3/wxp`, with the libraries, the headers, IBM's documentation and its own sample applications under it. What the tools here read is the static libraries in `evv4.3/wxp/lib/NT/X86/COMMON`. `ecienus.lib` is US English and is the one this engine was made from; `eciengb`, `ecidede`, `ecieses`, `eciesus`, `ecifrfr`, `ecifrca`, `eciitit` and `ecijajp` are the other eight formant languages, and a `C`-suffixed library beside one of them is the concatenative build of that language, which uses recorded speech rather than the synthesiser and is not what any of this reads.

Point `EVV_LIBDIR` at that directory and run the extractors:

    EVV_LIBDIR=/somewhere/evv4.3/wxp/lib/NT/X86/COMMON tools/extract.sh
    EVV_LIBDIR=/somewhere/evv4.3/wxp/lib/NT/X86/COMMON tools/extract-langs.sh

`extract.sh` fills `analysis/enus` with the 207 objects of the English module, which is what `make -C reference` links and what every lifter reads. It also writes `analysis/obj` and `analysis/delta-ibm`, which carry the same objects with IBM's symbols renamed out of the way; that was for standing our code beside IBM's in one binary, and that harness is retired.

`extract-langs.sh` puts each of the other eight languages in `analysis/<tag>`, which is for comparison rather than for building. Both extractors want `llvm-ar`, `llvm-objdump` and the mingw `objcopy`, so both run inside `nix develop`.

IBM's public host carries more than the SDK: the AIX packages of the same engine, whose headers are how the interface across four generations was read, and the Pocket PC runtimes, are under `/software/` beside it. None of it is needed here.

Mainline ViaVoice is a different product line and not a wider language set.
Embedded ViaVoice is the small-footprint, fixed-point build and comes as static
object libraries, which is the only reason any of this was possible. The desktop
engine is mainline and floating point, and ships runtime data files rather than
objects -- so its seventeen languages, Danish and Finnish and Korean among them,
are not waiting to be lifted. There is nothing compiled to read, and the
synthesiser underneath them is not the one in `klatt_*.c`. The nine in the EVV
4.3 SDK are the reachable set.

## Testing

    make probe
    make -C reference
    nix develop --command test/suite.sh

The suite speaks each case through our engine and through IBM's and compares the samples. It needs Wine, and it needs IBM's objects in `analysis/enus`, which `tools/extract.sh` puts there out of the SDK above. Building the reference binary writes it to `build/reference/speak.exe`.

Six categories run by default: plain text, UTF-8, annotations, annotations with the annotation input type on, real-world text with the parameters read back in a person's units, and the user dictionary. A seventh, `long`, is paragraphs rather than sentences and is left out of the default set because under Wine it takes minutes. Name any of them to run only those: `test/suite.sh plain long`.

`EVV_NATIVE=$PWD/build/probe32 test/suite.sh` runs the same cases through the thirty-two bit build. Both word sizes have to pass, and so does `RULES=c`.

Without Wine there is no automatic check that the audio is right. `tools/say.sh` speaks a sentence and plays it, laying the dictionaries down first, so a change to the language data can be heard.

`tools/delta-check.sh` is the other check. It holds named rules written as C against the same rules left as bytecode: it speaks each of the seven plain cases twice, once each way, with the engine saying which rule it is entering and every call it makes, arguments and all, and the two accounts have to be identical. That is finer than the audio, because a rule can go wrong in a way that changes what runs and not what is heard.

Four things about it are deliberate, and the comment at the top of the script says why at length. One sentence at a time in its own run, because tracing costs twenty times what the synthesis does and seven of them in one run faults part way with less audio written; the wave files are compared first for that reason. The stores are left out, because the interpreter prints the ones it makes and a rule written as C makes its own. So is the interpreter's remark about the argument area being a different depth than the compiled code expected, which is about the compiled code rather than either form of it. The rules are written out with `EVV_FAITHFUL` set, which leaves a wrapper rule as a call to that rule rather than writing out the primitive it stood for, since an inlined wrapper is never entered and so cannot appear in a trace at all. And addresses in the arena are masked, because a rule written as C takes a smaller frame on purpose and the two land in different places.

The check deletes the generated C when it finishes, so the next build writes the ordinary form again rather than finding the faithful one sitting there newer than everything it is made from.

### The taps

The suite says whether two engines agree and nothing about where they stopped agreeing. The taps say where.

    make -C reference tap

builds `build/reference/speak-tap.exe`, which is the reference binary with a wrapper standing in front of four of IBM's own functions. Each writes down what it was handed and calls the real one, so the audio is unchanged and a tapped run can be checked against an untapped one before its dump is believed. Nothing is written unless the matching variable names a file:

    EVV_TAP_SYNTH=ref.synth    the cells and frame overrides a rule hands the synthesiser
    EVV_TAP_KLATT=ref.klatt    the sixty-two parameters of every frame
    EVV_TAP_STREAM=ref.stream  every point a rule writes into a stream array

The head of `reference/tap.c` says which four functions and why it can only be those: a rename reaches the object's own relocations too, so only a function called from another object can be stood in front of at all.

The other side of each tap is a few lines in our own C at the same function, printing the same line to the same variable. They are not kept in the tree -- a diagnostic that is always compiled in is a diagnostic nobody checks -- so they get written for an afternoon and taken out again. The stream one goes in `src/eci_stmarray.c` and looks like this:

    fprintf(f, "PT stream=%d val=%d t=%d\n", stream[1], when[1], value[1]);
    fprintf(f, "SS stream=%d val=%d t1=%d t2=%d\n",
            stream[1], when[1], first[1], second[1]);

A cell holds its value at offset two, which is what the `[1]` is. The two argument names are the other way round from what they carry -- what the original calls the moment is the value and what it calls the value is the moment -- so a `val` above is a value and `t1` and `t2` are the two ends of the segment it covers.

Then speak one short word through each and hold the dumps against each other. That is how the German /r/ was found: of the hundred and thirty-one points the language writes into its streams for `tra`, a hundred and thirty were ours to the byte and one was not, and one wrong line in one rule is a very different thing to look for than a wave file that differs.

`test/langs.py` is the check for a build with more than one language in it. It makes an instance in each, before any of them speaks, and then holds what each says against what the same library says speaking that language on its own. Byte for byte: a language that sounds nearly right beside another one is exactly what it is looking for. Point it at the library -- `python3 test/langs.py build/eci.dll` -- and a build with one language in it says so and passes.

What it is proving is that nothing in the engine has quietly stayed global. Two things keep a language in force, and either alone is enough: every method of the engine wrapper sets it from its own machine, and `delta_run_rule` sets it again from the machine it was handed. Breaking one changes nothing, which is what redundancy means; breaking both makes a German machine read English tables and the process falls over, which is how the path is known to be live.

`test/hash.sh` is the check that needs nothing at all: it speaks one fixed sentence and holds the samples against a hash in `test/samples.sha256`. That does not prove the engine right -- only IBM's binary can -- but it proves it unchanged, which is what catches a careless edit, and it is what the workflow in `.github` runs on every push. The samples do not depend on the compiler: gcc 15 and clang 21 agree byte for byte, which is what an engine with no floating point in it should do.

## The sixty-four bit build

The Delta machine keeps addresses in thirty-two bit values, so on a wider host everything it can point at has to live somewhere such a value can still name. `src/evv_arena.c` maps a region low in memory and everything the machine holds comes out of it. That includes the language's own data: the rules name their constants by address, and the set and action tables hand over an address per entry, so `src/delta_low.c` copies those stores out of the program at startup and translates an address into its copy at the few places where one becomes a value. A pointer from anywhere else says so and stops.

Which is why there is no `-no-pie` and no fixed image base any more. The program can be loaded wherever the loader fancies, ASLR and all, which is what makes a shared library possible: a library does not get to choose where it goes. The Makefile asks the compiler how wide a pointer is and leaves the arena out altogether when the host is thirty-two bit, where a pointer is a value already.

`-Werror=int-conversion` and `-Werror=incompatible-pointer-types` are on for both builds. A narrowed field assigned from a pointer, or the other way about, was the whole of what went wrong in the sixty-four bit port, so it is an error rather than a warning nobody reads. The rest of the warnings are off: this is transcribed code and it is loud.
