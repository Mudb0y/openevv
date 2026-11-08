# A language module

Everything in a module except the rules, which are in `docs/rules.md`: the five texts beside them, what a language has to declare about itself, and what it takes to put a language in the tree that IBM never shipped.

## The tables beside the rules

A language module is the rules and five other things: the variables the machine declares for it, the settings it carries in its own image, the statement table the machine is parameterised by, the lookup sets its dictionary lives in, and the bytes its rules name by address. All five were generated out of IBM's objects and said so at the top. All five have a text form now, beside the dictionary that already had one:

    lang/enus/enus.globals      the variables, 106 lines
    lang/enus/enus.settings     the settings, 83
    lang/enus/enus.statements   the statement table, 905
    lang/enus/enus.sets         the sets and the dictionary actions, 9,750
    lang/enus/enus.consts       the bytes the rules name, 445
    lang/<tag>/<tag>.dict       the words, which tools/module/dict.py writes

    make tables-dump      writes the four
    make tables-check     the C from each, held against the tree
    make tables-write     the C from each, for real

`tables-check` is the one to believe and it wants no objects: it writes each generated file out of its text into a directory of its own and holds it against what is in the tree, byte for byte. All five match for all nine languages, which is 45 of 45.

Each of the four keeps one writer, and the tool that lifts is the tool that writes. That is the whole discipline: a lifter that reads objects and a reader that reads text hand the same model to the same emitter, so what the text says and what a lift says cannot come out differently formatted, and the round trip is exact rather than approximately right.

What is deliberately not in the text is anything that follows from what is. The variables are a run of kinds -- `word 20`, `short 2`, `compound 1 5` -- and where each one lands and how big a machine of the language is are worked out from them by the same walk `delta_new` does, so English's 808 variables are 98 lines and the state size is derived rather than declared. The statement table's readers and writers are an offset and a width each, and their names follow the order the fields are in, exactly as the original's compiler numbered them: `vfg0000` upwards, one per field, no two fields sharing one across all 58 of English's. The settings' language number is the section that names it read as a family and a dialect. Nothing in any of the four is stated twice.

The dictionaries read for any language now, not only English. `EVV_LANG_DIR` points `tools/module/dict.py` and the two tools it leans on at one module, the same way it points the decompiler, so

    EVV_LANG_DIR=lang/plpl python3 tools/module/dict.py dump

writes `lang/plpl/plpl.dict`. Italian declares 13 dictionaries with 892 entries where English declares 28 with 5,945, and the shapes are the same: words to action numbers, and what an action says in an arm of a rule.

The names and the roots also say where a word is stressed, and the file shows it as `stress` and a pattern after the action number wherever a word's own arm states one: a digit a syllable, 1 for the main stress, 2 for a secondary one and 0 for none, counted back from the last syllable, so 20100 is Appalachians and 1 is a word stressed on its last syllable. A word added, or moved onto an action of its own because what it says changed, is given an arm of its own rather than one a removed word left behind -- that arm would keep the removed word's stress -- and is stressed by the rules unless it asks. `tools/module/lexicon64.py` reads the words of 6.1's dictionaries out of Apple's module beside it, which is how the two can be set side by side.

Two things about the sets are worth knowing before touching them. Its text is lifted from the C in the tree and not from IBM's objects, on purpose: the dictionary's three arrays in that file are laid down by `tools/module/dict.py` out of the words, so the objects hold what the dictionary said before anything was ever added to it. Running the sets lifter over that file is the one thing this repository tells you not to do, and this is why. And its numbers are the language: English declares 511 sets and 28 dictionary actions in 274 kilobytes of entries where Italian declares 153 and 13 in 77.

The statement table is the same shape in every language and that is a measurement rather than an assumption: ten types each, with 57 fields in Italian and both Spanishes, 58 in the two Englishes, 61 in German, 63 in Canadian French and 65 in French.

One thing this found and fixed. `tools/module/sets.py` had not been able to write the file it generates for some time: the copy in the tree had been brought to the arena's forms during the sixty-four bit work -- `EVV_REF(0)` where the tool still wrote `0` -- and the tool's own comment about the stores had gone stale with it, saying they are copied when what is copied is the table of pointers and the stores are handed over as they lie. English's file had the newer forms and the other seven the older ones. The tool now writes what English's says and the other seven have been brought into line: ten lines each, no data touched, and every one of the eight then regenerates byte for byte.

So a language IBM never shipped is now five text files and a table. The rules in `rules/`, the four above, the words in `<tag>.dict`, and `tools/module/gather.py` for the one table the engine knows a language by.

## The dictionary a language carries

A language can carry a dictionary of the kind a caller loads, beside the tables above and consulted after the caller's own, and part of the voice rather than a setting: `docs/quirks.md` says why US English carries one, begun from the community's. It is the place for a word whose spelling the rules cannot be expected to read -- a name, a brand, a word from another language -- where `<tag>.dict` is IBM's tables and a change there is a change to what the rules know. A word that is one case of a pattern belongs in a letter rule instead, which `docs/rules.md` and `lang/enus/letters` cover.

It is text in `lang/<tag>/dictionary`, and every build writes `delta_builtin_<tag>.c` out of it with `tools/module/builtindict.py`; that file is not in the tree, and a module with no such directory carries an empty dictionary. `main.dic`, `root.dic` and `abbr.dic` are the three volumes. Each opens with remarks saying what it is, and then comes the body: entries with no remark among them, kept sorted by the two tools below, which write it. After the body come blocks decided by hand, each under a remark saying why; the tools read those and never write them, and a word in one is left alone. Where a word is in a volume twice the later line wins, as it does in a file a caller loads. `removed` is the record of entries taken out, one a line: the volume, the word, what the entry said and why, with a tab between each. The build does not read it; the tools do, so that a word taken out on purpose is not put back. `upstream` names the commit of the community's IBMTTSDictionaries whose changes US English's has taken. `split`, where it is there, turns on the reading of a word with a capital inside it as the words it is made of, which the engine does after both dictionaries' main volumes have declined the word and before its rules see it: a capital starts a word after a small letter, and the last of a run of capitals does when small letters follow, unless all that follows is a plural s. Each line of `split` is a beginning that, with a capital after it, makes the word a name to be left whole -- Mc, Mac, O', De, Di, Du, La, Le, Van, Von -- and a word a root volume holds in small letters is left whole too, so that the root entry says it. A language may carry `split` and no entries, as `lang/engb` does. `fold`, where it is there, names a letter on each line and what the language's normaliser writes in its place before any dictionary is asked, and a word in a volume with that letter in it is filed under what is written, since the word the engine looks up is the one written so: US English's says ñ is n and y, so niño is found as ninyo. A word a volume already writes that way keeps its own reading. Only a letter the normaliser writes as something no plain word is spelled with belongs there; English writes every other accented letter as the plain one, and filing café under cafe that way would put exposé's reading on the verb expose.

`sayable` and `pieces`, where they are there, turn on the cut of a word English could not say, which the engine makes after the split has declined the word and before its rules see it; `docs/quirks.md` says what it does and why. `sayable` holds `onset` and `coda` lines, the consonants a word may begin and end with, and `word` lines, words that fail that test and are never cut. `pieces` holds the words a cut may use, one a line. Both are written by `tools/module/sayable.py` out of Moby's word lists and CMU's dictionary of 1993, whose addresses its own head gives, and the language's `<tag>.dict`, so that every word the tables hold is kept; `sayable` is never edited by hand, while `pieces` keeps the blocks written by hand below its body, each under a remark, and the tool leaves them alone. A word that is cut wrongly wants an entry, or, where it is a technical word said as a word that many others are glued to, a line in the hand block of `pieces`. A build with `EVV_NO_PIECES` defined leaves `pieces` out, about 140 kilobytes, and cuts into chunks and letters only, which reads more of a word as letters: env as E N V.

A line is the word, a tab, and what to say in its place, in UTF-8; the build converts it to the Windows Western set and stops on a character that set has not got. A line starting `#` is a remark. What to say is either words spelled as they sound, which the rules then read, or a pronunciation in the engine's own phoneme alphabet between `` `[ `` and `]` -- `` `[.1nIks.0o.2Es] `` -- with a stress mark before each syllable. `build/evv -A` takes the same notation in text, which is the quick way to try one before writing it down.

Which file decides how the word is found. A main entry is matched against a whole token as written, capitals and all, so `Allrecipes` in main is found for Allrecipes and not for allrecipes, and it is the place for anything with a capital or a mark in it. The one allowance is for the first word of a sentence: where the carried dictionary's main volume holds no entry for a word whose only capital is its first letter, it is asked for the word with that letter small, so `kubectl` is found for Kubectl as well, and a word that means something else with the capital wants an entry of its own, as `Ms` and `Io` have. A caller's dictionary has no such allowance, as IBM's had none. A root entry is written in small letters and is found inside a longer word as well, as a stem the rules have taken an ending off, so one entry for a noun gives its plural and its possessive too. A main entry gives its possessive as well, but not its plural. The abbreviation file is read only while the abbreviation dictionary is on, which it is not by default. A root entry has to say one word or a phonetic spelling, since several words in its place do nothing, so an entry that wants several goes in main in small letters. And a word with a digit in it never reaches either dictionary: the normaliser spells IPv6 and SAPI5 out a part at a time before any lookup, so they cannot be entries.

A word of one's own goes in a block decided by hand, an existing one or a new one at the end of the file under a remark saying why, never in the body, which the tools write. To hear what it does, say the word before writing it down:

    build/evv -o before.wav "We met on teamtalk."

and the same again into after.wav once the line is in and `make evv` has built it.

An entry moves `test/words.sh` wherever its word is in the list, and the recording refuses if CMU then agrees with fewer words than it did, which is the check that an entry made things better rather than different.

To take the community's later changes, clone their repository, https://github.com/eigencrow/IBMTTSDictionaries, and give the tool a library built from the tree as it stands, the clone, and the commit of theirs to take changes up to, HEAD for their newest:

    tools/measure/dictgate.py build/libeci.so IBMTTSDictionaries HEAD dictgate.tsv

It reads what they changed between the commit `upstream` names and that one, and nothing else: an entry added, a reading changed, an entry dropped. Each change is judged against what the dictionary says now -- a word decided by hand, a word `removed` names and a word the body has since said otherwise are left as they are -- and the rest against CMU, speaking only the words a change touches, so it takes about a minute. A line for every change goes to `dictgate.tsv` and nothing else is touched. Run it again with `--take` to put the changes it takes into the bodies, the entries CMU stopped into `removed` and the commit into `upstream`. What it stopped or left is in the report, for a person to decide by the published pronunciation, as `docs/quirks.md` says; the verdicts are in its own head.

To look for more in the published pronunciations, give the tool a library built from the tree as it stands and the three sources it reads -- Wiktionary as kaikki.org extracts it, the Moby Pronunciator and CMU's dictionary of 1993, whose addresses its own head gives:

    tools/measure/published.py build/libeci.so kaikki.jsonl.gz mpron.txt cmudict.0.3 published.tsv

It speaks every word Wiktionary or CMU has, about 175,000 of them, several times over, which is twenty minutes of the whole machine, so run it in an idle-class scope. What it would add and take out goes to `published.tsv`, and with `--write` into the bodies and `removed` as well. It leaves alone every word decided by hand or named in `removed`. Its head says how a word is judged clearly wrong, why an entry is a root and how an entry or a removal that harms another word is found and dropped.

## Adding a language

`lang/plpl` is the ninth language in the tree and the first IBM never shipped. As it stands it is Italian: made by copying `lang/itit`'s text forms and renaming them, so it speaks Italian under a Polish name. That is not a placeholder, it is the chassis -- everything after this is a change with something audible on both sides of it -- and `NOTICE` says what the licence consequence is, which is that all of it is IBM's Italian until it has been replaced.

Italian is the template for reasons rather than convenience. Its stress is predominantly penultimate and Polish's is almost always penultimate. Its five vowels have no reduction. Its consonants have the affricates ts and dz, tʃ and dʒ, the palatal nasal that is exactly Polish ń, and a trilled r -- which is the hardest part of Polish and the part Spanish only half has. And it is the smallest of the European modules, 1,749 rules against English's 3,377.

What making one takes. Every module in the tree already holds its rules as text, so a template needs no lifting first:

    cp the template's five text forms and its rules/, with the tag renamed
    a section naming the language, and a library name
    "plpl": "Polish" in tools/module/gather.py, then run it
    make LANGS="lang/enus lang/plpl" tables-write             the C from the texts

and then it builds and speaks like any other: the three files a build compiles are written out of the copied text by the build itself. No object is opened at any point, which is the whole reason the text forms exist.

### The number a language is

A language is a family and a dialect packed into a word, and the family is not free. Three tables are indexed by it and all three hold eighteen: the standard voices in `src/eci/lang/eci_voicetable.c`, the dictionary in force in `src/eci/dict/eci_dict.c` and the romanizers in `src/eci/lang/eci_romanizer.c`. IBM used families one to five and eight. And four more are spoken for: `rz_isRomExist` says families 6, 10, 11 and 16 have a romanizer, so an instance of one of those is refused outright when the romanizer is not there -- which is what happened when Polish was first given family sixteen, and `eciNewEx` answered -21 and nothing else. Polish is family seventeen, `0x110000`, which is clear of all of it with eighteen left spare. Finnish is family nine, `0x90000`, and Brazilian Portuguese family seven, `0x70000`, which are 6.1's own numbers for them and clear of the four.

### What the language means by its variables

IBM's names for the machine's variables are gone: the only record is a disassembly that carries kinds and not names. So a rule that sets a formant says `global half 2926` and nothing tells you what that is. `<tag>.globals` can now say:

    name short 423 f2_in

and a rule written in the upper form says `set f2_in to 2000`, which compiles to that same offset. `python3 tools/module/globals.py where plpl 2926` is how one is worked out from the other: it answers `short number 423, 2 bytes into it`, two bytes being where a short cell keeps its value.

The ten that are named in `lang/plpl/plpl.globals` are the formant targets a consonant is spoken with, each formant twice because the transition into it and the one out of it are separate numbers. They were read off Italian's own value rules: the trill sets the first to 450 and the second to 1250, the labials set the second to 850, the dentals and velars to 1700 and the palatals to 1800 -- which is where a labial's low second formant and a palatal's high one belong, so the reading is the language's own rather than a guess.

`lang/plpl/rules/is_val.up` is the first rule written for Polish rather than lifted for Italian, and all it does is say what the alveolo-palatals -- the series Polish has and Italian has not -- are spoken with. Nothing calls it yet. Its numbers are a starting point: ś and ź sit between Italian's palatal and its dentals with a higher third formant, and the ear settles the rest.

### The alphabet, and what each letter says

A language's alphabet is the value names of the input statement's first field: 207 of them for Italian, from `GAP` and the five vowels through the consonants, the digits, the punctuation and the accented Latin-1 characters. And beside it, in the same statement, the `variants` bytes are one record of five bytes for every one of those names, in the same order -- 1,040 bytes for 208 -- holding what case the character is, whether it is a letter or a digit or punctuation, whether it is a vowel or a consonant or a glide, whether it carries an accent, and the phoneme it says on its own.

That last field is letter-to-sound at its simplest, and it is data. `a` says a, `b` says b, `y` is a glide that says y, `ó` carries an accent and says nothing of its own because the rules decide it. A capital says nothing either: `B` and `A` both have GAP where `C` and `N` have C and N, which is the table having been filled by matching a phoneme's name to a character's, so the rules take a capital down to its own lower case before they ask.

    python3 tools/module/alphabet.py show plpl          every character
    python3 tools/module/alphabet.py show plpl a e y    only the ones named
    python3 tools/module/alphabet.py add plpl 82 case=lower type=letter \
                                   letter=vow accent='~yes' phoneme=a

reads and writes it by name, because a five-byte record read by eye in a hex blob is how a letter quietly becomes a digit.

`add` puts a character at a byte value the alphabet does not claim yet and appends its record, rather than reusing a code: the dictionaries are keyed by these codes, so moving one moves every word that used it. Nineteen byte values between 0x20 and 0xff are claimed by no name in Italian's alphabet, and Polish needs sixteen.

Those sixteen are in now, each starting from the nearest phoneme the module already has: `ł` says w, which is what Polish ł is; `ń` says N, which is the palatal nasal Italian spells gn and is exactly Polish ń; `ć` says C, `ś` says S, `ź` and `ż` say Z, `ą` says a and `ę` says e until the nasal vowels are read out of French. `ó` needed nothing, being already in the alphabet. The capitals say GAP as every other capital does.

What that changed, measured rather than assumed. Before it, a Polish letter cost about thirteen thousand samples wherever it appeared, because the engine had no name for the byte and read it as a symbol -- eight of them alone came to 103,356 samples, a second each. After it, `kąt` is 10,648 samples where `kat` is 8,107, and the two share the first 14,336 rule entries of their traces, which is what says ą is being handled as the vowel it now is rather than as an interruption.

A Polish letter *alone* is still silent, and that is the next thing rather than a fault: a lone letter is spoken by its name -- Italian says esse for s -- and Polish's letters have no names yet.

### How a character gets in

A caller writes code points and the machine reads single bytes, and between them IBM's engine does almost nothing: the code set only ever mattered under the SSML filter, which recodes, and for the four families with a romanizer, which convert their own. On the ordinary path the caller's bytes are the characters. That was enough for eight of the nine languages IBM shipped, because every letter any of them has is in the Windows Western byte set, and the ninth has a romanizer that recodes. It is not enough for a language whose letters are not, and a caller writing UTF-8 -- which is every caller now -- would hand over two bytes the machine reads as two characters. That is what `Zażółć gęślą jaźń` did: 120,714 samples, a minute of symbol names.

So a language can say what its own characters arrive as, and `lang/<tag>/<tag>.codepoints` is where:

    0105 82   # a with ogonek
    0107 83   # c with acute

    make EVVLANG=lang/plpl codepoints

writes that into `delta_codepoints_<tag>.c`, and the language carries it in `delta_language` beside everything else it knows. `tools/module/codepoints.py` refuses a byte the language's alphabet does not name, since a character arriving as a byte nothing names would simply be something else.

`addTextRun` in `src/eci/synth/eci_synthtext.c` then converts the text on the way in -- and this is a deliberate divergence from IBM's engine, the fourth in the tree. What makes it safe rather than merely careful is the guard: the conversion runs only for a language that declares characters of its own, and the nine IBM shipped declare none, so their behaviour cannot change. The suite says so rather than the argument: English's 81 cases, German's 80 and the samples hash are all untouched by it, on sixty-four bits and on thirty-two.

What it buys, measured on the same pangram: 16,819 samples where there were 120,714, and `kąt` written as UTF-8 comes out byte for byte identical to `kąt` written in the module's own bytes, which is what says the conversion is exact rather than approximately right.

Text that is not UTF-8 after all is left alone, because the converter answers whether it was and the caller's own bytes are used when it was not. So a caller that sends the module's bytes directly still works, which is what the measurements above were taken with before any of this existed.

### Reading what a language decided

`build/probe <text> <file> p` asks for phonemes instead of sound: what the language decided the words are made of, under the names its own statement table gives them. It is the tool the whole of Polish wants, since it says what letter-to-sound answered without anybody listening.

**It reports now, and what it reports is IBM's.** `"Hello there."` comes back as ``` `2 `[.2hE.1lo]`0 `[.1Der]. ```, which is what IBM's own engine answers to the byte, and thirty cases over the plain, annotation and long files do the same. `make phonemes` is that check and `docs/testing.md` describes it.

What was in the way was not the flag and not the state. It was that the Delta runtime's printing layer was a stub. `print_lit`, `print_var` and `print_stream` are what a rule calls to write anything -- 78 calls between them across English's rules -- and `src/delta/delta_trace.c` left all three empty on the reasoning that printing was the Delta debugger's trace and no target here wants a debugger. That reasoning was wrong about one thing: printing is also how a language says what it decided, through the ECI link, which is a shipped feature rather than a debugger's. The three are written now, with the two workers under them, `vprt_var` and `vprt_strm`.

Writing them found a second fault, in code that had only ever had one caller. `disptok` spells a token under the name its field gives the value, and it took the pointer it was handed to be four bytes into the token where every caller of the original hands over eight -- so every name came back as the value four bytes further on, which for the phoneme field is entry nought of its list and reads as `GAP`. One caller could not show that; the second one did, and every phoneme in a sentence came out `GAP` until it was fixed.

### What a phoneme is made of

A phoneme is in three places at once and none of them alone says what it is. Its name is a value of the phone statement's first field, which is the list the rules index by. Its numbers are a `Phoneme` line in the settings: four bytes of name and eleven values, which is what a caller handing the engine phonemes rather than text is read against. And what it sounds like is a rule named for it -- `ital_ph_S` -- which sets its source parameters and then calls one locus rule, `ital_pal_Fv` and its kin, where the formant targets are.

    python3 tools/module/phonemes.py plpl

puts the three beside each other. Italian declares 35 in the statements, 34 in the settings and gives 21 a rule of their own; the vowels and a few consonants have none, being spoken by other machinery. It also says which place each one is spoken at, which is the thing to know before changing any of it: `ital_pal_Fv` is called by `ital_ph_S` and `ital_ph_Z` and also by `ital_ph_t` and `ital_ph_d`, so moving that rule moves four phonemes and moving the call inside two of them moves two.

`registerPhoneme` takes nineteen arguments and all eighteen after the machine are addresses of the rule's own locals -- places the engine keeps that phoneme's numbers, not the numbers themselves. There are 34 of those calls for 34 phonemes, in the order the settings declare them.

### Changing a sound

Polish speaks sz, ż, cz and dż as retroflexes, further back than the palato-alveolars Italian spells with sc and gi, and the signature of a retroflex is a low third formant. `lang/plpl/rules/is_val.up` is that, written in the upper form against the names in `plpl.globals`: `pol_retroflex_Fv` brings f3 down from Italian's 2400 to 2200 and f2 from 1800 to 1700, and the two calls inside `ital_ph_S` and `ital_ph_Z` in Polish's own copy of `is_val.dr` point at it. Two lines of the lower notation changed and one rule written.

What that proves, and what it does not. It proves the whole sound path from an authored rule to the samples: `sciarpa` spoken by Italian and by Polish is the same word at the same length -- 10,197 samples each -- with 17,448 of its 20,438 bytes identical, so exactly one sound in it moved and nothing else did. That is the formant path end to end, and it is the first change to how Polish sounds rather than to what it accepts.

It does not make a Polish word sound different, and the reason is the next piece of work. `sciarpa` reaches that locus three times; `szafa` reaches it not once. Polish spells its retroflexes as digraphs -- sz, cz, rz, dz, dź, dż -- and Italian's letter-to-sound knows sc, gi, gn and gl. So a Polish word today comes out as the letters it is spelled with, one at a time: `szafa` is s and z and a and f and a. The phonemes are in the module and the letters are in the alphabet; what is missing is the rules that say two letters make one sound, and those are rules to write rather than data to fill in.

### Keeping the chassis honest

    make EVVLANG=lang/plpl census

says how much of the module is still the template's, rule by rule and table by table, out of the text forms alone. It reads 99% Italian today: 1,749 rules of 1,750 character for character Italian's, one ours, the settings two lines apart and the variables named. The number falls as the work is done, and what it is there for is the failure it prevents -- Italian phonology coming out of something labelled Polish without anyone noticing.

`TEMPLATE` says what to hold it against, and `tools/module/census.py <tag> <template> rules` lists every rule with which of the three it is.

### A language lifted from ETI's 6.1

Apple ships ETI Eloquence 6.1's language modules with its operating systems, and every one of them can be read the way Finnish was. The module is a fat Mach-O; its x86-64 slice is the one the tools read:

    llvm-lipo -thin x86_64 fin.dylib -o fin.x64.dylib
    tools/module/lift61.py fin.x64.dylib fifi globals          the variables
    tools/module/lift61.py fin.x64.dylib fifi statements       the statement table
    tools/module/lift61.py fin.x64.dylib fifi sets             the sets and dictionaries
    tools/module/lift61.py fin.x64.dylib fifi rules            every rule, through tools/rules/lift64.py
    tools/module/lift61.py fin.x64.dylib fifi settings         out of the rules that register voices
    python3 tools/rules/consts.py fifi                         the strings the rules name
    "fifi": "Finnish" in tools/module/gather.py, then run it
    make LANGS="lang/fifi" tables-write                        the C from the texts

A tag goes in `LANG_NAMES` and `LANG_NUMBERS` in `lift61.py` first: its name, and the family, dialect and library name 6.1 gives it. A rule the lifter cannot read is reported by name and is not written; `rules/written.up` is where one written by hand goes, saying `instead`. The settings say `Version=6.1`, which is what tells the interface to drive the language as 6.1's does.

What the result is held to is 6.1 itself, through `test/harness/eti.sh` and a `test/cases/eti-<tag>.txt` of its own, with its word list run through the same harness; `docs/status.md` says how Finnish was.

## Languages

`LANGS` says which languages go in. One:

    make LANG=lang/dede probe

or several, in one binary:

    make LANGS="lang/enus lang/dede" probe

`LANG` is the name for one of them and is what everything already says; `LANGS` takes a list, and the first one named is what a caller gets when it asks for no language in particular.

A build of English alone keeps the plain names -- `build/probe`, `build/libevv.a`. Anything else carries what it has in it: `build/probe-dede`, `build/probe-enus-dede`, and the archives to match. That is not tidiness. An archive is built out of one set of objects, and those already sit in directories of their own, so building German and then English again would leave an archive newer than every English object: make would not rebuild it, and the English probe would be linked against the German engine.

How several fit in one program is in `src/delta/delta_lang.h`. The short of it: every module names its own tables after itself -- `enus_vstmtbl`, `dede_vstmtbl` -- because IBM gave them the same names in every language, and the engine reaches whichever is in force rather than linking to one by name. A machine remembers the language it was made for, the engine keeps one engine per language as the original does, and `eciGetAvailableLanguages` answers with all of them.

## Testing another language

The oracle has to be built from that language's own objects, and goes somewhere of its own:

    make -C reference TAG=dede BUILD=../build/reference-dede

Both have to be given. The default output directory is the English one, because that is where `test/compare.sh` looks when nothing says otherwise.

Then `EVV_LANG` runs the suite against it:

    EVV_LANG=dede test/suite.sh

which picks `build/probe-dede`, `build/reference-dede` and the cases named for that language -- `test/cases/plain-dede.txt` and the rest. Naming the language is what keeps an English engine from being held against a German oracle, which differs on every case and says nothing.

A binary with several languages in it is driven the same way, with `EVV_NATIVE` naming it:

    EVV_NATIVE=$PWD/build/probe-enus-dede test/suite.sh
    EVV_LANG=dede EVV_NATIVE=$PWD/build/probe-enus-dede test/suite.sh

`compare.sh` sets `EVV_LANGUAGE` from the language it was asked for, and the probe asks the engine for that one rather than whichever is first. Those are IBM's own numbers, the ones its ini names each language section for; a language added to the tree adds a line to that table.

All nine of the SDK's languages pass the cases there are for them, each against a reference built from its own objects: US and British English, German, both Spanishes, both Frenches, Italian and Japanese. `docs/status.md` says in which configurations, and what Japanese wanted first.

The language numbers `compare.sh` knows are IBM's own: 0x10000 and 0x10001 for the two Englishes, 0x20000 and 0x20001 for the Spanishes, 0x30000 and 0x30001 for the Frenches, 0x40000 for German, 0x50000 for Italian. A language added to the tree adds a line to that table.

One thing about the `utf8` cases is worth knowing before reading too much into them. The engine takes one byte at a time, so what those cases really check is that both sides mangle multi-byte text the same way, not that either handles it. For Spanish that is not merely mangled: an o-acute directly before an n faults IBM's engine and ours identically, so `razón` in UTF-8 cannot be compared and the Spanish case files avoid the sequence. The same word in Latin-1 speaks perfectly, which is the answer for a caller that wants accents.

Everything a language module holds is named for that module, and the build takes whatever `.c` and `.h` files are in one. A file left behind by an earlier lift, or copied in from another language, would otherwise be compiled in without a word, which is how `lang/dede` carried an unprefixed rule shim into every German binary for a day: its names collided with nothing, so the linker had nothing to say. The build now refuses a module holding a file that is not named for it, and says which file.
