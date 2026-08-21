# What each directory is

## In the tree

`src` is the engine: hand-written C, one file per object in IBM's own module
decomposition. `eci_` files are the ECI layer, which is the published interface
and the machinery behind it. `delta_` files are the Delta machine, which runs
the language's rules. `klatt_` files are the formant synthesiser that makes the
sound. `port_` and `evv_` files are what the port supplies itself: threads,
timers, files, and the low arena the sixty-four bit build needs.

The names are IBM's, and they are kept that way on purpose. A file named for
the object it came from can be checked against that object; renaming them would
look tidier and cost real verification.

Three things there are not from an object. `src/delta_rules_none.c` is the empty
table of rules-written-as-C that an ordinary build links instead of the thirteen
megabytes. `src/port_win32.c` is the Win32 platform layer, which the Windows
build and the reference build both use and the Linux build does not.
`src/evv_land.c` is the landing place a backtrack jumps to, which is ours
because the C library's does not fit what the engine does with it.
`src/delta_low.c` copies the language's own data out of the program into the
arena, which is what lets the program be loaded anywhere -- and therefore what
lets it be a shared library.

`lang/enus` is US English. `delta_rules_enus.c` is the rules as bytecode,
`delta_consts_enus.c` the constant blobs they read, `delta_link_enus.c` the
statement and field tables, `delta_sets_enus.c` the lookup sets and dictionary
action tables, `delta_globals_enus.c` the variable declarations,
`delta_rules_shim.c` each rule under its own name, `delta_rules.h` what the
interpreter needs of all that, `eci_ini_enus.c` the voice presets, and
`enus.dict` the dictionary in a form a person can edit. All of it is transcribed
from IBM's objects.

`cli` holds the two console front ends. `evv.c` is the command a person runs:
options, a wave file or a pipe, and nothing printed that was not asked for.
`probe.c` is the one the tests drive, which prints what the engine answered at
every step. `reference/speak.c` is a third, driving IBM's engine through the
published ECI names, and it is separate because those are different names for
the same calls and joining them would mean a conditional around every one.

`win` is the Windows front, and it holds two different things. `speak.c` with
`speak.rc` is the speak window: the only front end that plays what the engine
makes, through waveOut, and Windows-only for the same reason it exists -- that
is the platform someone can be handed one file for. `eci_api.c` is the library:
the engine exported under the names IBM published, so that a program expecting
IBM's `eci.dll` can load ours instead. `eci.ini` is a stub that goes beside the
library because add-ons look for one and rewrite a path in it.

`nvda` is the NVDA add-on, which is Python rather than C because that is what a
screen reader loads. `addon/synthDrivers/openevv.py` is the driver the reader
talks to: the settings it shows and the turning of a speech sequence into text
with annotations in it. `_openevv.py` beside it is everything below that: the
library through ctypes, the one thread that is allowed to call into it, and the
audio. `manifest.ini.in` is what NVDA reads to identify the add-on, with the
version filled in at packing time, `build.py` does the packing, and
`test` holds the three checks: `sequence.py` for the driver's half, `engine.py`
for the engine layer against a library stood in for, and `windows.py`, which
wants a real Windows and the real library and is where the faults have been
found.

`test` holds `suite.sh`, which runs the categories of cases through both
engines, `compare.sh`, which does one category, and `cases`, the text itself.
`hash.sh` and `samples.sha256` are the check that needs neither Wine nor IBM.
`dll.c` and `dll.py` speak through the library the way something else would,
once from C and once through ctypes.

`tools` holds three kinds of thing. The lifters turn IBM's objects into the C
in `lang`: `extract.sh` and `extract-langs.sh` unpack the SDK, `delta-lift.py`
reads the compiled rules, `delta-emit.py` writes them out as bytecode,
`delta-link.py` and `delta-sets.py` the tables beside them, `gen-globals.py` the
variable declarations, and `delta-tables.py`, `gen-tables.py` and
`catalogue.py` the synthesiser's own tables. The decompiler is
`delta-decompile.py` with `delta-census.py`, which turn that bytecode into
readable C. The analysers answer questions about what is in there:
`delta-lexicon.py`, `delta-dict.py`, `delta-wrappers.py`, `delta-arms.py`,
`delta-shape.py` and `missing.py`. `say.sh` and `delta-check.sh` are the two
checks that need no Wine.

`reference` builds IBM's own binary from IBM's own objects, under Wine. It is
all that is left of the differential harness that made the port.

`.github` holds the workflow that builds both binaries on every push and cuts a
release with the Windows exes when a tag is pushed.

`docs` is this.

## Not in the tree

`analysis` holds the objects extracted from IBM's SDK by `tools/extract.sh`,
and the eight other languages lifted by `tools/extract-langs.sh`. It is the
input to every lifter and none of it is needed to build. The reference material
the port was read with -- IBM's headers for four API generations, symbol maps
for 199 objects, the phoneme alphabet, the SDK's sample code and the original
source tree listing -- was in the tree as `research` until 20 August 2026 and
is in git history at that commit.

`build` is output: `evv` and `probe` with their objects under `obj`, the
thirty-two bit pair with theirs under `obj32`, and `reference/speak.exe` with
its own. `lang/enus/delta_rules_c.c` is output too, which is why it is not
tracked. `.wine` is a Wine prefix for the reference binary.
