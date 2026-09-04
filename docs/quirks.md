# What will trip you

The things about this engine that cost an afternoon if nobody says them first. Some are ours, most are IBM's, and each says which. `docs/api.md` is the reference and `docs/using.md` the integration guide; this is the list to read before deciding you have found a bug.

## Silence

**An instance sends its samples to an audio device by default, and there is no audio device.** The platform layer answers that this machine has none, which is what sends the engine down the buffer path that everything else depends on.

Until a buffer is registered the instance has nowhere to put anything, and `eciAddText` answers nought. It is the only call that says so, which is the part to know: `eciSynthesize` and `eciSynchronize` both answer success on an instance that will never speak, and `eciSpeaking` answers that it is not. A program that checks the calls it thinks of as the important ones and not the one that adds the text gets silence with no complaint.

`eciSetOutputFilename` and `eciSynthesizeFile` do not help: both are empty in IBM's own object and write no file. There is no way to make the engine write a file. Write it from the callback.

`eciSpeakText` and `eciSpeakTextEx` make a whole instance, say one thing and take it away, registering no callback and no buffer on the way -- so the instance they make is exactly the one above, its `eciAddText` refuses, and both answer nought having said nothing. They also make and destroy a synthesis thread and a four megabyte frame stack each time they are called.

`eciSetOutputBuffer(h, 0, 0)` puts an instance back to the device, which is to say back to silence.

## No error ever gets reported

`eciProgStatus` and `eciIsBeingReentered` always answer nought. `eciErrorMessage` writes nothing into the buffer it is given. `eciClearErrors` clears what nothing reads, and `eciRequestLicense` answers nought. All five are empty in IBM's own object and are transcribed empty here. The instance really does record what it refused and why; nothing published hands it over.

`eciStartLogging`, `eciStopLogging`, `eciGetLog`, `eciGetIntLog` and `eciDialogBox` are empty in IBM's object too.

So judge by return values: nought means refused, and `eciSetParam` answers -1.

## Answering the callback wrong costs thirty milliseconds a buffer

`eciDataNotProcessed` means "my buffer is full, offer these samples again", and the engine answers that by sleeping for thirty milliseconds and offering the same buffer. That is right for a player whose buffer is genuinely full and ruinous for a program that is discarding.

**A program that means "throw this away" must answer `eciDataProcessed`.** This is IBM's behaviour, nothing in the interface warns of it, and it was found while chasing a latency that turned out not to be the engine's at all.

## Nothing can make the engine abandon an utterance

`eciStop`, `eciDataAbort` and simply letting the utterance finish all cost the same, because all three wait for the same thing: the synthesis thread finishing the message it is on. All a stop does is stop more buffers being handed over.

This is settled rather than open. The rules build a shared structure as they go and later rules assume the earlier ones finished it, so abandoning a rule part way leaves that structure half built and a later rule faults on it. Six ways of adding an abandonment point were tried and all fail, and the reason is one thing rather than six. Making the machine interruptible would mean the rules checking their own inputs, which is the language and not a patch.

What can be done about the latency is doing the leftover work faster, which is what `RULES=c` -- the default -- is for: about 27 milliseconds to cancel and speak again against about 86 interpreted.

An interrupted utterance is not necessarily short, and no harness asserts that it is. Whether the sample count comes out short depends on where the suspension lands between two buffers, and a callback that paces itself like a real player runs on the engine's own thread, so the stop then waits for the whole delivery and can never truncate.

## The second utterance is not the first, and that is correct

Saying the same sentence twice on one instance gives 38,423 samples both times and 30,495 of them differ, because the machine's state has moved on. It is entirely deterministic -- three processes give the same first utterance and the same second one, to the hash -- and IBM's own engine does it to the same 30,495 samples, with ours matching its second utterance byte for byte.

So samples are comparable, a second utterance included, as long as both sides have spoken the same history. **What is not comparable is a second utterance against a first.** A program checking the engine against a recorded hash has to compare like with like.

## Text is bytes, not UTF-8

The engine reads single bytes and IBM's engine does almost nothing between the caller's bytes and the machine's characters. Text is the language's own code set, which for the nine languages IBM shipped is the Windows Western set. Handing it UTF-8 gets the mangling IBM's engine produces, byte for byte.

The exception is a language that declares characters of its own, which is Polish and none of IBM's nine. Those get their text converted from UTF-8 on the way in, and their own bytes let through the romanizer's table rather than turned into spaces. Those two are the fourth and fifth deliberate divergences, and the guard is the whole point: the nine IBM shipped declare no characters, so nothing about them changes.

To hand over UTF-16 instead, OR `eciUnicodeCodeSet` -- 0x800 -- into the language. The code set is the third byte of the same word the language is in, and `eciLanguageDialect` is what the engine reads to find out; `eciTextMode` is a different setting and is not it.

## Parameters that are not what they look like

**`eciDictionary` is inverted.** One turns the dictionary off. The value is flipped on its way in and out, which is IBM's.

**Numbers 11 and 17 are refused by both `eciGetParam` and `eciSetParam`**, which is IBM's own refusal transcribed. Seventeen holds the number of the voice being spoken in and `eciCopyVoice` is what moves it.

**Numbers 4 and 6 can be set and read and nothing anywhere reads them.**

**The sample rate has a gap in the middle of its range.** Nought to six are the numbered rates and 8,000 upwards is a rate in hertz, so seven to 7,999 pass the range check and are not rates. `eciSetParam` answers -1 for those and leaves the rate alone. It used to answer -1 and record the number anyway, so `eciGetParam` reported a rate nothing was synthesising at; that was ours rather than IBM's, and it was only reachable because we widened the range.

**IBM's own fourth sample rate could not be asked for.** It numbered 16 kHz rate three and then set the range of the sample rate parameter to two. The range here runs to the highest rate the tables can be built for, which is the first of three divergences in `docs/notes/sample-rates.md`. The other two are that 22.05 and 16 kHz are doubled now rather than mislabelled, and that a rate given in hertz is not something IBM's engine would take at all.

**Changing the sample rate loses a registered buffer in IBM's engine and not in ours.** IBM rebuilds the output as a device regardless, which hands the engine a null buffer on the way past; the instance then reports the new rate ever after and answers no more samples. Ours chooses on where the samples were already going. That is the first deliberate divergence, and the four audio-format numbers -- parameters 13 to 16 -- are the second, for the same reason.

## Voices

Voice 0 is the one being spoken in, 1 to 8 are the language's presets and are read-only, and 9 to 16 are the caller's. `eciCopyVoice` will only write to 0 or to 9 through 16. The way to a voice of your own is to copy a preset into an editable number, change it there, and copy that onto 0.

The presets are named `Adult Male 1`, `Adult Female 1`, `Child 1`, `Adult Male 2`, `Adult Male 3`, `Adult Female 2`, `Elderly Female 1` and `Elderly Male 1`, and all eight editable ones are called `User-Defined` until something renames them, so a program picking a voice by name has to know that the interesting ones are the read-only eight.

## Making a filter

`eciNewFilter` speaks out whatever is queued and refuses while anything is outstanding. So an index inserted, or text added, before the SSML filter is made makes `eciNewFilter` answer nothing -- which looks exactly like the filter being unavailable. Do the filter setup first.

`eciRegisterFilter` takes the **address of a variable holding** the entry point rather than the entry point itself.

`eciActivateFilter`, `eciDeactivateFilter` and `eciSetFilter` take the handle `eciNewFilter` answered with, not the id it was made from, although the engine's own transcription of them takes that handle as a thirty-two bit number. It holds because every filter is in the arena and every arena address fits in thirty-two bits. `include/eci.h` declares them as taking a handle, which is what they take.

`eciGetFilteredText` takes four arguments and none of them is a buffer size: the filter, the document, and where to leave the answer. The wrapper in `lib/eci_api.c` declared the last as an `int` until the header was written, which truncated the caller's answer pointer on the way past and worked only because the compiler tail-called rather than storing anything.

Hand the reader a document it may write on. IBM's reader ends the digits of a numeric character reference by writing a nought over the semicolon that closes them, in the caller's own string, so `&#65;` in a literal page-faults. Ours copies the digits out instead, which is the eleventh deliberate divergence -- but a program meant to work against both engines should still pass a copy.

## Names that are not exported

The dictionary find, lookup and update calls -- `eciDictFindFirst`, `eciDictFindNext`, `eciDictLookup` and `eciUpdateDict`, each in a plain and a wide form -- exist inside the engine with no public wrapper. A caller asking for one of those gets nothing rather than something wrong. Nothing known needs them.

Everything speech-dispatcher's `sd_eloquence` module resolves is exported, `eciGeneratePhonemes` included.

## Asking for phonemes

`eciGeneratePhonemes` will answer nought and look broken unless three things are true, and the first two are IBM's own tests rather than advice. A callback has to be registered, because that is the only way the phonemes can arrive. `eciSynthMode` has to be one, because the call walks the queue that mode builds. And the text has to have been added first.

What comes back has the separator between phonemes followed by backspaces and spaces, because the engine is writing to what it takes for a terminal. That is IBM's, byte for byte.

## Calling convention on thirty-two bit Windows

IBM's own objects export `_eciSetParam@12` and `_eciAddText@8`. The byte count is part of a stdcall name on x86, so the published interface is stdcall there and a caller that assumes so of a cdecl function leaves the stack out by the size of the arguments after every call.

`include/eci.h` says `__stdcall` on Windows for that reason, and `eci32.dll` is built from it. The names are published undecorated all the same, because that is what a caller asking by name asks for. On x86-64 there is one convention and none of this arises; off Windows there is nothing to say.

The wrappers in `lib/eci_api.c` were plain until the header existed, which was right for the sixty-four bit library and wrong for the thirty-two bit one.

## The words the engine cannot say

Every string that kills IBM's Eloquence is the same fault: the Delta machine dereferences a node reference of nought, either because a rule left a position variable unset and then walked from it, or because a walk stepped off the end of the spine. IBM's binary faults on the same reads.

Thirteen places now test for nought and answer the way that primitive already answers everything else it cannot do, and 43 walks count their steps so that links which have come round on themselves end the rule rather than hanging the engine. That is the tenth deliberate divergence. Every guard sits on a path the old code could not survive, so no working input can reach one.

`test/cases/crashers.txt` is the text, `make crashers` is the check, and `docs/notes/crashing-strings.md` is the whole of it. If your program feeds the engine arbitrary text -- a screen reader does -- this is the class of thing it used to die on.

## Mixing toolchains on Windows

The libraries in a release are built by one mingw and tested with harnesses built by the same one. A caller built by a different mingw, with a different thread runtime -- nixpkgs uses mcfgthreads where Debian uses winpthreads -- can fault on the crossing, and one direction of that pairing does.

It does not matter for the callers that exist: Python's ctypes and a screen reader's host DLL are MSVC built with no mingw runtime in them at all, and CI checks both of those crossings on Windows itself. But do not conclude from a fault in a hand-mixed pair that the shipped library is broken. Check a matched pair first.

## If it sounds wrong

It is not a fault in the port. The audio is identical to IBM's by design, over 881 recorded cases in nine languages and every build the tree makes. That is Eloquence sounding like Eloquence.

Changing it is a deliberate change to the language data, and the gate will correctly report that as a difference.
