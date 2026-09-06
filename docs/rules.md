# The rules

**This describes the format as it is, not as it should be.** Authoring in it is harder than it should be -- the surface is Delta's own model rather than anything designed to be written -- and `docs/authoring.md` sets out why, what to do about it, and what has to be settled before starting. Read that before doing any large amount of work in the form below.

A language's rules are the whole of what it knows about its own spelling, and they are the only part of a module that exists in three forms at once: as text in the tree, as bytecode the interpreter runs, and as C the compiler compiles. This says what each form is, how one becomes another, and how a rule of ours is written and proved. `docs/language.md` is everything else in a module.

## The rules as text

`lang/<tag>/rules` holds a language's rules as text, one file to an object, written by `tools/rules/notation.py`. Every module in the tree has one -- 21,075 rules over the nine, from English's 3,377 down to Italian's 1,749 -- and `EVV_NOTATION_LANG` says which language every target below reads, English by default. This is the form to read a rule in, and it is meant to become the form to *change* one in.

    rule eng_ph_Z_dur from es_cdur.obj
    shape frame 196 argbase 8 params 1
    label L0 was _eng_ph_Z_dur
      alu andl imm 0 slot -4
      push slotaddr -104
      call setjmp3 arity 2 depth 2
      cmp testl reg r0 reg r0
      load movl state 0 into r6
      branch jne to L1

One operation to a line, the verb first, and an operand is one or two words -- so a line can be read straight through with nothing to keep track of. Nothing is carried by indentation and nothing needs punctuation counted. Registers are the machine's eight, `r0` to `r7`, with `w`, `b` or `h` for how much of one is meant. Blocks are numbered; `was ...` on the label line is what the block was called in IBM's object, which is only useful while rules are still being lifted and is ignored when the text is read.

It is one to one with what the machine does, which is the point: it holds registers, the argument stack and the backtracking as they are rather than tidying them into loops and conditionals. The readable C that `tools/rules/decompile.py` writes is the other form, for reading rather than for round-tripping, and inverting that exactly would be hard.

    make notation

writes the text out of IBM's objects again, and

    make notation-check

holds what is in the tree against those objects rule by rule: each is emitted twice, once from the text and once from a fresh lift, and the bytecode has to match. That is what says which rules have been changed on purpose -- an unedited rule matches and an edited one is named, which is what somebody changing a rule needs to be told.

    make notation-prove

is the stronger check and the one to believe. It emits three whole streams -- a fresh lift of IBM's objects, that lift written out as text and read back, and the text as it stands in the tree -- and all three have to agree. The pools the rules draw on, the constants and strings and entry points and tag maps, are shared across the whole language and numbered in the order the rules are taken, so reproducing a stream byte for byte says the text carries every rule, in order, with nothing added and nothing left out. A rule-by-rule comparison cannot say that. It matches for all eight lifted languages: English's 1,496,807 bytes, British English's 1,466,417, German's 1,168,990, Canadian French's 1,107,281, French's 1,083,599, Spain's Spanish 744,858, American Spanish's 742,075 and Italian's 709,771, measured on 2 September 2026. Polish is the one it cannot be run on, because its rules are written rather than lifted and there are no Polish objects to lift against; there the suite and an ear are the check, as they are for anything nobody compiled before.

Both want IBM's objects, so they are in the same class as the suite: obtainable, and not needed to build.

## The rules a build compiles

Three files a language: `delta_rules_<tag>.c`, which holds the bytecode array and the pools; `delta_rules_<tag>.h`, which numbers every entry; and `delta_rules_shim_<tag>.c`, a stand-in under each rule's own name.

    make rulecode

writes them, and every build runs it first. **They are not in the tree.** The text is the source, and a second copy of the same rules sitting beside it could only ever be the stale one: a rule edited in the text and not written out again would be a change that silently did not happen, and the build would go on compiling yesterday's rules with nothing to say so. It costs two seconds a language, so there is nothing to weigh against that. `.gitignore` says which three, and the one file in a language module that looks like them and is not generated -- `delta_rules_none_<tag>.c`, the empty table `RULES=bytecode` links -- is named there as an exception.

What the build writes is the module as it means itself: the lifted text with its own rules in the upper form compiled in over the top. Every one of them, in every language. `lang/<tag>/rules/trials` names any the build is to pass over, and as of 6 September 2026 no module names anything: it is there to hold a rule out while it is half written, not to hold one out for good.

English named its four until then. They stood in for rules IBM itself compiled, and building them would have put our bytecode where IBM's was and given up the byte identity English was checked by. That property is retired, for all ten languages rather than for English alone -- a rule that let you mend a Spanish rule and forbade the identical English one is a rule people get wrong. What holds the engine now is `test/matrix.sh` over 979 recorded answers and `test/words.sh` over twenty thousand words, and neither asks whose bytecode it is. What was worth keeping was never the bytecode but the ability to ask the original what it did, and `test/suite.sh` still answers that whenever it is asked.

`make upper-check` is unaffected and still runs. It builds both sides itself -- the lifted text alone against the same with every upper-form file compiled in -- whatever `trials` says, so retiring the property changed nothing about it. What it proves has changed name rather than substance: that the compiler renders a rule faithfully, rather than that our bytecode is IBM's.

What taking English's four in would cost is worth stating exactly, because it is not what a person would guess. It would not change the sound: `upper-check` is precisely the check that they enter the same rules, make the same calls with the same values and produce the same samples, so the suite would pass. What it would cost is the claim that English's bytecode is IBM's, which is the thing every other check in this tree is anchored to. That is why the four are named and not merely left to be noticed.

Two other ways of writing the same three files exist and are not what a build wants. `make notation-rewrite` writes the lifted text alone -- IBM's rules and nothing of ours -- and `make authored` writes every `.up` file in, trials and all. They are the two sides `tools/rules/check-upper.sh` builds and holds against each other, and it puts the module's own back when it is done.

A module with no rules as text is the one exception, and `lang/jajp` is it: the commands in `docs/japanese.md` lift it whole and write these three files themselves, so for Japanese alone they are tracked -- they are the only copy there is. The Makefile has a rule for that case whose whole job is to say so rather than leave make reporting a target it does not know how to make.

What made the text a source was one small table. A rule names a constant by a symbol; the bytes behind it are a whole data section of the object it was compiled into, and what the rule gets is an offset into that section. The bytes were already in the tree, in `delta_consts_<tag>.c`. The mapping -- which store and how far in -- was not, and it was the last thing the emitter needed the objects for. It is now `lang/<tag>/rules/symbols`, written by `make notation-symbols`: 76 stores and 6,719 addresses for English, and between 63 and 90 stores for each of the others.

So the rules are built from text a person can read and change, every time, and IBM's objects are wanted for the comparison suite and for nothing else.

## What a rule stands for

`lang/enus/rules/wrappers.up` is the beginning of the upper layer: a rule as what it means rather than as what the machine does to arrive at it.

    wrapper ZZbspush_ca__12 takes 1
      bspush_ca 12
    wrapper ZZget_parm_ptr2 takes 5
      get_parm arg 1 arg 2 -6
      get_parm arg 3 arg 4 -6
    wrapper ZZlprp_load__insert_2pt_i_7_2_ZZstring2 takes 3 answering truth
      lpta_rpta_loadp arg 1 arg 2
      insert_2pt_i 7 2 ZZstring2 0

Every one takes the machine's state as its first argument, so that is not written; `arg n` is the wrapper's own nth, `as byte` or `as half` widens one before it is handed over, and `answering truth` is the three operations that turn whatever came back into nought or one. The name of a wrapper already spells its arguments -- `ZZtest_string_s_2_1_ZZstring480` -- so this only says out loud what the name is spelling.

    make upper
    make upper-prove

`upper` writes it and `upper-prove` checks it: each is compiled back down and the bytecode has to match the lower form byte for byte. 1,954 of the 2,335 wrappers are there and all 1,954 match.

**It writes only what it can reproduce exactly.** 381 wrappers are left in the lower form, of which 371 do not fit the shape at all and ten do fit but widen an argument, and the original compiler put that load where it suited it rather than always in one place. Where this cannot reproduce the placement, an upper form would be a description that is not the rule, so the rule stays as it is. That is the whole discipline of the thing: byte-identity is not a nicety here, it is what makes a re-description of an existing rule worth having.

What is deliberately not attempted is the 1,042 real rules. Those are programs: a median of 28 calls over 15 blocks, 1,058 distinct shapes between them, and only 12% fitting even a loose template of tests and ordinary actions. Only 4% merely test and assign. For those the readable form is the C `tools/rules/decompile.py` writes, and the naming it already does -- which primitive a wrapper stands for, which variable a reach touches, which alternative an arm is -- is the win. A declarative form would not fit them and pretending otherwise would cost the exactness that makes any of this checkable.

The other use of an upper layer is the one that has nothing to be identical to: writing rules that do not exist yet, which is what Polish needs. There the check is the suite and an ear, not a byte comparison, so the constraint above does not bind.

## Writing a rule

One trap in the decompiler before any of this. `tools/rules/decompile.py` with no arguments writes the hundred smallest rules, and it writes them to the same file `all` writes to -- so reading its usage by running it truncates the language's rules-as-C from every rule to a hundred. That file is gitignored, so nothing says so, and the next default build links it and aborts on the first rule that is missing: `init_platform was not written as C and this build has no bytecode to run it as`. The answer is `tools/rules/decompile.py all` again, and the lesson is to read the usage in the file.

`lang/enus/rules/*.up` beside the `.dr` files is the form to write a rule in. It is the same rule: every call is the same entry with the same arguments in the same order, because a form that reworded what a rule calls would be describing the rule rather than being it. What it takes over is the machine.

That is where the length of the lower form goes. Of the 322,890 operations in English's 1,042 real rules, 97,071 are pushes, 60,947 calls and 43,893 pops -- two thirds of every rule is the argument stack being written out by hand -- and another 53,000 are a comparison setting the flags on one line and a branch reading them on the next. None of it says anything about a language. `eng_ph_F_dur` is 49 lines of the lower notation and this is all of it:

    rule eng_ph_F_dur takes 1 from es_cdur.obj
      call ZZfence_null
      set global half 2226 to 20
      set global half 3150 to 5
      match
    end

The 43 lines that went are the landing place, the `ventproc` entry, the `vretproc` tail with its 94, the pushes, the pops, the register that carries the answer and the return.

A line is a verb and then its words, one operation to a line, and a block ends at a bare `end`, so nothing depends on indentation and no line has to be read together with another. `tools/rules/upper.py lower <file>` prints what a file compiles to, which is the way to read what the compiler did.

What a rule can say. `local <name>` gives it a word of its frame and `local <name> bytes <n>` gives it more; `variable <name> <width> <offset>` names a state variable so the body can call it something. `call <entry> <values>...` makes a call and leaves the answer in `answer`. `set <place> to <value>`, and `add`, `subtract`, `and`, `or`, `shift left`, `shift right`, `increment`, `decrement` and `negate` for the arithmetic. `put <value> into <value> at <n>` writes through a pointer, which is how a rule answers something to whatever called it: the machine cannot store from one place in memory to another, so both ends go through a register, and neither of them is the one the answer is in. `if <test>` with `else` and `end`, and `while <test>` with `leave` and `again`. `match` and `give up` are the two ways out, `answer <value>` for a rule that leaves something else, and `raw` takes a line of the lower notation for the operations too rare to have a word here -- the nine rules that read a table, the little floating point the Frenches have, and anything else.

A value is one or two words: a number, `arg <n>`, a local by name, `addr <name>` for where a local is, `cell <name> <part>` for one of the three parts of a local the machine has written -- its `kind`, its `field` or its `value` -- `global <width> <offset>`, `sym <name>`, `answer`, `state`, or `unwind`. A test is two values with a comparison between them -- `is`, `is not`, `is less than`, `is at least`, `is more than`, `is at most`, and `is below`, `is above`, `is not below`, `is not above` for the unsigned ones -- or a value on its own, which means it is not nothing.

Two things about it are the machine's and are easy to get wrong. `arg 1` is the first argument after the state, because the state is every rule's first argument and `takes 3` counts it: a rule that says `takes 3` has `arg 1` and `arg 2`. And a local the machine is handed the address of has to be as big as the machine writes -- `get_parm` fills in a compiled location, which is eight bytes, so it wants `local word bytes 8` and a four-byte local would take the next one with it. Nothing in the compiler knows how much any entry writes.

### A phoneme, and the record that says what it is

A phoneme is in three places and `tools/module/phonemes.py <tag>` prints all three beside each other: its name is a value of the phone statement's first field, which is the list the rules index by; its numbers are a `Phoneme` line in the settings, four bytes of name and eleven values, read when a caller sends phonemes rather than text; and what it sounds like is a rule named for it which sets its source parameters and calls one locus rule, where the formant targets are.

Beside those it carries a record, in the `variants` bytes of its own statement exactly as a letter does in the input statement's. The statement says how long one is -- `at start stride` -- and the fields it covers are its own, after the name and less `afterslash` where it has one. For the phone statement that is eight: class, voicing, sonority, manner of articulation, place of articulation, and the three a vowel wants. Those are not decoration. `place_of_artic` runs lab, alv, pal, vel and ret, and it is where a language says that its sz is retroflex and its s is not.

    tools/module/phonemes.py set plpl L manner_of_artic=fric place_of_artic=ret

writes one by name, which is the point: read eight bytes by eye and a fricative quietly becomes a lateral.

What that is for is adding a sound, and the answer there is usually not to add one. A phoneme code is tested by name in some five hundred places in a module -- every locus rule asks what its neighbours are, every vowel rule asks which consonant follows it, the durations and the syllabifier ask too -- and a code that has never existed is invisible to all of them, so its neighbours are coarticulated as if it were absent and its durations come from nowhere. A code that already exists has an arm in every one of those chains. Polish needed two sounds Italian has not got and took over two Italian has that no Polish word can reach, which cost two records, four call names and one rule.

### The frame, and the places a rule backtracks to

A rule hands the machine five places in its own frame and the machine writes to all five, so their sizes are not ours: the record `ventproc` saves is 92 bytes, the landing place is 64, and the three fence arrays are 12 each, which is a byte per statement type and ten is all English declares. Those five sit together as one block of 192 bytes, the locals above it, and the last word of the frame is the count `backtrack_function` is handed. All 1,042 of IBM's rules lay that block out the same way -- the record, then the landing 64 bytes in, then the three arrays at 156, 168 and 180 -- and what varies is only where the block sits and which of two arrays it hands over first, 532 rules one way and 510 the other, which says that pair is scratch either way. `eng_ph_F_dur` above comes out with the frame IBM gave it, 196 bytes with the landing at -104 and the record at -196, from a rule that says nothing about any of it.

The backtracking is the other half. A rule plants a choice point carrying a small number and later asks `backtrack_function` what number came back; the answer says where to carry on, and -1, which is what the rule's own marker answers, says it has run out of alternatives. So `plant test <place>` plants one -- `plant choice <place>`, `plant scan <place>` and `with boa` on the end are the other kinds -- `place <name>` says where one carries on, `go to <place>` is a jump to one, and `backtrack` asks. `matched` and `gave_up` are places every rule has, so a plant may name either. The numbers are the compiler's business, which is the point: `has_lex_prefix` has six of them across two alternatives and a shared tail, and a wrong one there is a mispronunciation nobody would find by reading.

A rule re-expressed from one of IBM's needs its numbers rather than ours, and they are not always ours to choose: `high_tone` plants 1, 2 and 4 and dispatches on 3 as well. `plant test <place> as <n>` states the number and `place <name> on <n>` binds a place to one that nothing in the rule plants. The chain the compiler writes then steps from 1 to the highest of them, which is how the answer is read -- a decrement leaves nothing when the answer was the number of decrements so far -- and a number with nowhere to go costs the decrement and no branch.

`bare` in a rule's declarations gives it the shape the language's own wrappers have: no landing place, no `ventproc`, no `succeed`, no frame unless it declares a local, and `answer` as its only way out. 1,037 of Italian's 1,749 rules are that shape and a rule of ours that stands where one of them stands has to be too -- the choice points around the call belong to the rule that planted them, and a `succeed` of ours commits them. What that costs when it is missed is worth knowing: the first version of Polish's `pol_test_own_letters` was an ordinary rule, nothing was visibly wrong, and a word with no vowel in it crashed several rules later in the durations.

`through wrappers` in a rule's declarations makes a plant call the wrapper rule of that name -- `ZZstarttest2` rather than `starttest 2`. That is only for a rule re-expressed from IBM's own: a wrapper is a rule, so a run says it was entered, and a rule that skipped it would do the same work and say something different.

### Whether it is the same rule

    make upper-check

is what says so, and there is no byte comparison in it. There was never going to be: our compiler would have to make the same register choices and put the instructions in the same order as IBM's, which is a study of their compiler rather than of this engine, and it would forbid us writing anything they never wrote. `eng_ph_F_dur` says it in one line -- theirs pushes the state register, does the two stores and then calls `succeed`, and anything straightforward does the stores and then the push.

So the standard is what the engine can observe. With tracing on it says every rule it enters and every call it makes with its arguments, and that is what the audio is made of. `tools/rules/check-upper.sh` speaks each case through a build carrying the authored rules and through one carrying IBM's, and those have to match, and the audio besides.

Four rules are in the tree that way, chosen for their shapes rather than their size. `eng_ph_F_dur` is a body with no alternatives in it. `has_lex_prefix` is two alternatives, a tail they share and a dispatch through six planted places. `high_tone` carries a value from one alternative into a test they share, and has a gap in its planted numbers -- IBM's compiler planted 1, 2 and 4 and dispatches on 3 as well -- so its numbers are stated rather than allocated and one place is bound to a number nothing plants. `clear_delta` is the loop: the language's loops are backtracking loops, where the body is reached by the machine answering an alternative rather than by falling into it, so a place inside a `while` is what says one. All four come out the same, call for call, over 7,986,891 lines of trace.

Polish's rules in `lang/plpl/rules/it_phone.up` are what exercise the rest of it: `if` with a call's answer in it, `plant test` and `backtrack` for a letter rule's alternatives, `go to` a shared tail, `put cell right value into arg 2 at 4` for the position a rule answers to its caller, and `bare`. What still nothing in the tree exercises is `else`, `leave`, `again` and the unsigned comparisons, because nothing IBM wrote has that shape and nothing written for Polish has needed it yet: those compile and can be read, and neither is the same as having run. Nothing in the machine is beyond the form -- what has no word here has `raw` -- but a statement proved through the engine and a statement merely compiled are not the same thing, and this says which is which.

The sentences are the suite's seven plain ones and `test/cases/upper.txt`, which is this harness's own; `EVV_UPPER_CASES` names another list, which is how the workflow runs the short one. The seven were not enough, and why is worth more than the fix. `has_lex_prefix` takes one alternative when the word carries the prefix "re" and another when it does not, and not one of the seven has such a word: with its action number changed from 351 to 352 on purpose, all seven still passed. The way to know a case reaches the rule is to trace one sentence and look for the value -- `ZZlprp_load__setd(..., 0000015f)` comes up 28 times in "The rewritten prefix was remade and reopened" and never in the seven.

Three things are left out of the comparison and all three are the harness. The interpreter prints every store it makes, and an authored rule may keep a value somewhere else -- `has_lex_prefix` keeps in a local what IBM's kept on the argument stack -- so the stores are held against each other and reported rather than required. It remarks when the depth a call carries disagrees with the area's, which is IBM's compiler batching its pops and ours not. And addresses in the arena are masked, because a frame with different locals in it lands somewhere else.

The audio is the third comparison and it is not the weakest of them. A rule whose whole effect is to write a variable is invisible to a trace of calls, and that is not hypothetical: setting `eng_ph_F_dur`'s duration to 21 where IBM sets 20 passes the trace on every sentence and changes the sound of the second. Sabotage a rule and see which check answers, and if none of them does, the cases do not reach it.

A rule a module claims gets into a build by being there: an ordinary build compiles the module as it means itself, which is the lifted text with its own `.up` rules over the top, and every one of Polish's fifty-five is in that way. What a module does not claim it names in `lang/<tag>/rules/trials`, and English's four are named there -- they stand in for rules IBM compiled and are kept to be checked against them, not to be built.

    make notation-rewrite
    make authored

are the two ends of that, IBM's rules alone and every `.up` file in whatever the module says, and they exist because `tools/rules/check-upper.sh` builds both and holds them against each other. Running either by hand leaves the tree holding rules an ordinary build did not ask for, so `make rulecode` puts it back.

### Bytes of our own

A rule that tests text names the bytes it tests against by address, and until there was a compiler every one of those came out of IBM's objects with the rule that named it. `lang/<tag>/rules/constants` is where one of ours goes:

    bytes lex_prefix_re 18 02

`make constants` writes it into `delta_authored_<tag>.c`, which is the one file in a language module that no lifter writes, and records where it falls in `rules/symbols`. A rule then says `sym lex_prefix_re` and nothing else has to know. Startup copies that store into the arena beside the lifted ones, because the machine holds addresses in thirty-two bit values and an address in the program is not one of those; `src/delta/delta_low.c` is where both lists are walked. A new store is named in the generated rules file as well, so `make notation-rewrite` goes with it.

The bytes are bytes. A string a rule holds against the text being read is not ASCII: it is one code per character in the alphabet the statement type declares, which `tools/module/lexicon.py` prints for a language. `text <name> "..."` is there for the ones that really are ASCII.

`rules/symbols` names an address by the object that compiled it and the symbol it had there, which is what a rule holds; a constant of ours belongs to the language rather than to an object and is recorded against none, so any rule may name it. That file used to be a list in order, which was only right as long as no rule was ever added or written afresh -- a rule naming a constant nothing had named before would have been handed an index past the end of the table and read whatever lay after it. It says so now instead.

What proves the naming, as against the linking, is a rule reading our copy of bytes IBM also has. `lex_prefix_re` is IBM's own two-byte prefix as `ZZstring278` holds it: the codes 24 and 2, which in the alphabet statement type 1 declares spell "re", and `tools/module/lexicon.py` is what says so. A `has_lex_prefix` that calls `test_string_s 1 2 sym lex_prefix_re` where IBM's calls the wrapper for `ZZstring278` therefore has to sound exactly the same, and `tools/rules/check-upper.sh -sound` is how that is asked: the audio is the standard and the trace is reported instead of required, since the wrapper is a rule and a run of IBM's says it was entered. On 23 August 2026 all nine sentences came out identical to the sample, with the traces 18 to 87 lines apart out of between 505,443 and 1,386,180 -- the wrapper being entered and answering, at each of the sites where it is called, and nothing else.

How far apart is said with the running count of rules entered masked off, which is what that harness does. A trace one entry short differs in the count on every line after it, so the raw figure is the length of the trace rather than the size of the difference: the same sentences read as 178,356 to 475,222 lines apart before the mask went in, which is a hundredth of the truth about them.

Nothing in the tree names the constant, so what every build proves is the path -- the store compiled, registered and copied into the arena -- and the measurement above is what proved the name.

## The rules, twice

The language's rules exist in the tree as bytecode, and the engine has an interpreter for them. They also exist as C: `tools/rules/decompile.py` writes all 3,377 of them out of that same bytecode into `lang/enus/delta_rules_c00_enus.c` and thirty-one more beside it, and the interpreter prefers a rule written as C wherever it finds one. It writes beside whichever language it was pointed at, so `make LANG=lang/dede rules` writes German into `lang/dede`.

Thirty-two files rather than one because a translation unit cannot be compiled on more than one core, and thirteen megabytes of it is seven minutes. The decompiler deals the rules out by size so the files finish together; each carries its own piece of the table of rules-written-as-C and the first gathers the pieces, which is what lets every rule stay `static` and keeps one language's names from meeting another's. `PARTS` in the Makefile and `EVV_RULE_PARTS` in the decompiler have to agree, because the build names the files it expects rather than looking for whatever is there -- and the recipe deletes the old ones first, so lowering the number does not leave yesterday's files to be compiled in beside today's.

Both speak the same samples. That is not a hope: `test/suite.sh` holds each form against IBM's binary over all 81 cases, and the two forms are set against each other call by call by `tools/rules/check-c.sh`. So which one is linked is a trade of build time and size against speed, and nothing else.

Every rule of every language in the tree is written as C, with nothing refused: 3,377 of 3,377 for US English, 3,395 for British, 2,600 for German, 2,378 and 2,386 for the Frenches, 1,724 and 1,717 for the Spanishes, 1,749 for Italian and 1,804 for the Polish chassis. So the interpreter is not a fallback anything depends on any more, and a `RULES=c` build carries none of it: the bytecode, the constants it names and the tables it jumps through come to a megabyte and a half a language, and the language table says nought where it used to name them so the linker can drop the lot.

What `RULES=bytecode` is for, then, is not building. It is the second opinion. `tools/rules/check-c.sh` is the only check finer than the audio -- it speaks a sentence twice, once each way, and holds every rule entered and every call made with its arguments against the other -- and that check exists only while there is something to compare against. The auxiliary harnesses use it too, for the plain reason that it builds in half a minute. Retiring it would save nothing that ships and would cost the decompiler its oracle.

C is the default, because the speed is the part a person waiting for speech feels. Measured on one machine, the same long sentence, bytecode against C: the whole utterance synthesises in 138 ms against 63; the wait before the first samples of an utterance is 38 ms against 12; and interrupting an utterance and asking for another costs 124 ms against 39. That last one matters most and is the least obvious: the engine cannot abandon an utterance it has been told to stop -- see the interrupting section of `docs/status.md` for why not -- so what a cancel costs is whatever is left of the work, and compiled rules do that leftover work in a third of the time.

What it costs is the build. The C is thirteen megabytes: a little over two minutes of Python to write and about fifteen seconds to compile over twenty-four cores, where the bytecode build wants half a minute and the two seconds a language that writing the rules out of the text costs either way. The binaries are about twice the size -- `build/probe` is 7.1 MB against 3.7 -- because that is what a machine's worth of lifted code looks like written out as C.

    make RULES=bytecode

is therefore the one to build while working on anything but the rules, and

    make RULES=c

says the default out loud, which is worth doing in a script. `make rules` writes the file without building anything. It is not kept in the tree, because every change to the decompiler rewrites the whole of it.
