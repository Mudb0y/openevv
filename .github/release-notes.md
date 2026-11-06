## Since v0.6.1

This one is mostly about what a screen reader hands the engine.

- A long sentence handed over in one piece, as a screen reader hands over a paragraph, no longer breaks in the middle of a phrase with a full stop's pause and a fresh start of the melody. IBM's engine ended a sentence for its length about 360 characters in; every language now goes on as far as ETI Eloquence 6.1 does, more than twice as far. danielw97 found the limit (issue 54).
- With a large dictionary loaded, switching NVDA to another synthesiser took seconds, and changing a dictionary file left the voice silent for as long; on a Raspberry Pi it took minutes. A dictionary of 69,000 entries is now let go of in a hundredth of a second. aaron-gh traced it (issue 49).
- In English ñ is n and y, so El Niño and España are said as they are spelled, and the dictionaries find the words they hold written with ñ: niño, piñata, Buñuel (issue 50).
- A punctuation mark standing on its own is read as 6.1 reads it in German, both Spanishes, both Frenches, Italian and Polish, as it has been in English since 0.5: a lone semicolon is punctuation rather than Semikolon or point-virgule (issue 4).
- Local Disk (C:), as Windows names a drive, is C and not C smiley face. A smiley is read only where it stands on its own, at the start of the text or after a space, as in 6.1.
- A lone hyphen through NVDA says dash once rather than dash dash, and an en dash says en dash rather than en dash dash. A hyphen at the end of a word, or starting a word after the first as in ls -la, is no longer named outside the spelling modes in English, German, French, Italian and Polish, which is how 6.1 reads it. Polish had been naming it in Italian.
- gif is said with a hard g.
- Through Speech Dispatcher, a currency sign with no number beside it is read, so the $ of $HOME or of a shell prompt says dollar below punctuation level all too.

## What is in it

`eci.dll` exports the names IBM published, so a program written against IBM's library -- a screen reader add-on, for instance -- can load ours instead. It wants nothing but the system's own DLLs, and `eci.ini` goes beside it because add-ons look for one.

There is one of each bitness, in `eci-x86_64` and `eci-x86`, and which you want depends on the add-on rather than on Windows. An add-on that loads the engine into the screen reader's own process wants the reader's bitness -- sixty-four bit for NVDA 2026. The most used driver, davidacm/NVDA-IBMTTS-Driver, hosts the engine in a thirty-two bit process of its own whatever the reader is, so that one wants `eci-x86`. Copy the contents of the folder, not the folder.

On Linux the same names are in `libeci.so.1`, with `libeci.so` beside it and `eci.h` to compile against. `docs/using.md` is how to use it, `docs/api.md` what every call does, and `docs/quirks.md` what will trip you.

`openevv-say "Hello from OpenEVV."` speaks from the Linux command line through the first available ordinary audio client: `pw-play`, `paplay`, then `aplay`. `openevv-say -w hello.wav "Hello from OpenEVV."` saves the same speech instead. The lower-level `evv` command is beside it for programs that want a WAV file or pipe directly.

Every library here carries all twelve languages: US and British English, German, Castilian and American Spanish, French and Canadian French, Italian, Japanese, Polish, Finnish and Brazilian Portuguese. `eciGetAvailableLanguages` answers all of them, US English is what a new instance speaks, and `eciSetParam(h, eciLanguageDialect, ...)` on an instance that is not speaking changes it. All twelve can be alive in one process at once, each saying what it says alone.

`evvspeak.exe` is the speak window: type something, pick one of the eight voices, set the rate in words a minute, and hear it. `evv.exe` is the same engine on the command line. Both are one file, sixty-four bit, and want nothing installed.

`openevv-0.6.2.nvda-addon` is the engine as a synthesiser for NVDA, listed there as OpenEVV. It carries both bitnesses and loads the engine into NVDA's own process, so nothing else needs installing. Every language is offered as a voice, and a document that says part of itself is in another language is read in it. Cancelling speech waits for the utterance in flight to finish synthesising, which is a fraction of a second for a line and longer for a long chat message.

The engine is held to its own recorded answers over 1,327 test cases in twelve languages, and twenty thousand words of every language but Japanese, on every build. It sounds like IBM's Embedded ViaVoice, and where it differs on purpose `docs/quirks.md` says so; two of those places are new in this release: a long sentence is cut where 6.1 cuts it, and ñ in English is n and y.

NOTICE says which parts of this are ours and whose the rest are: the engine is a reimplementation, the language data it speaks with is IBM's, and Finnish, Brazilian Portuguese and the rules the other languages took from ETI Eloquence 6.1 are ETI's. The MIT licence covers none of those. It also says where US English's dictionary came from: the community's IBMTTSDictionaries and the Moby Pronunciator, both in the public domain, and CMU's dictionary of 1993.
