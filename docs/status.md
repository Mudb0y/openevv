# What works and what does not

Last measured 22 August 2026.

## Works

The engine speaks, and it speaks IBM's samples. All 81 cases in six categories come out byte for byte identical to IBM's own binary: plain text, UTF-8, annotations, annotations with the annotation input type on, real-world text with the parameters read back in a person's units, and the user dictionary.

That holds in all four configurations the tree can build for this machine -- thirty-two and sixty-four bit, each with the rules run as bytecode and with the same rules run as the C they decompile to -- and in the Windows build as well, which is a fifth: `build/probe.exe` matches IBM's binary over the same 81 cases, under Wine on Linux and on Windows itself, where the scripts run both binaries without Wine in front of them.

Nothing is borrowed at build time. `make missing` answers nothing, which is the check that no call has quietly gone back to IBM's objects. The language data is all transcribed and in the tree, so an ordinary build needs a C compiler and nothing else.

Dictionaries can be edited. `tools/delta-dict.py` writes `lang/enus/enus.dict` out of the tables and reads it back in, so a pronunciation can be changed, laid down and heard.

All nine languages in the SDK lift and decompile: US and British English, both Spanishes, both Frenches, German, Italian and Japanese. English is the one that is finished; German builds, speaks and matches IBM over the cases there are for it, and the section below says in which configurations.

A build takes as many languages as it is given. `make LANGS="lang/enus lang/dede"` puts both in one binary: `eciGetAvailableLanguages` answers with both, a caller picks one the way IBM's interface always allowed, and each is held against its own oracle out of the same binary. What made that possible is in `src/delta_lang.h` -- every module names its own tables after itself, because IBM gave them the same names in every language, and the engine reaches whichever is in force rather than linking to one by name.

It builds and speaks on Windows, sixty-four bit, as one static file. The speak window plays what it makes through waveOut; `win/speak.c` is that, and it is the only front end that plays anything.

And it builds as `eci.dll`, exporting the fifty-two names IBM published, so a program written against IBM's library -- a screen reader add-on, most likely -- can load ours instead. Both bitnesses: sixty-four bit for an add-on that loads the engine into the reader's own process, thirty-two bit for the most used driver, which hosts the engine in a 32-bit process of its own whatever the reader is. Checked on Windows itself: by name from C for both, and through ctypes for the sixty-four bit one, as an add-on does.

What is not exported is the filter interface, which the engine does not implement, and the dictionary find, lookup and update calls, which have no public wrapper yet.

## Not done

Live audio on Linux. The engine hands its samples to the caller, and `build/evv` writes them as a wave file or down a pipe; nothing sends them to a sound card as they are made. Windows got there first because waveOut is forty lines; PipeWire is next and is a thin sink on top of the same buffer.

The compiler. The rules are readable C, but there is no way to write a new rule except by writing that C. This is the next piece of work and the gate to adding a language.

Polish, which is the reason the compiler matters. Nothing started.

Anything but English and German at build time. The other seven lift and decompile, but nothing has been built from them.

## German

German builds and speaks, and speaks IBM's samples. `make LANG=lang/dede probe` and the reference beside it are in `docs/building.md`, and `EVV_LANG=dede test/suite.sh` runs the same six categories over 80 cases of its own. On 22 August 2026 all 80 came out byte for byte identical to IBM's German binary.

That is the sixty-four bit Windows build, running on Windows against a reference built from `analysis/dede`, with the rules run as bytecode and with the same rules run as the C they decompile to; and both on its own and linked beside English in one binary, where each language still matches its own oracle over its own cases. Nothing has been built from `lang/dede` for Linux or thirty-two bit.

Two things had to be fixed to get there, and both were ours rather than the language's.

The first was the table of which dictionary is in force for which language. It holds a four-byte slot per language and dialect, and it was being written as a host pointer, which on a sixty-four bit host reaches over the slot beside it. English never showed it: family one is the first slot the sweep looks at, so the slot its store reaches into is one nothing ever reads. German is family four, so the sweep read the slot the store had reached into and handed the engine half a pointer, and every German dictionary case crashed. `src/eci_dict.c` keeps a value there now, as the original does.

The second was in the lifter, and it was one wrong number in one rule. Where the compiler wrote a small constant into a register as `push` and `pop`, `tools/delta-lift.py` folded the pair into a single load. A switch arm that wants a different constant in the same register is written as its own push and a jump to the pop of the arm below it, so that pop is a landing place, and folding it away took it out from under the jump: the arm that jumped arrived past it and the register kept whatever it happened to hold. In the German `/r/` that put the alternative's own number into the fricative amplitude -- 82 where IBM says 45 -- one of the hundred and thirty-one points the rule writes into its streams for `tra`, and the only one that differed. It is what made a German word with certain consonants before an `r` wrong -- `tra`, `kra`, `pra`, `gra`, `fra` differed and `bra`, `dra` and `ra` did not -- and, through the rules that name a character, every case with a backtick in it.

The lifter no longer folds when something jumps at the pop. Lifting again changed three places in German and two in English, which is why `lang/enus/delta_rules_enus.c` moved as well: the same pattern was there all along and no English case had ever reached it. English still passes all 81.

What is left is not German's. Two of the cases with markers in them -- an audio marker and a pair of index marks -- differ from run to run, and it is IBM's binary that varies: over six runs of one of them ours produced the same samples every time and the reference produced different ones once. `test/compare.sh` already retries a case that hangs, which is how the same flakiness shows up in English, where it hangs instead of answering differently.

Not done for German: no dictionary in a form a person can edit, since `tools/delta-dict.py` has only been run for English; no `long` cases; and no Linux or thirty-two bit build.

## Partly done

The rules read as rules, to eleven passes of the decompiler. What that means concretely: calls sit with their arguments, wrapper rules say which primitive they stand for, state reaches say which language variable they touch, frame reaches say which argument they are, switch arms say which alternative they are, register halves say which half, and the machine's dead leavings are gone.

What is left in them: 63,739 gotos, of which 22,389 are the backtracking dispatch and are correct as gotos. The rest are branches that could not be structured without changing meaning.

Three things in the rules cannot be recovered and are not going to be. The global variables' names are gone: they are known only by kind and number, because the only record of them is a disassembly of `delta_new` that carries kinds and not names. The frame below a rule's arguments is unnamed, because nothing in the object says what any of it is for. And the 152 wrapper rules that do arithmetic as well as calling keep their names.

## Known limits

The test suite needs IBM's objects, because it compares against IBM's binary, and on anything but Windows it needs Wine to run that binary. Both are obtainable: `docs/building.md` says where IBM's SDK still is. Without them there is no automatic check that the audio is right, only `tools/say.sh` to listen with and `tools/delta-check.sh` to hold the two forms of a rule against each other.

The reference binary is not steady on a case with a marker in it. Usually it hangs, and `test/compare.sh` retries a case once on its own before reporting it as hung, because calling that a difference cost false alarms. Sometimes it answers with different samples instead, which the retry cannot tell from a real difference: six runs of one German audio-marker case gave the same samples from ours every time and a different one once from IBM's. A marker case that differs once and not again is that, not a change in the engine.

The sixty-four bit build maps a region low in memory, because the Delta machine keeps addresses in thirty-two bit values and everything it can point at has to be nameable in one. The program itself may be loaded anywhere: the language's data is copied into that region at startup rather than named where it lies. A machine that cannot map anything below two gigabytes would need a different answer, and would say so rather than misbehave.

If the audio sounds wrong to you, it is not a fault in the port: our output is identical to IBM's. Changing it is a deliberate change to the language data, and the suite will correctly report that as a difference.
