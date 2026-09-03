# openevv

IBM's Embedded ViaVoice text-to-speech engine, taken out of its 1999 Windows objects and rebuilt as C. `docs/tree.md` says what every directory is, `docs/building.md` what every target and variable does, `docs/rules.md` the rules, `docs/language.md` the rest of a language module, `docs/testing.md` what proves any of it, and `docs/windows.md` the Windows side; read those rather than guessing, and keep them true when something moves.

## Prove it before saying it

Nothing works until `test/matrix.sh` says so. It speaks every case of every language through the engine and holds each against what this engine has said before: two hashes a case, the samples and the answers the interface gave, 881 cases over the nine languages. It wants neither Wine nor IBM's objects. `make matrix` runs it.

**A case that moves is not a failure, it is a question.** The engine is being changed on purpose now and byte-identity with IBM is a thing that was proved rather than a thing that is maintained. What the gate is for is telling a change from an accident: if a case moved, say in the commit which and why, then `test/matrix.sh record` writes the new answers down. If a case you did not touch moved, that is the accident it exists to catch.

Six builds have to pass, not one: `probe`, `probe32` and `probe.exe`, each with `RULES=bytecode` and `RULES=c`. C is the default as of 22 August 2026, so it is the interpreter that goes untested unless `RULES=bytecode` is what was built -- the opposite of the trap this warned about before. `EVV_MATRIX_NATIVE` names which binary to drive.

`test/suite.sh` is the oracle, not the gate. It speaks each case through our engine and through IBM's binary under Wine and passes only on identical samples, and it is what to reach for when the question is what the original did rather than whether anything moved -- a path no case has walked before, a machine primitive no rule has ever called, a piece of IBM's code just transcribed. Every number in `test/samples` was blessed by it: the whole suite was run for all eight lifted languages immediately before those were recorded, so the baselines are values IBM's binary had just agreed with. Run it from inside `nix develop`, or Wine is not on the path, both sides produce no file, and every case reports a difference that is not real. The Windows one is `EVV_NATIVE=$PWD/build/probe.exe test/suite.sh`.

Polish has no oracle and never will, since IBM never shipped it. Its baselines are what this engine does rather than what any original did, and `test/matrix.sh` is the only thing besides an ear that can tell a change to Polish from an accident. `test/cases/*-plpl.txt` are its cases and they are ours, written for Polish orthography rather than translated.

`test/hash.sh` is the quick one, one English sentence. It is the two-second smoke test, not the gate.

`make crashers` is another that wants neither Wine nor IBM's objects. It speaks the text in `test/cases/crashers.txt`, which is text IBM's engine dies on and ours used to die on with it, and it fails if the engine faulted on one or would not finish. It is not part of the differential suite and cannot be: the reference produces no audio for any of those strings, so there is nothing to compare. `tools/engine/crashers.py` is what found them and is how to find more.

The library has its own two: `test/lib/dll.c` loads `eci.dll` by name and speaks, and `test/lib/dll.py` does it through ctypes. `make win32` builds the thirty-two bit library, which is where a wrong signature shows up -- stdcall carries the argument size in the decorated name on x86, so a declaration that disagrees with the engine fails to link there and links silently on x86-64.

A pass proves nothing until the new code is shown to be the code that ran. Break the function on purpose, rebuild, check the audio changes, then put it back. That has caught two functions that were never reached at all. When a sabotage changes nothing, ask whether the harness can observe that function at all before concluding the code is dead.

Rebuild both sides before believing a difference. A stale binary reads as a bug, and a single difference on a long sentence that does not reproduce is a timeout.

`make missing` has to keep answering zero. A name that reappears there is a call that has quietly gone back to IBM's objects.

A rule written in the upper form is proved by `make upper-check`, and byte identity is not the standard there and cannot be: matching IBM's bytes would mean making its compiler's own register choices. What is required is that the engine cannot tell the difference -- every rule entered and every call made with its arguments, over both builds, and the audio besides. Two things that check taught, both worth keeping. A rule whose whole effect is to write a variable makes no call that shows it, so the audio is the only thing that catches a wrong value there. And the sentences have to reach the rule: the seven plain ones never take one of `has_lex_prefix`'s two alternatives, so its action number could be changed to anything and every case still passed, which is what `test/cases/upper.txt` is for.

German has its own cases and its own oracle: `EVV_LANG=dede test/suite.sh`, against a reference built from German objects. It matches over all 80 of them, on its own and in one binary with English -- `make LANGS="lang/enus lang/dede"`, then the suite twice with `EVV_NATIVE` naming that binary. `docs/status.md` says in which configurations, and what has not been built from `lang/dede` at all.

A build with two languages in it proves something a build with one cannot: that nothing has quietly stayed global. `test/lib/langs.py build/eci.dll` is the cheap form of that -- every language spoken from one process, each held against what it says alone -- and it needs neither Wine nor IBM's objects. If a change makes only one language's suite pass, the language in force is being read from the wrong place.

A change made for one language is not finished until every language has been run again, which is why `test/matrix.sh` walks all nine rather than the one being worked on. They share every line of `src` and every tool in `tools`: the dictionary table German crashed on had been wrong on sixty-four bits all along, and the lift that German needed changed two places in the English bytecode as well.

A marker case that differs once and not again is IBM's binary being unsteady, not a change in ours. Run it again, and if in doubt hash both sides over several runs -- it is the reference that varies.

The engine's second utterance is not its first, and that is faithful rather than random. Saying the same sentence twice on one instance gives 38,423 samples both times and 30,495 of them differ: the machine's state has moved on. It is entirely deterministic -- three processes give the same first utterance and the same second one, to the hash -- and IBM's own engine does it too, to the same 30,495 samples, with ours matching its second utterance byte for byte. So bytes are comparable, including a second utterance, as long as both sides have spoken the same history. What is not comparable is a second utterance against a first. `probe` and the reference both take a `t` in their mode argument, which says the same text twice and writes the second beside the first; that is what settled this, and it is the `second` category of both `test/suite.sh` and `test/matrix.sh` now, so the state carried between utterances is recorded rather than merely known.

## What not to tidy

File names in `src` are the names of IBM's objects. A file named for the object it came from can be checked against that object; renaming them would look tidier and cost real verification.

`lang/plpl` says Polish and is Italian. It was copied from `lang/itit`'s text forms and renamed, so every rule and every table in it is IBM's Italian until something written here has replaced it -- which is what `make EVVLANG=lang/plpl census` counts, rule by rule. Two things follow. NOTICE governs it exactly as it governs `lang/itit`, and a change made there is only Polish when the census says so; a module that sounds plausible because it is still Italian is the failure that check exists to prevent. Polish is family seventeen, and the family is not free: three tables are indexed by it and hold eighteen, IBM used six, and four more are families its own code says have a romanizer, so an instance of one of those is refused when the romanizer is absent.

`lang/enus` is transcribed data, not code to improve. It is what the engine sounds like. `tools/module/sets.py` puts IBM's own dictionary tables back and loses anything added through `tools/module/dict.py`, so do not run it to "regenerate" that file.

The audio is identical to IBM's by design. If it sounds wrong, that is Eloquence sounding like Eloquence, not a fault to fix.

Never hand the machine an address in the program. A value is thirty-two bits, and the program may be loaded anywhere -- it has to be, or there could be no library. Anything the machine can be given the address of is copied into the arena at startup by `src/delta/delta_low.c` and translated at the crossing; a pointer in the program that is in none of the registered stores aborts with a message. If a new table is ever handed over, register it there rather than linking the program low again.

## Two hard rules

Nothing here may reconfigure, restart or kill PipeWire, and nothing may write speech-dispatcher configuration. `tools/measure/say.sh` plays as an ordinary client, which is the only way anything in this project touches sound.

Our own code is MIT, in LICENSE. `lang/enus` is IBM's data and is not ours to license: never put a licence header on anything in there, and never write anything that implies the MIT licence reaches it. NOTICE is the file that says whose is whose, and it is the one to keep true.

## Habits

Everything runs inside `nix develop`: outside it there is no compiler, no Python and no Wine. That Wine is wow64 and one prefix serves both kinds of PE; a prefix made by an older 32-bit-only Wine is refused outright, and the answer is to delete `.wine` and let it be made again.

Read IBM's objects with `llvm-objdump -d -r --no-show-raw-insn` and never with binutils `objdump -d`. Each function is its own COMDAT `.text` section and MSVC gave local labels the same names in different sections, so binutils takes a recurring label for a function boundary, resynchronises the instruction stream at that byte, and prints plausible nonsense from there to the end of the section -- `into` and `add %al,(%eax)` where the code is really a compare and a jump. Nothing warns. If a function's control flow stops making sense in the middle, suspect the disassembler before suspecting IBM. The lifters in `tools` go on using binutils `objdump` and `nm` for sections, symbols and relocations, none of which is affected; it is instruction decoding that is wrong.

The rules a build compiles are not in the tree, in either form. `lang/<tag>/rules` is the source: `make rulecode` writes `delta_rules_<tag>.c`, its header and its shim out of that text, two seconds a language, and every build runs it first. A module says in `lang/<tag>/rules/trials` which of its `.up` files it keeps but does not build -- English's four stand in for rules IBM compiled and are there for `make upper-check`, Polish's fifty-five are its own and are built. Editing a `.dr` or a `.up` is therefore the whole of changing a rule; there is no second copy to write out afterwards and none that can go stale. `lang/jajp` is the exception, lifted whole by the commands in `docs/japanese.md`, which write those three files themselves.

The rules as C are thirteen megabytes of generated C in `lang/<tag>/delta_rules_cNN_<tag>.c`, decompiled from that bytecode; `make rules` writes them. Two minutes of Python and about fifteen seconds of compiler on twenty-four cores, where the one file this used to be was seven minutes that could not be shared out. `PARTS` in the Makefile and `EVV_RULE_PARTS` in the decompiler have to say the same number, since the build names the files it expects rather than taking whatever is there.

Releases are tags: pushing `vN` makes the workflow build the archives and cut the release. Nothing about them is manual.

## Markdown formatting

All Markdown files are formatted with hard line breaks removed within paragraphs. Paragraphs are separated by blank lines, and structure (headers, lists, code blocks) is preserved. This keeps documents readable while removing artificial mid-paragraph line wrapping.
