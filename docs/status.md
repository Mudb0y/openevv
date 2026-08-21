# What works and what does not

Last measured 21 August 2026.

## Works

The engine speaks, and it speaks IBM's samples. All 81 cases in six categories
come out byte for byte identical to IBM's own binary: plain text, UTF-8,
annotations, annotations with the annotation input type on, real-world text with
the parameters read back in a person's units, and the user dictionary.

That holds in all four configurations the tree can build for this machine --
thirty-two and sixty-four bit, each with the rules run as bytecode and with the
same rules run as the C they decompile to -- and in the Windows build as well,
which is a fifth: `build/probe.exe` under Wine matches IBM's binary over the
same 81 cases.

Nothing is borrowed at build time. `make missing` answers nothing, which is the
check that no call has quietly gone back to IBM's objects. The language data is
all transcribed and in the tree, so an ordinary build needs a C compiler and
nothing else.

Dictionaries can be edited. `tools/delta-dict.py` writes `lang/enus/enus.dict`
out of the tables and reads it back in, so a pronunciation can be changed, laid
down and heard.

All nine languages in the SDK lift and decompile: US and British English, both
Spanishes, both Frenches, German, Italian and Japanese. Only English is built.

It builds and speaks on Windows, sixty-four bit, as one static file. The speak
window plays what it makes through waveOut; `win/speak.c` is that, and it is the
only front end that plays anything.

And it builds as `eci.dll`, exporting the fifty-two names IBM published, so a
program written against IBM's library -- a screen reader add-on, most likely --
can load ours instead. Both bitnesses: sixty-four bit for an add-on that
loads the engine into the reader's own process, thirty-two bit for the most
used driver, which hosts the engine in a 32-bit process of its own whatever
the reader is. Checked on Windows itself: by name from C for both, and through
ctypes for the sixty-four bit one, as an add-on does.

What is not exported is the filter interface, which the engine does not
implement, and the dictionary find, lookup and update calls, which have no
public wrapper yet.

## Not done

Live audio on Linux. The engine hands its samples to the caller, and
`build/evv` writes them as a wave file or down a pipe; nothing sends them to a
sound card as they are made. Windows got there first because waveOut is forty
lines; PipeWire is next and is a thin sink on top of the same buffer.

The compiler. The rules are readable C, but there is no way to write a new rule
except by writing that C. This is the next piece of work and the gate to adding
a language.

Polish, which is the reason the compiler matters. Nothing started.

Anything but English at build time. The other eight lift and decompile, but the
build knows only `lang/enus`.

## Partly done

The rules read as rules, to sixteen passes of the decompiler. What that means
concretely: calls sit with their arguments, wrapper rules say which primitive
they stand for, state reaches say which language variable they touch, frame
reaches say which argument they are, the arms of a backtracking dispatch say
which alternative they are whichever of the two ways the compiler wrote the
dispatch, register halves say which half, a test of what a call answered is a
comparison of the answer with nought rather than a flag set and a flag read,
letting go of a call's arguments says that and not that a scratch register was
written, a jump at the return is a return, 2,391 loops are loops, a jump out of
one says it is leaving it, the two places a rule ends say whether it has matched
or given up, and the machine's dead leavings are gone.

What is left in them: 59,295 gotos, of which 26,668 are the arms of a
backtracking dispatch and are right as they are, and 4,254 more say plainly that
the rule has matched or has given up. Most of the rest are the same thing
without a name, because that is what the language is: a pattern matcher whose
every failure jumps to a shared tail. 3,364 conditions still read the flags and
2,057 comparisons still set them, which is what is left once the answer of a
call is compared directly: those are the ones an arithmetic operation set rather
than a test.

`tools/delta-shape.py` says what more structuring could reach, and the answer is
bounded. Of the 106,072 edges between the 53,439 basic blocks, 6,004 jump back
to a block that does not stand on every path to them, which is the definition of
flow that no arrangement of loops and conditionals can say. 610 of the 1,042
rules have at least one, 191 have ten or more, and the worst is `hebrew_ph_Q`
with 129 in 186 blocks. Saying those in a structured language means copying the
code the jump lands on, or adding a variable to dispatch on; the first costs the
correspondence between the C and the bytecode, which is what makes the C
checkable, and the second buries the dispatch the rules already have under an
invented one.

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

That last one compares every rule entered and every call made, with their
arguments, over the seven plain cases taken one at a time. Three kinds of line
are left out and all three are the interpreter's alone: the stores it makes,
which a rule written as C makes for itself; its remark about the argument area
being a different depth than the compiled code expected; and addresses in the
arena, which differ because a rule written as C takes a smaller frame on
purpose.

One thing that check turned up is a fault of ours, and is not fixed. Tracing at
the level that prints every call costs twenty times what the synthesis does, and
feeding the synthesis that slowly faults part way through a run of several
sentences, with less audio written than there should be. Any one sentence is
fine. Nothing but a trace makes the engine that slow, so it is not in the way of
anything, but it is a fault and this is where it is written down.

The reference binary hangs now and again on an index mark. `test/compare.sh`
retries a case once on its own before reporting it as hung, because calling that
a difference cost false alarms.

The sixty-four bit build maps a region low in memory, because the Delta machine
keeps addresses in thirty-two bit values and everything it can point at has to
be nameable in one. The program itself may be loaded anywhere: the language's
data is copied into that region at startup rather than named where it lies. A
machine that cannot map anything below two gigabytes would need a different
answer, and would say so rather than misbehave.

If the audio sounds wrong to you, it is not a fault in the port: our output is
identical to IBM's. Changing it is a deliberate change to the language data, and
the suite will correctly report that as a difference.
