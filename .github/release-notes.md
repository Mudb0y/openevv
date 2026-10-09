## Since v0.4

This one is mostly about how the engine sounds by default and about the NVDA add-on. Three defaults change, so it will not sound exactly like v0.4: phrase prediction and the abbreviation dictionary are off, and both Englishes read punctuation and odd words the way ETI Eloquence 6.1 does.

- The add-on reads pronunciation dictionaries, the community's IBMTTSDictionaries among them (#48). Choose a set from Eloquence Dictionary Manager under "Dictionary set" in the voice settings, or put `.dic` files in a folder called `openevv` in your NVDA user configuration folder. Entries added in the manager's editor are used too, and changed files are picked up within a few seconds.
- A dictionary now survives a change of language. Before, making one dictionary per language and putting one back after switching language crashed the process, which is what the IBMTTS and Eloquence 64 drivers do with dictionaries loaded.
- A dictionary path of 264 bytes or more is refused rather than overflowing the stack.
- Phrase prediction, the pause the engine guesses into a long clause with no punctuation, is off unless asked for (#41). The add-on's "Phrase prediction" checkbox brings it back, and so do `` `pp1 `` in the text and `eciSetDefaultParam(11, 1)`.
- The abbreviation dictionary is off unless asked for, so "Dr. Smith" is read as written rather than as doctor Smith. The add-on's "Expand abbreviations" checkbox or `eciSetParam(h, eciDictionary, 0)` turns it on. With it off, "Mr." and "Dr." end a sentence, as they always did in IBM's engine with it off.
- US and British English read a lone sentence mark as punctuation rather than naming it, outside verbatim mode, as 6.1 does (#4). The other languages still name it, as IBM's engine did.
- US and British English normalise text as 6.1 does: utf8, ipv6 and amd64 are words, a web or email address is read a part at a time with the domain said as a word, and a path is read slash by slash.
- An optional wideband voice above 11 kHz: the 11,025 voice below about 5.4 kHz, and frication and breath above it from a second synthesiser. It is the add-on's "Wideband above 11 kHz" checkbox, parameter 32 through the library, and `evv -W` on the command line. Off, which is the default, every sample rate above 11,025 is still the same voice raised (#37, #40).
- Everything above 11,025 is now whole-number arithmetic, so the thirty-two bit builds give the same samples as the sixty-four bit ones there, and a raised rate costs a fifth of the processor time it did.
- In the add-on, a rate set for typed characters by another add-on no longer sticks to everything after it (#42).
- It is called OpenEVV everywhere, the add-on in NVDA's synthesiser list included (#47).

## What is in it

`eci.dll` exports the names IBM published, so a program written against IBM's library -- a screen reader add-on, for instance -- can load ours instead. It wants nothing but the system's own DLLs, and `eci.ini` goes beside it because add-ons look for one.

There is one of each bitness, in `eci-x86_64` and `eci-x86`, and which you want depends on the add-on rather than on Windows. An add-on that loads the engine into the screen reader's own process wants the reader's bitness -- sixty-four bit for NVDA 2026. The most used driver, davidacm/NVDA-IBMTTS-Driver, hosts the engine in a thirty-two bit process of its own whatever the reader is, so that one wants `eci-x86`. Copy the contents of the folder, not the folder.

On Linux the same names are in `libeci.so.1`, with `libeci.so` beside it and `eci.h` to compile against. `docs/using.md` is how to use it, `docs/api.md` what every call does, and `docs/quirks.md` what will trip you.

`openevv-say "Hello from OpenEVV."` speaks from the Linux command line through the first available ordinary audio client: `pw-play`, `paplay`, then `aplay`. `openevv-say -w hello.wav "Hello from OpenEVV."` saves the same speech instead. The lower-level `evv` command is beside it for programs that want a WAV file or pipe directly.

Every library here carries all ten languages: US and British English, German, Castilian and American Spanish, French and Canadian French, Italian, Japanese and Polish. `eciGetAvailableLanguages` answers all of them, US English is what a new instance speaks, and `eciSetParam(h, eciLanguageDialect, ...)` on an instance that is not speaking changes it. All ten can be alive in one process at once, each saying what it says alone.

`evvspeak.exe` is the speak window: type something, pick one of the eight voices, set the rate in words a minute, and hear it. `evv.exe` is the same engine on the command line. Both are one file, sixty-four bit, and want nothing installed.

`openevv-0.5.nvda-addon` is the engine as a synthesiser for NVDA, listed there as OpenEVV. It carries both bitnesses and loads the engine into NVDA's own process, so nothing else needs installing. Every language is offered as a voice, and a document that says part of itself is in another language is read in it. Cancelling speech waits for the utterance in flight to finish synthesising, which is a fraction of a second for a line and longer for a long chat message.

The engine is held to its own recorded answers over 1,073 test cases in ten languages, and twenty thousand words of each language but Polish and Japanese, on every build. It sounds like IBM's Embedded ViaVoice, and where it differs on purpose `docs/quirks.md` says so; four of those places are new in this release: phrase prediction, abbreviations, and the two ways the Englishes now read as 6.1 does.

NOTICE says which parts of this are ours and whose the rest are: the engine is a reimplementation, the language data it speaks with is IBM's, and the rules both Englishes took from ETI Eloquence 6.1 are ETI's. The MIT licence covers none of those.
