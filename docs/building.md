# Building openevv

## What you need

A C compiler and Python 3. The language data is in the tree, so there is no IBM SDK to find and nothing is downloaded. Python is wanted twice. Every build writes the rules the engine runs out of the text in `lang/<tag>/rules`, which is about two seconds a language and is where those rules live now; and an ordinary build then decompiles them into C, which is two minutes and is what `RULES` below is about. `make RULES=bytecode` skips the second and not the first.

Two more things are wanted only for particular jobs. A thirty-two bit compiler builds the thirty-two bit engine. Wine and IBM's own objects run the comparison tests, which is the only automatic check that the audio is right.

On this machine all of those come from the flake, and `nix develop` puts them on the path.

## Building

    make

That builds `build/libevv.a` and `build/evv`, which speaks. From nothing, that is about two and a half minutes: two seconds to write the rules out of the text, a little over two minutes for Python to decompile them into C, and about fifteen seconds to compile the thirteen megabytes of it across twenty-four cores. Once those files exist they are not written again unless the text, the decompiler or the bytecode changes.

    make RULES=bytecode

That is the small, quick build -- half a minute on one core, under twenty seconds with `make -j8`, plus the two seconds a language the rules cost whichever form they are in. It speaks the same samples and it is the one to use while working on anything but the rules. What it costs is speed, which the next section puts numbers to.

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

`lang/<tag>/rules` holds a language's rules as text, one file to an object,
written by `tools/delta-notation.py`. Every module in the tree has one --
21,075 rules over the nine, from English's 3,377 down to Italian's 1,749 --
and `EVV_NOTATION_LANG` says which language every target below reads, English
by default. This is the form to read a rule in, and it is meant to become the
form to *change* one in.

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

is the stronger check and the one to believe. It emits three whole streams --
a fresh lift of IBM's objects, that lift written out as text and read back,
and the text as it stands in the tree -- and all three have to agree. The
pools the rules draw on, the constants and strings and entry points and tag
maps, are shared across the whole language and numbered in the order the rules
are taken, so reproducing a stream byte for byte says the text carries every
rule, in order, with nothing added and nothing left out. A rule-by-rule
comparison cannot say that. It matches for all eight lifted languages:
English's 1,496,807 bytes, British English's 1,466,417, German's 1,168,990,
Canadian French's 1,107,281, French's 1,083,599, Spain's Spanish 744,858,
American Spanish's 742,075 and Italian's 709,771, measured on 2 September
2026. Polish is the one it cannot be run on, because its rules are written
rather than lifted and there are no Polish objects to lift against; there the
suite and an ear are the check, as they are for anything nobody compiled
before.

Both want IBM's objects, so they are in the same class as the suite:
obtainable, and not needed to build.

## The rules a build compiles

Three files a language: `delta_rules_<tag>.c`, which holds the bytecode array
and the pools; `delta_rules_<tag>.h`, which numbers every entry; and
`delta_rules_shim_<tag>.c`, a stand-in under each rule's own name.

    make rulecode

writes them, and every build runs it first. **They are not in the tree.** The
text is the source, and a second copy of the same rules sitting beside it
could only ever be the stale one: a rule edited in the text and not written
out again would be a change that silently did not happen, and the build would
go on compiling yesterday's rules with nothing to say so. It costs two seconds
a language, so there is nothing to weigh against that. `.gitignore` says which
three, and the one file in a language module that looks like them and is not
generated -- `delta_rules_none_<tag>.c`, the empty table `RULES=bytecode`
links -- is named there as an exception.

What the build writes is the module as it means itself: the lifted text with
its own rules in the upper form compiled in over the top. Which those are is
the module's to say, in `lang/<tag>/rules/trials`, which names the `.up` files
it keeps but does not build. English names its four, because they stand in for
rules IBM itself compiled and are kept so that `make upper-check` has
something to hold against those; building them would put our bytecode where
IBM's is and give up the byte identity English is checked by. Polish names
none, because all fifty-five of its written rules are its own. Silence means
the module's own, because that is what a module being written wants and
writing modules is what the form is for.

What taking English's four in would cost is worth stating exactly, because it
is not what a person would guess. It would not change the sound: `upper-check`
is precisely the check that they enter the same rules, make the same calls
with the same values and produce the same samples, so the suite would pass.
What it would cost is the claim that English's bytecode is IBM's, which is the
thing every other check in this tree is anchored to. That is why the four are
named and not merely left to be noticed.

Two other ways of writing the same three files exist and are not what a build
wants. `make notation-rewrite` writes the lifted text alone -- IBM's rules and
nothing of ours -- and `make authored` writes every `.up` file in, trials and
all. They are the two sides `tools/upper-check.sh` builds and holds against
each other, and it puts the module's own back when it is done.

A module with no rules as text is the one exception, and `lang/jajp` is it:
not in the tree at all, lifted whole by the commands in `docs/japanese.md`,
which write these three files themselves. The Makefile has a rule for that
case whose whole job is to say so rather than leave make reporting a target it
does not know how to make.

What made the text a source was one small table. A rule names a constant by a
symbol; the bytes behind it are a whole data section of the object it was
compiled into, and what the rule gets is an offset into that section. The bytes
were already in the tree, in `delta_consts_<tag>.c`. The mapping -- which store
and how far in -- was not, and it was the last thing the emitter needed the
objects for. It is now `lang/<tag>/rules/symbols`, written by
`make notation-symbols`: 76 stores and 6,719 addresses for English, and
between 63 and 90 stores for each of the others.

So the rules are built from text a person can read and change, every time,
and IBM's objects are wanted for the comparison suite and for nothing else.

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

## Writing a rule

One trap in the decompiler before any of this. `delta-decompile.py` with no arguments writes the hundred smallest rules, and it writes them to the same file `all` writes to -- so reading its usage by running it truncates the language's rules-as-C from every rule to a hundred. That file is gitignored, so nothing says so, and the next default build links it and aborts on the first rule that is missing: `init_platform was not written as C and this build has no bytecode to run it as`. The answer is `delta-decompile.py all` again, and the lesson is to read the usage in the file.

`lang/enus/rules/*.up` beside the `.dr` files is the form to write a rule in. It is the same rule: every call is the same entry with the same arguments in the same order, because a form that reworded what a rule calls would be describing the rule rather than being it. What it takes over is the machine.

That is where the length of the lower form goes. Of the 322,890 operations in English's 1,042 real rules, 97,071 are pushes, 60,947 calls and 43,893 pops -- two thirds of every rule is the argument stack being written out by hand -- and another 53,000 are a comparison setting the flags on one line and a branch reading them on the next. None of it says anything about a language. `eng_ph_F_dur` is 49 lines of the lower notation and this is all of it:

    rule eng_ph_F_dur takes 1 from es_cdur.obj
      call ZZfence_null
      set global half 2226 to 20
      set global half 3150 to 5
      match
    end

The 43 lines that went are the landing place, the `ventproc` entry, the `vretproc` tail with its 94, the pushes, the pops, the register that carries the answer and the return.

A line is a verb and then its words, one operation to a line, and a block ends at a bare `end`, so nothing depends on indentation and no line has to be read together with another. `tools/delta-upper.py lower <file>` prints what a file compiles to, which is the way to read what the compiler did.

What a rule can say. `local <name>` gives it a word of its frame and `local <name> bytes <n>` gives it more; `variable <name> <width> <offset>` names a state variable so the body can call it something. `call <entry> <values>...` makes a call and leaves the answer in `answer`. `set <place> to <value>`, and `add`, `subtract`, `and`, `or`, `shift left`, `shift right`, `increment`, `decrement` and `negate` for the arithmetic. `put <value> into <value> at <n>` writes through a pointer, which is how a rule answers something to whatever called it: the machine cannot store from one place in memory to another, so both ends go through a register, and neither of them is the one the answer is in. `if <test>` with `else` and `end`, and `while <test>` with `leave` and `again`. `match` and `give up` are the two ways out, `answer <value>` for a rule that leaves something else, and `raw` takes a line of the lower notation for the operations too rare to have a word here -- the nine rules that read a table, the little floating point the Frenches have, and anything else.

A value is one or two words: a number, `arg <n>`, a local by name, `addr <name>` for where a local is, `cell <name> <part>` for one of the three parts of a local the machine has written -- its `kind`, its `field` or its `value` -- `global <width> <offset>`, `sym <name>`, `answer`, `state`, or `unwind`. A test is two values with a comparison between them -- `is`, `is not`, `is less than`, `is at least`, `is more than`, `is at most`, and `is below`, `is above`, `is not below`, `is not above` for the unsigned ones -- or a value on its own, which means it is not nothing.

Two things about it are the machine's and are easy to get wrong. `arg 1` is the first argument after the state, because the state is every rule's first argument and `takes 3` counts it: a rule that says `takes 3` has `arg 1` and `arg 2`. And a local the machine is handed the address of has to be as big as the machine writes -- `get_parm` fills in a compiled location, which is eight bytes, so it wants `local word bytes 8` and a four-byte local would take the next one with it. Nothing in the compiler knows how much any entry writes.

### A phoneme, and the record that says what it is

A phoneme is in three places and `tools/lang-phonemes.py <tag>` prints all three
beside each other: its name is a value of the phone statement's first field,
which is the list the rules index by; its numbers are a `Phoneme` line in the
settings, four bytes of name and eleven values, read when a caller sends
phonemes rather than text; and what it sounds like is a rule named for it which
sets its source parameters and calls one locus rule, where the formant targets
are.

Beside those it carries a record, in the `variants` bytes of its own statement
exactly as a letter does in the input statement's. The statement says how long
one is -- `at start stride` -- and the fields it covers are its own, after the
name and less `afterslash` where it has one. For the phone statement that is
eight: class, voicing, sonority, manner of articulation, place of articulation,
and the three a vowel wants. Those are not decoration. `place_of_artic` runs
lab, alv, pal, vel and ret, and it is where a language says that its sz is
retroflex and its s is not.

    lang-phonemes.py set plpl L manner_of_artic=fric place_of_artic=ret

writes one by name, which is the point: read eight bytes by eye and a fricative
quietly becomes a lateral.

What that is for is adding a sound, and the answer there is usually not to add
one. A phoneme code is tested by name in some five hundred places in a module --
every locus rule asks what its neighbours are, every vowel rule asks which
consonant follows it, the durations and the syllabifier ask too -- and a code
that has never existed is invisible to all of them, so its neighbours are
coarticulated as if it were absent and its durations come from nowhere. A code
that already exists has an arm in every one of those chains. Polish needed two
sounds Italian has not got and took over two Italian has that no Polish word can
reach, which cost two records, four call names and one rule.

### The frame, and the places a rule backtracks to

A rule hands the machine five places in its own frame and the machine writes to all five, so their sizes are not ours: the record `ventproc` saves is 92 bytes, the landing place is 64, and the three fence arrays are 12 each, which is a byte per statement type and ten is all English declares. Those five sit together as one block of 192 bytes, the locals above it, and the last word of the frame is the count `backtrack_function` is handed. All 1,042 of IBM's rules lay that block out the same way -- the record, then the landing 64 bytes in, then the three arrays at 156, 168 and 180 -- and what varies is only where the block sits and which of two arrays it hands over first, 532 rules one way and 510 the other, which says that pair is scratch either way. `eng_ph_F_dur` above comes out with the frame IBM gave it, 196 bytes with the landing at -104 and the record at -196, from a rule that says nothing about any of it.

The backtracking is the other half. A rule plants a choice point carrying a small number and later asks `backtrack_function` what number came back; the answer says where to carry on, and -1, which is what the rule's own marker answers, says it has run out of alternatives. So `plant test <place>` plants one -- `plant choice <place>`, `plant scan <place>` and `with boa` on the end are the other kinds -- `place <name>` says where one carries on, `go to <place>` is a jump to one, and `backtrack` asks. `matched` and `gave_up` are places every rule has, so a plant may name either. The numbers are the compiler's business, which is the point: `has_lex_prefix` has six of them across two alternatives and a shared tail, and a wrong one there is a mispronunciation nobody would find by reading.

A rule re-expressed from one of IBM's needs its numbers rather than ours, and they are not always ours to choose: `high_tone` plants 1, 2 and 4 and dispatches on 3 as well. `plant test <place> as <n>` states the number and `place <name> on <n>` binds a place to one that nothing in the rule plants. The chain the compiler writes then steps from 1 to the highest of them, which is how the answer is read -- a decrement leaves nothing when the answer was the number of decrements so far -- and a number with nowhere to go costs the decrement and no branch.

`bare` in a rule's declarations gives it the shape the language's own wrappers have: no landing place, no `ventproc`, no `succeed`, no frame unless it declares a local, and `answer` as its only way out. 1,037 of Italian's 1,749 rules are that shape and a rule of ours that stands where one of them stands has to be too -- the choice points around the call belong to the rule that planted them, and a `succeed` of ours commits them. What that costs when it is missed is worth knowing: the first version of Polish's `pol_test_own_letters` was an ordinary rule, nothing was visibly wrong, and a word with no vowel in it crashed several rules later in the durations.

`through wrappers` in a rule's declarations makes a plant call the wrapper rule of that name -- `ZZstarttest2` rather than `starttest 2`. That is only for a rule re-expressed from IBM's own: a wrapper is a rule, so a run says it was entered, and a rule that skipped it would do the same work and say something different.

### Whether it is the same rule

    make upper-check

is what says so, and there is no byte comparison in it. There was never going to be: our compiler would have to make the same register choices and put the instructions in the same order as IBM's, which is a study of their compiler rather than of this engine, and it would forbid us writing anything they never wrote. `eng_ph_F_dur` says it in one line -- theirs pushes the state register, does the two stores and then calls `succeed`, and anything straightforward does the stores and then the push.

So the standard is what the engine can observe. With tracing on it says every rule it enters and every call it makes with its arguments, and that is what the audio is made of. `upper-check.sh` speaks each case through a build carrying the authored rules and through one carrying IBM's, and those have to match, and the audio besides.

Four rules are in the tree that way, chosen for their shapes rather than their size. `eng_ph_F_dur` is a body with no alternatives in it. `has_lex_prefix` is two alternatives, a tail they share and a dispatch through six planted places. `high_tone` carries a value from one alternative into a test they share, and has a gap in its planted numbers -- IBM's compiler planted 1, 2 and 4 and dispatches on 3 as well -- so its numbers are stated rather than allocated and one place is bound to a number nothing plants. `clear_delta` is the loop: the language's loops are backtracking loops, where the body is reached by the machine answering an alternative rather than by falling into it, so a place inside a `while` is what says one. All four come out the same, call for call, over 7,986,891 lines of trace.

Polish's rules in `lang/plpl/rules/it_phone.up` are what exercise the rest of it: `if` with a call's answer in it, `plant test` and `backtrack` for a letter rule's alternatives, `go to` a shared tail, `put cell right value into arg 2 at 4` for the position a rule answers to its caller, and `bare`. What still nothing in the tree exercises is `else`, `leave`, `again` and the unsigned comparisons, because nothing IBM wrote has that shape and nothing written for Polish has needed it yet: those compile and can be read, and neither is the same as having run. Nothing in the machine is beyond the form -- what has no word here has `raw` -- but a statement proved through the engine and a statement merely compiled are not the same thing, and this says which is which.

The sentences are the suite's seven plain ones and `test/cases/upper.txt`, which is this harness's own; `EVV_UPPER_CASES` names another list, which is how the workflow runs the short one. The seven were not enough, and why is worth more than the fix. `has_lex_prefix` takes one alternative when the word carries the prefix "re" and another when it does not, and not one of the seven has such a word: with its action number changed from 351 to 352 on purpose, all seven still passed. The way to know a case reaches the rule is to trace one sentence and look for the value -- `ZZlprp_load__setd(..., 0000015f)` comes up 28 times in "The rewritten prefix was remade and reopened" and never in the seven.

Three things are left out of the comparison and all three are the harness. The interpreter prints every store it makes, and an authored rule may keep a value somewhere else -- `has_lex_prefix` keeps in a local what IBM's kept on the argument stack -- so the stores are held against each other and reported rather than required. It remarks when the depth a call carries disagrees with the area's, which is IBM's compiler batching its pops and ours not. And addresses in the arena are masked, because a frame with different locals in it lands somewhere else.

The audio is the third comparison and it is not the weakest of them. A rule whose whole effect is to write a variable is invisible to a trace of calls, and that is not hypothetical: setting `eng_ph_F_dur`'s duration to 21 where IBM sets 20 passes the trace on every sentence and changes the sound of the second. Sabotage a rule and see which check answers, and if none of them does, the cases do not reach it.

A rule a module claims gets into a build by being there: an ordinary build compiles the module as it means itself, which is the lifted text with its own `.up` rules over the top, and every one of Polish's fifty-five is in that way. What a module does not claim it names in `lang/<tag>/rules/trials`, and English's four are named there -- they stand in for rules IBM compiled and are kept to be checked against them, not to be built.

    make notation-rewrite
    make authored

are the two ends of that, IBM's rules alone and every `.up` file in whatever the module says, and they exist because `upper-check.sh` builds both and holds them against each other. Running either by hand leaves the tree holding rules an ordinary build did not ask for, so `make rulecode` puts it back.

### Bytes of our own

A rule that tests text names the bytes it tests against by address, and until there was a compiler every one of those came out of IBM's objects with the rule that named it. `lang/<tag>/rules/constants` is where one of ours goes:

    bytes lex_prefix_re 18 02

`make constants` writes it into `delta_authored_<tag>.c`, which is the one file in a language module that no lifter writes, and records where it falls in `rules/symbols`. A rule then says `sym lex_prefix_re` and nothing else has to know. Startup copies that store into the arena beside the lifted ones, because the machine holds addresses in thirty-two bit values and an address in the program is not one of those; `src/delta_low.c` is where both lists are walked. A new store is named in the generated rules file as well, so `make notation-rewrite` goes with it.

The bytes are bytes. A string a rule holds against the text being read is not ASCII: it is one code per character in the alphabet the statement type declares, which `tools/delta-lexicon.py` prints for a language. `text <name> "..."` is there for the ones that really are ASCII.

`rules/symbols` names an address by the object that compiled it and the symbol it had there, which is what a rule holds; a constant of ours belongs to the language rather than to an object and is recorded against none, so any rule may name it. That file used to be a list in order, which was only right as long as no rule was ever added or written afresh -- a rule naming a constant nothing had named before would have been handed an index past the end of the table and read whatever lay after it. It says so now instead.

What proves the naming, as against the linking, is a rule reading our copy of bytes IBM also has. `lex_prefix_re` is IBM's own two-byte prefix as `ZZstring278` holds it: the codes 24 and 2, which in the alphabet statement type 1 declares spell "re", and `tools/delta-lexicon.py` is what says so. A `has_lex_prefix` that calls `test_string_s 1 2 sym lex_prefix_re` where IBM's calls the wrapper for `ZZstring278` therefore has to sound exactly the same, and `tools/upper-check.sh -sound` is how that is asked: the audio is the standard and the trace is reported instead of required, since the wrapper is a rule and a run of IBM's says it was entered. On 23 August 2026 all nine sentences came out identical to the sample, with the traces 18 to 87 lines apart out of between 505,443 and 1,386,180 -- the wrapper being entered and answering, at each of the sites where it is called, and nothing else.

How far apart is said with the running count of rules entered masked off, which is what that harness does. A trace one entry short differs in the count on every line after it, so the raw figure is the length of the trace rather than the size of the difference: the same sentences read as 178,356 to 475,222 lines apart before the mask went in, which is a hundredth of the truth about them.

Nothing in the tree names the constant, so what every build proves is the path -- the store compiled, registered and copied into the arena -- and the measurement above is what proved the name.

## The tables beside the rules

A language module is the rules and five other things: the variables the machine declares for it, the settings it carries in its own image, the statement table the machine is parameterised by, the lookup sets its dictionary lives in, and the bytes its rules name by address. All five were generated out of IBM's objects and said so at the top. All five have a text form now, beside the dictionary that already had one:

    lang/enus/enus.globals      the variables, 106 lines
    lang/enus/enus.settings     the settings, 83
    lang/enus/enus.statements   the statement table, 905
    lang/enus/enus.sets         the sets and the dictionary actions, 9,750
    lang/enus/enus.consts       the bytes the rules name, 445
    lang/<tag>/<tag>.dict       the words, which tools/delta-dict.py writes

    make tables-dump      writes the four
    make tables-check     the C from each, held against the tree
    make tables-write     the C from each, for real

`tables-check` is the one to believe and it wants no objects: it writes each generated file out of its text into a directory of its own and holds it against what is in the tree, byte for byte. All five match for all nine languages, which is 45 of 45.

Each of the four keeps one writer, and the tool that lifts is the tool that writes. That is the whole discipline: a lifter that reads objects and a reader that reads text hand the same model to the same emitter, so what the text says and what a lift says cannot come out differently formatted, and the round trip is exact rather than approximately right.

What is deliberately not in the text is anything that follows from what is. The variables are a run of kinds -- `word 20`, `short 2`, `compound 1 5` -- and where each one lands and how big a machine of the language is are worked out from them by the same walk `delta_new` does, so English's 794 variables are 95 lines and the state size is derived rather than declared. The statement table's readers and writers are an offset and a width each, and their names follow the order the fields are in, exactly as the original's compiler numbered them: `vfg0000` upwards, one per field, no two fields sharing one across all 58 of English's. The settings' language number is the section that names it read as a family and a dialect. Nothing in any of the four is stated twice.

The dictionaries read for any language now, not only English. `EVV_LANG_DIR`
points `tools/delta-dict.py` and the two tools it leans on at one module, the
same way it points the decompiler, so

    EVV_LANG_DIR=lang/plpl python3 tools/delta-dict.py dump

writes `lang/plpl/plpl.dict`. Italian declares 13 dictionaries with 892 entries
where English declares 28 with 5,945, and the shapes are the same: words to
action numbers, and what an action says in an arm of a rule.

Two things about the sets are worth knowing before touching them. Its text is lifted from the C in the tree and not from IBM's objects, on purpose: the dictionary's three arrays in that file are laid down by `tools/delta-dict.py` out of the words, so the objects hold what the dictionary said before anything was ever added to it. Running the sets lifter over that file is the one thing this repository tells you not to do, and this is why. And its numbers are the language: English declares 511 sets and 28 dictionary actions in 274 kilobytes of entries where Italian declares 153 and 13 in 77.

The statement table is the same shape in every language and that is a measurement rather than an assumption: ten types each, with 57 fields in Italian and both Spanishes, 58 in the two Englishes, 61 in German, 63 in Canadian French and 65 in French.

One thing this found and fixed. `tools/delta-sets.py` had not been able to write the file it generates for some time: the copy in the tree had been brought to the arena's forms during the sixty-four bit work -- `EVV_REF(0)` where the tool still wrote `0` -- and the tool's own comment about the stores had gone stale with it, saying they are copied when what is copied is the table of pointers and the stores are handed over as they lie. English's file had the newer forms and the other seven the older ones. The tool now writes what English's says and the other seven have been brought into line: ten lines each, no data touched, and every one of the eight then regenerates byte for byte.

So a language IBM never shipped is now five text files and a table. The rules in `rules/`, the four above, the words in `<tag>.dict`, and `tools/gen-lang.py` for the one table the engine knows a language by.

## Adding a language

`lang/plpl` is the ninth language in the tree and the first IBM never shipped.
As it stands it is Italian: made by copying `lang/itit`'s text forms and
renaming them, so it speaks Italian under a Polish name. That is not a
placeholder, it is the chassis -- everything after this is a change with
something audible on both sides of it -- and `NOTICE` says what the licence
consequence is, which is that all of it is IBM's Italian until it has been
replaced.

Italian is the template for reasons rather than convenience. Its stress is
predominantly penultimate and Polish's is almost always penultimate. Its five
vowels have no reduction. Its consonants have the affricates ts and dz, tʃ and
dʒ, the palatal nasal that is exactly Polish ń, and a trilled r -- which is the
hardest part of Polish and the part Spanish only half has. And it is the
smallest of the European modules, 1,749 rules against English's 3,377.

What making one takes. Every module in the tree already holds its rules as
text, so a template needs no lifting first:

    cp the template's five text forms and its rules/, with the tag renamed
    a section naming the language, and a library name
    "plpl": "Polish" in tools/gen-lang.py, then run it
    make LANGS="lang/enus lang/plpl" tables-write             the C from the texts

and then it builds and speaks like any other: the three files a build compiles
are written out of the copied text by the build itself. No object is opened at
any point, which is the whole reason the text forms exist.

### The number a language is

A language is a family and a dialect packed into a word, and the family is not
free. Three tables are indexed by it and all three hold eighteen: the standard
voices in `src/eci_voicetable.c`, the dictionary in force in `src/eci_dict.c`
and the romanizers in `src/eci_romanizer.c`. IBM used families one to five and
eight. And four more are spoken for: `rz_isRomExist` says families 6, 10, 11
and 16 have a romanizer, so an instance of one of those is refused outright
when the romanizer is not there -- which is what happened when Polish was first
given family sixteen, and `eciNewEx` answered -21 and nothing else. Polish is
family seventeen, `0x110000`, which is clear of all of it with eighteen left
spare.

### What the language means by its variables

IBM's names for the machine's variables are gone: the only record is a
disassembly that carries kinds and not names. So a rule that sets a formant
says `global half 2926` and nothing tells you what that is. `<tag>.globals` can
now say:

    name short 423 f2_in

and a rule written in the upper form says `set f2_in to 2000`, which compiles
to that same offset. `python3 tools/gen-globals.py where plpl 2926` is how one
is worked out from the other: it answers `short number 423, 2 bytes into it`,
two bytes being where a short cell keeps its value.

The ten that are named in `lang/plpl/plpl.globals` are the formant targets a
consonant is spoken with, each formant twice because the transition into it and
the one out of it are separate numbers. They were read off Italian's own value
rules: the trill sets the first to 450 and the second to 1250, the labials set
the second to 850, the dentals and velars to 1700 and the palatals to 1800 --
which is where a labial's low second formant and a palatal's high one belong,
so the reading is the language's own rather than a guess.

`lang/plpl/rules/is_val.up` is the first rule written for Polish rather than
lifted for Italian, and all it does is say what the alveolo-palatals -- the
series Polish has and Italian has not -- are spoken with. Nothing calls it yet.
Its numbers are a starting point: ś and ź sit between Italian's palatal and its
dentals with a higher third formant, and the ear settles the rest.

### The alphabet, and what each letter says

A language's alphabet is the value names of the input statement's first field:
207 of them for Italian, from `GAP` and the five vowels through the consonants,
the digits, the punctuation and the accented Latin-1 characters. And beside it,
in the same statement, the `variants` bytes are one record of five bytes for
every one of those names, in the same order -- 1,040 bytes for 208 -- holding
what case the character is, whether it is a letter or a digit or punctuation,
whether it is a vowel or a consonant or a glide, whether it carries an accent,
and the phoneme it says on its own.

That last field is letter-to-sound at its simplest, and it is data. `a` says a,
`b` says b, `y` is a glide that says y, `ó` carries an accent and says nothing
of its own because the rules decide it. A capital says nothing either: `B` and
`A` both have GAP where `C` and `N` have C and N, which is the table having been
filled by matching a phoneme's name to a character's, so the rules take a
capital down to its own lower case before they ask.

    python3 tools/lang-alphabet.py show plpl          every character
    python3 tools/lang-alphabet.py show plpl a e y    only the ones named
    python3 tools/lang-alphabet.py add plpl 82 case=lower type=letter \
                                   letter=vow accent='~yes' phoneme=a

reads and writes it by name, because a five-byte record read by eye in a hex
blob is how a letter quietly becomes a digit.

`add` puts a character at a byte value the alphabet does not claim yet and
appends its record, rather than reusing a code: the dictionaries are keyed by
these codes, so moving one moves every word that used it. Nineteen byte values
between 0x20 and 0xff are claimed by no name in Italian's alphabet, and Polish
needs sixteen.

Those sixteen are in now, each starting from the nearest phoneme the module
already has: `ł` says w, which is what Polish ł is; `ń` says N, which is the
palatal nasal Italian spells gn and is exactly Polish ń; `ć` says C, `ś` says S,
`ź` and `ż` say Z, `ą` says a and `ę` says e until the nasal vowels are read out
of French. `ó` needed nothing, being already in the alphabet. The capitals say
GAP as every other capital does.

What that changed, measured rather than assumed. Before it, a Polish letter cost
about thirteen thousand samples wherever it appeared, because the engine had no
name for the byte and read it as a symbol -- eight of them alone came to 103,356
samples, a second each. After it, `kąt` is 10,648 samples where `kat` is 8,107,
and the two share the first 14,336 rule entries of their traces, which is what
says ą is being handled as the vowel it now is rather than as an interruption.

A Polish letter *alone* is still silent, and that is the next thing rather than a
fault: a lone letter is spoken by its name -- Italian says esse for s -- and
Polish's letters have no names yet.

### How a character gets in

A caller writes code points and the machine reads single bytes, and between
them IBM's engine does almost nothing: the code set only ever mattered under
the SSML filter, which recodes, and for the four families with a romanizer,
which convert their own. On the ordinary path the caller's bytes are the
characters. That was enough for the nine languages IBM shipped, because every
letter any of them has is in the Windows Western byte set. It is not enough for
a language whose letters are not, and a caller writing UTF-8 -- which is every
caller now -- would hand over two bytes the machine reads as two characters.
That is what `Zażółć gęślą jaźń` did: 120,714 samples, a minute of symbol names.

So a language can say what its own characters arrive as, and
`lang/<tag>/<tag>.codepoints` is where:

    0105 82   # a with ogonek
    0107 83   # c with acute

    make EVVLANG=lang/plpl codepoints

writes that into `delta_codepoints_<tag>.c`, and the language carries it in
`delta_language` beside everything else it knows. `tools/lang-codepoints.py`
refuses a byte the language's alphabet does not name, since a character
arriving as a byte nothing names would simply be something else.

`addTextRun` in `src/eci_synthtext.c` then converts the text on the way in --
and this is a deliberate divergence from IBM's engine, the fourth in the tree.
What makes it safe rather than merely careful is the guard: the conversion runs
only for a language that declares characters of its own, and the nine IBM
shipped declare none, so their behaviour cannot change. The suite says so
rather than the argument: English's 81 cases, German's 80 and the samples hash
are all untouched by it, on sixty-four bits and on thirty-two.

What it buys, measured on the same pangram: 16,819 samples where there were
120,714, and `kąt` written as UTF-8 comes out byte for byte identical to `kąt`
written in the module's own bytes, which is what says the conversion is exact
rather than approximately right.

Text that is not UTF-8 after all is left alone, because the converter answers
whether it was and the caller's own bytes are used when it was not. So a caller
that sends the module's bytes directly still works, which is what the
measurements above were taken with before any of this existed.

### Reading what a language decided

`build/probe <text> <file> p` asks for phonemes instead of sound: what the
language decided the words are made of, under the names its own statement table
gives them. It is the tool the whole of Polish wants, since it says what
letter-to-sound answered without anybody listening.

It does not report anything yet, and where it stops is written down rather than
guessed at. The engine places its phonemes -- `placePhoneme` in
`src/eci_deltacb.c` is reached, five times for one short word -- and returns at
once because `ELOQ_WANT_PHONEMES` is nought. Registering a phoneme buffer sets
the thread's state, parameter four sets the flag through
`setPhonemeIndiciesRun`, and something puts it back before the utterance:
`es_setCurrentState` sends `espr0` when the state says the engine is not in
phoneme mode, and the text path sends the same on a fresh utterance. `disptok`,
which spells a token and was an empty stub in this port, is written now -- so
the names will be there the moment the flag stays on.

### What a phoneme is made of

A phoneme is in three places at once and none of them alone says what it is.
Its name is a value of the phone statement's first field, which is the list the
rules index by. Its numbers are a `Phoneme` line in the settings: four bytes of
name and eleven values, which is what a caller handing the engine phonemes
rather than text is read against. And what it sounds like is a rule named for
it -- `ital_ph_S` -- which sets its source parameters and then calls one locus
rule, `ital_pal_Fv` and its kin, where the formant targets are.

    python3 tools/lang-phonemes.py plpl

puts the three beside each other. Italian declares 35 in the statements, 34 in
the settings and gives 21 a rule of their own; the vowels and a few consonants
have none, being spoken by other machinery. It also says which place each one is
spoken at, which is the thing to know before changing any of it: `ital_pal_Fv`
is called by `ital_ph_S` and `ital_ph_Z` and also by `ital_ph_t` and
`ital_ph_d`, so moving that rule moves four phonemes and moving the call inside
two of them moves two.

`registerPhoneme` takes nineteen arguments and all eighteen after the machine
are addresses of the rule's own locals -- places the engine keeps that
phoneme's numbers, not the numbers themselves. There are 34 of those calls for
34 phonemes, in the order the settings declare them.

### Changing a sound

Polish speaks sz, ż, cz and dż as retroflexes, further back than the
palato-alveolars Italian spells with sc and gi, and the signature of a retroflex
is a low third formant. `lang/plpl/rules/is_val.up` is that, written in the
upper form against the names in `plpl.globals`: `pol_retroflex_Fv` brings f3
down from Italian's 2400 to 2200 and f2 from 1800 to 1700, and the two calls
inside `ital_ph_S` and `ital_ph_Z` in Polish's own copy of `is_val.dr` point at
it. Two lines of the lower notation changed and one rule written.

What that proves, and what it does not. It proves the whole sound path from an
authored rule to the samples: `sciarpa` spoken by Italian and by Polish is the
same word at the same length -- 10,197 samples each -- with 17,448 of its 20,438
bytes identical, so exactly one sound in it moved and nothing else did. That is
the formant path end to end, and it is the first change to how Polish sounds
rather than to what it accepts.

It does not make a Polish word sound different, and the reason is the next piece
of work. `sciarpa` reaches that locus three times; `szafa` reaches it not once.
Polish spells its retroflexes as digraphs -- sz, cz, rz, dz, dź, dż -- and
Italian's letter-to-sound knows sc, gi, gn and gl. So a Polish word today comes
out as the letters it is spelled with, one at a time: `szafa` is s and z and a
and f and a. The phonemes are in the module and the letters are in the alphabet;
what is missing is the rules that say two letters make one sound, and those are
rules to write rather than data to fill in.

### Keeping the chassis honest

    make EVVLANG=lang/plpl census

says how much of the module is still the template's, rule by rule and table by
table, out of the text forms alone. It reads 99% Italian today: 1,749 rules of
1,750 character for character Italian's, one ours, the settings two lines apart
and the variables named. The number falls as the work is done, and what it is
there for is the failure it prevents -- Italian phonology coming out of
something labelled Polish without anyone noticing.

`TEMPLATE` says what to hold it against, and `lang-census.py <tag> <template>
rules` lists every rule with which of the three it is.

## The rules, twice

The language's rules exist in the tree as bytecode, and the engine has an interpreter for them. They also exist as C: `tools/delta-decompile.py` writes all 3,377 of them out of that same bytecode into `lang/enus/delta_rules_c00_enus.c` and thirty-one more beside it, and the interpreter prefers a rule written as C wherever it finds one. It writes beside whichever language it was pointed at, so `make LANG=lang/dede rules` writes German into `lang/dede`.

Thirty-two files rather than one because a translation unit cannot be compiled on more than one core, and thirteen megabytes of it is seven minutes. The decompiler deals the rules out by size so the files finish together; each carries its own piece of the table of rules-written-as-C and the first gathers the pieces, which is what lets every rule stay `static` and keeps one language's names from meeting another's. `PARTS` in the Makefile and `EVV_RULE_PARTS` in the decompiler have to agree, because the build names the files it expects rather than looking for whatever is there -- and the recipe deletes the old ones first, so lowering the number does not leave yesterday's files to be compiled in beside today's.

Both speak the same samples. That is not a hope: `test/suite.sh` holds each form against IBM's binary over all 81 cases, and the two forms are set against each other call by call by `tools/delta-check.sh`. So which one is linked is a trade of build time and size against speed, and nothing else.

Every rule of every language in the tree is written as C, with nothing refused: 3,377 of 3,377 for US English, 3,395 for British, 2,600 for German, 2,378 and 2,386 for the Frenches, 1,724 and 1,717 for the Spanishes, 1,749 for Italian and 1,804 for the Polish chassis. So the interpreter is not a fallback anything depends on any more, and a `RULES=c` build carries none of it: the bytecode, the constants it names and the tables it jumps through come to a megabyte and a half a language, and the language table says nought where it used to name them so the linker can drop the lot.

What `RULES=bytecode` is for, then, is not building. It is the second opinion. `tools/delta-check.sh` is the only check finer than the audio -- it speaks a sentence twice, once each way, and holds every rule entered and every call made with its arguments against the other -- and that check exists only while there is something to compare against. The auxiliary harnesses use it too, for the plain reason that it builds in half a minute. Retiring it would save nothing that ships and would cost the decompiler its oracle.

C is the default, because the speed is the part a person waiting for speech feels. Measured on one machine, the same long sentence, bytecode against C: the whole utterance synthesises in 138 ms against 63; the wait before the first samples of an utterance is 38 ms against 12; and interrupting an utterance and asking for another costs 124 ms against 39. That last one matters most and is the least obvious: the engine cannot abandon an utterance it has been told to stop -- see the interrupting section of `docs/status.md` for why not -- so what a cancel costs is whatever is left of the work, and compiled rules do that leftover work in a third of the time.

What it costs is the build. The C is thirteen megabytes: a little over two minutes of Python to write and about fifteen seconds to compile over twenty-four cores, where the bytecode build wants half a minute and the two seconds a language that writing the rules out of the text costs either way. The binaries are about twice the size -- `build/probe` is 7.1 MB against 3.7 -- because that is what a machine's worth of lifted code looks like written out as C.

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

Eight of the SDK's nine languages pass the cases there are for them, each against a reference built from its own objects: US and British English, German, both Spanishes, both Frenches and Italian. `docs/status.md` says in which configurations, and why Japanese is the ninth.

The language numbers `compare.sh` knows are IBM's own: 0x10000 and 0x10001 for the two Englishes, 0x20000 and 0x20001 for the Spanishes, 0x30000 and 0x30001 for the Frenches, 0x40000 for German, 0x50000 for Italian. A language added to the tree adds a line to that table.

One thing about the `utf8` cases is worth knowing before reading too much into them. The engine takes one byte at a time, so what those cases really check is that both sides mangle multi-byte text the same way, not that either handles it. For Spanish that is not merely mangled: an o-acute directly before an n faults IBM's engine and ours identically, so `razón` in UTF-8 cannot be compared and the Spanish case files avoid the sequence. The same word in Latin-1 speaks perfectly, which is the answer for a caller that wants accents.

Everything a language module holds is named for that module, and the build takes whatever `.c` and `.h` files are in one. A file left behind by an earlier lift, or copied in from another language, would otherwise be compiled in without a word, which is how `lang/dede` carried an unprefixed rule shim into every German binary for a day: its names collided with nothing, so the linker had nothing to say. The build now refuses a module holding a file that is not named for it, and says which file.

## Running

    ./build/evv -o hello.wav "Hello from Eloquence."
    ./build/evv -f speech.txt -o speech.wav
    ./build/evv "Hello from Eloquence." | aplay -q -
    echo "Hello from Eloquence." | ./build/evv | pw-play -

With no `-o` it writes the wave to standard output, unless that is a terminal, in which case it says so rather than filling the terminal with samples. With no text it reads standard input.

`-v` picks one of the eight voices, `-s` the speed, `-p` the pitch and `-V` the volume. Those numbers are the engine's own; `-r` makes them a person's instead, so speed is words per minute and pitch is hertz. `-l` prints what each voice is set to, in whichever units are in force.

`-R` is the sample rate, which is not the speed. Nought to six are 8,000, 11,025, 22,050, 16,000, 32,000, 44,100 and 48,000 hertz, in the order IBM numbered the first four and this port the rest. The default is 11,025.

Above 11,025 the engine goes on running at 11,025 and the rate is raised from there, so the voice is the same one at every setting; `docs/status.md` says why that is better than synthesising at the higher rate and what it does to the spectrum. A number of 8,000 or more is that rate in hertz, raised the same way, so `-R 24000` is a rate nobody numbered. `EVV_UPSAMPLE` says how the raising is done -- `sinc` by default, or `cubic`, `linear`, `hold` or `zeros` -- and `none` synthesises at the rate instead, so the two halves can be compared. `none` is an experiment rather than a setting, and `docs/status.md` says why: synthesised above 11.025 the engine loses up to 24 dB through the consonant band, for a reason that is in the Klatt design rather than in this port. `EVV_SINC_CUTOFF` and `EVV_SINC_TAPS` move where the sinc stops passing the band and over how many samples it stops, which is the one knob that changes how bright the result is without letting images back in. `tools/rate-compare.py` reads the resulting wave files and says it in numbers:

    ./build/evv -R 1 -o held11.wav "She sells sea shells."
    ./build/evv -R 2 -o held22.wav "She sells sea shells."
    EVV_UPSAMPLE=hold ./build/evv -R 2 -o held22.wav "She sells sea shells."
    tools/rate-compare.py held11.wav held22.wav native22.wav

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

One setting in it is worth naming because it reads as two things at once. The
list called sample rate is what the synthesiser runs at, not how fast it
speaks -- NVDA already calls the speed the rate. It offers the seven the
library will build tables for, defaults to 11,025 hertz, and moves the audio
player with the engine or moves neither: an engine at one rate feeding a
player made for another is speech at the wrong speed, and neither half of that
reports anything wrong.

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

What neither of those two can reach is the sound, and the add-on now makes a
claim about it. A long message is handed over as several utterances so that
asking for silence waits out one piece rather than the whole of it, and a piece
boundary is a clause end to this engine: it ends the utterance there, with the
pause a full stop gets. So a boundary in the wrong place is heard.

    make pieces

is that measurement. It speaks one text whole on one instance and in named
pieces on another, and compares the samples -- a pause the engine did not mean
to make is samples it did not mean to produce. A boundary at a sentence end
costs nothing: 177,837 samples whole and 177,837 in five pieces, and the same
after a closing quotation mark. Anywhere else costs about 0.40 s each: the same
text cut every eighty characters at whitespace is 1.12 s longer, "Mr. Jones
asked whether the header is read first, and Mrs. Adams said it is." cut after
the two titles is 0.70 s longer, and "The book by J. R. R. Tolkien is on the
shelf by the door." cut at every initial is 1.48 s longer than 3.72.

That asymmetry is the whole of the driver's rule. It ends a piece at a sentence
end, makes a dot argue that it is one -- a word with a dot inside it, or a short
word starting with a capital, is an abbreviation or an initial -- and falls back
to whitespace only past five hundred characters, where a stretch has no
sentence end to offer. The cases that cost are in the harness as the evidence
for declining them, and are printed rather than held to a number, since a
number measured here is a number about English at one speaking rate. The ones
that must cost nothing fail the target if they ever do.

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

Read those objects with `llvm-objdump -d -r --no-show-raw-insn` and not with binutils `objdump -d`. Every function is its own COMDAT `.text` section and MSVC gave local labels the same names in different sections -- `$L61863` occurs several times in one object -- so binutils takes the recurring name for a function boundary, resynchronises the instruction stream at that byte and prints plausible nonsense from there to the end of the section. In `JpnUtil::ConvertDakuten` it produced `into` and `add %al,(%eax)` where the code is a compare and a conditional jump, and nothing warned. If a function's control flow stops making sense in the middle, suspect the disassembler first. The lifters go on using binutils `objdump` and `nm` for section bytes, headers, symbols and relocations, none of which is affected; it is instruction decoding that is wrong.

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

    make crashers

is the other check that wants neither Wine nor IBM's objects. `test/cases/crashers.txt` holds text the original cannot survive: every one of those strings takes an unhandled page fault in IBM's engine, all of them on a node reference of nought, and ours used to take the same one. This speaks each of them through ours and answers non-zero if the engine died on one or would not finish. There is nothing to hold the audio against, since the reference produces none for any of these, so what it checks is only that ours answers -- either the word is spoken or the utterance is given up, and the process is still there afterwards. It takes about fifteen seconds across the machine's cores, and `EVV_CRASH_JOBS` says how many to use.

The strings were found rather than listed. `tools/crash-search.py` takes text that kills the engine, tries every one-letter change to it, keeps whatever kills it too, and does the same to those, with the engine itself as the oracle. Point it at a build with the guards taken out to find more of them, and at the build in the tree to check that none is left.

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

`make rate` is the check for the output path a rate change goes through. It registers a buffer, asks for 11 kHz, 8 kHz, 11 kHz, 8 kHz, 22 kHz and 11 kHz in that order, speaks a sentence after each, and fails if any of them comes back with no samples, if the rate reads back as something else, or if 8 kHz and 11 kHz answer the same number of samples -- which would mean the rate was written into the environment and handed to nobody. It exists because the suite cannot see any of that: `cli/probe.c` registers a buffer but never asks for another rate, and IBM's engine loses the buffer there as well, so both sides agreed and all 81 cases passed while an instance went permanently silent. It needs neither Wine nor IBM's objects, so it runs in CI.

`make rates` is the check for the rates above eleven thousand, and it is about a different question from `make rate` beside it. It has three halves.

The first is the claim the built resonator tables rest on: that IBM's four are the two formulae they look like -- the excitation table is the exponential of minus pi times the bandwidth over the rate and the cosine table is the cosine of two pi times the frequency over the rate, both in fifteen-bit fixed point and both indexed by hertz from ten. It builds the tables for IBM's own two rates and holds them against IBM's own arrays entry by entry. Every one of the 3,991 excitation entries matches at both rates and every cosine entry matches at 11.025 kHz; two cosine entries differ at 8 kHz, at 3,992 and 4,008 hertz, where IBM stored the saturated value and the formula comes to 32,767.35. Those two are pinned rather than tolerated, so a third mismatch fails. The engine never uses what this builds at those two rates, which is exactly why the check has to be made here.

The second is what says the rate change is a rate change and not an effect, and it drives the resampler directly rather than measuring the audio it produces. Three properties, over every way of raising the rate and both whole and uneven ratios: a held level comes out at that level, a straight line comes out straight, and a run split into uneven pieces comes out identical to the same run handed over whole.

That third one is the one nothing else would catch. The engine hands its samples over a few dozen at a time, so an utterance is hundreds of separate calls; a way that did not carry its tail across would put a seam at every one, which is a buzz under the speech that would barely move a spectrum. Stopping the tail being carried fails it at sample 74 of the first ratio tried.

The slope is steep on purpose, and that was learned from a sabotage that got through. A curve drawn through the wrong four samples still passes through the right places where the grids meet and still holds a level, so only how far it strays between them catches it -- and that is in proportion to the slope. At three a sample the worst stray is a fifth of a count; at sixty it is nearly four, and the shifted tap is caught.

Beside it the engine-level claim, which depends on the way in force: under `hold` nothing is interpolated so every sample is the engine's own, byte for byte; under `cubic` every output sample that lands on an input one, which is every second at 22,050 and every fourth at 44,100, has to be that sample exactly, since a curve passes through the points it is drawn through. The default is not held to either, because a windowed sinc cuts the band off below the input's Nyquist and so changes the signal on the grid as well as between it; what stands for it is the three properties above. Checked across instances rather than within one, because the engine's second utterance is not its first while a fresh instance says the same thing every time.

The third is that every rate speaks, for the same length of time, and at a level that is speech. `EVV_RATES_REPORT=1 make rates` prints what each one said. It needs neither Wine nor IBM's objects, so it runs in CI.

The target runs the harness four times: as it ships, with `EVV_UPSAMPLE=cubic`, with `=hold` and with `=none`. Each has a different claim to make and no one of them enters another's code. The last is what keeps the built resonator tables honest, since with the rate raised afterwards nothing uses them.

The level check is a slew bound rather than a loudness one, because real speech is normalised and reaches full scale exactly as a runaway does once it has been wrapped into sixteen bits; what separates them is that a waveform sampled well above its own bandwidth cannot swing its whole amplitude between one sample and the next. It is asked only of the rates the engine synthesises outright. A held rate steps exactly as its base does -- repeating a sample leaves the jumps between them where they were -- so the rule says nothing about one that is deliberately not bandlimited, and the byte-for-byte check is what covers it instead. `docs/status.md` describes the runaway this catches.

`make inikeys` is the check for the settings reader, and for the same class of thing as `make rate`: a fault neither suite can reach. It asks a reader for a key that is not in the section it names, over a blob written by hand with its sections deliberately butted together, and over the blob the build itself carries. An absent key has to come back as nothing; it used to come back holding the next section's first value, which killed every build with two languages in it on Linux. It also holds every dataset key this build carries to the shape the voice table reads -- eight numbers and then whatever else -- so it grows teeth as languages are added without being rewritten. It needs neither Wine nor IBM's objects, so it runs in CI.

`make voices` is the check for the eight voices the caller may edit, and it exists because of an accident. Nothing the suite runs asks an instance about voice nine: `cli/probe.c` reads the eight parameters of voice nought and the seventeen environment ones, and neither it nor the reference ever mentions the editable eight. So the loop in `eo_newInstance` that copies the language's eight standard voices into them can be turned off altogether -- `for (i = 0; i < 0; i++)` -- and all 81 English cases, all 80 German ones and the samples hash still pass. That is not hypothetical: a stale script left in `/tmp` on 17 August made exactly that edit on 23 August, by being imported under a standard module's name, and every check in the tree passed with it in.

What it holds an instance to is what a caller can tell. A fresh editable voice is the standard voice of the same position in all eight parameters, and it is called "User-Defined" where the standard ones are called things like Adult Male 1. Writing a parameter or a name into voice nine moves voice nine and leaves voice one and voice ten alone, which is what says the eight are copies in slots of their own rather than the language's own table. A voice the caller does not own refuses a write. Copying voice three onto voice nine makes them equal. And a second instance starts again, which is what says the eight belong to an instance. All of it twice, once in the engine's units and once in a person's, because those go through a conversion on the way out.

Three sabotages are caught and each says which: the copy not happening names the parameter that differs, the renaming not happening names the voice that kept IBM's name, and making all eight the same slot shows on voice ten. It wants neither Wine nor IBM's objects, so it runs in CI.

One thing it reports rather than fails on. In a person's units, two of the eight parameters will not take the value they themselves read back: IBM's real-world range starts at one for six of the eight, and the conversion answers nought for the roughness and the breathiness of a voice that has none. The suite already holds those numbers against IBM's binary over the twenty cases it reads the parameters back in a person's units for, so the nought is theirs; the refusal is their range meeting their own conversion.

`make stopthread` is the check for a stop that crosses a thread, which is what a screen reader does and what `make interrupt` does not: that one answers `eciDataAbort` from the callback, on the engine's own thread. This one has a second thread call `eciStop` once the callback has taken a given number of buffers, a different number every turn, and requires the process to survive, the stop to have been made while the engine was still delivering, and every utterance afterwards to be worth exactly what a whole one is. `make win-stopthread` is the same binary under Wine, which is where it used to fail. The Linux half runs in the bytecode CI job, since like `make rate` and `make inikeys` it wants neither Wine nor IBM's objects. Taking the busy guard out of `es_engsynFlush` faults both, which is how the harness is known to see what it claims to. It does not require the interrupted utterance to come out short, and the comment at the top of `test/stopthread.c` says at length why that would be wrong.

`test/prims.sh` is the check for a primitive the suite cannot reach at all. A call that no rule in the nine languages IBM shipped ever makes cannot be exercised by speaking a sentence, and those are exactly the calls this engine was missing: the arithmetic beyond addition, the bare ordering tests, the right pointer register's half of the loads. So they are held against IBM's own objects directly rather than through the audio. `test/prims.c` is a table of cases and is compiled twice -- `make prims` builds it against our engine and `make -C reference prims` against IBM's, which define these under plain C names -- and the script runs both and diffs what they print. What is compared is the bytes each call leaves behind, eight of an operand and sixteen of each pointer register, so a primitive writing four bytes where the original wrote two is a difference rather than a coincidence. It wants Wine and IBM's objects, like the suite.

Two things about it are worth knowing. `vadd` is in the table although it was ported long ago: it is the control, and if it differed the harness would be what is wrong. And the one line the two builds do not share is which language is in force -- a table is a plain global in IBM's build and is reached through the language in force in ours -- which is what `EVV_PRIMS_OURS` is for.

The machine is a real one and both sides build it with the same calls: `delta_new`, `etiwinMainDLL`, `initializeIO`, the language's `DeltaProc_start`, a sentence handed to the link with `eciLinkDataFromECI`, and `reset_sent_vars` and `get_tok` to read it in. All of those are IBM's own names and are in its objects too, which is what lets one file drive both; the only line the two builds do not share is which language is in force, since a table is a plain global there and is reached through the language in force here. What is on the spine is compared without being decoded: a record holds one code per character of the alphabet its statement type declares, so rather than spell it out the harness offers every code to the string test and prints the ones that match.

The spine itself is compared as a shape rather than as a list of addresses. `show_spine` walks it from the token the rules left -- not from the spine's own end, which is on a different chain -- and asks every node the string test for every statement kind and every one-byte code, printing the pairs that answer. A node holding the same thing on both sides answers the same pairs, and nothing has to know what a pair means. That is what says where an insert put its tokens: the four widths leave the spine in four different shapes and the two engines agree on each. What it does not see is the value a wide insert decodes, since the nodes those make answer no string test of either width at any kind; putting the two-byte decode under the four-byte name passes, which was tried rather than assumed.

Three cases come at the end, the last two of them because unlike the rest they take the machine apart. `vgen` is the largest thing in the machine and is given a cell built for the occasion: what is compared is what it answers and the two marks its first pass settled on, said as landmarks, since on a spine of two nodes there is no span to walk and the frame loop is not entered. `set_saved_ptrs` is given a word variable and two pushed locations carrying the same made-up value and asked to move one of them; the values are made up because nothing in that call dereferences a position, it compares and replaces, and the word variable is put back afterwards. `ins_rdtoks` is asked twice: once with the stack as the cases above it left it, where the record on top is the floor marker and it refuses, and once with a bottom marker and two pushed values built under it, where it lays a token for each and stops on the marker. That second round is the one that reaches `vins_tok` and `vins_sync`, and the spine printed after it is what says the tokens went where the original puts them -- where, and not what: reading the record one byte over changes nothing printed and neither does pushing different values, so the content is the same blind spot the wide inserts have. Nothing may be added after those two.

Not everything it tests wants a spine. The walk over the names a field can take, the uniqueness test and the two prefix tests all read the language's own table, so those cases print whole answers rather than landmarks -- the alphabet, `undefined lower upper`, which characters could still begin a name -- and they are the strongest cases in the file for that reason.

Three things it does not reach, and it says so where it stops. `get_tok` leaves two nodes rather than a sentence, so a call that walks the spine walks a short one, and the branch of the context pair that has to look for a mark is never taken -- putting the wrong walk under it changes nothing, which was checked rather than assumed. `init_stream` tears a stream down and builds it again, which on a machine with a sentence in it leaves nothing for the next call to stand on. And IBM's own `vsplit_time` faults on the only position the harness can offer `divide_time`, so that pair cannot be compared at all. A spine with statements on it wants the whole pipeline run with an output attached for the samples, and that is the next piece of this harness rather than something it does now.

`test/romcan.sh` and `test/romprims.sh` are the two checks for a language written in another script, which is Japanese and nothing else in this SDK. Such a language goes through a romanizer on its way to the engine, and until that romanizer is transcribed there is no way to get a word of the language as far as the engine at all -- so the seam is recorded instead. `make -C reference TAG=jajp BUILD=../build/reference-jajp romtap` builds IBM's engine with `reference/romtap.c` standing in front of the eight public methods of its romanizer manager, which writes down every call and every answer when `EVV_ROMTAP` names a file. `make romcan LANGS=lang/jajp` builds `test/romcan.c`, a romanizer with no Japanese in it that reads such a recording back and answers from it, and `EVV_LANG=jajp test/romcan.sh` runs both sides over the Japanese cases. Two things have to hold: the samples have to be IBM's, which says the whole engine below the seam is right, and every call that arrives has to be the call the recording holds, which says our manager asks the same questions. That is what proved the engine right for Japanese before a line of its romanizer existed, and it is what says the romanizer is all that is left.

The one thing a recording cannot show is a parameter being read: the manager reads a parameter before it writes one and flushes what the romanizer is holding when the two differ, so what `getParam` has to answer is worked out from the recording's own flushes. `EVV_ROMCAN_PARAMS=real` stops working it out and hands that half to the romanizer's own `rom/jajp/rominstparam.c`, which is how that file is proved: a wrong answer moves every flush after it and the recording stops lining up.

`test/romprims.sh` is for a romanizer class the seam never reaches. It is the same arrangement as `test/prims.sh`: `test/romprims.c` is compiled twice, by `make romprims LANGS=lang/jajp` against ours and `make -C reference TAG=jajp romprims` against IBM's objects, and the script diffs what they print. What it sweeps is every input there is -- each single byte, each two-byte pair the codeset converter accepts, and all sixty-five thousand code points in the other direction, twice -- which is about a hundred and forty thousand calls a side. It found the one difference there was on its first run, and that difference turned out to be IBM reading past the end of its own table.

Every language module is built and spoken in the bytecode CI job, and then all eight are built into one binary and each spoken out of it. Neither wants Wine or IBM's objects -- only the comparison against IBM does -- so what that catches is a module that stops linking, or an engine change that suits one language and not another, and it requires samples rather than only a successful link.

`test/hash.sh` is the check that needs nothing at all: it speaks one fixed sentence and holds the samples against a hash in `test/samples.sha256`. That does not prove the engine right -- only IBM's binary can -- but it proves it unchanged, which is what catches a careless edit, and it is what the workflow in `.github` runs on every push. The samples do not depend on the compiler: gcc 15 and clang 21 agree byte for byte, which is what an engine with no floating point in it should do.

## The sixty-four bit build

The Delta machine keeps addresses in thirty-two bit values, so on a wider host everything it can point at has to live somewhere such a value can still name. `src/evv_arena.c` maps a region low in memory and everything the machine holds comes out of it. That includes the language's own data: the rules name their constants by address, and the set and action tables hand over an address per entry, so `src/delta_low.c` copies those stores out of the program at startup and translates an address into its copy at the few places where one becomes a value. A pointer from anywhere else says so and stops.

Which is why there is no `-no-pie` and no fixed image base any more. The program can be loaded wherever the loader fancies, ASLR and all, which is what makes a shared library possible: a library does not get to choose where it goes. The Makefile asks the compiler how wide a pointer is and leaves the arena out altogether when the host is thirty-two bit, where a pointer is a value already.

`-Werror=int-conversion` and `-Werror=incompatible-pointer-types` are on for both builds. A narrowed field assigned from a pointer, or the other way about, was the whole of what went wrong in the sixty-four bit port, so it is an error rather than a warning nobody reads. The rest of the warnings are off: this is transcribed code and it is loud.
