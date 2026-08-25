# What works and what does not

Last measured 24 August 2026.

## Works

The engine speaks, and it speaks IBM's samples. All 81 cases in six categories come out byte for byte identical to IBM's own binary: plain text, UTF-8, annotations, annotations with the annotation input type on, real-world text with the parameters read back in a person's units, and the user dictionary. That is English; eight of the SDK's nine languages do the same over the cases there are for each, and the sections below say which.

That holds in all four configurations the tree can build for this machine -- thirty-two and sixty-four bit, each with the rules run as bytecode and with the same rules run as the C they decompile to -- and in the Windows build as well, which is a fifth: `build/probe.exe` matches IBM's binary over the same 81 cases, under Wine on Linux and on Windows itself, where the scripts run both binaries without Wine in front of them.

Nothing is borrowed at build time. `make missing` answers nothing, which is the check that no call has quietly gone back to IBM's objects. The language data is all transcribed and in the tree, so a build needs a C compiler and Python and nothing else -- Python because the default build writes the rules out as C first. `make RULES=bytecode` wants the compiler alone and is half a minute rather than a quarter of an hour.

Dictionaries can be edited. `tools/delta-dict.py` writes `lang/enus/enus.dict` out of the tables and reads it back in, so a pronunciation can be changed, laid down and heard.

All nine languages in the SDK lift and decompile: US and British English, both Spanishes, both Frenches, German, Italian and Japanese. Eight of the nine build, speak and match IBM byte for byte over the cases there are for them -- everything but Japanese, which has no oracle to be held against and is the subject of a section of its own. English is the one that is finished in the fuller sense of having a dictionary a person can edit.

A build takes as many languages as it is given. `make LANGS="lang/enus lang/dede lang/engb lang/eses lang/frfr"` puts all five in one binary, and any other set the same way: `eciGetAvailableLanguages` answers with all of them, a caller picks one the way IBM's interface always allowed, and each is held against its own oracle out of the same binary. What made that possible is in `src/delta_lang.h` -- every module names its own tables after itself, because IBM gave them the same names in every language, and the engine reaches whichever is in force rather than linking to one by name.

That worked on Windows before it worked here. Any build with two languages in it died on Linux, in `sscanf` under `loadStandardConcatenativeVoice`, for a reason that had nothing to do with which two: joining the modules' settings blobs put one section straight behind another with no blank line between them, and `ini_getString` decides a key is absent by reading the byte where its search stopped rather than by where it stopped. With another section behind it, a key that is not there came back holding the next section's first value -- a dataset path where eight numbers were being read. The defect is IBM's, in their own `analysis/enus/win_iniread.obj`, and cannot fire in their engine: their `readFileIntoMemory` hands over one embedded blob for one language, whose sections are a blank line apart. Two sections butted together is a shape their data never has. `src/delta_lang.c` now writes that blank line and `src/eci_iniread.c` decides absence by where the search stopped; `make inikeys` is the check, and it needs neither Wine nor IBM's objects.

It builds and speaks on Windows, sixty-four bit, as one static file. The speak window plays what it makes through waveOut; `win/speak.c` is that, and it is the only front end that plays anything.

On Linux, `make speechd` builds a native Speech Dispatcher output module. It
streams PCM to the server, advertises every built language and its eight voice
presets, and implements the ordinary speech settings, spelling, characters,
keys, index marks, stop and pause. Punctuation uses Speech Dispatcher's
server-side symbol preprocessing; retained symbols are suppressed after their
localized names so they are not spoken twice. Its direct protocol suite passes
with all eight languages in one process, and Speech Dispatcher 0.12.1 discovers
the module and lists all 64 voices. Audible punctuation, playback, and
screen-reader behavior still require the manual checks in
`docs/speech-dispatcher.md`.

And it builds as `eci.dll`, exporting the fifty-two names IBM published, so a program written against IBM's library -- a screen reader add-on, most likely -- can load ours instead. Both bitnesses: sixty-four bit for an add-on that loads the engine into the reader's own process, thirty-two bit for the most used driver, which hosts the engine in a 32-bit process of its own whatever the reader is. Checked on Windows itself: by name from C for both, and through ctypes for the sixty-four bit one, as an add-on does.

What is not exported is the filter interface, which the engine does not implement, and the dictionary find, lookup and update calls, which have no public wrapper yet.

## Not done

Direct live audio in `build/evv` on Linux. The command still writes a wave file
or a pipe; live playback is available through Speech Dispatcher rather than a
PipeWire-specific CLI sink.

The compiler. The rules are readable C, but there is no way to write a new rule except by writing that C. This is the next piece of work and the gate to adding a language.

Polish, which is the reason the compiler matters. Nothing started.

Japanese, which is the one language still not built. Everything else in the SDK now is.

## German

German builds and speaks, and speaks IBM's samples. `make LANG=lang/dede probe` and the reference beside it are in `docs/building.md`, and `EVV_LANG=dede test/suite.sh` runs the same six categories over 80 cases of its own. On 22 August 2026 all 80 came out byte for byte identical to IBM's German binary.

That was the sixty-four bit Windows build, running on Windows against a reference built from `analysis/dede`, with the rules run as bytecode and with the same rules run as the C they decompile to; and both on its own and linked beside English in one binary, where each language still matches its own oracle over its own cases. Linux followed on 23 August 2026, again in both rule forms, on its own and in a binary with English and British English in it, and thirty-two bit the same day in both rule forms -- all 80 every time. So German now passes everywhere English does.

Two things had to be fixed to get there, and both were ours rather than the language's.

The first was the table of which dictionary is in force for which language. It holds a four-byte slot per language and dialect, and it was being written as a host pointer, which on a sixty-four bit host reaches over the slot beside it. English never showed it: family one is the first slot the sweep looks at, so the slot its store reaches into is one nothing ever reads. German is family four, so the sweep read the slot the store had reached into and handed the engine half a pointer, and every German dictionary case crashed. `src/eci_dict.c` keeps a value there now, as the original does.

The second was in the lifter, and it was one wrong number in one rule. Where the compiler wrote a small constant into a register as `push` and `pop`, `tools/delta-lift.py` folded the pair into a single load. A switch arm that wants a different constant in the same register is written as its own push and a jump to the pop of the arm below it, so that pop is a landing place, and folding it away took it out from under the jump: the arm that jumped arrived past it and the register kept whatever it happened to hold. In the German `/r/` that put the alternative's own number into the fricative amplitude -- 82 where IBM says 45 -- one of the hundred and thirty-one points the rule writes into its streams for `tra`, and the only one that differed. It is what made a German word with certain consonants before an `r` wrong -- `tra`, `kra`, `pra`, `gra`, `fra` differed and `bra`, `dra` and `ra` did not -- and, through the rules that name a character, every case with a backtick in it.

The lifter no longer folds when something jumps at the pop. Lifting again changed three places in German and two in English, which is why `lang/enus/delta_rules_enus.c` moved as well: the same pattern was there all along and no English case had ever reached it. English still passes all 81.

What is left is not German's. Two of the cases with markers in them -- an audio marker and a pair of index marks -- differ from run to run, and it is IBM's binary that varies: over six runs of one of them ours produced the same samples every time and the reference produced different ones once. `test/compare.sh` already retries a case that hangs, which is how the same flakiness shows up in English, where it hangs instead of answering differently.

The NVDA add-on follows. Where the library it loads has more than one language in it, every language's eight presets are offered as voices of their own -- "German - Voice 3" -- each saying which language it is, so the reader matches a document's language to one of them; and a `LangChangeCommand` in a speech sequence switches the engine mid-utterance, so a German quotation in an English page is read as German. A library with one language in it offers what it always did, under the same names, so nothing a reader had chosen is lost.

Not done for German: no dictionary in a form a person can edit, since `tools/delta-dict.py` has only been run for English, and no `long` cases.

## British English

British English builds and speaks, and speaks IBM's samples. `make LANG=lang/engb probe` builds it and `EVV_LANG=engb test/suite.sh` runs the same six categories over 81 cases of its own, against a reference built from `analysis/engb` -- `make -C reference TAG=engb BUILD=../build/reference-engb`. On 23 August 2026 all 81 came out byte for byte identical to IBM's British binary, in all six builds -- sixty-four bit, thirty-two bit and Windows, each with the rules as bytecode and as the C they decompile to -- and again out of a binary with English and German in it.

It is family one dialect one where US English is family one dialect nought, which is the first pair of dialects in one family the language mechanism has carried; `test/compare.sh` knows it as `0x10001`. That is the thing British English proves which German could not: nothing keys off the family alone.

It matched on the first run, with no fix needed anywhere -- the lift, the mechanism and the tools were all already right. What it did find was the multi-language crash above, and only because two dialects of one family was the pair being tried at the time; English and German turned out to crash identically.

Not done for British English: no dictionary a person can edit, and no `long` cases of its own.

## The Spanishes, the Frenches and Italian

All five lifted and matched IBM byte for byte on 23 August 2026, over 81 cases each: `EVV_LANG=eses`, `esus`, `itit`, `frfr` and `frca`, each against a reference built from its own objects. Their language numbers are 0x20000 and 0x20001, 0x50000, and 0x30000 and 0x30001, and `test/compare.sh` knows them all.

Four things had to be fixed to get there, and every one of them was ours rather than the languages'.

**A compound variable of the seventh kind sits on a four-byte boundary.** The two Spanishes, French of France and Italian could not be lifted at all: the model of the variable area came out two bytes short at one compound. It was not the payload rounding, which had been the standing guess and which breaks the languages that already worked. It is alignment -- a compound whose first word is 6 holds four-byte items and is laid four-aligned, and every other kind two-aligned. Across all nine modules every one of the thirty-six is four-aligned and no other kind's are. The rule lives in two places that have to agree, `tools/gen-globals.py` for the lifter's model and `src/eci_deltaglob.c` for the area the engine really builds, and fixing only the first left the engine reading a variable's kind out of the middle of the cell before it. What says it is this rather than merely sufficient: every one of the eight built modules regenerates its variable area byte for byte from the objects, the three that were committed before this included.

**A jump names a place in its rule, and the place was being read as a signed number.** So it wrapped at 32,767. English's longest rule is 30,929 bytes -- within six per cent of that and never over it, which is why three languages never showed it. French of France has a rule of 33,075 bytes and Canadian French one of 34,154, and both jumped somewhere negative and took the machine apart, by way of a rule finding its own activation record above the live frames. Nothing about the bytecode changed: the emitter always wrote a position from the start of the rule, which cannot be negative, and only the reading of it was wrong. The emitter now refuses a rule too long to name a place in rather than writing one that cannot be read.

**Two machine primitives no English rule reaches.** `lpta_loadi` loads an immediate into the left accessor, asking the statement table for the kind instead of reading it off a location; all five wanted it. `insert_rv` inserts a rule's variable over a range taken to the right, and only Canadian French wants it -- it is `insert_2ptv` with a different range call, whose answer means the opposite way round.

**A little floating point, which is the surprise.** Two rules in France's module and eight in Canada's use the x87 stack: an integer pushed, multiplied or added to by a double constant or another integer, and truncated. Nothing else in nine languages does, which is what one would expect of a fixed-point engine. It is worked out in `long double` because that is the register the original computes in, and the difference is not academic: with the constant 0.4, an input of 5 and an addend of -3, sixty-four bit arithmetic keeps 2.0 exactly and truncates to -1 where the eighty-bit register keeps 2.000000000000000111 and truncates to 0. Two of two point nine million combinations differ and that is them.

One thing about the case files is worth knowing. The engine is a single-byte engine, so the `utf8` cases really test that both sides mangle multi-byte input the same way rather than that either handles it. For Spanish that is not merely mangled: an o-acute directly before an n crashes IBM's engine and ours identically, at the same fault address, so `razón` cannot be compared at all and the case file avoids it. The same text in Latin-1 speaks perfectly.

Not done for any of the five: no dictionary a person can edit, and no thirty-two bit or Windows build.

## Japanese

`docs/japanese.md` is the whole of it, written for somebody picking this up who was not the person who found it: what is done, what is left with its sizes, the oracle and why it can be trusted, the target and how to observe it, the decisions already taken, and the traps. What follows here is the short version.

Japanese lifts -- 477 rules, its settings, and the language number 0x80000 --
and one thing stands between that and a language that speaks: the romanizer.

**It has an oracle now, which it did not before.** A reference built from
`analysis/jajp` would not link: it wanted `getFullPathName`, which every other
module's `libmain.obj` defines and Japanese's does not, and `ralStrNicmp` and
`_chkstk`, which are in none of the nine. So there was nothing to hold a
Japanese build against. All three are now supplied, and it matters where each
one came from. `ralStrNicmp` went into `src/port_ral.c` beside `ralStrIcmp`,
which already takes a length first with nought meaning the whole string, and is
the same call shape -- the runtime abstraction layer has always been ours on
both sides of the comparison, so that is the existing boundary and not a new
one. The other two are in `reference/jajp_shim.c` and are linked for that one
module only: a path to files this port never reads, and the stack-frame helper
Microsoft's compiler calls instead of subtracting from the stack pointer. IBM's
Japanese binary now speaks, and the English and German references are unchanged
-- English still matches over all 81.

**What is left is the romanisation module**, which is what `jpnrom.dll` is in
stock Eloquence. `rz_isRomExist` in `src/eci_romanizer.c` says family 8 dialect
0 has one, which is Japanese, and `rz_getRomanizerInst` always answers that
there is none because loading one is Win32 `LoadLibrary` work that was
deliberately left on the far side of the porting boundary. So
`rz_setActiveLanguage` returns -1, setting the language fails, and no instance
is made. Take that one line out and Japanese speaks: 13,486 samples, against
IBM's 18,293 for the same text, and the difference is the romanisation.
Everything else about the language -- rules, globals, sets, settings -- is
already right.

**And the target is observable now, which it was not.** IBM's Japanese does
speak Japanese script, and what decides whether it does is how the instance was
made: `eciNew()` gives nothing for Shift-JIS kana, and `eciNewEx(0x80000)` --
the only language there is -- gives 13,266 samples. That is why
`reference/speak.c`, which tries `eciNew` first, produced nothing and looked for
a while like an engine that could not do it. Setting the codeset parameter
afterwards is refused; the language given at creation is what carries it.
`make -C reference TAG=jajp jptry` builds the driver that settled it, and the
head of `reference/jptry.c` has the table. Romaji gives 18,293 samples and kana
13,266, so the romanizer is not passing letters through, and that difference is
what anything transcribed has to reproduce.

Measured properly, the transcription is about 163 KB of x86 across thirty
objects once the engine objects already ported and the dictionary lifted as data
are taken out. That is a Japanese morphological analyser: phrase tables, a path
search, number reading, intonation phrases, unknown-word handling, penalties.
The dictionary beside it is 2.67 MB in 1,723 blobs and lifts in one command with
tools/lift-rom.py, which is written and proved.

The subsystem is sixteen objects and about half a megabyte: `rominstance`,
`rommanager`, `rominstparam`, `romreg` and `romedll_link` are the framework,
`jpnrom`, `jpnutil`, `kanastr`, `PCRoman2BG` and the three `MakeReadable*` the
Japanese half, and `skana0`, `skana1`, `stakankana0` and `jpnsdict` the tables
and dictionary. `romedll_link` is what makes the reference romanise despite
loading no DLL: in a static build it stands in for the library.

`lang/jajp` is deliberately not in the tree until it can make an instance, since
a module that cannot would break any build that named it. It lifts again in one
pass from the tools.

## What the lifting cost, across all nine

All nine lift their rules cleanly, which was the part expected to be hard, and
the holes that remained were smaller than the headline suggested. Every one of
them turned out to be a gap in our machine rather than something not understood
about the language: two primitives no English rule reaches, one alignment rule,
one signed number that should not have been signed, and a little floating point
nobody expected a fixed-point engine to contain. The sections above say which
was which. What is left is Japanese, and what is left of Japanese is an oracle
rather than a lift.

## Partly done

The rules read as rules, to sixteen passes of the decompiler. What that means concretely: calls sit with their arguments, wrapper rules say which primitive they stand for, state reaches say which language variable they touch, frame reaches say which argument they are, the arms of a backtracking dispatch say which alternative they are whichever of the two ways the compiler wrote the dispatch, register halves say which half, a test of the flags is the comparison the machine made -- a call's answer against nought, a length against a limit, a bit against a mask -- rather than a flag set and a flag read, letting go of a call's arguments says that and not that a scratch register was written, a jump at the return is a return, 2,391 loops are loops, a jump out of one says it is leaving it, the two places a rule ends say whether it has matched or given up, and the machine's dead leavings are gone.

What is left in them: 59,295 gotos, of which 26,668 are the arms of a backtracking dispatch and are right as they are, and 4,254 more say plainly that the rule has matched or has given up. Most of the rest are the same thing without a name, because that is what the language is: a pattern matcher whose every failure jumps to a shared tail.

Of the flags, 49 comparisons and 1,054 conditions are left out of the 24,140 and 27,013 there were. The comparisons are the ones with a label between them and the condition that reads them, where something else can arrive with other flags. The conditions are mostly reading what an arithmetic operation left rather than what a comparison did -- the decrement in a dispatch, whose zero flag says the answer was one -- and those are the next ones that could go.

`tools/delta-shape.py` says what more structuring could reach, and the answer is bounded. Of the 106,072 edges between the 53,439 basic blocks, 6,004 jump back to a block that does not stand on every path to them, which is the definition of flow that no arrangement of loops and conditionals can say. 610 of the 1,042 rules have at least one, 191 have ten or more, and the worst is `hebrew_ph_Q` with 129 in 186 blocks. Saying those in a structured language means copying the code the jump lands on, or adding a variable to dispatch on; the first costs the correspondence between the C and the bytecode, which is what makes the C checkable, and the second buries the dispatch the rules already have under an invented one.

Three things in the rules cannot be recovered and are not going to be. The global variables' names are gone: they are known only by kind and number, because the only record of them is a disassembly of `delta_new` that carries kinds and not names. The frame below a rule's arguments is unnamed, because nothing in the object says what any of it is for. And the 152 wrapper rules that do arithmetic as well as calling keep their names.

## Known limits

The suite compares one utterance per process, and until 22 August 2026 that was the only utterance anything had ever compared. It is no longer: `probe` and the reference both take a `t` in their mode argument, which says the same text a second time on the same instance and writes it beside the first.

What that found is worth knowing before reading any byte comparison here. The second utterance is not the first -- 38,423 samples both times, 30,495 of them different -- because the machine's state has moved on. That is faithful: IBM's own engine differs across its two utterances to the same 30,495 samples, and ours matches IBM's *second* utterance byte for byte as well as its first. And it is deterministic: three processes give the same first utterance and the same second one, to the hash.

So samples are comparable, a second utterance included, as long as both sides have spoken the same history; what cannot be compared is a second utterance against a first. An earlier note here said the engine produced a different hash each time, which was wrong -- the difference is between the first utterance and the second, not between one run and the next.


The test suite needs IBM's objects, because it compares against IBM's binary, and on anything but Windows it needs Wine to run that binary. Both are obtainable: `docs/building.md` says where IBM's SDK still is. Without them there is no automatic check that the audio is right, only `tools/say.sh` to listen with and `tools/delta-check.sh` to hold the two forms of a rule against each other.

That last one compares every rule entered and every call made, with their arguments, over the seven plain cases taken one at a time. Three kinds of line are left out and all three are the interpreter's alone: the stores it makes, which a rule written as C makes for itself; its remark about the argument area being a different depth than the compiled code expected; and addresses in the arena, which differ because a rule written as C takes a smaller frame on purpose.

Interrupting from another thread is answered, and `make stopthread` is what answers it. `eciStop` called from a second thread while the engine is handing samples over used to fault every time; with the busy guard it was none of twelve on real Windows and eight of twelve under Wine, and both of those were taken before the queue-count fix. On 23 August 2026, with both fixes in, twenty-four turns come through clean on Linux and twenty-four under Wine, every one of them stopping an engine that was still delivering, and every follow-up utterance worth exactly what a whole one is. Removing the busy guard faults both of them at once, which is what says the harness can see the door at all. Answering `eciDataAbort` from the callback is clean too: `make interrupt` holds twelve turns on Linux and twenty-five under Wine and on real Windows.

What that harness will not assert, and the reason is worth keeping, is that an interrupted utterance comes out short. A stop cannot make the engine abandon an utterance; all it can do is stop the rest of the buffers being handed over, so whether the sample count is short depends on where the suspension lands between two buffers. A callback that returns at once lets the engine finish before another thread reacts, and a callback that paces itself like a real player runs on the engine's own thread, so the stop then waits for the whole delivery and can never truncate. Both were tried. The count is reported and not required.

Two lessons were bought expensively on the way and are worth more than the numbers were. A library held open cannot be replaced, and a screen reader holds its own: an evening of "the abort door faults on Windows" turned out to be an add-on loading an `eci.dll` from its installed directory that had been locked open since before the fix, so every measurement was against the engine that still had the bug. Anything experimental gets its own staged copy of the library. And a fault with an empty stderr and 0xC0000005 says only that nothing in the engine complained; it does not say what happened, and it is not evidence for whichever mechanism looks like it.

A hazard was found while chasing that, and it is real on its own terms even though it explained nothing that was reported. A landing place jumped to from a thread that never planted it used to jump to nought. The engine backtracks to a place a rule planted, named by the address of the rule's frame; the frames are in the arena, which every thread shares, and the machine keeps the one it means to return to in its own state, where whoever stops the engine can reach it. So the name travels between threads although the landing place cannot: the table of places is one per thread, on the stated assumption that two threads cannot share a name, which the arena makes false. The lookup then made a place rather than refusing, the new place was all noughts, and the jump loaded nought as the stack pointer and went to nought.

The lookup now refuses, so the same mistake is a sentence rather than a fault with nothing in it. `make landing` is the check: it plants a landing and lands on it, then jumps to that name from another thread, and answers non-zero if the guard does not fire. Against the committed code that test is a bare segmentation fault. This does not make stopping from another thread correct -- it makes it say what is wrong, on every platform, at the moment it happens. Doing it properly means the stop asking the engine's own thread to unwind rather than unwinding it from outside, and that is not done. Nothing observed so far needs it: five drive loops on Linux -- polling, `eciSynchronize`, synthesis on a worker thread, the same with the cancelling thread polling, and `eciStop` from a second thread -- all run clean, and so does the add-on's own loop once it is loading a current library.

One narrow window is left in the guard and is not closed. A place is marked as planted before the save writes into it, so another thread jumping in between the two would find a planted but empty place and fault as it did before. Closing that means marking after the save returns, which is awkward across a call that returns twice and across the thirty-two bit `setjmp` path. The window only exists while a cross-thread jump is already happening, which is the mistake being diagnosed.

What cancelling costs, measured in the engine. Every figure here is from `build/probe` on Linux with no player in the picture at all; the numbers taken through the add-on on Windows have been withdrawn by the session that took them, because the fake player in that harness paced a buffer by sleeping for its duration and a cancel did not reliably interrupt the sleep, so most of a buffer was being counted as engine time. What that harness said about *relative* improvement was measured the same way on both sides and is probably sound; its absolute numbers are not, and none of them are quoted here.

Cancelling costs the same by all three routes -- letting the utterance finish and throwing its samples away, answering `eciDataAbort`, and calling `eciStop` from the cancelling thread -- because all three wait for the same thing. That was measured on Windows too and is a comparison rather than an absolute, so it survives the withdrawal above.

So the abort door being open does not help the latency, and the earlier claim here that what remained was the add-on removing its workaround was an inference from the door opening rather than a measurement. It is withdrawn. Answering `eciDataAbort` stops the engine handing over more buffers; it does not stop it finishing the utterance, and neither does `eciStop`.

**The engine cannot abandon an utterance it has been told to stop, and that is settled rather than open.** All of a cancel is `stm_qtSuspend` waiting for the synthesis thread to finish the message it is on: 71 ms of 71 for a long row, where re-initialising the language globals is 0.3 ms and suspending the queues is nought. The machine is never told to stop -- `ELOQ_BUSY` is set every time that branch is reached, so `throwDeltaErrorNow` is never called -- and the walk does exactly the work that was left: a row is 298,745 rule entries, 97,382 before the first buffer and 201,411 after the stop, and the two add up. The cooperative interrupt that is raised is honoured 123 times in the sample generators and is worth about 20 ms of that.

Six ways of making it give up were tried and all fail, and the reason is one thing rather than six. The best of them had the decompiler emit, just after each rule is entered, a jump to that rule's own give-up tail -- the one that calls `vretproc` and puts back everything the rule's record holds -- in all 459 rules that have such a tail. It faults in `vdef_proj` under `vrange_2pt` under `mark_s`, from a rule that ran *after* the one that gave up. **The rules build a shared structure as they go and later rules assume the earlier ones finished it**, so abandoning any rule part way leaves that structure half built and nothing validates it. There is no safe abandonment point anywhere in the machine, and the only safe stop is to let the unit of work finish -- which is what `stm_qtSuspend` already does. Making the machine interruptible would mean the rules checking their own inputs, which is the language and not a patch.

What that leaves is doing the leftover work faster, which is why the rules are now compiled by default. Measured on Linux, bytecode against `RULES=c`: the row synthesises in 138 ms against 63, the wait before the first samples is 38 ms against 12, and cancel-and-speak is 124 ms against 39. So the cancel cost falls from about 86 ms to about 27, and a filename from 22 ms to 8. Nothing was written to achieve that; it is the same rules, compiled.

A trap in the interface, found while chasing a residual that turned out not to be the engine's at all, and worth knowing on its own terms. A caller answering `eciDataNotProcessed` -- "I did not take these samples" -- costs a flat thirty milliseconds per buffer: `eo_tell` in `src/eci_old.c` maps it to `BRIDGE_ABORT`, which is `APP_AGAIN`, and `aq_synchronize` answers that with `th_sleep(30, 0)` before offering the same buffer again. That is right for a player whose buffer is genuinely full and ruinous for a caller that is discarding, and nothing in the interface warns of it. A caller that means "throw this away" must answer `eciDataProcessed`. This is IBM's behaviour and is not being changed.

Interrupting an utterance and then speaking again on the same instance used to leave the engine quiet from the second interruption onwards, accepting the text, reporting no error, answering that it was not speaking, and saying nothing. That is fixed. `make interrupt` is the check and it now asserts what it used to only print: twelve turns, every follow-up utterance worth exactly what the first whole one was.

It was never on the interrupt path. The application queue keeps two counts, one of everything it has been told about and one of everything the application has collected, and it takes them being equal to mean there is nothing outstanding. The stop put the first back to nought and left the second where it was, so the two drifted; when the stale one happened to equal the fresh one, the queue believed it had caught up and handed over none of the samples already sitting in it. The text had arrived and the samples had been made. They were made and then not collected.

Only the sixty-four bit builds had it, which is why the suite never saw it: the stop reached that second count by the byte it sat at when a pointer was four bytes, and at sixty-four bits that byte is inside the queue's own send lock. The same build thirty-two bit runs the check clean without the fix. That is the third time an unconverted byte offset has cost a fault, so the offsets that reach a block by number rather than by name were swept afterwards; what the sweep found is below.

The sweep, one fixed and one left. Eighteen blocks in the engine are still reached by the byte a field sat at. Most are safe and stay that way for a reason: a block the machine can see has the same layout in both bitnesses by design, because everything in it that points is a four-byte `evv_ref` rather than a pointer, so a number into one of those means the same thing either way. What is not safe is reading such a field as a pointer, or reaching into a block whose layout is ours and has grown. Two of the eighteen do one of those.

`src/eci_deltalib.c` writes the machine's "undefined reads back as this" field as a `const char **`, and what it writes is the address of `"---"` in the program -- or it did, and this one is fixed. Both halves were wrong at sixty-four bits: the field is a four-byte reference into the arena, so an eight-byte write spills past it, and an address in the program is the one thing the machine may never be given. Going through the raw offset is exactly what slips it past `evv_ref_checked`, which exists to refuse this and never gets the chance. The reader, in `src/eci_access.c`, takes it as a reference and translates it, so at sixty-four bits it translates rubbish; at thirty-two the two agreed by accident, because a reference and a pointer are the same four bytes there. Nothing asks for it in the 81 cases, so it was latent rather than live -- but reading that field back and using it as a string faults, which is a measurement and not an argument. The string is now copied into the arena by `delta_low_copy` and the field holds a reference to the copy, which reads back as "---".

`src/eci_pcm16.c` and `src/eci_soundfmt.c` reach a sound file's block by offsets that overlap once a pointer is eight bytes: the format at 0x0c runs over the rate at 0x10, the stream at 0x18 over how it was opened at 0x1c, and the index function at 0x20 over its parameter at 0x24. That block is ours, not the machine's, so it should be a struct -- and `src/eci_soundfile.c` already declares that struct and already has the right idiom for the format table, `FMT_SLOT`, which reads a slot as the byte offset divided by four. Three things say the layer is unreachable rather than merely unused, so this is left alone and written down instead. `ST_SOUND(t)` is nought, measured by printing it: every sender in the engine is guarded by it, so nothing in the sound thread runs. `ealAudioSoundFormat` is `int32_t[16]`, sixteen words of nought, and it registers first and is therefore the default format, so nothing could be called through it. And `constructSoundFile` and `soundFileFormat` have no callers anywhere in the tree. Since nothing can observe that code, nothing can check a change to it either, which is the reason for leaving it: a fix there would be mechanically right and unverified. Two things to know when it is wired up. `FMT_SLOT` itself is wrong -- `(*(void ***)(f))[(off) / 4]` has one dereference too many for a table that is the array, so it would index off the format's name -- and the six offsets `eci_soundfile.c` names all land on the right function when read as slots, which is what says the layout is determined rather than guessed. It becomes live the day anything asks the engine to write a file itself, or the day Linux audio is wired through this layer rather than through the buffer.

Making and throwing away engine instances used to leak four megabytes each, and the engine went quiet on the 63rd: it had run out of the arena, the region below two gigabytes that everything the machine can hold a pointer to comes out of. That is fixed, and `make instances 200` now runs two hundred rounds all owing the same samples where it used to stop at 63.

It was the frame stack. A rule hands the machine the address of its own frame, so frames cannot be ordinary locals and come from a four megabyte stack the thread takes on the first rule it runs -- kept for the life of the thread, which is right, since the frames nest strictly and the stack is reused. What was wrong is that nothing gave it back. Every instance starts a synthesis thread, so every instance kept four megabytes, and 256 divided by four is 62. Both trampolines in the porting layer now call `evv_frame_done` once the thread body has returned, which is the one moment it is certainly safe: no rule of that thread is running and nothing holds a frame.

`evv_arena_outstanding` is what found it and is now in the tree. It walks the arena and reports what is still held, grouped by the allocation that asked and the size it got, heaviest first. A leak is the group that grows by the same amount with every instance: this one was one block of 4,194,320 bytes per instance, against everything else standing still, which named it in a single run. `whence` is the low thirty-two bits of a return address; build with `make CFLAGS=-no-pie` to turn one into a line with addr2line.

What is left is two blocks of thirty-two bytes per instance from one allocation site, which the same report shows and nobody has chased. Sixty-four bytes an instance moves the ceiling from sixty-two instances to about four million, so it is a real leak that no caller will meet.

The number is 63 in both forms of the rules, which says the leak is in the engine rather than in either rule table. What differed until 22 August 2026 was what happened at that point: the interpreter answers nought and the engine goes quiet, because `run_bytecode` checks whether it got a frame, while a rule written as C went straight to `memset` on the nought it had been handed and faulted. So exhausting the arena crashed the compiled build and merely silenced the interpreted one. The decompiler now emits the same check the interpreter has, and both answer nought.

That is worth knowing beyond this one fault: the two rule forms are held against each other over the 81 cases and by `tools/delta-check.sh`, and neither of those speaks through more than one instance, so a difference that only shows when something runs out was invisible to both. `make instances` is the only check that makes a second instance at all.

One thing the trace check turned up is a fault of ours, and is not fixed. Tracing at the level that prints every call costs twenty times what the synthesis does, and feeding the synthesis that slowly faults part way through a run of several sentences, with less audio written than there should be. Any one sentence is fine. Nothing but a trace makes the engine that slow, so it is not in the way of anything, but it is a fault and this is where it is written down.

The reference binary is not steady on a case with a marker in it. Usually it hangs, and `test/compare.sh` retries a case once on its own before reporting it as hung, because calling that a difference cost false alarms. Sometimes it answers with different samples instead, which the retry cannot tell from a real difference: six runs of one German audio-marker case gave the same samples from ours every time and a different one once from IBM's. A marker case that differs once and not again is that, not a change in the engine.

The sixty-four bit build maps a region low in memory, because the Delta machine keeps addresses in thirty-two bit values and everything it can point at has to be nameable in one. The program itself may be loaded anywhere: the language's data is copied into that region at startup rather than named where it lies. A machine that cannot map anything below two gigabytes would need a different answer, and would say so rather than misbehave.

If the audio sounds wrong to you, it is not a fault in the port: our output is identical to IBM's. Changing it is a deliberate change to the language data, and the suite will correctly report that as a difference.
