# Building openevv

## What you need

A C compiler. That is the whole of it for an ordinary build: the language data
is in the tree, so there is no IBM SDK to find and nothing is downloaded.

Three things are wanted only for particular jobs. Python 3 writes the rules out
as C, which `make RULES=c` asks for and an ordinary build does not. A
thirty-two bit compiler builds the thirty-two bit engine. Wine and IBM's own
objects run the comparison tests, which is the only automatic check that the
audio is right.

On this machine all of those come from the flake, and `nix develop` puts them
on the path.

## Building

    make

That builds `build/libevv.a` and `build/evv`, which speaks. From nothing, that
is about half a minute on one core and under twenty seconds with `make -j8`.

    make probe

That builds `build/probe` instead: the same engine behind the front the tests
drive. It prints what the engine answered at every step so those answers can be
set against IBM's, which is why it is not the thing to run by hand.

    make evv32
    make probe32

The same two, thirty-two bit. That build is a check rather than a target: a
difference between the word sizes is a layout mistake, and this is what makes
one show up early. It needs a thirty-two bit compiler, which is `CC32`.

`make install` copies the binary to `/usr/local/bin/evv`, or wherever `PREFIX`
and `DESTDIR` say. There is nothing else to install: it reads no file of its own
at run time and wants no library but the C one, libm and pthreads. `make clean`
takes the objects and the binaries away and leaves the generated C alone.

## The variables

`CC` is the compiler for this machine, `cc` by default. `CC32` is the
thirty-two bit one, which on this machine is the cross compiler the flake
provides, `i686-unknown-linux-gnu-gcc`, and elsewhere is usually the host
compiler with a flag: `make evv32 CC32="gcc -m32"`. `NM` is used by `make
missing`. `OPT` is the optimisation level, `-O2`. `CFLAGS` is added to both
builds after everything else, so it can override.

`RULES` chooses which form of the language's rules gets linked, and is
explained next.

## The rules, twice

The language's rules exist in the tree as bytecode, and the engine has an
interpreter for them. They also exist as C: `tools/delta-decompile.py` writes
all 3,377 of them out of that same bytecode into `lang/enus/delta_rules_c.c`,
and the interpreter prefers a rule written as C wherever it finds one.

Both speak the same samples, so an ordinary build links an empty table and runs
every rule as bytecode. The C is thirteen megabytes in one file, which is seven
minutes of compiler and Python to write it first, and it is asked for by name:

    make RULES=c

That is the form to build when the decompiler itself is what is being worked
on, since it is the C that a change to it changes. `make rules` writes the file
without building anything. It is not kept in the tree, because every change to
the decompiler rewrites the whole of it.

## Running

    ./build/evv -o hello.wav "Hello from Eloquence."
    ./build/evv -f speech.txt -o speech.wav
    ./build/evv "Hello from Eloquence." | aplay -q -
    echo "Hello from Eloquence." | ./build/evv | pw-play -

With no `-o` it writes the wave to standard output, unless that is a terminal,
in which case it says so rather than filling the terminal with samples. With no
text it reads standard input.

`-v` picks one of the eight voices, `-s` the speed, `-p` the pitch and `-V` the
volume. Those numbers are the engine's own; `-r` makes them a person's instead,
so speed is words per minute and pitch is hertz. `-l` prints what each voice is
set to, in whichever units are in force.

## Testing

    make probe
    make -C reference
    nix develop --command test/suite.sh

The suite speaks each case through our engine and through IBM's and compares
the samples. It needs Wine, and it needs IBM's objects in `analysis/enus`,
which `tools/extract.sh` puts there out of the SDK. Building the reference
binary writes it to `build/reference/speak.exe`.

Six categories run by default: plain text, UTF-8, annotations, annotations with
the annotation input type on, real-world text with the parameters read back in a
person's units, and the user dictionary. A seventh, `long`, is paragraphs rather
than sentences and is left out of the default set because under Wine it takes
minutes. Name any of them to run only those: `test/suite.sh plain long`.

`EVV_NATIVE=$PWD/build/probe32 test/suite.sh` runs the same cases through the
thirty-two bit build. Both word sizes have to pass, and so does `RULES=c`.

Without Wine there is no automatic check that the audio is right.
`tools/say.sh` speaks a sentence and plays it, laying the dictionaries down
first, so a change to the language data can be heard.

`tools/delta-check.sh` is the other check. It holds named rules written as C
against the same rules left as bytecode, by tracing every call both ways and
comparing the traces line for line.

## The sixty-four bit build

The Delta machine keeps addresses in thirty-two bit values, so on a wider host
everything it can point at has to live somewhere such a value can still name.
`src/evv_arena.c` maps a region low in memory for that, and `-no-pie` puts the
program's own tables at four megabytes rather than wherever the loader fancies.
Both are load-bearing, not preferences. The Makefile asks the compiler how wide
a pointer is and leaves both out when the host is thirty-two bit already.

`-Werror=int-conversion` and `-Werror=incompatible-pointer-types` are on for
both builds. A narrowed field assigned from a pointer, or the other way about,
was the whole of what went wrong in the sixty-four bit port, so it is an error
rather than a warning nobody reads. The rest of the warnings are off: this is
transcribed code and it is loud.
