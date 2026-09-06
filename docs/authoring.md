# Authoring, and why it is the wrong shape

Not done. This is a plan and a decision to be taken, written down on 6 September 2026 so that the next person to pick it up starts where this left off rather than where it began.

## The complaint

Adding one heteronym to English took a day and did not land. Adding one word to a dictionary worked, after the writer that does it was found to have been broken since the rule compiler landed. Neither of those is a hard problem in any other synthesiser: eSpeak wants a line in a lexicon file saying `produce $noun` and a compiler run.

The reason is that **the authoring surface is a reverse-engineered virtual machine**. `lang/<tag>/rules` is Delta's bytecode written out as text -- readable, editable, and still Delta's model. Even the upper form, which is a real improvement and a real compiler, asks an author to think in frames, slots, planted tests, arms and immediates. That is a disassembler with manners rather than a language for writing rules in.

eSpeak is easy for one reason and it is not that its engine is simpler: its source format was designed for a person to write, and a compiler turns it into whatever the engine wants. Here the engine's own form *is* the source format.

So the principle to fix it: **the Delta machine should be an implementation detail, not the interface.** Everything below follows from that.

## What today established, since it bears on the choice

Writing a dictionary arm in the lower form is an hour's work and now supported -- `tools/rules/newarm.py`, and `docs/status.md` has the fault it fixed. Writing a *heteronym* arm is not, and the reason is instructive. A working two-reading arm names no records at all: it hands two frame addresses to a shared `test_noun_verb` and identifies the word by a bare immediate, so the two readings are reached through code keyed by that number. Which table that number indexes has not been traced. Every feature beyond the simplest is its own archaeology.

The filter written instead -- `src/eci/hetero` -- works and is opt-in, and the reason it cannot be on by default is also evidence here: loading any filter turns annotation reading on for the whole instance, and there is no way to escape a backtick in the caller's text. Attempting the escape in `et_processAnnotations` made it worse, because the parser that matters for filtered text is further down, in the rules. `docs/notes/heteronyms.md` has all of it.

## Three routes

**Compile our own format down to Delta.** One engine, nothing else changes, everything keeps working. And every construct we want to emit is a fresh piece of archaeology on IBM's idioms, of which the two-reading arm is one example and not the worst. Not recommended.

**A lexicon and letter-to-sound layer of our own, beside Delta, consulted first.** Author in a format we design; a layer of ours answers for what it knows; Delta answers for everything else. No archaeology, complete freedom over the format, incremental, and it cannot break the eight lifted languages because they never reach it. **Recommended**, explicitly as the road to the third: every word moved into it is a word out of Delta.

**Replace Delta entirely.** Where this ends up. Not the next step, because the rules do prosody, intonation, normalisation and morphology as well as letter-to-sound, and starting there means reimplementing a synthesiser rather than building an authoring system.

## The decision that makes or breaks it

Where the layer injects.

As a filter writing annotations: proved fragile, for the backtick reason above. Not this.

At the dictionary lookup: the clean answer, because the engine already carries a runtime dictionary with a part of speech on it -- `eciMainDictExt`, `eciUpdateDictA` and `ECIPartOfSpeech` -- which is exactly what a heteronym needs and exactly what eSpeak's `$noun` is. **Whether that dictionary reaches the synthesiser at all is the first thing to settle and it is not yet known.** A rough test on 6 September taught `produce` through `eciUpdateDict`, was told it succeeded, and then got no phonemes out of the instance at all -- which is either a mistake in the test or something worth knowing, and either way is where to start.

Deeper still, replacing words in the engine's own internal form before the rules see them: cleanest and most work.

## How to design the format

Write the content first. Take the seventeen heteronyms and a slice of the community dictionary and write, by hand, the file you wish existed. Then build the smallest loader that makes that file work. A format designed the other way round ends up serving the compiler rather than the author, which is the mistake Delta's surface already makes.

## What makes it safe

`make words` -- 24,318 words, ten seconds, names what moved. Any layer that changes how words are said is precisely what it was built to police, and this plan would be much harder to justify without it.
