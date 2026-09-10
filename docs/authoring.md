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

**What that makes of the measuring.** It is the oracle, not the product. Every value read out of a rule can be held against the tables and the tap, which is the "prove it before saying it" this tree runs on, and the audibility calibration from an ear stays the standard for when something is close enough. But the tables were the wrong deliverable, and the mistake was not looking for where the numbers come from before spending a day measuring where they land.

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

