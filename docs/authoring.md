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

### All sixteen vowels reproduce byte for byte

`tools/measure/replay.py` builds frames from `lang/measured/enus-vowels.txt` and holds them against what the engine actually gave `KlattSynth`. All sixteen English vowels match on sixty of the sixty-two parameters of every frame -- everything except `step`, which is the frame counter, and `f0`, which is the intonation and belongs to the utterance rather than the vowel.

Four sabotages prove the check is not vacuous: a shared resting value, a value only one vowel holds, one breakpoint's value, and one breakpoint's denominator. Each is caught, and one of them was caught only after a first attempt at it silently failed to edit the file at all, which is exactly the false pass this project's habit of sabotaging on purpose exists to find.

### The format is breakpoint tracks, because a vowel is not two targets

The first format tried was eight numbers a vowel: two formant targets and the fraction of its length it holds the first. That reproduced seven of the sixteen and could not be made to reproduce the rest, and the reason turned out to be a fact about the vowels rather than a bug in the arithmetic.

**/aU/ moves three times.** Its f1 drifts 750 to 700 over forty-three frames, then runs to 550 over ten, then holds. Its f2 does the same shape from 1400 through 1300 to 900. And its third bandwidth, fourth formant and fifth formant move out and come back -- `b3` goes 150 to 500, sits there five frames, and returns to 150 over the last eight -- which no other vowel does at all, so the table had them as shared constants. **/aI/ moves twice** and not from frame nought: its f2 drifts 1200 to 1350 over thirty-six frames and only then runs to 1850. Its f1 and f3 hold flat for those thirty-six frames and glide in the last fifteen, which is what a diphthong actually is: a target, held, and then a fast transition, not a slow interpolation across the whole vowel.

So the format became what the synthesiser is already told: **a piecewise-linear track per parameter, written as the frames at which it turns and the value it turns at.** Between two breakpoints,

    v0 + int((v1 - v0) * min(den, 2 * (i - i0)) / den)

with `den` the segment's length in half-frames. `tools/measure/tracks.py` fits those breakpoints from the engine's own frames, greedily taking the longest segment that still reproduces every value exactly.

Three things fell out of that form for free, each of which had been a special case before:

The clamp inside the `min` is the hold. A segment written `42:700/20` reaches 700 ten frames later and stays there, so "glide then settle" needs no separate rule.

**The half-frame denominator is why four vowels looked anomalous.** `I`, `E`, `U` and `H` had appeared to break every rule about voicing -- releasing from one below the plateau rather than two, and stopping at 2 rather than nought. In half-frames there is nothing wrong with them: their segments are one half-frame shorter than the obvious length, `den` says so, and the same walk produces them and everything else. What had looked like four exceptions to a rule was one unit of measurement being wrong.

And the truncation is the whole of the arithmetic. `int` towards nought, as C converts, which makes a falling parameter appear to round up and a rising one down: 470 less 2.5 is 468 because `int(-2.5)` is -2, while 1800 plus 4.17 is 1804. No rounding rule reproduces both.

### All twenty-six consonants reproduce byte for byte too

A consonant cannot be measured on its own. Asked for one, the engine spells the letter out and hands back four hundred live frames of it, so each is measured in a carrier -- between two /a/ -- and `lang/measured/enus-consonants.txt` holds all twenty-six. With the sixteen vowels that is 42 of 42 cases reproducing every one of the sixty parameters of every frame. Two sabotages on the consonant table prove it, one moving a locus by a single hertz and one lying about a frame count.

The carrier shows the structure the format was hoping for. In `ama` the first /a/ holds its targets to frame 30, six frames carry every parameter to the /m/, the /m/ holds from 36 to 46, and six more carry it out. Aspiration goes to nought through the nasal and voicing *rises*, from 49 to 52, which is a real thing about nasals and not an artefact. So a consonant is a locus and two transitions, exactly as expected.

### The locus is coarticulated, and it is separable, which is what matters

The first measurement of this said the locus does not compose, and that was wrong. It was measured only in symmetric carriers -- /a/-C-/a/, /i/-C-/i/ -- where both neighbours change together, so there was no way to tell which of them was doing the work. Asymmetric carriers answer it immediately.

Compare the transition into /m/. In `ama` f1 runs 750 to 300; in `ami` it runs 750 to 250. Same preceding vowel, same starting value, different destination -- so the destination is set by what comes *after* the consonant. And in `ima` f1 runs 270 to 300 while in `imi` it runs 270 to 250, which says the same thing from the other side.

So the shape of a consonant between two vowels is this:

**f1 holds one constant across the whole consonant, and the following vowel sets it.** For /m/ it is 300 before /a/, /e/ and /o/ and 250 before /i/ and /u/ -- which is to say before a close vowel. The transition into the consonant therefore aims at a value chosen by the phoneme after it, which is the engine reading ahead, and is why no amount of measuring in symmetric carriers could separate the two.

**f2 and f3 ramp straight across the consonant, from a value the preceding vowel sets to one the following vowel sets.** For /m/, f2 starts at 1147 after /e/ whatever follows -- 1147, 1155, 1147, 1147 across `ema`, `emi`, `emu`, `emo` -- and ends at about 1250 before /e/ whatever precedes: 1240, 1250, 1250, 1218 across `ame`, `ime`, `ume`, `ome`. The thirty-hertz spread in the second set looked like ramp arithmetic, those holds being ten and eleven frames long. **That explanation is wrong and was tested at scale: over 640 pairs with holds of equal length, 230 agree and 390 still differ.** What the spread really is appears below.

That the two ends separate is the whole game, because it says what the corpus is. **A table of consonant-vowel pairs is enough: 26 by 16, and one Latin square of 416 carriers measures both sides of every pair at once**, since `aCi` reports /a/-before-C and C-before-/i/ in the same utterance. The earlier note here guessed a few thousand carriers because it assumed the full cross product; separability is what makes it 416 rather than 6,656, and about ten minutes rather than an afternoon.

**An obstruent behaves the same way and more cleanly than a nasal.** /t/ and /s/ hold f1 at 300 in every context measured, and their f2 locus is 1500 after /a/ or /u/ and 1800 after /i/ -- so a vowel can raise an alveolar's locus but not lower it, /a/ at 1200 and /u/ at 870 both leaving it where it was. Their `out` values agree across contexts to within ten hertz, 1740 against 1750 before /i/.

Worth knowing for later: between two /a/, /t/ and /s/ sit at exactly the same place -- f1 300, f2 1500, f3 2550, with b1, b2 and b3 unmoved. What tells them apart is entirely the frication and the voicing. /t/ gives one frame of `af` at 48 and drops `av` to nought for its closure; /s/ ramps `af` to 70 and holds it for twenty frames. So manner lives in the noise sources and place lives in the formants, which is the textbook account and is reassuring to find rather than have to assume.

What is still unknown is whether the two directions share one table. /m/'s f2 is 1300 leaving an /i/ and about 1390 approaching one, which is close enough to be suspicious and far enough apart not to assume.

### Separability, measured over 832 carriers rather than four

Two Latin squares are in `lang/measured/enus-pairs.txt` and `enus-pairs2.txt`: 416 carriers each, the second pairing every consonant with the vowel three places along rather than one, so every (consonant, vowel) pair is measured twice with a different vowel at the far end. All 874 cases reproduce exactly. If a value belongs to the neighbour that sets it, the two squares must agree, and `tools/measure/loci.py --compare` says how far they do.

Six consonants have to be left out, and the reason is the finding that made the rest trustworthy. **Aspiration is the marker: it sits at 34 through a vowel and at nought through a consonant, and that boundary is exact** -- over the twenty-six consonants between two /a/, the `ah == 0` span agrees frame for frame with the stretch held at one f1 for twenty of them. The six it disagrees about are the six a plateau is the wrong question for: /h/ is aspiration and never suppresses it, and /r/, /l/, /y/, /w/ and /R/ are sonorants with no closure, a glide being a continuous transition rather than a target held. An earlier version of this guessed at those by taking the longest interior run at one f1, found stretches of the *vowels* instead, and reported an f1 of 272 against 742 for the same pair -- which made separability look far worse than it is. **A detector that always answers is worse than one that admits it cannot see.**

On the twenty that do have a closure, the answer splits by parameter:

**f1, b1, b2, b3, f4, f5 and ah separate outright.** f1 agrees on 632 of 640, and the other six on 640 of 640. So the value a consonant holds for those is genuinely a function of one neighbour and nothing else.

**f2 and f3 do not, and the size of the failure tracks how long the closure is.** /T/ holds its closure for 22 frames and its worst disagreement is 11 Hz; /s/, /S/ and /f/ hold 20 and land within 15; /D/, /m/ and /v/ hold 10 and reach 24; /F/, /d/, /n/ and /t/ hold 5 and reach 48. **/p/'s closure is a single frame and /k/'s is two or three.** So for most of them this is not a fact about the engine but about the measurement: a locus read off a plateau five frames long is a point on a slope that has not finished moving, and reading it off a one-frame plateau is barely reading anything.

**The velars are the real exception.** /k/, /g/ and /G/ disagree by 687, 208 and 309 Hz while holding closures of two, thirteen and twelve frames -- so /g/ and /G/ are as well sampled as /b/ and /v/, which land within 24, and are wrong by an order of magnitude more. An F2 locus that depends on whether the *other* neighbour is front or back is what a velar is famous for, and the engine evidently models it. So the velars want the full cross product, 3 by 16 by 16, which is 768 carriers and no new method.

The way to settle the rest is not more carriers but a better reading. The in-transition is a fitted straight line whose endpoint is the target the engine was aiming at, so the locus should be taken from the breakpoint rather than from whatever the plateau had reached -- which costs nothing, the fits being already in the tables.

### The timing separates completely, and to the left

`tools/measure/split.py` asks the question the locus reading cannot: not what value a consonant holds but *when* it holds it. The two squares give, for every (consonant, vowel) pair, two carriers sharing that vowel and that consonant, so any timing difference between them is the far vowel reaching across the consonant.

Grouped by the consonant and the vowel **before** it, over 320 pairs:

The closure begins at the same frame in 320 of 320. The closure lasts the same number of frames in 320 of 320.

Grouped by the consonant and the vowel **after** it instead, the closure begins at the same frame in only 90 of 320 -- as it must, the first vowel differing and so shifting everything -- and lasts the same number of frames in 300 of 320.

So **when the closure happens and how long it lasts are functions of the preceding vowel and the consonant, and nothing else.** That is the first thing in this whole exercise that separates completely rather than approximately, and it is worth more than it looks: a front end that knows the phoneme sequence can lay out the timing of every closure from a table of 416 pairs before it computes a single formant value.

What does not separate is the length of what follows. The frames after the closure agree in only 202 of 320 pairs sharing the consonant and the following vowel, so how long the second vowel runs depends on what came before the consonant -- a rhythm effect, and the first sign in these measurements of anything reaching further than one phoneme.

One thing this test cannot do, and it is worth saying why rather than reporting a number nobody should trust. Comparing the run-in frame by frame measures nothing about separability: the transition into a consonant aims at an f1 that the *following* vowel sets, so two carriers sharing their first vowel and their consonant are supposed to differ all through the run-in. A first version of this reported that they agree for 0 frames and called it a failure, when it was the design.

### Generating an utterance nobody measured: the run-in is solved, the rest is not

Every table above reproduces the engine exactly, and that proves the measurement rather than the format. What tests the format is generating a carrier nobody measured. `lang/measured/enus-holdout.txt` is a third Latin square at a vowel offset neither training square uses -- carrier k is vowel k, the consonant, vowel k+2, where the training squares use k+1 and k+3 -- so no carrier in it appears in either, and `tools/measure/compose.py` has seen no frame of it. All 1,290 measured cases across the five tables reproduce exactly, so the held-out square is trustworthy ground truth without running the engine again.

Composing all 320 held-out carriers from the training squares gives, as it stands:

Five reproduce every one of the sixty parameters of every frame. Eighty-nine have every formant within one per cent, which is below the ear's threshold for telling two formants apart. Eighty-nine come out the wrong length, and never by more than a frame.

Broken down by where in the utterance the error is, against the number of values compared:

The **run-in** is 54 wrong out of 541,800, which is 0.0 per cent. The **closure** is 4.7 per cent. The **run-out** is 2.2 per cent.

**So the run-in is solved, and the thing that solved it is worth keeping.** Rescaling one measured run-in to a new target needs to know where the vowel stops and the transition starts, and getting that boundary wrong distorts the vowel's own glide -- a first version did exactly that and came out 15 per cent wrong on /p/ and 27 per cent on /J/. There is no need to know. Each side of a carrier has *two* measured neighbours, one from each training square, aimed at two different targets, and a run-in is linear in its target: /a/ into /m/ reaches one tenth and fourteen fifteenths of the way whether that way ends at 300 or at 250. So the answer is the straight line through the two measurements evaluated at the target wanted, and where the two measurements agree it returns that value, which is how the vowel's own portion comes through untouched without ever being located.

**What is not solved, and what was tried.**

The run-out is still the right carrier's own frames, spliced. The same interpolation applied to it, parameterised by the closure's first frame, made the answer worse -- 82 carriers within one per cent against 88, the run-out's own error unmoved -- so whatever the preceding vowel does to a run-out is not linear in the closure's onset.

Most of what is wrong in the run-out is voicing, and voicing is not a phoneme's property at all. It declines in a staircase across the whole utterance, holds the consonant's own value through the closure, resumes declining and lets go at the end, so splicing two carriers of different lengths lands every step after the join in the wrong place. A staircase of one step every nine frames with the remainder given to the first step, fitted to two carriers and tried, made it much worse: 8,085 frames wrong against the splice's 3,892, and no carrier exact at all. The nine does not generalise and the rule is not found.

The closure's remaining 4.7 per cent is the residual non-separability, and it is small but real. For /m/ between /E/ and /a/ the true closure runs 1147 to 1012; composition gives 1152 to 1004, because the left training carrier's own following vowel was /A/ rather than /a/ and the right one's preceding vowel was /A/ rather than /E/. Five hertz and eight hertz. The velars are the exception that is not small, as above.

**The honest summary is that pair tables get within a few hertz and not to the byte.** Whether that is enough is an audio question rather than a measurement one, and it is now answerable by ear whenever somebody wants to: the composer writes frames, and frames are what the synthesiser takes. Byte-exactness would need the full cross product, 16 by 26 by 16, which is 6,656 carriers and about seven hours of measurement with no new method required.

### Listened to, and the answer is that pair tables are enough

Counting wrong parameter values was the wrong measure and the ear said so. `test/harness/klattplay.c` drives `KlattSynth` directly from a file of frames -- the other half of the tap, which only ever read them out -- so a composed utterance and a measured one can be rendered through the same code and heard against each other, the only difference being the frames. `tools/measure/hear.py` does that over the held-out square and reports the waveform difference, and it builds a self-describing A/B file a case, the engine speaking which side is which so there is nothing to read alongside the sound.

Stas listened on 9 September 2026. Of the first five, spanning nought to 36 per cent formant error, he could not tell any pair apart, and the one he thought he might have heard turned out to have the *smallest* waveform difference of the four. Of the three worst in the whole set, he heard one.

**The waveform difference over all 320 held-out carriers**: 55 under 2 per cent, 63 between 2 and 5, 89 between 5 and 10, 72 between 10 and 20, 17 between 20 and 40, and 24 above 40. Median 7.1 per cent. Only seven exceed 80 per cent and only **two exceed 85, both of them /J/**. The `/g/` case at 82.9 per cent was not audible; `/J/` at 98.7 was.

**So 318 of 320 compose to something indistinguishable, and the format is usable as it stands.** The full cross product -- 6,656 carriers, seven hours -- is not needed and should not be measured. That was the open question and it is answered.

### Two corrections the ear forced

**The parameter count overstates errors that land in silence.** /k/ between /A/ and /u/ has a formant 32 per cent out, and that frame's voicing is nought -- frication only -- so there is almost no sound for it to be wrong in. Eleven wrong values there come out quieter than eighty-eight in a nasal. Any future metric here should weight by what is audible, or better, just render and compare the samples.

**"The velars are the exception" was the wrong grouping.** In parameter terms it held: /k/, /g/ and /G/ disagree between squares by hundreds of hertz where everything else lands within tens. But in audible terms /k/ and /G/ are 0 of 16 over 20 per cent, and what actually fails is **the voiced obstruents** -- /J/ 16 of 16, /g/ 9 of 16, then /z/, /Z/, /v/, /D/ and /n/ with a handful each. Every other consonant is 0 of 16, /b/ and /d/ included. So composing a *voiced* closure is wrong in a way a voiceless one is not, and the two facts are about different things: place of articulation moves the numbers, voicing moves the sound.

**/J/ is the one real defect and there is an obvious suspect.** It is an affricate, which is a stop and a fricative in sequence, and it is being composed as though it were one closure with one locus. Its sixteen contexts run from 48 to 99 per cent, worse than anything else by a wide margin. Modelling it as two segments rather than one is the thing to try, and it is cheap.

### /J/ fixed, and with it every case above twenty per cent

The affricate was the only audible defect and the suspect was right, though not for the reason guessed. It is not that /J/ needs two segments spelt out. It is that **a closure must not be crossed with a straight line**, and /J/ is the phoneme where that shows worst.

Look at voicing through a closure. /C/, voiceless, holds `av` flat at nought for all seventeen frames, so a line between its ends is right by accident -- which is why /C/ composed fine all along. /J/ ramps 0 0 4 12 20 27 35 40 41 43 44 45 as voicing returns through the affricate, an S-curve. /g/ steps: 20 for twelve frames, then nought for three while `af` jumps to 62 for the burst. Neither is a line between its endpoints, and imposing one is exactly why the voiced obstruents were the ones that failed.

The fix is the same idea that solved the run-in, extended over the closure: take the measured *shape* from the two training carriers that share the near pair and interpolate it in the target, rather than inventing a ramp between two endpoints. Three things had to be got right and each was found by the answer getting worse:

**Only the closure has to line up, not the whole carrier.** The two left carriers share their first vowel and their consonant but not their second, so their total lengths differ -- /J/'s are 98 and 82 frames. A first version required those to match, which silently skipped the shape for every consonant and left /J/ exactly as wrong as before.

**Two measurements that agree mean the parameter does not respond, and the measurement stands.** Rescaling in that case instead cost 24 exact cases, because it distorts everything that genuinely does not move.

**Two measurements that nearly agree say nothing at all, and must say so.** For /g/ before /u/ the two left carriers' f2 closure targets are 1658 and 1652 -- six hertz apart -- while the wanted target is 1208. The straight line through them has a weight of 75, and every difference between the two gets multiplied by it: /g/, /k/ and /G/ before /u/ went from six per cent wrong to two hundred and twenty. That is the velar pinch, and it is unidentifiable from these two carriers, because neither of their far vowels is back. Rescaling the shape there was tried and was no better. What works is answering nothing and letting the caller draw the straight line it would have drawn anyway.

**The result over all 320 held-out carriers**: 148 under 2 per cent, 70 from 2 to 5, 89 from 5 to 10, 13 from 10 to 20, and **nothing above 20**. Median 2.1 per cent against 7.0, worst 19.4 against 98.7, and nothing left in the range Stas could hear -- he judged 25.6 per cent indistinguishable and even 82.9. /J/ itself went from 98.7 to 8.0. In frames: 63 cases exact against 5, the run-in still 0.0 per cent, the closure down from 4.7 to 1.2.

The run-out is now the largest remaining error at 2.2 per cent, and most of it is voicing, which is an utterance-level staircase rather than a pair's business.

### The stretch law: a phoneme is two endpoints and a length

The tables described utterances rather than phonemes because every breakpoint in them is an absolute frame number at the one length that case happened to be. That is now answered, and it needed no new measurement at all: a vowel before a voiceless consonant is shorter than the same vowel before a voiced one -- /a/ runs 32 frames before /p/ and 37 before /b/ -- so the pair corpus already holds vowels at two lengths.

Comparing them gives the law in two parts.

**The voice-quality onset does not stretch.** Open quotient rises 18, 27, 36, 45, 54 and diplophonia falls 100, 77, 53, 29, 5 over five frames whatever the phoneme's length. Five frames, fixed.

**The body stretches and keeps its endpoints.** /a/'s f2 glides 1200 to 1151 at both lengths, in 26 steps at the short one and 33 at the long, and the engine's own segment arithmetic reproduces both from the same two numbers -- `v0 + int((v1 - v0) * min(den, 2 * i) / den)` with `den` twice the number of steps. The `av` droop moves with it, frame 16 of 32 against 19 of 37.

`tools/measure/stretch.py` tests that by predicting each vowel's body at one length from the same vowel's two endpoints at another. **66 of 70 parameters come out exactly**, and /c/ and /O/ are exact on all fourteen. The four misses are not the law failing: each is a body *endpoint* differing by forty to sixty hertz between the two contexts, which is the same coarticulation the pair tables already record, reaching back into the vowel from the consonant after it.

One thing had to be got right and cost a round. **The body is not the whole stretch up to the closure** -- the run-in belongs to the consonant, not the vowel, and /a/ glides its f2 over 26 frames and then runs in over 6 before /p/, or over 33 and then 4 before /b/. Taking everything up to the closure as the body left the law unable to describe even its own source, and the fix is to find the body as the longest straight line the law does describe.

So a phoneme is two endpoints and a length, which is what makes these tables about phonemes rather than about the utterances they were measured in.

### A whole utterance, and the voicing envelope is the last thing in the way

`tools/measure/chain.py` composes more than one closure. A word is a chain of overlapping pair contexts -- in /atapa/ the /t/ is the (a,t) and (t,a) pairs, the /p/ is (a,p) and (p,a), and the /a/ between them is the run-out of one meeting the run-in of the next -- so the pair tables are enough for a word if they can be stitched.

What it does not do is decide the timing. When each closure starts and how long it lasts is a language's business, settled long before the synthesiser sees anything, so the frame layout is taken from the engine's own frames for the same text. That separates the question these tables can answer, which is whether they describe real speech, from what a language chooses to do, which is not a formant question at all.

The stitch had one bug worth recording because it was invisible in the totals. Writing the run-out of one closure and the run-in of the next into the same stretch and letting the second overwrite the first leaves the vowel with **no run-out at all**, and puts 179 of 180 wrong values in the vowels while the closures come out nearly perfect. Meeting them in the middle, with the vowel holding between, took /atapa/ from 39 per cent to 13.5 and /aCaSa/ from 42 to 10.8.

**And then one parameter turned out to be nearly the whole of what was left.** Lending `av` from the measured utterance and composing everything else:

/atapa/ 13.5 per cent becomes 8.6. /aCaSa/ 10.8 becomes 3.0. /akaga/ 40.3 becomes 7.3. /asaka/ **72.6 becomes 7.7**. Lending `af` and `ah` as well changes almost nothing further -- 8.6 stays 8.6, 7.7 stays 7.7.

So with voicing right, every chain composes to between three and nine per cent, which is well inside what has been shown inaudible. **The voicing envelope is the single remaining piece**, and it is not a formant problem: `av` declines in a staircase across a whole utterance, holds each consonant's own value through its closure, and lets go at the end. In one carrier a spliced staircase is nearly right by luck. Across two closures the errors compound, which is why /asaka/ was five times worse than /atapa/ from the same tables.

That is also the third time this has surfaced -- in the isolated vowels, in the single carriers' run-out, and now in chains -- so it is the thing to do next. A staircase of one step every nine frames was fitted to two carriers and tried once and was much worse than splicing, so the rule is genuinely not known yet.

### The metric was wrong, and the ear said which one to use instead

Stas heard /akaga/ at 40 per cent waveform difference and barely heard /asaka/ at 72. The difference between them was not size but kind, and his description named it: "ours slips a bit, almost like the synth loses his voice for a second before coming back."

That is exactly what it was. /akaga/'s middle vowel had **twenty-four frames with `av` at nought where the engine has 47 declining to 44** -- a hundred and twenty milliseconds of the voice cutting out and coming back. /asaka/'s 72 per cent is formants in slightly the wrong places, and that is forgiven.

So the metric is not the waveform ratio. **It is whether the voice stops.** `dropouts()` in `tools/measure/compose.py` reports every run where the engine is voicing at 20 or more and the composition is under 10 for three frames or longer, which is fifteen milliseconds and about where a gap stops being a click. By that measure: **none of the 320 held-out carriers cuts out, and none of nineteen chains does either.** The waveform ratio stays in the reports as a rough guide, and is now known to be a poor predictor.

The cause of the dropout was a rule of mine and worth recording. Both sides of a vowel between two closures are now handed over whole, and `stitch` cuts the middle. Trying to find where each transition ends first was worse than not trying: **a run-out begins with a plateau** -- after a /k/ closure the voicing is still nought for several frames before it returns -- so a rule that stopped at the first repeated value captured one frame of silence and held it across the entire vowel. Handing both sides over whole took /akaga/ from 40.3 per cent to 12.3 and removed the dropout.

### It works at every rate, given that rate's own tables

Stas asked whether speeding the engine up would change anything, on the grounds that a fault can hide at a normal rate and show at a fast one. It does, and he was right to ask: with the tables measured at probe's own 175 words a minute, composing at 450 gave 80 to 100 per cent waveform difference, **voicing dropouts from 350 upwards**, and above 450 the segmentation failed outright.

Three facts settled it.

**Speed changes the number of frames, not their length.** `step` stays five milliseconds at every rate, so a short utterance is 115 frames at 175 words a minute and 12 at 700. Laying a shape measured at one rate down at its measured length is therefore wrong by a factor of ten across the range.

**The format itself is rate-independent.** Every table reproduces the engine exactly at its own rate: 2,538 of 2,538 cases with the 450 tables in, and the breakpoints, the truncation and the half-frame denominators all hold unchanged.

**And measuring a rate is cheap.** All three squares at 450 words a minute took ninety seconds, against the better part of an hour at 175, because there is a tenth as much to fit. So the answer is a table per rate rather than a stretch law stretched across the whole range, and `tools/measure/tracks.py` takes a rate as its third argument.

Measured at ten rates from 200 to 700 words a minute, composing each from its own tables, the held-out carriers give: **no dropouts at any rate**, the run-in 0.0 to 0.2 per cent wrong, the closure 1.2 to 1.4, the run-out 2.2 to 3.3. At 175 from its own tables those are 0.0, 1.2 and 2.2, so the method does not care about the rate at all once the tables match it. Chains likewise: no dropouts at any rate, and ratios mostly between 8 and 35 per cent.

**One structural fix was needed.** Aspiration returning during a vowel is what separates two closures, and at speed the vowel between them is too short for it to return, so both read as one stretch. `closures()` now takes how many consonants the phoneme string has and splits the longest stretch at the loudest frame inside it: voicing is nought across a voiceless stop, high across the vowel, low but present across a voiced one, so the peak is the vowel.

**And one limitation, which counting made obvious.** At 175 words a minute 96 of the 416 carriers have no findable closure, and 96 is exactly the six consonants that have none by nature -- /h/, /r/, /l/, /y/, /w/ and /R/ -- times sixteen vowels. At 450 it is 106 and at 700 it is 124, so speed costs another ten and another twenty-eight as obstruents lose theirs too. Those pairs are missing from the tables, which is why a chain containing one is refused rather than composed badly. They want an anchor that is not a closure, and that is the second time this gap has appeared: `loci.py` already answers "no plateau to compare" for the same six.

### The numbers are in the rules, and measuring where they land was the wrong way round

Every phoneme in `lang/enus` is declared "at" one of nine rules named for a place of articulation -- `eng_lab_Fv`, `eng_alv_Fv`, `eng_vel_Fv`, `eng_pal_Fv`, `eng_ret_Fv`, `eng_lat_Fv`, `eng_intd_Fv`, `eng_high_pal_Fv`, `eng_bilab_Fv` -- and `tools/module/phonemes.py` prints which. Those rules **compute the formant values**, and they are already in the tree as C, `make rules` having written them out.

What is inside them is not a model of anything. It is the numbers:

`eng_lat_Fv` sets `s439` and `s440` to 800 and `s441` and `s442` to 3000. `eng_alv_Fv` sets them to 1500 and 2550. `eng_lab_Fv` sets 1000, then 2200 and **2250**, then `s443` 3300 and `s445` 3600.

Set those against what a day of measuring found. /l/'s f2 goes to 800 and its f3 to 3000. /t/ and /s/ between two /a/ sit at f2 1500 and f3 2550. /m/ between two /a/ holds f2 at 1000, runs f3 from 2200 to 2248, and takes f4 to 3300 and f5 to 3600. **Every one of those numbers is an immediate in a rule.** And `s441` against `s442` -- 2200 and 2250 -- is the finding that a locus separates into a value the preceding vowel sets and a value the following one sets, which took two Latin squares and 832 carriers to establish. It is two variables.

The context-dependence is there too, as branches rather than as noise. A few lines further into `eng_lat_Fv`:

    STATE(int16_t, s439) = (65534);
    STATE(int16_t, s441) = (65534);

set after a `test_string_s`, 65534 being -2 as an int16 and so a sentinel. **So coarticulation is conditional logic.** That is why the velar pinch was "unidentifiable from pair tables" and why a run-out's level came out a hundred hertz wrong: the output being sampled is a decision tree's, and no curve fits it.

**So the route to formant values that are the engine's rather than nearly the engine's is to read those nine rules.** Exact by construction rather than fitted; it explains the coarticulation instead of approximating it; and it needs no corpus at all -- no 416 carriers, no eleven rates, no cross product. `eng_lat_Fv` is about 480 lines of generated C.

### What `eng_lat_Fv` actually is, and the four steps to read one

Read out, `eng_lat_Fv` is a base locus and fourteen context-dependent overrides, every value an integer immediate:

The base sets f2 to 800 for both halves and f3 to 3000 for both, and then sets the first half of each to 65534 -- minus two as an int16, the sentinel for "leave it". Fourteen labelled blocks then override: f2 at 700, 850, 900, 950 or 1000, and f3 at 2700, 2800 or 2900, with the preceding and following halves settable apart. Twenty-one distinct settings in all.

**The sentinel is why a sonorant looked like it had no extent.** Where a target is not set, there is nothing to ramp *to* at that end, so the trajectory runs from wherever the vowel already was -- which is exactly the "whole vowel is the transition" that a day of measuring produced, and it is one magic number in a rule.

**And there is no curve to fit, which is why none fitted.** Fourteen arms is a decision tree with fourteen leaves. The velar pinch being "unidentifiable from pair tables", a run-out's level being a hundred hertz out, /l/'s third bandwidth marking it in a carrier and not in a word -- all of it is the same thing: sampling the output of a decision tree.

Reading the conditions as well as the values needs four steps and every one of them is in the tree as text, with no IBM object anywhere:

The arm's test names a symbol, `CALLW(test_string_s, FIELD(0), 2, 1, delta_sym_ref[6260])`.

`lang/enus/rules/symbols` says where that symbol falls: `at ut_norm.obj string_58 enus_evv_ut_norm_data_3 29` -- a store and an offset.

`lang/enus/enus.consts` holds that store as bytes, thirty-two to a line.

And `tools/module/phonemes.py` says which phoneme each code in it is.

So the whole context table for every consonant is recoverable mechanically. `tools/module/sets.py` is not the tool for it -- that one lifts from IBM's objects and wants `link.obj` -- but nothing new has to be lifted, only read.

**What that makes of the measuring.** It is the oracle, not the product. Every value read out of a rule can be held against the tables and the tap, which is the "prove it before saying it" this tree runs on, and the audibility calibration from an ear stays the standard for when something is close enough. But the tables were the wrong deliverable, and the mistake was not looking for where the numbers come from before spending a day measuring where they land.

### Read out: nine rules, 155 blocks, 110 distinct values

`tools/module/formants.py` reads all nine and writes `lang/enus/enus.formants`. Every place of articulation comes out as a base locus and a set of context-dependent overrides, with the test that selects each one named -- and the test's symbol resolved through `lang/enus/rules/symbols` to a store and an offset, so a condition can be chased to the bytes it matches against.

The base loci, straight from the rules: alveolar f2 1500 f3 2550; interdental 1450 and 2600; palatal 1700 and 2400 with f4 3600 and f5 4000; velar 1650 and 2300; retroflex 1200 and 1650/1600 with f4 3300 and f5 3600; bilabial 600 and 2200; labial 1000 and 2200/2250 with f4 3300 and f5 3600; lateral 800 and 3000. **155 blocks set a formant value and there are 110 distinct values in all.**

**Held against measurement, 25 of 32 comparable values match exactly**, to within two hertz: /D/, /F/, /S/, /T/, /Z/, /d/, /g/, /k/, /n/, /s/, /t/ and /z/ agree on both f2 and f3. That is the slot mapping proved, since the slots carry no names -- a language declares its globals by kind and count, so a number is positional and the original's names are gone.

And what does not match confirms the structure rather than denying it. Six consonants -- /b/, /f/, /l/, /m/, /p/, /v/ -- have `keep` for a base value, so whatever they measure at must come from an override arm and there is nothing in the base to compare. The four that differ, /G/, /R/, /r/ and /w/, are cases where an arm fired: /w/'s base f2 is 600 and it measures 1038, which is a bilabial's base being overridden, not a wrong reading.

**The conditions are partly decoded too.** `test_string_s(d, st, n, str)` walks the scan comparing each node's field against a string, and every call in these rules passes one byte, so such a condition is "the neighbouring phoneme is X" -- and the byte is read out of the store the symbol names. `lang/enus/enus.formants` therefore says things like

    place eng_alv_Fv
      base      f2a=1500  f2b=1500  f3a=2550  f3b=2550
      alt1_3    f2a=1300  f3a=2450
          when the next is i
      alt1_20   f2a=1700
          when the next is u

Forty-eight conditions come out as a named phoneme that way. And note what the shape of it says: `f2a`, the half *before* the consonant, is chosen by what comes *after* it -- which is the separability that two Latin squares and 832 carriers were built to establish.

`starttest` turned out to select nothing: it sets a tag, clears the stack back and pushes a context record, so its number is a label. The blocks are a sequential chain instead, each with its own predicates, and the scan-setter that precedes a test says which way it looks -- a name ending `l` sets the scan leftwards and one ending `r` rightwards, so a condition is about the phoneme *before* or the phoneme *after*. That is what the table prints.

**Two things it does not do, both named in the file itself.**

**Thirty-three of the 267 writes take their value from a register**, `GLOBAL(int16_t, r6, s440) = (LOW(r7))`, rather than from an immediate. Those come out as `computed` and are not resolved. They are very likely the interesting ones -- a value computed from the neighbouring vowel's own formants is exactly what coarticulation would look like -- and a first version of this tool matched only immediates and so dropped all thirty-three silently, which is worse than saying where it cannot see.

**The ordering is verified now, and it was my attribution of conditions that was wrong.** Every value write in `eng_alv_Fv` was instrumented with a print, the engine was asked to say /ati/, and what fires is the base and then one block: `f2b=1750`, `f3b=2750`. /t/ before /i/ measures f2 ramping to 1720 and f3 to 2726, which is that block's pair approached and not quite reached. So the blocks are tried in the order they appear and the first whose conditions hold wins, exactly as read.

What was wrong is which conditions belong to which block. **A block runs from its label to the `goto` that ends it, and the guards are the tests between the label and the value write.** Collecting every test seen since the last label crosses the nested `if` blocks and the gotos the generated C is full of, so the block setting 1750 came out guarded by a string test naming /k/ when its real guard is `testFldeq(2, 6, 0)` -- a *feature* of the item to the right rather than its identity.

And the guards are conjunctions that reach further than one position. That block's full condition is: field 6 of the item to the right is nought, then `advance_tok`, then the item *after that* is a particular phoneme. So a condition can be two positions out, and the table says `after+1` where it is.

### Witnessed rather than read: `lang/enus/enus.formants-witnessed`

Reading the guards is error-prone and reading them is not necessary. `tools/module/fvwitness.py` puts a print at each of the 267 formant writes in the nine rules, builds, speaks every consonant between every pair of adjacent vowels and at both word edges, and writes down which blocks the engine chose and what each slot ended up holding. **1,210 cases, 172 distinct outcomes.** It owes nothing to understanding the scan, and it is the shape a composer wants: context to values, as the engine chose them.

**The print reads the slot back rather than copying the expression**, which matters more than it sounds. A third of the writes take their value from a register, and /l/'s are *all* `LOW(r0)` -- so printing the expression collapsed every one of /l/'s contexts into the same unhelpful string, and 150 outcomes came out where there are 172. Read back, /l/ resolves: 800 and 3000 between /a/ and /u/, 750 and 2950 between /U/ and /o/. **That is why /l/ resisted every table-reading approach: its values are computed at run time, so no amount of reading immediates was ever going to produce them.**

**Held against the measured tables, 86 per cent of obstruent values fall within forty hertz** of the locus measured independently, and 81 per cent within twenty. The tolerance curve flattens at 86, so the remaining fourteen per cent differ genuinely rather than marginally and want explaining.

The sonorants are excluded from that figure and should be: 0 of 4 agree for /h/, 0 of 6 for /y/, 1 of 18 for /l/. That is not the rule disagreeing with the engine -- both come from the engine -- it is the *comparison* being invalid, because a sonorant has no closure and the measured "value at the closure end" is an artefact of whichever marker found a span for it. The witnessed value is the one to trust there.

So: **the values are the engine's own; 25 of 32 base loci match measurement exactly; the block order is verified by watching a rule run; and the witnessed table gives every context's outcome without needing the guards read at all.** What the guards *say* is still only an index, and the fourteen per cent of obstruent disagreements are unexplained.

### The whole table, witnessed: every rule's targets in every context

Nine place-of-articulation rules are not all of it. **Thirty-one rules write a formant slot**, and the other twenty-two are per-phoneme -- `ga_ph_a`, `ga_ph_u`, `eng_ph_x` and the like -- which is where a **vowel's** targets come from. `tools/module/phonemes.py` reports a vowel as having "no rule of its own" because it looks for `eng_ph_<v>`, and the vowels' are `ga_ph_<v>`, General American's.

That matters because a real word's error is in its vowels. Of `tomato`'s 405 wrong values, 389 are in the vowel regions.

So `tools/module/fvwitness.py` instruments all 522 formant writes in all thirty-one rules and speaks the whole cross product -- every consonant between every pair of vowels and at both word edges, 6,912 cases. **`lang/enus/enus.formants-witnessed` is 7,488 cases and 3,963 distinct outcomes**, and it records each rule's targets separately:

    case t:Ao
      eng_alv_Fv    s439=1500  s440=1500  s441=2550  s442=2550  s443=4000  s445=4300
      ga_ph_A       s439=1650  s440=1650  s441=2410  s442=2410
      ga_ph_o       s439=1200  s440=850   s441=2400  s442=2400

**Recording only the final value per slot loses exactly what matters**, and a first run did: the place rules and the per-phoneme rules write the same slots, so a vowel's targets overwrite the consonant's and there is no telling which belongs to the closure and which to the vowel around it. That run gave 296 outcomes where there are 3,963.

**The vowel rules agree with measurement exactly.** /A/'s rule says f2 1650 and f3 2410 for both halves; the measured vowel table says 1650 to 1650 and 2410 to 2410. /o/'s f3 is 2400 in both.

### And the consonant targets were not the bottleneck

Wiring the witnessed consonant targets into the composer, in place of the two carriers' measured ends, **does not move the ratio at all**: `tomato` stays at 76.3 per cent, `hello` at 42.8, `atapa` at 10.8. It reduces the wrong-value count for two chains -- /akaga/ from 127 to 97, /banana/ from 366 to 351 -- and raises it slightly for four others.

That is worth knowing rather than disappointing. The measured loci were already 86 per cent within forty hertz, so replacing them with exact ones cannot buy much, and it confirms where the error is not. `EVV_CHAIN_FV=0` composes with the measured ends instead, which is how the two were compared.

One restriction was needed: the rules' targets are used only where the consonant really closes. A sonorant has none, so ramping two targets across whatever span a marker found for it imposes a shape that is not there, and doing it for all of them took `hello` from 42.8 per cent to 57.2.

**What that leaves is the vowels**, whose targets are now in hand and not yet used. That is the next thing and it is where `tomato`'s 389 wrong values are.

### What is actually left

Six things, in the order they block a front end.

**Consonants with no closure are anchored, and nothing cuts the voice out any more.** Every segmentation here finds a consonant by aspiration going to nought, and 96 of the 416 carriers have no such stretch -- exactly the six with none by nature, /h/, /r/, /l/, /y/, /w/ and /R/, times sixteen vowels. Three markers tried in order cover every manner now: aspiration at nought for the obstruents and nasals, voicing at nought for /h/, and the third bandwidth leaving the vowel's own 150 for the five sonorants, which have no amplitude marker at all and move only their formants. Coverage went from 320 of 416 to 410 at 175 words a minute, and from 292 to 402 at 700.

The last two dropouts that survived that were one fault, and not the marker's. **An anchor is not always the same length in every carrier of a pair.** /l/ between /W/ and /O/ anchors over seventeen frames and between /W/ and /H/ over forty-three, because /W/ is the one vowel that moves the third bandwidth itself and one of those two anchors caught the vowel's movement rather than the consonant's. The composer took whichever carrier came first, so it laid a seventeen-frame closure into a forty-three frame one and put twenty-eight frames of silence in the middle of the word. Choosing the carrier whose own closure is nearest the length wanted fixes it, and is a smaller thing than telling a vowel's bandwidth from a consonant's -- which is still not done, and is what would fix it properly.

**So no held-out carrier cuts the voice out at any rate**, 150 through 700 words a minute, and 67 of 405 reproduce every parameter of every frame at the default rate against 63 before.

**The voicing envelope is measured and composed from, as of 9 September 2026.** It was the largest error that is not a hole, and it had surfaced four times -- in the isolated vowels, in the single carriers' run-out, in the chains, and again in the sonorant work.

The thing to know is that **the staircase does not decline across an utterance -- it resets after every consonant.** After each closure the voicing resumes at the *following* vowel's own value less three and steps down by one, four steps, spread over that vowel. /a/ is 50 and gives 47, 46, 45, 44; /A/ is 49 and gives 46, 45, 44, 43; /u/ is 59 and gives 56, 55, 54, 53 -- and it gives those in `imu` as well as in `umu`, so it is the vowel after the consonant that decides and not the one before. The first vowel of an utterance is its own value and then one less, two steps rather than four, nothing having reset it yet. /imi/ looks like an exception and is not: /i/ is 55, so its staircase starts at 52, which is also what /m/ holds through its own closure, and the two runs merge into a single one of seventeen frames.

**The structure is the same at every rate** -- 50, 49, then the /m/'s 52, then 47, 46, 45, 44 -- at 175, 250, 350 and 450 words a minute alike. At 700 only three of the four steps fit and the first is dropped for want of room.

Four things had to be right, and every one was found by the answer getting worse rather than by thinking:

**The drop must not be taken off twice.** A pair carrier's own run-out already *is* the staircase, so its top is the number wanted; subtracting three again put every step exactly three low and took /atapa/ from 14.5 per cent to 71.3.

**A closure keeps its own shape** rather than one held value: /t/ holds 35 for four frames and then drops to nought for its burst, so the shaped path that fits every other parameter fits this one too.

**The voicing does not come back when the closure ends.** A voiceless stop holds it at nought through its release as well -- /p/ for four frames past the `ah == 0` span and /k/ for five -- so starting the staircase there moved every step boundary with it, and cost 60 per cent against splicing's 14.5.

**And the letting go at the end is not a fixed length.** It is thirteen frames at 175 words a minute, eight at 250, four at 350, three at 450 and one at 700. Taking it as thirteen always reserved thirteen frames where the engine used three and squeezed the whole staircase into what was left, which cost /atapa/ at 450 words a minute 51 per cent against splicing's 16.3. It is read off the carrier instead, as the trailing frames whose voicing falls by more than one a frame: a staircase steps by one and a release plunges.

With all four right the rule beats splicing at every rate, over nineteen chains, and neither cuts the voice out anywhere: median 14.0 per cent against 17.6 at 175 words a minute, 43.0 against 44.2 at 250, 33.4 against 38.1 at 450, 38.3 against 41.1 at 700. `EVV_CHAIN_AV=0` splices instead, which is how the two are compared.

**The velar pinch is unidentifiable from pair tables.** /k/, /g/ and /G/ take their f2 locus from both neighbours at once, and two training carriers whose far vowels are both non-back say nothing about what a back one does: /g/ before /u/ has left targets of 1658 and 1652 against a wanted 1208. `between()` answers None there and the caller draws a straight line, which is the honest thing but not the right answer.

**The timing is borrowed, not composed.** `chain.py` takes the frame layout -- where each closure starts and how long it lasts -- from the engine's own frames for the same text. That is deliberate, and it separates what these tables can answer from what a language decides, but a front end has to decide it, and that is a durations-from-the-rules question rather than a formant one.

**Rates between the measured ones are unmeasured.** There are tables at 125, 150, 200, 250, 300, 350, 450, 500, 550, 600 and 700 words a minute. A rate in between wants either its own table -- ninety seconds at the fast end, twenty minutes at the slow -- or an interpolation between the two nearest, which is untested.

**A word's edges are measured now, and a real word still will not compose.** The pair corpus had no consonant with a vowel on only one side, every carrier in it being vowel-consonant-vowel, and a real word opens and closes on one: `hello` is /h/ before /E/ and there is no (silence, h) anywhere in the squares. `lang/measured/enus-initial.txt` and `enus-final.txt` are those two cases, 416 carriers each, every sampled one reproducing the engine exactly, and `tools/measure/chain.py` will now segment a consonant at either edge -- which it would not before, since a run touching an edge is the utterance's own onset or release in a carrier and a word-edge consonant in a word, and only the phoneme string can say which.

What still stops `hello` is /l/. It needs the (l, o) pair and that is one of six /l/ pairs with no anchor: **the third bandwidth marks a sonorant in a carrier and does not always mark one in a word** -- /l/ takes `b3` from 150 to 400 between two /a/ and leaves it at 150 throughout `hello`. The third formant does mark it in both, and marks every sonorant, so it is the marker `chain.py` uses to place a consonant in an utterance. It is deliberately *not* used to decide which pairs the tables offer: tried at 200, 300 and 400 hertz it anchored between five and ten more of /l/'s pairs and cost two to four dropouts every time, one of them thirty-eight frames, because the span it finds for /l/ is the wrong extent often enough to misplace the voicing. Preferring the earlier markers where a pair has a choice did not save it. **A dropout is the one fault the ear reliably catches and a missing pair only refuses to compose, so refusing is the better failure.**

Telling a vowel's own third bandwidth and formant from a consonant's is the thing that would fix this properly. **Anchoring by exclusion was tried for it and does not work**, and the reason is worth keeping because it is a fact about the corpus rather than about the code.

The idea was sound on its face: every carrier of a Latin square shares its two vowels with the twenty-five others of the same vowel context and differs only in the consonant between them, so the stretch where they disagree ought to *be* the consonant, with no marker needed. Implemented, the bracket it answers is (0, n-1) -- the whole utterance -- for every case tried, so it constrains nothing at all.

**The vowels are not the same across consonant contexts.** A vowel before a voiceless consonant is shorter than the same vowel before a voiced one -- /a/ runs 32 frames before /p/ and 37 before /b/, which is the pre-fortis clipping the stretch law was derived from -- so its whole track is compressed rather than merely cut short, and two carriers of the same vowel context disagree from their first frames. Where they disagree is everywhere.

Grouping by the consonant's voicing before comparing was the obvious repair -- a voiceless consonant is what shortens the vowel -- and it does not help: within a group of eleven carriers sharing both vowels and the voicing, the bracket is still (0, n-1).

**And the reason for that is the finding worth having.** In `l:cW` the first frame is f1 630, f2 960, f3 2580, where every sibling in its group starts at /c/'s actual onset targets of 700, 1050 and 2410. **/l/ colours the entire preceding vowel, from its very first frame** -- f3 is already lifted a hundred and seventy hertz toward /l/'s own 3000 before the vowel has begun.

So a sonorant has no localised extent to find. That is why every marker tried has failed on /l/ and why exclusion fails too: there is no vowel-only stretch to exclude, because the consonant is in all of it. A stop is a closure with transitions either side and the whole apparatus here suits it; **a sonorant is not that shape at all**, and modelling it as one is the mistake underneath six unanchored pairs, four failed markers and two failed exclusions.

**And that guess is now measured.** A vowel pair with no consonant in it speaks -- `` `[.1aa] `` gives 93 live frames -- so the sonorant's whole contribution can be had by difference. Against `ala`:

f1 is identical in both, 750 throughout the first vowel. f3 is flat at /a/'s own 2440 in the pair, and in `ala` it climbs from the second frame onward -- 2440, 2446, 2453, 2460 and on to 2611 by frame 25 -- a continuous ramp from the vowel's own value toward /l/'s 3000, spanning the *entire* vowel.

So a sonorant is not a segment with a locus and two transitions. **The whole vowel is the transition.** That is why there is nothing to anchor: the consonant is the midpoint of a ramp that spans the utterance, and asking where it starts and stops is asking the wrong question of it. It also explains the length: `clW` is 80 frames against the pair's 86, so a sonorant does not add time, it bends what is there.

**Composed that way, every pair in the corpus now composes.** A carrier with no closure is kept rather than dropped and composed end to end: the straight line through the two measurements that share its near pair, drawn over the whole carrier rather than over a prefix, and stretched to the length wanted rather than padded or truncated -- padding misplaces the release, which is at the end, and cost two dropouts until it was fixed. **416 of 416 held-out carriers, at 150, 175, 250, 450 and 700 words a minute, and no voicing dropout at any of them.** Coverage was 405 before. The sonorants' own region is 6.4 per cent wrong against the closures' 2.4, which is a new capability rather than a regression.

**And real words compose.** `hello` is 43 per cent, `money` 24, `happy` 32, `banana` 43, `tomato` 76, `lemon` 99 and `potato` 142, none of them with a voicing dropout. Two fixes got `hello` there from 154. A word-edge consonant has a vowel on one side only and the key for the missing side lumps all sixteen of that consonant's carriers together, so choosing among them by span length picked one with the wrong vowel -- which matters most for /h/, that having no formants of its own, being the following vowel's shape excited by noise, so the engine holds f1 at /E/'s 570 flat where the composition had 685 falling to 550. And a carrier with no closure had been translated as "the closure is the whole carrier", which left no run-in and no run-out, so the first vowel held flat where the engine ramps it and the final vowel was never laid down at all.

**The threshold for a word is lower than for a nonsense syllable.** Stas judged 72 per cent barely noticeable on `asaka` and 82.9 inaudible on `aCaSa`, and called `tomato` at 76 wrong. A word has a lexical identity to violate, so it wants to be under something like 50 rather than under 70.

**What is wrong with `tomato` is the vowels and not the closures** -- 389 of its 405 wrong values are in the vowel regions, its closures being nearly exact. The engine ramps the vowel between /t/ and /m/ smoothly, f2 from 1263 down to 1057, and the composition runs 1360 down to 1300 and then jumps to 1147: two measured halves that were measured with different neighbours and do not meet.

Two repairs for that were tried and both were worse. **Blending the two halves across the whole region** rather than cutting and butting them cost /aCaSa/ 3.6 per cent against 20.6, because it destroys both shapes where only the join was wrong. **Offsetting each half so it meets the closure it leaves** helped `tomato` a little, 76.3 to 73.4, and cost /atapa/ 10.8 against 23.2 and /aCaSa/ 3.6 against 16.3. So the discontinuity is not the main error; the levels the halves arrive at are, and that is the coarticulation residual again -- a run-out's first value is 100 hertz out because the carrier it came from had a different vowel before the consonant.

That gives a composition model, and a simpler one than the closure model rather than a harder one. For a sonorant between two vowels: ramp from the first vowel's own target to the consonant's locus across the whole of that vowel, then from the locus back to the second vowel's target across the whole of that. The locus is one number a parameter, which the /aCa/ carrier already gives. Nothing else is needed -- no closure, no run-in, no run-out, no anchor.

One thing measured and unexplained: /l/ leaves /a/'s f1 alone at 750 but pulls /c/'s from 700 down to 630 in the very first frame. So how far a vowel's own target is bent toward the consonant depends on the vowel, and the amount is not yet measured.

**And the corpus is one language.** English, sixteen vowels and twenty-six consonants, measured between vowels, at both word edges, and chained two consonants deep. Consonant clusters are untested -- `system` reports two closures for four consonants, /st/ merging into one -- and so are unstressed vowels.


## The formant tracks are a breakpoint list, and it is exact

Everything above measures where the engine's formants go. **None of it needed to.** The engine's formant tracks are a list of breakpoints -- a value at a moment in milliseconds, and a straight line in whole numbers to the next -- and the frames are what that list unfolds into. `EVV_ARRAY_TAP` names a file and `src/eci/bridge/eci_arraygen.c` writes the list out.

`tools/measure/breaks.py` rebuilds every frame from the list alone and compares it against `EVV_KLATT_TAP`. **Twenty-eight parameters, every frame, exact, on all eleven test utterances** -- `atapa`, `aCaSa`, `akaga`, `amada`, `tomato`, `potato`, `hello`, `money`, `happy`, `banana` and `lemon`. Not close: identical. There is no measurement in it, no fitting, and no residual.

So the question "can the formant values and transitions be recreated exactly" has a yes and the thing to recreate is the breakpoint list. `/atapa/`'s f2 is nine numbers:

    at 0    span 136   1200..1200
    at 136  span 20    1200..1500
    at 156  span 25    1500..1500
    at 181  span 30    1500..1250
    at 211  span 83    1250..1150
    at 294  span 30    1150..1000
    at 324  span 75    1000..1000
    at 399  span 20    1000..1200
    at 419  span 163   1200..1200

/a/ at 1200, the /t/ locus at 1500 reached over 20 milliseconds and held for 25, the vowel between the stops running 1250 to 1150 over 83, /p/'s 1000, and the last /a/ back at 1200. That is the whole track and it is the whole of what the composer was trying to estimate.

**Two things had to be right to get the rebuild exact**, and both were wrong first. The moment a frame is built at has to come from the tap and not from the run header: a bounded run trims its end to a whole step and the next run starts from where the last one really got to, so by the second segment of /atapa/ the header's `from` is four milliseconds early and every value after it reads wrong. And the gap in force has to be found by replaying the cursor, in the order the cursor crossed onto each gap, rather than by searching the list for the one covering a moment -- a stream whose cursor is reset mid-utterance has two gaps covering the same moment and only the order says which was in force. Searching by time left five amplitude parameters wrong on three of the eleven words.

### Where the numbers in the list come from

The rules, and they are exactly the numbers `tools/module/fvwitness.py` witnesses. `set_seg_default_acoustic_vals` fires once a segment and sets every formant target to -1, which is "unset"; then the segment's own rules fill them in. For /atapa/, in order: `ga_ph_a` 1200/1200, `eng_alv_Fv` 1500/1500, `ga_ph_a` 1200/1200, `ga_ph_a` 1250/1150, `eng_lab_Fv` 1000/1000, `ga_ph_a` 1200/1200. Every one of those appears in the f2 list above, in that order. **So the witnessed values are right, the segment boundaries are what delimits them, and `set_seg_default_acoustic_vals` is the delimiter.**

**Filtering the instrumentation by rule name was a mistake worth naming.** `fvwitness.py` instrumented rules whose names end in `_Fv` or start `eng_ph_`/`ga_ph_`, which is 56 of the 65 rules that write a formant slot. The nine left out include `set_seg_default_acoustic_vals`, which is both the segment delimiter and where f4 and f5 get their defaults of 3600 and 3900. The criterion should have been behavioural from the start: instrument every write to one of the eight slots, whatever the rule is called. It is now.

### And this retires the composition model above

Wiring the witnessed consonant targets into `tools/measure/chain.py` moved nothing. Wiring the vowels' in as well made it slightly worse -- `tomato` 61.9 per cent to 64.7, `banana` 42.6 to 44.3, `atapa` 10.9 to 12.6 -- and the reason is now plain: a rule fires once per occurrence of its phoneme, the witnessed table keeps one value per rule per case, and a value placed in the wrong segment is worse than no value. The placement is what `set_seg_default_acoustic_vals` supplies and what the table did not record.

So the measured corpus, the Latin squares, the markers, the locus interpolation, the stretch law and the voicing staircase are all estimates of something the engine states outright. They stay as an oracle -- a value read out of a rule can be held against a value measured coming out of the synthesiser, which is how anything here is proved -- but they are not the way to build a formant generator. **The way is the breakpoint list.**

What that leaves is a real and much smaller question: how much of the list is predictable from the phoneme and its neighbours alone. The values are; that is what the witnessing showed. The spans are the duration rules' business and are not measured yet.

### A segment is a unit, and it can be borrowed

The runs the tap reports are one per phoneme and one trailing silence, exactly, and each run's breakpoints tile exactly its own interval. /atapa/ is six runs, /sIstxm/ seven, and the /st/ cluster does not merge them -- which is where `tools/measure/chain.py`'s closure finder saw two closures for four consonants. `/h/` is the one exception and it is the measured one: it has no run of its own, being the following vowel's shape excited by noise, so `hello` is three runs and a silence.

So an utterance is a list of segments and `tools/measure/segs.py` asks whether a segment can be borrowed. Take a word, harvest each of its segments out of a *different* utterance that has the same phoneme between the same two neighbours, lay them end to end, and unfold.

**Every parameter comes back exactly, except f0.** Six cases: /atapa/ and /asasa/ and /akapa/ with the vowel between the two stops taken from a carrier whose outer vowels are /u/ or /i/, so a match says the two-away neighbours do not reach it; /atapa/ again with its /p/ taken from the second syllable of `apapa`, so a match says the position in the word does not either; `hello` with its /hE/ from a made-up `hEla` and the rest from `Elo`; and `sIstxm` in six pieces from five other utterances, cluster and all.

f0 is the exception and has to be. It is the phrase's melody, laid over the whole utterance by the intonation pass, so a borrowed segment carries a piece of a different tune. It is the one parameter a segment table cannot hold, and this engine already generates it elsewhere.

### What the duration does to a segment

Not all of it. `hElo` and the made-up `hEla` give /hE/ 154 milliseconds and 167, and the two segments are otherwise the same thing:

    hElo   f2 [(0, 80, 1650, 1650), (80, 74, 1650, 1500)]
    hEla   f2 [(0, 80, 1650, 1650), (80, 87, 1650, 1500)]

Same values, same first span, and the whole of the difference in the last one. Across twelve words rebuilt from made-up donors, **every span that differed was the last one, 83 of 83.**

So a segment is: values fixed by the phoneme and its two neighbours, spans fixed but for one, and that one absorbs whatever length the segment is given. **The values are a table and the durations are a separate question** -- which is the division `chain.py` already worked under, borrowing the engine's timing and composing only the values. The difference is that the values can now be exact rather than estimated.

### What else a segment depends on

Stress and the shape of the word, and this is measured rather than guessed. Of 59 segments harvested from donors made up on the spot -- `aC1VC2a` for a vowel, `V1CV2` for a consonant -- 25 came back identical and 10 more differed only in that last span. Five differed in a value and 19 in the *number* of breakpoints. The clearest case is a word-final unstressed vowel: /a/ at the end of /atapa/ against the same /a/ in `pa`, where it is a stressed monosyllable and takes the full targets rather than the reduced ones.

That is a longer key, not a different model. A segment is decided by its phoneme, its two neighbours, its stress and its position in the word, and the honest size of the table is not known until those are enumerated. What is known is that it is a table: nothing in it was measured, fitted or estimated, and where the key matched the engine's own frames came back to the digit.

**And `test/matrix.sh` passes with the tap in, 979 cases, every one as it was.**

## The durations are in the spine, and a segment is two pieces

The formant values are in the rules and the tracks are a breakpoint list. What the list still needed was its spans, and those come from somewhere else: not the arrays but the item spine, put there by `insert_2ptv`, which inserts a rule's value as a statement in one of the spine's fields. **Field 9 is how long a piece lasts and field 8 is the pitch.** `EVV_INSERT_TAP` names a file and `src/delta/delta.c` writes every insertion out.

`hello`'s four segments are 154, 80, 168 and 460 milliseconds. Its field 9 insertions are 80 and 74, 45 and 35, 20 and 148, 60 and 400. Every segment is two of them, and **they are also, to the digit, the spans of its formant breakpoints**:

    hElo  f2  [(0, 80, 1650, 1650), (80, 74, 1650, 1500)]
    hEla  f2  [(0, 80, 1650, 1650), (80, 87, 1650, 1500)]

So a segment is a fixed piece and a variable one, the variable one is what a duration model computes, and that is why every span that ever differed between a segment and a borrowed twin was the last one. The two questions -- what value, and for how long -- are answered in two different places, by two different mechanisms, and each has its own tap now.

**Speed is an integer percentage.** `apply_speed_anno` in `us_dur.obj` compares two globals against 100, and where they differ multiplies the duration by one and divides by the other, truncating. Nothing more elaborate, and `apply_min_durs` then clamps field 9 from below.

**Where the base durations come from splits by kind.** The consonants' are in `es_cdur.obj`: twenty-six rules writing `s274` and `s447`, a few as immediates -- /t/, /p/ and /k/ 5, /F/ 20, /g/ 25, /l/ 35 -- and the rest computed, so the same witnessing the formants needed applies. The nuclei's are not in a state slot at all: `es_ndur.obj` writes none, and its durations reach field 9 through the spine. So the insert tap is the oracle for durations in the way the array tap is for values, and reading rules would not have answered it.

### A duration is not decided locally, and a value is

`tools/measure/segs.py --durations` gathers every segment of a corpus by the same key the values use -- the phoneme and its two neighbours -- and says whether the length came out the same wherever the context appeared. Over forty-one words: **thirty-eight contexts of fifty-one were the same length everywhere, and thirteen were not.**

The stable ones are stable hard. /t/ between two /a/ is 45 milliseconds in nine places across seven words, first syllable and last, two syllables and four. /d/, /g/, /s/ between two /a/, /l/ before /o/, /n/ before /i/, word-final /a/ after four different stops: all fixed.

The thirteen that vary say what a duration model has to know, and none of it is local:

The stress. /a/ between /t/ and /p/ is 113 milliseconds in `atapa`, `atapata` and `patapa`, and 180 in `tapa` and `tapata` where it is the stressed first syllable.

How many syllables there are. Word-initial /a/ before /m/ is 152 in `ama` and 113 in `amada`.

What is further away than a neighbour. /hE/ before /l/ is 154 in `hElo` and in `hEli` and 167 in `hEla`, so the *last* vowel of the word changes the length of the first syllable.

Where in the word it is. The second /l/ of `lalala` is 70 and the third is 80; the second /m/ of `mamama` is 80 and the third is 70; /x/ between /t/ and /m/ is 94 at the end of `sIstxm` and 143 at the start of `txmeto`.

**So the two halves of a segment divide cleanly and they divide by mechanism, not by convenience.** The values are local, decided by the phoneme and its neighbours, stated as immediates in the rules and reproducible exactly from a table. The durations are prosodic, computed over the whole word by `distribute_nucdur` and its neighbours in `us_dur.obj` and clamped by `apply_min_durs`, and no local table can hold them. That is the same division `chain.py` worked under from the start -- borrow the timing, compose the values -- and it turns out to be the real one rather than a convenience.

**One trap in measuring this.** A piece boundary is only visible where some parameter changes at it, so /p/ between two /a/ reads as 30+70+5 in four words and as a single 105 in a fifth. That is one segment with a boundary unobserved, not two different segments, and comparing pieces rather than totals reports it as a difference. Compare totals.

### What a segment actually is

A run is two gaps: a transition of fixed length into the phoneme's first target, then an interior that runs from that target to its second and carries whatever duration the segment was given. `/atapa/`'s middle /a/ is

    at 181  span 30  1500..1250      the transition in, from /t/'s locus
    at 211  span 83  1250..1150      the interior, and where the length goes

and 1250 and 1150 are, exactly, what `ga_ph_a` writes into `s439` and `s440` in that context. **So the slots really are the segment's two targets, the transition runs from the previous segment's second target to this one's first, and the interior runs between this one's two.** The witnessed table and the breakpoint list are the same numbers seen from either end.

Where a target is unset -- the -2 sentinel -- no breakpoint is placed and the line runs straight on to the next target that is set. Schwa between /b/ and /n/ has no second formant of its own, so its whole segment is one ramp to whatever the /n/ after it wants: 1500 before an /a/, 1625 before an /i/. That is the /n/'s number, not the schwa's.

**Three attribution mistakes had to be undone to see this, and each made a vowel look as though it depended on more than it does.**

A gap belongs to the run its own start falls in, not to the run being built when the cursor crossed onto it. The cursor advances only when a frame needs a value past its right end, so a parameter holding still crosses late and its gaps land a run or two after the ones they cover. That put two of /l/'s gaps inside the /E/ after it.

The value a segment starts from is inherited from the one before and is not its own. /E/ between /l/ and /m/ starts at 875 after an /a/ and 1050 after an /i/, and ends at 1500 in both.

And where a segment ends is the next segment's business when its own target is unset, which is the schwa above.

**With those undone, the reach of the context is short.** `tools/measure/segs.py --reach` puts the same three phonemes in six different words -- different phonemes two away, the stress on it or off it, a longer word -- and compares. **Every consonant tried keeps one set of targets across all six**: /t/, /s/, /m/, /l/, /k/, /p/ and /d/ between two /a/. Every vowel tried keeps two, and the split is stress: /E/ between /l/ and /m/ goes to 1500 unstressed and 1450 stressed, with the first formant 570 against 600. That is vowel reduction, and it is the whole of the extra key.

So a segment's targets are decided by the phoneme, its two neighbours and its stress, and by nothing else. The transition length is decided the same way. The interior length is the duration model's and is not local.

## The segment table

`tools/module/segments.py` writes `lang/enus/enus.segments`: what every segment of English is made of, as targets, keyed by the phoneme, the phoneme either side and the stress on its syllable. `E l m 1 f2  1450 1500` is /E/ between /l/ and /m/ in a stressed syllable, whose second formant reaches 1450 and then 1500.

**The corpus is not made up.** `test/samples/enus.words` already holds what this engine says each of twenty-four thousand words is made of, stress marks and all, because that is the word gate's baseline. Speaking those covers exactly the contexts the language produces, rather than a cross product most of which never occurs, and it costs one run of the array tap a word.

**A key seen twice that disagrees with itself is the check.** Nothing is averaged and nothing is preferred by frequency: where a key comes out two ways, either one is the other cut short -- which is a duration effect and allowed -- or it is reported as a disagreement, which says the key is too short. Over the whole corpus: **17,103 segments, 428,514 lines, 282,675 keys seen more than once, and 833 disagreeing.** One line in five hundred.

Five things had to be right first, and each was found by the count of disagreements dropping.

**Runs have to line up with phonemes.** One run a phoneme and one more for the trailing silence, or every segment after a mismatch goes under the wrong key. Two things break the count and both are measured rather than assumed: /h/ has no run of its own and folds into the vowel after it, and **five phonemes take two runs -- /C/, /J/, /Y/, /W/ and /O/**, the two affricates and the three diphthongs. Over six hundred words, every word containing one of those has a run more than it has phonemes and no other phoneme is above a third. The two rules compose, so `hY` in `anaheim` is two runs and not one. With both in, 24,222 of 24,302 words line up and the eighty that do not are dropped.

**A gap belongs to the run its own start falls in.** Not the run being built when the cursor crossed onto it, which is what the tap prints: the cursor advances only when a frame needs a value past its right end, so a parameter holding still crosses late and its gaps land a run or two after the ones they cover.

**Every span is the duration model's, not only the last.** /n/ before /t/ after an /X/ reaches 350 and then 200 over spans of 63 and 49 milliseconds in one word and 43 and 29 in another. Same two targets, and the table holds targets.

**A target repeated is a breakpoint that changes nothing.** The line between two equal values is the same line whether it is drawn in one stretch or two, so collapsing them is lossless and stops one word's extra breakpoint reading as a different segment. That alone took the disagreements from 3,027 to 597 over two thousand words.

**A trajectory cut short writes down where it got to.** Shortening a segment can drop the breakpoints it never reached and leave a single interpolated value: /E/ between /s/ and /l/ reaches 1650 then 1500 when there is room, and a single 1575 when there is not, which is the midpoint of the two and neither of the targets. What can be checked is that the value lies on the path -- between where the segment started and the targets it still had to reach -- and 6,684 shapes are accounted for that way.

**What is left disagreeing is mostly not the formants.** Of 833 keys, the frication and voicing amplitudes are 301 and the bandwidths 264; the five formants together are 209, of 17,103 segments each. `af` on /s/ before a word edge is the single worst, three hundred words of it, which is the same kind of thing as f0: an excitation envelope belongs to the utterance rather than to a segment.

### The table's shape, and what it predicts

**A base and its exceptions, not a line a context.** Most of the cross product says the same thing -- eleven parameters hold one value across better than fifteen thousand of the seventeen thousand segments -- so a flat dump buries what a neighbour actually changes under four hundred thousand lines that repeat. Written as a base with overrides it is 42,204 lines and 776 kilobytes rather than 428,514 and seven megabytes, and it has the shape the rules themselves have: a base locus and a handful of context overrides. /l/'s second formant is `base 800`, which is the immediate in `eng_lat_Fv`, and 310 exceptions; /t/'s is `base 1500`, the immediate in `eng_alv_Fv`, and 162.

**Held out, it predicts.** Building the table from four fifths of the words and testing on the fifth it never saw: 765,150 segment parameters, of which **560,123 exactly as the table says and 14,455 the same segment cut short -- 99.70 per cent of those whose context the table holds -- and 1,745 wrong.** The remaining 188,827 are in contexts the training fifth never contained, which is coverage rather than correctness: the key is right, the corpus does not yet fill it.

So the next thing the table wants is not a better key but the rest of the key space. Twenty-four thousand words of running English do not contain every phoneme between every pair of phonemes at every stress, and a table meant to stand in for the rules has to answer for the ones they never say.

### Filling the key space

Running English does not contain every phoneme between every pair of phonemes. A table meant to stand in for the rules has to answer for the ones no word asks for, so `segments.py --corpus both` adds a corpus built for coverage rather than taken from words.

**One sequence holds every triple.** A de Bruijn sequence over the forty-five phoneme alphabet at order three is 91,125 symbols long and contains each of the 91,125 possible triples exactly once, so walking it in short chunks covers the whole of the key's context in a few tens of thousands of utterances rather than the quarter of a million that one carrier a triple would want. Three passes give the three stresses, marked before every vowel so each carries it, and the word edges get their own two-phoneme carriers.

**Eight phonemes a chunk, measured rather than chosen.** A long enough stretch of arbitrary phonemes stops parsing as a pronunciation and is spelled out instead, which shows up as a run count far past the phoneme count -- thirty glottal stops in a row gives 386 runs for 30 phonemes. Twenty phonemes line up 73 times in a hundred, twelve 86, eight 94, and a short chunk also loses less when it does fail. Anything that does not line up is dropped.

**The Lyndon-word construction was written from memory and was wrong, silently.** It produced 16,215 symbols holding 131 distinct triples where there should have been 91,125 of each, and the harvest that used it added six thousand contexts and looked like a success. What caught it was checking the sequence against what it is for -- its length against k to the n, and its distinct n-grams against the same -- which the Eulerian-circuit construction that replaced it passes for every small alphabet. **A generator of test material needs its own test**: a corpus that silently covers a fraction of what it claims makes every number downstream of it meaningless, and nothing else in the pipeline would have noticed.

With the fill corpus in, the table covers **221,726 segments** rather than 23,112, written as 5,382 base lines and 417,242 exceptions. Self-consistency holds up at that size: of 1,511,762 keys seen more than once, **11,388 disagree, and 8,409 of those are the voicing and frication amplitudes**. The five formants together are 1,042, which is one segment in two hundred.

**And the table is more structured than its size suggests.** The 5,382 phoneme-stress-parameter groups take only 14,967 distinct values between them, under three each: 3,814 of them are one value whatever is either side, 276 depend on the phoneme to the left alone, 257 on the one to the right alone, and 1,035 on both. So the enumeration is a decision tree flattened out, and the tree is small. Writing it as a tree rather than as its output is the obvious next compression and is not needed for correctness.

### Held against whole words

`tools/measure/fromtable.py` takes a word, asks the table what each of its segments should reach, and holds that against what the engine's own breakpoints reached. Over 1,200 words: **912 are entirely as the table says, and 1,122 are right in every parameter but the excitation envelopes** -- 207,482 segment parameters with 387 disagreeing, one in five hundred.

The envelopes are `av`, `af`, `ah` and `tl`, and counting them separately is not a convenience. They are how hard the utterance is being voiced and how hard it is being blown; the voicing staircase was measured as the utterance's property rather than a phoneme's long before any of this, and `af` on a word-final /s/ is the single largest disagreement in the whole table. They belong with f0, which no segment table can hold either.

**Rebuilding the frames by substitution was tried first and measures nothing.** The table deliberately collapses a repeated target and drops one that runs on into the next segment, so putting its values back onto the engine's own gap list misaligns them, and every frame then differs -- zero words of two hundred came out right, against a per-segment agreement of better than ninety-nine per cent measured the same afternoon. The misalignment was the harness's. Frame-exact generation needs the spans, and the spans are the duration model's.

So the segment table is done and checked to the level it specifies. **What it cannot do on its own is make a frame**, because a frame needs to know how long each stretch lasts, and that is the next table.

## The duration table, and generating frames from data

A frame wants two things: what each parameter reaches and how long each stretch takes. The segment table holds the first. `tools/module/durations.py` measures the second and `lang/enus/enus.durations` holds it.

### What a duration depends on, measured

`durations.py` harvests every segment of the word corpus with the prosodic context around it, then asks of each candidate key: of the contexts seen more than once, how many came out the same length every time. Only repeated contexts count, because a key long enough to name nearly every segment on its own scores perfectly by having nothing to compare.

The segment's own key -- phoneme, both neighbours, stress -- gets 524 of 1,998 repeated contexts wrong, by up to 196 milliseconds. Adding, in turn, the syllable the segment sits in entire, where that syllable is in the word, the stress either side of it, and whether the next syllable begins with a consonant takes that to **35 of 1,711, by at most fourteen milliseconds with a median of three.** Over the whole corpus, 484 of 23,497.

Each addition was found from the data rather than guessed. The coda came from the worst offenders being diphthongs: /W/ is 114 milliseconds in `bausch` and 237 in `baum`, /O/ 108 in `benoit` and 228 in `annoys`, which is pre-fortis clipping -- and a diphthong's first half has the diphthong itself as its right neighbour, so the consonant that clips it is not a neighbour at all and only the syllable says what it is. The next syllable's onset came from what was left: /A/ is 142 milliseconds in `ballad` and 78 in `balfour`, the difference being that `ballad`'s second syllable has no onset of its own, so the /l/ between them behaves as its onset rather than as the first syllable's coda.

Adding the last syllable's shape and its vowel was tried and is not it -- 235 to 198 while nearly doubling the contexts. **A change to one phoneme mostly does not reach far**: swapping the final vowel of `abacuses` moves that vowel and the consonant after it and nothing else. `hello` was the misleading case, where swapping the final vowel moves all three segments, and only for the lax vowels.

### Spans are per parameter

The formants do not share one split. Of 4,149 segments, 2,455 have one span list across all five and **1,694 do not**, the commonest disagreement being (0, 62) against (0, 77): one formant stopping at the segment's end and another running on past it, having no target there. So the table holds the majority split with the parameters that differ named beside it, and each gap is recorded as an offset and a span rather than a span alone, because a gap that overshoots leaves the next one starting late.

### Frames out of the two tables

`tools/measure/generate.py` takes a word's phonemes and stress, asks the duration table where every gap falls and the segment table what each one ends at, and unfolds. Nothing of the engine's is used but the phoneme string.

`abbey` comes out gap for gap: 1650 held for 157 milliseconds, down to the /b/'s 1100 over 25 and on to 1400 over 55, up to 1890 over 30 and 2190 over 110. Over 150 words, **8 are frame for frame and 93.8 per cent of formant values are exact.**

**What is left is the segment table's key disagreements, and they cascade.** `aback`'s schwa is one of the five hundred keys that come out two ways, and getting it wrong shifts every breakpoint after it in the word, so half a per cent of bad keys becomes six per cent of bad frames and a word is either right or ruined. That is where the remaining work is, and it is a small and named place rather than a mystery.

**One rule fell out that is worth having.** A gap whose span overshoots its own segment is one the segment has no target for: the engine draws no breakpoint at the boundary and the line runs on to whatever the next segment wants. The spans say so by themselves, so the generator needs no flag for it -- and the flag tried first, recorded in the segment table, was unstable because whether a segment's last stretch has a target of its own depends on how many stretches there are, which is duration-dependent. Counting it as part of the key took the segment table's disagreements from 11,388 to 27,837 and said nothing new.

### A whole frame, and something to listen to

Extended to all twenty-seven parameters the language drives -- everything the map names but the pitch -- the two tables make a whole frame, and `generate.py --wav` renders three wave files a word through `test/harness/klattplay` so the comparison is fair: the engine's own frames, the tables' frames, and a third with the tables' spectrum over the engine's excitation.

**Two words come out sample for sample.** `hello` and `money`, spectrum against the engine's own rendering: every one of 4,895 and 4,235 samples identical. `absolutely` is 3.4 per cent of the signal, `tomato` 3.9, `banana` 6.1, `abandonment` 25.9.

**The excitation envelopes are where it falls down and that was expected.** With them taken from the tables as well, `hello` is 43 per cent different and `absolutely` 84. Of 51,690 wrong parameter values over 150 words, `av`, `ah`, `af`, `tl`, `ab` and `a5f` are 34,710 -- and they are how hard the utterance is being voiced and blown, which the voicing work established long ago is the utterance's business rather than a segment's. A segment table is the wrong shape for them, as it is for f0.

So what the tables reproduce is the spectrum, and for some words they reproduce it exactly. What they do not hold is the excitation, which wants the same treatment the intonation already has: a pass over the utterance rather than a lookup per segment.

### What the ear said

Stas heard `hello`, `tomato` and `abandonment`, engine against tables' spectrum, each with a spoken label: **"honestly? Same. I'm unsure about abandonment, but generally they sound practically the same."**

So four and six per cent of the signal are inaudible on a real word, and twenty-six is at the edge of noticing. That is a much looser threshold than the composed model needed -- the calibration there was under fifty per cent for a word and under seventy for a nonsense syllable, but those were shape errors in the composer's own measure rather than waveform differences against the engine, and they are not the same scale.

**Which reorders the work.** Frame exactness is a verification tool and a good one -- it catches an accident that no ear would -- but it is not the product's standard, and by the product's standard the spectrum is done. Eight words in a hundred and fifty being frame for frame sounded like a poor result and is not one.

**And the excitation is better than its numbers too.** The envelopes from the tables give a waveform 43 per cent different on `hello` and 84 on `absolutely`, but the difference is level rather than breaks: over six words the tables' frames have the same voiced-frame counts as the engine's to within two, and no more one-frame voicing gaps -- 0 against 0 on four of them, 0 against 1 on two. A dropout is the thing that would be unacceptable and there are none.

### A stretch does not always start where the last one left off

Stas heard the fully-from-tables version and it was wrong: **"they fluctuated in volume and were kind of messed up."** No voicing dropouts -- the tables' frames match the engine's voiced-frame counts to within two over six words and add no one-frame gaps -- so the fault was level, not breaks.

The cause was an assumption stated three sections above and never tested: that the value a stretch begins at is whatever the stretch before left behind. **It is not, and not rarely.** Over four hundred words, the proportion of a parameter's stretches that begin somewhere else is 40 per cent for the aspiration, 38 for the voicing, 15 for the frication, 14 for the third formant, 12 for the tilt and the diplophonia, and 8 for the second formant. The engine's arrays can hold two points at one moment, and a jump is what that is: /hE/'s voicing in `hello` sits at nought through the /h/ and then begins the vowel at 51, which is /E/'s own value less three -- the staircase reset the voicing work measured long ago.

Writing those starts down as `from:to` costs the table size -- 909,775 exceptions rather than 521,761 -- and it **improves everything else**. The table's own disagreements over two thousand words fall from 280 to 153. The generated frames go from 9.28 per cent of parameter values wrong to 7.31. And `absolutely`, whose spectrum was 3.4 per cent of the signal away from the engine's, becomes **identical, all 162 frames** -- so three of the six words tried now render sample for sample.

**The excitation is unmoved by it**, 84 per cent to 82, and that is the honest state: the envelopes are not a table. `av` is a rule and it was measured in full for the composer -- the staircase resets after every consonant, resumes at the following vowel's own value less three and steps down by one four times across that vowel, two steps rather than four for the first vowel of an utterance, with a release at the end that scales with the rate. A table cannot hold that because the number of breakpoints follows the duration: /o/ at the end of `hello` is one stretch of 239 milliseconds from 56 to 54 where the same key in a shorter word is two. The rule is already written down in `tools/measure/chain.py`. What is left is to put it where the generator can use it.

### The excitation as rules

The voicing and the aspiration are rules and this is what they are, measured over three thousand words.

**The voicing.** Every vowel has a base value. Stressed, it runs from that base down two across the vowel; unstressed or with secondary stress, from three below the base down four more. The bases are /i/ 55, /I/ 57, /e/ 55, /E/ 54, /A/ 49, /a/ 50, /u/ 59, /U/ 57, /o/ 56, /c/ 52, /H/ 54, and the two schwas are the exception, dropping three rather than two or four. The rule accounts for 70 to 85 per cent of cases for most vowels.

**The composer measured this once and got it half right.** It read the two-step form as belonging to the first vowel of an utterance; its carriers were all stressed on the first vowel, so `first` and `stressed` could not be told apart. At scale it is the stress, and that matters because a word has one stressed syllable and several unstressed ones.

**The aspiration.** Held at 34 through a vowel and at nought through a consonant, the latter in every one of the thousands of consonant segments measured -- /b/, /n/, /s/, /z/, /t/, /F/, /d/, /k/, /G/ all at 100 per cent. A vowel an /h/ folds into holds 42 instead, which is the /h/ being that vowel's shape excited by noise.

**A rule says what a stretch does, not where it starts.** Laying the voicing over a whole segment voices the /h/ in `hello` and took that word from 43 per cent different to 143. The table already knows where the stretches fall and how long they are; only the values of the last one -- the vowel proper -- are the rule's.

With both rules in, and applied that way, **the whole frame out of the tables**: `money` is identical to the engine over all 4,235 samples, `hello` is 0.6 per cent of the signal, `absolutely` 5.9 and `tomato` 38.3. Across a hundred and fifty words the wrong parameter values fall from 7.31 per cent to 4.37, the voicing's own share from 11,765 to 2,993 and the aspiration's from 9,338 to 1,780.

So a word can be synthesised from two data tables and two rules, with nothing of the engine's in it but the pitch, and come out sample for sample.

### The tilt running on into nothing

Stas heard `tomato` and said it "almost phased through" the stressed vowel. It did: the spectral tilt swept from nought to 35 across that vowel where the engine holds it flat.

**The tilt is nearly binary** -- flat nought in 10,883 segments of 12,287, and 35 falling to nought in the rest, which are only the voiced stops /b/, /d/ and /g/ and the flap /F/. The sweep was the generator's doing. A stretch with no target of its own runs on to the next target there is, and the flap after that vowel begins its tilt at 35 out of nowhere a quarter of the way through itself. Running the vowel up to that 35 spreads a stop's burst across a whole syllable.

**So a stretch runs on only when the next target continues from where this one is.** A target that jumps is not continued into, and nothing should run to meet it. With that, `tomato` falls from 38.3 per cent of the signal to 4.7 and the wrong parameter values over a hundred and fifty words from 4.37 per cent to 3.96.

### What is a rule and what is a table

A survey of all twenty-seven parameters over fifteen hundred words, asking how many distinct shapes each takes and how well the phoneme alone predicts it:

Four never vary at all -- `fl` at nought, `fnp` at 200, `ftp` and `ftz` at 250 -- and are one line each. Fifteen more are decided by the phoneme alone to better than 88 per cent: the open quotient, the tilt, the diplophonia, the aspiration, four of the five bandwidths, the nasal zero, all five formant gains and the bypass. **Only eight are genuinely context-dependent**, and they are the five formants, the third bandwidth, the voicing and the frication -- the second formant is predicted by its phoneme alone only 25 per cent of the time, the third 31.

So the table is carrying a great deal that is not table-shaped. That is the next compression and it is a bigger one than writing the exceptions as a tree: a parameter that is a constant, or a lookup on the phoneme, should be written as that and not as a quarter of a million context lines.

**And one bug was paying for a lot of it.** Nothing runs before the first stretch of an utterance, so its start is not a jump -- but it was being recorded as one, for every parameter of every word's first segment. That put 24,187 exception lines in the table for `fl`, a parameter that never leaves nought. Fixing it took the table from 909,775 exceptions to 780,612 with no change to what it generates.

### Splitting the tables, and what the split cost

`lang/enus/enus.phonemes` now holds the nineteen parameters a context cannot move -- one line a phoneme and a stress, 3,776 lines and 48 kilobytes, and it is the first thing here a person could read straight through. `lang/enus/enus.segments` holds the eight a context does decide.

**Two ways of doing it were tried and the cheap one is wrong.** Moving those nineteen out *with their exceptions* takes the segments file from fifteen megabytes to nine, and costs `money` its exactness: nought to 25 per cent of the signal, `banana` 12.7 to 30.8, `tomato` 4.9 to 11.2. The nineteen are settled by the phoneme alone 88 to 100 per cent of the time, and the last few per cent are worth more than six megabytes. So the exceptions stay in the segments file for every parameter and the phonemes file holds only the bases.

**Whole-segment rules were tried for the same reason and are also not it.** Letting the aspiration and the tilt be rules across a whole segment, rather than over its voiced tail only, would have let them leave the table -- and it costs 0.2 points of accuracy while the table does not shrink, because what fills it for those two is the jumps rather than the bases. The rules stay on the last stretch.

So the state is: **`money` identical to the engine over every sample, `hello` 0.6 per cent of the signal, `tomato` 4.9, `absolutely` 5.9, `banana` 12.7**, with 3.955 per cent of parameter values wrong over a hundred and fifty words. Stas heard all five and called them accurate.

**The compression that would actually work is still undone.** The segments file is three quarters of a million exception lines and its content is a decision tree: 5,382 phoneme-stress-parameter groups taking 14,967 distinct values between them. Writing each value once with the contexts that select it, rather than each context once with its value, is the thing -- and it needs the contexts to be describable as sets rather than listed, which is the part nobody has looked at yet.

### The table as rectangles

The exceptions were a line a context: three quarters of a million of them, each repeating the phoneme, the stress and the parameter to say one number. Written instead as **a line a rectangle** -- a set of left neighbours crossed with a set of right ones, and what that block of contexts does -- the file goes from **14.9 megabytes and 915,175 lines to 1.56 megabytes and 53,267**, generating exactly the same frames: 3.955 per cent of parameter values wrong over a hundred and fifty words, `money` still identical to the engine over every sample.

It works because the contexts really do block up. Of 13,592 value blocks, 7,785 are a single rectangle and the rest take a handful. The right-hand sets are the wide ones -- 10.2 phonemes on average, and 39,741 of the 51,630 rectangles name more than one -- while the left-hand sets average two.

**And they are classes rather than lists.** `t 1 f2  N CDFJNTdntz  >` says that /t/'s second formant in a stressed syllable, with /N/ to its left and any of C, D, F, J, N, T, d, n, t or z to its right, has no target of its own. That right-hand set is the coronal obstruents and nasals, which is exactly the class a phonetician would write, and it was arrived at by grouping identical behaviour rather than by assuming any feature system.

That is the form authoring wants. A person can read a line, see which neighbours it covers, and change the number.

### The duration table

The same treatment, and it needed a different cut. **Rectangles do not help here**: the durations are near-continuous, 94,089 contexts holding 26,440 distinct values, three and a half contexts to a value, and most blocks are a single context. Collapsing the ten context fields where they do not matter takes 94,089 lines to 92,031, which is nothing.

What was paying for the size was repetition of a different kind. Of 716,586 lines, 622,486 were per-parameter splits, each repeating a twelve-field key to say one list of spans. **Writing the key once and the splits as continuations** takes the file from 29 megabytes to 13.6. And **one continuation a split rather than a parameter** -- the parameters needing their own split within a segment often need the same one -- takes it to **8.3 megabytes and 310,139 lines**, generating exactly the same frames.

So the three files are now `enus.phonemes` at 48 kilobytes, `enus.segments` at 1.56 megabytes and `enus.durations` at 8.3, against 44 megabytes for the same information this morning. The durations are still much the largest and will stay so while they are a table: a duration is a number in milliseconds and there are twenty-six thousand distinct ones. Making that small wants a model -- a base times factors for stress, position and coda -- which is a different piece of work from compressing a table and the obvious next one.

### Durations do not factorise

The obvious way to shrink the duration table is the one the literature uses: a base length for the phoneme multiplied by a factor for each thing that stretches or shortens it. `tools/module/durmodel.py` fits exactly that, by coordinate descent in logs, which needs no linear algebra and converges in a dozen passes.

**It does not work.** A base for the phoneme at its stress, times factors for the onset, coda, syllable position, syllable count, distance from the end, the stresses either side, the next syllable's onset and both neighbours -- 187 bases and 1,038 factor levels -- leaves a **median error of fifteen milliseconds**, with 36 per cent of lengths within ten. The table's own noise is at most fourteen milliseconds and usually three, so the model is worse than the thing it would replace.

Interactions are what is missing, and paying for them eats the saving. Folding the coda into the base halves the median to ten; adding the next syllable's onset takes it to six; adding the syllable count takes it to five, at 29,477 bases -- by which point the model is larger than the 26,440 distinct lengths it was meant to replace, and still only half of them within five milliseconds.

So the engine's durations are a decision tree with interactions all through it, not a product of factors, and **the table is the representation**. That is worth knowing rather than assuming, and it is why `enus.durations` is eight megabytes while the two tables beside it are one and a half and forty-eight kilobytes.

## A second language

All of the above is English, so the question is how much of it is English. British English is the control: it shares the phone statement exactly, so the machinery can be pointed at it without touching anything, and its values differ enough to be worth measuring.

**It has no word list.** Only `enus` has one, and the corpus of what the engine says each word is made of is built from it. So `segments.py --corpus fill` was taught to take the alphabet from the language's own phone statement where there is no word baseline, and British English was harvested from the de Bruijn corpus alone -- every phoneme between every pair of phonemes at every stress, and nothing else.

**The segment half transfers.** `lang/engb/engb.segments` is 1.34 megabytes and `engb.phonemes` 48 kilobytes, covering 168,685 segments with 7,623 disagreeing. Held against nine real British words the engine was asked to say: **959 segment parameters, five disagreeing, six of the nine entirely as the table says** -- and the table had never seen a word. So the segment table wants nothing of a language but its phoneme inventory.

**The duration half does not.** Every one of those words came back "not covered": the duration key is the phoneme, its neighbours, its stress, the syllable it sits in, where that syllable is in the word, the stresses either side and the next syllable's onset, and a corpus of eight-phoneme chunks exercises none of those as a word does. A made-up chunk has made-up prosody.

So a new language needs a word list before its durations can be harvested, and `test/words.sh record` is the thing that makes one. That is the concrete prerequisite for doing this for Polish, and it is worth knowing now rather than after building the rest.

## The pitch

The last thing the generator borrowed. It is not a segment's property at all and never could be: it is **one contour over the whole word, five stretches, and its values barely move.**

Measured over twelve hundred words, every one of which has five or six pitch breakpoints and no other number: it starts flat, rises to a peak, falls, falls again and holds. The values cluster hard -- the start is 1217 when the first syllable carries the accent and 1044 when it does not, the peak 1293, the first fall lands at 792 and the second at 742, in tenths of a hertz. Those five numbers and the shape are the whole of English's declarative intonation as this engine draws it.

**The peak is anchored to the accented vowel's middle.** That came out of the measurement cleanly -- median 0.48 of the way through it, quartiles 0.42 and 0.63 -- where anchoring to the accented syllable's start gave nothing usable and the offsets scattered from 3 to 63 milliseconds. The rise begins about a third of a vowel-length before the peak, which is to say inside the consonant in front of it.

**The timing is where it is still loose.** With the medians as offsets -- 51 milliseconds of rise, the first fall landing 61 before the word's end and the second 58 after that -- the contour comes within 3.5 hertz of the engine's at the median and 5.7 on average, with a worst case of 42. On a contour that runs from 74 to 130 hertz that is close but not right, and it is the offsets rather than the values or the shape that want another pass.

So the shape and the values of English intonation are five numbers and a rule, which is a good deal smaller than expected, and the placement is a measurement still to do.

### The pitch, anchored properly

Stas heard the first cut and it was noticeably wrong on `hello` and `tomato` and right on `money` -- which is the whole diagnosis, `money` being the one of the three whose accent is on its first syllable.

Two things were wrong and the measurement had hidden both behind medians taken over the wrong population. **The rise lasts 124 milliseconds, not 51**: the engine takes 116 on `hello` and 131 on the other two, and the 51 came from a median that mixed in words whose contour has six points rather than five. And **the first fall ends where the last vowel ends** -- median six milliseconds past it -- where I had it at a fixed 61 before the word's end. Neither a fixed offset nor a fraction of the word is an anchor for it: those scatter from 0.69 to 0.89 of the word between the quartiles.

With the peak at 0.47 through the accented vowel, the rise 124 before it, the first fall ending with the last vowel and the second 67 after that, the contour comes within **2.0 hertz at the median and 3.2 on average**, against 3.5 and 5.7, and 61 per cent of frames are within three hertz rather than 46.

### The word has to finish

Stas heard the second cut and it was almost right: **"ours ends the way the engine would end at a comma, instead of ending the sentence."**

Exactly so, and the numbers say the same thing once you know where to look. Anchoring the first fall to the last vowel's end leaves nothing for the second fall on a word that ends in a vowel -- `hello`'s last vowel ends where the word does -- so the contour stopped at 792 rather than reaching 742. Eleven twentieths of a hertz, five hertz on a contour spanning fifty-six, and it is the difference between a full stop and a continuation.

**A landing has to be reserved.** The second fall lasts 58 milliseconds -- quartiles 40 and 64, the one tight number in the whole tail -- and ends three before the word does. So the first fall ends at the last vowel's end **or** 58 before that landing, whichever comes first: anchoring only to the vowel clips the landing away on a word ending in one, and anchoring only to the landing puts the fall late on a word ending in consonants.

With both, the contour is within 2.0 hertz at the median and 3.1 on average, and the word's last frame is within 0.3 hertz of the engine's with a worst case of 1.0. The intonation is now a rule with five values and four anchors.

### The burst belongs at the release

Stas heard `banana` as "vanana": the initial /b/ came out a fricative. The frames say why at once. The engine holds the frication and the bypass at nought through the closure and then bursts -- 51 and 70 at the eleventh frame, decaying over five -- and the generated version had them at 51 and 70 from the first frame, which is a stop's burst smeared over its whole closure, which is a fricative.

**It is the collapse of repeated targets, biting from the other side.** /b/ has three stretches and the engine's frication reaches nought, nought, then 51; collapsing the repeat leaves two targets for three stretches, and the generator padded at the back, putting the 51 first. Which of them the spare stretch belongs to depends on whether the last target jumps: **a jump means a new value starts there, so it belongs at the end and the padding goes in front.** Without a jump the last target is a hold and the padding goes behind it, which is what /A/ at the start of `abbey` wants.

With that, the frication's wrong values fall from 3,531 to 668 over a hundred and fifty words, the tilt and the bypass leave the list of the twelve worst entirely, and the whole error falls from **3.955 per cent to 2.961**, with three words frame for frame rather than one.

**And one measurement stops working here.** With the pitch generated too, the waveform difference against the engine reads 120 to 140 per cent for every word -- not because the words sound wrong but because two hertz of pitch difference decorrelates the phase completely. Root-mean-square difference is only meaningful while the pitch is shared; past that the parameter counts and the ear are the measures.

## An automated ear

Stas asked whether there is a better way to measure the engine, and there is, because counting wrong parameter values had stopped being informative. Two of the three faults he heard -- `banana` as vanana, `abandonment` as abandonwend -- were a per cent or two of values inside one segment, invisible against a total of three per cent, and each turned one phoneme into another.

`tools/measure/confuse.py` asks the other question: **not how far a segment is from where it should be, but whether it is now nearer to some other phoneme.** It builds a centroid for every phoneme out of the engine's own frames -- the mean of fourteen parameters over every segment of that phoneme in the corpus, each scaled by its own spread so a formant and an amplitude count alike -- and then asks of each generated segment which centroid it lands nearest.

**The control is the whole of it.** A centroid is one mean over a whole segment, so phonemes that are genuinely close land on each other whoever made them: the engine's own segments land on the wrong phoneme 25.7 per cent of the time by this measure. Ours land wrong 28.9. Neither number means anything alone, and the difference does: **86 segments of 2,159, 3.98 per cent, are ours alone** -- the engine's landed right and ours did not.

And it names them. Schwa heard as the second half of /Y/ 23 times, /t/ as the first half of /C/ 11 times, /k/ as /p/ 9, /W/'s first half as /Y/'s 7. That is a work list, arrived at without listening to anything, and it is what the next round of fixes should be driven by.

**It also puts the earlier faults in proportion.** A stop's burst smeared over its closure and a nasal with no murmur were each a handful of frames in one segment of one word; against 556,740 parameter values they were noise, and to a listener they were a different word.

### Two more from one word

`abandonment` was still abandonwend, and the /m/ had two faults, both of a kind the parameter count could not see and the automated ear could.

**A run-through's start has to be recorded always**, not only when it differs from what came before. The /n/ before the /m/ ran its nasal zero past its own end, and with the /m/ saying nothing there was nothing to stop at, so the ramp went to the default 200 -- the murmur gone. The /m/ said nothing because its 350 matched the /n/'s and so read as no jump at all. Recording it unconditionally puts the murmur back exactly: 350 held for five frames and then falling, to the digit.

**And one stretch can carry several targets.** The table writes /m/'s second formant as 1000 then 1200, and the engine draws it as a single stretch from 1000 to 1200. Taking the last target and starting from wherever the segment before left off gave a ramp from 1500, which is not a nasal. The rule is that such a stretch runs from the target before the last to the last.

Together they take the error from 2.413 per cent to 2.269, the frication's share from 573 to 308, and the confusions the automated ear finds from 86 of 2,159 to 77 -- **3.57 per cent against the engine's own 25.7 by the same measure.**

### The context is two deep, not one

`abandonment` stayed wrong after the nasal was fixed, and the fault was not in the /m/ at all: the word's whole unstressed tail came out flat where the engine glides. The schwa before the last /n/ runs its second formant from 1500 to 1600 across the segment boundary, and neither side held the 1600 -- not the schwa, because the stretch runs past its own end and only its start was recorded, and not the /n/, because 1600 is where it already was and so was not a jump.

**So a stretch that runs past its segment has to be recorded whole**, end included. The end is where the line goes, and the engine drew the line there.

That fixes the tail and exposes what was underneath it. **A segment's breakpoints depend on the phonemes two away, on both sides.** Measured over fifteen hundred words on the second formant: with the key as it stands -- the phoneme, its neighbours and its stress -- 218 of 1,532 repeated keys disagree with themselves, 14 per cent. Adding the phoneme two to the right takes that to 122, six per cent. **Adding both takes it to 20, one per cent.**

And it is not prosody. Adding how far the segment is from the end of the word, or its syllable's coda, or where the syllable sits, or the next syllable's onset -- each of which earned its place in the duration key -- takes 14 per cent to between 9 and 12. The reach is segmental, and the reason is plain once the run-through is recorded: a stretch that ends inside the next segment ends at *that* segment's target, and that target depends on *its* neighbours, one of which is two away from here.

**Which leaves a coverage problem worth stating.** The de Bruijn corpus covers every phoneme between every pair, at order three. Two either side is order five, which is 184 million strings for a forty-five letter alphabet and is not going to be spoken. So the long key can only be filled from real words, and everything else has to fall back to the short one -- another level of the same base-and-exception structure the table already is.

### Building the two-deep key, and why it was taken out again

It was built: a third level under the rectangles, one line for each context two phonemes deep that disagrees with the rectangle above it. **The table's own self-disagreement fell from 16,635 to 4,004**, which is what the measurement promised.

**And it generated worse, 2.245 per cent of parameter values wrong against 2.809.** The reason is the coverage problem, arriving sooner than expected. Of 1,738 far lookups over a hundred and twenty real words, **58 hit**. The 150,346 far contexts in the table are almost all the de Bruijn corpus's -- arbitrary chunks of eight phonemes, whose two-away neighbours are combinations no English word contains -- and the words' own far contexts are mostly not there at all. Meanwhile the rectangles beneath got worse, their majority now being taken across the two-away combinations rather than over the contexts that actually occur.

So the finding stands and the implementation does not: **a segment's breakpoints depend on the phonemes two away, and that key can only be filled from a word corpus.** Filling it from made-up strings is not merely useless, it is harmful, because it moves the fallback underneath it as well. This is the same lesson the durations gave -- a made-up chunk has made-up prosody -- one level further down, and it is the second time a corpus built for coverage has turned out to cover the wrong thing.

The tools are reverted to the two-per-cent state. Doing this properly wants the far level harvested from `test/samples/enus.words` alone, with the rectangles left as they are, and that is a small change to make once rather than a thing to guess at.

### The two-deep key, third attempt

Harvested from the word corpus alone and with one further filter, it works.

**The filter is the whole difference.** A far context whose shape is only the rectangle's shape cut short is not a disagreement -- the generator already lays a shorter segment by stopping the same line early -- and recording those was the whole of the first two attempts. Of 179 far lookups that hit, **164 replaced a two-target rectangle with a one-target truncation**, which is why adding the level made the words worse both times.

With truncations filtered out, the far level is **1,301 lines** rather than 150,346, and it helps: 2.245 per cent of parameter values wrong falls to **2.187**, five words come back frame for frame rather than four, and the automated ear's count falls from 77 confusions to 73.

That is a small gain for a lot of machinery, and it is worth saying so plainly. The finding that a segment reaches two phonemes either side is solid -- 14 per cent self-disagreement to one -- but almost all of what that explains is truncation, which was already handled. **The residue is a per cent of a per cent.** What is left wrong is elsewhere: the second and third formants at 3,702 and 2,750 wrong values, and the voicing at 2,617, none of which the two-deep key touches.
