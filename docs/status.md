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
dispatch, register halves say which half, a test of the flags is the comparison
the machine made -- a call's answer against nought, a length against a limit, a
bit against a mask -- rather than a flag set and a flag read, letting go of a
call's arguments says that and not that a scratch register was written, a jump
at the return is a return, 2,391 loops are loops, a jump out of one says it is
leaving it, the two places a rule ends say whether it has matched or given up,
and the machine's dead leavings are gone.

What is left in them: 59,295 gotos, of which 26,668 are the arms of a
backtracking dispatch and are right as they are, and 4,254 more say plainly that
the rule has matched or has given up. Most of the rest are the same thing
without a name, because that is what the language is: a pattern matcher whose
every failure jumps to a shared tail.

Of the flags, 49 comparisons and 1,054 conditions are left out of the 24,140 and
27,013 there were. The comparisons are the ones with a label between them and
the condition that reads them, where something else can arrive with other flags.
The conditions are mostly reading what an arithmetic operation left rather than
what a comparison did -- the decrement in a dispatch, whose zero flag says the
answer was one -- and those are the next ones that could go.

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

Interrupting from another thread is not fully closed. `eciStop` called from a
second thread while `eciSynthesize` runs used to fault every time; with the
guard it faults in none of twelve runs on real Windows and in eight of twelve
under Wine, with the address moving between runs. That is a race rather than a
certainty, so it is the one place where a caller doing what IBM's interface
offers is still not safe. Answering `eciDataAbort` from the callback -- the same
call reached from inside -- is clean on Linux, under Wine and on Windows, twelve
turns each.

Interrupting an utterance and then speaking again on the same instance used to
leave the engine quiet from the second interruption onwards, accepting the text,
reporting no error, answering that it was not speaking, and saying nothing. That
is fixed. `make interrupt` is the check and it now asserts what it used to only
print: twelve turns, every follow-up utterance worth exactly what the first
whole one was.

It was never on the interrupt path. The application queue keeps two counts, one
of everything it has been told about and one of everything the application has
collected, and it takes them being equal to mean there is nothing outstanding.
The stop put the first back to nought and left the second where it was, so the
two drifted; when the stale one happened to equal the fresh one, the queue
believed it had caught up and handed over none of the samples already sitting in
it. The text had arrived and the samples had been made. They were made and then
not collected.

Only the sixty-four bit builds had it, which is why the suite never saw it: the
stop reached that second count by the byte it sat at when a pointer was four
bytes, and at sixty-four bits that byte is inside the queue's own send lock. The
same build thirty-two bit runs the check clean without the fix. That is the
third time an unconverted byte offset has cost a fault, so the offsets that
reach a block by number rather than by name were swept afterwards; what the
sweep found is below.

The sweep, and the two things left in it. Eighteen blocks in the engine are
still reached by the byte a field sat at. Most are safe and stay that way for a
reason: a block the machine can see has the same layout in both bitnesses by
design, because everything in it that points is a four-byte `evv_ref` rather
than a pointer, so a number into one of those means the same thing either way.
What is not safe is reading such a field as a pointer, or reaching into a block
whose layout is ours and has grown. Two of the eighteen do one of those.

`src/eci_deltalib.c` writes the machine's "undefined reads back as this" field
as a `const char **`, and what it writes is the address of `"---"` in the
program. Both halves are wrong at sixty-four bits: the field is a four-byte
reference into the arena, so an eight-byte write spills past it, and an address
in the program is the one thing the machine may never be given. Going through
the raw offset is exactly what slips it past `evv_ref_checked`, which exists to
refuse this and never gets the chance. The reader, in `src/eci_access.c`, takes
it as a reference and translates it, so at sixty-four bits it translates
rubbish; at thirty-two the two agree by accident, because a reference and a
pointer are the same four bytes there. Nothing asks for it in the 81 cases, so
this is latent rather than live, and it wants the string copied into the arena
by `delta_low_copy` and kept as a reference.

`src/eci_pcm16.c` and `src/eci_soundfmt.c` reach a sound file's block by
offsets that overlap once a pointer is eight bytes: the format at 0x0c runs
over the rate at 0x10, the stream at 0x18 over how it was opened at 0x1c, and
the index function at 0x20 over its parameter at 0x24. That block is ours, not
the machine's, so it should be a struct. It is dormant for a plain reason:
nothing in these builds ever opens a sound file. `port_ral.c` says there are no
audio devices, which sends the engine down the buffer path, and the callers
write their own files out of that buffer. It becomes live the day anything
asks the engine to write a file itself.

Making and throwing away engine instances leaks a few megabytes each. After
about sixty the engine still runs and still answers, and says nothing: it has
quietly run out of the arena. `make instances` is what shows it -- every round
owes the same samples and after sixty-two they stop coming. What an instance
owns and does not give back has not been chased down.

One thing the trace check turned up is a fault of ours, and is not fixed.
Tracing at the level that prints every call costs twenty times what the
synthesis does, and
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
