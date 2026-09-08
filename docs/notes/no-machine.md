# An engine with no machine in it

The goal is an engine that has no virtual machine inside it: no `delta_state`, no bytecode array, no backtracking stack, no four-byte reference and therefore no arena. A language's rules would be C over named structures, the machine's 278 entry points would be ordinary functions with typed arguments, and the language's data would be read where it lies. The engine would sound exactly as it does now, because that never moves.

**Half of that is already true and was before any of this began, which is worth saying first because the rest of the document does not make it obvious.** `RULES=c` is the default build and sets `EVV_NO_BYTECODE`, which compiles the interpreter out; the section flags let the linker drop the bytecode array with it. Checked rather than assumed: `nm` over a `RULES=c` probe finds no `run_bytecode`, no `delta_rule_code`, no `delta_rule_imm`. The two symbols matching "interp" are a language rule called `interpret_single_char_modes`.

So the machine as *a program being interpreted* is gone from what ships. What is left is the machine as *a runtime*: a state block whose variables are numbered rather than named, frames that are byte arrays, a backtracking stack, and a reference that is four bytes wide -- which is what the arena exists for. That is what the rest of this is about, and `RULES=bytecode` still builds because it is half the gate.

This is the account of what stands in the way, what was measured on 7 September 2026, and what the measuring changed about the plan. It is a handover rather than a description: almost none of the work is done.

## Why the arena exists, and why it is a symptom rather than the problem

The Delta machine is a virtual machine from 1999 and its word is four bytes. Every register, every stack slot, every language variable is four bytes, and the same four bytes serve as a number or as an address -- the machine draws no distinction. So anything the machine can be handed a pointer to must have an address that fits in four bytes. On a 32-bit host that is simply true and there is no arena: `EVV_REF` and `EVV_AT` compile to nothing. On a 64-bit host it is only true if everything lives somewhere low, and `src/port/evv_arena.c` is that somewhere.

So the arena is not a decision anybody made for its own sake. It is sixty lines of `mmap` and a walk, and it is the consequence of one fact: the machine's word is four bytes. Removing it means widening that word, and it is the widening that is expensive, not the arena.

`src/delta/delta.c:33` states the constraint the tree has always worked under: the language's two self-description tables may grow on a 64-bit host because no compiled rule reaches into them, and "everything above holds whatever the host is, because the rules do reach into all of it." The layouts are not an implementation detail the machine is free to choose. They are the interface 21,134 rules were compiled against.

## What the rules address, and how much is already named

The language's own variables are named. The decompiler's `name_globals` pass writes `GLOBAL(int16_t, r6, s326)`, resolving through `DG_s326` to an offset, and 34,012 reaches across the languages go through that named form -- 495 distinct variables in English alone. `delta_state`'s own named fields stop at `DG_BASE`, which is 0xb0, and the language's cells follow.

What is not named is every other reach: a rule doing arithmetic on a register with a literal offset, `r6 + 288`, or reaching through one, `*(int32_t *)(r6 + 1272)`. `name_globals` is deliberately conservative and gives up on any register that holds the state at one point and something else at another, which is why so many are left. Counting the sites it leaves, over all ten languages: 26,094.

## The census, and what it answered

Naming those sites statically means knowing what each call hands back, and that is not recoverable: all 278 of the machine's entry points return a bare `int` or `int32_t`, because a reference is a value here. No C signature says what object a returned reference names.

What is recoverable is where a block came from, because the allocator has recorded it since the overrun guard wanted it: every block in the arena carries the return address of whoever asked for it. So the question was put to the allocator at run time instead. `src/delta/delta_prov.c` is that census, `tools/rules/provenance.py` reads it, and the decompiler's `prov_sites` pass gives each unnamed site a number of its own. All of it is behind `-DEVV_PROVENANCE=1` and `EVV_RULE_PROVENANCE=1`; without both, nothing is compiled and a rule is the expression it always was.

Run over every recorded case of all ten languages, and over the 24,318 English words as well:

    26,094 sites the decompiler numbered
    16,133 reached, and addressing exactly one kind of storage   (62%)
         0 reached and mixing kinds                              (0%)
     9,961 never reached by either gate                          (38%)

Of the 16,133, all but twenty address a block of the arena. Nineteen address something outside it and one a null. Not one shows as the state, the language's data stores, or the C stack -- but that is the classification being too coarse rather than a fact about the rules, and the next section is what corrected it. The state itself lives in an arena block, and so does every frame, because a rule hands the machine the address of its frame and only the arena can be named in four bytes. So "a block of the arena" is nearly everything and says almost nothing.

That nothing mixes kinds is the most encouraging number here. Every site either addresses the language's heap or it addresses a frame, consistently, over every case and every word.

The census is inert, and that was checked rather than assumed: with the instrumented build, every language's recorded cases came out unmoved and so did all 24,318 words. The matrix holds the reported answers as well as the samples, so a census that changed anything would have said so.

## What the census cannot answer, and it is not a shortcoming of the census

The allocator names the storage class and can never name the record. `delta_lang_alloc` hands out a single 4,256-byte block and `evv_frame_push` a single four-megabyte one: these are pools, and the machine sub-allocates every record it keeps out of them. So the recorded allocation site is whichever call happened to grow the pool, and with wide coverage the same rule site reports `ed_add_active_dict`, `delta_lang_alloc` and `hetero_install` by turns. 1,629 of English's sites saw more than one such name. Block size does not rescue it either, because there is only one block: every site in the language's heap reports the same 4,256 bytes.

An earlier and thinner run of this census, over the recorded cases alone and before the words were added, reported 59 per cent of sites resolving to a single allocator. That number was an artefact of thin coverage -- one run tends to grow the pool from one place -- and it should not be quoted. The allocator dimension is noise.

## What the census changed about the plan

The route it closes is naming records from the allocator. The route it opens is better, and the census is what made it visible.

A reference is not born at an allocation. It is born at one of 162 `EVV_REF` sites in the engine's own code, where a pointer with a real C type is flattened into a value: `EVV_REF(v)`, `EVV_REF(to)`, `EVV_REF(&cell->value)`. The type is right there in the source, at the moment the type is lost. So instead of classifying an address at the point of use and trying to infer the object, tag a reference at birth and let it carry what it is to the point of use. That is 162 places rather than 26,094, the information is exact rather than inferred, and it needs no dataflow analysis over the rules -- which also means it avoids the hazard that would otherwise have been the main risk here. `docs/status.md:369` records that hazard: the first control-flow graph read a backtracking dispatch as falling from one arm into the next, seventeen rules came out needing fewer registers than they really do, and nothing said so.

Some of the 162 have no type to offer -- `EVV_REF(malloc(...))` is a void pointer -- and those want the surrounding code read. That is a bounded job on a small number of sites rather than an open-ended one on a large number.

**Measured, and it is what corrected the storage answer above.** With births recorded, English's 2,599 reached sites resolve as follows: 2,474 come from places naming exactly one C type, 60 from more than one, and 65 from no birth the run saw. Ninety-five per cent of what was reached. The types are `delta_state` overwhelmingly, then `delta_loc`, a `void *` that genuinely has no type to give, `delta_actrec` and `uint8_t`. So most of these sites do address the machine's state, which the storage classification could not see.

The places have to be collapsed by type before that number means anything: the state reaches the rules through four wrapper functions in a language module, each with its own `EVV_REF`, and a site seeing two of those is seeing one type twice. The first version of the report counted that as ambiguity and made the answer look far worse than it is. `tools/rules/ctype.py` is what reads a type out of a source line -- a plain identifier, a member, an address-of, a call, and `EVV_AT`, which carries the type it produces as its own first argument.

## Where the static route stops, and why it is not the census's fault either

Stage two named everything it could reach from inside one rule. What is left in English is 622 sites, and 292 of them are reaches through a register loaded out of a frame slot. Every one of those 292 reads a slot the rule never wrote: they are the rule's own arguments, pointers handed in by whoever called it. Measured, not assumed -- there is no exception in the whole language.

So what those reaches address was decided by the caller, and no analysis inside one rule can know it. Slot tracking was written and then taken out again for that reason: it is sound, it costs nothing, and it names nothing, because the pointers it would follow never come from the rule.

**The census cannot answer this one either, which settles a question that was open.** A census names what a rule was seen to be passed. A rule called from two places with two sorts of pointer would be named after whichever the cases happened to exercise, and that name becomes a wrong offset the moment a layout moves -- silently, at the stage where it is least recoverable. Nine hundred and twenty-six entries are handed the address of a rule's own slot, so this is not a rare shape. That is the argument for keeping the census a check and never letting it become an input, and it is a better argument than the one about hermetic builds.

What would answer it is a signature for each rule: what each of its arguments points at. That is inter-procedural over 21,134 rules, and it is a stage of its own rather than more of this one.

**And it wants types rather than offsets, which was measured before being assumed.** The call graph is favourable: of English's 3,377 rules, 1,187 have exactly one caller and 862 have between two and four, so call sites agreeing is the common case. But a signature saying only *how far into the state* an argument points names almost nothing -- over every call site of every English rule, 262 of 5,309 argument positions agree on such an offset, and 4,494 are never known at all, because those arguments are not pointers into the state. They point at the machine's own records, and where those came from is the caller's business, and the caller got them from a primitive writing into its frame.

So the missing thing is what each of the machine's entries writes through a pointer handed to it. `docs/rules.md:128` already says so in as many words: "Nothing in the compiler knows how much any entry writes."

**Four routes to this population came out at or near nothing, and the fifth is the one that works.** Worth writing down so nobody spends the afternoon on the first four again.

Slot tracking names nothing, because every slot those reaches read is one of the rule's own arguments and the rule never wrote it.

Rule signatures expressed as an offset into the state name 262 of 5,309 argument positions, five per cent, because those arguments are not pointers into the state.

Typing an entry's arguments from its own declaration works -- `delta.h` declares 370 of 371 entries with at least one typed pointer argument, `delta_loc` at 81 of them and `delta_token` at 28 -- and a rule does reveal a pointer's type by what it hands it to: 880 values are passed where a `delta_loc *` is wanted and 694 where a `delta_token *` is. But on its own it names no reach, because the registers so typed are not the registers reached through. A rule *hands* a record to the machine and lets the machine read it; it does not reach into it itself.

**What works is chasing the same information along the call graph.** Three links, each already written down somewhere. An entry declares its arguments, so handing a pointer to one says what that pointer is. A rule hands the address of its own slot to an entry, which says what that slot holds -- 1,029 slots in English. And a rule passes the address of a slot to another rule, which says what that rule's argument is. So the answer travels from the machine's own calls into the language inwards, and it has to be chased to a fixed point rather than read off: six rounds over English, 1,037 argument positions typed, 483 reaches named as the field they are across the ten languages.

`argument_types()` in `tools/rules/decompile.py` is that fixed point. It reads every rule once, which is why a language now takes about half again as long to write out; there is no cheaper way, because the answer for one rule lives in its callers and theirs in turn. `RECORD(t, p, type, field)` is what it emits, and `src/delta/delta.c` asserts every offset it replaces, so a field that moved would stop the build rather than name a different byte in silence.

And the census cannot be used, for the reason above.

So English is down to 580 of its original 4,614 sites saying numbers, from 622 before the call graph was chased. What is left is mostly reaches whose argument the fixed point could not settle -- 155 of the 292 -- and reaches at offsets that are not a field of any record named here yet. Both are more of the same work rather than a different kind of it: more records described in `RECORD_FIELDS` with their assertions, and a fixed point that does not give up where a register is written between the load and the reach.

## The frame, which is the next large thing and is measured ready

22,102 sites name a byte offset into a rule's own frame. That is the last big population, and it matters for stage three because a rule's local holding a reference is four bytes today and would want eight.

It looks tractable, and this was measured rather than hoped. Over 600 English rules, not one has a slot whose span runs into the next; a rule uses a median of five distinct slots and at most 38; and the widths are one, two and four bytes. So each rule's slots could become a struct the compiler lays out, which is what lets one of them grow.

It is also smaller than 22,102 suggests. Only 1,042 of English's 3,377 rules have a frame at all: the other 2,335 are wrappers, which `write()` already detects and which keep their few words on the C stack like any other function. Across those 1,042 there are 121 distinct shapes, `pbase` is 8 for every wrapper and between 104 and 124 for a real rule, and the frame runs from 196 to 392 bytes.

What the widening actually needs is to know which slots hold a reference, because those are the ones that grow from four bytes to eight and shift everything after them. That is answerable now and was not this morning: `state_offsets` and `argument_records` say which registers hold a reference at each point, so a store into a slot from such a register says the slot does. Slot tracking was written and removed earlier in the day because it named no reaches -- this is a different use for it and a better one.

Two things make it a stage of its own rather than an afternoon. The frames are all deliberately the same size -- `docs/rules.md:128` says why: the address of a frame is the name a landing place is filed under, and frames of one size put a rule at a given depth back where it was -- so a per-rule struct has to be padded to that size. And the machine writes into the frame through addresses a rule hands it, so the block stays where it is while the rule's own slots move around it. The safe order is a struct with today's layout spelled out first, which is a pure rename and provably inert, and only then letting the compiler choose.

## The stages, and where the point of no return is

**One, provenance.** Which object each reach is addressing. The census above is the first half of it and the `EVV_REF` tagging is the second. Verifiable to a standard with no judgement in it: `make notation-prove` must still find the bytecode identical and the recorded cases must not move, because nothing in this stage changes what the generated C does, only what it says about itself.

**Two, typed access.** With provenance in hand, the generated C stops doing arithmetic and starts naming fields. Same layouts, same four-byte references, same arena. Verified the same way. This is the first stage with a payoff if the work stops there: the rules become readable in the sense the decompiler has been reaching for since it was written.

Done for the state, for the frame's machine-written half, and for the records a rule is handed, over 7 and 8 September 2026. Across the ten languages: 19,552 addresses into the state name the variable they point at, 4,357 reaches name the variable they are, 2,142 name a variable and a step into it, 1,218 name both ends of a pointer into the state, 1,413 name a field of a record the rule was handed, and all 8,243 `ENTER` calls name `delta_rule_block` rather than five offsets.

**English went from 4,614 sites naming a number to 175, which is 96 per cent.** Six builds at 979 cases with nothing moved at every step, and every macro proved live by breaking it: a byte on an address, on a field, on a step or on the difference between two variables each moves all 98 English cases. Moving the landing slot moves nothing, which is correct -- `src/port/evv_land.c` uses that address only as a name, never as storage.

Not done: the rule's own frame slots, and 172 reaches through argument positions the call-graph fixed point could not settle -- call sites that disagree about what they pass, or a position no caller ever passes a typed slot to.

The 172 that remain are a long tail: 155 distinct rule-and-argument pairs wanting one or two reaches each, so there is no single fix left in them. Widening `RECORD_FIELDS` past `delta_loc` and `delta_token` gains nothing either -- tried with `delta_tpos`, `delta_operand` and `delta_node` added, and the fixed point still settles on only the two, because those are the only ones a rule is handed the address of a slot for.

**Two mistakes cost most of a day between them and are the same mistake.** Both flow analyses started their fixed point at the bottom of the lattice rather than the top, so the first pass met every unvisited predecessor with nothing, and a must-analysis only narrows, so nothing is where they stayed. They were answering empty everywhere. And neither seeded the walk with what the whole body already knew -- that a register loaded only ever with the state holds it -- which matters because the flow graph *cannot see a landing place*: it is entered by the machine rather than by any visible jump, so its label has no predecessor, is seeded knowing nothing, and poisons every join below it. Fixing both took reaches through a pointer into the state from 30 to 1,218 and record fields from 483 to 1,413. The seed has to go inside the walk, not onto its answer, or a pointer computed from such a register is still unknown while the walk runs.

**Three, the layouts become ours.** Only now may a struct change shape. References become real pointers, `delta_low`'s copying goes, the arena goes, and Apple silicon and iOS fall out as a side effect rather than as the point.

What licenses stage three is a measurement made the same day: moving the arena from `0x10000000` to `0x30000000` and rebuilding left all 979 cases unchanged. No address value reaches the audio or the reported answers, so the recorded baselines can police a change to how memory is named, over all ten languages, without IBM and without Wine.

**Four, retire the machine.** Frames become locals, the backtracking stack becomes real control flow -- the decompiler already recovers 2,391 loops -- and the state's cells become each language's own struct. The interpreter goes.

Be clear-eyed about the cost of that last one. `RULES=bytecode` cannot survive it and neither can `make notation-prove`, because there is no longer a bytecode for the text to compile to. Stages one through three are provable against baselines IBM blessed; stage four dismantles the machinery that proves them, and afterwards the only things holding the engine are the 979 recorded cases and the twenty thousand words. That is not nothing -- it is what already holds Polish -- but it is a real loss of leverage, and it argues for doing stage four last, deliberately, and one language at a time.

## What is left of the 38 per cent

9,961 sites were never reached by either gate, and they are where a change to a layout would break something with no test to say so. They are not spread evenly. In English 2,015 remain, and 376 rules hold them; the ten worst hold 799 between them, led by `hebrew_ph_Q` at 179, `eng_abbr` at 133 and `homog_roots` at 129. A word list for a language other than English would cut the other nine considerably -- `test/cases/words-enus.txt` is the only one that exists, which is why English is the only language whose coverage the words improved.

Two honest options for the remainder. Reach them, by writing cases and word lists aimed at the rules that hold them, which is worth doing anyway. Or accept that they cannot be named and leave them arithmetic, which costs only that those sites pin their layouts. The `EVV_REF` route above changes this calculation entirely, since it does not depend on a site being reached at all.
