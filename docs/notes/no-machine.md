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
