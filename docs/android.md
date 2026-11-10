# Android

`android` is the engine as an Android voice: a text-to-speech service around it, so that TalkBack, and anything else on a phone, a tablet or a Wear OS watch that speaks through Android, can speak with OpenEVV. It is to Android what the Speech Dispatcher module is to Linux and the add-on is to NVDA, and it came in whole, history and all, from Quin Gillespie's evvdroid, with Aryan Choudhary's, aman kumar gupta's and adil-adysh's work in it. NOTICE says whose is whose.

## Installing it

Every release carries the app beside the Windows and Linux archives, one package for each kind of processor and one for any of them:

- `openevv-vN-android-arm64-v8a.apk` is nearly every phone of the last ten years.
- `openevv-vN-android-armeabi-v7a.apk` is older phones and some watches; a Pixel Watch 4 is this.
- `openevv-vN-android-x86_64.apk` is an emulator, or one of the few x86 tablets.
- `openevv-vN-android-universal.apk` is all three in one, three times the size.

The pre-release called `android-dev` is the same packages built from main, replaced by every push to it.

Android installs a package from outside a store only once it has been allowed to, which it asks about the first time. Every package, release or development build, is signed with the same key, so each one installs over the one before it and the settings stay. The application is `com.mudb0y.openevv`, which is not evvdroid's, so the two can be installed side by side and nobody has to lose one voice to try the other.

**Turning it on.** TalkBack settings, Text-to-speech settings, Preferred engine, OpenEVV. The gear beside it opens the voice settings, as does the launcher icon.

**Turning it off.** TalkBack settings, Text-to-speech settings, Preferred engine, Google. Know this before turning it on: if a voice a screen reader depends on stops talking, the screen reader cannot be used to put it back, and someone has to be able to get there another way.

## Updates

Two rows at the bottom of the settings. Update channel is Stable, the default, which is the latest release, or Development, which is `android-dev`. Check for updates says what the channel has and offers to install it, which Android then asks about itself.

Moving from Development back to Stable waits for the next release. Android will not install an older build over a newer one, and a development build is newer than the release before it, so Stable has nothing to offer until a release is cut after it.

## What it says

What the engine says on Linux and in NVDA it says here. The text a reader hands over is turned into the bytes the language reads exactly as the NVDA add-on turns NVDA's, in `EngineText.kt`: composed, with what only lays text out taken away, in the Windows Western set, with a Latin letter the set has not got losing as few accents as it must, so Dvořák is Dvorák. Polish is handed UTF-8, which the engine converts itself, and Japanese Shift-JIS, which its romanizer reads. The one difference from the add-on is what has no place in a code set at all: the add-on sends a question mark, as IBM's drivers did, and this sends a space. NVDA names every symbol and emoji before the add-on sees the text, so little reaches it; Android hands a voice the text as it is, and a question mark where an emoji was makes the sentence before it a question.

Nothing else is rewritten. A number, a time, an abbreviation, a word typed against digits and a symbol are all the engine's to read, and where it reads one wrongly that is fixed in the engine, where every front end has the fix. evvdroid had its own number reading, emoji names in English and its own punctuation names, and they were taken out when it came in for that reason. So an emoji is silence until the engine can name one.

Two things are put into words before the engine sees them, and neither is the app's choice. When TalkBack is asked to name punctuation, at its own most and all, it does so by attaching to each mark the words to read, and those words are read in the mark's place, with a comma or a full stop kept behind its name for the pause. And a word the listener's own dictionary holds by its case, which the engine's dictionaries cannot, is put as they wrote it.

**Languages.** All twelve modules are in every package, so Android's list of the engine's languages is the list below and each has the same eight voices. Which one speaks is the caller's to say. A screen reader asks by locale, and the voice's name carries it too, so `pol-POL-Reed` is Reed speaking Polish. A locale that matches on the language and not the country still gets an answer, from the first module of that language: Australian English is read by US English rather than refused.

- US English, `eng-USA`, 0x00010000
- British English, `eng-GBR`, 0x00010001
- Castilian Spanish, `spa-ESP`, 0x00020000
- Mexican Spanish, `spa-MEX`, 0x00020001
- French, `fra-FRA`, 0x00030000
- Canadian French, `fra-CAN`, 0x00030001
- German, `deu-DEU`, 0x00040000
- Italian, `ita-ITA`, 0x00050000
- Brazilian Portuguese, `por-BRA`, 0x00070000
- Japanese, `jpn-JPN`, 0x00080000
- Finnish, `fin-FIN`, 0x00090000
- Polish, `pol-POL`, 0x00110000

## The settings

They are the NVDA add-on's, with Android's names. Each row is one control: a label, a bar and a number would be three stops for a screen reader, so each row merges into one node that reads as "Speed, 20 percent" and adjusts in place. An arrow moves one percent, where TalkBack's own adjustment of a slider is a twentieth of its range and five percent of the speed scale is the difference between too slow and much too fast. The row takes the keyboard and the bar inside it does not, because a slider keeps a focus of its own that no screen reader can see once the row holds the semantics, and answers End by going to the maximum.

- **Voice** is one of the eight presets. It is whichever the settings say, not whichever the caller named: a screen reader holds on to the voice it was given, so one that connected while Reed was chosen would go on asking for Reed however often the setting changed. The name still decides the language.
- **Gender, speed, pitch, inflection, head size, roughness, breathiness and volume** are the eight voice settings, as percentages of each one's own range, since the engine has three scales and a bare number means nothing without knowing which. Each voice keeps its own: Reed at pitch 55 is still at 55 after a trip through Glen. Speed is the listener's rather than the voice's and is kept once for all eight, since two presets ship half again as fast as the rest. A setting nobody has touched is never sent, so a fresh install sounds as the presets do.
- **Preview language** picks the language Speak a sample speaks in, so a voice can be tuned in the language it will be heard in. It changes nothing a screen reader hears.
- **Pauses** is `eciPauseMode`: do not shorten, shorten at the end of text, which is the default as it is in the engine and the add-on, and shorten all pauses, which takes punctuation as well.
- **Phrase prediction** and **Expand abbreviations** are the engine's, and off, as everywhere.
- **Dictionaries** pick a file each for the main, root and abbreviation volumes, and edit them: a word can be added, changed, suspended or marked as matching only in its own case. The files are the ones people share, a word, a tab and what to say, either a respelling or the engine's own phonemes, a byte a character. They are read here and taught to the engine a word at a time, about 450 milliseconds for 3,000 entries, once, and again only when a file changes.
- **Sample rate** and **Wideband above 11 kHz** are the engine's own. Above 11,025 the voice is the same one raised to the rate unless wideband is on, and then it is the 11,025 voice below about five and a half kilohertz and a second synthesiser's top above it, as in `docs/api.md`.
- **Background and stability** opens Android's battery and app hibernation screens and the phone maker's auto start screen, and on Android 11 and later says why Android has stopped the engine, from its own record of it. A phone saving battery does not ask what a process is for, and freezing or killing the engine sounds exactly like it crashing.

Android's own speech rate and pitch are multipliers and are treated as such. The engine's speed is nothing like linear -- speed 100 is 2.7 times the default rate and 200 is 16.6 times -- so a measured curve in `SpeechRate.kt` converts between them, 200 percent is about twice as fast, and very high rates stop at the engine's own ceiling.

## Interrupting

A screen reader wants every utterance to cut off the one before, and the engine cannot abandon what it is saying, which `docs/api.md` explains. Two things make that bearable here. Long text is handed over in sentence-sized pieces, so asking for silence waits out a piece rather than a paragraph; where a piece may end is the NVDA add-on's rule, measured there, since a sentence end is free and anywhere else costs about four tenths of a second. And synthesis is paced to real time: the engine makes a second and a third of audio in twelve milliseconds and Android would queue all of it, so a fast swipe would leave five or six items queued under the one being spoken and lose them all at the next stop. No more than 300 milliseconds runs ahead.

## Building

On this machine, from the top of the tree:

    nix develop .#android
    cd android
    ./gradlew assembleDebug

That shell is the Android SDK, the NDK the app names, 27.0.12077973, JDK 17, the emulator with an x86_64 image, and Python and make for the engine. The SDK is unfree and comes with a licence to accept, so it has a nixpkgs of its own and the default shell is untouched by it. The Gradle plugin fetches its own aapt2 as a Linux binary that has no loader to run under on NixOS, so the shell points it at the SDK's, which is patched to run.

Anywhere else it wants the Android SDK with that NDK, JDK 17, Python 3, make and bash, with `ANDROID_NDK_HOME` set. No Android Studio; the wrapper fetches its own Gradle.

`android/native/build-native.sh` builds the engine half. It runs `make android-objects` once for each ABI, with the NDK's clang as `CC`, which compiles the same objects `make so` links into `libeci.so.1` and names them in a file, and then links them with the JNI bridge in `app/src/main/cpp/evv_jni.c` into one `libevvjni.so`, since Android packages only files named so and would not find a library whose soname is `libeci.so.1`. Two flags are its own. `-fsigned-char`, because a plain char is signed on x86 and unsigned on ARM, and this engine compares bytes out of its tables as numbers the way the x86 code it was transcribed from did; turning it on changed no sample of anything tried, so it is the safer of the two rather than the fix for anything heard. And a sixteen kilobyte page size, which Android 15 asks of every library.

The rules are written and decompiled once whatever the number of ABIs, since that is per language, and compiling them is per ABI. On this machine's forty threads, with the rules already written, the three ABIs and twelve languages compile and link in under three minutes; writing and decompiling the rules first is about two more, and ccache hands back what a change left as it was. Four Gradle properties change what is built:

- `openevv.abis`, `arm64-v8a,armeabi-v7a,x86_64` by default, says which ABIs.
- `openevv.rules`, `c` by default, is `RULES`: `bytecode` skips the decompiling and speaks at under half the speed. Compiled rules are the default because interrupting costs a third of what it does interpreted, and a screen reader interrupts constantly.
- `openevv.langs`, every language by default, is `LANGS`, space separated, the first answering a caller that names none.
- `openevv.abiSplits`, true by default, makes one package per ABI and a universal one. Twelve languages make a library of 53 megabytes for arm64, 41 for armeabi-v7a and 57 for x86_64, and a library is stored uncompressed so it can be mapped straight out of the package rather than unpacked at install, so the packages are 56, 44 and 60 megabytes and the universal one 154.

`-Popenevv.abis=x86_64 -Popenevv.rules=bytecode` is the quick loop for working on the Kotlin against an emulator.

A release build is signed from `android/keystore.properties`, which the tree does not carry: `storeFile`, `storePassword`, `keyAlias` and `keyPassword`. Without it a release build comes out unsigned. The workflow writes one from the repository's secrets for every build but a pull request's.

The version code is the commit count and the version name is what `git describe` says of the newest tag, so a build of v0.7 is 0.7 and one twelve commits later is 0.7-12-g and its hash. That is what the updater compares, and it is why a checkout for building wants its history and its tags.

## Testing

Three things test it, and the first two are the app's own.

    ./gradlew testDebugUnitTest

is the part that needs no Android, eleven checks of the updater's choices and of TalkBack's spans.

    ./gradlew connectedDebugAndroidTest

runs on a device or an emulator, 68 checks: the engine through the JNI bridge, every language speaking its own sentence, interrupting and the pacing, the dictionaries, the settings screen, the pauses getting shorter mode by mode.

The third is the gate everything else is held to, run on Android. `android/native/tools/probe-on-device.sh` stands in for the probe, so `test/matrix.sh` speaks every recorded case through a probe built with the NDK and running on the device, and holds what comes back against what this engine has said on x86:

    make RULES=c LANGS="lang/enus lang/engb lang/dede lang/eses lang/esus lang/frfr lang/frca lang/itit lang/plpl lang/jajp lang/fifi lang/ptbr" \
        BUILD=build/android-probe CFLAGS=-fsigned-char \
        CC="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/clang --target=x86_64-linux-android21" probe
    adb exec-out mkdir -p /data/local/tmp/evvmatrix
    adb push build/android-probe/probe-enus-engb-dede-eses-esus-frfr-frca-itit-plpl-jajp-fifi-ptbr /data/local/tmp/evvmatrix/probe
    EVV_MATRIX_NATIVE=$PWD/android/native/tools/probe-on-device.sh test/matrix.sh check

An emulator for those, from the Android shell, with no window and no sound -- it never touches the machine's audio:

    avdmanager create avd -n openevv -k "system-images;android-36;google_apis;x86_64"
    emulator -avd openevv -no-window -no-audio -no-boot-anim -no-snapshot &
    adb wait-for-device

An x86_64 image runs at the host's own speed under KVM, and Google's images carry a translator that runs arm64 code too, an app's library and a program run from adb alike, at about a sixteenth of the speed. So both ABIs a phone is likely to have can be held to the gate here. On 10 November 2026, on Android 16, all 1,327 cases came back as they were from the x86_64 probe and from the arm64 one, and the 68 device checks passed on each. armeabi-v7a, which some watches are, is not translated, and the two faults only thirty-two bit ARM had shown, in `docs/status.md`, were found on a watch.

**Wear OS.** A Pixel Watch has no USB, so adb is wireless. On the watch: Settings, System, About, Versions, and tap Build number seven times; then Developer options, ADB debugging on, Wireless debugging on, Pair new device. Watch and computer on the same network, with the watch's Wi-Fi actually on rather than dropped for Bluetooth. `adb mdns services` shows both ports the watch announces, `adb pair` the pairing one with the six digits on its screen, and `adb connect` the other; `./gradlew installDebug -Popenevv.abis=armeabi-v7a` installs it.

## Where things are

- `android/app/src/main/cpp/evv_jni.c` is the bridge: one engine instance, a ring the engine's thread fills and Android's synthesis thread drains, and one thread kept for the life of the instance that the engine runs on.
- `android/app/src/main/kotlin/com/mudb0y/openevv` is the rest. `EvvTtsService.kt` is the service Android talks to, `EvvEngine.kt` one instance and the settings on it, `EngineText.kt` the text as bytes, `TalkBackSpans.kt` what TalkBack put into words, `TextPieces.kt` where long text is cut, `Settings.kt` what is stored, `SettingsScreen.kt` and `SettingsModel.kt` the settings, `Dictionaries.kt` the dictionary files, `DirectBoot.kt` what the engine can read before the phone is unlocked, `Stability.kt` the background screens, and `Updater.kt` the updates.
- `android/native/build-native.sh` builds the engine half, and `android/native/tools/probe-on-device.sh` runs the gate on a device.
