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

The first is the static dictionary: 1,723 blobs, 2,669,092 bytes, and seven pointer arrays whose lengths match the symbol counts exactly. The second takes the three objects that are mostly table -- `dictman.obj`, `unicodeconvt.obj` and `jpnutil.obj`, whose own data section holds the romaji every kana is spelled with -- and answers 113 tables, 192,706 bytes. It writes a header beside the C declaring what is in it, so a table cannot be declared one way and defined another. Each object comes out as one block with a pointer per table rather than an array each, and that matters -- see the note on the lead-byte tables below.

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

The remaining thirty-five are the romanizer proper: about 168,000 bytes of x86 and 198,000 of data, of which the data is the three objects already lifted -- `dictman`, `unicodeconvt` and `jpnutil`, whose own data section holds the romaji spellings. Five of those objects are written. What is left is roughly twenty to thirty thousand lines of C, judged from the four to eight bytes of x86 per line of ours that three already-ported objects came out at.

It is a Japanese morphological analyser, not a lookup table. The classes, with how many methods each has:

    DictSearch 64   TextAnalysis 36   JpnUtil 32        MakeReadableJP 30
    DictMan 26      ConverterInterface 24                InputChar 20
    IntonPhrase 17  Romanizer 16      PhraseTable 16     NumRead 11
    JPath 11        InputManager 10   RomUserDict 9      PhraseBuf 9
    TextNormalizer 4

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
- `JpnUtil::euc2shift` and `seven2shift`, in `codeconv.obj`, 2,794 bytes
- `getFullRomPathName`, twenty bytes in `libmain.obj`, which has the same trap in it as `getFullPathName` above

The skiplist chain is written and proved: `src/eci_key.c`, `eci_translation.c`, `eci_listnode.c`, `eci_skiplistnode.c`, `eci_arraylistnode.c` and `eci_skipstore.c`, held to IBM's own objects by `test/romprims.sh` over insert, search, multiSearch, remove, a full walk and a save-and-load round trip.

Two things about that store are worth knowing before reading it. Its constructor calls `srand(time(0))`, so the tower over the entries differs between two runs and **a saved file is not the same file twice** -- which is why the sweep compares what the list answers rather than what it writes, and why a round trip is checked by walking the loaded list. Nothing else in this engine uses `rand`, so the seeding disturbs nothing. And the load path turns file indices back into pointers only after every node exists, because it cannot do it sooner.

`RomUserDict` is written: eleven methods in `rom/jajp/userdict.c`, the whole class. It was blocked on `DictSearch::GetYoonIndex`, `SetLongWord` and `ConvertYoonDict`, and writing those unblocked it.

What it does. A caller gives it a word as it is written and a reading in kana with a caret where the accent falls. `makeKey` normalises the written form -- half-width kana, letters, digits and the two commas all become the two-byte forms the built-in dictionary is written in, and a following voicing mark is folded into the character it marks -- so that a user word is looked up by the same key shape as a built-in one. `makeTransValue` copies the reading out and counts which mora the caret marked, skipping the small kana that join the sound before them, and stepping the accent back off a doubled consonant or a lengthened vowel, neither of which can carry one. `transKatakana2Yomi` then spells that reading as the engine's own yomi codes -- it is `DictSearch::ProcessKatakana`'s inner walk done again over a string, and it has to stay that way or the path search would weigh a taught word differently from a found one. The record goes into the same skip list the English dictionary uses, keyed by the normalised form.

And on the way back: `lookup` hands the whole of what is left of the sentence to `SkipList::multiSearch`, which answers with what matched each prefix of it at once, so a one-character word and a five-character one starting in the same place both come back in one call. Each becomes a candidate entry through `writeData`, written at IBM's own offsets so that it is indistinguishable from what the built-in dictionary produced.

Two things the sweep settled that reading alone had not. The two longs at the end of `DictSearch` are a mode and a pointer: when the mode is one, a user entry is taken only if its first two bytes and its written form are the ones that pointer names. And that pointer is the last field of the record, which on a sixty-four bit host takes eight bytes where IBM had four -- so ours allocates a pointer's worth more than `TextAnalysis::initialize` asks for. `DS_ROOM` in `rom/jajp/dictsearch.h` is that, and the offsets stay IBM's.

The dictionary methods of `ConverterInterface` go through `RomUserDict` and are what is left of that half; the ECI dictionary calls answer refused for Japanese until they are written.

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

Thirty-four of `DictSearch`'s sixty-four methods are written, in `rom/jajp/dictsearch.c`, and they are not twenty scattered ones: they are the closure of `GenerateWord`, which is the whole of what it takes to turn one run of text into candidate readings and then into dictionary words. Nothing that closure calls is outside the file except `JpnUtil`, `DictMan` and `memset`, all of which were already written, which is why it was the right unit to take next.

What it does, end to end. `GenerateWord` copies the run of text starting at a position -- hiragana and katakana freely and at most one kanji, which is the ordinary Japanese word with its okurigana trailing off it -- and hands it to `GenerateKanaString`. That walk goes character by character. Katakana and hiragana it spells out itself, through `ProcessKatakana` and `ProcessHiragana` and the yomi table: a small kana after another kana makes one sound if `ConvertYoonDict` has that pair and two if it has not, a small tsu becomes the doubling code, a long-vowel bar doubles the vowel in front of it. A kanji goes to `LookupKanaDict`, which may answer with several readings at once, and every reading already being built is then copied once per answer so that the product of all the choices is present. What comes out is up to thirty candidate readings for the same stretch of text. Then, for each candidate the kanji dictionary did not itself produce, `SearchTankanTable` walks the single-kanji table and `GetDictEntry` walks the trie under it, writing every word whose reading is exactly that long into the candidate entry array -- which is what the path search above will choose between.

The second unit is the whole of `dictapi.obj`: the five dictionaries a stretch of text is looked up in, and the three writers that put what they find into the candidate array. They share a shape -- walk a hash to find where in the dictionary to start, walk a trie or a block of records from there, hand every match to a writer -- and what differs is the dictionary. The compound-word dictionary is a trie over whole characters whose hash is taken on *two* of them, which is what makes a dictionary that size searchable; the single-kanji one is another trie, keyed one character at a time, with every kanji put through the variant table first so a variant form finds its standard form's readings; the supplement and English dictionaries are flat blocks of self-delimiting records with an index over first characters.

Three things in it are worth writing down. The compound walk retries twice, and neither retry is obvious from the code alone: the hash lands on a block boundary, so a word may sit in the block *before* the one it points at -- hence stepping back while anything matched at all -- and where the walk went deeper than one character the block after is worth one try. Then, third, a run that found nothing at all is tried again with the variant table on. A word may not end where the character after it would join it, which is what `WriteData` refuses before writing anything: a small kana joins the sound before it, and a long bar after a katakana or another bar lengthens it. And the English walk lowercases as it goes and stops at a capital following a small letter, which is how a name written as one run comes apart into its words.

The third unit is `fdictapi.obj`, and it is a different kind of lookup from the five above. A function word -- a particle, an ending, an auxiliary -- is not chosen on its spelling alone but on what it may attach to, so the dictionary carries a bit vector per word saying which kinds of phrase can precede it, and the search is handed the vector of what actually does. A word is taken only where the two agree. What comes out is not a candidate entry but a row of the function-word array, and `LookupFuncWordDict` is the pass that turns those into entries afterwards -- working out each word's part of speech by describing it in four bytes and finding the row of the phrase-type table that matches.

Two details in it are worth having. A long-vowel bar does not end a function word: the same trie node is asked again with the bar counted in, which is the only place in the analyser where a walk goes back over a node. And a word that says it begins a phrase does not begin one if the character after it is a doubled consonant, a small ya, yu or yo, or an n -- nothing in Japanese starts with those.

That file keeps IBM's layout rather than naming its own fields, which is a departure from the rest of the directory and a deliberate one. A class this size is half-written for a long while, and a half-written one has to be driven over state built by hand; sharing the layout means `test/romprims.c` can build that state the same way on both sides instead of maintaining two descriptions of the same bytes. It also avoids inventing names for the fields nobody has read yet.

Writing it settled four more regions and two record formats. The ten twelve-byte slots at 0x847c hold one kanji's readings before they are spread over the candidates, with a byte of characters and a byte of length beside each; the three arrays reach the word below them exactly, which is what says ten and not eight. The word at 0x8508 is the cursor into the entry array. And in the spine, 0x5f2 turned out to be thirty readings of twenty-six bytes with the count at 0x8fe -- a reading too long for the ten bytes an entry holds goes there and the entry keeps its number -- which is `SetLongWord` and `TextAnalysis::AddLongWord` between them, and thirty times twenty-six from 0x5f2 is 0x8fe to the byte.

The character classes are read off `InputChar::GetCharType`, which is the only place the numbering is stated, and getting them from anywhere else is how a katakana test gets read as a kanji one. One is katakana, four hiragana, eight the long-vowel bar, eleven the middle dot, and nine is kanji -- nine being the classifier's default, so anything it does not recognise arrives as a kanji, and so does an index before the start of the text. `rom/jajp/dictsearch.h` names all twelve.

That sweep caught a real error on its first run, and the kind worth naming. `CheckCaseMarker` compares against a string stored under the mangled name `??_C@_02PGLDAILO@?$IC?p?$AA@`, and working that encoding out by hand gives 0x8270. The bytes in the object are 0x82f0, which is the particle *wo* rather than a full-width Q -- a different character and a different meaning for the method. Read the bytes; do not decode the name.

It caught a second on the closure's first run, and it is the same mistake in another dress. `CompareKanji` tests each character of the span against nine, and nine had been taken for the long-vowel bar; it is kanji. The method is what says a reading that matched belongs to *this* word and not another with the same sound -- every kanji in the span has to appear somewhere in the entry -- and read as the bar it meant nothing at all. Seventy-nine lines moved.

How the closure is swept. Everything below `GenerateWord` is called directly on both sides, over ranges rather than examples: all 256 codes through `IsOnin`, all 768 two-byte characters in the three lead bytes a small kana can have through `GetYoonIndex`, every row and every small kana and both flags through `ConvertYoonDict`, every page of both dictionaries and the offsets either side of each bound through `ReadGWDict`, and the long-reading store filled thirty-three times so that its refusal at thirty is swept too. `WriteKanaData` gets nodes built by hand with nought to eight readings, because no kanji in IBM's own dictionary has five on one node and the cap at five would otherwise go untested -- which the sabotage matrix is what noticed. Above that, the twelve texts are the seven the differential suite already speaks plus five written for the roads a sentence does not take, and `GenerateWord` runs at every position of every one of them, with each thirty-two byte entry it writes printed as bytes.

The character classes that sweep feeds in are the harness's own copy of `GetCharType`, since `InputChar` is not written. That decides only what goes in, and both sides get the same thing, so a mistake there narrows the sweep and cannot hide a difference.

All twenty-two sabotages move lines -- one per method, and two for `WriteKanaData` because the first was below its threshold.

The dictionary unit is swept the same way and, with the function words after it, brings the total to 335,965 calls a side: the variant table over all 65,536 values a two-byte character can have, the three writers over records built by hand with a word count of nought to three and a reading of every length a nibble can hold, and the five dictionaries at every position of nineteen texts, out of context and in it.

Getting from fourteen sabotages observed to twenty-one is the part worth recording, because none of the seven that did not move lines was a statement about the code. Five were dead because of the corpus: the English dictionary is keyed by full-width lowercase letters and every text was half-width ASCII, so that whole road was never walked; the supplement dictionary holds symbols and compounds beginning with a full-width digit, and no text had either; and the retry that turns the variant table on only matters where a variant kanji is present, which none was. Seven texts were added for exactly those. Two more were sabotages too weak to show -- a mask narrowed where no value in the shipped dictionary was wide enough to notice -- and became visible when aimed at the arithmetic rather than at the mask. One is left: shortening the English scan by its last block changes nothing, because the five words it finds are all in earlier ones. The block arithmetic itself is observed by a different sabotage.

`RomUserDict` is swept the same way and brings the total to 243,474: every one of the 256 bytes through `makeKey` on its own and again after a kana, so that the voicing marks and the half-width range are covered; a caret in every position of a reading, including ones that are not kana at all; every part of speech and two that are not; a candidate entry written at each end of the array and with the long-reading store both empty and full; and then a real dictionary taught eleven words, read back one at a time, and looked up against a sentence in both of `DictSearch`'s modes. All fifteen sabotages of it move lines.

Making the mode-one arm testable took a correction. It compares the stored record's first two bytes against what the context names, and those are how many characters the written form has and how long its reading is -- not the reading itself, which is what the harness fed it at first. With the wrong two bytes nothing ever matched, the arm was never entered, and its sabotage moved nothing. That is the same shape as `WriteKanaData`'s cap: a sabotage that changes nothing is a question about the harness first.

**A pointer at offset four cannot stay there.** The owner is at `DS_OWNER`, four bytes, and the candidate array begins at offset eight -- so on a sixty-four bit host the pointer's upper half and the first entry are the same bytes, and writing one candidate makes the owner unreadable. Nothing caught it until the dictionary unit, because it takes a method that writes an entry and then asks the owner for something, and until `WriteUserData` no method did both. Ours keeps the owner past the end of IBM's record now, at `DS_OWNER_AT`, and `test/romprims.c` sets it through a macro so each side writes its own place. IBM's offsets are untouched, which is what the map and the sweep depend on. Two other pointer fields in this record already lean on the four bytes behind them being unclaimed; this one had a claim.

**A ninth deliberate divergence, and IBM's fault.** `updateDictExt` lets the written form be thirty-two bytes, and every half-width kana in it becomes two, so the key it builds can reach sixty-four. IBM's buffer for that key is about thirty-six bytes of its own stack. Nineteen half-width kana is the last word that works; at twenty its frame goes and Wine's debugger comes up. Measured at exactly twenty, not reasoned about. Ours sizes the buffer for the bound the function itself enforces, so it answers instead of dying -- and that case is left out of the sweep, because there is nothing to compare against a side that has stopped running.

Those four arrays carry a caution for whoever transcribes `GenerateKanaString`: it clears each of them with a memset of thirty bytes, which is the whole of the first and half of each of the other three. That has to be reproduced rather than tidied. A slot past the fifteenth starts out holding whatever was there before, and the only thing keeping the reads inside what was cleared is the count.

About twelve hundred bytes are left over in two spans, each named as unresolved with its exact bounds so the map still tiles and says plainly what is not known. Twenty-nine offsets across its four objects, all inside a named region; a count or a stride changed by one is caught as a gap or an overlap of exactly that much.

Two regions inside the map of the spine are named but not resolved: the parse's own marks
between 0x2c and 0x5d8, cleared at the top of `TextParsing` and read at several
widths, and a working area of 1,716 bytes that `CheckPhraseLink` takes the
address of. Both are bounded exactly; what is in them is for whoever writes
`TextParsing`.

## Where to go next

The order that follows from the above, if it helps. `GenerateWord`'s closure is done and so is `RomUserDict`, so the record formats are known and the sweep pattern for a class over IBM's own state is established twice over. The rest of `DictSearch` is forty-four methods over the same records, and `Do`, `TextParsing` and the function-word half are the next coherent unit in it. `InputChar` is what makes the input the whole of the above reads, and it is on the text path rather than the dictionary one; `Annotation` beside it is small and self-contained. `ConverterInterface` is the surface -- its dictionary methods are the last of the user-dictionary half and are short, since `RomUserDict` does the work -- and `Romanizer` behind it is the thing that turns text into the readable form; `InputManager` is how text arrives. Then the analyser and the path search -- `TextAnalysis`, `PhraseTable`, `PhraseBuf`, `JPath`, `comppenalty`, `unknown`, `kakutei` -- then the number and English reading and the normalisation, and last the output side: `IntonPhrase`, `MakeReadableJP` and the ESPR writer.

Read the objects with `llvm-objdump`, for the reason `docs/building.md` gives under getting IBM's objects: binutils `objdump` misparses whole functions here and says nothing about it.

Each class gets a harness before it gets a transcription. Where it sits on the seam, `test/romcan.c` can hand it the real work and keep replaying the rest, as it already does for the parameters. Where it does not, `test/romprims.c` calls it directly on both sides. A class whose methods are spread over several objects -- `DictSearch` is spread over four -- can also be tapped the way `reference/romtap.c` taps the manager, because those calls cross an object boundary.

And the standard the rest of this project is held to applies: it is not right until the samples are identical to IBM's, and a passing check proves nothing until the new code has been broken on purpose and seen to fail.
