# Building openevv

What you need, what to build, and what every variable does. The rules are the long part of a build and have a file of their own in `docs/rules.md`; adding a language is in `docs/language.md`, the Windows side in `docs/windows.md`, and what proves any of it in `docs/testing.md`.

## What you need

A C compiler and Python 3. The language data is in the tree, so there is no IBM SDK to find and nothing is downloaded. Python is wanted twice. Every build writes the rules the engine runs out of the text in `lang/<tag>/rules`, which is about two seconds a language and is where those rules live now; and an ordinary build then decompiles them into C, which is two minutes and is what `RULES` below is about. `make RULES=bytecode` skips the second and not the first.

Two more things are wanted only for particular jobs. A thirty-two bit compiler builds the thirty-two bit engine. Wine and IBM's own objects run the comparison tests, which is the only automatic check that the audio is right.

On this machine all of those come from the flake, and `nix develop` puts them on the path.

## Building

    make

That builds `build/libevv.a` and `build/evv`, which speaks. From nothing, that is about two and a half minutes: two seconds to write the rules out of the text, a little over two minutes for Python to decompile them into C, and about fifteen seconds to compile the thirteen megabytes of it across twenty-four cores. Once those files exist they are not written again unless the text, the decompiler or the bytecode changes.

    make RULES=bytecode

That is the small, quick build -- half a minute on one core, under twenty seconds with `make -j8`, plus the two seconds a language the rules cost whichever form they are in. It speaks the same samples and it is the one to use while working on anything but the rules. What it costs is speed, which the next section puts numbers to.

    make so

That builds `build/libeci.so.1`, with `build/libeci.so` beside it: the same engine under the names IBM published, which is what a program of someone else's links against or loads by name. `include/eci.h` is what it compiles against, and `make so32` builds the same library thirty-two bit. `docs/using.md` is how to use it and `docs/api.md` what every call does.

    make probe

That builds `build/probe` instead: the same engine behind the front the tests drive. It prints what the engine answered at every step so those answers can be set against IBM's, which is why it is not the thing to run by hand.

    make evv32
    make probe32

The same two, thirty-two bit. That build is a check rather than a target: a difference between the word sizes is a layout mistake, and this is what makes one show up early. It needs a thirty-two bit compiler, which is `CC32`.

On a Nix machine `nix build` makes the same binary at `result/bin/evv`, and `nix run . -- -o hello.wav "text"` runs it without installing anything. `nix develop` is the shell the rest of this assumes: the thirty-two bit compiler, Wine and Python on the path.

`make install` copies the binary to `/usr/local/bin/evv`, or wherever `PREFIX` and `DESTDIR` say. There is nothing else the command needs: it reads no file of its own at run time and wants no library but the C one, libm and pthreads. `make install-lib` is the other half and puts the shared library and the header under `LIBDIR` and `INCDIR`, which default to `PREFIX`; it is a separate target because it is a separate build, so `make so` comes first. `make clean` takes the objects and the binaries away and leaves the generated C alone.

## The variables

`CC` is the compiler for this machine, `cc` by default. `CC32` is the thirty-two bit one, which on this machine is the cross compiler the flake provides, `i686-unknown-linux-gnu-gcc`, and elsewhere is usually the host compiler with a flag: `make evv32 CC32="gcc -m32"`. `NM` is used by `make missing`. `OPT` is the optimisation level, `-O2`. `CFLAGS` is added to both builds after everything else, so it can override.

`LANGS` says which language modules go in, and `EVVLANG` is the name for one of them: `make LANGS="lang/enus lang/dede"` builds both and the first named is what a caller gets when it asks for nothing in particular. A build with anything but English alone names what it makes after what is in it, so builds sit beside each other rather than over each other, and so that an archive left over from another language set cannot be linked in by mistake.

`EVVPLAIN=1` keeps the shared library's plain name, and its soname with it, however many languages are in it. It is what a release is built with: the archives carry every language and the library still has to be the `libeci.so.1` a caller opens, since nothing loading it can be asked to know what is inside. It changes no other name, so the archives go on saying what they hold.

`RULES` chooses which form of the language's rules gets linked, and is explained next.

## Running

    ./build/evv -o hello.wav "Hello from Eloquence."
    ./build/evv -f speech.txt -o speech.wav
    ./build/evv "Hello from Eloquence." | aplay -q -
    echo "Hello from Eloquence." | ./build/evv | pw-play -

With no `-o` it writes the wave to standard output, unless that is a terminal, in which case it says so rather than filling the terminal with samples. With no text it reads standard input.

`-v` picks one of the eight voices, `-s` the speed, `-p` the pitch and `-V` the volume. Those numbers are the engine's own; `-r` makes them a person's instead, so speed is words per minute and pitch is hertz. `-l` prints what each voice is set to, in whichever units are in force.

`-R` is the sample rate, which is not the speed. Nought to six are 8,000, 11,025, 22,050, 16,000, 32,000, 44,100 and 48,000 hertz, in the order IBM numbered the first four and this port the rest. The default is 11,025.

Above 11,025 the engine goes on running at 11,025 and the rate is raised from there, so the voice is the same one at every setting; `docs/status.md` says why that is better than synthesising at the higher rate and what it does to the spectrum. A number of 8,000 or more is that rate in hertz, raised the same way, so `-R 24000` is a rate nobody numbered. `EVV_UPSAMPLE` says how the raising is done -- `sinc` by default, or `cubic`, `linear`, `hold` or `zeros` -- and `none` synthesises at the rate instead, so the two halves can be compared. `none` is an experiment rather than a setting, and `docs/status.md` says why: synthesised above 11.025 the engine loses up to 24 dB through the consonant band, for a reason that is in the Klatt design rather than in this port. `EVV_SINC_CUTOFF` and `EVV_SINC_TAPS` move where the sinc stops passing the band and over how many samples it stops, which is the one knob that changes how bright the result is without letting images back in. `tools/measure/rates.py` reads the resulting wave files and says it in numbers:

    ./build/evv -R 1 -o held11.wav "She sells sea shells."
    ./build/evv -R 2 -o held22.wav "She sells sea shells."
    EVV_UPSAMPLE=hold ./build/evv -R 2 -o held22.wav "She sells sea shells."
    tools/measure/rates.py held11.wav held22.wav native22.wav

## The sixty-four bit build

The Delta machine keeps addresses in thirty-two bit values, so on a wider host everything it can point at has to live somewhere such a value can still name. `src/port/evv_arena.c` maps a region low in memory and everything the machine holds comes out of it. That includes the language's own data: the rules name their constants by address, and the set and action tables hand over an address per entry, so `src/delta/delta_low.c` copies those stores out of the program at startup and translates an address into its copy at the few places where one becomes a value. A pointer from anywhere else says so and stops.

Which is why there is no `-no-pie` and no fixed image base any more. The program can be loaded wherever the loader fancies, ASLR and all, which is what makes a shared library possible: a library does not get to choose where it goes. The Makefile asks the compiler how wide a pointer is and leaves the arena out altogether when the host is thirty-two bit, where a pointer is a value already.

`-Werror=int-conversion` and `-Werror=incompatible-pointer-types` are on for both builds. A narrowed field assigned from a pointer, or the other way about, was the whole of what went wrong in the sixty-four bit port, so it is an error rather than a warning nobody reads. The rest of the warnings are off: this is transcribed code and it is loud.

## Getting IBM's objects

None of this is needed to build. It is needed for two things: the comparison tests, which speak every case through IBM's own binary as well as ours, and the lifters, which is how the language data in `lang` was made and how another language would be.

Everything comes out of IBM's Embedded ViaVoice 4.3 SDK for Windows, which IBM still serves from its public download host:

    https://public.dhe.ibm.com/software/pervasive/tools/viavoice/sdk/evvWXP.exe

114,984,719 bytes, dated 30 November 2004, sha256 47182a6b16bd8a5335944a1a03058ce52cba83b03de9da700e97fea68be0c29f. Despite the .exe it is an ordinary Microsoft cabinet, so it unpacks on any machine, with `nix shell nixpkgs#p7zip` first if that is how the machine gets its tools:

    7z x evvWXP.exe

That gives `evv4.3/wxp`, with the libraries, the headers, IBM's documentation and its own sample applications under it. What the tools here read is the static libraries in `evv4.3/wxp/lib/NT/X86/COMMON`. `ecienus.lib` is US English and is the one this engine was made from; `eciengb`, `ecidede`, `ecieses`, `eciesus`, `ecifrfr`, `ecifrca`, `eciitit` and `ecijajp` are the other eight formant languages, and a `C`-suffixed library beside one of them is the concatenative build of that language, which uses recorded speech rather than the synthesiser and is not what any of this reads.

Point `EVV_LIBDIR` at that directory and run the extractors:

    EVV_LIBDIR=/somewhere/evv4.3/wxp/lib/NT/X86/COMMON tools/sdk/extract.sh
    EVV_LIBDIR=/somewhere/evv4.3/wxp/lib/NT/X86/COMMON tools/sdk/extract-langs.sh

`tools/sdk/extract.sh` fills `analysis/enus` with the 207 objects of the English module, which is what `make -C reference` links and what every lifter reads. It also writes `analysis/obj` and `analysis/delta-ibm`, which carry the same objects with IBM's symbols renamed out of the way; that was for standing our code beside IBM's in one binary, and that harness is retired.

`tools/sdk/extract-langs.sh` puts each of the other eight languages in `analysis/<tag>`, which is for comparison rather than for building. Both extractors want `llvm-ar`, `llvm-objdump` and the mingw `objcopy`, so both run inside `nix develop`.

Read those objects with `llvm-objdump -d -r --no-show-raw-insn` and not with binutils `objdump -d`. Every function is its own COMDAT `.text` section and MSVC gave local labels the same names in different sections -- `$L61863` occurs several times in one object -- so binutils takes the recurring name for a function boundary, resynchronises the instruction stream at that byte and prints plausible nonsense from there to the end of the section. In `JpnUtil::ConvertDakuten` it produced `into` and `add %al,(%eax)` where the code is a compare and a conditional jump, and nothing warned. If a function's control flow stops making sense in the middle, suspect the disassembler first. The lifters go on using binutils `objdump` and `nm` for section bytes, headers, symbols and relocations, none of which is affected; it is instruction decoding that is wrong.

IBM's public host carries more than the SDK: the AIX packages of the same engine, whose headers are how the interface across four generations was read, and the Pocket PC runtimes, are under `/software/` beside it. None of it is needed here.

Mainline ViaVoice is a different product line and not a wider language set. Embedded ViaVoice is the small-footprint, fixed-point build and comes as static object libraries, which is the only reason any of this was possible. The desktop engine is mainline and floating point, and ships runtime data files rather than objects -- so its seventeen languages, Danish and Finnish and Korean among them, are not waiting to be lifted. There is nothing compiled to read, and the synthesiser underneath them is not the one in `klatt_*.c`. The nine in the EVV 4.3 SDK are the reachable set.
