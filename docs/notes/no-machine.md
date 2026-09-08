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

## The frame, half done

A rule's own locals are a struct per rule now -- 6,643 rules over the ten languages, 116,182 slot uses -- with the layout spelled out so that every field lands where its number put it. That half is a rename and the gate says so. `frame_struct()` in the decompiler is it.

Two things it taught, both guarded now. **The fields have to be byte runs, not the types the slots are read as**: given their natural types, a slot at an unaligned offset makes the compiler pad in front of the field and everything after it moves, which showed up as exactly two German cases, both a voice change, out of 979. And **every struct asserts its own size** against the frame the rules were compiled for, which would have caught that at compile time instead of forty minutes later.

851 of English's 936 rules with locals are in. The other 85 read one word at two widths and offsets -- the high half of a 32-bit slot as a 16-bit value -- and want a union rather than two fields, so they keep their offsets.

**What is left of the frame is letting the compiler choose the layout**, which is the half that matters, because that is what lets a slot holding a reference grow from four bytes to eight. It is specified rather than sketched, and measured over English:

The write span of a slot is `sizeof(T)` for the widest `T *` any entry the rule hands its address to declares. `delta_loc` and `delta_token` are 8, `delta_tpos` and `delta_operand` 16, `delta_node` 44, `delta_actrec` 92, `delta_field` 4, `delta_mark` 20. 783 slots are declared narrower than the entry writes through them, which is `get_parm` filling in eight bytes of a four-byte local and is IBM's own doing.

Of those spans, 597 have no other slot inside them and only want the field made as wide as the span. **202 have another slot inside, and those must become one field, not two** -- because the rule means the wide write to fill both and reads the second afterwards. Give the second a field of its own somewhere else and the rule reads nothing. So a span swallows the slots inside it and their accesses become offsets within the covering field.

Then the padding goes and the compiler places the fields. Two consequences to handle: `base` stops being `frame + <frame>` and becomes `(unsigned char *)fp + sizeof(f_<rule>)`, since the machine's block and the argument area are still reached through it; and a frame that grows may pass `DELTA_RULE_FRAME_MAX`, which is a per-language maximum and would want raising. The size assertion changes from `sizeof == frame` to a minimum per field, because the layout is then deliberately not IBM's.

## What the frame stage was measured to be

22,102 sites name a byte offset into a rule's own frame. That is the last big population, and it matters for stage three because a rule's local holding a reference is four bytes today and would want eight.

It looks tractable, and this was measured rather than hoped. Over 600 English rules, not one has a slot whose span runs into the next; a rule uses a median of five distinct slots and at most 38; and the widths are one, two and four bytes. So each rule's slots could become a struct the compiler lays out, which is what lets one of them grow.

It is also smaller than 22,102 suggests. Only 1,042 of English's 3,377 rules have a frame at all: the other 2,335 are wrappers, which `write()` already detects and which keep their few words on the C stack like any other function. Across those 1,042 there are 121 distinct shapes, `pbase` is 8 for every wrapper and between 104 and 124 for a real rule, and the frame runs from 196 to 392 bytes.

What the widening actually needs is to know which slots hold a reference, because those are the ones that grow from four bytes to eight and shift everything after them. That is answerable now and was not this morning: `state_offsets` and `argument_records` say which registers hold a reference at each point, so a store into a slot from such a register says the slot does. Slot tracking was written and removed earlier in the day because it named no reaches -- this is a different use for it and a better one.

**The constraint that decides the design, and it is not the obvious one.** A slot cannot simply be given to the compiler to place, because slots are deliberately adjacent in places. `docs/rules.md:112` records it: `get_parm` fills in a compiled location, which is eight bytes, so a rule that declared a four-byte local there has the next local written into as well. Reorder those two and the second is corrupted, and no test of the rule in isolation would say so.

That is the same gap as "nothing in the compiler knows how much any entry writes", and it is closable with what stage two built. An entry taking a `T *` writes `sizeof(T)`, and `entry_ptrs()` already reads those declared types out of `delta.h` for 370 of 371 entries. So a slot's field wants a size of max(the widest access the rule makes of it, `sizeof(T)` for every entry the rule hands its address to). Given that, the compiler may place them freely.

Measured on the way to this: of the 936 English rules with a frame and negative slots, 851 use their slots at non-overlapping spans and 85 read one word at two widths and offsets -- the high half of a 32-bit slot as a 16-bit value -- which wants a union or a byte run rather than two fields. 106 rules have a frame and no negative slots at all. The 2,335 wrappers have no arena frame in the first place; `write()` already gives them a plain C array.

Two more things make it a stage of its own rather than an afternoon. The frames are all deliberately the same size -- `docs/rules.md:128` says why: the address of a frame is the name a landing place is filed under, and frames of one size put a rule at a given depth back where it was -- so a per-rule struct has to be padded to that size. And the machine writes into the frame through addresses a rule hands it, so the block stays where it is while the rule's own slots move around it. The safe order is a struct with today's layout spelled out first, which is a pure rename and provably inert, and only then letting the compiler choose.

## One crossing, and what it means for pull request 16

`W(x)` in `src/delta/delta_rules.c:283` is `((evv_word)(uint32_t)(x))`, and it is the only place a reference becomes a pointer on the way into one of the machine's entries: the `delta_call_N` family widens every argument through it and an entry that declared a pointer gets the whole of it. That is one line, not a layer.

It is also why pull request 16 needed a generated wrapper around every primitive. To make a reference an offset from a base rather than an address, the base has to be added to the pointer arguments and not to the ordinary numbers, and `W` cannot tell them apart. Wrappers were how that pull request told them apart.

**It no longer needs to be.** `entry_ptrs()` in the decompiler reads which of each entry's arguments are pointers straight out of `delta.h`, and gets an answer for 370 of the 371 declared there. A generated table of that, read by `delta_call_N`, does what those wrappers did and is derived from the header rather than written by hand. Anyone revisiting that pull request should start there.

It is worth being clear that this is a different goal from this document's. Base-relative references free the arena's *location*, which is what Apple silicon and iOS need. They do not remove the arena: a reference is still four bytes, so there is still one region and everything the machine points at is still inside it. Removing the arena wants references to be real pointers, and that wants the frame.

## Retiring the arena: what it actually costs, asked of the compiler

Widening `evv_ref` from `int32_t` to `intptr_t` and building produces **39 errors and nothing else**, all of them static assertions saying which field moved, in `src/delta/delta.c` and `src/delta/delta_heap.c` only. Ten are `delta_stack`'s fields, twenty-five are `delta_state`'s and `delta_vars`', and four are size claims -- `delta_rule_block`, `delta_mark`, `delta_seg`, and `delta_state_ends_at_the_cells`. Nothing else in the engine fails to compile. Those assertions are doing exactly the job they were added for.

None of the moved fields is reached by a rule: they are the machine's own bookkeeping and the C reaches them by name. What matters is the last of them. `delta_state`'s named part is exactly `DG_BASE`, which is 0xb0, and the language's variable cells begin where it ends -- so widening the references inside it moves every cell.

**And the rules' text names cells by an absolute offset.** `statefld 3078`, 697 of them in English. Both back ends read those numbers: the compiler writes them into bytecode and the interpreter reads them raw. So moving the cells invalidates the text.

The way through is a seam rather than a rewrite. The text goes on saying what IBM said, and the tools translate: one cell walk in IBM's layout to work out *which* variable an offset means, and a second in ours to work out *where* that variable now is. `variable_at()` is already half of that -- it turns an absolute offset into a variable and a step -- and what is missing is that `extents()` currently serves both purposes at once. Split it and the text stays faithful, `make notation-prove` keeps its meaning, and both back ends emit the offsets of the layout they are actually building.

That is the enabling step, and it is a no-op until something moves, so the gate can prove it inert before anything depends on it.

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

## The seam, and the third walk it caught

Before anything in the state can move, the two jobs the cell offsets do have to be told apart. A rule's text says `statefld 3078`, meaning the byte at 3078 of a state laid out the way 1999 laid it out; English says so 697 times and the number is not ours to change. Where that variable actually goes is a different question, and the answer stops being 3078 the moment a field of `delta_state` changes width -- which is precisely what retiring the arena does.

So `tools/rules/decompile.py` now reads an offset out of the text in IBM's layout and emits the placement as a distance from the first cell, and `DG_BASE` in `src/delta/delta.h` is `sizeof(delta_state)` rounded up to four. The decompiler no longer knows where anything goes; the C works it out from the struct, so the two cannot drift apart. `delta_state`'s trailing pad is gone with it -- the struct used to be padded out to meet a number, and now the number follows the struct.

Proved live rather than merely inert: with the placement moved sixteen bytes and the lookup untouched, all 98 English cases move.

The one assertion left is that `DG_BASE` is a multiple of four. The cell walk aligns each cell against the start of the state rather than against the cell before it, so only a four-aligned base shifts every cell by the same amount; off one, the two walks disagree from the first cell that has to be padded.

Which is not hypothetical, because splitting the walk found that it had been wrong all along. There are three copies of it -- `delta_new` at run time in `src/eci/bridge/eci_deltaglob.c`, the lifter in `tools/module/globals.py`, and the decompiler -- and the decompiler had two of its own, neither of which had the rule that a compound whose first word is 6 is four-aligned. It cost nothing while a cell's name round-tripped back to the number it came from: the offset went in, came out as a name the other two walks would have called something else, and the constant emitted for that name was the number it came from. Wrong label, right address, silent.

It would not have stayed silent. The moment the placement is a different walk from the lookup, a mislabelled cell is a mislabelled address, and 415 of Italian's would have landed two bytes out -- along with Spanish's, Mexican Spanish's, French's and Polish's, five of the ten. English, British English, German, Canadian French and Japanese have no compound where the rule bites, which is why every check to date passed.

What settles it is the language's own declared state size. The last cell has to end exactly there, and the walk now stops and says so if it does not. `eses` declares 0xfbc and the old walk ended at 0xfb8. That check is four lines, the docstring had claimed it for months, and nobody had run it.

The three walks are now two, `cells()` being the only one in the decompiler.

## Why the arena cannot go yet, stated as the compiler sees it

A machine register is `int32_t r0` in the generated C. That is the whole of it. The arena exists so that every address a register may hold fits in one, and no amount of tidying the allocator changes that -- the register has to widen first, and a register that widens stops wrapping at 32 bits, which the machine's arithmetic depends on.

So the two cannot be separated by fiat. What separates them is knowing, site by site, which values are addresses and which are numbers, and that is what the naming work has been building: 19,552 addresses into the state named, English down from 4,614 raw offsets to 175. A named site already yields a typed lvalue from a real pointer and needs no reference at all. What is left is the sites that materialise an address *into* a register -- `GLOBAL_AT`, the frame macros, and the raw form -- because those are the ones that still have to fit in 32 bits.

Counted over the ten languages: 14,200 `GLOBAL_AT`, 6,327 `FRAME_REC`, 6,327 `FRAME_JB`, 18,981 `FRAME_FENCE`, and 30 still raw. Forty-five thousand sites, but not forty-five thousand edits -- every one of them is a macro whose last act is a cast to `int32_t`, so widening them is one edit each in `src/delta/delta_rules_c.h`. That is what the naming work bought, and it is why the 30 raw ones are the ones that matter.

What those addresses then flow into is the question that decides the shape of the change, and three quarters of it is already settled by the rule signature: `static int32_t evv_name(void *state, const int32_t *args, int nargs)`, with `int32_t r0` through `r7` and an `int32_t arg[8]` inside. A rule hands the machine the address of its own frame through that array. So the registers and the argument array certainly carry addresses and certainly have to widen, along with every machine primitive's signature.

The one genuinely open question is whether an address is ever stored into a *variable cell*, because cells are the language's own data and widening them would move every offset in every rule's text. A single-line scan finds no address expression assigned straight into a cell, a frame slot or a record field: every one goes into a register or into a call. But an address can reach a cell in two steps, through a register, and settling that needs real liveness over the flow graph -- which is exactly where this tree has been bitten before, so it is not something to eyeball.

It does not have to be settled by analysis. Widen the registers, the argument array and the primitives; leave the cells at thirty-two bits; drop the arena so that an address is genuinely sixty-four bits wide. Then any address that has to round-trip through a cell is truncated and the engine falls over, and if it does not, the gate's 979 cases say the cells never held one. The experiment and the implementation are the same piece of work, which is the cheapest way this could have turned out.

## Retiring the arena: the whole of it, six places

The primitives do not change. That is the finding that resizes this from a rewrite to an afternoon, and it was sitting in `src/delta/delta_rules.c:282` the whole time:

```c
typedef uintptr_t evv_word;
#define W(x) ((evv_word)(uint32_t)(x))
typedef evv_word (*I1)(evv_word);
```

Every entry the machine can call is already declared taking and returning `evv_word`, which is `uintptr_t`. The 136 primitives -- the bulk of the engine -- are already pointer-width and already correct. The entire arena dependency funnels through the `(uint32_t)` inside `W`, which is the one place a pointer is narrowed to a value the machine can hold.

So the change is:

The truncation in `W` goes. One line, and it is the crossing.

A machine register, `int32_t r0` through `r7`, becomes pointer-width. The decompiler emits that declaration, so it is one line there.

The argument path widens: `int32_t arg[8]` in each rule, the cast inside `ARG`, and `delta_call_N(int, const int32_t *, int)`. This one certainly carries addresses, because handing the machine the address of its own frame is what a rule does through that array.

`GLOBAL_AT`, `SLOT` and `FIELD` stop casting their result down to `int32_t`. Three definitions in `src/delta/delta_rules_c.h`, covering the 14,200 sites the naming work made nameable. This is what that work was for.

The 1,124 shifts of the form `rN = rN >> 31` get their operand truncated. That idiom is the only uncast arithmetic on a register in any of the ten languages: everything else already carries an explicit `(int32_t)` or goes through `ALU`, because the machine's arithmetic is 32-bit by definition and the decompiler already emits it that way. One change in the emitter.

The variable cells stay at thirty-two bits, deliberately. If a rule ever stores an address into one it truncates and the engine falls over, and the 979 cases are the detector. That settles by experiment the one thing analysis could not settle cheaply, and it costs nothing extra because the experiment and the implementation are the same work.

## The route not taken, and why it is not a rival

Widening the word leaves the machine standing, sixty-four bits wide. The other route is type-directed: emit real C pointers at the named sites so that an address never becomes an integer at all, which is where "no machine" actually ends. It is a great deal more work and it needs real liveness over the flow graph, which this tree has been bitten by before.

It is not an alternative to the widening, though, and that is worth writing down so nobody re-argues it. Any address that lives in a register needs the register to hold it, whichever route is taken. The widening is the first half of the type-directed route, not a detour around it.

## Correction: it is not six places, because the records grow

The six above are the value path -- registers, arguments, the crossing, the address macros, the shifts -- and that part of the account stands. What it left out is that a reference is also a *field*, and 69 declarations in `src/delta/delta.h` have one. Widen the type and every one of those records grows.

`delta_actrec` is the one with teeth. It carries `back` and `top` as references and it is `delta_rule_block`'s first member -- the block a rule hands the machine on the way in, held by `src/delta/delta.c` to 192 bytes with `landing` at 92 and `fence` at 156, because 8,243 `ENTER` calls over the ten languages agree on those numbers. Widen a reference and the block is no longer 192, and `landing` is no longer at 92.

Most of that follows by itself, which is the point of the naming work: `FRAME_JB` and `FRAME_FENCE` are written with `offsetof`, so a rule's reach into the block moves with the block. `RECORD` likewise, for the 1,413 field reaches. The assertions are then not wrong so much as stale -- they pin numbers that were only ever the numbers a four-byte reference produced, and they have to be re-derived rather than deleted, because what they are really checking is that the rules and the struct agree.

What does not follow by itself is a site that reaches into one of these records by a raw number. There are 30 of those left in the address macros and 175 offsets in English the call-graph fixed point could not settle, and each one is a number that was right for a 192-byte block. Those have to be named before the records may grow, not after.

So the order is: name the tail, then widen. Not the other way round, and the earlier claim of six places was counting only the half that was already easy.

## What the tail actually is, and why most of it does not block anything

The 172 unnamed reaches in English are not 172 different problems. Sorted by offset they are 103 at +4, 55 at +2, 13 at +0, and one at +2562. So 171 of them reach the first few bytes of a record the fixed point could not type.

That matters less than it looks, because a reach at 0, 2 or 4 only breaks if the record shifts within its first eight bytes, and three records in `src/delta/delta.h` do: `delta_operand_at` (`ptr` at 0), `delta_seg` (`prev` at 0) and `delta_mark` (`pos` at 0, `seg` at 4). Everything else is safe. In particular the two obvious candidates for those offsets are not: `delta_loc` is `int16_t kind; int16_t field; int32_t value` and `delta_token` is two `int32_t`, and neither carries a reference, so both stay eight bytes however wide a pointer gets.

So the tail is a blocker only where it reaches one of those three, and that is a question about 171 sites with a known answer set rather than an open problem.

## The one site that is a real constraint, and what it says about the machine

The odd one out is `lang/enus/delta_rules_c11_enus.c:733`, an `int16_t` read at `r6 + 2562`. That is inside the cell area and two-aligned, so it is a short variable's value at the cell starting 2560, and `variable_at` would name it in a moment. The analysis refuses, and it is right to.

Twelve lines earlier the rule does `r6 = FIELD(0)`, which is the state. Six lines later it does `r6 = delta_sym_ref[1700]`, which is one of the language's own byte stores. The site sits under the label `alt1_564`, and the only thing that jumps there is the alternative dispatch six hundred lines below -- a switch on an alternative number, reached from everywhere, and reached from after the reassignment. So `r6` at that label is the state on one path and language data on another, and no must-analysis can say which.

This is the landing-place problem generalised, and it is worth stating plainly because it bounds the whole programme: the machine's backtracking dispatch is a computed goto whose predecessors are the entire rule, so a register's object is not always statically determined. Where it is not, the offset cannot be renamed, and a layout that offset names cannot move.

One site in English, so the practical answer is not an analysis but a measurement: trace what `r6` actually holds there across the cases and the twenty thousand words, and if it is always the state, name it and let the gate say whether that was true. What must not happen is naming it because the arithmetic looks right.

## Which resolves the tail: it is one site, not 172

The three records that shift within their first eight bytes are never handed to a rule. `delta_operand_at` is mentioned nowhere in `src` outside its own declaration; `delta_mark` once, in `delta_heap.c`; `delta_seg` seven times, across `delta.c` and `delta_heap.c`, all of it the heap's own segment bookkeeping. No entry in the machine takes any of the three, and a rule only ever reaches a record it was handed a pointer to.

A rule holding heap memory points into a segment's payload, not at its header, so a reach at +0 through such a pointer is the payload and not `prev`. That is the heap's own invariant and it is what the argument rests on.

So none of the 171 reaches at +0, +2 and +4 can be into a record that shifts, and all of them are safe under widening whether they are ever named or not. The tail is the single `r6 + 2562` site, and that one is settled by tracing what the register holds rather than by any amount of further analysis.

This supersedes the correction above: naming the tail is not a prerequisite for widening. The prerequisite is one measurement.

## Doing it in four steps rather than one, and why the first is free

`Makefile` decides the arena on pointer size alone: `LOW := -DEVV_ARENA=1` where a pointer is eight bytes, and nothing where it is four. So a thirty-two bit build is already the no-arena configuration and already passes, which is worth more than it looks -- it means every step below can be checked on i686 as well, where it must come out a no-op.

That gives a sequence where each step is provable on its own and only one of them is risky.

**One: widen the type, keep the arena.** `typedef intptr_t evv_ref`, and `EVV_REF(p)` becomes `((evv_ref)(intptr_t)(p))`. On i686 `intptr_t` is `int32_t` and this is character-for-character what it compiles to today, so the 32-bit gate says nothing structural moved. On x86-64 references widen and 69 records grow, but the arena is still there, so every address still fits and nothing truncates. This isolates *records growing* from *addresses going high*, which are the two things that would otherwise fail together and be hard to tell apart.

The open question in this step, and it wants answering before the edit rather than after: whether any of those 69 records overlays IBM's own module data rather than being built at run time. A record the machine fills is free to grow; a record that is a window onto bytes IBM wrote is not, and widening a field in one would read the wrong bytes. `src/delta/delta.c` already says `delta_stmt` and `delta_fielddesc` may grow because nothing compiled from a rule reaches into them, which is the shape of the argument but only for two of them.

**Two: widen the value path, keep the arena.** Registers, `arg[8]`, `ARG`, `delta_call_N`, `GLOBAL_AT`, `SLOT`, `FIELD`, and the 1,124 `>> 31` shifts. Addresses are still low, so this is a pure refactor and the gate can hold it to the letter.

**Three: turn the arena off.** `LOW :=` empty on sixty-four bits, and the `(uint32_t)` in `W` goes. Now an address is genuinely wide and this is where a thirty-two bit slot nobody found bites. One step, one suspect.

**Four: delete it.** `src/port/evv_arena.c`, and `delta_low.c`'s copying of the language's data out of the program, which existed only so that data could be named in thirty-two bits.

### Step one's open question, answered

None of the records that would grow is a window onto bytes IBM wrote. The language's data in `lang/<tag>/delta_*.c` is generated C -- `dede_vstmtbl[]` is a real `delta_stmt` array, compiled against whatever the struct currently says -- so a field widening moves the data and the reader together. What `delta_low.c` copies into the arena is byte blobs, and a byte blob stays a byte blob however wide a pointer gets; the offsets into it are IBM's and no struct governs them.

The casts in `src` bear this out: `delta_node` sixty-three times, then `delta_vars`, `delta_stack`, `delta_frame` and the rest, every one a record the machine builds for itself. Nothing reinterprets module bytes through a struct that carries a reference.

`delta_stmt` is the instructive case. It is already allowed to grow on sixty-four bits, and not because of a reference -- it holds real host function pointers, `void *(*const *get)(void *)` and the rest, which is why `src/delta/delta.c` asserts its 0x40 size only where a pointer is four bytes. A statement's own field layout comes from the module's `length` and `stride` and is reached through those function pointers, so it is the language's business and not the struct's.

### Step two, located

Every edit, with where it is, so that the doing of it is mechanical.

`src/port/evv_arena.h:26` -- `typedef int32_t evv_ref` becomes `intptr_t`, and the `EVV_REF` in the no-arena branch stops casting down. On i686 both are what they compile to now.

`src/delta/delta_rules.c:283` -- `#define W(x) ((evv_word)(uint32_t)(x))` loses the `(uint32_t)`. That is the crossing, and it is the last line of step three rather than step two, because until the arena goes an address still fits.

`src/delta/delta_rules_c.h` -- `SLOT` at 138, `FIELD` at 139 and `GLOBAL_AT` at 295 stop casting their result to `int32_t`. `ARG` at 170 and 176 stops casting its argument down, and `arg[8]` and the `delta_call_N` and `delta_direct_N` declarations from 349 take the wide type.

`tools/rules/decompile.py:338` -- `r2 = r0 >> 31` becomes `(int32_t)r0 >> 31`. One line, for the `cltd` opcode, and the only uncast arithmetic on a register in any of the ten languages. Everything around it is already explicit: the divide below it reads `((int64_t)r2 << 32) | (uint32_t)r0` and casts both results to `int32_t`, so it stays right on a wide register without being touched.

The register declaration the decompiler emits, and `int32_t arg[8]` beside it, take the wide type in the same pass.

### Three references that are not declared as references, and the flag that will not catch them

Reading for step one turned up what the plan had missed: places where a reference is held in something declared `int32_t`, which therefore does not widen with the type and truncates in silence.

`delta_tpos.node`, at +0x00 of the sixteen bytes a rule's two pointer registers are. Twenty-five sites assign `EVV_REF` into it -- `d->lpta.node`, `p->node = EVV_REF(rmost(...))` -- and it is read back as `(int32_t *)(intptr_t)p->node`, a pointer taken out through a thirty-two bit field. `src/delta/delta.c` holds `delta_tpos` to sixteen bytes unconditionally, on the grounds that the rules reach into all of it, so widening `node` grows it to twenty-four and that assertion has to be re-derived -- the same stale-not-wrong case as `delta_actrec`.

The generate record's `value` at +0x00, "the frame", assigned from `EVV_REF(getDeltaStackVBot(d))` in two places. This one already has an `evv_ref params` at +0x08, so the record is half converted and nobody noticed the other half.

And a local: `int32_t stop = l->node` in `src/delta/delta.c:1496`, which then round-trips through `(delta_node *)(intptr_t)stop`. So it is not only fields. Locals of this shape cannot be enumerated by grepping for a type.

**The warning that is already on does not cover this.** `WARN` in the `Makefile` carries `-Werror=int-conversion`, and its comment says a narrowed field assigned from a pointer was the whole of what went wrong in the sixty-four bit port. That catches pointer against integer. It does not catch integer against integer, and `int32_t stop = l->node` with `node` widened is exactly that: a `long` narrowed to an `int`, which GCC only mentions under `-Wconversion`.

So step one's method is a build with `-Wconversion` added, and the output filtered to the narrowings whose source is pointer-width. Turning `-Wconversion` on for good is not on: this is transcribed code and it narrows int to short constantly and deliberately. It is a sieve to run once per step, not a flag to keep.

### And it is a category, not three sites

Pushing the same reading further, the shape is broader than fields. A reference held in something declared `int32_t` turns up in three places, and only the first can be found by grepping for a type.

Fields: `delta_tpos.node`, the generate record's `value`, and any other that reading has not reached yet.

Locals: `int32_t stop = l->node` in `delta.c:1496`, `int32_t at = s->walk` in `delta_heap.c:269`. The second walks the stack's records and hands the reference back out, so it is not a scratch copy.

Return types: `peekDeltaStackNext` is declared `int32_t` and returns `at`, which is a reference; `peekDeltaStackStart` returns `s->walk`; and five sites across `src` say `return EVV_REF(...)` from a function declared `int32_t`, in `delta_heap.c`, `delta.c`, `eci_phonemes.c`, `eci_deltacb.c` and `eci_hash.c`.

That last group is the reassuring one, because a narrowing at a `return` is exactly what `-Wconversion` reports. So the sieve covers all three categories and there is no need to find them by hand -- which is just as well, since the local case cannot be found by hand.

What this changes about step one is its size, not its shape. It is a sieve, a list, and a pass of retyping, and the i686 gate holds it to a no-op throughout.

## Eleven thousand frozen offsets nobody had looked for

Widening the type to run the sieve turned up the largest thing standing in the way, and it was not in the value path at all.

`GLOBAL_AT` names the address of a cell taken through a register that holds the state, and `GLOBAL` names a reach through one. Both were done and counted. But where the state is the rule's own parameter rather than something in a register, the decompiler emitted `FIELD(288)` for an address and `FLD(int16_t, 2562)` for a reach -- a frozen byte offset into the cell area, and no analysis had ever been asked for because none is needed to see that the operand says "the state".

Over the ten languages that is 5,220 addresses and 5,916 reaches. Every one of them would have named the wrong variable the moment `DG_BASE` moved, which is precisely what widening a reference does. None of it was visible while the layout could not move, and the seam is what made it findable.

The fix wanted no analysis, which is why it had been so easy to leave: the operand gives the offset, `variable_at` names it, and the address is the same address. `FIELD(0)` stays as it is, being the state itself and not a cell -- 10,524 sites in English alone. Every language now reports zero of both kinds.

Proved live by an isolated sabotage of the new emission alone: offset it by four and every English case moves. An earlier attempt sabotaged the macro and caught `GLOBAL_D` with it, which proved something broader and therefore nothing.

### And it flushed out a bug of its own

`NATURAL` in the decompiler says what width an operand already is, so a comparison need not widen it again. Its `GLOBAL` entry asked for capture group two while the pattern held one group, and `GLOBAL_D` was not in the table at all. Neither had ever mattered: a `GLOBAL` could only appear after `direct_tests` had run, so the entry was never reached. Naming the state's own offsets emits one before it, and the build stopped with `IndexError: no such group`.

That is twice in a day that making something layout-independent has flushed out a bug that was unreachable while the code was layout-dependent -- the cell walk being the first. Worth expecting a third.

## The layout can move, and that is the whole precondition

This is the measurement the rest of it was for. With every reach and every address named, `DG_BASE` was moved sixteen bytes and the gate was run: **979 cases over the ten languages, every one as it was.**

That same sabotage, before the `FIELD` and `FLD` naming, moved all 98 English cases. So the property is new this morning and it is the one the arena retirement rests on: where the cells start is now the build's business and nothing in the rules pins it.

One piece the seam had not covered turned up on the way, and it would have been a heap overrun rather than a wrong note. Every *offset* followed the layout, but the state's total **size** still came from the module's `state_bytes`, which is IBM's number written against a base of 0xb0. Move the base and the cells run past the allocation. So `src/delta/delta.h` now carries `DG_BASE_IBM`, said plainly to be IBM's and not to follow ours -- the way the offsets in `lang/<tag>/rules` are IBM's -- and `DELTA_STATE_BYTES(n)`, which is `DG_BASE` plus the bytes of cells the language declared. `delta_lang.c:81` was the only runtime consumer of the module's number and there is no `memset` of the state to match.

That it was needed rather than merely tidy was worth checking, because the 979-case pass could not tell the difference: sixteen bytes past a `malloc` is the sort of thing glibc absorbs without a word. So the base was moved with the allocation deliberately left following IBM's number, and the arena's own guard was turned on -- `-DEVV_ARENA_GUARD=1`, which exists for exactly this. It named it at once: block 133 asked for 4248 bytes and was written at 4248, with the poison pattern intact after it. A real overrun, silently tolerated for the whole of that passing run.

### And the bytecode build cannot follow, by construction

The interpreter reads `statefld 3078` out of its input and adds 3078 to the state. Nothing can name that for it, so a moved base must move its cases -- and if it did not, the offsets would not be coming from where this says they come from. Measured: with the base moved, `RULES=c` holds all 979 and `RULES=bytecode` moves the lot. Same tree, same sabotage, opposite answers, which is the pair that proves it rather than either half alone.

Which fixes something about the plan that had been left vague: **retiring the arena is a property of `RULES=c` and cannot be one of `RULES=bytecode`.** That is not a new cost -- `RULES=c` has been the default and the shipped form since 22 August 2026, the interpreter is already absent from the shipped build, and stage four retires it. But it means the six-build gate stops being six builds of one engine at that point, and the bytecode half has to be held at the old base while the C half moves. Two configurations of the gate, not one, for as long as both forms exist.

## The frame is the same seam again, for the third time

Step one needs `delta_actrec` to grow, since it carries two references, and it is the first member of `delta_rule_block` -- the 192 bytes a rule hands the machine on the way in. Whether that may grow at all comes down to what else is in a rule's frame, and the generated C answers it plainly:

```c
typedef struct {
    unsigned char pad0[192];
    unsigned char s132[4];
    ...
} f_homog_roots;
static int32_t evv_homog_roots(void *state, const int32_t *args, int nargs)
{
    unsigned char *frame = evv_frame_push(DELTA_RULE_FRAME_MAX);
    unsigned char *base  = frame + 324;
```

The block is the leading `pad0[192]` and the rule's own locals begin after it. So growing the block would land on `s132` -- if any of those numbers were fixed. None of them is: the decompiler writes the struct, the frame size, and the `FRAME_REC(-324)` the block is addressed by, all in one pass. What is IBM's is where a local sits *relative to base*, because that is what the rules' text says; the block's size is ours.

Which makes the frame the same arithmetic as the state, and the same as the cells before it:

    frame size = sizeof(delta_rule_block) + (IBM's frame size - 192)

exactly as `DELTA_STATE_BYTES(n)` is `DG_BASE + (n - DG_BASE_IBM)`. Three places now where a number is IBM's plus a delta that is ours: the cell offsets, the state's size, and a rule's frame. That is the shape of this whole conversion and it is worth recognising on sight -- anything that mixes the two in one constant is the next bug.

So step one is: let the block grow, have the decompiler take the leading pad and the frame size from `sizeof(delta_rule_block)`, re-derive the three block assertions, and retype the references that are declared `int32_t`. The i686 gate holds the lot to a no-op, since `intptr_t` is `int32_t` there.

### Except the frame is not a shift, and the reason is worth having

The arithmetic above is right and the assumption under it was not. A rule's block is not always at the start of its frame: over English's 849 rules the block sits at frame zero 789 times and somewhere else 232 times, and a rule may hand the machine blocks at several places. Worse, **694 of the rules' own named slots fall inside a block's 192 bytes, across 321 rules.** So growing the block in place would change which bytes are the block while the rule went on reaching the old ones.

What is actually happening there is better than it looks. Take `homog_roots`: its block sits at frame 100, and it has a slot `s132` at frame 192 -- which is block plus 92, which is `landing`. And the rule's use of it is `(int32_t)(intptr_t)&fp->s132`, the address of its own landing buffer, handed to the machine. That is the `ENTER` mechanism, and the decompiler is naming the same bytes twice: once as `FRAME_JB(-224)`, which follows the struct through `offsetof`, and once as `fp->s132`, which does not.

So the fix is not to move the block or to hold `delta_actrec` narrow. It is the same move as the cell offsets and the state's size: a slot that coincides with a block field *is* that block field, and should be emitted as one. Then `sizeof(delta_rule_block)` may be whatever the host makes it and every reach follows.

`delta_actrec` carries four references -- `back` at 0x1c, `top` at 0x20, `vbot` at 0x24, `err_jmp` at 0x2c -- all inside the 92 bytes before `landing`, so any of them widening moves `landing` and every one of those 694 slots with it. Which is why this has to be done before step one and not after.

The care it needs: a rule with blocks at two positions has slots that could fall inside either, and those are ambiguous. Count them before assuming they are rare.

## What the arena's problem actually is, which changes the plan

The arena is not a region. It is a region **below two gigabytes**: `evv_arena_open` maps it low and gives up if it cannot, because a reference is thirty-two bits and has to name it. A region addressed by *offsets from a known base* has no such requirement and can live wherever the system puts it.

So there are two ways to stop needing a low mapping, not one. Widen every reference into a host pointer, which is what the four steps above assumed. Or turn the references the rules can see into offsets, and leave everything else alone. The second keeps every layout still, which matters more than it first appears.

**Because the rules see almost nothing.** Over the ten languages they reach into exactly two record types by field: `delta_loc`, 825 sites, and `delta_token`, 588 -- and neither carries a reference. `delta_loc` is `int16_t kind; int16_t field; int32_t value` and `delta_token` is two `int32_t`. The only rule-visible record with references in it is `delta_actrec`, inside the 192 bytes of `delta_rule_block`.

Which means **every other record that carries a reference may widen freely**, because nothing compiled from a rule reaches into it. The whole constraint is `delta_actrec`: `back`, `top`, `vbot`, `err_jmp`, and `node` in each of the two `delta_tpos` it holds. Six fields.

Six fields against the 4,967 frame slots that would have to be named if the block were allowed to grow -- 698 of them inside two different block positions and so not nameable statically at all. That settles it: keep `delta_rule_block` at 192 bytes for good, and make those six offsets.

`top` and `vbot` are offsets from `delta_stack.base`, which the struct already has at 0x0518. `back` and `err_jmp` are saved copies of the same fields in `delta_vars`, so they take whatever base those do. The two `node` fields point at statement nodes out of the segment heap, and that is the one that needs a decision rather than a lookup: an offset wants a single base, and the node heap is segments.

But that is a pool with a base, not a low mapping. Nodes addressed by offset from one reserved region can live anywhere in memory, which is the whole of what retiring the arena means.

## A wrong turn worth writing down: references as offsets

The reframing above -- that the arena's problem is the low mapping and not the region -- is right. The conclusion drawn from it, that a reference could simply become a distance from the region's base, is wrong, and it took a working build and four crashes to find out why.

It very nearly works. `evv_ref_checked` subtracts the base instead of truncating, `EVV_AT` adds it instead of casting, `arena_map` stops asking for a low address, and the region comes up at 0x7fffe7c00000 -- high memory, no `MAP_FIXED`, no two-gigabyte limit. Everything the machine allocates already comes from that region, so every reference is expressible, and the first eight bytes are already reserved so nought goes on meaning nothing.

**Where it fails is `W(x)`, the crossing into a primitive.** Every argument a rule pushes goes through it, and it cannot tell a reference from a number -- today it does not have to, because a thirty-two bit absolute address is both at once. With offsets a pointer argument needs the base added and a number must not have it, and `W` has no way to know which it is holding: the entries are called through `I1`..`I12`, which take `evv_word` and nothing more. That is not a patch, it is the premise failing.

The four crashes before that point were all real and all the same kind -- a reference used as an address without going through the crossing -- and each is worth keeping in mind because they will matter under any scheme:

`VARS_1128` in `eci_deltamisc.c` cast `d->vars` straight to `char *`. `CLRONESTM((delta_node *)(intptr_t)t)` and 496 other casts of the shape `(T *)(intptr_t)x`. `*(int32_t *)p` inside `VRSYNC` and `VLSYNC`, with no intermediate cast at all, which no grep for a cast shape can find. And the generated shim, `delta_run_rule((void *)(intptr_t)a0, ...)`, which survived every sweep of `src` because it is written by `tools/rules/emit.py` rather than kept in the tree.

So the crossing is the thing that decides this, and the plan is the earlier one with a reason attached: **real pointers everywhere the rules cannot see, and offsets only in the six fields of `delta_actrec` that they can.** Then `W` becomes the identity rather than a decision, because a register holds a pointer and a number is a number. The patch for the abandoned attempt is kept out of tree; nothing of it is wanted except the knowledge of where it broke.

## What actually stands between here and no arena: 158 signatures

The single crossing is the whole problem, and it has a shape now.

`W(x)` in `src/delta/delta_rules.c` is applied to every argument a rule pushes, and it cannot tell a reference from a number. Today it does not have to: a reference *is* an address. Any scheme that makes a reference something other than an address -- an offset, an index, anything -- has to put that distinction somewhere, and the only place it can live is the entry's own signature. `delta_direct_1` calls `((I1)delta_rule_entry[which])(W(a0))`, and `I1` is `evv_word (*)(evv_word)`, which has thrown the types away.

So the last piece is typed entries, and it is smaller than the table suggests. English's `delta_rule_entry` has 3,500 slots, but 3,341 of them are the module's own rules, which go through `delta_run_rule` and are already handled by the shim. **The machine's own entries number 158.** Of those, 156 are declared in `src` with their real C signatures and the other two are `memcpy` and `memset`.

That is the job: read the 158 declarations, and for each emit a wrapper that converts the arguments the declaration says are pointers and leaves the rest alone. Then `W` is gone -- not made cleverer, gone, because each entry converts what it knows it has -- and a reference need not be an address, and the region can be mapped wherever the system likes.

Two things to get right when doing it. The arguments arrive in the reverse of the order the entry takes them, which `delta_call_N` already knows and a generator must not forget. And this is the hot path: two and a half million rule entries in a run, which is why `delta_run_rule` was made to read the language in force once and hold it, so a wrapper per entry must not undo that.

It is also, and not by coincidence, exactly what the no-machine endgame needs. An entry that declares its arguments is the difference between a machine calling numbered primitives and a program calling functions.

## The crossing, made to know what it is holding

`W(x)` is gone. In its place each argument is converted or not according to what the entry declares it takes, and that is the thing without which the arena cannot go.

`tools/rules/entrysig.py` reads the entries' own C declarations and answers a mask per entry: bit *n* for argument *n* being a pointer, bit thirty-one for one that answers with a pointer. All 158 of the machine's entries resolve -- 156 declared in `src`, plus `memcpy` and `memset`, which the C library provides and which are written down in the module. There is no second copy of a signature anywhere, which is the point: a mask that disagreed with a declaration would hand a primitive a distance where it wanted an address, and the fault would land a long way from the mistake.

`emit.py` writes it beside the entry table as `delta_rule_argmask[]` -- 158 nonzero and 3,342 zero for English, the zeros being the module's own rules, which take references and keep them. `delta_rules.c` uses it at all 57 crossing sites through `WM(m, i, x)` and `RM(m, r)`, `call_entry` having gained the mask as a parameter.

**Under absolute addressing both arms of `WM` are the cast that was there before, so this must be inert, and the gate says it is.** That is what makes it worth having on its own: it changes nothing today, and nothing else can change until it exists.

Japanese wanted its table written by hand, which is the ten-language gate earning its keep. `lang/jajp` keeps its rules in the tree rather than generating them -- the lift wrote those files and they are the only copy -- so `make rulecode` never rewrites them and the symbol was simply absent. The numbers still come from `entrysig.py`, so there is one source and not two: 1,029 entries, 878 of them Japanese's own at nought, none unresolved.

### Two traps met on the way

The object directory is keyed on the rules form and the language set but **not on `OPT`**. So a build with `-O0` leaves un-optimised objects that the next ordinary build reuses, and it does not fail quietly: it fails at the link, complaining about `vtbl_eCollection`, because `sti_indexQueueCtor` assigns five vtables over one another and only the optimiser's dead-store removal keeps the first four from being referenced. Touch the sources to be rid of it.

And the struct-wrapper trick -- making `evv_ref` a one-field struct so every misuse is a compile error -- is the wrong tool for finding a reference used as an address. It reports 112 sites across three files and every one of them is arithmetic or a comparison on a reference, which is correct: an offset plus a number is an offset. It finds no casts at all, because those already go through the crossing. What it is good for is the opposite question.
