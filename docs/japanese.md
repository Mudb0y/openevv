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

`RomUserDict` and the dictionary methods of `ConverterInterface` are what is left of the user-dictionary half, and they are blocked rather than deferred: `RomUserDict` calls `DictSearch::GetYoonIndex`, `SetLongWord` and `ConvertYoonDict`, so it cannot be written until `DictSearch` is. The ECI dictionary calls answer refused for Japanese until then.

`IniFileWriter` is wanted only by `romreg.obj` and by English's `engreg.obj`, both registration rather than speech, and our arrangement retired registration -- there is no library to find, so there is no path to write into an ini file. Transcribing it would be a file with no caller in either half of the tree. Nine of its thirteen methods have been read and are in this session's notes; the four left are `writeToMemory`, `deleteKeyFromSection`, `deleteSection` and the rest of `writeString`.

## Decisions already taken

**Call our romanizer directly and retire the vtable slot offsets.** Agreed 23 August 2026, done 27 August. `src/eci_romanizer.c` reached a romanizer through IBM's numbered slots -- `ROM_ADD_TEXT` at 0x0c and the rest -- and those existed only because IBM loaded a DLL. They are named functions in one struct now. The alternative considered and rejected was building a C++-ABI-compatible vtable object so those offsets kept working.

**One registration struct rather than a link-time symbol.** IBM answers "is there a romanizer" with the presence of `getRomObject`. A binary of ours can hold several languages, so the question is answered by family and dialect in `src/eci_romedll.c`, at compile time for what is linked and at run time for what a caller registers over it -- which is how `test/romcan.c` stands a recording where the romanizer would be. A weak symbol would have been the obvious way to ask the linker instead and does not resolve in PE the way it would in ELF, and this engine is built both ways.

**The dictionary and the tables are data, not code.** Lifted verbatim, like every other language's.

**`rominstance.obj` is not transcribed.** Every one of its 31 methods forwards to a `RomInstParam` or a `Romanizer`, and with the vtable gone there is nothing left for it to do. `rom/jajp/rominstance.c` is that forwarding written once.

## Where to go next

The order that follows from the above, if it helps. `ConverterInterface` is the surface, and `Romanizer` behind it is the thing that turns text into the readable form; `InputManager` and `InputChar` are how text arrives. Then the dictionary readers, `DictMan` over the tables already lifted and `DictSearch` over the dictionary; `kanastr.obj`'s `DictSearch::GenerateKanaString` and `PCRoman2BG` are the smallest pieces near the actual conversion and the natural place to see the record format for the first time. Then the analyser and the path search -- `TextAnalysis`, `PhraseTable`, `PhraseBuf`, `JPath`, `comppenalty`, `unknown`, `kakutei` -- then the number and English reading and the normalisation, and last the output side: `IntonPhrase`, `MakeReadableJP` and the ESPR writer.

Read the objects with `llvm-objdump`, for the reason `docs/building.md` gives under getting IBM's objects: binutils `objdump` misparses whole functions here and says nothing about it.

Each class gets a harness before it gets a transcription. Where it sits on the seam, `test/romcan.c` can hand it the real work and keep replaying the rest, as it already does for the parameters. Where it does not, `test/romprims.c` calls it directly on both sides. A class whose methods are spread over several objects -- `DictSearch` is spread over four -- can also be tapped the way `reference/romtap.c` taps the manager, because those calls cross an object boundary.

And the standard the rest of this project is held to applies: it is not right until the samples are identical to IBM's, and a passing check proves nothing until the new code has been broken on purpose and seen to fail.
