# What works and what does not

Last measured 20 August 2026.

## Works

The engine speaks, and it speaks IBM's samples. All 81 cases in six categories
come out byte for byte identical to IBM's own binary: plain text, UTF-8,
annotations, annotations with the annotation input type on, real-world text with
the parameters read back in a person's units, and the user dictionary.

That holds in all four configurations the tree can build: thirty-two and
sixty-four bit, each with the rules run as bytecode and with the same rules run
as the C they decompile to.

Nothing is borrowed at build time. `make missing` answers nothing, which is the
check that no call has quietly gone back to IBM's objects. The language data is
all transcribed and in the tree, so an ordinary build needs a C compiler and
nothing else.

Dictionaries can be edited. `tools/delta-dict.py` writes `lang/enus/enus.dict`
out of the tables and reads it back in, so a pronunciation can be changed, laid
down and heard.

All nine languages in the SDK lift and decompile: US and British English, both
Spanishes, both Frenches, German, Italian and Japanese. Only English is built.

## Not done

Live audio. The engine hands its samples to the caller, and `build/evv` writes
them as a wave file or down a pipe; nothing sends them to a sound card as they
are made. Deliberately left until last, because it is mostly straightforward
and the decompiling mattered more.

The compiler. The rules are readable C, but there is no way to write a new rule
except by writing that C. This is the next piece of work and the gate to adding
a language.

Polish, which is the reason the compiler matters. Nothing started.

Anything but English at build time. The other eight lift and decompile, but the
build knows only `lang/enus`.

## Partly done

The rules read as rules, to eleven passes of the decompiler. What that means
concretely: calls sit with their arguments, wrapper rules say which primitive
they stand for, state reaches say which language variable they touch, frame
reaches say which argument they are, switch arms say which alternative they are,
register halves say which half, and the machine's dead leavings are gone.

What is left in them: 63,739 gotos, of which 22,389 are the backtracking
dispatch and are correct as gotos. The rest are branches that could not be
structured without changing meaning.

Three things in the rules cannot be recovered and are not going to be. The
global variables' names are gone: they are known only by kind and number,
because the only record of them is a disassembly of `delta_new` that carries
kinds and not names. The frame below a rule's arguments is unnamed, because
nothing in the object says what any of it is for. And the 152 wrapper rules that
do arithmetic as well as calling keep their names.

## Known limits

The test suite needs Wine and IBM's objects, because it compares against IBM's
binary. Both are obtainable: `docs/building.md` says where IBM's SDK still is.
Without them there is no automatic check that the audio is right, only
`tools/say.sh` to listen with and `tools/delta-check.sh` to hold the two forms
of a rule against each other.

The reference binary hangs now and again on an index mark. `test/compare.sh`
retries a case once on its own before reporting it as hung, because calling that
a difference cost false alarms.

The sixty-four bit build maps a region low in memory and is built `-no-pie`.
Both are required by the Delta machine keeping addresses in thirty-two bit
values, not preferences, and a port to a machine that can do neither would need
a different answer.

If the audio sounds wrong to you, it is not a fault in the port: our output is
identical to IBM's. Changing it is a deliberate change to the language data, and
the suite will correctly report that as a difference.
