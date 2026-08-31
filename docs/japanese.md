# Japanese, and what is left of it

Eight of the nine languages in the SDK build, speak and match IBM byte for byte. Japanese is the ninth. What stands between it and the other eight is the romanisation module -- what `jpnrom.dll` is in stock Eloquence -- and that is now a measured piece of work with an oracle in front of it rather than an unknown.

This is written for somebody who is not the person who found it. Everything here was measured rather than assumed, and where something cost hours to learn it says so, because the same hour is easy to spend twice.

## What is settled

**Everything below the romanizer is right.** That was not known until 27 August 2026 and it is the thing the rest of the work stands on. Japanese text goes through the romanizer, comes out as a phoneme string with prosody annotations in it, and from there the engine that speaks the other eight languages speaks it. `test/romcan.sh` proves that half: for each case it runs IBM's engine with the romanizer seam recorded, then runs ours with those recorded answers replayed in place of a romanizer, and the samples come out identical. Seven Japanese cases, kana and kanji and katakana and numbers and embedded English and romaji, all identical, and the conversation with the romanizer identical call for call.

So Japanese is now a text-to-text problem with an exact oracle. For any input, the bytes IBM's romanizer produces are known, and anything that produces the same bytes is right.

**The seam is ours.** `src/eci_romanizer.c` used to say that finding a romanizer was Win32 `LoadLibrary` work and stub it out. That was wrong: IBM's `getRomanizerInst` takes the address of `getRomObject`, a link-time symbol that `romedll_link.obj` answers when the romanizer is part of the program, and the only Win32 in it is `GetModuleFileNameA` asked for the directory the program was loaded from. It is transcribed now. `src/eci_rom.h` says what a romanizer is -- one struct of named functions where IBM had numbered vtable slots -- and `src/eci_romedll.c` stands in for the linker's answer. A romanizer is a property of its language, so an English build carries none of it.

**The data is lifted.** Two commands, no format understood:

    python3 tools/lift-rom.py analysis/jajp lang/jajp
    python3 tools/lift-romtables.py analysis/jajp lang/jajp

The first is the static dictionary: 1,723 blobs, 2,669,092 bytes, and seven pointer arrays whose lengths match the symbol counts exactly. The second takes the five objects that carry tables -- `dictman.obj`, `unicodeconvt.obj` and `jpnutil.obj`, whose own data section holds the romaji every kana is spelled with, and the one table apiece of `userdict.obj` and `phrasebuf.obj` -- and answers 115 tables, 193,858 bytes. It writes a header beside the C declaring what is in it, so a table cannot be declared one way and defined another. Each object comes out as one block with a pointer per table rather than an array each, and that matters -- see the note on the lead-byte tables below.

**Five files are written and proved.** `rom/jajp/rominstparam.c` is the parameter block and the errors, held to IBM's own behaviour by `EVV_ROMCAN_PARAMS=real test/romcan.sh`, which hands the parameter half of the recorded conversation to it and fails if a single answer differs. `rom/jajp/rominstance.c` is the instance the manager holds and the forwarding it does. The other three are held to IBM's by `test/romprims.sh`, which sweeps every input there is and answers 231,327 identical calls a side: `unicodeconvt.c` is the codeset conversion, `dictman.c` is the twenty-six accessors over the sixty thousand bytes of table its object holds, and `jpnutil.c` is the thirty-two small things everything else asks -- what a byte is, what a two-byte character is, kana into letters, a voicing mark, hiragana into katakana.

## Where it stands

`lang/jajp` lifts in one pass from the tools: 477 rules, its statement and field tables, its settings, language 0x80000, and `make missing` answers nothing. Its rules, globals, lookup sets and settings are all already right.

It is deliberately **not** in the tree, and `.gitignore` says so. A language module that cannot make an instance would fail any build that named it, including the CI step that builds and speaks every module in `lang/`. Lift it when you start:

    python3 tools/gen-globals.py analysis/jajp/glob.obj lang/jajp/delta_globals_jajp.c
    python3 tools/delta-link.py jajp
    python3 tools/delta-sets.py jajp
    python3 tools/lift-ini.py jajp
    python3 tools/delta-emit.py analysis/jajp lang/jajp jajp
    python3 tools/gen-lang.py jajp lang/jajp
    python3 tools/lang-codepoints.py jajp
    python3 tools/lift-rom.py analysis/jajp lang/jajp
    python3 tools/lift-romtables.py analysis/jajp lang/jajp

`rom/jajp/jprom.h` defines `JPROM_INCOMPLETE` while the romanizer cannot yet convert anything, and what that does is make `jp_rom_new` answer no instance at all -- so the engine behaves exactly as it did before there was a romanizer to find, and refuses the instance rather than speaking something wrong. Take it out when `Romanizer` is finished and not before. Nothing in `test/romcan.sh` depends on it either way: that registers its own romanizer over whatever is linked.

## The two harnesses

`reference/romtap.c` is wrappers in front of the eight public methods of IBM's `RomanizerManager`, which is the seam. It writes down every call and every answer, in hex, and writes nothing unless `EVV_ROMTAP` names a file, so the tapped binary stands in for the plain one -- and it does: the samples are identical with the variable unset, which is the first thing to check before believing a dump.

    make -C reference TAG=jajp BUILD=../build/reference-jajp romtap

`test/romcan.c` reads such a dump back and answers from it. It is a romanizer with no Japanese in it. Every call that arrives is compared against the recording -- the text, the lengths, the flags, the parameter numbers -- and a difference is a failure, because a manager asking different questions would make the answers meaningless. What it cannot see directly is the parameter reads: the manager reads a parameter before it writes one and flushes what the romanizer is holding when the two differ, so the answers `getParam` has to give are read off the recording's own flushes. `EVV_ROMCAN_PARAMS=real` stops guessing and hands that half to `rom/jajp/rominstparam.c` instead, which is how that file is proved.

    make romcan LANGS=lang/jajp
    EVV_LANG=jajp test/romcan.sh
    EVV_LANG=jajp EVV_ROMCAN_PARAMS=real test/romcan.sh

`test/romprims.c` is the other kind: a class the romanizer reaches for itself is never called on the seam at all, so it is called directly instead, on both sides, from one file compiled twice. `test/romprims.sh` diffs the two. This is the same arrangement `test/prims.c` uses for the machine's primitives.

Both harnesses found real faults on the first run, which is the argument for building them before writing the romanizer rather than after. They are in the next section.

## What the harnesses found

**The sample rate never reached the romanizer.** `src/eci_managers.c` answers for the concatenative engine, which this extraction does not have, and `getActiveSampleRate` was a stub answering nought. The romanizer is the only thing in the engine that ever asks, so no language could see it: eight languages match IBM byte for byte with that stub in place. The four numbers the synthesis thread hands that manager and reads back out of it are remembered now, at IBM's own offsets inside the block the thread already allocates.

**A byte of 0x80, 0xfe or 0xff hangs IBM's converter.** Those reach the end of `MBCSToUCS2`'s chain of tests without its walk advancing over them, so it loops on the same byte for ever and the synthesis thread never comes back. Ours drops the byte. There are no samples of IBM's to differ from, because it produces none.

**A pair beginning 0xfd is looked up past the end of its table.** `m_pLeadByteTable2` holds 29 lead bytes, 0xe0 to 0xfc, and IBM's own bound test lets 0xfd through it as well -- so for any such pair it answers with whatever the linker put after that table. Ours refuses the pair. 0xfd is not a Shift-JIS lead byte, and an answer that changes when the link changes is not an answer to reproduce. `test/romprims.sh` is what found this: it was the only difference in 142,802 calls.

**`Hiragana2Katakana` hangs on hiragana small wa.** It scans its table for a match and does not advance the input when it fails; the table holds 82 hiragana and `IsHiragana` accepts 83, and the one it accepts that the table has not got is 0x82ec. So any text with that character in it makes IBM's walk the same character for ever. That is measured rather than reasoned about: with 0x82ec in the sweep, IBM's side does not finish in sixty seconds and ours finishes at once. Ours passes the character through. The same scan's bound is the table's length in bytes rather than in entries, six times too many, which only stays inside the table because a match always comes first.

That last one is also why the lifted tables are one block per object with a pointer per table. IBM's code does not always stay inside the table it started in -- the lead-byte tables are shorter than the range of lead bytes accepted, and a packed record can run on past its own table's end -- and laid out that way, whatever such a read finds is what IBM's found.

## The oracle, and why it can be trusted

There was no reference for Japanese until 23 August 2026, because one would not link: IBM's Japanese object set is missing three names. Where each one came from matters more than that they are now supplied.

`ralStrNicmp` is in `src/port_ral.c` beside `ralStrIcmp`, which already had the same signature -- a length first, nought meaning the whole string -- and is called the same way, comparing a phone name against a table of five-byte entries. The runtime abstraction layer has always been ours on both sides of every comparison this project makes, so that is the boundary the reference already stood on rather than a new one.

`getFullPathName` and `__chkstk` are in `reference/jajp_shim.c`, linked for that one module. They cannot go in the shared layer: every other module defines `getFullPathName` itself in `libmain.obj` and collides, and a weak alias does not resolve in PE the way it would in ELF, which is why the shim is a separate object chosen by `TAG` rather than something cleverer.

**`getFullPathName` must answer an empty string, not nought.** IBM's own is one line: it returns a global that `DllMain` fills in, and that global is a 260-byte buffer in the bss, so in a static build with no `DllMain` it answers a pointer to an empty string. Answering nought instead changes nothing observable today, and is exactly the kind of difference that makes an oracle worth less than no oracle. It was written wrong first and found by reading IBM's version rather than by reasoning about what could need a path.

Build it with:

    make -C reference TAG=jajp BUILD=../build/reference-jajp

## The target

IBM's Japanese engine does speak Japanese script, and **how the instance is made decides whether it does**:

    romaji,         eciNew()             18,293 samples
    shift-jis kana, eciNew()                  0 samples
    shift-jis kana, eciNewEx(0x80000)    13,266 samples
    ucs-2 kana,     eciNewEx(0x80800)    13,266 samples

`eciNew()` is not the same as `eciNewEx` with the only language the module has. That is why `reference/speak.c` produced nothing for Japanese and read like an engine that cannot do it; it reads `EVV_LANGUAGE` now, the way `cli/probe.c` already did, so both sides make the instance the same way. Setting the codeset parameter afterwards is refused; the language handed over at creation is what carries it, in bits eight to fifteen -- which is what `isUnicodeCodeSet` tests against 0x800.

    make -C reference TAG=jajp BUILD=../build/reference-jajp jptry

`reference/jptry.c` is that driver, kept rather than thrown away, with the table in its head.

Two harness mistakes cost most of an afternoon there and both are ones this project has made and written down before. The output-**filename** path is not the one that works: the engine wants a callback and a sample buffer, as `speak.c` uses. And `eciSynchronize` does not wait in this engine, so an instance gets deleted while the synthesis thread is still in it -- pump with `eciSpeaking` and a sleep. Either one looks like the engine failing on Japanese when it is the harness failing on everything.

## What the romanizer produces

For `こんにちは` in Shift-JIS, the string handed to the engine is

    ` `vv692 `ui `i2 `g6_koNnitSiwa'.

That is the engine's own phoneme notation with prosody annotations around it, not romaji for the rules to read. So `MakeReadableJP` and the ESPR writer are producing the readable form directly, and the analysis chain in front of them is what decides the readings and the accents. Any transcription is right when it produces that string.

## What is left

The Japanese-only object set is 116 objects. Sixteen are the Delta language data, which the ordinary lifters take. Forty-nine are the static dictionary, which `tools/lift-rom.py` takes. Sixteen are the prosody chain, of which thirteen are empty -- everything inlined away -- leaving `PCWriteESPR2` at 5,834 bytes, `PCRoman2BG` at 2,724 and `PCProsCtrl` at 308 over 1,589 bytes of table.

The remaining thirty-five are the romanizer proper: about 168,000 bytes of x86 and 198,000 of data, of which the data is the three objects already lifted -- `dictman`, `unicodeconvt` and `jpnutil`, whose own data section holds the romaji spellings. Fifteen of those objects are written whole: `rominstparam`, `unicodeconvt`, `dictman`, `jpnutil`, `codeconv`, `annotation`, `userdict`, `inputchar`, `inputmngr`, `convtinterface`, `phrasebuf`, and the five of DictSearch's seven that hold nothing else -- `dictapi`, `fdictapi`, `kanastr`, `engread` and `numanal`. The other two are written too but hold a method apiece of `TextAnalysis` and of `PhraseTable`, which are not. What is left is roughly twenty thousand lines of C, judged from the four to eight bytes of x86 per line of ours that three already-ported objects came out at.

It is a Japanese morphological analyser, not a lookup table. The classes, with how many methods each has:

    DictSearch 64 (done)              TextAnalysis 36
    JpnUtil 37 (done)                 MakeReadableJP 30
    DictMan 26 (done)                 ConverterInterface 21 (done)
    InputChar 21 (done)               IntonPhrase 17
    Romanizer 16                      PhraseTable 16
    NumRead 11                        JPath 11
    InputManager 10 (done)            RomUserDict 9 (done)
    PhraseBuf 9 (done)                TextNormalizer 4

The two counts that have moved since the first census are IBM's, not a
recount of ours. `JpnUtil` has thirty-seven methods rather than thirty-two:
five of them are in `codeconv.obj` rather than in its own object.
`ConverterInterface` has twenty-one that exist rather than twenty-four
declared, the other three being pure in it and Romanizer's to supply.

and the objects they sit in, with their code sizes:

    dictsearch       11484   dictapi          10674   txtanal          12429
    phrasetable      16974   numread          13369   intonphrase      10121
    MakeReadableJP   10087   jpath             9401   jpnrom            9633
    inputchar         9243   unknown           5569   fdictapi          5024
    engread           3897   phrasebuf         3890   userdict          3916
    jpnutil           3920   kanastr           3728   numanal           3133
    convtinterface    2460   PCRoman2BG        2724   kakutei           2186
    TextNormalizer    2146   comppenalty       1776   inputmngr         1674
    dictman            629   romreg            180    romedll_link      191
    MakeReadableJP_SPR 1463  MakeReadableLangInt 161  codeconv         2794
    annotation        1446   PCWriteESPR2      5834   PCProsCtrl        308

Note `codeconv.obj`, `annotation.obj` and the `PC` family: those are not in the romanizer's own set but the romanizer wants names from them, and an earlier count of this work missed them.

## The boundary

The thirty-five objects want only 85 names from outside themselves, and most are libc or things this port already has: `Mutex`, `ETIqueue`, `ETIThread::sleep`, `DynaBuf`, `fileFindInPath`, `RequestLicense`, `IniFileReader`, `ralStrNicmp`. Six are not written yet:

- the skiplist that holds the user dictionary -- `win_skipstore` 4,228 bytes, `win_key` 901, `win_translation` 1,783, `win_listnode` 437
- the `Annotation` class, `annotation.obj`, 1,446 bytes
- `ProsCtrl::GenerateESPR` and the ESPR writer behind it, about 6,100 bytes
- `IniFileWriter`, `win_iniwrite`, 4,112 bytes
- `getFullRomPathName`, twenty bytes in `libmain.obj`, which has the same trap in it as `getFullPathName` above

`JpnUtil::euc2shift` and `seven2shift` used to be on that list and are not any more: `codeconv.obj` is `rom/jajp/codeconv.c` now, all five of its functions.

The skiplist chain is written and proved: `src/eci_key.c`, `eci_translation.c`, `eci_listnode.c`, `eci_skiplistnode.c`, `eci_arraylistnode.c` and `eci_skipstore.c`, held to IBM's own objects by `test/romprims.sh` over insert, search, multiSearch, remove, a full walk and a save-and-load round trip.

Two things about that store are worth knowing before reading it. Its constructor calls `srand(time(0))`, so the tower over the entries differs between two runs and **a saved file is not the same file twice** -- which is why the sweep compares what the list answers rather than what it writes, and why a round trip is checked by walking the loaded list. Nothing else in this engine uses `rand`, so the seeding disturbs nothing. And the load path turns file indices back into pointers only after every node exists, because it cannot do it sooner.

`RomUserDict` is written: eleven methods in `rom/jajp/userdict.c`, the whole class. It was blocked on `DictSearch::GetYoonIndex`, `SetLongWord` and `ConvertYoonDict`, and writing those unblocked it.

What it does. A caller gives it a word as it is written and a reading in kana with a caret where the accent falls. `makeKey` normalises the written form -- half-width kana, letters, digits and the two commas all become the two-byte forms the built-in dictionary is written in, and a following voicing mark is folded into the character it marks -- so that a user word is looked up by the same key shape as a built-in one. `makeTransValue` copies the reading out and counts which mora the caret marked, skipping the small kana that join the sound before them, and stepping the accent back off a doubled consonant or a lengthened vowel, neither of which can carry one. `transKatakana2Yomi` then spells that reading as the engine's own yomi codes -- it is `DictSearch::ProcessKatakana`'s inner walk done again over a string, and it has to stay that way or the path search would weigh a taught word differently from a found one. The record goes into the same skip list the English dictionary uses, keyed by the normalised form.

And on the way back: `lookup` hands the whole of what is left of the sentence to `SkipList::multiSearch`, which answers with what matched each prefix of it at once, so a one-character word and a five-character one starting in the same place both come back in one call. Each becomes a candidate entry through `writeData`, written at IBM's own offsets so that it is indistinguishable from what the built-in dictionary produced.

Two things the sweep settled that reading alone had not. The two longs at the end of `DictSearch` are a mode and a pointer: when the mode is one, a user entry is taken only if its first two bytes and its written form are the ones that pointer names. And that pointer is the last field of the record, which on a sixty-four bit host takes eight bytes where IBM had four -- so ours allocates a pointer's worth more than `TextAnalysis::initialize` asks for. `DS_ROOM` in `rom/jajp/dictsearch.h` is that, and the offsets stay IBM's.

## The phrase buffer

`rom/jajp/phrasebuf.c` is all nine methods of `PhraseBuf`, which is where the path search's answers become phrases. A phrase here is an accent phrase rather than a word -- a content word and whatever function words hang off it -- and one slot of the buffer holds the words that make it up, their readings, where the accent falls, how many moras it runs to and what kind of phrase it is. There are 686 slots of 344 bytes, which is one of `TextAnalysis`'s own three buffers to the byte, and `Copy` is what fills this one from one of those.

It was the right unit because it closes: everything it calls outside itself -- two of `DictMan`'s accessors and one method of `DictSearch` -- was already written. `JPath` was read at the same time and is not written yet, because `JrtJrtCheck` alone is a thousand lines and the two together would have been too much for one commit.

Three records came out of the reading and `rom/jajp/phrasebuf.h` and `rom/jajp/jpath.h` are the maps. A path is a count and up to twelve entry indices; a sub-word is one of `DictSearch`'s candidate entries copied out with the fields a phrase wants; and a phrase is eight bytes of head, up to eighteen words of eighteen bytes, and the function words after them at ten bytes each. `tools/rom-offsets.py` grew a case for each of the two objects and both tile exactly.

Two of those readings were wrong at first and the sweep found both. The cost of a phrase comes from the first word on the path and not the last, in both of the roads that write one -- read from the road that has a sub-word already in hand, the last is the obvious answer and it is not IBM's. And the test that refuses a one-word path reads how many characters the word covers, not how long its reading is; the two are adjacent bytes of the same record. Neither would have shown without fixtures where the two differ, which is the same lesson the sweep has taught before about varying a hand-built record's fields independently.

Sixty-one sabotages, fifty-seven of which move lines. The four that do not are these. Two are facts about IBM's own table rather than holes: no part of speech in it reaches the third of `IsBunsetsuEnd`'s tests with the bit that test looks for, over all 256 of them, and the phrase kind `GetSpecialPhraseType` works out is the one already there in every phrase the sweep reaches. One is a limit of the fixtures: the bound on how many moras a chain of function words may come to never fires before the bound on how many words it may have, because the function words in IBM's dictionary are short. And one is a deliberate consequence of what the harness can build: `SetPhraseBuffer` asks `DictSearch::FzkParsing` for the function words that may follow a phrase, and that method wants a parse state neither side can be handed by hand -- driven over a made-up one both engines walk off their own tables -- so the sweep tells the reader the text is used up and the road through it waits for `TextAnalysis`.

## The surface

`rom/jajp/convtinterface.c` is all twenty-one methods of `ConverterInterface`, `rom/jajp/inputmngr.c` all ten of `InputManager` with the three queue-element classes that belong to it, and `rom/jajp/codeconv.c` the five conversions of `JpnUtil` that IBM keeps in an object of its own. Together they are everything the engine asks a Japanese instance that is not the analysis itself.

What that is. Text arrives and is recoded into Shift-JIS if it did not arrive in it -- from EUC-JP or from any of the three seven-bit JIS codesets -- and then waits with the `InputManager` until something asks for it, because a caller hands over whatever it has rather than one sentence at a time. A mark or a parameter set part way through does not belong to the text as a whole but to a point in it, so those go on a queue with a note of how far into the output each belonged, and are written back out as the output passes that place. And the whole of the user dictionary passes through to `RomUserDict`, recoding the word and the reading on the way. So the ECI dictionary calls no longer answer refused for Japanese.

`ConverterInterface` is the base class of `Romanizer`, so the two are one object, and `rom/jajp/romanizer.h` says so now: the eight fields from 0x00 to 0x1f are the base's and are read out of `convtinterface.obj` rather than guessed at, and the rest of the record is the partial map it always was. The evidence that the head is complete is mechanical -- every displacement on a pointer anywhere in that object is one of 0x04 through 0x1c, plus 0x20 and 0x24 which occur only on the frame pointer, where they are arguments. Six of the eight are pointers and none of them can stay where IBM put it on a sixty-four bit host, so they are parked past the record as DictSearch's and InputChar's are.

Four things here are IBM's and are reproduced rather than corrected.

`InputManager::getText`, given new text when older text is still waiting, puts the new text in front and the waiting text behind it, so what was said first comes out last. It is only reached when a caller adds text twice without speaking in between.

`JpnUtil::han2zen`, walking EUC rather than Shift-JIS, loads the single shift as a signed byte and compares it against 0x8e as a number. A byte of 0x8e sign-extended is minus a hundred and fourteen, so the two are never equal and the whole of that arm is dead: a half-width kana coming out of EUC is widened but never has its voicing mark joined to it, which is the one thing the arm existed to do.

`JpnUtil::euc2shift` reads the second byte of a two-byte character without first asking whether there is one, so a text ending on a lead byte is read one byte past its end.

And `ConverterInterface::loadDict` looks the file up along the path, finds it, and then opens it by the name the caller gave rather than by the one that was found -- so a dictionary that is only on the path is located and then not opened.

A hundred sabotages of the three files were tried and eighty-one move lines. One was replaced because it did not change the predicate it aimed at -- setting a flag to two where only its truth is ever read -- and the eighteen that stay quiet are these, which is the point of listing them:

Three are memory rather than answers, and the arena guard sees them where the sweep cannot: the parameter text not freed, the recoded word not freed, and the old join buffer freed when it should have been kept.

Four want an allocation to fail, which nothing here can make happen: the manager's queue, the converter's manager, and the two roads through the Unicode converter's first use.

Eleven change nothing any road reads. The queue grows when it fills, so how deep it starts decides nothing. The text waiting is never read while its length is nought. The queue's own peek already answers nothing for an empty queue, so the manager's test in front of it is doubled. Whether the join buffer is kept or made again decides only whether an allocation happens; both roads write the same bytes, and so does a byte more allocated than anything reads. `findDictFile`'s answer is only ever tested against nought, so what size it reports does not matter. It answers minus one or a real size and never nought, so the difference between testing for nought and testing for negative does not arise. The parameter block refuses any codeset the conversion does not know, so a conversion that answers nothing cannot be reached through `addText`. The store's two failure answers cannot be reached through `loadDict` at all -- one wants the open to fail after it has already succeeded, the other an allocation. `RomUserDict` discards the reading's length, so what `updateDictExt` computes for it is never read. And the arm IBM's sign extension makes dead is dead: the sabotage that makes it live moves 819 lines, which is how a dead branch is shown to be dead rather than asserted to be.

One correction to something this file used to say. `RomInstParam::setInputType` is private and was described here as having no caller at all, which is why `getParam(0)` and `isAnnotationsInText` were said always to answer nought. It has exactly one caller and it is `ConverterInterface::addText`, so both answer whatever the last text handed over was said to be. The two fields still look like one and are not, which is what that note was for.

`IniFileWriter` is wanted only by `romreg.obj` and by English's `engreg.obj`, both registration rather than speech, and our arrangement retired registration -- there is no library to find, so there is no path to write into an ini file. Transcribing it would be a file with no caller in either half of the tree. Nine of its thirteen methods have been read and are in this session's notes; the four left are `writeToMemory`, `deleteKeyFromSection`, `deleteSection` and the rest of `writeString`.

## Decisions already taken

**Call our romanizer directly and retire the vtable slot offsets.** Agreed 23 August 2026, done 27 August. `src/eci_romanizer.c` reached a romanizer through IBM's numbered slots -- `ROM_ADD_TEXT` at 0x0c and the rest -- and those existed only because IBM loaded a DLL. They are named functions in one struct now. The alternative considered and rejected was building a C++-ABI-compatible vtable object so those offsets kept working.

**One registration struct rather than a link-time symbol.** IBM answers "is there a romanizer" with the presence of `getRomObject`. A binary of ours can hold several languages, so the question is answered by family and dialect in `src/eci_romedll.c`, at compile time for what is linked and at run time for what a caller registers over it -- which is how `test/romcan.c` stands a recording where the romanizer would be. A weak symbol would have been the obvious way to ask the linker instead and does not resolve in PE the way it would in ELF, and this engine is built both ways.

**The dictionary and the tables are data, not code.** Lifted verbatim, like every other language's.

**`rominstance.obj` is not transcribed.** Every one of its 31 methods forwards to a `RomInstParam` or a `Romanizer`, and with the vtable gone there is nothing left for it to do. `rom/jajp/rominstance.c` is that forwarding written once.

## The spine

`TextAnalysis` is the record everything else in the analyser reads. `Romanizer`
allocates one in a single lump of 946,216 bytes, and `DictSearch`, `InputChar`,
`JPath`, `PhraseBuf`, `Annotation` and `RomUserDict` all take a reference to it
in their constructors and read its fields directly. So not one of them can be
written -- or even constructed in a harness -- until the record is known.
`rom/jajp/txtanal.h` is that map and `tools/rom-offsets.py` is what keeps it
true.

The head is settled outright, because the constructor and `initialize` write
every field of it and nothing else does: a vtable, the romanizer that owns it,
the text as it arrives, and pointers to the six objects it makes -- an
`InputChar` of 10,168 bytes, an `Annotation` of 1,292, a `DictSearch` of 35,080,
a `JPath` of 31,980, a `PhraseBuf` of 235,996 and a `PhraseTable` of 20, with a
`TextNormalizer` of 20 at the very end.

The tail is settled by `InitPhraseTable`, which fills in a chain of two
sixteen-bit indices per entry and whose arithmetic says where that chain begins
and how long it is: 707 entries, the number `ClearPhraseTable` asks for. It is
the same shape `JpnUtil::TableFree` splices. Above it, `initialize` memsets
exactly 0x389d8 bytes, which is 707 times 0x148 to the byte -- the phrase table
proper, one row per chain entry.

The middle came from the arithmetic in `CheckPhraseLink`, which reaches a
candidate word as `this + 0x900 + buffer * 0x399d0 + slot * 0x158`. That is
three buffers of 686 slots of 344 bytes, and what says three rather than two or
four is that three of them reach exactly as far as the next named field. Nothing
indexes those buffers with a constant, so no sweep of the object can see them;
the arithmetic is the only evidence, and it is why the checker tests it.

**What the checker does.** It takes every offset `txtanal.obj` uses on a pointer
-- displacements and the immediates the compiler adds to form an inner base --
and refuses any that does not fall inside a region the header names. It does the
same across every other object in the module for offsets too large to belong to
anything else, since nothing else the analyser allocates is that wide. And it
holds the map's own arithmetic together: the regions have to tile the object
from nought to 946,216 with no gap and no overlap. Forty-three offsets, all
accounted for, and the tiling exact. Changing the buffer count from three to two
leaves a gap of 235,984 bytes; changing the phrase count by one leaves a gap of
328; growing the chain by one makes the phrase table overlap it.

`DictSearch` is mapped beside it, in `rom/jajp/dictsearch.h`, and held by the same checker. Its 35,080 bytes are mostly working store reached by arithmetic rather than by a constant, so nothing is claimed that its own code does not prove.

Most of it is one array: 710 candidate entries of 32 bytes at offset eight, which is where the words that might match a stretch of text are built. Two arguments agree on it -- `Do` clears 0x58c0 bytes from offset eight, and the loop after that writes a marker into a field at +0x1a of 710 entries of 32, and 710 times 32 is 0x58c0 to the byte. Above it are 726 function-word entries of 14 bytes, whose extent is the memset in `FzkParsingReverse` and whose stride and bound are in `LookupFuncWordDict`; three records of 16, which reach the count after them exactly; thirty readings of 20 bytes, from the memset in `GenerateKanaString` and the stride in `SearchTankanTable`; and four arrays with a slot per candidate -- a byte flag, how many characters, how many bytes, and whether it has been taken -- which reach exactly the count that bounds them.

Forty-six of `DictSearch`'s sixty-two methods are written, in `rom/jajp/dictsearch.c`, and they are not twenty scattered ones: they are the closure of `GenerateWord`, which is the whole of what it takes to turn one run of text into candidate readings and then into dictionary words. Nothing that closure calls is outside the file except `JpnUtil`, `DictMan` and `memset`, all of which were already written, which is why it was the right unit to take next.

What it does, end to end. `GenerateWord` copies the run of text starting at a position -- hiragana and katakana freely and at most one kanji, which is the ordinary Japanese word with its okurigana trailing off it -- and hands it to `GenerateKanaString`. That walk goes character by character. Katakana and hiragana it spells out itself, through `ProcessKatakana` and `ProcessHiragana` and the yomi table: a small kana after another kana makes one sound if `ConvertYoonDict` has that pair and two if it has not, a small tsu becomes the doubling code, a long-vowel bar doubles the vowel in front of it. A kanji goes to `LookupKanaDict`, which may answer with several readings at once, and every reading already being built is then copied once per answer so that the product of all the choices is present. What comes out is up to thirty candidate readings for the same stretch of text. Then, for each candidate the kanji dictionary did not itself produce, `SearchTankanTable` walks the single-kanji table and `GetDictEntry` walks the trie under it, writing every word whose reading is exactly that long into the candidate entry array -- which is what the path search above will choose between.

The second unit is the whole of `dictapi.obj`: the five dictionaries a stretch of text is looked up in, and the three writers that put what they find into the candidate array. They share a shape -- walk a hash to find where in the dictionary to start, walk a trie or a block of records from there, hand every match to a writer -- and what differs is the dictionary. The compound-word dictionary is a trie over whole characters whose hash is taken on *two* of them, which is what makes a dictionary that size searchable; the single-kanji one is another trie, keyed one character at a time, with every kanji put through the variant table first so a variant form finds its standard form's readings; the supplement and English dictionaries are flat blocks of self-delimiting records with an index over first characters.

Three things in it are worth writing down. The compound walk retries twice, and neither retry is obvious from the code alone: the hash lands on a block boundary, so a word may sit in the block *before* the one it points at -- hence stepping back while anything matched at all -- and where the walk went deeper than one character the block after is worth one try. Then, third, a run that found nothing at all is tried again with the variant table on. A word may not end where the character after it would join it, which is what `WriteData` refuses before writing anything: a small kana joins the sound before it, and a long bar after a katakana or another bar lengthens it. And the English walk lowercases as it goes and stops at a capital following a small letter, which is how a name written as one run comes apart into its words.

A correction that arrived with the third unit and applies to everything above it. `DictSearch` is spread over **seven** objects, not four: engread's four string-rule methods, numanal's eight number ones and phrasetable's copy of `IsOnin` were missed, and two of the sixty-four symbols the first four objects name are one method compiled twice as a COMDAT. So the class is sixty-two methods, the earlier counts of twenty and thirty-one and thirty-four were each one too many, and every closure computed before this was smaller than the truth -- `ProcessRomanAlphabet` reaches `EngRulesConvert` in engread, which the tool silently dropped because its filter for "outside these objects" excluded anything with `DictSearch` in the name. `tools/rom-offsets.py` was reading the same four objects while saying it checked the class; it reads all seven now. The map itself was never wrong, because none of the three missing objects touches a `DictSearch` field, and the tiling holds unchanged over all seven.

The sixth unit is `Annotation`, six methods and the first class outside `DictSearch` since the user dictionary. A caller may put marks in what it sends -- a pause, an index, a phoneme spelled out by hand -- and those are not Japanese and must not go through the analyser. `InputChar` lifts them out of the text as it reads and leaves them here, each with the position it belonged to; the output side asks for them back as it passes that position, so what is finally spoken has them where the caller put them. It is a ring of 128, and that is its only bound: a sentence with more annotations than that overwrites the oldest without saying so, which the sweep drives past on purpose so that IBM's answer is on record rather than an opinion.

A slip of IBM's is kept in two of the six. `Remove` clears the kind at the head of the ring, which is right where the head is the slot being given up; `Remove(after)` and `Flush` do the same while working on some other slot, and there it is wrong. Nothing has been seen to depend on it, and changing it would be a difference from IBM rather than a fix.

Four of that unit's twenty-two sabotages move no lines and none of them says the code is unreached. Two are pure rotations -- the ring's absolute origin is not observable, because everything that reads it reads it relative to the head -- one adds a byte to an allocation nothing reads, and one clears a field on a slot no later read can reach.

The seventh unit is the front half of `InputChar`: ten of its twenty methods -- the constructor and `Init`, both `SetText` overloads, `GetNextChar`, `IsAnnotationsInText`, the three that keep the SNLK chain, and `GetUnknownKanji`. The reading of a sentence, which is `ReadSentence` and its closure of thirteen methods and 2,168 lines, is the other half and is not here yet.

What the class is. Text arrives at `TextAnalysis` as bytes; `InputChar` splits it into characters and hands the rest of the analyser three parallel arrays of seven hundred and twenty-six -- the characters two bytes each, what each one is, and where each began in the caller's bytes -- with a fourth beside them saying what a candidate carries away. Nearly everything `DictSearch` and `JPath` do is an index into those, so `rom/jajp/inputchar.h` is now the record they are read from, and `dictsearch.h` and `txtanal.h` include it rather than keeping their own copies of the five offsets each had.

The record settled what the user-dictionary context is. `DictSearch`'s second mode makes an entry agree with a record it holds a pointer to, and that record had been read only as far as "a string at +4 and two bytes at +0x10". It is a `_SNLK_TABLE` -- a reading the caller gave for this very stretch of text -- and `Do` gets it by asking `GetSnlkTableAt` for the one at the character it has reached. So the two bytes are how many characters the written form has and how many yomi codes its reading is, and the string is the normalised key. That mattered for more than tidiness: the key is a pointer at IBM's fourth byte and could not stay there, and `dictsearch.c` and `userdict.c` had been reading eight bytes at offset four. Nothing had caught it because nothing yet builds that record for real; the harness was building one at IBM's offsets and both sides agreed on the wrong thing.

Three of `InputChar`'s own fields are pointers and every one of them collides once a pointer is eight bytes wide: the owner at nought would run over the first character, the text at 0x2788 over the word after it, and the chain head at 0x27ac over `IC_LENGTH`, which `DictSearch` reads. `IC_ROOM` parks all three past the record, as `DS_ROOM` does for `DictSearch`.

Two methods go up to `TextAnalysis`, on to the romanizer above it and down again -- for the parameter block, to ask whether annotations are in the text, and for the user dictionary, to turn a caller's reading into yomi codes. `Romanizer` is not transcribed, so what its record will be is still ours to choose: `RM_PARAM` and `RM_USERDICT` say which pointer slot rather than which byte, which on a thirty-two bit build is IBM's own +8 and +0x18 exactly.

The arrays are bigger than the constructor clears, and that is IBM's. Each of three memsets is handed 0x2d6 -- the count of characters -- where the size in bytes was wanted, so `IC_OFFSET` is cleared to the half and `IC_KIND` and `IC_MARK` to the quarter, and the rest is whatever the allocator left. `IC_KIND`'s fill is a byte for the same reason: it means to write twelve into each int32 and writes 0x0c0c0c0c. The sweep sees all of it only because it fills the block with a byte of its own before each construction; with the block zeroed first, five sabotages of those memsets moved nothing, since clearing more of something already clear is not a change.

`GetUnknownKanji` is the odd one. It walks a span of bytes collecting every double-byte character that is not one of seven punctuation marks, remembers where each began, and then lays the collection into the record **backwards** -- the last collected becomes character nought -- while leaving `IC_MARK` in the order it was written, so the two disagree by design. Every one gets kind four, hiragana, which none of them is: what the caller wants is a set of characters to look up, and four is what makes the walk that reads them treat every one alike. Its bound is one too generous -- `IC_SCRATCH` holds six hundred and ninety-four characters and the guard lets the six hundred and ninety-fifth through, which writes two bytes over the start of `IC_KIND` -- and no sentence the analyser accepts is that long, so it is kept.

Two slips of IBM's are kept in `AddSnlkTable`. A node whose reading `makeTransValue` refuses is leaked: the key and the value are freed on that road and the node is not. And the last of its four arguments is tested for not being negative and then never looked at again.

The sweep of it is 2,096 lines a side and brings `test/romprims.sh` to 404,123. The record is printed as runs of equal bytes after each of the two that clear it, over a block filled with a byte of the harness's own first; both `SetText` overloads run at every byte of every text, and `GetNextChar` beside them; the unknown-kanji walk runs over six spans of twenty-four texts from two starting characters, with every character it laid down printed with its kind, its offset and its mark; and the chain is built out of order, looked up at every position it could be at under three values of `IC_LENGTH`, walked from the head so that a node the lookup cannot reach is seen anyway, thrown away, and built again in order.

Thirty-nine of its forty-two sabotages move lines. Getting from thirty to thirty-nine was corpus work of the same kind as the dictionary unit's. Five memsets were invisible because the harness zeroed the block before constructing, so clearing more of something already clear is not a change; filling it with a byte of its own fixed all five. Two sabotages of the clamp on the yomi count were invisible because no reading in the sweep reached it -- `transKatakana2Yomi` stops itself at twenty-five and the only way past is a long-vowel bar after the twenty-fifth, which nothing had. Two more were behind a node the lookup could not reach, which is what walking the chain from the head is for. One wanted a written form whose normalised key has fewer characters than what the caller wrote, which needs a byte `makeKey` drops outright. And the seven punctuation marks the walk steps over were in none of the texts, and neither was a newline, so five texts were written for it.

Three stay quiet and none of them says the code is unreached. The scratch is cleared and then every entry read back from it was written first, so the clearing is unobservable by construction. The 0xff written into `SN_TRANS` before `makeTransValue` is the same: on the road where that call succeeds it is overwritten, and on the road where it fails the node is leaked without ever going on the chain. And the third is IBM's rather than ours -- `GetUnknownKanji` tests for a newline before testing whether the byte begins a double-byte character, and a newline is not a lead byte, so the first test can never change the answer.

The eleventh unit is the last four methods of `DictSearch`, and with them the class is whole: fifty-eight of sixty-two written before, and `FzkParsing`, `FzkParsingReverse`, `FzkSearchUnknown` and `HitFuncWordReverse` now.

They are the other half of the function-word search. A Japanese phrase ends in a run of particles and endings, and which may follow which is not free: the dictionary carries a vector per word saying what kind of thing can come next, and a run is a run only where every step agrees with the one before. The half already written walks that forward from a place a word was found. This half walks it from a character where nothing was found, which is what the analyser must do when it has to guess where a phrase ends.

The two `Parsing` methods are the same shape -- seed the vector, search once, then keep searching from the end of everything the last round found until a round finds nothing -- and each remembers in `FZ_MARK` the entry a find grew out of, so the chain can be walked back. `FzkParsing` goes forward through the ordinary dictionary and `FzkParsingReverse` backwards through the one carrying the phrase vectors, taking each new vector from the word just found. Nothing in the objects calls either of them: they are entry points for a class above that is not written yet.

`HitFuncWordReverse` is what writes a candidate, and the flag it leaves says whether the word may begin a phrase of its own. The dictionary's own bit says it may, and then the character after it is looked at, because none of the five that join the sound before them -- the small tsu, the three small y kana and n -- can start one. Those five are read out of the object's data rather than decoded from the names MSVC filed them under, which is the rule this project learnt the hard way and which held again here.

One thing the sweep taught before it agreed. Handed a made-up dictionary node, `HitFuncWordReverse` walks off the end of the table, and what lies past a lifted table is not what lies past IBM's own; sixty-four lines differed and none of them was a fault in the transcription. The sweep drives it from nodes the walk would really reach now, and the difference went away. **A sweep of a function that walks a data structure has to be given a real one; a plausible pointer is not a substitute, because the two sides only agree inside the table.**

The sweep of it brings `test/romprims.sh` to 578,726 calls a side. Thirty-five of its fifty sabotages move lines. Of the fifteen that do not, three are the bound on an array of seven hundred and twenty-six that no test approaches, four are index guards whose other arm still ends by answering nothing, three are the step that passes a node by -- which the shipped dictionary seems never to need, since the chain for a character is entered at the node that matches -- one is a `break` that saves iterations and cannot change an answer, and the rest are clears the harness had already done itself.

The tenth unit is `Do` itself, with one private method of `Romanizer` to close it. With it the analyser's search runs: text goes in as characters and comes out as every way the sentence can be read.

What it does. It walks the characters and, at each place a parse mark says a word may begin, asks every dictionary there is in turn -- the caller's own taught words, the built-in one loaded from a file, the function words, the single kanji, the ordinary words, and the English rules -- and lays what each answers into the candidate array. `JPath` afterwards picks a way through them. A character with no parse mark is passed over, and the marks are written as the walk goes by `CheckJrtTable` and `SetSuushiWord`, so the search is deciding as it goes which characters can still start a word.

The caller's own text can carry marks of its own, and the byte standing at a character's place becomes a mode. One means the caller gave a reading for this stretch, which is the SNLK chain `InputChar` keeps; two means leave the stretch alone. In mode one the candidates are filtered twice afterwards, first to those whose reading matches what was asked for and then, if more than one survives, to those the caller's own dictionary produced -- and where nothing at all survives, the reading itself becomes the candidate.

`Romanizer::GetParameter` is here because `Do` calls it: an annotation that names a setting takes effect at the character it stands before, which is the only place where the text position and the annotation position are both known. It reads the six shapes Eloquence allows -- an absolute value, a percentage of what is there, a step in words per minute or in hertz, the middle setting, and a voice number that resets the other four.

Three things IBM's are kept. `GetParameter` writes every relative setting twice, once clamped at the top and then again from the unclamped value, so the upper clamp has no effect at all and only the clamp at nought does anything. `Do` returns a local it never assigned when its loop runs no turns, which is what happens for a sentence that read as nothing; the sweep leaves that case out, because stack left over in one process cannot be held against another's. And the initialisation walk sets `DE_AT` rather than the field beside it.

That last one corrected the map. `dictsearch.h` had said of `DE_LINK` that it was "the field Do sets to minus one", which was a displacement read out of the disassembly without allowing for the candidate array starting eight bytes into the object. What `Do` sets is `DE_AT`, as a sentinel meaning no position; `DE_LINK` is still a field nothing read so far writes. The sweep found it as a difference in two bytes of every candidate.

And it corrected a second map, the same way the Romanizer one was corrected two units ago. `TextAnalysis` holds ten pointers to the objects below it, each four bytes from the next, so on a sixty-four bit host no two of them can stay where IBM has them: writing the annotation runs over the dictionary search, and the raw text over the normalizer. Nothing had noticed because the harness only ever set two or three at a time. Setting a third overwrote the second's upper half and the search followed a wild pointer on its first call. `TA_ROOM` in `rom/jajp/txtanal.h` parks all ten past the record, which is now the fourth class to need it -- and the pattern is plain enough to state as a rule: **a record IBM lays out four bytes to a pointer cannot be shared with a sixty-four bit build at all, and the only question is when it will be noticed.**

The sweep of it brings `test/romprims.sh` to 577,300 calls a side: every shape of voice annotation at four starting settings, and the whole search over twenty-six texts three ways -- with the caller's text carrying no marks of its own, saying a reading follows, and saying leave the stretch alone -- with a user dictionary taught six words first, the parameter block told annotations are in the text, and every candidate the search produced printed as bytes.

Thirty-eight of its sixty-three sabotages move lines. The twenty-five that do not fall into three kinds and none of them is a coverage gap that a longer corpus would obviously close.

Four are the dead upper clamps in `GetParameter`, and they are a confirmation rather than a gap: the second write is what makes the first unobservable, and that is the defect written down above.

Four are the road from an annotation to a setting. `Do` asks for the last annotation of type nought standing before the character, and nothing in the corpus produces one -- the annotations the reader lifts out of these texts are of other kinds. That road is walked in the engine and not in the sweep, and saying so is better than pretending otherwise.

The rest are predicates whose two arms agree on everything the corpus contains: a lookup that answers nothing either way, a guard on a state the texts never reach, a clear of entries the harness had already zeroed. Each was aimed twice.


The ninth unit is the number reader: eleven methods, which is everything in `Do`'s closure except `Do` itself. A run of digits is not a word any dictionary holds, so it is read by rule, and this is that rule.

A Japanese number is spoken by its places rather than by its digits -- ten, hundred, thousand, and then the four-digit steps man, oku and chou -- so what the reader has to work out is not which digits are there but which place words go with them, and whether what is written is a number at all. Four tables in the counter data say which characters are which: the kanji digits, the full-width digits, the small places and the large ones. `IsMember` is the search over one of them and the four `IsZ` methods are its four callers. What comes out is not kana but the numbers the reading rules take: nought to nine for digits, ten upwards for places, and one mark that says a place word was left out and must be spoken anyway.

`SetSuushiWord` is the walk. It goes forward one character at a time and keeps two counts: how far it has got, and how far it had got the last time what it held was a whole number. When a character says the run is not a number after all, the entry is written from the second pair -- so a text reading "three thousand and" gives back the three thousand and leaves the rest. `CheckKetaOrder` beside it is what decides that: a place word out of order means the run was never one number, and the counts go back. `IsCommaPosition` is the thousands-mark test, and it is stricter than it looks -- reading backwards, a mark must fall at every fourth place and nowhere else.

Two things about that walk are worth writing down because they cost time to find. A place word switches the reader into a mode that stops it two characters later, so a number with a place word early in it can never fill the buffer. And a run beginning with a kanji zero sends the place word down a different road entirely, which is what kept the buffer-full case out of reach until the generated texts started at a one.

Writing it corrected the map of a class that is not written at all. `DictSearch` reads two settings out of `Romanizer` -- whether an English word is spelled out letter by letter, and which number mode is in force -- and `InputChar` reaches through the same object for the parameter block and the user dictionary. The offsets those two want had been invented as pointer slots, on the reasoning that Romanizer was ours to lay out; they are not, and on a sixty-four bit host the invented slot for the user dictionary fell exactly on IBM's number mode. `rom/jajp/romanizer.h` is that record now, partial and saying so, with the size settled at 0x78 from what `RomInstance` allocates and the two pointers parked past it. The sweep found the collision within a minute of the first run.

The sweep of it brings `test/romprims.sh` to 576,024 calls a side: all 65,536 two-byte characters against each of the four tables and again through the closing-quote test, `IsMember` at every length from nothing to past the end of what it is given, the thousands-mark test over every arrangement of five codes drawn from a digit and the two marks and something that is neither, the place-order check over every level it takes and twenty-four runs built to walk each road, and the two writers over sixty-two texts built out of the tables themselves -- every counter after a one-digit number and after a two-digit one, every place word after each digit, and runs of exactly the length the buffer holds.

Fifty-six of its sixty-one sabotages move lines. Two of the five that do not are unobservable by construction: the reader looks at one character past the end of the text, which can match none of the four tables, and one guard inside the counter switch has its whole effect downstream disabled in the only mode that reaches it. The other three all aim at the same block -- the walk that cuts a full buffer back to its last large place word -- and none of them moves anything. The reasoning says that block can only ever assign what the run already has, because a place word early enough to shift the count also stops the run before the buffer fills; but that is a conjecture from reading, not something the harness has shown, and it is written here as one.

The eighth unit is the rest of `InputChar`: eleven methods, which is `ReadSentence` and everything only it calls. With it the class is whole, and so is the first stage of the analyser.

What the reader does. It walks the caller's bytes and fills the three arrays -- the characters, what each one is, and where each began -- one sentence at a time, stopping at the end of each so that the analyser can work on it and then asking for the next. The loop is written round the pair of bytes last taken rather than round the one about to be: every turn begins by asking whether what was taken last ends a sentence, which is what lets an arm end one by setting that pair and going round again. The space arm does exactly that, and it is the only way a plain space ends a sentence.

What ends one is a full stop, a question mark or an exclamation mark always; a full-width period or comma only where no run of digits and no run of letters is open; and an ideographic comma only where no run of digits is. Those two flags are what keep a decimal point from cutting a number in half and an abbreviation from cutting a sentence.

`GetCharType` is here, and it is the method every earlier unit's character tests were read off. It is now transcribed and swept over all 65,536 two-byte characters against IBM's, so the twelve kinds are settled by measurement rather than by reading. Two things about them came out of writing it. `KIND_DIGIT` covers a character that is none of the things the name suggests -- 0x815a, which is not a digit at all -- and `KIND_ENGWORD`, thirteen, is not something `GetCharType` can answer: `CheckContext` writes it, over a run of full-width letters that began after a space, which is how the analyser is told to spell that run out as English rather than read it as Japanese. `CheckContextForNum` also uses thirteen, as a value of its own that never reaches the array.

`IC_OFFSET` turned out not to be an offset into the buffer at all. It is an offset into the raw text `TextAnalysis` keeps, `IC_RAWPOS` is the cursor into that, and the two exist so that an annotation can be put back where the caller wrote it after the reader has folded, dropped and rewritten characters on the way through. That is why `RecoverOverflow` hands `IC_RAWPOS` to `Annotation::Remove` rather than a character index.

`RecoverOverflow` is what happens when sixty-two characters go by with no end in sight. It looks for four places to cut, each overriding the one before: any kanji a hiragana led into, the same where that hiragana was the topic particle, any punctuation that is neither a repeat mark nor a long-vowel bar and does not follow a digit, and the last closing bracket. Then, if the text the caller sent carries its own marks, the last mark of the first kind before here. Whatever is chosen gets an ideographic comma written over it, so that what the analyser sees is a break rather than a truncation.

One thing the reader does is worth stating plainly because nothing else in the engine does it: **it writes into the caller's buffer**. A full-width space that falls where a break belongs is overwritten, in the text as it was handed in, with an ideographic comma. The sweep prints the text back after every read for exactly that reason.

Three bounds sit within one of each other -- sixty-two, sixty-three and sixty-four -- and they are kept apart rather than folded into one constant, because nothing in the code says they are the same number.

The rule about mangled names earned its keep again. Two of this unit's string constants decode by hand to something other than what they are: the one that reads as 0x820c is the topic particle 0x82cd, and getting it wrong would have made the second of the four recovery walks look for a character that is not in Japanese text. `i686-w64-mingw32-objdump -s -j .rdata` on the object is what settled both.

The sweep of it brings `test/romprims.sh` to 491,281 calls a side. All 65,536 two-byte characters go through the classifier and through the kanji-numeral test; every half-width kana through the voicing fold with both marks; every single byte through the ASCII writer at the start of a sentence and inside one, and through both look-ahead questions; the context walk over fourteen kinds before it, both values of its flag, six things after it and the bound itself; runs of middle dots of every length; the reader over thirty-two texts four ways -- carrying state, starting afresh, against a raw text that carries its own marks, and with the buffer cut in the middle of a sentence and a second one handed over -- with every character it laid down, everything it left in the record, every annotation it lifted out, and the caller's own buffer printed back afterwards; and the recovery walk over eight records built by hand so that each of the four places it may cut is the one that decides.

Seventy-nine of its eighty-two sabotages move lines. The three that do not are each a different kind of quiet and none of them is a corpus hole. The carriage-return arm is redundant: without it the byte falls through to the arm that drops what it does not recognise, and no pair holding one ends a sentence. `CheckNextAnnotation`'s ideographic-space arm is unreachable, because the byte it tests as a lead is only ever set to a space or left at nought. And `GetCharType`'s guard against a negative index cannot be swept at all: the other road reads the two bytes in front of the character array, which are IBM's owner pointer on its side and nothing on ours, so the two processes cannot agree on what is there. That last one is worth naming as its own kind -- not a corpus hole and not dead code, but a road leading into memory the differential method has no way to compare.

Getting there took two passes over the harness, and both faults were the harness's rather than the code's. A flag was being read in the same `printf` as the call that sets it, so the compiler was free to read it first and it always printed nought; four sabotages were invisible behind that one line. And the annotations the reader lifts out were never printed at all, which hid both the position it saves them at and the cut the recovery walk makes in them.

The fifth unit is the fallbacks: nine methods of `dictsearch.obj` under `HandleError`, which is the answer to a question every dictionary leaves open -- what to do with a character none of them knew. Something has to be produced or the path search has no way through the sentence at all. A katakana run is analysed unless what has already been found is good enough; a full-width letter goes through the English rules just written; hiragana is spelled out; a long-vowel bar is a placeholder only where no kana came before it, since otherwise it belongs to that kana; anything else is a placeholder. Then the number counters, which are their own small dictionary and only consulted where the parse marks say a number ended here. Last of all the character itself is handed back where nothing readable came of any of it, which is what the caller says aloud instead of a reading.

`CheckJrtTable` beside it is what writes those parse marks: every candidate's end position is marked so the pass above knows the character is accounted for, and a candidate that ends past the end of the text, or that is itself a placeholder, sets none.

Two of that unit's twenty-seven sabotages stay below their threshold and both say something about IBM's data rather than about the code. The guard of 600 in `JoSuusiSearch` cannot be reached with the shipped counter dictionary -- the walk stops at the first record past the key, long before -- so it is a guard against a corrupt table and nothing else. And `HandleError` copies nine bytes of a counter's reading where the longest in the dictionary is eight, so the ninth is never anything but what was already there.

The fourth unit is `engread.obj`, and it is the one part of `DictSearch` that is not a dictionary at all. An English word written in Japanese text has no entry anywhere -- a Japanese dictionary cannot hold English -- so it is spelled out by rule instead: one table of substitutions turns the letters into romaji, a second turns the romaji into kana codes, and what falls out is a reading the rest of the analyser uses like any other. A rule is five parallel arrays with an entry each: what to match, what to put in its place, what to leave behind for the next pass to see, and two that say where the accent goes. Three characters in a rule are not literal -- `!` anchors the match to the end of the word, `@` matches any consonant and remembers which, and `*` marks where in the replacement the accent may fall.

Before any of that, the word is judged: no vowel in a word longer than three letters, no vowel at all in a short one, a capital in the middle of a word of four or fewer, or a word of one letter, and it is not treated as English at all but spelled out letter by letter instead. `y` counts as half a vowel -- enough for a short word, not for a long one.

The third unit is `fdictapi.obj`, and it is a different kind of lookup from the five above. A function word -- a particle, an ending, an auxiliary -- is not chosen on its spelling alone but on what it may attach to, so the dictionary carries a bit vector per word saying which kinds of phrase can precede it, and the search is handed the vector of what actually does. A word is taken only where the two agree. What comes out is not a candidate entry but a row of the function-word array, and `LookupFuncWordDict` is the pass that turns those into entries afterwards -- working out each word's part of speech by describing it in four bytes and finding the row of the phrase-type table that matches.

Two details in it are worth having. A long-vowel bar does not end a function word: the same trie node is asked again with the bar counted in, which is the only place in the analyser where a walk goes back over a node. And a word that says it begins a phrase does not begin one if the character after it is a doubled consonant, a small ya, yu or yo, or an n -- nothing in Japanese starts with those.

That file keeps IBM's layout rather than naming its own fields, which is a departure from the rest of the directory and a deliberate one. A class this size is half-written for a long while, and a half-written one has to be driven over state built by hand; sharing the layout means `test/romprims.c` can build that state the same way on both sides instead of maintaining two descriptions of the same bytes. It also avoids inventing names for the fields nobody has read yet.

Writing it settled four more regions and two record formats. The ten twelve-byte slots at 0x847c hold one kanji's readings before they are spread over the candidates, with a byte of characters and a byte of length beside each; the three arrays reach the word below them exactly, which is what says ten and not eight. The word at 0x8508 is the cursor into the entry array. And in the spine, 0x5f2 turned out to be thirty readings of twenty-six bytes with the count at 0x8fe -- a reading too long for the ten bytes an entry holds goes there and the entry keeps its number -- which is `SetLongWord` and `TextAnalysis::AddLongWord` between them, and thirty times twenty-six from 0x5f2 is 0x8fe to the byte.

The character classes are read off `InputChar::GetCharType`, which is the only place the numbering is stated, and getting them from anywhere else is how a katakana test gets read as a kanji one. One is katakana, four hiragana, eight the long-vowel bar, eleven the middle dot, and nine is kanji -- nine being the classifier's default, so anything it does not recognise arrives as a kanji, and so does an index before the start of the text. `rom/jajp/inputchar.h` names all twelve.

That sweep caught a real error on its first run, and the kind worth naming. `CheckCaseMarker` compares against a string stored under the mangled name `??_C@_02PGLDAILO@?$IC?p?$AA@`, and working that encoding out by hand gives 0x8270. The bytes in the object are 0x82f0, which is the particle *wo* rather than a full-width Q -- a different character and a different meaning for the method. Read the bytes; do not decode the name.

It caught a second on the closure's first run, and it is the same mistake in another dress. `CompareKanji` tests each character of the span against nine, and nine had been taken for the long-vowel bar; it is kanji. The method is what says a reading that matched belongs to *this* word and not another with the same sound -- every kanji in the span has to appear somewhere in the entry -- and read as the bar it meant nothing at all. Seventy-nine lines moved.

How the closure is swept. Everything below `GenerateWord` is called directly on both sides, over ranges rather than examples: all 256 codes through `IsOnin`, all 768 two-byte characters in the three lead bytes a small kana can have through `GetYoonIndex`, every row and every small kana and both flags through `ConvertYoonDict`, every page of both dictionaries and the offsets either side of each bound through `ReadGWDict`, and the long-reading store filled thirty-three times so that its refusal at thirty is swept too. `WriteKanaData` gets nodes built by hand with nought to eight readings, because no kanji in IBM's own dictionary has five on one node and the cap at five would otherwise go untested -- which the sabotage matrix is what noticed. Above that, the twelve texts are the seven the differential suite already speaks plus five written for the roads a sentence does not take, and `GenerateWord` runs at every position of every one of them, with each thirty-two byte entry it writes printed as bytes.

The character classes that sweep feeds in are the harness's own copy of `GetCharType`, written before `InputChar` was. It decides only what goes in, and both sides get the same thing, so a mistake there narrows the sweep and cannot hide a difference; the real classifier is swept over all 65,536 characters separately.

All twenty-two sabotages move lines -- one per method, and two for `WriteKanaData` because the first was below its threshold.

The dictionary unit is swept the same way and, with the function words and the English rules after it, brings the total to 402,027 calls a side: the variant table over all 65,536 values a two-byte character can have, the three writers over records built by hand with a word count of nought to three and a reading of every length a nibble can hold, and the five dictionaries at every position of nineteen texts, out of context and in it.

Getting from fourteen sabotages observed to twenty-one is the part worth recording, because none of the seven that did not move lines was a statement about the code. Five were dead because of the corpus: the English dictionary is keyed by full-width lowercase letters and every text was half-width ASCII, so that whole road was never walked; the supplement dictionary holds symbols and compounds beginning with a full-width digit, and no text had either; and the retry that turns the variant table on only matters where a variant kanji is present, which none was. Seven texts were added for exactly those. Two more were sabotages too weak to show -- a mask narrowed where no value in the shipped dictionary was wide enough to notice -- and became visible when aimed at the arithmetic rather than at the mask. One is left: shortening the English scan by its last block changes nothing, because the five words it finds are all in earlier ones. The block arithmetic itself is observed by a different sabotage.

`RomUserDict` is swept the same way and brings the total to 243,474: every one of the 256 bytes through `makeKey` on its own and again after a kana, so that the voicing marks and the half-width range are covered; a caret in every position of a reading, including ones that are not kana at all; every part of speech and two that are not; a candidate entry written at each end of the array and with the long-reading store both empty and full; and then a real dictionary taught eleven words, read back one at a time, and looked up against a sentence in both of `DictSearch`'s modes. All fifteen sabotages of it move lines.

Making the mode-one arm testable took a correction. It compares the stored record's first two bytes against what the context names, and those are how many characters the written form has and how long its reading is -- not the reading itself, which is what the harness fed it at first. With the wrong two bytes nothing ever matched, the arm was never entered, and its sabotage moved nothing. That is the same shape as `WriteKanaData`'s cap: a sabotage that changes nothing is a question about the harness first.

**A pointer at offset four cannot stay there.** The owner is at `DS_OWNER`, four bytes, and the candidate array begins at offset eight -- so on a sixty-four bit host the pointer's upper half and the first entry are the same bytes, and writing one candidate makes the owner unreadable. Nothing caught it until the dictionary unit, because it takes a method that writes an entry and then asks the owner for something, and until `WriteUserData` no method did both. Ours keeps the owner past the end of IBM's record now, at `DS_OWNER_AT`, and `test/romprims.c` sets it through a macro so each side writes its own place. IBM's offsets are untouched, which is what the map and the sweep depend on. Two other pointer fields in this record already lean on the four bytes behind them being unclaimed; this one had a claim.

**A ninth deliberate divergence, and IBM's fault.** `updateDictExt` lets the written form be thirty-two bytes, and every half-width kana in it becomes two, so the key it builds can reach sixty-four. IBM's buffer for that key is about thirty-six bytes of its own stack. Nineteen half-width kana is the last word that works; at twenty its frame goes and Wine's debugger comes up. Measured at exactly twenty, not reasoned about. Ours sizes the buffer for the bound the function itself enforces, so it answers instead of dying -- and that case is left out of the sweep, because there is nothing to compare against a side that has stopped running.

Those four arrays carry a caution for whoever transcribes `GenerateKanaString`: it clears each of them with a memset of thirty bytes, which is the whole of the first and half of each of the other three. That has to be reproduced rather than tidied. A slot past the fifteenth starts out holding whatever was there before, and the only thing keeping the reads inside what was cleared is the count.

About twelve hundred bytes are left over in two spans, each named as unresolved with its exact bounds so the map still tiles and says plainly what is not known. Twenty-nine offsets across its seven objects, all inside a named region; a count or a stride changed by one is caught as a gap or an overlap of exactly that much.

Two regions inside the map of the spine are named but not resolved: the parse's own marks
between 0x2c and 0x5d8, cleared at the top of `TextParsing` and read at several
widths, and a working area of 1,716 bytes that `CheckPhraseLink` takes the
address of. Both are bounded exactly; what is in them is for whoever writes
`TextParsing`.

## Where to go next

What is left, and in what order. The counts below are entry points as `nm` reports them across the whole directory, so they include constructors and destructors; the method counts in the census above are smaller for that reason and the two are not in disagreement.

**169 entry points, in ten classes.** `TextAnalysis` 37, `MakeReadableJP` 32, `Romanizer` 18 of which one is written, `PhraseTable` 17, `IntonPhrase` 17, `ProsCtrl` 16, `JPath` 12, `NumRead` 11, `TextNormalizer` 6, `MakeReadableLangInt` 3.

The order comes from a graph rather than from reading. Pooling what the unwritten classes call makes the remainder look inseparable; asking of each class which other *unwritten* class it calls gives six that call none at all -- `IntonPhrase`, `JPath`, `MakeReadableJP`, `NumRead`, `PhraseBuf` and `ProsCtrl`. `PhraseBuf` is written. So:

The five remaining leaves can be taken in any order and each closes on code already here. `JPath` is the smallest at twelve and its record is already mapped and checked, so only the code is left; `MakeReadableJP` is the largest at thirty-two and is the one that produces the phoneme string.

Then `TextNormalizer`, which wants `MakeReadableJP`.

Then `TextAnalysis` and `PhraseTable` **together**, because they are mutually recursive -- `TextAnalysis` calls `PhraseTable::initialize` and `SetPhraseTable`, and `PhraseTable` calls `TextAnalysis::CopyJrtPart`. That is 54 entry points in one unit and the largest single piece of work left. `TextAnalysis` is the spine: 946,216 bytes of record that every other class indexes into, which is why `tools/rom-offsets.py` had to map it before anything at all could be written. The map is true and checked, so the work is the code rather than the reading.

`Romanizer` is **last**, not next, which is the opposite of what this file used to say. It drives everything -- `processSentence`, `ResetBuffer`, `getOffset` and the two conversions between the caller's bytes and the readable form -- and `processSentence` reaches straight into `TextAnalysis`, `TextNormalizer`, `IntonPhrase` and `ProsCtrl`, so it cannot be finished until all four are.

Two objects on the list will not be transcribed at all. `romreg.obj` is registration, which this port retired, and `romedll_link.obj` is the link-time symbol `src/eci_romedll.c` already stands in for.

**Nothing is audible until nearly all of it exists**, and that is worth knowing before starting. The phoneme string the engine speaks is made by `MakeReadableJP` and the ESPR writer, and `Romanizer` is what drives the chain into them. Every unit before that is held to IBM's own objects byte for byte and heard by nobody.

## What finishing it means, besides the transcription

Three things, none of them large, and all of them gated on the romanizer working.

`JPROM_INCOMPLETE` comes out of `rom/jajp/jprom.h`. While it is defined `rom/jajp/rominstance.c` refuses to make an instance at all, which is what stops a half-written romanizer speaking something wrong; that file is its only reader. Take it out when `Romanizer` works and not before.

`lang/jajp` stops being gitignored. It is kept out of the tree only because a module that cannot make an instance would break any build that named it, and `.gitignore` says so.

And the real check becomes `EVV_LANG=jajp test/suite.sh` against a reference built from Japanese objects, which is the shape German already has. The cases are there: `test/cases/plain-jajp.txt` and `test/cases/utf8-jajp.txt`, seven apiece, which `test/romcan.sh` uses today to prove the engine below the seam by replaying IBM's answers. When the romanizer answers for itself, the same cases become the test of the whole of it, and `test/langs.py` should carry Japanese beside the other languages.

Read the objects with `llvm-objdump`, for the reason `docs/building.md` gives under getting IBM's objects: binutils `objdump` misparses whole functions here and says nothing about it.

Each class gets a harness before it gets a transcription. Where it sits on the seam, `test/romcan.c` can hand it the real work and keep replaying the rest, as it already does for the parameters. Where it does not, `test/romprims.c` calls it directly on both sides. A class whose methods are spread over several objects -- `DictSearch` is spread over four -- can also be tapped the way `reference/romtap.c` taps the manager, because those calls cross an object boundary.

And the standard the rest of this project is held to applies: it is not right until the samples are identical to IBM's, and a passing check proves nothing until the new code has been broken on purpose and seen to fail.
