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

## What the seam turned out to be, 9 September 2026

The plan above says the Delta machine should be an implementation detail rather than the interface, and leaves open where a layer of ours would inject. Measuring answered it, and the answer is lower and narrower than any of the three routes assumed.

`KlattSynth` takes one frame of sixty-two parameters and answers a run of samples, and it is the only way anything reaches the formant engine. Everything above it -- rules, dictionaries, prosody, intonation -- exists to produce those frames. So that is the seam: a front end of ours has to produce parameter frames, and `src/klatt` stays exactly as it is, which is what keeps it sounding like Eloquence.

**And the comparison is exact.** Same text, same frames, identical audio, because the synthesiser below does not change. No ear, no alignment, no judgement about whether it sounds close enough. That is a completely different standard from the one route three was judged against, and it is why replacing the front end is a smaller thing than it looked: it is not reimplementing a synthesiser, it is writing a text-to-parameters program with an oracle.

### What the data is

`src/port/evv_klatttap.c` writes every frame, off unless `EVV_KLATT_TAP` names a file, and the audio is byte identical with it idle. `tools/measure/formants.py` runs a corpus through.

Over `test/cases/plain.txt` -- seven sentences, 10,982 frames -- English drives twenty-five of the sixty-two parameters and leaves thirty-seven alone. Formants six, seven and eight are nailed at 5000, 6300 and 7500, the nasal and tracheal poles and zeros are fixed, and every parallel-branch voicing amplitude is nought throughout. Which says what Eloquence is: **a cascade-only Klatt with five moving formants and three fixed ones.**

The tracks are piecewise linear. Fitting straight segments wants 456 pieces for f0, about 1,450 each for f1 to f3, nine hundred to a thousand for the amplitudes, 342 for `ab`, and thirty-five for `oq` over the whole fifty-five seconds. Twenty-five tracks of fifty-five seconds is some eight thousand breakpoints: targets and the times they are reached, joined by straight lines.

### What a vowel is

`tools/measure/phonetargets.py` speaks a phoneme alone as `` `[.1X] `` and writes what the synthesiser was told. `lang/measured/enus-vowels.txt` is the result for the sixteen vowels, and it is **eight numbers a vowel**: two formant triples, the fraction of its length it holds the first before gliding to the second, its voicing, and a first bandwidth that only two of them move. Eighteen further parameters are the same for every vowel and are said once.

The hold fraction is what makes one shape do for both kinds. A monophthong holds a hundred per cent and the second target never arrives; `i` sits at 270, 2190, 2800 from end to end. A diphthong holds one per cent and glides the whole way; `e` runs 470, 1800 to 350, 2000 and `Y` runs 750, 1200 to 400, 1850. And `O` holds sixty-one per cent, which is /ɔɪ/ dwelling on its first element -- a fact about English that fell out of the measurement rather than being put in.

Durations are 240 to 325 milliseconds for a stressed vowel alone.

### What is not done, and what it costs

`tools/measure/replay.py` builds frames from the table and holds them against the engine. No vowel reproduces exactly yet, and the two reasons are the next design decisions rather than faults:

The amplitudes have envelopes. `av` differs in about sixty per cent of frames because it ramps in and out rather than holding, and `ah`, `oq` and `di` differ in the first few. A single number per vowel cannot say that; an attack and decay can.

Consonants have no steady state and cannot be spoken alone -- a pronunciation annotation wants a pronounceable syllable, and `` `[s] `` is read as the literal characters. In a carrier they measure cleanly: `asa` peaks at `af` seventy, `aSa` fifty-eight, `afa` fifty-five, `aTa` sixty-four, while `ama`, `ana` and `ala` stay at nought, which is right. So a consonant is a locus and a transition rather than a target, and how to say that is the format's next question.

Per-phoneme durations in running speech are still not available: the rules only fill a phoneme's proportion when a caller has asked for phoneme indices, and asking redirects the audio to a callback, so frames and durations cannot come from one utterance. Isolated phonemes give their own durations by frame count, which is what the table's `ms` column is.

### The format holds: one vowel reproduces byte for byte

`tools/measure/replay.py` builds frames from `lang/measured/enus-vowels.txt` and holds them against what the engine actually gave `KlattSynth`. `i` now matches on every parameter of every frame, and `A`, `a`, `X`, `x`, `e`, `o` and `c` differ in one frame each. That is the format validated: eight numbers a vowel, plus an utterance layer, reproduce the engine exactly.

The utterance layer turned out to be four rules and they are worth having written down, because none of them is the vowel's and all of them had to be found by looking:

Open quotient rises 18, 27, 36, 45, 54 to its resting 56 over five frames, and diplophonia falls 100, 77, 53, 29, 5 to nought over the same five. A creaky start, the same every time, recorded as measured rather than fitted.

Voicing holds its plateau for two fifths of the vowel and then drops by one. Not half: `a` holds twenty-seven of its sixty-five frames, `i` twenty-one of fifty-three, `E` twenty-two of fifty-five.

Voicing lets go over the last thirteen frames -- sixty-five milliseconds, the same however loud it was -- starting two below the plateau and falling to nought in twelve even steps, **rounded up rather than to nearest**. `i` gives 53, 49, 45, 40, 36, 31, 27, 23, 18, 14, 9, 5, 0, which is `ceil` and not `round`, and getting that wrong is most of what stood between close and exact.

Aspiration rises by one from the second frame of the release.

### What is left, and it is bounded

The release top is two below the plateau for twelve of the sixteen and one below for four -- `I`, `E`, `U` and `H`. That is one rule not yet found rather than four exceptions, and there is a clue: those same four are the only ones whose release ends at 2 rather than 0, `... 11 7 2` where the others give `... 8 4 0`. They also pair off exactly, `I` and `U` both fifty frames at 57, `E` and `H` both fifty-five at 54, which says the split is about duration and loudness together rather than either alone.

The diphthong glide is the wrong shape. Holding the first target then interpolating linearly to the second leaves `I`, `e`, `o`, `u`, `Y`, `W` and `O` differing across most of their frames, so the transition is not a straight line between the two targets over the remaining time. `W` also moves `b3`, `f4` and `f5`, which the table says are shared -- so it is not only the glide that a diphthong does.

And `c` and `O` move `b1` during the release, which the table has as a constant.

None of that is archaeology. It is all visible in the frames, and every attempt is a run of `replay.py` away from being told exactly how wrong it is -- which is the whole point of measuring against the engine rather than by ear.
