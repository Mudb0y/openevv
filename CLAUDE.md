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

Four builds have to pass, not one: `probe` and `probe32`, each with
`RULES=bytecode` and `RULES=c`. Bytecode is the default, so a change to the
decompiler is not being tested at all unless `RULES=c` is what was built.

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

## Two hard rules

Nothing here may reconfigure, restart or kill PipeWire, and nothing may write
speech-dispatcher configuration. `tools/say.sh` plays as an ordinary client,
which is the only way anything in this project touches sound.

There is no licence yet and that decision is Stas's. Do not add a LICENSE file
or licence headers, and do not push anything anywhere.

## Habits

Everything runs inside `nix develop`: outside it there is no compiler, no
Python and no Wine. `lang/enus/delta_rules_c.c` is thirteen megabytes of
generated C and is not in the tree; `make rules` writes it.
