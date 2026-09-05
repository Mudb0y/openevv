# The published interface

What a program calls, what each call takes, what it answers, and where this engine answers differently from IBM's. `docs/using.md` is how to get the library into a build and drive it; `docs/quirks.md` is the short list of things that will cost you an afternoon. This file is the reference.

It is written from the code rather than from IBM's documentation. Where the two disagree the code is what happens, and this says so against the call.

## The header and the libraries

`include/eci.h` declares everything below. It is the only file a program needs to include and it declares nothing of the engine's insides. `lib/eci_api.c` is built from it, so what the header says and what the libraries export cannot drift apart.

Three things come out of a build:

    make so       build/libeci.so.1, and build/libeci.so beside it
    make win      build/eci.dll, sixty-four bit
    make win32    build/eci32.dll, thirty-two bit

All three export the same names out of the same `lib/eci_api.c`. `make install-lib` puts the shared library and the header under `PREFIX`. A program may also link `build/libevv.a` directly, which is what `cli/evv.c` does, but then it is calling the engine's own short names rather than these; the published names live in the library.

`ECICALL` in the header is `__stdcall` on Windows and nothing elsewhere. On thirty-two bit Windows that is not decoration: IBM's own objects export `_eciSetParam@12` and `_eciAddText@8`, so the byte count is part of the name and the convention is stdcall. A caller that gets it wrong leaves the stack out by the size of the arguments after every call. The libraries publish the names undecorated all the same, because that is what a caller asking by name asks for.

## The shape of a session

Every program that gets sound out of this engine does these things in this order. Everything else is detail.

    unsigned int langs[16];
    int n = 0;
    short buffer[2048];

    eciGetAvailableLanguages(0, &n);          /* how many */
    eciGetAvailableLanguages(langs, &n);      /* which */

    ECIHand h = eciNewEx((int)langs[0]);
    eciRegisterCallback(h, on_message, 0);
    eciSetOutputBuffer(h, 2048, buffer);
    eciAddText(h, "Hello.");
    eciSynthesize(h);
    while (eciSpeaking(h))
        sleep_a_little();
    eciDelete(h);

The callback is where the samples arrive:

    static int ECICALL on_message(ECIHand h, ECIMessage msg, int param,
                                  void *data)
    {
        if (msg == eciWaveformBuffer)
            take(buffer, param);      /* param samples, not bytes */
        return eciDataProcessed;
    }

**Registering the buffer is not optional.** An instance starts out sending its samples to an audio device; the platform layer answers that this machine has none, which is what sends the engine down the buffer path in the first place. Until a buffer is registered the instance has nowhere to put anything, and `eciAddText` answers nought. It is the only call that says so: `eciSynthesize` and `eciSynchronize` both answer success on an instance that will never speak, and `eciSpeaking` answers that it is not.

## Instances

`eciNew` makes an instance in whichever language the build names first. `eciNewEx` takes one of the numbers `eciGetAvailableLanguages` answered with. Both answer a handle or nothing.

An instance is not cheap. It starts a synthesis thread of its own, and that thread takes a four megabyte frame stack the first time it runs a rule, which it keeps until the thread ends. Make one and keep it. A program that makes and throws away instances in a loop is fine now -- `make instances 200` runs two hundred rounds -- but it was not always, and each one still leaks sixty-four bytes.

`eciDelete` ends it and gives back what it held. It answers a handle in IBM's declaration, which is always nothing.

`eciReset` puts an instance back to the settings a new one would have. `eciTestPhrase` says "1 2 3." in the first standard voice, which is what it is for.

Calls are refused rather than serialised while another call on the same instance is running. There is no lock: a second thread calling in during the first call is turned away, and `eciIsBeingReentered` was published to say so and always answers nought.

## Languages

    int eciGetAvailableLanguages(unsigned int *languages, int *count);

Asked with no room it answers how many languages the build has; asked with room it fills that many in and says how many it wrote. Either way the count travels in the same place. Asking for the count with room left over is a parameter error, which is IBM's behaviour too.

A language is one word: the family in the top half, the code set in the third byte and the dialect in the bottom one, so US English is 0x00010000 and British English is 0x00010001. `eci.h` names the families IBM numbered, but the constants name families rather than promise them -- a build has as many language modules as it was given, and `eciGetAvailableLanguages` is the only honest source. `make LANGS="lang/enus lang/dede"` puts two in one binary, and the first named is what `eciNew` gives.

Family seventeen is Polish here and Thai in IBM's tables. That is the twelfth deliberate divergence and it costs nothing, there being no Thai in the SDK this engine came out of.

The code set travels in that same word rather than in a setting of its own. ORing `eciUnicodeCodeSet` -- 0x800 -- into the language says that text handed over is UTF-16 rather than bytes, and `eciLanguageDialect` is what the engine reads to find out, not `eciTextMode`.

## Text in

    int eciAddText(ECIHand h, const void *text);
    int eciSynthesize(ECIHand h);
    int eciInsertIndex(ECIHand h, int index);
    int eciClearInput(ECIHand h);

`eciAddText` adds to what will be said next; `eciSynthesize` starts saying it. Several `eciAddText` calls before one `eciSynthesize` is the ordinary way to build an utterance.

`eciClearInput` throws away what has been added and not yet spoken, and what that means depends on `eciSynthMode`: it empties the queue that mode one builds, and in mode nought -- the default -- the text has already gone down to the engine and there is nothing here to empty. `eciStop` is what forgets it there.

`eciInsertIndex` puts a numbered mark in the stream. When the engine reaches it the callback is told `eciIndexReply` with that number in `param`, and `eciGetIndex` answers the last one reached. This is how a program follows where in the text the speech has got to.

With `eciSynthMode` set to one, each `eciAddText` is queued with the settings that were in force at the moment it arrived, so a program can set a voice, add words, set another voice, add more, and have each stretch spoken in the voice it was written under. With it at nought -- the default -- the text goes straight down and the settings that matter are the ones in force when synthesis starts.

Text is bytes in the language's own code set, which for eight of the nine languages IBM shipped is the Windows Western set. It is not UTF-8: the engine reads single bytes and IBM's engine does almost nothing between the caller's bytes and the machine's characters. Japanese is the ninth and is the exception: its text is Shift-JIS, EUC-JP or one of three seven-bit JIS sets, and its romanizer recodes whichever it was given. A language that declares characters of its own -- which is Polish, and none of IBM's nine -- gets its text converted from UTF-8 on the way in, and that guard is the fourth and fifth deliberate divergences.

    int eciSpeakText(const void *text, int annotations);
    int eciSpeakTextEx(const void *text, int annotations, int language);

These make a whole instance, say one thing and take it away. They register no callback and no buffer, so the instance they make has nowhere to send samples, its `eciAddText` refuses, and both calls answer nought having said nothing -- measured, on a build with English in it. They are not the short way to speak; they are a way to make a synthesis thread and a four megabyte frame stack and throw both away.

## Annotations

An annotation is a backtick, a letter or two, and usually a number. They are read only when `eciInputType` is one, and a backslash before a backtick is a literal backtick. The engine acts on them itself, so they change what is being spoken from that point on rather than for one word.

The voice ones take the same numbers `eciSetVoiceParam` does. `` `vs `` is speed, `` `vb `` pitch baseline, `` `vf `` fluctuation, `` `vr `` roughness, `` `vh `` head size, `` `vv `` volume and `` `vg `` gender; `` `v `` followed by a digit selects one of the eight voices, so `` `v2 `` is voice two. Several of them also take a name rather than a number -- `` `vsfast ``, `` `vsslow ``, `` `vsmed `` -- and a relative form: `` `vs%+25 `` is a quarter faster than now, `` `vbst-6 `` is six semitones down, `` `vbhz120 `` is a baseline in hertz and `` `vswpm180 `` a speed in words a minute.

`` `0 `` to `` `4 `` are pauses of increasing length and `` `p `` followed by a number is a pause in milliseconds, so `` `p300 `` is three hundred of them.

`` `ui"name" `` is an index mark carrying a string rather than a number, and `` `aud"name" `` an audio marker. Both are reported through the callback.

The bracketed ones say how to read the text they enclose: `` `ord[ `` for a number read out in full, `` `tel[ `` and `` `telpunc[ `` for a telephone number, `` `cur[ `` for currency, `` `bool[ `` for a boolean, and the eight date forms `` `datemdy[ ``, `` `dateymd[ ``, `` `datedmy[ ``, `` `dateydm[ ``, `` `datemy[ ``, `` `datemd[ ``, `` `datedm[ `` and `` `dateym[ ``. `` `[ `` encloses a pronunciation in the engine's own phoneme alphabet.

A malformed annotation is spoken rather than refused, which is what `test/cases/anno.txt` holds the engine to.

## Where the samples go

    int eciSetOutputBuffer(ECIHand h, int samples, short *buffer);
    int eciSetOutputDevice(ECIHand h, int device);
    int eciSetOutputFilename(ECIHand h, const void *filename);

`eciSetOutputBuffer` is the one that works. The count is in samples, not bytes, and the buffer is the caller's: the engine fills it and calls back, and the caller must have copied what it wants before returning, because the next buffer goes in the same place.

`eciSetOutputBuffer(h, 0, 0)` puts the instance back to the device, which is to say back to nothing.

`eciSetOutputDevice` names a device by number and there are no devices. `eciSetOutputFilename` and `eciSynthesizeFile` are empty in IBM's own object: they answer nought and write no file. There is no way to make the engine write a file; a program that wants one writes it from the callback, which is what `cli/evv.c` does.

Samples are signed sixteen bit, mono, little endian, at whatever `eciSampleRate` says. The default is 11,025 hertz, which is what Eloquence has always sounded like.

## The callback

    typedef int (ECICALL *ECICallback)(ECIHand h, ECIMessage msg, int param,
                                       void *data);
    void eciRegisterCallback(ECIHand h, ECICallback cb, void *data);

`data` is the caller's and is handed back untouched. The messages are `eciWaveformBuffer`, where `param` is a count of samples; `eciIndexReply`, where it is the number given to `eciInsertIndex`; and `eciPhonemeBuffer` and the phoneme, word, string and audio index replies, which arrive only when they were asked for.

IBM's Linux header spells `param` as `long`, which is eight bytes on an ordinary sixty-four bit machine and four in everything the engine puts there. A program porting from that header should change the type rather than keep it.

**The callback runs on the engine's own synthesis thread**, not on the thread that asked for the speech. What it may do is take the samples and return. What it may not do is call back into the same instance.

What it answers matters more than it looks:

`eciDataProcessed` means "I have taken these". It is the right answer even when the program is discarding the samples.

`eciDataNotProcessed` means "my buffer is full, offer them again", and the engine waits thirty milliseconds before doing so. A program that answers this to mean "throw them away" pays that wait on every buffer. This is IBM's behaviour and nothing in the interface warns of it.

`eciDataAbort` stops the engine handing over any more buffers. It does not make it abandon the utterance, and nothing does -- see the stopping section below.

## Waiting and stopping

    int  eciSpeaking(ECIHand h);
    int  eciSynchronize(ECIHand h);
    void eciSynchronizeSynth(ECIHand h);
    int  eciStop(ECIHand h);
    int  eciPause(ECIHand h, int pause);

`eciSpeaking` answers whether anything is still outstanding, and a poll on it with a short sleep is the ordinary drive loop. `eciSynchronize` waits for all of it and is the same thing without the loop.

`eciStop` asks the engine to stop handing samples over. **It cannot make the engine abandon the utterance**, and that is settled rather than open: the rules build a shared structure as they go and later rules assume the earlier ones finished it, so there is no safe abandonment point anywhere in the machine. All of a cancel is waiting for the synthesis thread to finish the message it is on. What that costs was measured -- about 27 milliseconds with the rules compiled, about 86 interpreted -- and `docs/status.md` has the figures.

Because of that, all three ways of cancelling cost the same: letting the utterance finish and discarding it, answering `eciDataAbort`, and calling `eciStop`. Choose on convenience.

`eciStop` from a thread other than the one driving the engine is measured clean -- twenty-four turns on Linux and twenty-four under Wine, every one stopping an engine that was still delivering. One narrow window is left in the guard behind it and `docs/status.md` says which.

## Parameters

    int eciGetParam(ECIHand h, int which);
    int eciSetParam(ECIHand h, int which, int value);
    int eciGetDefaultParam(int which);
    int eciSetDefaultParam(int which, int value);

`eciSetParam` answers what the setting was before, or -1 if it refused. The default ones are what a new instance starts from, so setting a default changes nothing about an instance that already exists.

Eighteen settings, by the numbers `eci.h` names:

`eciSynthMode` (0), nought or one. One queues text with the settings it arrived under.

`eciInputType` (1), nought or one. One turns annotations on.

`eciTextMode` (2), nought to three.

`eciDictionary` (3), nought or one, **and one turns the dictionary off**. The value is inverted on its way in and out, which is IBM's.

`eciSampleRate` (5). Nought to six are 8,000, 11,025, 22,050, 16,000, 32,000, 44,100 and 48,000 hertz, in the order IBM numbered the first four and this port the rest; a value of 8,000 or more is that rate in hertz, so 24,000 is a rate nobody numbered. Everything between seven and 7,999 is in range and is not a rate: the call answers -1 and leaves the rate where it was. Above 11,025 the engine goes on synthesising at 11,025 and the rate is raised from there by a windowed sinc, so the voice is the same one at every setting. IBM numbered 16 kHz rate three and then set the range of this parameter to two, so its own fourth rate could not be asked for; the range now runs to the highest rate the tables can be built for, which is the first of three divergences `docs/notes/sample-rates.md` describes.

`eciWantPhonemeIndices` (7), nought or one.

`eciRealWorldUnits` (8), nought or one. With it on, a voice's speed is words a minute and its pitch is hertz, and both have ranges of their own.

`eciLanguageDialect` (9). One of the numbers `eciGetAvailableLanguages` answered with, optionally with `eciUnicodeCodeSet` in it.

`eciNumberMode` (10), nought or one.

`eciRomanizer` (12), nought or one. Only reachable in a language written in another script.

`eciAudioFormatA` through `eciAudioFormatD` (13 to 16) are the four an audio device's format is built from. Nothing in the tree knows what any of them means. Setting one while the samples are going to a buffer records the number and rebuilds nothing, which is the second deliberate divergence -- IBM's engine rebuilds regardless and the registered buffer is lost, so the instance goes silent and reports success ever after.

Numbers 11 and 17 are refused by both `eciGetParam` and `eciSetParam`, which is IBM's own refusal transcribed. Seventeen holds the number of the voice being spoken in; `eciCopyVoice` is what moves it. Numbers 4 and 6 can be set and read and nothing anywhere reads them.

## Voices

    int eciCopyVoice(ECIHand h, int from, int to);
    int eciGetVoiceParam(ECIHand h, int voice, int which);
    int eciSetVoiceParam(ECIHand h, int voice, int which, int value);
    int eciGetVoiceName(ECIHand h, int voice, void *name);
    int eciSetVoiceName(ECIHand h, int voice, const void *name);

Voice 0 is the one being spoken in. Voices 1 to 8 are the language's own presets and are read-only. Voices 9 to 16 are the caller's, copied from the presets when the instance is made. `eciCopyVoice` will only write to 0 or to 9 through 16, so the way to a voice of your own is to copy a preset into one of the editable numbers, change it there, and copy that onto 0.

The eight voice parameters are `eciGender`, `eciHeadSize`, `eciPitchBaseline`, `eciPitchFluctuation`, `eciRoughness`, `eciBreathiness`, `eciSpeed` and `eciVolume`. Gender is nought for male and one for female; speed runs to 250 and the rest to 100. With `eciRealWorldUnits` on the ranges change: gender stays nought or one, the pitch baseline runs from 40 to 422 hertz, and each of the other six takes 1 to 65,535 in its own units -- speed being words a minute.

A name is at most thirty bytes. The eight presets are called, in order, `Adult Male 1`, `Adult Female 1`, `Child 1`, `Adult Male 2`, `Adult Male 3`, `Adult Female 2`, `Elderly Female 1` and `Elderly Male 1`; the eight editable ones are all called `User-Defined` and start as copies of the presets in the same order, and voice 0 starts as a copy of voice 1.

`eciRegisterVoice` and `eciUnregisterVoice` are exported and are IBM's mechanism for a voice supplied from outside; nothing in this tree uses them.

`eciGetAvailableFilters` and `eciGetFilterDescription` are exported too, and both are empty in IBM's own object: they answer nought and never touch what they were handed. That was read rather than assumed.

## The user dictionary

    ECIDictHand eciNewDict(ECIHand h);
    ECIDictHand eciGetDict(ECIHand h);
    int         eciSetDict(ECIHand h, ECIDictHand dict);
    ECIDictHand eciDeleteDict(ECIHand h, ECIDictHand dict);
    int         eciLoadDict(ECIHand h, ECIDictHand dict, int volume,
                            const void *filename);
    int         eciSaveDict(ECIHand h, ECIDictHand dict, int volume,
                            const void *filename);

    const char *eciDictLookup(ECIHand h, ECIDictHand dict, int volume,
                              const void *key);
    int eciDictLookupA(ECIHand h, ECIDictHand dict, int volume,
                       const void *key, const char **out,
                       ECIPartOfSpeech *part);
    int eciDictFindFirst(ECIHand h, ECIDictHand dict, int volume,
                         const char **key, const char **translation);
    int eciDictFindNext(ECIHand h, ECIDictHand dict, int volume,
                        const char **key, const char **translation);
    int eciUpdateDict(ECIHand h, ECIDictHand dict, int volume,
                      const void *key, const void *translation);

A set holds four volumes. Three are the set's own -- `eciMainDict`, `eciRootDict` and `eciAbbvDict` -- and an instance has one set in force at a time; `eciSetDict(h, 0)` puts none in force. The fourth, `eciMainDictExt`, keeps a part of speech beside each entry and exists only for a language written in another script: Chinese, Korean and Japanese. **Ask any other language for it and every one of these answers `eciDictInvalidVolume`**, which is 7.

`eciUpdateDict` teaches the engine a word: a key and what to say instead. `eciDictLookup` answers what a key was taught, or nothing. `eciDictFindFirst` and `eciDictFindNext` walk a volume, handing back the key and the translation of each entry until there are none left, which is `eciDictNoEntry`.

The two strings a walk hands back live in memory the instance owns and are gone by the next call, so a caller that wants to keep them copies them.

Each has an `A` form carrying a part of speech, for `eciMainDictExt`. Two things about `eciDictLookupA` are worth knowing and both are IBM's. It answers an error code where the plain form answers the string, leaving what it found in the pointer you hand it. And it finishes by turning an empty answer into `eciDictNoEntry` -- a test it makes even on the roads that never write that pointer, so clear your own variable first or an invalid volume can come back as no entry.

`eciDictLookup` alone among the eight refuses a wide code set outright, before it has looked at the language at all.

The other way to teach the engine a word is to put it in the language module's own dictionary, which `tools/module/dict.py` does and `docs/language.md` describes. That is a build-time answer, not a run-time one, and it is the one that changes what the engine says without a program having to say it every time.

`test/harness/dict.sh` holds all eight against IBM's own engine, answer for answer, and `make dict` runs it.

## Phonemes instead of sound

    int eciGeneratePhonemes(ECIHand h, int room, void *buffer);

What the language decided the words were made of, in the engine's own alphabet, instead of the sound. `"Hello there."` comes back as ``` `2 `[.2hE.1lo]`0 `[.1Der]. ``` -- the annotations the engine would have acted on, with each word's phonemes and stress marks inside a pronunciation annotation.

Three things have to be true first, and two of them are IBM's own tests rather than advice.

A callback has to be registered, because the phonemes arrive through it as `eciPhonemeBuffer` messages into the buffer handed over, not as a return value. `eciSynthMode` has to be one, because the call walks the queue that mode builds and in the immediate mode there is nothing in it. And the text has to have been added already, with `eciAddText`.

The output is switched to the phoneme buffer, everything queued is put through, and the output is switched back to wherever it was, so an instance can be asked this in the middle of ordinary use. What the call answers is only whether that worked.

    eciRegisterCallback(h, on_message, 0);
    eciSetParam(h, eciSynthMode, 1);
    eciAddText(h, "Hello there.");
    eciGeneratePhonemes(h, sizeof buffer, buffer);

The separator between phonemes comes back followed by backspaces and spaces. That is the engine writing to what it takes for a terminal, and it is IBM's behaviour: strip them if they are in the way.

`test/harness/phonemes.sh` is what holds this to IBM's own engine, case for case over the same text, and `make phonemes` runs it.

## SSML

The engine carries IBM's own SSML reader and never loads it by itself. Turning it on is three calls and a fourth to use it:

    void *entry = (void *)ssmlFilterGetObject;
    ECIFilterAttribute attrib;

    eciRegisterFilter(h, 0, &entry, &attrib, 1);
    ECIFilterHand f = eciNewFilter(h, 0, 1);
    eciActivateFilter(h, f);

    const void *read = 0;
    eciGetFilteredText(h, f, writable_copy_of_document, &read);
    eciAddText(h, read);

`eciRegisterFilter` takes the **address of a variable holding** the entry point, not the entry point. Where IBM's caller got that entry point out of a DLL of its own -- `ssmlfilter.dll`, asking for `ssmlFilterGetObject` -- ours is in the same library under the same name, so there is one fewer file to find.

`eciNewFilter` speaks out whatever is queued and refuses while anything is outstanding, so do the filter setup before adding any text or inserting any index.

`eciActivateFilter`, `eciDeactivateFilter` and `eciSetFilter` take the handle rather than the id the filter was made from. The engine takes that handle as a thirty-two bit number, which holds because every filter is in the arena and every arena address fits in one.

What comes out of `eciGetFilteredText` is the annotations above, so `eciInputType` has to be one for the result to mean anything. The answer is the reader's own buffer and the caller is given a pointer into it.

Hand the reader a copy it may write on. IBM's reader ends the digits of a numeric character reference by writing a nought over the semicolon that closes them, in the caller's own string, so a document in a literal page-faults; ours copies the digits out instead, which is the eleventh deliberate divergence, but a program that wants to work against both should still pass a copy.

What the reader understands: say-as for numbers, ordinals, dates, times, telephone numbers and currency; prosody for rate, pitch, range and volume; emphasis; voice selection by gender and age; pronunciations in IPA or in the engine's own alphabet; pauses; marks; and language switching. It answers what IBM's answers over 176 documents. `docs/notes/ssml.md` is the whole of it.

Polish is on the prosody, rate and emphasis lists and deliberately not on the say-as or IPA ones, because a say-as annotation is read by the language's own rules and Polish's are still Italian's.

## Errors

There is no error reporting. `eciProgStatus` and `eciIsBeingReentered` always answer nought, `eciErrorMessage` writes nothing into the buffer it is given, `eciClearErrors` clears what nothing reads, and `eciRequestLicense` answers nought. All five are empty in IBM's own object and are transcribed empty. The instance really does record what it refused and why; nothing published hands it over.

So a program judges by return values. Everything above that answers an int answers nought when it refused, and `eciSetParam` answers -1.

`eciStartLogging`, `eciStopLogging`, `eciGetLog`, `eciGetIntLog` and `eciDialogBox` are empty in IBM's object too. They are exported so that a program which calls them links.

## Version

    char version[ECI_VERSION_LENGTH];
    eciVersion(version);

Four numbers with dots between them, and this engine answers `7.0.0.0`, which is IBM's own number for the Embedded ViaVoice engine and is lifted rather than chosen. A program keying on the version cannot tell ours from IBM's by it; the Windows version resource is what says `openevv`. The call is never told how big the buffer is.

On Windows the libraries also carry a version resource, and that is not decoration: the most used screen reader driver reads `ProductName` out of it to decide which engine it is talking to, and NVDA's own reader refuses a library with no version information at all. Ours says `openevv`. `docs/windows.md` says what that buys.
