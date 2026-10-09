## Since v0.3

v0.3 was US English alone. Every library here carries ten languages now, and a good deal has been fixed that anything driving the engine from a screen reader will meet.

- `eciStop`, and answering `eciDataAbort` from the callback, no longer fault or leave the instance silent, whether an utterance is being made or has just finished (#2, #35).
- The thirty-two bit builds, the `eci-x86` library among them, now survive all 20,526 strings IBM's engine dies on, as the sixty-four bit ones do. Before, every one of them killed the process.
- A dictionary of 69,000 entries loads in a fifth of a second rather than minutes, and adding words one at a time no longer slows as the dictionary grows (#25).
- A program that calls into the engine from a new thread each time no longer runs it out of memory after sixty-odd calls.
- `eciCopyVoice` refuses a voice number the caller does not own, rather than writing outside the instance.
- The thirty-two bit library exports its names stdcall, as IBM's does.
- A sample rate above 11,025 is the same voice raised to that rate, rather than 11,025 samples labelled as more.
- English reads 1,000,000 as one million and Windows10 as Windows ten, where IBM's engine said "one million comma hundred" and spelled the word out.

## What is in it

`eci.dll` exports the names IBM published, so a program written against IBM's library -- a screen reader add-on, for instance -- can load ours instead. It wants nothing but the system's own DLLs, and `eci.ini` goes beside it because add-ons look for one.

There is one of each bitness, in `eci-x86_64` and `eci-x86`, and which you want depends on the add-on rather than on Windows. An add-on that loads the engine into the screen reader's own process wants the reader's bitness -- sixty-four bit for NVDA 2026. The most used driver, davidacm/NVDA-IBMTTS-Driver, hosts the engine in a thirty-two bit process of its own whatever the reader is, so that one wants `eci-x86`. Copy the contents of the folder, not the folder.

On Linux the same names are in `libeci.so.1`, with `libeci.so` beside it and `eci.h` to compile against. `docs/using.md` is how to use it, `docs/api.md` what every call does, and `docs/quirks.md` what will trip you.

`openevv-say "Hello from OpenEVV."` speaks from the Linux command line through the first available ordinary audio client: `pw-play`, `paplay`, then `aplay`. `openevv-say -w hello.wav "Hello from OpenEVV."` saves the same speech instead. The lower-level `evv` command is beside it for programs that want a WAV file or pipe directly.

Every library here carries all ten languages: US and British English, German, Castilian and American Spanish, French and Canadian French, Italian, Japanese and Polish. `eciGetAvailableLanguages` answers all of them, US English is what a new instance speaks, and `eciSetParam(h, eciLanguageDialect, ...)` on an instance that is not speaking changes it. All ten can be alive in one process at once, each saying what it says alone.

`evvspeak.exe` is the speak window: type something, pick one of the eight voices, set the rate in words a minute, and hear it. `evv.exe` is the same engine on the command line. Both are one file, sixty-four bit, and want nothing installed.

`openevv-0.4.nvda-addon` is the engine as a synthesiser for NVDA, listed there as OpenEVV. It carries both bitnesses and loads the engine into NVDA's own process, so nothing else needs installing. Every language is offered as a voice, and a document that says part of itself is in another language is read in it. Cancelling speech waits for the utterance in flight to finish synthesising, which is a fraction of a second for a line and longer for a long chat message.

The engine is held to its own recorded answers over 997 test cases in ten languages, and twenty thousand words of each language but Polish and Japanese, on every build. It sounds like IBM's Embedded ViaVoice, and where it differs on purpose `docs/quirks.md` says so: twenty-two places, two of them the English readings above.

NOTICE says which parts of this are ours and which are IBM's: the engine is a reimplementation, and the language data it speaks with is IBM's, which the MIT licence does not cover.
