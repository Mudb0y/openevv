## Since v0.6

This one is mostly about speaking through Speech Dispatcher.

- With the wideband voice, or at any sample rate above 11,025, the last nine milliseconds of an utterance stayed inside the engine until the next one began, and were heard at its start. Through Speech Dispatcher that clipped every letter of typing echo and played its end under the next letter, and speech after a stop opened with a scrap of what was stopped. The end of an utterance now comes out with it, and the next one starts from silence.
- The Speech Dispatcher module hands the engine what Speech Dispatcher keeps for the synthesiser, where it used to turn every symbol but sentence punctuation into a space. So "(unread)" and "artist - album" pause as at a comma, 3-5 is three to five, and $5.50 is five dollars and fifty cents. A symbol some language would read aloud is still left out: a currency sign with no number beside it, a bracket with a space on its inner side or a word joined to its outer side, a lone angle bracket, and the French quotation marks.
- Square, curly and angle brackets around an aside pause as round ones do, in every language but Japanese, where before they ran on into the words either side; French and Italian no longer read the closing bracket of "<non lus>" as greater than. x[0] and f<x>, a lone < or >, spelling and verbatim modes, and phonetic spellings are as they were. Speech Dispatcher's own English symbol table drops square and curly brackets at punctuation levels none and some before the module sees them, so through it they pause only once your symbols file marks them always, as it marks round ones.
- On sixty-four bit Windows, the engine puts back every register Windows makes a function keep when a rule backtracks, where it put back only the integer ones. Compiled rules keep some values in SSE registers, so a rule could find a deeper rule's values in place of its own. No test case changed, but nothing kept one from doing so.

## What is in it

`eci.dll` exports the names IBM published, so a program written against IBM's library -- a screen reader add-on, for instance -- can load ours instead. It wants nothing but the system's own DLLs, and `eci.ini` goes beside it because add-ons look for one.

There is one of each bitness, in `eci-x86_64` and `eci-x86`, and which you want depends on the add-on rather than on Windows. An add-on that loads the engine into the screen reader's own process wants the reader's bitness -- sixty-four bit for NVDA 2026. The most used driver, davidacm/NVDA-IBMTTS-Driver, hosts the engine in a thirty-two bit process of its own whatever the reader is, so that one wants `eci-x86`. Copy the contents of the folder, not the folder.

On Linux the same names are in `libeci.so.1`, with `libeci.so` beside it and `eci.h` to compile against. `docs/using.md` is how to use it, `docs/api.md` what every call does, and `docs/quirks.md` what will trip you.

`openevv-say "Hello from OpenEVV."` speaks from the Linux command line through the first available ordinary audio client: `pw-play`, `paplay`, then `aplay`. `openevv-say -w hello.wav "Hello from OpenEVV."` saves the same speech instead. The lower-level `evv` command is beside it for programs that want a WAV file or pipe directly.

Every library here carries all twelve languages: US and British English, German, Castilian and American Spanish, French and Canadian French, Italian, Japanese, Polish, Finnish and Brazilian Portuguese. `eciGetAvailableLanguages` answers all of them, US English is what a new instance speaks, and `eciSetParam(h, eciLanguageDialect, ...)` on an instance that is not speaking changes it. All twelve can be alive in one process at once, each saying what it says alone.

`evvspeak.exe` is the speak window: type something, pick one of the eight voices, set the rate in words a minute, and hear it. `evv.exe` is the same engine on the command line. Both are one file, sixty-four bit, and want nothing installed.

`openevv-0.6.1.nvda-addon` is the engine as a synthesiser for NVDA, listed there as OpenEVV. It carries both bitnesses and loads the engine into NVDA's own process, so nothing else needs installing. Every language is offered as a voice, and a document that says part of itself is in another language is read in it. Cancelling speech waits for the utterance in flight to finish synthesising, which is a fraction of a second for a line and longer for a long chat message.

The engine is held to its own recorded answers over 1,291 test cases in twelve languages, and twenty thousand words of every language but Japanese, on every build. It sounds like IBM's Embedded ViaVoice, and where it differs on purpose `docs/quirks.md` says so; one of those places is new in this release: square, curly and angle brackets around an aside are read as round ones.

NOTICE says which parts of this are ours and whose the rest are: the engine is a reimplementation, the language data it speaks with is IBM's, and Finnish, Brazilian Portuguese and the rules both Englishes took from ETI Eloquence 6.1 are ETI's. The MIT licence covers none of those. It also says where US English's dictionary came from: the community's IBMTTSDictionaries and the Moby Pronunciator, both in the public domain, and CMU's dictionary of 1993.
