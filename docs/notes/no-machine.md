# An engine with no machine in it

The goal is an engine that has no virtual machine inside it: no `delta_state`, no bytecode array, no backtracking stack, no four-byte reference and therefore no arena. A language's rules would be C over named structures, the machine's 278 entry points would be ordinary functions with typed arguments, and the language's data would be read where it lies. The engine would sound exactly as it does now, because that never moves.

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

What would answer it is a signature for each rule: what each of its arguments points at. That is inter-procedural over 21,134 rules, and it is a stage of its own rather than more of this one. It is tractable in principle, because a rule's callers are rules too and the machine's own calls into the language are typed where they start.

## The stages, and where the point of no return is

**One, provenance.** Which object each reach is addressing. The census above is the first half of it and the `EVV_REF` tagging is the second. Verifiable to a standard with no judgement in it: `make notation-prove` must still find the bytecode identical and the recorded cases must not move, because nothing in this stage changes what the generated C does, only what it says about itself.

**Two, typed access.** With provenance in hand, the generated C stops doing arithmetic and starts naming fields. Same layouts, same four-byte references, same arena. Verified the same way. This is the first stage with a payoff if the work stops there: the rules become readable in the sense the decompiler has been reaching for since it was written.

Done for the state and for the frame's machine-written half, on 7 September 2026. 19,552 addresses into the state name the variable they point at, 4,353 reaches name the variable they are, 280 name both ends of a pointer into the state, and all 8,243 `ENTER` calls name `delta_rule_block` rather than five offsets. English went from 4,614 sites naming a number to 622. Six builds at 979 cases with nothing moved at every step, and the macros are provably live: a byte added to an address moves all 98 English cases, and four bytes added to the fence arrays moves them too. Moving the landing slot moves nothing, which is correct -- `src/port/evv_land.c` uses that address only as a name.

Not done: the rule's own frame slots, and the 622 reaches the section below accounts for. Both wait on rule signatures.

**Three, the layouts become ours.** Only now may a struct change shape. References become real pointers, `delta_low`'s copying goes, the arena goes, and Apple silicon and iOS fall out as a side effect rather than as the point.

What licenses stage three is a measurement made the same day: moving the arena from `0x10000000` to `0x30000000` and rebuilding left all 979 cases unchanged. No address value reaches the audio or the reported answers, so the recorded baselines can police a change to how memory is named, over all ten languages, without IBM and without Wine.

**Four, retire the machine.** Frames become locals, the backtracking stack becomes real control flow -- the decompiler already recovers 2,391 loops -- and the state's cells become each language's own struct. The interpreter goes.

Be clear-eyed about the cost of that last one. `RULES=bytecode` cannot survive it and neither can `make notation-prove`, because there is no longer a bytecode for the text to compile to. Stages one through three are provable against baselines IBM blessed; stage four dismantles the machinery that proves them, and afterwards the only things holding the engine are the 979 recorded cases and the twenty thousand words. That is not nothing -- it is what already holds Polish -- but it is a real loss of leverage, and it argues for doing stage four last, deliberately, and one language at a time.

## What is left of the 38 per cent

9,961 sites were never reached by either gate, and they are where a change to a layout would break something with no test to say so. They are not spread evenly. In English 2,015 remain, and 376 rules hold them; the ten worst hold 799 between them, led by `hebrew_ph_Q` at 179, `eng_abbr` at 133 and `homog_roots` at 129. A word list for a language other than English would cut the other nine considerably -- `test/cases/words-enus.txt` is the only one that exists, which is why English is the only language whose coverage the words improved.

Two honest options for the remainder. Reach them, by writing cases and word lists aimed at the rules that hold them, which is worth doing anyway. Or accept that they cannot be named and leave them arithmetic, which costs only that those sites pin their layouts. The `EVV_REF` route above changes this calculation entirely, since it does not depend on a site being reached at all.
