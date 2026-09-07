# evvdroid

Eloquence as a text-to-speech engine for Android and Wear OS.

The speech comes from [openevv](https://github.com/Mudb0y/openevv), a C rebuild of IBM's Embedded ViaVoice. This repo cross-compiles that engine for Android, wraps it in JNI, and puts a `TextToSpeechService` around it. openevv is a submodule and is not edited in place.

## Unofficial

A hobby project. Nobody's product. Not from IBM, Nuance, Cerence, Google or the openevv project, and none of them have endorsed or reviewed it.

It is not a supported screen reader engine. If you set it as your TalkBack voice and it stops talking, you have to undo that yourself, so read "Turning it off" before you turn it on.

No warranty. See LICENSE, and read the part about language data before redistributing anything.

## What works

* US English.
* Eight preset voices: Reed, Shelley, Bobby, Rocko, Glen, Sandy, Grandma, Grandpa. Offered through the Android voice API, so they appear separately.
* All eight voice parameters: speed, pitch, inflection, head size, roughness, breathiness, volume, gender.
* Speech rate and pitch from the system settings.
* Pronunciation dictionaries.
* Android 6 and later. arm64-v8a, armeabi-v7a, x86_64.

`app/src/androidTest` runs on a device. 74 checks.

## Turning it on

TalkBack settings, Text-to-speech settings, Preferred engine, Eloquence (openevv). The gear next to it opens the voice settings.

## Turning it off

TalkBack settings, Text-to-speech settings, Preferred engine, Google.

## Building

Needs the Android SDK, NDK r23 or later, JDK 17, Python 3 and bash. No Android Studio, and the wrapper fetches its own Gradle.

```
git submodule update --init --recursive
export ANDROID_NDK_HOME=/path/to/ndk
./gradlew installDebug
./gradlew connectedDebugAndroidTest
```

First build is about four minutes per ABI. Most of that is the engine writing the language rules out of text with Python and decompiling them into roughly 13 MB of C, which is per language rather than per ABI, so only the first ABI pays for it.

| property | default | effect |
| --- | --- | --- |
| `evvdroid.abis` | `arm64-v8a,armeabi-v7a,x86_64` | which ABIs to build |
| `evvdroid.rules` | `c` | `bytecode` builds in 30s and runs at under half the speed |
| `evvdroid.langs` | `lang/enus` | language modules to link in |
| `evvdroid.abiSplits` | `false` | one APK per ABI |

`-Pevvdroid.abis=arm64-v8a -Pevvdroid.rules=bytecode` is the fast loop for working on the Kotlin. Compiled rules are the default because interrupting and restarting costs about a third of what it costs interpreted, and a screen reader interrupts constantly.

A fix to openevv that upstream has not taken yet goes in `native/patches` and is applied at build time. There are none, so the submodule builds as upstream left it.

## Settings

Launcher icon, or the gear beside the engine in the text-to-speech settings. Sliders for the seven numeric voice parameters, plus gender, voice, sample rate, pauses, phrase prediction, abbreviations and dictionaries. Nothing speaks on its own: a setting takes effect when you set it, and "Speak a sample" is how you hear it.

Each row is one control. A label, a bar and a number would be three stops for a screen reader, so each row merges into a single node with `clearAndSetSemantics`. It reads as "Speed, 20 percent" and adjusts in place.

Values are percentages. The engine uses three scales (gender is 0 or 1, speed runs to 250, the rest to 100), so a raw number means nothing without knowing which.

The row takes the keyboard and the bar inside it does not. A Compose slider keeps a focus of its own, which no screen reader can see once the semantics belong to the row, and it answers the End key by going to the maximum. So the bar is out of the tab order and the row answers the keys itself.

An arrow moves one percent. TalkBack adjusts any slider by a twentieth of the range it is given, and the number of steps a Compose slider declares never reaches the platform, so TalkBack's own arrow asks for five. Five percent of the speed scale is the difference between too slow to bear and faster than was wanted, so the row reads a request to move as a request to move one.

Speech rate from the system is a multiplier, so it is treated as one. The engine's speed scale is far from linear: speed 100 speaks 2.7 times the default rate and speed 200 speaks 16.6 times. A measured curve converts between the two rather than multiplying, so 200 percent is about twice as fast and very high rates saturate at the engine's ceiling.

Voice settings belong to the voice. Set Reed's pitch to 55, go to Glen and come back, and Reed is still at 55. Only "Reset voice to defaults" forgets it, and only for the voice you are on.

Speed is the listener's, not the voice's. Six presets ship at engine speed 50 and Glen and Sandy ship at 70, half again as fast, so a preset brings its other seven settings and leaves the speed alone.

Sliders start at the voice's own values. Each preset carries its own head size, inflection and volume: Reed is inflection 30 and volume 92, Bobby is 35 and 90. A parameter you have not set is never sent to the engine, so a fresh install sounds like Eloquence always has.

## Pauses

Eloquence pauses generously, which reads well in prose and badly in a screen reader where most utterances are a few words. The engine has no setting for it but it takes an annotation: `` `p `` and a number is a pause of that many milliseconds, and one placed in front of a punctuation mark replaces the pause that mark would have had.

Three choices. Do not shorten leaves the engine alone. Shorten at end of text trims only the gap after the last thing said. Shorten all pauses trims punctuation as well, which is the default and takes a sentence with commas to 65 percent of its original time.

The rule is davidacm's, from the [NVDA IBMTTS driver](https://github.com/davidacm/NVDA-IBMTTS-Driver), which calls these JAWS-like pauses. A mark counts when it follows a letter, digit or space and is followed by whitespace, a slash or the end, which leaves the point in 3.14, the colon in 2:30 and the comma in 1,024 alone.

## Voice, phrase prediction, abbreviations

The voice is whichever the settings screen says. A caller that names a voice does not override it, because a client holds on to the Voice object it was given: a screen reader that connected while Reed was selected goes on asking for Reed however many times the setting changes underneath it. The name is still what decides the language.

Phrase prediction is the engine guessing where the phrase boundaries in a sentence are and shaping the intonation to match. That reads well in prose and gets in the way in a screen reader, where a line is usually a fragment and the guess is wrong, so it is off. It is an annotation rather than a parameter, so it goes out with every utterance.

Expand abbreviations is `eciDictionary`, which is inverted in the engine: zero turns it on. Off by default, and re-sent with every utterance, since it is the one setting with nothing to say whether something else has moved it.

## Dictionaries

Three rows pick a file each for the main, root and abbreviation volumes. "Remove dictionaries" clears all three.

The files people share are text: a word, a tab, then either a respelling (`dee oe jeigh`) or a pronunciation in the engine's own alphabet (`` `[Ekspoz1e] ``). [mohamed00/altibmttsdictionaries](https://github.com/mohamed00/altibmttsdictionaries) has the English pair.

The engine's own loader will not read them. `eciLoadDict` answers `eciDictAccessError` for every one because it expects IBM's saved binary form, so the text is parsed here and the engine is taught a word at a time through `eciUpdateDict`. 3000 entries take about 450 ms, once, when the service starts. They reload only when the files change.

A picked file is copied into app storage. A content URI belongs to whichever app supplied it, may not be readable later, and the speech service reads it from another process.

No dictionary is shipped here. See LICENSE.

## Interrupting

A screen reader wants each string to cut off the one before it. Two things make that work.

The engine cannot abandon an utterance, so long text is handed over in sentence-sized pieces and asking for silence waits out a piece rather than a whole paragraph. The rule for where a piece may end is openevv's, ported from its NVDA driver: a sentence end is free because the engine already pauses there, anywhere else costs about 0.4s, so a full stop has to prove itself. A word with a dot inside it is an abbreviation, and a short capitalised word is an initial.

Synthesis is paced to real time. The engine produces 1.3s of audio in 12 ms and Android queues every byte of it, so a reader swiping quickly would leave five or six items queued while the first is still playing and lose all of them when it asks for silence. Output runs no more than 300 ms ahead. Measured on the same text: 2229 ms of audio handed over in 700 ms of speaking without that limit, about 1000 ms with it.

## Wear OS

Runs on a watch. Tested on a Pixel Watch 4, which is armeabi-v7a.

A Pixel Watch has no USB, so adb is wireless. On the watch: Settings, System, About, Versions, tap Build number seven times. Then Developer options, ADB debugging on, Wireless debugging on, Pair new device. Watch and computer on the same Wi-Fi, with the watch's Wi-Fi actually on rather than dropped for Bluetooth.

```
adb mdns services                 # the watch announces both ports
adb pair 192.168.x.x:PAIRPORT     # then the six digits on screen
adb connect 192.168.x.x:PORT
adb -s SERIAL shell getprop ro.product.cpu.abilist
./gradlew installDebug -Pevvdroid.abis=armeabi-v7a
```

## Layout

```
native/openevv          the engine, a submodule, untouched
native/android.mk       two make targets openevv has no reason to carry
native/build-native.sh  cross-compiles the engine, links libevvjni.so
native/tools            drives openevv's own test gate over adb
app/src/main/cpp        JNI bridge
app/src/main/kotlin     the service, engine wrapper, settings
```

The engine is built by its own Makefile rather than CMake, because that Makefile generates the language rules with Python first. Gradle runs the script and picks the result up as prebuilt `jniLibs`.

Everything links into one shared object, `libevvjni.so`. Android only packages files named `lib*.so`, and openevv's own library records a soname of `libeci.so.1`, which nothing would then find.

`native/tools/probe-on-device.sh` stands in for openevv's own probe, so its gate, `test/matrix.sh check enus`, drives an Android build over adb unchanged. That is 98 cases comparing sample hashes and reported answers against baselines recorded on x86.

## Licence

MIT for the Kotlin, the C bridge, the build and the tests. See LICENSE.

Not the engine. openevv is MIT under its own copyright and is fetched from upstream, not copied here.

Not the language data openevv carries. That is transcribed from IBM's Embedded ViaVoice objects and is IBM's. `native/openevv/NOTICE` says what it is and whose it is.

No pronunciation dictionary is included. Those belong to whoever wrote them, and the best known collection carries no licence at all, so it is linked rather than copied. The test fixture here is written for the purpose.
