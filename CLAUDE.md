# openevv

IBM's Embedded ViaVoice text-to-speech engine, taken out of its 1999 Windows
objects and rebuilt as C. `docs/tree.md` says what every directory is and
`docs/building.md` what every target and variable does; read those rather than
guessing, and keep them true when something moves.

## Prove it before saying it

Nothing works until `test/suite.sh` says so. It speaks each case through our
engine and through IBM's binary under Wine and passes only on identical
samples. Run it from inside `nix develop`, or Wine is not on the path, both
sides produce no file, and every case reports a difference that is not real.

Six builds have to pass, not one: `probe`, `probe32` and `probe.exe`, each with
`RULES=bytecode` and `RULES=c`. Bytecode is the default, so a change to the
decompiler is not being tested at all unless `RULES=c` is what was built. The
Windows one is `EVV_NATIVE=$PWD/build/probe.exe test/suite.sh`, which runs it
under the same Wine as the reference.

`test/hash.sh` is the quick one, and the only check that wants neither Wine nor
IBM's objects. It proves the samples unchanged, not right.

The library has its own two: `test/dll.c` loads `eci.dll` by name and speaks,
and `test/dll.py` does it through ctypes. `make win32` builds the thirty-two bit
library, which is where a wrong signature shows up -- stdcall carries the
argument size in the decorated name on x86, so a declaration that disagrees with
the engine fails to link there and links silently on x86-64.

A pass proves nothing until the new code is shown to be the code that ran.
Break the function on purpose, rebuild, check the audio changes, then put it
back. That has caught two functions that were never reached at all. When a
sabotage changes nothing, ask whether the harness can observe that function at
all before concluding the code is dead.

Rebuild both sides before believing a difference. A stale binary reads as a
bug, and a single difference on a long sentence that does not reproduce is a
timeout.

`make missing` has to keep answering zero. A name that reappears there is a
call that has quietly gone back to IBM's objects.

## What not to tidy

File names in `src` are the names of IBM's objects. A file named for the object
it came from can be checked against that object; renaming them would look
tidier and cost real verification.

`lang/enus` is transcribed data, not code to improve. It is what the engine
sounds like. `tools/delta-sets.py` puts IBM's own dictionary tables back and
loses anything added through `tools/delta-dict.py`, so do not run it to
"regenerate" that file.

The audio is identical to IBM's by design. If it sounds wrong, that is
Eloquence sounding like Eloquence, not a fault to fix.

Never hand the machine an address in the program. A value is thirty-two bits,
and the program may be loaded anywhere -- it has to be, or there could be no
library. Anything the machine can be given the address of is copied into the
arena at startup by `src/delta_low.c` and translated at the crossing; a pointer
in the program that is in none of the registered stores aborts with a message.
If a new table is ever handed over, register it there rather than linking the
program low again.

## Two hard rules

Nothing here may reconfigure, restart or kill PipeWire, and nothing may write
speech-dispatcher configuration. `tools/say.sh` plays as an ordinary client,
which is the only way anything in this project touches sound.

Our own code is MIT, in LICENSE. `lang/enus` is IBM's data and is not ours to
license: never put a licence header on anything in there, and never write
anything that implies the MIT licence reaches it. NOTICE is the file that says
whose is whose, and it is the one to keep true.

## Habits

Everything runs inside `nix develop`: outside it there is no compiler, no
Python and no Wine. That Wine is wow64 and one prefix serves both kinds of PE;
a prefix made by an older 32-bit-only Wine is refused outright, and the answer
is to delete `.wine` and let it be made again.

`lang/enus/delta_rules_c.c` is thirteen megabytes of generated C and is not in
the tree; `make rules` writes it. Seven minutes of compiler, so a change to the
decompiler costs a quarter of an hour before a single case runs.

Releases are tags: pushing `vN` makes the workflow build the archives and cut
the release. Nothing about them is manual.
