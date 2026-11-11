## Since v0.6.2

This one brings OpenEVV to Android, and a great deal of English.

- **Android.** OpenEVV is now a voice for TalkBack and anything else on Android, a phone, a tablet or a Wear OS watch, with all twelve languages and the same eight voices. It came in from Quin Gillespie's evvdroid, history and all, with Aryan Choudhary's, aman kumar gupta's and adil-adysh's work in it. It reads what the engine reads on Linux and in NVDA and rewrites nothing of its own on the way, so what it gets wrong is fixed in the engine for everyone. It checks these releases for a newer one and offers to install it, or checks the development build of main if you choose that channel in its settings. Take `openevv-v0.7-android-arm64-v8a.apk` for nearly any phone, `armeabi-v7a` for older phones and some watches, `x86_64` for an emulator, or `universal` if unsure; `docs/android.md` says how to turn it on and, before that, how to turn it off.
- A run of 131 digits or more no longer kills either English. It took Speech Dispatcher's module down on a page with a long number on it; IBM's own engine fails the same way at about two hundred. Twenty thousand digits are read now.
- After the engine gave up on a piece of text rather than crash on it, the instance that had read it never spoke again, and nothing said so: through Speech Dispatcher a voice that was still running and silent. It now speaks the next thing it is given.
- A version with a hyphen and a number after it, the way a package's revision is written, is read as one: 1.2.3-1 is one point two point three dash one and 6.6.30-2 six point six point three oh dash two, in both Englishes. They had been taken for sums, and the second point and the number after it came out spelled letter by letter; 1.2.3+1 and 1.2.3/1 had the same trouble.
- The names a Linux user meets all day are said the way their makers say them, in whatever case they are written: NixOS and nixos, Qt as cute, Gitea, gopls, mypy. A command the rules ran together is said by its parts or its letters, iwctl and evv among them.
- A file's ending is said as the word it is where it is one, so flake.nix, error.log and go.mod are no longer spelled, and a file name keeps its dot after a name that ends in a spelled letter: nginx.conf, cpu.c.
- A word English could not possibly say, such as a shortened name in a file of source code, is cut into the pieces it can be said in rather than run together into one sound.
- Short abbreviations the rules read as words are said by their letters: cpu, eci, fyi, ROI, CEO. CI is no longer sigh, and SMSes is S M S's.
- Compounds are said as their two words, framebuffer and swapfile among them, more of them are split, and 27 Greek and Latin combining forms are read as prefixes. A compound written with a capital at the join, PowerShell or WordPress, is one word rather than two.
- Mr., Mrs. and Ms. are words, and the full stop after them no longer ends the sentence.
- read is reed where it is a noun, a quick read, and red after already.
- A word written in small letters in the dictionary is found when it starts a sentence with a capital, and a word written with an accent is found where it never was.

## What is in it

`eci.dll` exports the names IBM published, so a program written against IBM's library -- a screen reader add-on, for instance -- can load ours instead. It wants nothing but the system's own DLLs, and `eci.ini` goes beside it because add-ons look for one.

There is one of each bitness, in `eci-x86_64` and `eci-x86`, and which you want depends on the add-on rather than on Windows. An add-on that loads the engine into the screen reader's own process wants the reader's bitness -- sixty-four bit for NVDA 2026. The most used driver, davidacm/NVDA-IBMTTS-Driver, hosts the engine in a thirty-two bit process of its own whatever the reader is, so that one wants `eci-x86`. Copy the contents of the folder, not the folder.

On Linux the same names are in `libeci.so.1`, with `libeci.so` beside it and `eci.h` to compile against. `docs/using.md` is how to use it, `docs/api.md` what every call does, and `docs/quirks.md` what will trip you.

`openevv-say "Hello from OpenEVV."` speaks from the Linux command line through the first available ordinary audio client: `pw-play`, `paplay`, then `aplay`. `openevv-say -w hello.wav "Hello from OpenEVV."` saves the same speech instead. The lower-level `evv` command is beside it for programs that want a WAV file or pipe directly.

Every library here carries all twelve languages: US and British English, German, Castilian and American Spanish, French and Canadian French, Italian, Japanese, Polish, Finnish and Brazilian Portuguese. `eciGetAvailableLanguages` answers all of them, US English is what a new instance speaks, and `eciSetParam(h, eciLanguageDialect, ...)` on an instance that is not speaking changes it. All twelve can be alive in one process at once, each saying what it says alone.

`openevv-v0.7-android-*.apk` is the engine as an Android voice, one package for each kind of processor and one for all three. Every one is signed with the same key, so each installs over the one before it, and its application is `com.mudb0y.openevv`, so evvdroid can stay installed beside it.

`evvspeak.exe` is the speak window: type something, pick one of the eight voices, set the rate in words a minute, and hear it. `evv.exe` is the same engine on the command line. Both are one file, sixty-four bit, and want nothing installed.

`openevv-0.7.nvda-addon` is the engine as a synthesiser for NVDA, listed there as OpenEVV. It carries both bitnesses and loads the engine into NVDA's own process, so nothing else needs installing. Every language is offered as a voice, and a document that says part of itself is in another language is read in it. Cancelling speech waits for the utterance in flight to finish synthesising, which is a fraction of a second for a line and longer for a long chat message.

The engine is held to its own recorded answers over 1,327 test cases in twelve languages, and twenty thousand words of every language but Japanese, on every build. It sounds like IBM's Embedded ViaVoice, and where it differs on purpose `docs/quirks.md` says so; nine of those places are new in this release, every one of them in English but the stack a long run of digits needed.

NOTICE says which parts of this are ours and whose the rest are: the engine is a reimplementation, the language data it speaks with is IBM's, and Finnish, Brazilian Portuguese and the rules the other languages took from ETI Eloquence 6.1 are ETI's. The MIT licence covers none of those. It also says that the Android voice is Quin Gillespie's and evvdroid's contributors', under the same MIT licence as ours, and where US English's dictionary came from: the community's IBMTTSDictionaries and the Moby Pronunciator, both in the public domain, and CMU's dictionary of 1993.
