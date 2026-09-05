# Using the engine in your own program

Three ways in, what each costs, and the things about driving this engine that no interface documentation would tell you. `docs/api.md` is the call-by-call reference and `docs/quirks.md` the list of traps. This is how to get from a checkout to speech in your own build.

## What you get

    make so

That builds `build/libeci.so.1` with `build/libeci.so` beside it, and `include/eci.h` is what to compile against. `make install-lib` copies both under `PREFIX`, which is `/usr/local` unless you say otherwise; `LIBDIR` and `INCDIR` move each half.

    make win
    make win32

Those cross-compile `build/eci.dll` and `build/eci32.dll` with mingw. Both export the same names from the same source as the ELF library, and `docs/windows.md` is the Windows side in full.

A first build is about two and a half minutes, most of it decompiling the rules into C. `make RULES=bytecode so` is half a minute and speaks the same samples at rather less than half the speed; it is the one to use while working on the program rather than on the engine. `docs/building.md` says what every variable does.

The library wants nothing but the C library, libm and pthreads. It reads no file of its own at run time: the language data, the voices and the settings are all in the image.

## Route one: link against it

    cc -Iinclude myprogram.c -L build -leci -o myprogram

`-leci` finds `build/libeci.so`, which is a link to the real file; what the program records is the soname, `libeci.so.1`. So the loader has to be able to find that at run time -- `make install-lib` and `ldconfig`, or `-Wl,-rpath,$PWD/build` while working out of the tree, or `LD_LIBRARY_PATH`.

A program that would rather not link at all can load the library by name instead, which is route two below and is what every existing consumer does.

Here is the whole of a program that speaks. It compiles as it stands.

    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <time.h>
    #include <eci.h>

    #define FRAME 2048

    static short  frame[FRAME];
    static short *kept;
    static size_t nkept, room;

    static int ECICALL on_message(ECIHand h, ECIMessage msg, int param,
                                  void *data)
    {
        (void)h;
        (void)data;
        if (msg == eciWaveformBuffer) {
            if (nkept + (size_t)param > room) {
                room = (nkept + (size_t)param) * 2 + FRAME;
                kept = realloc(kept, room * sizeof *kept);
                if (kept == 0)
                    exit(1);
            }
            memcpy(kept + nkept, frame, (size_t)param * sizeof *frame);
            nkept += (size_t)param;
        }
        return eciDataProcessed;
    }

    int main(int argc, char **argv)
    {
        const char  *text = argc > 1 ? argv[1] : "Hello from Eloquence.";
        unsigned int langs[32];
        int          n = 0;
        ECIHand      h;
        struct timespec tenth = { 0, 10 * 1000 * 1000 };

        if (eciGetAvailableLanguages(0, &n) || n < 1) {
            fprintf(stderr, "no language in this build\n");
            return 1;
        }
        if (n > 32)
            n = 32;
        eciGetAvailableLanguages(langs, &n);

        h = eciNewEx((int)langs[0]);
        if (h == NULL_ECI_HAND) {
            fprintf(stderr, "no instance\n");
            return 1;
        }

        eciRegisterCallback(h, on_message, 0);
        if (!eciSetOutputBuffer(h, FRAME, frame)) {
            fprintf(stderr, "it refused the buffer\n");
            return 1;
        }

        if (!eciAddText(h, text) || !eciSynthesize(h)) {
            fprintf(stderr, "it refused the text\n");
            return 1;
        }
        while (eciSpeaking(h))
            nanosleep(&tenth, 0);

        eciDelete(h);
        printf("%lu samples\n", (unsigned long)nkept);
        return 0;
    }

Three lines of that are the ones people leave out. `eciSetOutputBuffer` is what decides the samples come to you at all: without it the engine is sending them to an audio device, this port has no audio device, and `eciAddText` refuses -- which is worth checking, because it is the only call that says so. `param` in the callback is a count of samples rather than bytes. And the buffer is reused for every message, so the callback has to have copied what it wants before it returns.

## Route two: load it by name

This is what a screen reader add-on does, and what speech-dispatcher's Eloquence module does: `dlopen` the library, `dlsym` each name, call through the pointers. Nothing links, so the program does not need the header and does not care which ECI implementation it found.

`test/lib/dll.c` is a working example of exactly that, in about two hundred lines, and it takes either kind of library:

    make sotest

builds and runs it against `build/libeci.so`. `EVV_ECI_LIB` names another library, so the same binary drives `eci.dll` under Wine. `test/lib/dll.py` is the same thing through ctypes, which is a different question -- ctypes has its own idea of a handle and of a calling convention -- and it too takes either kind now.

Two things to get right when loading by name. On thirty-two bit Windows the published interface is stdcall, so a callback made with `WINFUNCTYPE` rather than `CFUNCTYPE` and calls made through `WinDLL` rather than `CDLL`; off Windows there is one convention and the distinction disappears. And a handle is a pointer: ctypes truncates one to an int unless the `restype` and `argtypes` say otherwise, which on a sixty-four bit machine hands the engine half an address.

speech-dispatcher's `sd_eloquence` module resolves forty-two names when it loads an ECI runtime, opening `<dir>/eci.so`; all forty-two are exported here. So is everything else IBM published: seventy-one names, the same seventy-one, checked by diffing the export tables.

## Route three: run the command

    ./build/evv -o hello.wav "Hello from Eloquence."
    ./build/evv "Hello." | aplay -q -

`build/evv` writes a wave file, or writes the wave to standard output when there is no `-o` and standard output is not a terminal. For a program that wants a file now and again rather than a synthesiser in its own process, this is the whole of the integration, and `./build/evv -h` says what the options are. It is also the fastest way to hear whether a change to the language data did what you meant.

## Threads

The engine's threading model is worth stating outright, because most of what goes wrong with it is a program assuming one of these is different.

Each instance owns a synthesis thread. `eciSynthesize` returns as soon as the work is queued; the speaking happens on that thread, and so does every callback.

**The callback runs on the engine's thread.** Whatever it does, the engine is not synthesising while it does it. A callback that hands the samples to a player and blocks until the player has room is pacing the engine on purpose, which is usually what you want; a callback that takes a lock the driving thread might hold is a deadlock.

**A callback may not call back into the same instance.** Calls are refused rather than serialised while another is running, so what happens is not a crash but a nought nobody checked.

Five drive loops have been run on Linux and all of them are clean: polling `eciSpeaking`, waiting on `eciSynchronize`, synthesising on a worker thread, the same with a cancelling thread polling, and `eciStop` called from a second thread. `make stopthread` and `make interrupt` are the harnesses for the last two.

Two instances in one process is fine and is checked: `test/lib/langs.py` speaks every language a build has, all at once from one process, and holds each against what it says alone.

## Cancelling

There are three ways and they cost the same, because all three wait for the same thing: `eciStop` from any thread, answering `eciDataAbort` from the callback, and simply letting the utterance finish and throwing the samples away.

The engine cannot abandon an utterance it has been told to stop. That is settled rather than open -- the rules build a shared structure as they go and later rules assume the earlier ones finished it, so there is no safe abandonment point in the machine, and six ways of adding one were tried and all fault. A stop waits for the synthesis thread to finish the message it is on.

What that costs, measured with the rules compiled: about 27 milliseconds to cancel and speak again, against about 86 with the rules interpreted. So `RULES=c`, which is the default, is what a program that interrupts a lot wants.

If you are discarding samples, answer `eciDataProcessed`. Answering `eciDataNotProcessed` means "my buffer is full, offer it again" and costs a flat thirty milliseconds a buffer.

## Where a program may cut an utterance

A sentence end is free. A cut in the middle of a sentence costs a full-stop pause when speech resumes, because the engine is starting a new utterance and prosody starts over. A program feeding a screen reader's worth of text does better to submit whole sentences and cancel between them than to submit a paragraph and cut into it.

## Several languages

    make LANGS="lang/enus lang/dede" so

puts both in one library, `eciGetAvailableLanguages` answers both, and the first named is what `eciNew` gives. The libraries are named for what is in them -- `libeci-enus-dede.so.1` -- so builds sit beside each other rather than over each other. `make EVVPLAIN=1 ...` keeps the plain `libeci.so.1` instead, soname and all, which is what the released library is built with: it carries every language and still has to be the file a caller opens.

Change language with `eciSetParam(h, eciLanguageDialect, ...)` on an instance that is not speaking, or make an instance per language. Both work. The speak window on Windows does the first, setting the language on the instance it already has rather than building another, and it will not do it while something is being said; `test/lib/langs.py` does the second, and holds each language to what it says alone.

Ten languages are in the tree: US and British English, German, Castilian and American Spanish, French and Canadian French, Italian, Japanese, and Polish. The released library and the released DLLs carry all ten. Nine of those are IBM's own data lifted out of its objects. Polish is not IBM's and is not finished -- its rules are still Italian's where nothing here has replaced them, and `make EVVLANG=lang/plpl census` counts how much. `docs/status.md` says where each stands.

## Sample rates

The default is 11,025 hertz and that is what Eloquence has always sounded like. `eciSetParam(h, eciSampleRate, n)` takes nought to six for 8,000, 11,025, 22,050, 16,000, 32,000, 44,100 and 48,000, or any number of 8,000 or more as a rate in hertz.

Above 11,025 the engine goes on synthesising at 11,025 and the rate is raised from there, so the voice is the same one at every setting rather than a different one at each. `docs/notes/sample-rates.md` says why that is better than synthesising at the higher rate, and it is not a shortcut: synthesised outright above 11,025 the engine loses up to 24 dB through the consonant band, for a reason in the Klatt design rather than in this port.

Four environment variables reach the engine from outside and are worth knowing exist, since they work on a library as well as on the command. `EVV_UPSAMPLE` chooses how a rate is raised -- `sinc` by default, or `cubic`, `linear`, `hold`, `zeros`, or `none` to synthesise at the rate instead. `EVV_SINC_CUTOFF` and `EVV_SINC_TAPS` move where the sinc stops passing the band and over how many samples. `EVV_ARENA_TRACE` reports what the engine's low memory region is holding.

## Memory

The engine keeps everything the Delta machine can point at in a region mapped low in memory, because that machine holds addresses in thirty-two bit values. The program itself may be loaded anywhere, which is what makes a shared library possible at all; the language's data is copied into that region at startup rather than named where it lies. A machine that cannot map anything below two gigabytes would say so rather than misbehave.

An instance costs a four megabyte frame stack, taken by its synthesis thread on the first rule it runs and given back when the thread ends. It also leaks sixty-four bytes, which moves the ceiling on instances made and thrown away from sixty-two to about four million, so no program will meet it.

## Checking your integration

Three things in this tree are worth running against a program that embeds the engine, because each catches something the others cannot.

`make sotest` speaks through the library by name and says how many samples came out. Hold that against `./build/evv` on the same sentence: they are byte for byte the same file, and if yours is not, the difference is in how you are driving it.

`test/lib/langs.py build/libeci.so` speaks every language the build has from one process and holds each against what it says alone. That is the check for anything that has quietly stayed global.

`make matrix` is the engine's own gate -- 979 cases over ten languages -- and it wants neither Wine nor IBM's objects. If you have changed anything under `src` or `lang`, that is what says whether it moved.
