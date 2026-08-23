# Japanese, and what is left of it

Eight of the nine languages in the SDK build, speak and match IBM byte for byte.
Japanese is the ninth. It lifts completely and it is one line from speaking, and
what stands between that and a finished language is the romanisation module --
what `jpnrom.dll` is in stock Eloquence.

This is written for somebody who is not the person who found it. Everything here
was measured rather than assumed, and where something cost hours to learn it says
so, because the same hour is easy to spend twice.

## Where it stands

`lang/jajp` lifts in one pass from the tools: 477 rules, its statement and field
tables, its settings, language 0x80000, and `make missing` answers nothing. Its
rules, globals, lookup sets and settings are all already right.

It is deliberately **not** in the tree. A language module that cannot make an
instance would fail any build that named it, including the CI step that builds
and speaks every module in `lang/`. Lift it when you start:

    python3 tools/gen-globals.py analysis/jajp/glob.obj lang/jajp/delta_globals_jajp.c
    python3 tools/delta-link.py jajp
    python3 tools/delta-sets.py jajp
    python3 tools/lift-ini.py jajp
    python3 tools/delta-emit.py analysis/jajp lang/jajp jajp
    python3 tools/gen-lang.py jajp lang/jajp

## The one line

`rz_isRomExist` in `src/eci_romanizer.c` says family 8 dialect 0 has a
romanizer, and family 8 dialect 0 is Japanese. `rz_getRomanizerInst` always
answers that there is none, because loading one is Win32 `LoadLibrary` work that
was deliberately left on the far side of the porting boundary. So
`rz_setActiveLanguage` returns -1, `rz_setParam` fails on the language, and
`eo_newInstance` refuses.

Make that function answer 0 for family 8 and Japanese speaks: 13,486 samples for
`konnichiwa`. That is not a fix -- it is how to hear that everything else about
the language works. Put it back.

## The oracle, and why it can be trusted

There was no reference for Japanese until 23 August 2026, because one would not
link: IBM's Japanese object set is missing three names. Where each one came from
matters more than that they are now supplied.

`ralStrNicmp` is in `src/port_ral.c` beside `ralStrIcmp`, which already had the
same signature -- a length first, nought meaning the whole string -- and is
called the same way, comparing a phone name against a table of five-byte
entries. The runtime abstraction layer has always been ours on both sides of
every comparison this project makes, so that is the boundary the reference
already stood on rather than a new one.

`getFullPathName` and `__chkstk` are in `reference/jajp_shim.c`, linked for that
one module. They cannot go in the shared layer: every other module defines
`getFullPathName` itself in `libmain.obj` and collides, and a weak alias does not
resolve in PE the way it would in ELF, which is why the shim is a separate
object chosen by `TAG` rather than something cleverer.

**`getFullPathName` must answer an empty string, not nought.** IBM's own is one
line: it returns a global that `DllMain` fills in, and that global is a 260-byte
buffer in the bss, so in a static build with no `DllMain` it answers a pointer to
an empty string. Answering nought instead changes nothing observable today, and
is exactly the kind of difference that makes an oracle worth less than no oracle.
It was written wrong first and found by reading IBM's version rather than by
reasoning about what could need a path.

Build it with:

    make -C reference TAG=jajp BUILD=../build/reference-jajp

## The target

IBM's Japanese engine does speak Japanese script, and **how the instance is made
decides whether it does**:

    romaji,         eciNew()             18,293 samples
    shift-jis kana, eciNew()                  0 samples
    shift-jis kana, eciNewEx(0x80000)    13,266 samples
    ucs-2 kana,     eciNewEx(0x80800)    13,266 samples

`eciNew()` is not the same as `eciNewEx` with the only language the module has.
That is why `reference/speak.c`, which tries `eciNew` first and only falls back,
produces nothing for Japanese and reads like an engine that cannot do it.
Setting the codeset parameter afterwards is refused; the language handed over at
creation is what carries it, in bits eight to fifteen -- which is what
`isUnicodeCodeSet` tests against 0x800.

Romaji and kana do not give the same samples, so the romanizer is converting
rather than passing letters through. That difference is the thing to reproduce.

    make -C reference TAG=jajp BUILD=../build/reference-jajp jptry

`reference/jptry.c` is that driver, kept rather than thrown away, with the table
in its head.

Two harness mistakes cost most of an afternoon there and both are ones this
project has made and written down before. The output-**filename** path is not
the one that works: the engine wants a callback and a sample buffer, as
`speak.c` uses. And `eciSynchronize` does not wait in this engine, so an
instance gets deleted while the synthesis thread is still in it -- pump with
`eciSpeaking` and a sleep. Either one looks like the engine failing on Japanese
when it is the harness failing on everything.

## What is left

About 163 KB of x86 across thirty objects, once the engine objects already
ported and the dictionary lifted as data are taken out. It is a Japanese
morphological analyser, not a lookup table: `Romanizer`, `TextAnalysis`,
`DictMan`, `DictSearch` and `ConverterInterface`, over phrase, bunsetsu and
phoneme structures, with methods like `ChangeYomi` and `GenerateRomajiOutput`.

The objects, with their code sizes, are:

    dictsearch       11484   dictapi          10674   txtanal          12429
    phrasetable      16974   numread          13369   intonphrase      10121
    MakeReadableJP   10087   jpath             9401   jpnrom            9633
    inputchar         9243   unknown           5569   fdictapi          5024
    engread           3897   phrasebuf         3890   userdict          3916
    jpnutil           3920   kanastr           3728   numanal           3133
    convtinterface    2460   PCRoman2BG        2724   kakutei           2186
    TextNormalizer    2146   comppenalty       1776   rominstparam      1716
    inputmngr         1674   rominstance       1358   unicodeconvt      1355
    dictman            629   romreg             180   romedll_link      191
    MakeReadableJP_SPR 1463  MakeReadableLangInt 161

`romedll_link.obj` is worth knowing about: it is what makes the reference
romanise despite loading no DLL, standing in for the library in a static build.
`rominstance.obj` is a pure forwarder -- every method just calls the matching
one on a `ConverterInterface` held at offset 0x10 -- so it does not need
transcribing at all under the decision below.

## The dictionary

2.67 MB in 1,723 packed blobs across forty-eight objects: kanji and their
readings, single-kanji forms, normalisations, an English table, and the
substitution tables that make romaji. It lifts in one command and the lifter is
written and proved:

    python3 tools/lift-rom.py analysis/jajp lang/jajp

None of the record format has to be understood to lift it. The records are bytes
the way `lang/<lang>` is bytes, and the code that reads them is transcribed
separately and reads them exactly as the original does. What does have to be
right is which pointer goes where, and `StaticDict::Initialize` says so itself in
six and a half thousand unrolled stores, each naming an array, an index and a
blob by relocation. The lifter reads those off rather than guessing, the same way
`tools/gen-globals.py` reads the variable area off `delta_new`.

It answers 1,723 blobs and 2,669,092 bytes, which is every byte those objects
hold, and seven arrays whose lengths match the symbol counts exactly: 105
English, 511 kana, 576 normalisations, 389 tankan, 140 tankan kana and one each
of two supplements.

## Decisions already taken

**Call our romanizer directly and retire the vtable slot offsets.** Stas agreed
this on 23 August 2026. `src/eci_romanizer.c` currently reaches a romanizer
through IBM's vtable slots -- `ROM_ADD_TEXT` at 0x0c and the rest -- and those
exist only because IBM loaded a DLL and had to find its entry points at run
time. Since the romanizer would be compiled straight in, direct calls are
honest, drop a good deal of machinery, and cross the same boundary that was
already crossed when the loading half was declared platform code and stubbed.
The alternative considered and rejected was building a C++-ABI-compatible vtable
object so those offsets kept working.

**The dictionary is data, not code.** Lifted verbatim, like every other
language's.

## Where to start

The order that follows from the above, if it helps: get `lang/jajp` lifted and
the oracle built, hear the one-line version speak romaji so the ground is known
to be solid, then work from the outside in -- `ConverterInterface` is the surface
our manager needs, and behind it `Romanizer` is the thing that turns text into
romaji. `kanastr.obj`'s `DictSearch::GenerateKanaString` and `PCRoman2BG` are the
smallest pieces near the actual conversion and the natural place to see the data
format for the first time.

And the standard the rest of this project is held to applies: it is not right
until the samples are identical to IBM's, and a passing check proves nothing
until the new code has been broken on purpose and seen to fail.
