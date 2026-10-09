# What will trip you

The things about this engine that cost an afternoon if nobody says them first. Some are ours, most are IBM's, and each says which. `docs/api.md` is the reference and `docs/using.md` the integration guide; this is the list to read before deciding you have found a bug.

## Silence

**An instance sends its samples to an audio device by default, and there is no audio device.** The platform layer answers that this machine has none, which is what sends the engine down the buffer path that everything else depends on.

Until a buffer is registered the instance has nowhere to put anything, and `eciAddText` answers nought. It is the only call that says so, which is the part to know: `eciSynthesize` and `eciSynchronize` both answer success on an instance that will never speak, and `eciSpeaking` answers that it is not. A program that checks the calls it thinks of as the important ones and not the one that adds the text gets silence with no complaint.

`eciSetOutputFilename` and `eciSynthesizeFile` do not help: both are empty in IBM's own object and write no file. There is no way to make the engine write a file. Write it from the callback.

`eciSpeakText` and `eciSpeakTextEx` make a whole instance, say one thing and take it away, registering no callback and no buffer on the way -- so the instance they make is exactly the one above, its `eciAddText` refuses, and both answer nought having said nothing. They also make and destroy a synthesis thread and a four megabyte frame stack each time they are called.

`eciSetOutputBuffer(h, 0, 0)` puts an instance back to the device, which is to say back to silence.

## No error ever gets reported

`eciProgStatus` and `eciIsBeingReentered` always answer nought. `eciErrorMessage` writes nothing into the buffer it is given. `eciClearErrors` clears what nothing reads, and `eciRequestLicense` answers nought. All five are empty in IBM's own object and are transcribed empty here. The instance really does record what it refused and why; nothing published hands it over.

`eciStartLogging`, `eciStopLogging`, `eciGetLog`, `eciGetIntLog` and `eciDialogBox` are empty in IBM's object too.

So judge by return values: nought means refused, and `eciSetParam` answers -1.

## Answering the callback wrong costs thirty milliseconds a buffer

`eciDataNotProcessed` means "my buffer is full, offer these samples again", and the engine answers that by sleeping for thirty milliseconds and offering the same buffer. That is right for a player whose buffer is genuinely full and ruinous for a program that is discarding.

**A program that means "throw this away" must answer `eciDataProcessed`.** This is IBM's behaviour, nothing in the interface warns of it, and it was found while chasing a latency that turned out not to be the engine's at all.

## Nothing can make the engine abandon an utterance

`eciStop`, `eciDataAbort` and simply letting the utterance finish all cost the same, because all three wait for the same thing: the synthesis thread finishing the message it is on. All a stop does is stop more buffers being handed over.

This is settled rather than open. The rules build a shared structure as they go and later rules assume the earlier ones finished it, so abandoning a rule part way leaves that structure half built and a later rule faults on it. Six ways of adding an abandonment point were tried and all fail, and the reason is one thing rather than six. Making the machine interruptible would mean the rules checking their own inputs, which is the language and not a patch.

What can be done about the latency is doing the leftover work faster, which is what `RULES=c` -- the default -- is for: about 27 milliseconds to cancel and speak again against about 86 interpreted.

An interrupted utterance is not necessarily short, and no harness asserts that it is. Whether the sample count comes out short depends on where the suspension lands between two buffers, and a callback that paces itself like a real player holds the engine waiting for each answer, so the stop then waits for the whole delivery and can never truncate.

## The second utterance is not the first, and that is correct

Saying the same sentence twice on one instance gives 38,423 samples both times and 30,495 of them differ, because the machine's state has moved on. It is entirely deterministic -- three processes give the same first utterance and the same second one, to the hash -- and IBM's own engine does it to the same 30,495 samples, with ours matching its second utterance byte for byte.

So samples are comparable, a second utterance included, as long as both sides have spoken the same history. **What is not comparable is a second utterance against a first.** A program checking the engine against a recorded hash has to compare like with like.

## Text is bytes, not UTF-8

The engine reads single bytes and IBM's engine does almost nothing between the caller's bytes and the machine's characters. Text is the language's own code set, which for eight of the nine languages IBM shipped is the Windows Western set; Japanese is the exception and its romanizer takes any of five. Handing it UTF-8 gets the mangling IBM's engine produces, byte for byte.

The exception is a language that declares characters of its own, which is Polish and none of IBM's nine. Those get their text converted from UTF-8 on the way in, and their own bytes let through the romanizer's table rather than turned into spaces. Those two are the fourth and fifth deliberate divergences, and the guard is the whole point: the nine IBM shipped declare no characters, so nothing about them changes.

UTF-16 is not a way round that for these. ORing `eciUnicodeCodeSet` -- 0x800 -- into the language asks for it, and only a language whose text goes through a romanizer will take it: IBM's table of what may be asked for groups code sets under Chinese, Japanese and Korean and nothing else, so of the languages here that is Japanese alone. Asked for any of the others, `eciNewEx` answers no instance and `eciSetParam` answers -1 and leaves the language as it was, in IBM's engine and in ours. The code set is the third byte of the same word the language is in, and `eciLanguageDialect` is what the engine reads to find out; `eciTextMode` is a different setting and is not it.

## Parameters that are not what they look like

**`eciDictionary` is inverted.** One turns the dictionary off. The value is flipped on its way in and out, which is IBM's. It is the abbreviation dictionary, and it is off here unless asked for, where IBM had it on -- see below.

**Numbers 11 and 17 are refused by both `eciGetParam` and `eciSetParam`**, which is IBM's own refusal transcribed. Eleven is phrase prediction, which only `eciSetDefaultParam` and the `` `pp `` annotation reach, and which is off here where IBM had it on -- see below. Seventeen holds the number of the voice being spoken in and `eciCopyVoice` is what moves it.

**Numbers 4 and 6 can be set and read and nothing anywhere reads them.**

**The sample rate has a gap in the middle of its range.** Nought to six are the numbered rates and 8,000 upwards is a rate in hertz, so seven to 7,999 pass the range check and are not rates. `eciSetParam` answers -1 for those and leaves the rate alone. It used to answer -1 and record the number anyway, so `eciGetParam` reported a rate nothing was synthesising at; that was ours rather than IBM's, and it was only reachable because we widened the range.

**IBM's own fourth sample rate could not be asked for.** It numbered 16 kHz rate three and then set the range of the sample rate parameter to two. The range here runs to the highest rate the tables can be built for, which is the first of three divergences in `docs/notes/sample-rates.md`. The other two are that 22.05 and 16 kHz are doubled now rather than mislabelled, and that a rate given in hertz is not something IBM's engine would take at all.

**`eciWideband` (32) changes the sound and not the rate, and only above 11,025.** It is ours, so IBM's engine answers -1 for it and a program written for both has to take that as "not here" rather than as a failure. At 8,000 and 11,025 it is accepted and changes nothing, since there is no top to add. It is not one of the settings `eciGetDefaultParam` and `eciSetDefaultParam` know, `eciReset` turns it off, and at 16,000 it puts the sound six milliseconds later than the default, which is the cost of lowering a rate rather than raising one.

**Changing the sample rate loses a registered buffer in IBM's engine and not in ours.** IBM rebuilds the output as a device regardless, which hands the engine a null buffer on the way past; the instance then reports the new rate ever after and answers no more samples. Ours chooses on where the samples were already going. That is the first deliberate divergence, and the four audio-format numbers -- parameters 13 to 16 -- are the second, for the same reason.

## Voices

Voice 0 is the one being spoken in, 1 to 8 are the language's presets and are read-only, and 9 to 16 are the caller's. `eciCopyVoice` will only write to 0 or to 9 through 16. The way to a voice of your own is to copy a preset into an editable number, change it there, and copy that onto 0.

IBM's engine keeps that promise only when the source is refused as well. Its test asks about the destination and the source together and, once a real voice is named to copy, about the source alone, so a copy onto 1 to 8 or past 16 goes ahead and writes eighty bytes wherever the number points: into the instance's own fields in front of the editable voices, or past the end of the instance. Here a destination the caller does not own is refused whatever the source, which is the twentieth deliberate divergence. KamiKitsune420's eloquence-decomp, a rebuild of desktop Eloquence 6.1, found the same hole there and refuses it too. `make voices` is the check.

The presets are named `Adult Male 1`, `Adult Female 1`, `Child 1`, `Adult Male 2`, `Adult Male 3`, `Adult Female 2`, `Elderly Female 1` and `Elderly Male 1`, and all eight editable ones are called `User-Defined` until something renames them, so a program picking a voice by name has to know that the interesting ones are the read-only eight.

## Making a filter

`eciNewFilter` speaks out whatever is queued and refuses while anything is outstanding. So an index inserted, or text added, before the SSML filter is made makes `eciNewFilter` answer nothing -- which looks exactly like the filter being unavailable. Do the filter setup first.

`eciRegisterFilter` takes the **address of a variable holding** the entry point rather than the entry point itself.

`eciActivateFilter`, `eciDeactivateFilter` and `eciSetFilter` take the handle `eciNewFilter` answered with, not the id it was made from, although the engine's own transcription of them takes that handle as a thirty-two bit number. It holds because every filter is in the arena and every arena address fits in thirty-two bits. `include/eci.h` declares them as taking a handle, which is what they take.

`eciGetFilteredText` takes four arguments and none of them is a buffer size: the filter, the document, and where to leave the answer. The wrapper in `lib/eci_api.c` declared the last as an `int` until the header was written, which truncated the caller's answer pointer on the way past and worked only because the compiler tail-called rather than storing anything.

Hand the reader a document it may write on. IBM's reader ends the digits of a numeric character reference by writing a nought over the semicolon that closes them, in the caller's own string, so `&#65;` in a literal page-faults. Ours copies the digits out instead, which is the eleventh deliberate divergence -- but a program meant to work against both engines should still pass a copy.

## Names that are not exported

**Nothing.** Every one of the seventy-one names IBM's own `eci.obj` publishes is exported, and nothing is exported that IBM does not publish. That was checked by diffing the two export tables rather than by counting.

Two of the seventy-one are empty in IBM's object as well as here: `eciGetAvailableFilters` and `eciGetFilterDescription` answer nought and never touch what they were handed.

## The dictionary has a fourth volume, and it is not yours

A set holds `eciMainDict`, `eciRootDict` and `eciAbbvDict`, and then `eciMainDictExt`, which keeps a part of speech beside each entry. That fourth one exists only for a language written in another script -- Chinese, Korean, Japanese -- so for every language in this tree all eight dictionary calls answer `eciDictInvalidVolume` when you ask for it. That is IBM's, and it is why the calls come in pairs: the `A` forms carry the part of speech the extended volume wants.

`eciDictLookupA` answers an error code where `eciDictLookup` answers the string, and it finishes by turning an empty answer into `eciDictNoEntry` -- a test it makes even on the roads that never write your pointer. So clear your own variable before calling, or an invalid volume comes back as no entry. Both are IBM's.

One asymmetry in IBM's own switch is carried here because it is IBM's: Chinese in code set two reaches the extended volume in its second dialect and not its first. No build of this tree can show it, there being no Chinese in this SDK at all.

## A dictionary can be loaded from a file here and not in IBM's engine

`eciLoadDict` and `eciSaveDict` answer `eciDictNotSupported` in IBM's engine whatever they are handed, and did from the day they were published. Here they work, which is the nineteenth deliberate divergence: they go through to the volume calls the newer interface has, which the layer below always implemented. A program meant to work against both engines cannot rely on either name, and has no other way in through the older interface -- the per-entry calls are not exported by it. `make dictfile` is the check.

## Asking for phonemes

`eciGeneratePhonemes` will answer nought and look broken unless three things are true, and the first two are IBM's own tests rather than advice. A callback has to be registered, because that is the only way the phonemes can arrive. `eciSynthMode` has to be one, because the call walks the queue that mode builds. And the text has to have been added first.

What comes back has the separator between phonemes followed by backspaces and spaces, because the engine is writing to what it takes for a terminal. That is IBM's, byte for byte.

## Calling convention on thirty-two bit Windows

IBM's own objects export `_eciSetParam@12` and `_eciAddText@8`. The byte count is part of a stdcall name on x86, so the published interface is stdcall there and a caller that assumes so of a cdecl function leaves the stack out by the size of the arguments after every call.

`include/eci.h` says `__stdcall` on Windows for that reason, and `eci32.dll` is built from it. The names are published undecorated all the same, because that is what a caller asking by name asks for. On x86-64 there is one convention and none of this arises; off Windows there is nothing to say.

The wrappers in `lib/eci_api.c` were plain until the header existed, which was right for the sixty-four bit library and wrong for the thirty-two bit one.

## The words the engine cannot say

Every string that kills IBM's Eloquence is the same fault: the Delta machine dereferences a node reference of nought, either because a rule left a position variable unset and then walked from it, or because a walk stepped off the end of the spine. IBM's binary faults on the same reads.

**Four places in the Japanese phrase table read IBM's own uninitialised frame, and ours read nought or do not read at all.** `PhraseTable::FzkAccent` copies fifteen sixteen-bit values into its answer and clears only two of the three arrays it keeps, so a group the walk never reached carries out whatever was on the stack. `PhraseTable::SetSuushiPhraseTable` takes the length of the entry after the last its caller filled off the phrase's mora count, and in `SetSuushiPhrase`'s frame that entry is never written. And two of the four arms of `SetPhraseTable` that say whether a function word ends a phrase write nothing where the word runs to more than one mora, so what the accent walk reads there is whatever `SetPhraseTable`'s own frame held. Ours clears both, so both read nought. That is the fifteenth deliberate divergence, and there is nothing in a stack to reproduce; the entries that are reached are written before they are read either way.

**And a fifth, in the writer that turns a settled stretch into phrases.** `TextAnalysis::UpdatePhraseBuffer` asks `CountMoraInPhrase` how many moras the words run to and is handed back, through a pointer, how long the last word it corrected was. That method corrects a word only where the phrase is of the ninth kind -- a run of digits -- and the word carries both of two tags. The writer then walks the words twice more and writes that length back into any word carrying those two tags, whatever kind the phrase is. So a phrase of another kind with such a word has its reading set from a number nothing wrote: IBM's uninitialised local. Ours reads nought. That is the seventeenth deliberate divergence, and the sweep keeps to the side of the line rather than comparing it -- only a phrase of the ninth kind is given a word carrying both tags.

**The fourth of those places is a pointer, and IBM dereferences it.** `SetPhraseTable` reads the function words of a phrase into the accent walk's own record and stops early once the phrase has run to the twenty-five moras a row holds, and then tells the walk how many function words to score by counting them off the phrase rather than off the loop that stopped. Where it stopped early the walk reads a rule pointer that loop never stored -- on IBM's stack the pointer the last call left there, which is a real entry of the accent table and lets the run carry on producing an answer nothing can reproduce; on ours a nought, which faults. Ours scores the function words that were read. That is the sixteenth deliberate divergence, and it is the only one of the four where reading the frame could kill the engine on a caller's own text rather than merely give a number nobody can predict.

**A mark the engine reaches with no note behind it is a read of address four in IBM's engine.** `SynthThread::userIndexCallback` clears the pointer it is about to ask the mark queue to fill, asks, and then -- where the queue answered that it had nothing -- writes two noughts through the pointer and reads two fields back out of it. All four go through nought. Ours gives up instead when the queue had nothing, which is the eighteenth deliberate divergence. It is reachable: an index annotation whose closing quote is missing makes the Japanese romanizer write the mark and leaves no note to go with it. IBM's own engine hangs on that text before it ever arrives, which is why the fault is one only this engine can find, and why there is no oracle for it -- `test/cases/anno-jajp.txt` carries a closed mark in that place instead, and the hang is recorded here.

**IBM's Japanese engine hangs on an index annotation whose quote is never closed.** Not ours, and not any other language: the same shape in `test/cases/anno-itit.txt` is one of the twenty Italian cases and both engines agree on it. Ours speaks the Japanese one and finishes. There is nothing to compare it against, so it is not in the case files.

**IBM never writes the return code of `TextNormalizer::makeReadable` when the annotation's number names no reader.** The switch has an arm for each of the six kinds and no default, so a number outside them leaves the local it returns through untouched and the caller gets whatever the stack held there. Ours returns nought, which is what every arm that succeeds returns. That is the fourteenth deliberate divergence, and nothing in the engine can reach it: the numbers come from IBM's own table of annotation names, every one of which names a reader.

Thirteen places now test for nought and answer the way that primitive already answers everything else it cannot do, and 43 walks count their steps so that links which have come round on themselves end the rule rather than hanging the engine. That is the tenth deliberate divergence. Every guard sits on a path the old code could not survive, so no working input can reach one.

**A pause after such a string faulted after the string itself had been survived.** The guard abandons the words, and if a pause annotation follows them -- the `` `p1 `` the IBMTTS and Eloquence 64 drivers write at the end of a line, or the one `eciPauseMode` writes -- the pause is synthesised on an instance whose synthesiser was never given its parameters, since the words that would have set them never arrived. It names no rate, so nothing looked changed and the synthesiser was opened unset, which called an error reporter nobody had installed. Twelve of the strings did it. `synthesize` in `src/klatt/klatt_run.c` now answers nought for a call that names no rate when no rate was ever set, which is silence, and silence is what the pause was.

`test/cases/crashers.txt` is the text, `make crashers` is the check, and `docs/notes/crashing-strings.md` is the whole of it. If your program feeds the engine arbitrary text -- a screen reader does -- this is the class of thing it used to die on.

## Mixing toolchains on Windows

The libraries in a release are built by one mingw and tested with harnesses built by the same one. A caller built by a different mingw, with a different thread runtime -- nixpkgs uses mcfgthreads where Debian uses winpthreads -- can fault on the crossing, and one direction of that pairing does.

It does not matter for the callers that exist: Python's ctypes and a screen reader's host DLL are MSVC built with no mingw runtime in them at all, and CI checks both of those crossings on Windows itself. But do not conclude from a fault in a hand-mixed pair that the shipped library is broken. Check a matched pair first.

## Long numbers with commas in them

**IBM's English reads a group of three noughts wrongly when another group follows it.** 1,000,000 is one million comma hundred, 1,000,500 is one million comma hundred fifty zero, and 25,000,000 is twenty five million comma hundred, in both Englishes. The same numbers without commas are right, and so is a number whose noughts come last, 1,001,000 and 5,000 among them. Ours reads every one of them as the number without commas is read, which is the twenty-first deliberate divergence.

A long number is read three digits at a time, and the rule that inserts the word naming a group -- million, thousand -- also steps over the comma after the group, so the next group starts at its first digit. A group of noughts names nothing, so in IBM's rules nothing steps over its comma, and the next group is read from the comma, which is spoken as its hundreds digit. Ours steps over that comma too. And where the last group is noughts, the commas before it come out, since nothing is left after them to stand in front of: IBM's reads a 5,000-strong army as five thousand dash strong, and where such a number ends the text its phoneme report ends with a comma that makes no sound.

The rules are `lang/enus/rules/ut_numbr.up` and its British twin, standing where `convert_hundreds` stood in `convert_large_numbers`. Plain 8, dict 8 and second 8 of each English in `test/matrix.sh` hold it, and `test/suite.sh` reports those three as differing in both, which is the oracle saying what the original does. A comma list such as 1,2,3 is one comma two comma three as before.

What this leaves is a hyphen after a grouped number, which is read as minus where the same number without commas reads it as to or dash -- 5,001-6,001 against 5001-6001 -- with no group of noughts in it at all. That is another rule deciding from the commas themselves, IBM's as well, and not touched.

## A word typed against a number or a bracket

**IBM's English spells a word that runs straight into digits or into an opening mark, and ours reads it as a word.** Windows10 came out W I N D O W S ten, teamtalk5 T E A M T A L K five and this(thing) T H I, a pause, then S thing, where Windows 10 and this (thing) read as words. What decides it is `letter_sequence` in the text normaliser, which spells any run of letters that does not end where a word ends -- right for MP3 and H2O, wrong for a word somebody did not space. From 1 to 8 October 2026 both Englishes had a rule of ours, `eng_tok_lookup`, that put the space in before anything read the token, so the two halves read exactly as they would typed with one, and that was the twenty-second deliberate divergence. Both now read these tokens as ETI Eloquence 6.1 does, which needs no rule of ours: the section on 6.1's normaliser below says how. What the rule measured is kept here, because it is where 6.1's reading differs and why that is a cost.

Where it applies was measured over 242 tokens rather than guessed. A run of four letters or more takes the space before a digit or before `[`, `{`, `#`, `$`, `*` or `~`, and a run of two or more before `(`. Shorter runs before a digit are mostly the acronyms spelling is right for, and several read worse as words -- utf8, ipv6, amd64, sha256, CO2 and A4 among them -- so they are spelled as before, and so are AUD$5 and the other currency codes, which spaced say the currency's name and then dollars again. A parenthesis takes two letters because IBM's reading of a short word before one is broken outright, sum(x) coming out S U, a pause, M X; the exception is the (s), (es) and (ed) IBM already folds into the word, so friend(s) is still friends. The token is looked up whole before the space goes in, so an entry for it as written still wins, Win32 in the language's own table or one a caller added.

What it costs is that a word the engine says badly on its own is now said badly rather than spelled: iPhone12, MySQL8, Xbox360 and macOS14 say iPhone, MySQL, Xbox and macOS the way the engine says each typed alone. Digits before letters -- 10km, 3rd, 4x4 -- are untouched, and so are an apostrophe, a hyphen, an at sign and a slash. German, both Spanishes, both Frenches, Italian and Polish spell the same tokens the same way and are left as they are; Japanese's romanizer reads them as words already.

The last two cases of `test/cases/plain.txt` and of `test/cases/plain-engb.txt` hold 6.1's reading now, in the plain, dict and second categories. IBM's engine spells the words in them that this one reads, so none of the twelve is an answer it gives.

## Phrase prediction is off

**IBM's engine guesses where a phrase ends in a stretch with no punctuation and pauses there, and ours does not unless asked.** The reporter's sentence in issue 41, "world wide web is three syllables but luckily they abbreviated it to 9", has a 220 ms pause after "syllables" in IBM's engine and none in ours. A pause the text gives no reason for is one a listener cannot see coming, which is why issue 41 asked for a way to turn it off, and off is the default. That is the twenty-third deliberate divergence.

It is one switch in each language's rules and it governs three of them: `find_unpuncted_phrases`, which makes the guessed breaks, and `handle_non_phrasal_commas` and `delete_comma_sync`, which decide that a comma is not a break. So off has a second effect, which is that a comma those two would have passed over is a break like any other: "Paris, France, is where she lives" gains 170 ms after "Paris". All nine languages with rules have the switch. Japanese has none and is unchanged, and none of Polish's cases moved.

`` `pp1 `` in the text, with annotations on, turns it back on for the instance from there on, and `eciSetDefaultParam(11, 1)` does so for every instance made afterwards; `eciGetParam` and `eciSetParam` refuse eleven, as IBM's did. The default is the eleventh of `g_DefaultEnvironment` in `src/eci/api/eci_env_defaults.c`, which the older interface sends to every engine it makes, and `CMD_DEFAULTS` in `src/eci/synth/eci_synthlife.c` says the same for the newer one. The NVDA add-on has a checkbox for it, also off.

It moved 165 of the 1,073 recorded cases: 8 of English, 7 of British English, 9 of German, 40 of each Spanish, 27 of each French and 7 of Italian, none of them the plain cases of the Englishes, German or Italian. `test/compare.sh` turns phrase prediction on in ours whenever it is held to IBM's, so `test/suite.sh` compares like with like and does not report them.

## Abbreviations are said as written

**IBM's engine says what it takes an abbreviation to stand for, and ours says what is written unless asked.** With the abbreviation dictionary on, "Dr. Smith" is doctor Smith, "10 mg" is ten milligrams and "6 ft" six feet; off, they are D R, M G and F T, and "Feb" is read as a word. Every expansion tried in English guessed right -- "Elm Dr." was drive and "Dr. Who" doctor -- so this is not a fix for wrong guesses. It is that a listener hears a word that is not in the text and cannot tell that it was not, which matters to anyone who has to type, search for or correct what is being read. The NVDA add-on's "Expand abbreviations" checkbox has been off since its first version, and the engine now starts where the add-on does. That is the twenty-fourth deliberate divergence.

Off costs two things, and both are what IBM's own engine does with the dictionary off rather than anything added here. The full stop after an abbreviation is a full stop: with nothing to say that "Mr." is an abbreviation, its stop ends a sentence and pauses like one. "Mr. and Mrs. Jones live on Main St. in town." takes 3.5 seconds with the dictionary and 5.4 without, three pauses of about 400 ms where there were none. And the switch is the abbreviation volume's as well: a caller that loads entries into its own `eciAbbvDict` hears none of them until it turns the dictionary on. The main volume is unaffected.

`eciSetParam(h, eciDictionary, 0)` turns it back on for an instance and `eciSetDefaultParam(eciDictionary, 0)` for every instance made afterwards -- nought is on, the parameter being inverted, which is IBM's -- and `` `da1 `` does the same from inside the text with annotations on. The default is the fourth of `g_DefaultEnvironment`, which holds it the right way up, and `CMD_DEFAULTS` says `` `da0 `` for the newer interface, as for phrase prediction. In each of the nine languages with rules the switch governs one rule, `abbreviation` in ut_norm.

It moved the audio of 55 of the 1,073 recorded cases: 13 in each English, 12 in German, 5 in each Spanish, 4 in Italian and 3 in French, and none in Canadian French, Japanese or Polish. Each is an abbreviation now said as written -- "Dr.", "z.B.", "p.ej.", "p. 12" -- and two are less plain than that. The engine reads UTF-8 a byte at a time, so "Muñoz" ends in "oz", which was ounces; and French's case writes "qu" without its apostrophe, which the dictionary took for que. The second hash moved in 745, because in most categories the probe reports every general parameter and `eciDictionary` now answers one. Of the word lists, 114 words moved, every one an abbreviation: 76 of English's, 16 of each Spanish's, 2 of each French's, German's "log" and Italian's "mar", which was Tuesday. `test/compare.sh` and `test/harness/phonemes.sh` turn the dictionary back on in ours whenever it is held to IBM's, so the oracle compares like with like.

## Punctuation is read as 6.1 reads it

**In both Englishes a sentence mark standing on its own is punctuation, as ETI Eloquence 6.1 reads it, where IBM's 4.3 said its name.** A lone semicolon was "semicolon", 14,410 samples, and is nothing now; "value ; next" was value, semicolon, next and is value, a pause, next. That is the twenty-fifth deliberate divergence, and the first that is a later engine's behaviour rather than one of ours. Issue 4 reported it against a later IBM library, and IBM's own engine built from the SDK this project comes from says the name exactly as ours did, sample for sample in every text mode, so it was never a fault in the port: 6.1 rewrote the rule.

What 6.1 does, measured against it by phonemes. The six sentence marks -- full stop, comma, semicolon, colon, question mark and exclamation mark -- standing alone or between single letters are punctuation in text modes 0, 1 and 3, a pause and a break in the intonation, and are named only in mode 2, verbatim. Brackets and double quotes are named in modes 2 and 3 and in neither of the others, alone or not. A screen reader that hands over a line holding only a mark, or reads a code line with its punctuation level turned down, gets silence or a pause, which is what 6.1 has always given it.

Thirteen of each English's rules are 6.1's, lifted by `tools/rules/lift64.py` out of the US and British modules Apple ships and written at the end of each one's `ut_norm.up`: `punctuation`, the three 6.1 added for it -- `read_punct_by_name`, `in_brackets` and `convert_phone_number` -- and nine it changed with them, `end_of_word`, `end_of_sentence`, `single_chars`, `skip_punct_and_delimiters`, `build_phrase_final_structure`, `interpret_single_char_modes`, `parenthesis`, `bracket` and `quote_mark`. Taking `punctuation` alone hung the engine on a lone question mark, because 6.1 changed those together. And 6.1 changed the alphabet with them: 23 characters -- the space, both round brackets, the closing square and curly ones, the hyphen, underscore, plus, slash, equals sign, ampersand, backslash, greater-than sign, double quote and eight above 127 -- are a sixth character type, `eow_dlmtr`, which 6.1's `end_of_word` tests and 4.3's data never had, and ß is a consonant. Without that every word but the last of a sentence was spelled out. Each English's input statement now holds 6.1's records byte for byte, its British records being its American ones. The other languages read a lone mark as 4.3 does until their own modules are lifted.

There is no switch back, unlike the twenty-third and twenty-fourth: no parameter of IBM's ever chose between the two readings, and the rules are 6.1's whole rather than 4.3's with a flag in them. So `test/suite.sh` holds English's punctuation to IBM's 4.3 binary and will report a difference on any case with a mark standing alone; `test/harness/eti.sh` is what holds it now, against 6.1 itself, and `docs/testing.md` says how.

It moved three of the 1,073 recorded cases, all three in English's `utf8` category. Those hand the engine UTF-8 as it is, a byte a character, and a UTF-8 letter's second byte is often one of the characters above 127 that are now word delimiters, so the garbled text breaks into words at different places; the same sentences in the engine's own code set read exactly as they did. Not one of the 24,317 words of English's list moved, and the other nine languages are as they were.

## Words, addresses and paths are read as 6.1 reads them

**Both Englishes' text normaliser is 6.1's: a word typed against digits is a word, and an address or a path is read a part at a time.** Windows10 is Windows ten and this(thing) is this thing, as the twenty-second divergence made them; utf8, ipv6, amd64 and sha256 are utf eight, ipv six, amd sixty-four and sha two hundred fifty-six, where that rule left runs so short to be spelled; MP3, H2O, CO2 and A4 are spelled as before. www.example.org is w w w, dot, example, dot, org, with org said as a word however many dots come before it; john.doe@example.org is john, dot, D O E, at, example, dot, org; and /etc/nixos is slash, etc as a word, slash, nixos. A full stop after an abbreviation, before a word that can begin a sentence, ends the sentence where 6.1 decides it does: "5 p.m. Then leave.", "the U.S. The trip" and "i.e. It runs". That is the twenty-sixth deliberate divergence, and the second that is 6.1's rather than ours.

What came over. `normalize_text` and `letter_sequence`, which decide what a token is and whether a run of letters is spelled, and eleven more that 6.1 changed or added with them -- `slash`, `slash_before_measure`, `three_letter_extension`, `tok_dict_entry`, `abbreviation`, `init_ptr_End_reproc_string`, `is_pathname`, `process_pathname`, `URL`, `email_address` and `process_hostname` -- lifted the same way and written at the end of each English's `ut_norm.up`. Six variables 4.3 never declared are at the end of each one's globals, three of them constants that `set_global_constants` now sets in its `u_vars.dr`, and one, whether a slash was read as per, is read only by rules of 6.1's not yet taken. One lookup set 4.3 never had, the three-letter domain names, is the 512th in `lang/enus/enus.sets` and the 520th in `lang/engb/engb.sets`. `eng_tok_lookup` is gone from both, and the call to it in each `ut_norm.dr` goes to IBM's `tok_lookup` again.

Two pieces of 6.1 were not taken, and both are about annotations. 6.1's annotation reader does not know the annotations IBM added for SSML -- `card`, `ord`, `tel`, `cur`, `time` and the rest, which `<say-as>` turns into -- and reads them aloud as text, and in this module it hung on a phonetic annotation. So the Englishes keep IBM's reader, and `normalize_text` calls IBM's `backquote` for phonetic input just where 4.3's did, which 6.1 had folded into its own reader. And 6.1's rules mark a word boundary they lay down for themselves with a backslash and an exclamation mark, which only 6.1's reader takes, so the three strings that do it write the same boundary with the backquote, and a space after it keeps the length the rule states.

A lookup set is named by its place in the language's list, and 6.1's list is not 4.3's. The punctuation rules lifted earlier carried 6.1's numbers across as they stood, so `end_of_sentence` looked for a word that can begin a sentence in 4.3's list of compounds of "baby", which is why "5 p.m. Then leave." read differently from 6.1 until now. `tools/rules/lift64.py` puts every set's number through a map by name, and where paths that pick different sets meet at one lookup it writes a lookup for each.

What still differs from 6.1, in the cases tried, is other rules of 6.1's not yet taken: AUD$5 is five Australian dollars in 6.1 and A U D five dollars here, USD 50 is U S D fifty in 6.1 and United States dollars fifty here, C# is C pound in 6.1, a phone number's nought is oh, and C:\ is C colon. `test/cases/eti-norm.txt` holds 74 cases that read as 6.1 reads them, which `test/harness/eti.sh` runs beside the punctuation's. British English differs from 6.1's British engine in three more places, and none of them is the normaliser's or moved with it: after a comma 6.1 says a strong "and" where IBM's British reduces it, at the end of an email address it pauses one word earlier, and it reads ":)" as "smiley face". `eti-punct-engb.txt` and `eti-norm-engb.txt` hold the 160 and 72 cases that its British engine and ours read alike.

It moved 21 of the 1,073 recorded cases, all English: the four plain sentences with a web address, naiveté, and words typed against digits and brackets in them, in each of the four categories that speak them, and five of the raw UTF-8 cases, whose bytes above 127 go through 6.1's `letter_sequence` now, so café, spelled out before, is a word as it is in 6.1. Not one of the 24,317 words of English's list moved, the other nine languages are as they were, and so are IBM's say-as and phonetic annotations. British English took both the punctuation and the normaliser the same night and moved 22 of its cases: the same four plain sentences in each of the four categories, and all six of its raw UTF-8 cases. Not one of the 20,000 words of its list moved. Every rule taken from 6.1 says `instead`, which `docs/rules.md` explains: the upper-form check builds IBM's side with it as well, since nothing can hold it against IBM's.

## Text that stops without punctuation does not pause like a sentence

**IBM's engine pauses for as long as at a full stop wherever it is made to finish a stretch of text, and ours shortens that pause where the text there does not end in punctuation.** It is made to finish one at the end of every utterance and at every change of voice, speed, pitch, inflection, volume or language, so a screen reader saying "Desktop list" and then "Recycle bin" put 392 ms between them at the engine's default speed and about 140 ms at the speed NVDA's default rate gives, and a pitch raised for one word in the middle of a sentence put a 395 ms pause before it and another after. "Hi" spelled the way NVDA spells it, with the capital at a raised pitch, took 1.5 seconds with a 421 ms pause after the H, and takes 0.7. Issue 18 asked for it. That is the twenty-seventh deliberate divergence.

It is `eciPauseMode` (33), and the default is one. The IBMTTS and Eloquence 64 drivers have always done this from outside, by writing `` `p1 `` into their text at the end and, if asked, before every mark, and the three values are theirs: nought shortens nothing, one the end of text only, two every mark as well. The engine writes the same annotation itself now, in `stw_processRemaining` in `src/eci/synth/eci_synthwork.c`, after the romanizer has escaped the caller's own backquotes, so it acts whatever `eciInputType` says and wherever the engine finishes rather than only where the caller stopped. That second part is what a driver cannot do: a change of pitch at the very end of the text has already made the engine finish, pause and all, by the time the driver's annotation arrives, and NVDA ends every spelled capital with exactly that change. `test/lib/pauses.py` holds every mode to the drivers' annotation written in by hand, sample for sample.

What the text ends with is read a word at a time from the back. A mark, or a mark before a closing bracket or quote, leaves the pause alone, as the drivers do. So does a pause annotation the caller wrote there -- `` `p300 ``, or the `` `p0 `` Eloquence 64 writes when its own setting says never. An annotation that only sets something is passed over, and one that is spoken counts as words: a phonetic spelling, and what the SSML reader makes of a number or an ordinal, carry what is said in brackets with the sentence's full stop straight after them, which the first version of this read as nothing at all and so shortened the pause after a finished sentence. Japanese is untouched in every mode, because its romanizer ends every sentence it hands the engine with a full stop of its own.

The cost is any pause a caller got from a change of voice and meant to keep: two speakers in two voices with nothing between them now run together. Nought gives back exactly what IBM's engine did, and the NVDA add-on offers all three as "Shorten pauses". A driver's own "never" cannot reach this setting, since neither driver knows it is there: Eloquence 64's still means never, by way of the `` `p0 `` above, but IBMTTS's "Do not shorten" writes nothing, so on this library its users get the end of text shortened regardless.

It moved 254 of the 1,073 recorded cases, and how each moved was checked rather than assumed. With nought compiled in as the default, all six builds gave every one of the 1,073 answers as before. With the default, 83 cases -- every sentence that stops on a web address, a trailing annotation or an unclosed mark, in each language and category -- are the old samples cut short, identical up to where they end. 162 have a change of voice, speed, pitch, inflection or volume in the middle, and each is exactly what the engine with nought gives when `` `p1 `` is written in by hand before the changes that follow words, and at the end. The other nine are the `second` category's case 3 in each language: its first utterance is cut at the end like the rest, and its second differs throughout because the machine's state carries on from a different ending. Not one of the 184,577 words of the nine word lists moved: a transcription from `eciGeneratePhonemes` is left without the annotation, since it says what the text is made of and the pause is the sound's, and the first version, which wrote it in there too, moved every one of them. `test/compare.sh` and `test/harness/phonemes.sh` set the mode to nought whenever ours is held to IBM's, so the oracle compares like with like.

## If it sounds wrong

It is not a fault in the port. The audio is identical to IBM's by design, over the recorded cases in ten languages and every build the tree makes, apart from the cases a deliberate divergence on this page names and the wideband voice, which IBM never made. That is Eloquence sounding like Eloquence.

Changing it is a deliberate change to the language data, and the gate will correctly report that as a difference.

## Making and deleting many instances

**An instance used to leave a small block behind and the engine got slower the longer a program ran.** Ours, fixed on 21 September 2026, and worth knowing because of how it showed rather than what it was. `evv_task_start` allocates a block to name a task, nothing joins these threads and `evv_task_stop` has nothing to stop, so the block was held for the life of the process -- two or three of them for every instance made and deleted.

The bytes were nothing. What cost was the count: every allocation walks the whole region, used blocks included. A program that spoke a thousand words through `eciGeneratePhonemes`, one instance a word, took 23 seconds, and fourteen hundred took 61 -- which looks exactly like a hang rather than like a leak. With the handle given back when its task ends, fourteen hundred take 17 seconds and four thousand take 45, at about eleven milliseconds a word throughout.

So the handle `evv_task_start` answers is good only while its task is running. Nothing here keeps one past that, and the priority calls take a block of IBM's and ignore the task in it.

`test/harness/phonemes.c` will say what the region is still holding every so many words when `EVV_ARENA_REPORT` names a number, which is what found this. A leak is the group whose count climbs line after line; the blocks a process rightly keeps for its own life sit there unchanged.

## Handing the engine UTF-8

**Text is bytes in the language's own code set, which is the Windows Western set, and a word list in UTF-8 hangs Spanish outright.** IBM's, and `docs/api.md` has said the first half of it all along: the engine reads single bytes and does almost nothing between the caller's bytes and the machine's characters. What was not known is that it does not merely mispronounce them. The two bytes of a UTF-8 `ó`, read as two Windows Western characters, hang `apply_span_c_rules` between a `cc` and an `n` -- the Spanish for abduction is line 42 of `test/cases/words-eses.txt` and it never comes back.

So a word list is written in that set rather than in UTF-8, and `tools/measure/wordlist.py` does. Polish is the exception in the other direction: it declares characters of its own and its text is converted from UTF-8 on the way in, which `docs/quirks.md` lists among the deliberate divergences.

**And a shell that reads such a file wants `LC_ALL=C`.** GNU grep in a UTF-8 locale quietly drops a line whose bytes are not valid there, which is every accented word. `test/words.sh` lost three thousand Spanish words and six thousand French ones to that before the counts gave it away: it said twenty thousand words written and meant seventeen.
