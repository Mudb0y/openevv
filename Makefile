# The engine, built for the machine it is running on.
#
# `make' builds build/evv, which speaks a sentence into a wave file, and the
# library it is linked against. Nothing else is needed: no SDK, no Wine, and
# no Python unless the rules are wanted as C.
#
# `make probe' builds the driver the tests drive instead. It is the same
# engine with a different front: it prints what the engine answered at every
# step so that test/suite.sh can set those answers against IBM's own binary
# line for line, which is why it is not the thing a person would run.
#
# `make evv32' and `make probe32' build the same two thirty-two bit. That
# build is kept because a difference between the word sizes is a layout
# mistake caught early, and it costs about a minute. It needs a thirty-two
# bit compiler, which is CC32 below.
#
# `make missing' says what our code asks for that nothing of ours answers.
# It answers nothing now, and it is worth re-running whenever a source is
# added, since a name that reappears there is a call that has quietly gone
# back to the original.

# Said rather than left to the order of the file, because a rule that has to
# sit above `all' -- the language list is one -- would otherwise be what a bare
# `make' builds.
.DEFAULT_GOAL := all

SRC   := src
# Which languages get built in. As many as are named: every module names its
# own tables after itself, and the engine reaches whichever is in force
# through the table src/delta_lang.h describes, so several can be linked
# into one program and chosen between at run time.
#
# `make LANGS="lang/enus lang/dede"' builds both, and the first one named is
# the one a caller gets when it asks for no language in particular. LANG is
# kept as the name for one of them, because that is what everything already
# says.
# Not LANG: that is what a shell calls the locale, and `?=' lets the
# environment win, so an ordinary LANG=en_GB.UTF-8 sends the build looking for
# a language module of that name and it fails outright. EVVLANG is ours.
EVVLANG ?= lang/enus
LANG  := $(EVVLANG)
LANGS ?= $(LANG)
TAG   := $(notdir $(firstword $(LANGS)))
TAGS  := $(notdir $(LANGS))
BUILD := build

# make has no space of its own to substitute with.
empty :=
space := $(empty) $(empty)

# What a test binary and a library are called. A build of English alone
# keeps the plain names, because the workflow, the documents and everyone
# who has been told to run something ask for `build/probe'; anything else
# carries the languages it has in it, so builds sit beside each other rather
# than over each other.
#
# The libraries are named this way for a harder reason than tidiness. Each is
# built out of one set of objects, and those already live in directories of
# their own, so building German and then English again leaves an archive
# newer than every English object -- make would not rebuild it, and the
# English probe would be linked against the German engine.
SUF   := $(if $(filter-out enus,$(TAGS)),-$(subst $(space),-,$(TAGS)))

CC  ?= cc
NM  ?= nm

# Which rules the interpreter finds already written as C.
#
# `c' links the thirteen megabytes tools/delta-decompile.py writes out of the
# bytecode, and the interpreter prefers that C for every rule it has one for.
# `bytecode' links an empty table instead, so every rule is interpreted.
#
# Both speak the same samples -- that is what test/suite.sh holds them to, over
# all 81 cases in both forms -- so this is a trade of build for speed and
# nothing else. C is the default because the speed is what a person waiting for
# speech feels: the same utterance synthesises in rather less than half the
# time, and interrupting one and asking for another costs a third of what it
# costs interpreted, since the engine cannot abandon an utterance and has to
# finish the one it was told to stop.
#
# What it costs. Writing the file needs Python and about seven minutes, and
# compiling it another seven, where the bytecode build wants only a C compiler
# and half a minute. The binaries are some four times the size, because
# thirteen megabytes of C is what a machine's worth of lifted code looks like
# written out. `RULES=bytecode' is the small, quick build and is the one to use
# while working on anything but the rules. See docs/building.md.
RULES ?= c

# Each language has both forms of its rules beside it: the empty table that
# leaves every rule as bytecode, and the C the decompiler writes. One of the
# two is linked per language, never both.
GENERATED := $(foreach l,$(LANGS),$(l)/delta_rules_c_$(notdir $(l)).c)
STUBS     := $(foreach l,$(LANGS),$(l)/delta_rules_none_$(notdir $(l)).c)

# What the last build was: which form the rules are in, and which languages
# are in it. Neither can be asked of an object or an archive afterwards. The
# objects sit in a tree per answer, so a changed answer gives them somewhere
# new to go, but the archives and the things a person is handed keep their
# names -- `build/evv', `build/eci.dll', because that is what a caller looks
# for -- and would otherwise be left standing, newer than everything they were
# built from. This is written only when the answer is different, so it forces
# those then and never otherwise.
RULESTAMP := $(BUILD)/build.stamp
$(shell mkdir -p $(BUILD); \
        [ "$$(cat $(RULESTAMP) 2>/dev/null)" = "$(RULES) $(TAGS)" ] \
        || printf %s "$(RULES) $(TAGS)" > $(RULESTAMP))

ifeq ($(RULES),bytecode)
RULESRC := $(STUBS)
TRIM    :=
else ifeq ($(RULES),c)
RULESRC := $(GENERATED)
# Where every rule is written as C, nothing reads the bytecode, and it is a
# megabyte and a half -- the largest single thing in the library after the
# rules themselves. `EVV_NO_BYTECODE' compiles the interpreter out, which is
# what leaves the array unreferenced; the section flags are what let the
# linker actually drop it. A rule that turns out not to have been written as C
# then says so by name and stops, rather than reading an array that is gone.
TRIM    := -DEVV_NO_BYTECODE -ffunction-sections -fdata-sections \
           -Wl,--gc-sections
else
$(error RULES is bytecode or c, not $(RULES))
endif

# Which languages are in, as C the engine can walk. Written here because
# this is what knows.
LANGLIST := $(BUILD)/delta_langs_$(subst $(space),_,$(TAGS)).c

# A language written in another script has a romanizer, which is code of ours
# rather than data of IBM's and so does not live in lang/. It is built exactly
# when its language is: rom/<tag> beside lang/<tag>. Nothing but Japanese has
# one, and an English build carries none of it.
ROMS := $(sort $(foreach l,$(LANGS),$(wildcard rom/$(notdir $(l))/*.c)))

# And which of them is linked, said to the one file that has to know how a
# romanizer is found. IBM answers that question with the presence of a
# link-time symbol; a build of ours can hold several languages, so it is
# answered by name.
ROMDEFS := $(if $(filter jajp,$(TAGS)),-DEVV_ROM_JAJP)

# The engine, plus every language beside it. port_win32.c is the Windows
# porting layer and belongs to the reference build; the two rule tables are
# chosen between above rather than both linked.
SOURCES := $(filter-out $(SRC)/port_win32.c,$(wildcard $(SRC)/*.c)) \
           $(filter-out $(GENERATED) $(STUBS), \
             $(sort $(foreach l,$(LANGS),$(wildcard $(l)/*.c)))) \
           $(ROMS) $(RULESRC) $(LANGLIST)

# Every header, because a struct that changed shape and an object that was
# not rebuilt is a link that succeeds and an engine that writes over itself.
HEADERS := $(wildcard $(SRC)/*.h) $(foreach l,$(LANGS),$(wildcard $(l)/*.h)) \
           $(foreach l,$(LANGS),$(wildcard rom/$(notdir $(l))/*.h))

# Everything a language module holds is named for that module, and the
# wildcards above take whatever is there. So a file left behind by an earlier
# lift, or copied in from another language, is compiled into the build without
# a word -- which is how lang/dede carried an unprefixed rule shim into every
# German binary for a day. Its names did not collide with anything, so the
# linker had nothing to say; the name of the file is the only thing that told.
STRAYS := $(strip $(foreach l,$(LANGS), \
            $(filter-out %_$(notdir $(l)).c %_$(notdir $(l)).h, \
              $(wildcard $(l)/*.c) $(wildcard $(l)/*.h))))
ifneq ($(STRAYS),)
$(error these are in a language module but are not named for it, so they are \
        either left over or in the wrong place: $(STRAYS))
endif

vpath %.c $(SRC) $(LANGS) $(foreach l,$(LANGS),rom/$(notdir $(l))) $(BUILD)

# One line per language, and one call per language to fill in the numbers
# each module states in a file of its own.
$(LANGLIST): Makefile
	@mkdir -p $(BUILD)
	@echo '/* Written by the Makefile: which languages this program has'  > $@
	@echo '   in it, and the first of them, which is the one a caller'   >> $@
	@echo '   gets when it asks for no language in particular. */'       >> $@
	@echo '' >> $@
	@echo '#include "delta_lang.h"' >> $@
	@echo '' >> $@
	@for t in $(TAGS); do \
	   echo "extern delta_language delta_lang_$$t;" >> $@; \
	   echo "void delta_lang_bind_$$t(void);" >> $@; \
	 done
	@echo '' >> $@
	@echo 'const delta_language *const delta_languages[] = {' >> $@
	@for t in $(TAGS); do echo "    &delta_lang_$$t," >> $@; done
	@echo '    0,' >> $@
	@echo '};' >> $@
	@echo '' >> $@
	@echo 'void delta_lang_bind_all(void)' >> $@
	@echo '{' >> $@
	@for t in $(TAGS); do echo "    delta_lang_bind_$$t();" >> $@; done
	@echo '}' >> $@

# A narrowed field assigned from a pointer, or the other way about, was the
# whole of what went wrong in the sixty-four bit port, so it is an error here
# rather than a warning nobody reads. The rest of the warnings stay off: this
# is transcribed code and it is loud.
WARN := -w -Wno-implicit-function-declaration \
        -Werror=int-conversion -Werror=incompatible-pointer-types

# The machine this code was written for keeps addresses in thirty-two bit
# values, so on a wider host everything it can point at has to live somewhere
# such a value can still name: src/evv_arena.c maps that region low in memory
# and everything the machine holds comes out of it, including the language's
# own data, which src/delta_low.c copies out of the program at startup. That
# copy is what lets the program itself be loaded anywhere -- there is no
# -no-pie here any more, and there does not need to be. None of it is wanted
# when the host is thirty-two bit already.
POINTER := $(shell echo __SIZEOF_POINTER__ | $(CC) -E -P - 2>/dev/null | tail -1)
ifeq ($(POINTER),4)
LOW :=
else
LOW := -DEVV_ARENA=1
endif

OPT        ?= -O2
INCS       := -I$(SRC) $(addprefix -I,$(LANGS)) \
              $(foreach l,$(LANGS),-Irom/$(notdir $(l)))
ALL_CFLAGS := $(OPT) -std=gnu99 $(INCS) $(WARN) $(LOW) $(TRIM) $(ROMDEFS) \
              $(CFLAGS)

# One directory per build, where a build is which form the rules are in and
# which languages are in it. Neither can be asked of an object afterwards,
# and either changing gives the same file a different meaning: a stale one
# from the other build is newer than the source that should replace it, so
# it would be linked without being rebuilt.
OBJDIR  := $(BUILD)/obj-$(RULES)/$(subst $(space),-,$(TAGS))
OBJECTS := $(patsubst %.c,$(OBJDIR)/%.o,$(notdir $(SOURCES)))

.PHONY: all probe rules missing install clean evv32 probe32 instances interrupt landing rate voices inikeys stopthread pieces prims romcan romprims
all: $(BUILD)/evv

$(BUILD)/evv: cli/evv.c $(BUILD)/libevv$(SUF).a $(RULESTAMP)
	@$(CC) $(ALL_CFLAGS) cli/evv.c $(BUILD)/libevv$(SUF).a -lpthread -lm -o $@
	@echo "built $@"

probe: $(BUILD)/probe$(SUF)

$(BUILD)/probe$(SUF): cli/probe.c $(BUILD)/libevv$(SUF).a
	@$(CC) $(ALL_CFLAGS) cli/probe.c $(BUILD)/libevv$(SUF).a -lpthread -lm -o $@
	@echo "built $@"

# An instance made and thrown away over and over, which the suite never does:
# it speaks a great deal through one. A fault in what an instance owns and
# gives back shows here and nowhere else.
instances: $(BUILD)/instances

$(BUILD)/instances: test/instances.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) test/instances.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# An utterance interrupted and another asked for, on one instance, over and
# over. The suite never interrupts anything.
interrupt: $(BUILD)/interrupt

$(BUILD)/interrupt: test/interrupt.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) test/interrupt.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# The sample rate changed on an instance that speaks into a buffer, which the
# suite cannot see: the probe registers a buffer but never asks for another
# rate, and IBM's engine goes silent there too, so both sides agreed.
rate: $(BUILD)/rate
	@$(BUILD)/rate

$(BUILD)/rate: test/rate.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) test/rate.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# A romanizer with no language in it, replaying what IBM's romanizer answered.
# This is what proves that everything below the romanizer is already right for
# a language written in another script, before a line of that romanizer exists:
# our engine is handed IBM's own answers at the same seam and has to produce
# the same samples. See the head of test/romcan.c. Built against whichever
# languages LANGS names, since the point of it is Japanese:
#
#   make romcan LANGS=lang/jajp
romcan: $(BUILD)/romcan$(SUF)

$(BUILD)/romcan$(SUF): test/romcan.c $(BUILD)/libevv$(SUF).a
	@$(CC) $(ALL_CFLAGS) test/romcan.c $(BUILD)/libevv$(SUF).a -lpthread -lm -o $@
	@echo "built $@"

# The romanizer's converters, ours against IBM's, one call at a time. The same
# file is built against IBM's objects by `make -C reference TAG=jajp romprims',
# and test/romprims.sh diffs what the two print. This is the only thing that
# reaches a romanizer class the text path never asks for.
romprims: $(BUILD)/romprims$(SUF)

$(BUILD)/romprims$(SUF): test/romprims.c $(BUILD)/libevv$(SUF).a
	@$(CC) $(ALL_CFLAGS) -DEVV_ROMPRIMS_OURS test/romprims.c \
	  $(BUILD)/libevv$(SUF).a -lpthread -lm -o $@
	@echo "built $@"

# One text spoken whole and then in pieces, which is what the add-on does with
# a long message so that a cancel waits out a piece. The suite speaks whole
# utterances and cannot see where a boundary may go; a boundary at a sentence
# end costs nothing and one anywhere else costs the pause a full stop gets.
pieces: $(BUILD)/pieces
	@$(BUILD)/pieces

$(BUILD)/pieces: test/pieces.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) test/pieces.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# The eight voices the caller may edit, which the suite is blind to: nothing it
# runs asks an instance about voice nine, so the loop that copies the language's
# own eight into them can be turned off and all 81 cases still match. That
# happened by accident once. This is what catches it.
voices: $(BUILD)/voices
	@$(BUILD)/voices

$(BUILD)/voices: test/voices.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) test/voices.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# A backtrack landed on from a thread that never planted it, which is how
# stopping the engine from another thread used to fault with nothing in the
# fault to say so. It is meant to be killed by the guard, so it answers
# non-zero when the guard does not fire.
landing: $(BUILD)/landing

$(BUILD)/landing: test/landing.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) test/landing.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# A stop from a thread that is not the one speaking, which is what a screen
# reader does and what test/interrupt.c does not: it aborts from the callback,
# on the engine's own thread. This one crosses a thread and requires the
# interrupted utterance to come out short, so a stop that did nothing fails.
stopthread: $(BUILD)/stopthread
	@$(BUILD)/stopthread

$(BUILD)/stopthread: test/stopthread.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) test/stopthread.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# A key that is not in a section, which neither suite can see: the reader
# decided a key was absent by reading the byte where its search stopped, so
# with another section behind it the key came back holding the next section's
# first value, and a two-language build died on it.
inikeys: $(BUILD)/inikeys
	@$(BUILD)/inikeys

$(BUILD)/inikeys: test/inikeys.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) test/inikeys.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# A machine primitive no rule calls, held against IBM's own. The suite cannot
# see one of these: a call nothing makes cannot be reached by speaking a
# sentence, which is why the primitives the shipped languages never use were
# missing in the first place. `test/prims.sh' builds this and the same file
# against IBM's objects and diffs the two.
prims: $(BUILD)/prims
	@$(BUILD)/prims > /dev/null && echo "built and ran $(BUILD)/prims"

$(BUILD)/prims: test/prims.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) -DEVV_PRIMS_OURS test/prims.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

$(OBJDIR)/%.o: %.c $(HEADERS)
	@mkdir -p $(OBJDIR)
	@$(CC) $(ALL_CFLAGS) -c $< -o $@

# A source that gets renamed leaves its object behind, and a stale one is a
# duplicate definition waiting to happen, so they go before the archive is
# built rather than at the next link. Switching RULES leaves one the same way.
$(BUILD)/libevv$(SUF).a: $(OBJECTS) $(RULESTAMP)
	@for o in $(OBJDIR)/*.o; do \
	   case " $(OBJECTS) " in *" $$o "*) ;; *) rm -f "$$o" ;; esac; \
	 done
	@rm -f $@
	@ar rcs $@ $(OBJECTS)
	@echo "built $@ from $(words $(OBJECTS)) objects"

# The rules as text that can be read and edited, in lang/enus/rules. Written
# out of IBM's objects once; `notation-check' holds what is in the tree against
# those objects again, emitting the bytecode from each and comparing, so it says
# both that the text is still faithful and, once a rule has been changed on
# purpose, which rules those are. It wants the objects, so it is in the same
# class as the suite: obtainable, and not needed to build.
.PHONY: notation notation-check notation-prove notation-regenerate \
        notation-symbols notation-rewrite upper upper-prove upper-check \
        authored constants codepoints
notation:
	@python3 tools/delta-notation.py tree

notation-check:
	@python3 tools/delta-notation.py verify

# The stronger of the two, and the one to believe: every rule emitted out of
# the text into one stream, held against the bytecode the engine actually runs.
# The pools are shared and numbered in the order the rules are taken, so
# matching the whole stream says the text carries every rule, in order, with
# nothing added and nothing lost -- which a rule-by-rule comparison cannot.
notation-prove:
	@python3 tools/delta-notation.py prove

# The rules rebuilt out of the text alone, opening no object, and the result
# held against the two generated files in the tree. This is the one that says
# the text is the source rather than a second copy: what it writes has to be
# what is already there, byte for byte.
notation-regenerate:
	@python3 tools/delta-notation.py regenerate

# Where each address the rules name falls. Written out of the objects once,
# because it is the last thing the emitter wanted them for.
notation-symbols:
	@python3 tools/delta-notation.py symbols

# The two generated files written for real rather than compared, out of the
# lifted text alone. Wanted when something other than a rule changes what they
# hold -- a constant of ours adds a store, and the store is named in there.
notation-rewrite:
	@python3 tools/delta-notation.py rewrite

# What a rule stands for, and what a rule does.
#
# `upper' and `upper-prove' are the wrappers as the primitive each stands for:
# written out of the lower form and compiled back, where the bytecode has to
# match byte for byte. `authored' is the other upper form, the real rules in
# lang/<tag>/rules/*.up, compiled into the two generated files a build
# compiles -- which is how one gets into a build at all, since an ordinary
# build reads what is in the tree.
#
# `upper-check' is the one that says whether an authored rule is the rule it
# stands in for. There is no byte comparison to be had: our compiler would
# have to make the same choices IBM's did. So it speaks the seven plain cases
# through a build carrying the authored rules and through one carrying IBM's,
# and holds every rule entered and every call made with its arguments against
# each other, and the audio besides. It wants no objects and no Wine.
upper:
	@python3 tools/delta-notation.py upper

upper-prove:
	@python3 tools/delta-notation.py upper-prove

upper-check:
	@bash tools/upper-check.sh

authored:
	@python3 tools/delta-notation.py authored

# And that held against the tree, which is the check for a module written with
# `authored' rather than lifted. EVV_NOTATION_LANG says which one.
authored-check:
	@python3 tools/delta-notation.py authored-check

# Bytes a rule of ours names by address, out of lang/<tag>/rules/constants
# into the one file in a language module that no lifter writes. Run
# `notation-rewrite' after it: a new store is named in the generated file too.
constants:
	@python3 tools/delta-consts.py $(TAGS)

# What each of a language's own characters arrives as: the code point a caller
# writes and the byte its alphabet knows it by. Authored like the constants
# rather than lifted, since the nine IBM shipped need none -- every letter they
# have is in the byte set the engine was built around, and a language of ours
# can have letters that are not.
codepoints:
	@python3 tools/lang-codepoints.py $(TAGS)

# The tables beside the rules, as text: the variables the language declares,
# the settings it carries, the statement table, the lookup sets and the bytes
# the rules name by address. Each has a text form in lang/<tag> and one writer
# for the C, so what a lifter writes and what the text writes cannot drift.
#
# `tables-dump' writes the four texts. Three of them read IBM's objects; the
# sets read the C in the tree instead, on purpose, because the dictionary's
# arrays in that file are laid down by tools/delta-dict.py out of the words and
# IBM's objects hold what the dictionary said before anything was added.
#
# `tables-check' writes the C from each text into a directory of its own and
# holds it against the tree, byte for byte. That is the one to believe, and it
# wants no objects. `tables-write' does it for real, which is how a language
# that was authored rather than lifted gets built.
TEMPLATE ?= itit

.PHONY: tables-dump tables-check tables-write
tables-dump:
	@for t in $(TAGS); do \
	    python3 tools/gen-globals.py dump $$t && \
	    python3 tools/lift-ini.py dump $$t && \
	    python3 tools/delta-link.py dump $$t && \
	    python3 tools/delta-sets.py dump $$t && \
	    python3 tools/delta-consts.py dump $$t || exit 1; \
	done

tables-check:
	@for t in $(TAGS); do \
	    python3 tools/gen-globals.py regenerate $$t && \
	    python3 tools/lift-ini.py regenerate $$t && \
	    python3 tools/delta-link.py regenerate $$t && \
	    python3 tools/delta-sets.py regenerate $$t && \
	    python3 tools/delta-consts.py regenerate $$t || exit 1; \
	done

# How much of a module is still the module it was copied from. A language IBM
# never shipped starts as one it did and becomes itself a rule at a time, and
# what this answers is how far that has got: TEMPLATE says which to hold it
# against. It reads the text forms only.
.PHONY: census
census:
	@python3 tools/lang-census.py $(firstword $(TAGS)) $(TEMPLATE)

tables-write:
	@for t in $(TAGS); do \
	    python3 tools/gen-globals.py write $$t && \
	    python3 tools/lift-ini.py write $$t && \
	    python3 tools/delta-link.py write $$t && \
	    python3 tools/delta-sets.py write $$t && \
	    python3 tools/delta-consts.py write $$t || exit 1; \
	done

# The rules as C. Thirteen megabytes written out of the bytecode beside it,
# so it is made here rather than kept in the tree, where every change to the
# decompiler would rewrite the whole of it.
rules: $(GENERATED)

# One rule per language rather than a pattern, because the name has the
# language in it twice -- once in the directory and once in the file -- and a
# pattern rule may only have the one stem. EVV_LANG_DIR is how the decompiler
# is told which language to read; without it, `make LANG=lang/dede rules'
# would write English out into lang/dede.
define rules_for
$(1)/delta_rules_c_$(notdir $(1)).c: tools/delta-decompile.py \
                                     tools/delta-census.py
	@EVV_LANG_DIR=$(1) python3 tools/delta-decompile.py all
endef
$(foreach l,$(LANGS),$(eval $(call rules_for,$(l))))

# What our code asks for and nothing of ours answers. The C library's own
# names are dropped, since those come from the system; what is left is the
# original's, and writing it is the rest of the port.
missing: $(OBJECTS)
	@$(NM) $(OBJECTS) > $(BUILD)/syms.txt
	@python3 tools/missing.py $(BUILD)/syms.txt

clean:
	@rm -rf $(BUILD)/obj-* $(BUILD)/obj32-* $(BUILD)/objwin-* \
	        $(BUILD)/objwin32-* \
	        $(BUILD)/eci32.dll $(BUILD)/dlltest32.exe \
	        $(BUILD)/libevv-win32$(SUF).a $(BUILD)/evv $(BUILD)/probe$(SUF) \
	        $(BUILD)/evv32 $(BUILD)/probe32$(SUF) \
	        $(BUILD)/libevv$(SUF).a $(BUILD)/libevv32$(SUF).a \
	        $(BUILD)/libevv-win$(SUF).a \
	        $(BUILD)/evv.exe $(BUILD)/evvspeak.exe $(BUILD)/eci.dll \
	        $(BUILD)/eci.ini $(BUILD)/dlltest.exe $(BUILD)/syms.txt \
	        $(BUILD)/openevv-*.nvda-addon

# Where `make install' puts it. There is nothing else to install: one binary,
# which reads no file of its own at run time and wants no library but the C
# one, libm and pthreads.
PREFIX  ?= /usr/local

install: $(BUILD)/evv
	@mkdir -p $(DESTDIR)$(PREFIX)/bin
	@cp $(BUILD)/evv $(DESTDIR)$(PREFIX)/bin/evv
	@echo "installed $(DESTDIR)$(PREFIX)/bin/evv"

# The same engine thirty-two bit. On this machine the compiler is the one the
# flake provides; elsewhere it is usually the host compiler with -m32, which
# CC32 can be set to whole: `make evv32 CC32="gcc -m32"'.
CC32      ?= i686-unknown-linux-gnu-gcc
OBJDIR32  := $(BUILD)/obj32-$(RULES)/$(subst $(space),-,$(TAGS))
CFLAGS32  := $(OPT) -std=gnu99 $(INCS) $(WARN) $(TRIM) $(ROMDEFS) \
             $(CFLAGS)
OBJECTS32 := $(patsubst %.c,$(OBJDIR32)/%.o,$(notdir $(SOURCES)))

evv32: $(BUILD)/evv32
probe32: $(BUILD)/probe32$(SUF)

$(BUILD)/evv32: cli/evv.c $(BUILD)/libevv32$(SUF).a $(RULESTAMP)
	@$(CC32) $(CFLAGS32) cli/evv.c $(BUILD)/libevv32$(SUF).a -lpthread -lm -o $@
	@echo "built $@"

$(BUILD)/probe32$(SUF): cli/probe.c $(BUILD)/libevv32$(SUF).a
	@$(CC32) $(CFLAGS32) cli/probe.c $(BUILD)/libevv32$(SUF).a -lpthread -lm -o $@
	@echo "built $@"

$(OBJDIR32)/%.o: %.c $(HEADERS)
	@mkdir -p $(OBJDIR32)
	@$(CC32) $(CFLAGS32) -c $< -o $@

$(BUILD)/libevv32$(SUF).a: $(OBJECTS32) $(RULESTAMP)
	@for o in $(OBJDIR32)/*.o; do \
	   case " $(OBJECTS32) " in *" $$o "*) ;; *) rm -f "$$o" ;; esac; \
	 done
	@rm -f $@
	@ar rcs $@ $(OBJECTS32)
	@echo "built $@ from $(words $(OBJECTS32)) objects"

# The same engine for Windows, cross-compiled, as one file with no DLL beside
# it. `make win' builds both fronts: evv.exe, the same console driver as on
# this machine, and evvspeak.exe, the speak window, which is the one to hand
# somebody who wants to hear it.
#
# The Win32 porting layer stands in for the POSIX one. src/port_win32.c was
# written for the reference build and answers the same twelve calls, so nothing
# else in the engine knows the difference.
CCWIN      ?= x86_64-w64-mingw32-gcc
WINDRES    ?= x86_64-w64-mingw32-windres
ARWIN      ?= x86_64-w64-mingw32-ar
OBJDIRWIN  := $(BUILD)/objwin-$(RULES)/$(subst $(space),-,$(TAGS))

CFLAGSWIN  := $(OPT) -std=gnu99 $(INCS) $(WARN) -DEVV_ARENA=1 \
              $(TRIM) $(ROMDEFS) $(CFLAGS)
# Static, so what ships is one file. MINGW64_LDFLAGS is where the cross gcc's
# thread runtime is; the flake sets it, since nothing puts it on the link path
# outside a real cross stdenv.
LDFLAGSWIN := -static $(MINGW64_LDFLAGS)

SOURCESWIN := $(filter-out $(SRC)/port_posix.c,$(wildcard $(SRC)/*.c)) \
              $(filter-out $(GENERATED) $(STUBS), \
                $(sort $(foreach l,$(LANGS),$(wildcard $(l)/*.c)))) \
              $(ROMS) $(RULESRC) $(LANGLIST)
OBJECTSWIN := $(patsubst %.c,$(OBJDIRWIN)/%.o,$(notdir $(SOURCESWIN)))

.PHONY: win win-probe win-dlltest win-stopthread
win: $(BUILD)/evv.exe $(BUILD)/evvspeak.exe $(BUILD)/eci.dll

# The cross-thread stop, for Windows, because that is where it used to fault:
# eight of twelve turns under Wine against none on real Windows, before the
# busy guard. A native Linux run cannot answer for either.
win-stopthread: $(BUILD)/stopthread$(SUF).exe
	@wine $(BUILD)/stopthread$(SUF).exe

$(BUILD)/stopthread$(SUF).exe: test/stopthread.c $(BUILD)/libevv-win$(SUF).a
	@$(CCWIN) $(CFLAGSWIN) test/stopthread.c $(BUILD)/libevv-win$(SUF).a $(LDFLAGSWIN) -o $@
	@echo "built $@"

# The probe, for Windows, which is how a fault there gets localised: it says
# what the engine answered at every step, so the last line it prints is where
# to look.
win-probe: $(BUILD)/probe$(SUF).exe

$(BUILD)/probe$(SUF).exe: cli/probe.c $(BUILD)/libevv-win$(SUF).a
	@$(CCWIN) $(CFLAGSWIN) cli/probe.c $(BUILD)/libevv-win$(SUF).a $(LDFLAGSWIN) -o $@
	@echo "built $@"

$(BUILD)/evv.exe: cli/evv.c $(BUILD)/libevv-win$(SUF).a $(RULESTAMP)
	@$(CCWIN) $(CFLAGSWIN) cli/evv.c $(BUILD)/libevv-win$(SUF).a $(LDFLAGSWIN) -o $@
	@echo "built $@"

# -mwindows because a speak window with a console behind it looks like a
# mistake. winmm is the sound: waveOut is all this needs and every Windows
# since 1995 has it.
$(BUILD)/evvspeak.exe: win/speak.c $(OBJDIRWIN)/speak.res $(BUILD)/libevv-win$(SUF).a $(RULESTAMP)
	@$(CCWIN) $(CFLAGSWIN) -mwindows win/speak.c $(OBJDIRWIN)/speak.res \
	   $(BUILD)/libevv-win$(SUF).a $(LDFLAGSWIN) -lwinmm -lcomdlg32 -o $@
	@echo "built $@"

# The engine as a library, under the names IBM published, so that a program
# written against IBM's eci.dll -- a screen reader add-on, most likely -- can
# load ours instead. win/eci_api.c is the whole of the difference: fifty-two
# wrappers and an entry point.
#
# eci.ini goes beside it because add-ons look for one and patch a path inside
# it. Nothing here reads it: the engine carries its own settings in the image.
$(BUILD)/eci.dll: win/eci_api.c $(OBJDIRWIN)/eci.res $(BUILD)/libevv-win$(SUF).a $(RULESTAMP)
	@$(CCWIN) $(CFLAGSWIN) -shared win/eci_api.c $(OBJDIRWIN)/eci.res \
	   $(BUILD)/libevv-win$(SUF).a $(LDFLAGSWIN) -o $@
	@cp win/eci.ini $(BUILD)/eci.ini
	@echo "built $@"

$(OBJDIRWIN)/eci.res: win/eci.rc
	@mkdir -p $(OBJDIRWIN)
	@$(WINDRES) -I win win/eci.rc -O coff -o $@

# Speaks through the library rather than against the engine, by name, the way
# an add-on does. `test/hash.sh build/dlltest.exe' then holds what comes out of
# eci.dll against what comes out of everything else.
win-dlltest: $(BUILD)/dlltest.exe

$(BUILD)/dlltest.exe: test/dll.c $(BUILD)/eci.dll $(RULESTAMP)
	@$(CCWIN) $(CFLAGSWIN) test/dll.c -static -lversion -o $@
	@echo "built $@"

$(OBJDIRWIN)/speak.res: win/speak.rc win/speak.h
	@mkdir -p $(OBJDIRWIN)
	@$(WINDRES) -I win win/speak.rc -O coff -o $@

$(OBJDIRWIN)/%.o: %.c $(HEADERS)
	@mkdir -p $(OBJDIRWIN)
	@$(CCWIN) $(CFLAGSWIN) -c $< -o $@

$(BUILD)/libevv-win$(SUF).a: $(OBJECTSWIN) $(RULESTAMP)
	@for o in $(OBJDIRWIN)/*.o; do \
	   case " $(OBJECTSWIN) " in *" $$o "*) ;; *) rm -f "$$o" ;; esac; \
	 done
	@rm -f $@
	@$(ARWIN) rcs $@ $(OBJECTSWIN)
	@echo "built $@ from $(words $(OBJECTSWIN)) objects"

# The same library thirty-two bit, which is what a screen reader driver that
# hosts the engine in its own 32-bit process loads -- and the most used one
# does exactly that, through rundll32 from SysWOW64, whatever bitness the
# reader itself is. Nothing here needs the arena: on thirty-two bits a pointer
# is a value already.
#
# --kill-at because stdcall decorates a name with @N on x86 and a caller asking
# by name wants it plain. On x86-64 there is no decoration to strip.
CCWIN32     ?= i686-w64-mingw32-gcc
WINDRES32   ?= i686-w64-mingw32-windres
ARWIN32     ?= i686-w64-mingw32-ar
OBJDIRWIN32 := $(BUILD)/objwin32-$(RULES)/$(subst $(space),-,$(TAGS))
CFLAGSWIN32 := $(OPT) -std=gnu99 $(INCS) $(WARN) $(TRIM) $(ROMDEFS) $(CFLAGS)
LDFLAGSWIN32 := -static -Wl,--kill-at $(MINGW_LDFLAGS)
OBJECTSWIN32 := $(patsubst %.c,$(OBJDIRWIN32)/%.o,$(notdir $(SOURCESWIN)))

.PHONY: win32
win32: $(BUILD)/eci32.dll $(BUILD)/dlltest32.exe

$(BUILD)/eci32.dll: win/eci_api.c $(OBJDIRWIN32)/eci.res $(BUILD)/libevv-win32$(SUF).a $(RULESTAMP)
	@$(CCWIN32) $(CFLAGSWIN32) -shared win/eci_api.c $(OBJDIRWIN32)/eci.res \
	   $(BUILD)/libevv-win32$(SUF).a $(LDFLAGSWIN32) -o $@
	@echo "built $@"

$(BUILD)/dlltest32.exe: test/dll.c $(BUILD)/eci32.dll $(RULESTAMP)
	@$(CCWIN32) $(CFLAGSWIN32) test/dll.c -static -lversion -o $@
	@echo "built $@"

$(OBJDIRWIN32)/eci.res: win/eci.rc
	@mkdir -p $(OBJDIRWIN32)
	@$(WINDRES32) -I win win/eci.rc -O coff -o $@

$(OBJDIRWIN32)/%.o: %.c $(HEADERS)
	@mkdir -p $(OBJDIRWIN32)
	@$(CCWIN32) $(CFLAGSWIN32) -c $< -o $@

$(BUILD)/libevv-win32$(SUF).a: $(OBJECTSWIN32) $(RULESTAMP)
	@for o in $(OBJDIRWIN32)/*.o; do \
	   case " $(OBJECTSWIN32) " in *" $$o "*) ;; *) rm -f "$$o" ;; esac; \
	 done
	@rm -f $@
	@$(ARWIN32) rcs $@ $(OBJECTSWIN32)
	@echo "built $@ from $(words $(OBJECTSWIN32)) objects"

# The NVDA add-on: the engine as a synthesiser for the screen reader, loaded
# into its own process. Both libraries go in, because a reader is one bitness
# or the other and the one that loads in process has to match, so `make nvda'
# wants both builds. nvda/build.py does the packing and refuses to pack a
# library that does not export something the driver calls.
.PHONY: nvda nvda-test
nvda: win win32
	@python3 nvda/build.py

# The two checks that want neither Windows, nor the libraries, nor sound.
# sequence.py is a speech sequence in and the calls it becomes out, with NVDA
# stood in for. engine.py goes further down: it runs the engine layer itself
# against a library and a player that are stood in for, which is what catches a
# fault in code the first check never enters -- the add-on shipped once with a
# name error in exactly that gap.
nvda-test:
	@python3 nvda/test/sequence.py
	@python3 nvda/test/engine.py
