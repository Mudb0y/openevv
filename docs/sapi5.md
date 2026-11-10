# The SAPI 5 voice

`sapi5` is the engine as a Windows voice: something every program that speaks through Windows can be told to use, Narrator, the speech control panel and any program written against SAPI among them, without knowing anything about ECI. It is to Windows what `speechd` is to Linux. The NVDA add-on is a different thing for a different reader: NVDA drives the library itself, and a SAPI voice is for everything else.

## What it is

A COM library, `openevv_sapi5.dll`, that SAPI loads into whichever program wants to speak. It offers eight voices for each language the engine has -- each language's eight presets, under the names the engine gives them -- so a build with all twelve languages offers ninety-six, each with the locale SAPI filters a voice list by. A voice is called `OpenEVV Adult Male 1 - US English` and its token `OpenEVV-en-US-1`.

It does not link the engine. It loads `eci.dll` by name, as any other program written against IBM's interface does, from a folder beside itself named for its word size: `eci-x86_64` for the sixty-four bit voice and `eci-x86` for the thirty-two bit one, which is how the release lays the two libraries out. So the voice builds in seconds, wants none of the rules, and speaks with the same library everything else in the tree is checked against.

There are two of it because a SAPI program can only load a voice of its own bitness. Narrator and most programs now are sixty-four bit; a great many older ones are thirty-two, and some screen readers' SAPI support is among them. The installer puts both in one folder, the thirty-two bit one as `openevv_sapi5_x86.dll`, and registers each into its own view of the registry.

`openevv_config.exe` is the settings dialog. The same dialog is behind the Settings button the speech control panel and some other programs show for a voice. Its settings are the NVDA add-on's switches with the add-on's defaults: whether abbreviations are expanded, which pauses the engine shortens (`eciPauseMode`), whether a backtick in the text is a command to the engine, and phrase prediction. They live in `HKEY_CURRENT_USER\Software\OpenEVV\SAPI5`, shared by every voice of every language and by both word sizes, so changing them never asks for administrator.

## What it does with what SAPI asks

SAPI hands a voice a list of fragments, each with the text and the state to say it in. Rate, pitch and volume become `` `vs ``, `` `vb `` and `` `vv `` annotations in front of the text they apply to, so a document that changes its voice part way changes it from that word; nought on SAPI's scale is the preset as it was made, and plus ten is about four times as fast rather than the engine's own maximum, which is some twenty times and not speech anyone can follow. Spelling out is `` `ts1 ``, silence is `` `p ``, and a bookmark is an index mark reported when the audio in front of it has been written, so SAPI is told it where the listener is rather than where the engine is. Pronunciation by phoneme is not acted on yet.

Every utterance starts with the annotations that put the instance in a known state, because an annotation changes an instance and the change stays: an utterance abandoned part way would otherwise leave spelling on, or the pitch moved, for whatever is said next.

Stopping is what `docs/using.md` says it is. When SAPI says the caller has given up, the voice stops writing audio and lets the utterance finish synthesising into nothing, which is the only stop there is and costs a fraction of a second.

Text goes to the engine in the code set of the language it is speaking: the Windows Western set for ten of them, Shift-JIS for Japanese and UTF-8 for Polish, as the add-on sends them. It is composed first, and anything the code set has not got is left to Windows to best-fit. With voice tags off, which is the default, a backtick becomes a space before it is converted -- before, because in Shift-JIS a backtick's byte is also the second half of some characters.

The voice makes 11,025 samples a second, which is what Eloquence sounds like, and SAPI converts for a program that wants another rate.

## Building it

    make sapi5 sapi5-32

That builds `build/sapi5/openevv_sapi5.dll`, `openevv_sapi5_x86.dll`, both configurators and both test drivers, with `CXXWIN` and `CXXWIN32`, which are mingw's C++ compilers: `x86_64-w64-mingw32-g++` and `i686-w64-mingw32-g++` unless told. They are C++ because COM is. mingw ships `sapi.h` and not `sapiddk.h`, which is the half a voice is written against, so `sapi5/sapiddk.h` declares the three interfaces and one structure it needs from Microsoft's published IDL, in the same order and with the same identities -- a method out of order there is a call to the wrong function and nothing at compile time can say so.

    make sapi5-stage

That builds both voices and both libraries and lays them out in `build/sapi5` as the installer installs them: the voices, `eci-x86_64/eci.dll`, `eci-x86/eci.dll`, LICENSE and NOTICE. The installer is Inno Setup and is built on Windows from that folder:

    iscc /DAppVersion=0.7 sapi5\installer\openevv.iss

It installs both word sizes on sixty-four bit Windows and the thirty-two bit one alone on thirty-two bit Windows, registers in `HKEY_LOCAL_MACHINE`, which needs administrator, and on uninstalling offers to keep the settings for a later installation. CI builds it on every push and a release attaches it beside the archives.

## Proving it

`voice_test.exe` loads the voice by hand, asks for the class through `DllGetClassObject`, and drives it with a site and a token of its own standing in for SAPI's, so nothing is registered. It checks the format, that a higher rate is quicker, that spelling is longer than saying, that a bookmark comes back where its audio is, that silence lengthens an utterance, that a backtick is not obeyed with voice tags off, that an abort stops the audio and leaves the next utterance whole, and then that every language the library has speaks through a token naming it, and that German given English text says something other than English does. Run it from the staged folder, since it finds the engine where the voice does:

    cd build\sapi5
    voice_test.exe openevv_sapi5.dll
    voice_test32.exe openevv_sapi5_x86.dll

`sapi5/speak.ps1` asks Windows rather than the voice: it lists the voices SAPI offers, counts those that are ours, and has each language's first voice say something into a wave file through SAPI's own automation object, which is how a program finds and uses a voice. Run from the thirty-two bit PowerShell under `SysWOW64` it asks the other word size. It wants the voice registered in `HKEY_LOCAL_MACHINE`, because SAPI will not make a voice whose token is anywhere else -- a voice registered for one user is listed and then refused -- so it is CI's Windows runner that runs it: it registers both voices, speaks through each, unregisters them and checks they are gone, then builds the installer, installs it, speaks again and uninstalls.

## What it does not do

It speaks only at 11,025. The engine offers other rates and the wideband voice, and a SAPI voice could too, by answering SAPI with another format; nothing here asks for one yet.

It reads no pronunciation dictionaries but the language's own. The add-on's dictionaries -- the community's files and a person's own -- are not loaded.

It does not follow a change of language inside a document. SAPI's fragments carry a language, and the engine can change language on a live instance, but each voice speaks the one language its token names.
