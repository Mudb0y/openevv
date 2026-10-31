## Since v0.5

This one is mostly about how US English says words, and about two new languages. US English carries a pronunciation dictionary of its own and reads much of its text the way ETI Eloquence 6.1 does, and Finnish and Brazilian Portuguese, which IBM never shipped for this engine, come whole from 6.1. It will not sound exactly like v0.5.

- Finnish and Brazilian Portuguese, both lifted whole from ETI Eloquence 6.1. They give 6.1's phonemes for every test sentence and for twenty thousand words of each, and the add-on offers both as voices.
- US English carries a pronunciation dictionary in the library itself, always on. It began as the community's IBMTTSDictionaries, about seventy thousand entries written for this engine by the people who listen to it, and it is now OpenEVV's own: entries that the CMU Pronouncing Dictionary and Merriam-Webster showed to be wrong for this engine are out, the community's changes up to 30 October are in, and so are words of ours. A dictionary you load still wins over it.
- In the add-on, a dictionary set whose source is IBMTTSDictionaries is no longer used for US English, since its entries are built in already and loading the set over them would put back the ones taken out. Sets for other languages, and entries of your own, are used as before.
- Software names are said the way their makers say them, where the maker publishes a reading: nginx as engine X, Qt as cute, and etcd, Vitest, uv, PyPI and gunicorn as their projects give them. A name shortened out of words is said by its letters rather than by the words it stands for, so kubectl is kube C T L and dmesg is D M E S G, and what you hear is what you would type.
- A word with a capital inside it is read as the words it is made of, in both Englishes, so NixOS, ChatGPT and HTMLElement are Nix OS, Chat GPT and HTML Element.
- US English's words are a merge of IBM's 4.3 and 6.1, decided word by word against the CMU Pronouncing Dictionary and, where it came down to taste, by ear. Ordinary words that were filed as names are gone, so composed is no longer COM-pust and poor is no longer pore; homographs such as transport, produce and wound are told apart more often; and neuroscience and the other neuro words are right.
- Words the rules said clearly wrongly against their published pronunciations are mended, by entries from public-domain sources and by a letter rule that says ng without its g before another consonant, as in Lexingtonian. Sixty words take Merriam-Webster's stress.
- The possessive of a word the dictionary spells in phonemes is read whole in both Englishes, where Frey's used to be fry ess.
- US English reads numbers more as 6.1 does. Money says the currency after the number, so US$5.50 is five US dollars and fifty cents and EUR 20.50 is twenty euros and fifty cents. A nought in a phone number is oh. The day after a month name is an ordinal, as in due September thirtieth, and 31 December 1999 is December thirty-first. Three or more numbers joined by hyphens say dash at every hyphen, so a date written 2026-10-11 is no longer read as a range; two numbers alone still are.
- In US English, :) and :( are smiley face and sad face.
- The engine no longer pauses for as long as at a full stop wherever text stops without punctuation, which is most of what a screen reader sends, so moving down a list or through a word by character is quicker (#18). Parameter 33, eciPauseMode, takes the values the IBMTTS and Eloquence 64 drivers give their own setting: 0 never shortens, 1 shortens at the end of text only, which is the default, and 2 always. The add-on has it as "Shorten pauses" in the voice settings.
- On Windows, a program that has spoken through eci.dll no longer hangs or crashes when it exits or lets the library go, which v0.4 and v0.5 did. eci.dll's version resource names the release now; v0.4 and v0.5 said 0.3.0.0.
- The wideband voice's vowels go on above the join instead of stopping at it, and the click it made after every utterance and at pauses is gone.
- Text that IBM's engine dies on no longer crashes ours when a short pause follows it, which is what the IBMTTS and Eloquence 64 drivers write at the end of every line.
- On sixty-four bits, eciSetDict with no dictionary after phonemes had been reported crashed the process. It no longer does.

## What is in it

`eci.dll` exports the names IBM published, so a program written against IBM's library -- a screen reader add-on, for instance -- can load ours instead. It wants nothing but the system's own DLLs, and `eci.ini` goes beside it because add-ons look for one.

There is one of each bitness, in `eci-x86_64` and `eci-x86`, and which you want depends on the add-on rather than on Windows. An add-on that loads the engine into the screen reader's own process wants the reader's bitness -- sixty-four bit for NVDA 2026. The most used driver, davidacm/NVDA-IBMTTS-Driver, hosts the engine in a thirty-two bit process of its own whatever the reader is, so that one wants `eci-x86`. Copy the contents of the folder, not the folder.

On Linux the same names are in `libeci.so.1`, with `libeci.so` beside it and `eci.h` to compile against. `docs/using.md` is how to use it, `docs/api.md` what every call does, and `docs/quirks.md` what will trip you.

`openevv-say "Hello from OpenEVV."` speaks from the Linux command line through the first available ordinary audio client: `pw-play`, `paplay`, then `aplay`. `openevv-say -w hello.wav "Hello from OpenEVV."` saves the same speech instead. The lower-level `evv` command is beside it for programs that want a WAV file or pipe directly.

Every library here carries all twelve languages: US and British English, German, Castilian and American Spanish, French and Canadian French, Italian, Japanese, Polish, Finnish and Brazilian Portuguese. `eciGetAvailableLanguages` answers all of them, US English is what a new instance speaks, and `eciSetParam(h, eciLanguageDialect, ...)` on an instance that is not speaking changes it. All twelve can be alive in one process at once, each saying what it says alone.

`evvspeak.exe` is the speak window: type something, pick one of the eight voices, set the rate in words a minute, and hear it. `evv.exe` is the same engine on the command line. Both are one file, sixty-four bit, and want nothing installed.

`openevv-0.6.nvda-addon` is the engine as a synthesiser for NVDA, listed there as OpenEVV. It carries both bitnesses and loads the engine into NVDA's own process, so nothing else needs installing. Every language is offered as a voice, and a document that says part of itself is in another language is read in it. Cancelling speech waits for the utterance in flight to finish synthesising, which is a fraction of a second for a line and longer for a long chat message.

The engine is held to its own recorded answers over 1,291 test cases in twelve languages, and twenty thousand words of every language but Japanese, on every build. It sounds like IBM's Embedded ViaVoice, and where it differs on purpose `docs/quirks.md` says so; four of those places are new in this release: the shortened pause, the merge of US English's words, the dictionary it carries, and the possessive of a word spelled in phonemes.

NOTICE says which parts of this are ours and whose the rest are: the engine is a reimplementation, the language data it speaks with is IBM's, and Finnish, Brazilian Portuguese and the rules both Englishes took from ETI Eloquence 6.1 are ETI's. The MIT licence covers none of those. It also says where US English's dictionary came from: the community's IBMTTSDictionaries and the Moby Pronunciator, both in the public domain, and CMU's dictionary of 1993.
