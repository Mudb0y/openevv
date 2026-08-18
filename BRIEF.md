# openevv — starting brief

Assembled 14 August 2026 out of the Eloquence history research in
`~/Projects/Chat/software/eloquence-history/`. That project is finished; this one
is new. Read this file before designing anything.

## Goal

A portable, usable Eloquence codebase. Stas's targets, in his order of interest:
Rockbox, Symbian, and PowerPC Mac. Symbian is arguably already solved — a
working ARM binary exists at
`/mnt/storage/Software/Symbian/Eloquence_esp_6_1_115.SIS` — so the real targets
are Rockbox and PPC Mac.

This supersedes the approach in `~/Projects/Reloquence`, which disassembles
Apple's arm64e dylibs and rewrites function by function. The material here is a
better starting point: x86 and PowerPC objects with full symbol tables, typed
signatures from two independent manglings, IBM's own module decomposition, and
headers for four API generations.

## What Eloquence actually is

Three layers, not one engine.

**Delta** is Susan Hertz's rule language, begun 1983. The linguistic behaviour
lives in compiled Delta rule tables, not in C. This is why the language modules
are mostly data: `enu.dylib` is 4.8 MB of which only a couple of hundred KB is
code.

**Klatt** is the formant synthesizer that makes the sound. It carries an IBM
copyright dated 1996-1997 and a version banner reading "KlattID version 4.0",
unchanged from 2001 to the present day. In IBM's build tree it is a single
source file, `clsyn.obj` — the classic filename for a cascade-parallel Klatt
synthesizer. The model is published (Klatt 1980; Klatt and Klatt 1990), so this
part can be implemented from literature and tuned against the original rather
than transcribed.

**ECI** is the C API and dispatch layer. `eci.dylib` contains no Klatt code at
all; the synthesizer is compiled into each of the fourteen language modules
separately.

## Size of the job

From IBM's reconstructed build tree (`reference/source-tree/SOURCE-TREE.txt`),
the irreducible core is about 46 source files:

- `klatt` — 1 file, `clsyn.obj`
- `delta` — 14 files: runtime with its own heap, memory manager and I/O
- `dlang` — 12 files: dictionaries, stream arrays and queues, operator table,
  and `runklatt`, the bridge from Delta into the synthesizer
- `ccode` — 12 files: `assign`, `for`, `pointer`, `stack`, `optimize`. Compiler
  back-end names, so almost certainly the Delta-to-C code generator
- `libeTTS` — 7 files: `endians`, `FixUtils`, `LogSteps`, `LogSteps32`,
  `LookupTable`. Fixed-point maths and byte-order handling — the integer-only
  path for targets without an FPU

Note that `ccode` is build-time tooling. If the port keeps the existing compiled
language data rather than regenerating rules from Delta sources nobody has, the
**runtime** core is about 34 files. That distinction is worth settling early
because it changes the scope substantially.

Everything else in the tree is platform glue — mutexes, semaphores, hash tables,
skip lists, linked lists, string handling, INI parsing — which a port rewrites
from scratch anyway. Half the module list falls into that category and none of it
needs reverse engineering.

## What is in this folder

`reference/headers/` — the ECI API across four generations: AIX 6.2.1.10,
6.2.1.11, 6.4.1.1, 6.4.1.2, 6.4.1.3, 6.7.3.2, and Embedded ViaVoice 4.3. The
API grows from 74 functions and 27 language enums at 6.2.1.10 to 86 and 31 at
EVV 4.3, so the series doubles as a changelog. `ecifilter.h` is the filter
subsystem, originally named `mailfilter.h`, IBM 1999-2003. The remaining
headers (`aop.h`, `bfm.h`, `dil.h`, `eal*.h`, `edu.h`, `elg.h`, `esr*.h`,
`mvc.h`, `voc*.h`) are the rest of the EVV 4.3 SDK surface; `esr*` is speech
recognition and not needed.

`reference/symbol-maps/evv4.3-x86-msvc/` — module lists and symbol dumps for all
31 static libraries in the EVV 4.3 SDK, plus demangled names. `ecienus.lib` alone
is 207 objects and 35,515 symbols. MSVC mangling encodes types, so these
demangle to real declarations, and the stdcall decorations give exact argument
byte counts.

`reference/symbol-maps/aix-powerpc-xlc/` — the same for 106 AIX archives,
PowerPC, xlC mangling. Independent confirmation of the same signatures.

`reference/source-tree/` — the reconstructed build tree, recovered from source
paths left in shipped binaries. 2,947 paths under `E:/build/embedded`.

`reference/samples/` — IBM's own ECI sample application in C, six source files
plus headers and a makefile. The only real IBM source that survives anywhere, and
it shows correct API usage.

`reference/data/KonaVoicePresets.plist` — 112 voice presets across 14 languages
with the full formant parameter set per voice: `vocalTract`, `headSize`,
`pitchBase`, `pitchFluctuation`, `breathiness`, `roughness`, `volume`, `speed`,
`eciVoiceNumber`. Useful as parameter defaults and as a target to match.

`reference/data/eti-SPR-phoneme-alphabet.txt` — ETI's documented phoneme
alphabet, single-character, with the two reduced vowels, the flap, the syllabic
nasal and the glottal stop.

## Where the bulk material is

Nothing large is duplicated here. All of it is under
`/mnt/storage/Software/eloquence-archive/`:

- `ibm-public/embedded-viavoice-sdk/extracted/evv4.3/` — the full EVV 4.3 SDK,
  221 MB: the 31 static libraries themselves, build tools (`bldrom`, `bldvocab`,
  `bldwords`, `bldpcm`, `rompeek`), and an eCTTS voice tree
- `ibm-public/aix-extracted/` — 83 AIX filesets, 194 MB, big-endian PowerPC.
  The language data in these is the correct byte order for a PPC target
- `ibm-public/voicetoolkit-extracted/` — 64 Windows mini-SDKs across four
  toolkit generations, engine versions 6.7.3.1 through 6.7.3.3
- `ibm-public/embedded_viavoice/` — 24 Pocket PC ARM builds from 2001-2002, the
  small-footprint line, whole stack in a single 2.35 MB binary
- `datajake/` — the ETI trunk: Eloquence 3.0, 3.3, 4.0, 5.0, 6.1
- `apple-kona-macos27/` — Apple's current build plus a per-module `__cstring`
  hash manifest
- `~/Projects/apple-eloquence-elf/vendor/tvOS-18.2/` — Apple's dylibs, arm64 and
  x86_64 fat

`ibm-public/EXTRACTING-BFF.txt` explains how to open AIX `.bff` filesets;
`bffextract` is not in nixpkgs and needs `gcc` in the shell to build.

## Constraints and gotchas already established

The language data cannot be rewritten or regenerated. Keep the original files.

Endianness is a real hazard and is already solved for PPC: the AIX filesets are
big-endian builds, so their data is the right byte order for a PowerPC target.
Do not byte-swap little-endian data; use the AIX data instead.

Version strings are useless as identifiers. Eloquence has claimed 6.1.0.0 across
twenty years of ports. Identify builds by section content hashes, symbol sets and
build metadata instead.

Fixed-point support exists in the original as `libeTTS`. Rockbox targets without
an FPU need that path, not the desktop float path.

Apple exports `eciRegisterKlattHooks2`, which registers constant and per-frame
hooks into the synthesizer. That is a working reference for how the Klatt layer
is driven, and it is how Apple exposes ten formant parameters to end users.

## One thing to be careful about in public

Reloquence has been described as clean-room. This port is not, and neither is
that one in practice — both work from disassembly and symbols of legally-held
binaries. That is ordinary reverse engineering and fine, but do not call it
clean-room in anything public, because it is a claim people check.

## Decisions for Stas, to raise one at a time

These are not settled and should not be guessed at. He architects; ask him.

- Whether the port keeps the compiled language data (runtime core, about 34
  files) or also reimplements the Delta compiler so new rules can be authored
  (about 46 files).
- Which target drives the first milestone. Rockbox and PPC Mac pull the design
  in different directions on memory, threading and floating point.
- Whether Klatt is implemented from the published model and tuned to match, or
  transcribed from the objects. The first is faster and legally cleaner; the
  second is closer to bit-exact.
- What "usable" means as an acceptance test — an audible sentence, a phoneme
  stream matching a reference, or bit-identical PCM against a known build.
