# The engine, built for the machine it is running on.
#
# `make' builds build/evv, which speaks a sentence into a wave file, and the
# library it is linked against. Nothing else is needed: no SDK and no Wine.
# Python is, because the rules a build compiles are written out of the text in
# lang/<tag>/rules rather than kept beside it.
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
# Every directory the engine's sources sit in, worked out rather than listed:
# the groups under src, and the ECI layer's own groups under those. A group
# added or renamed therefore needs nothing said here. The source wildcards,
# the include path and vpath below are all driven from this, so there is one
# statement of where the engine is rather than four.
SRCDIRS := $(SRC) $(patsubst %/,%,$(wildcard $(SRC)/*/ $(SRC)/*/*/))
# Which languages get built in. As many as are named: every module names its
# own tables after itself, and the engine reaches whichever is in force
# through the table src/delta/delta_lang.h describes, so several can be linked
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
# `c' links the thirteen megabytes tools/rules/decompile.py writes out of the
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
# What it costs. Writing the files is about two minutes of Python and
# compiling them fifteen seconds over twenty-four cores, where the bytecode
# build is half a minute and the two seconds a language RULECODE costs below.
# `RULES=bytecode' is the quick build, and it is also the second opinion:
# tools/rules/check-c.sh holds the two forms against each other call for call,
# which is the only check finer than the audio. See docs/rules.md.
RULES ?= c

# The rules as the engine runs them: the bytecode array, the header that
# numbers every entry, and a shim under each rule's own name. Written out of
# lang/<tag>/rules by tools/rules/notation.py, about two seconds a language,
# and not in the tree. The text is the source and a second copy of the same
# rules beside it could only ever be the stale one -- a rule edited in the
# text and not written out again would be a change that silently did not
# happen. Everything else below is made out of these.
RULECODE := $(foreach l,$(LANGS), \
              $(l)/delta_rules_$(notdir $(l)).c \
              $(l)/delta_rules_$(notdir $(l)).h \
              $(l)/delta_rules_shim_$(notdir $(l)).c)

# Each language has both forms of its rules beside it: the empty table that
# leaves every rule as bytecode, and the C the decompiler writes. One of the
# two is linked per language, never both.
#
# The C comes in PARTS files rather than one. Thirteen megabytes in a single
# translation unit is seven minutes of compiler that cannot be shared out, and
# it is the same work in about a minute over this many. The decompiler must be
# told the same number, since what is named here is what the build expects to
# find; it deals the rules out by size so the files finish together.
PARTS  ?= 32
PARTNS := $(shell seq -w 0 $$(($(PARTS) - 1)))
GENERATED := $(foreach l,$(LANGS), \
               $(foreach n,$(PARTNS),$(l)/delta_rules_c$(n)_$(notdir $(l)).c))
STUBS     := $(foreach l,$(LANGS),$(l)/delta_rules_none_$(notdir $(l)).c)

# And every one of them, whatever PARTS says now, so that lowering the number
# does not leave yesterday's files to be compiled in beside today's. The
# wildcard below takes whatever a language directory holds.
STALE := $(foreach l,$(LANGS),$(wildcard $(l)/delta_rules_c[0-9][0-9]_$(notdir $(l)).c))

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
#
# RULECODE is named rather than left to the wildcard beside it, and taken out
# of that wildcard so as not to be named twice. A wildcard answers with what
# is there when the Makefile is read, and on a fresh clone none of those three
# files is there yet -- so a wildcard would leave them out of this build and
# find them in the next one, which is a link that fails for no visible reason
# the first time and succeeds the second.
SOURCES := $(filter-out %/port_win32.c, \
             $(foreach d,$(SRCDIRS),$(wildcard $(d)/*.c))) \
           $(filter-out $(STALE) $(GENERATED) $(STUBS) $(RULECODE), \
             $(sort $(foreach l,$(LANGS),$(wildcard $(l)/*.c)))) \
           $(filter %.c,$(RULECODE)) \
           $(ROMS) $(RULESRC) $(LANGLIST)

# Every header, because a struct that changed shape and an object that was
# not rebuilt is a link that succeeds and an engine that writes over itself.
# The rule header is named for the same reason its sources are.
HEADERS := include/eci.h \
           $(foreach d,$(SRCDIRS),$(wildcard $(d)/*.h)) \
           $(filter-out $(RULECODE), \
             $(foreach l,$(LANGS),$(wildcard $(l)/*.h))) \
           $(filter %.h,$(RULECODE)) \
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

vpath %.c $(SRCDIRS) $(LANGS) $(foreach l,$(LANGS),rom/$(notdir $(l))) $(BUILD)

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
# such a value can still name: src/port/evv_arena.c maps that region low in memory
# and everything the machine holds comes out of it, including the language's
# own data, which src/delta/delta_low.c copies out of the program at startup. That
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
INCS       := -Iinclude $(addprefix -I,$(SRCDIRS)) $(addprefix -I,$(LANGS)) \
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

.PHONY: all probe so so32 sotest phonemes dict objects rules missing install install-lib clean evv32 probe32 instances interrupt landing rate rates voices inikeys stopthread pieces prims ipa xmltok ssml romcan romprims
all: $(BUILD)/evv

$(BUILD)/evv: cli/evv.c $(BUILD)/libevv$(SUF).a $(RULESTAMP)
	@$(CC) $(ALL_CFLAGS) cli/evv.c $(BUILD)/libevv$(SUF).a -lpthread -lm -o $@
	@echo "built $@"

probe: $(BUILD)/probe$(SUF)

$(BUILD)/probe$(SUF): cli/probe.c $(BUILD)/libevv$(SUF).a
	@$(CC) $(ALL_CFLAGS) cli/probe.c $(BUILD)/libevv$(SUF).a -lpthread -lm -o $@
	@echo "built $@"

# The engine as a shared library, under the names IBM published: the same
# thing build/eci.dll is on Windows, out of the same lib/eci_api.c, so a
# program that dlopens an ECI runtime and asks for the names by string gets
# ours. include/eci.h is what a program compiles against.
#
# Two flags rather than one. -fPIC because a shared object's code has to be,
# and -fvisibility=hidden because without it every one of the engine's eight
# thousand internal names would be exported and interposable -- which is
# slower, larger, and would mean a host program's own `dict' or `keyword'
# could bind over ours. The only names left visible are the ones eci.h marks,
# which is the published interface and nothing else. It is also what lets
# --gc-sections do its work in this build, since with everything exported
# there is nothing the linker may drop.
#
# The objects go in rather than the archive. A shared library linked from an
# archive keeps only the members something already references, and a language
# module is reached through a table the Makefile writes -- so an archive would
# quietly leave the language out and the library would have nothing to say.
SOVERSION := 1
CFLAGSPIC := $(ALL_CFLAGS) -fPIC -fvisibility=hidden
OBJDIRPIC := $(BUILD)/objpic-$(RULES)/$(subst $(space),-,$(TAGS))
OBJECTSPIC := $(patsubst %.c,$(OBJDIRPIC)/%.o,$(notdir $(SOURCES)))
SONAME    := libeci$(SUF).so.$(SOVERSION)

so: $(BUILD)/$(SONAME)

$(OBJDIRPIC)/%.o: %.c $(HEADERS)
	@mkdir -p $(OBJDIRPIC)
	@$(CC) $(CFLAGSPIC) -c $< -o $@

$(BUILD)/$(SONAME): lib/eci_api.c $(OBJECTSPIC) $(RULESTAMP)
	@for o in $(OBJDIRPIC)/*.o; do \
	   case " $(OBJECTSPIC) " in *" $$o "*) ;; *) rm -f "$$o" ;; esac; \
	 done
	@$(CC) $(CFLAGSPIC) -shared -Wl,-soname,$(SONAME) \
	   lib/eci_api.c $(OBJECTSPIC) -lpthread -lm -o $@
	@ln -sf $(SONAME) $(BUILD)/libeci$(SUF).so
	@echo "built $@ and the libeci$(SUF).so beside it"

# And the harness that proves it: it links against nothing, loads the library
# by name, asks for each published name by string and speaks. Being the same
# source as the Windows one is the point -- a name exported from one and not
# the other is what this catches.
.PHONY: sotest
sotest: $(BUILD)/libecitest
	@cd $(BUILD) && EVV_ECI_LIB=./libeci$(SUF).so ./libecitest -o sotest.wav \
	   "Hello. This is the Eloquence synthesizer speaking."

$(BUILD)/libecitest: test/lib/dll.c $(BUILD)/$(SONAME)
	@$(CC) $(OPT) -std=gnu99 test/lib/dll.c -ldl -o $@
	@echo "built $@"

# The same library thirty-two bit, for the same reason the thirty-two bit
# engine is built at all: a difference between the word sizes is a layout
# mistake, and a published interface is where one shows up as a wrong answer
# rather than a crash.
CFLAGSPIC32 = $(CFLAGS32) -fPIC -fvisibility=hidden
OBJDIRPIC32 := $(BUILD)/objpic32-$(RULES)/$(subst $(space),-,$(TAGS))
OBJECTSPIC32 := $(patsubst %.c,$(OBJDIRPIC32)/%.o,$(notdir $(SOURCES)))
SONAME32    := libeci32$(SUF).so.$(SOVERSION)

so32: $(BUILD)/$(SONAME32)

$(OBJDIRPIC32)/%.o: %.c $(HEADERS)
	@mkdir -p $(OBJDIRPIC32)
	@$(CC32) $(CFLAGSPIC32) -c $< -o $@

$(BUILD)/$(SONAME32): lib/eci_api.c $(OBJECTSPIC32) $(RULESTAMP)
	@$(CC32) $(CFLAGSPIC32) -shared -Wl,-soname,$(SONAME32) \
	   lib/eci_api.c $(OBJECTSPIC32) -lpthread -lm -o $@
	@ln -sf $(SONAME32) $(BUILD)/libeci32$(SUF).so
	@echo "built $@"

# An instance made and thrown away over and over, which the suite never does:
# it speaks a great deal through one. A fault in what an instance owns and
# gives back shows here and nowhere else.
instances: $(BUILD)/instances

$(BUILD)/instances: test/harness/instances.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) test/harness/instances.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# An utterance interrupted and another asked for, on one instance, over and
# over. The suite never interrupts anything.
interrupt: $(BUILD)/interrupt

$(BUILD)/interrupt: test/harness/interrupt.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) test/harness/interrupt.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# The sample rate changed on an instance that speaks into a buffer, which the
# suite cannot see: the probe registers a buffer but never asks for another
# rate, and IBM's engine goes silent there too, so both sides agreed.
rate: $(BUILD)/rate
	@$(BUILD)/rate

$(BUILD)/rate: test/harness/rate.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) test/harness/rate.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# The rates IBM never shipped: that the built resonator tables are IBM's own
# formulae, held against IBM's own arrays, and that every rate speaks for the
# same length of time. EVV_RATES_REPORT=1 prints what each one said.
rates: $(BUILD)/rates
	@$(BUILD)/rates
	@EVV_UPSAMPLE=cubic $(BUILD)/rates
	@EVV_UPSAMPLE=hold $(BUILD)/rates
	@EVV_UPSAMPLE=none $(BUILD)/rates

$(BUILD)/rates: test/harness/rates.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) test/harness/rates.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# A romanizer with no language in it, replaying what IBM's romanizer answered.
# This is what proves that everything below the romanizer is already right for
# a language written in another script, before a line of that romanizer exists:
# our engine is handed IBM's own answers at the same seam and has to produce
# the same samples. See the head of test/harness/romcan.c. Built against whichever
# languages LANGS names, since the point of it is Japanese:
#
#   make romcan LANGS=lang/jajp
romcan: $(BUILD)/romcan$(SUF)

$(BUILD)/romcan$(SUF): test/harness/romcan.c $(BUILD)/libevv$(SUF).a
	@$(CC) $(ALL_CFLAGS) test/harness/romcan.c $(BUILD)/libevv$(SUF).a -lpthread -lm -o $@
	@echo "built $@"

# The romanizer's converters, ours against IBM's, one call at a time. The same
# file is built against IBM's objects by `make -C reference TAG=jajp romprims',
# and test/harness/romprims.sh diffs what the two print. This is the only thing that
# reaches a romanizer class the text path never asks for.
romprims: $(BUILD)/romprims$(SUF)

$(BUILD)/romprims$(SUF): test/harness/romprims.c $(BUILD)/libevv$(SUF).a
	@$(CC) $(ALL_CFLAGS) -DEVV_ROMPRIMS_OURS test/harness/romprims.c \
	  $(BUILD)/libevv$(SUF).a -lpthread -lm -o $@
	@echo "built $@"

# One text spoken whole and then in pieces, which is what the add-on does with
# a long message so that a cancel waits out a piece. The suite speaks whole
# utterances and cannot see where a boundary may go; a boundary at a sentence
# end costs nothing and one anywhere else costs the pause a full stop gets.
pieces: $(BUILD)/pieces
	@$(BUILD)/pieces

$(BUILD)/pieces: test/harness/pieces.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) test/harness/pieces.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# The eight voices the caller may edit, which the suite is blind to: nothing it
# runs asks an instance about voice nine, so the loop that copies the language's
# own eight into them can be turned off and all 81 cases still match. That
# happened by accident once. This is what catches it.
voices: $(BUILD)/voices
	@$(BUILD)/voices

$(BUILD)/voices: test/harness/voices.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) test/harness/voices.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# A backtrack landed on from a thread that never planted it, which is how
# stopping the engine from another thread used to fault with nothing in the
# fault to say so. It is meant to be killed by the guard, so it answers
# non-zero when the guard does not fire.
landing: $(BUILD)/landing

$(BUILD)/landing: test/harness/landing.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) test/harness/landing.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# A stop from a thread that is not the one speaking, which is what a screen
# reader does and what test/harness/interrupt.c does not: it aborts from the callback,
# on the engine's own thread. This one crosses a thread and requires the
# interrupted utterance to come out short, so a stop that did nothing fails.
stopthread: $(BUILD)/stopthread
	@$(BUILD)/stopthread

$(BUILD)/stopthread: test/harness/stopthread.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) test/harness/stopthread.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# A key that is not in a section, which neither suite can see: the reader
# decided a key was absent by reading the byte where its search stopped, so
# with another section behind it the key came back holding the next section's
# first value, and a two-language build died on it.
inikeys: $(BUILD)/inikeys
	@$(BUILD)/inikeys

$(BUILD)/inikeys: test/harness/inikeys.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) test/harness/inikeys.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# A machine primitive no rule calls, held against IBM's own. The suite cannot
# see one of these: a call nothing makes cannot be reached by speaking a
# sentence, which is why the primitives the shipped languages never use were
# missing in the first place. `test/harness/prims.sh' builds this and the same file
# against IBM's objects and diffs the two.
prims: $(BUILD)/prims
	@$(BUILD)/prims > /dev/null && echo "built and ran $(BUILD)/prims"

$(BUILD)/prims: test/harness/prims.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) -DEVV_PRIMS_OURS test/harness/prims.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# The IPA converters, held against IBM's own. The suite cannot see these
# either: nothing in the engine asks what an IPA symbol means until the SSML
# reader is on, and even then a sentence reaches only the symbols it spells.
# `test/harness/ipa.sh' builds this and the same file against IBM's objects and diffs
# the two, over every code point in six languages.
ipa: $(BUILD)/ipa
	@$(BUILD)/ipa > /dev/null && echo "built and ran $(BUILD)/ipa"

$(BUILD)/ipa: test/harness/ipa.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) -DEVV_IPA_OURS test/harness/ipa.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

# The XML scanner, held against IBM's own. What has to be right about it is
# the token stream, and neither the suite nor the audio can see that: a
# document tokenised differently may still sound the same. `test/harness/xmltok.sh'
# builds this and the same file against IBM's objects and diffs the handler
# calls.
xmltok: $(BUILD)/xmltok
	@$(BUILD)/xmltok > /dev/null && echo "built and ran $(BUILD)/xmltok"

$(BUILD)/xmltok: test/harness/xmltok.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) -DEVV_XMLTOK_OURS test/harness/xmltok.c $(BUILD)/libevv.a -lpthread -lm -o $@

# And the whole reader through the published filter interface, which is what
# `test/harness/ssml.sh' holds against IBM's own. The document goes in as text and the
# engine's annotations come out as text, so a difference names itself.
ssml: $(BUILD)/ssml
	@$(BUILD)/ssml > /dev/null && echo "built and ran $(BUILD)/ssml"

$(BUILD)/ssml: test/harness/ssml.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) -DEVV_SSML_OURS test/harness/ssml.c $(BUILD)/libevv.a -lpthread -lm -o $@
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

# The rules as text that can be read and edited, in lang/<tag>/rules, and the
# source everything a build compiles is written out of. EVV_NOTATION_LANG says
# which language; English by default.
#
# Written out of IBM's objects once; `notation-check' holds what is in the tree
# against those objects again, emitting the bytecode from each and comparing,
# so it says both that the text is still faithful and, once a rule has been
# changed on purpose, which rules those are. It wants the objects, so it is in
# the same class as the suite: obtainable, and not needed to build.
.PHONY: notation notation-check notation-prove rulecode \
        notation-symbols notation-rewrite upper upper-prove upper-check \
        authored constants codepoints
notation:
	@python3 tools/rules/notation.py tree

notation-check:
	@python3 tools/rules/notation.py verify

# The stronger of the two, and the one to believe: three streams emitted --
# a fresh lift of the objects, that lift written to text and read back, and
# the text as it stands in the tree -- and all three have to agree. The pools
# are shared and numbered in the order the rules are taken, so matching a
# whole stream says the text carries every rule, in order, with nothing added
# and nothing lost, which a rule-by-rule comparison cannot.
notation-prove:
	@python3 tools/rules/notation.py prove

# Where each address the rules name falls. Written out of the objects once,
# because it is the last thing the emitter wanted them for.
notation-symbols:
	@python3 tools/rules/notation.py symbols

# The three files a build compiles, written out of the lifted text alone --
# IBM's rules and nothing of ours. That is one of the two sides `upper-check'
# holds against each other, and it is not what an ordinary build wants; `make
# rulecode' is.
notation-rewrite:
	@python3 tools/rules/notation.py rewrite

# What a rule stands for, and what a rule does.
#
# `upper' and `upper-prove' are the wrappers as the primitive each stands for:
# written out of the lower form and compiled back, where the bytecode has to
# match byte for byte. `authored' is the other upper form, the real rules in
# lang/<tag>/rules/*.up -- every one of them, including the files
# lang/<tag>/rules/trials says are not the module's own, which an ordinary
# build leaves out.
#
# `upper-check' is the one that says whether an authored rule is the rule it
# stands in for. There is no byte comparison to be had: our compiler would
# have to make the same choices IBM's did. So it speaks the seven plain cases
# through a build carrying the authored rules and through one carrying IBM's,
# and holds every rule entered and every call made with its arguments against
# each other, and the audio besides. It wants no objects and no Wine.
upper:
	@python3 tools/rules/notation.py upper

upper-prove:
	@python3 tools/rules/notation.py upper-prove

upper-check:
	@bash tools/rules/check-upper.sh

authored:
	@python3 tools/rules/notation.py authored

# Bytes a rule of ours names by address, out of lang/<tag>/rules/constants
# into the one file in a language module that no lifter writes. Run
# `notation-rewrite' after it: a new store is named in the generated file too.
constants:
	@python3 tools/rules/consts.py $(TAGS)

# What each of a language's own characters arrives as: the code point a caller
# writes and the byte its alphabet knows it by. Authored like the constants
# rather than lifted, since the nine IBM shipped need none -- every letter they
# have is in the byte set the engine was built around, and a language of ours
# can have letters that are not.
codepoints:
	@python3 tools/module/codepoints.py $(TAGS)

# The tables beside the rules, as text: the variables the language declares,
# the settings it carries, the statement table, the lookup sets and the bytes
# the rules name by address. Each has a text form in lang/<tag> and one writer
# for the C, so what a lifter writes and what the text writes cannot drift.
#
# `tables-dump' writes the four texts. Three of them read IBM's objects; the
# sets read the C in the tree instead, on purpose, because the dictionary's
# arrays in that file are laid down by tools/module/dict.py out of the words and
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
	    python3 tools/module/globals.py dump $$t && \
	    python3 tools/module/settings.py dump $$t && \
	    python3 tools/module/link.py dump $$t && \
	    python3 tools/module/sets.py dump $$t && \
	    python3 tools/rules/consts.py dump $$t || exit 1; \
	done

tables-check:
	@for t in $(TAGS); do \
	    python3 tools/module/globals.py regenerate $$t && \
	    python3 tools/module/settings.py regenerate $$t && \
	    python3 tools/module/link.py regenerate $$t && \
	    python3 tools/module/sets.py regenerate $$t && \
	    python3 tools/rules/consts.py regenerate $$t || exit 1; \
	done

# How much of a module is still the module it was copied from. A language IBM
# never shipped starts as one it did and becomes itself a rule at a time, and
# what this answers is how far that has got: TEMPLATE says which to hold it
# against. It reads the text forms only.
.PHONY: census
census:
	@python3 tools/module/census.py $(firstword $(TAGS)) $(TEMPLATE)

tables-write:
	@for t in $(TAGS); do \
	    python3 tools/module/globals.py write $$t && \
	    python3 tools/module/settings.py write $$t && \
	    python3 tools/module/link.py write $$t && \
	    python3 tools/module/sets.py write $$t && \
	    python3 tools/rules/consts.py write $$t || exit 1; \
	done

# The rules as the engine runs them, written out of lang/<tag>/rules. Two
# seconds a language, so this runs before anything else in a build and is
# never noticed.
rulecode: $(RULECODE)

# One rule per language rather than a pattern, for the same reason as the
# decompiler's below: the name carries the language twice and a pattern rule
# may only have the one stem. The three files come out of one run, so they are
# named as a group.
#
# `build' is the module as it means itself: its own rules written in the upper
# form taken in, and the ones lang/<tag>/rules/trials names left out. That is
# the whole of the per-language knowledge, and it is in the module rather than
# here.
define rulecode_from_text
$(1)/delta_rules_$(notdir $(1)).c \
$(1)/delta_rules_$(notdir $(1)).h \
$(1)/delta_rules_shim_$(notdir $(1)).c &: \
                    $(wildcard $(1)/rules/*.dr) $(wildcard $(1)/rules/*.up) \
                    $(wildcard $(1)/rules/symbols) \
                    $(wildcard $(1)/rules/trials) \
                    tools/rules/notation.py tools/rules/lower.py \
                    tools/rules/upper.py tools/rules/emit.py tools/evv.py
	@EVV_NOTATION_LANG=$(notdir $(1)) \
	  python3 tools/rules/notation.py build > /dev/null
	@echo "wrote the rules of $(notdir $(1)) out of $(1)/rules"
endef

# And for a module that has no rules as text. lang/jajp is the one: it is not
# in the tree at all, it is lifted whole by the commands in docs/japanese.md,
# and those write these three files themselves. So there is nothing to build
# them from here, and the rule exists to say that rather than to leave make
# reporting a target it does not know how to make. A file that is already
# there has no prerequisites to be older than, so this never runs for a
# module that has been lifted.
define rulecode_lifted
$(1)/delta_rules_$(notdir $(1)).c \
$(1)/delta_rules_$(notdir $(1)).h \
$(1)/delta_rules_shim_$(notdir $(1)).c &:
	@echo "$(1) holds no rules as text and none of the three files a" >&2
	@echo "build compiles, so there is nothing here to write them from." >&2
	@echo "A module lifted whole rather than kept as text is made by the" >&2
	@echo "commands in docs/japanese.md." >&2
	@false
endef

$(foreach l,$(LANGS),$(eval $(call \
    $(if $(wildcard $(l)/rules),rulecode_from_text,rulecode_lifted),$(l))))

# The rules as C. Thirteen megabytes written out of the bytecode beside it,
# so it is made here rather than kept in the tree, where every change to the
# decompiler would rewrite the whole of it.
rules: $(GENERATED)

# One rule per language rather than a pattern, because the name has the
# language in it twice -- once in the directory and once in the file -- and a
# pattern rule may only have the one stem. EVV_LANG_DIR is how the decompiler
# is told which language to read; without it, `make LANG=lang/dede rules'
# would write English out into lang/dede.
#
# One run writes all PARTS files of a language, so they are named as a group:
# make asks for the recipe once and gets the lot, rather than once per file.
#
# The bytecode is a prerequisite because the decompiler reads it: this writes
# the rules as C out of the array in delta_rules_<tag>.c, which is itself
# written out of the text. A rule changed in the text goes through both.
define rules_for
$(foreach n,$(PARTNS),$(1)/delta_rules_c$(n)_$(notdir $(1)).c) &: \
                                     $(1)/delta_rules_$(notdir $(1)).c \
                                     tools/rules/decompile.py \
                                     tools/rules/patterns.py tools/evv.py
	@rm -f $(1)/delta_rules_c[0-9][0-9]_$(notdir $(1)).c \
	       $(1)/delta_rules_c_$(notdir $(1)).c
	@EVV_LANG_DIR=$(1) EVV_RULE_PARTS=$(PARTS) \
	  python3 tools/rules/decompile.py all
endef
$(foreach l,$(LANGS),$(eval $(call rules_for,$(l))))

# The other direction from `make missing'. That one asks what our code wants
# and nothing of ours defines, which the linker can answer; this asks what
# IBM's objects define that nothing here has written, which the linker never
# can -- a function no rule in the nine shipped languages happens to call is
# a function the link never asks for. The Delta runtime's printing layer sat
# in that blind spot for months.
#
# It wants IBM's objects in analysis/<tag>, so it is not part of any build.
# `--matched' prints what it answered by name rather than by alias, which is
# the half worth looking over; `--object eci' is one object in full.
.PHONY: objects
objects:
	@python3 tools/engine/census.py

# What our code asks for and nothing of ours answers. The C library's own
# names are dropped, since those come from the system; what is left is the
# original's, and writing it is the rest of the port.
missing: $(OBJECTS)
	@$(NM) $(OBJECTS) > $(BUILD)/syms.txt
	@python3 tools/engine/missing.py $(BUILD)/syms.txt

# What the language decided a word was made of, as phonemes rather than as
# sound, which is what eciGeneratePhonemes asks for. Both sides of it: ours
# and the same driver against IBM's own objects, since a wrong letter-to-sound
# answer names itself here where in a wave file it is a hash that moved.
.PHONY: phonemes
phonemes: $(BUILD)/phonemes
	@$(MAKE) -C reference phontry
	@bash test/harness/phonemes.sh

# lib/eci_api.c goes in because the harness calls the published names and the
# archive holds only the engine's own short ones. It brings the constructor
# with it, which is what starts the platform and runs the static
# initialisers, so this driver does not do either for itself.
$(BUILD)/phonemes: test/harness/phonemes.c lib/eci_api.c $(BUILD)/libevv$(SUF).a
	@$(CC) $(ALL_CFLAGS) -DECI_STATIC test/harness/phonemes.c lib/eci_api.c \
	   $(BUILD)/libevv$(SUF).a -lpthread -lm -o $@
	@echo "built $@"

# The eight dictionary calls, held against what IBM's own engine answers.
# They were the last published names with no wrapper here and they are glue
# rather than transcription, which is the kind of code that looks right and
# answers differently -- so it is checked against the original.
.PHONY: dict
dict: $(BUILD)/dict
	@$(MAKE) -C reference dicttry
	@bash test/harness/dict.sh

$(BUILD)/dict: test/harness/dict.c lib/eci_api.c $(BUILD)/libevv$(SUF).a
	@$(CC) $(ALL_CFLAGS) -DECI_STATIC test/harness/dict.c lib/eci_api.c \
	   $(BUILD)/libevv$(SUF).a -lpthread -lm -o $@
	@echo "built $@"

# The words IBM's engine cannot say. It wants neither Wine nor the objects,
# because there is nothing to hold ours against: the original takes a page
# fault on every one of them. What this says is that ours still answers.
.PHONY: crashers
crashers: $(BUILD)/evv
	@test/crashers.sh $(BUILD)/evv

# The gate. Every case of every language spoken and held against what this
# engine has said before -- the samples and the answers the interface gave,
# both -- so that a change made on purpose can be told from one nobody meant.
# It builds the probe it needs and wants neither Wine nor IBM's objects.
#
# `matrix-record' writes the answers down instead of checking them, which is
# what a deliberate change ends with. What test/suite.sh does is a different
# question now -- what IBM's engine does, rather than whether anything moved
# -- and docs/testing.md says when each is the one to reach for.
# It takes no LANGS and ignores the one a build was made with, on purpose. A
# gate that checked only the language last worked on would be worse than none,
# since the thing it exists to catch is a change made for one language landing
# in another. `test/matrix.sh check plpl' is there for iterating on one.
.PHONY: matrix matrix-record
matrix:
	@bash test/matrix.sh check

matrix-record:
	@bash test/matrix.sh record

clean:
	@rm -rf $(BUILD)/obj-* $(BUILD)/obj32-* $(BUILD)/objwin-* \
	        $(BUILD)/objwin32-* $(BUILD)/objpic-* $(BUILD)/objpic32-* \
	        $(BUILD)/libeci*.so $(BUILD)/libeci*.so.* \
	        $(BUILD)/eci32.dll $(BUILD)/dlltest32.exe \
	        $(BUILD)/libevv-win32$(SUF).a $(BUILD)/evv $(BUILD)/probe$(SUF) \
	        $(BUILD)/evv32 $(BUILD)/probe32$(SUF) \
	        $(BUILD)/libevv$(SUF).a $(BUILD)/libevv32$(SUF).a \
	        $(BUILD)/libevv-win$(SUF).a \
	        $(BUILD)/evv.exe $(BUILD)/evvspeak.exe $(BUILD)/eci.dll \
	        $(BUILD)/eci.ini $(BUILD)/dlltest.exe $(BUILD)/syms.txt \
	        $(BUILD)/openevv-*.nvda-addon

# Where `make install' puts it. The command is one binary, which reads no
# file of its own at run time and wants no library but the C one, libm and
# pthreads.
PREFIX  ?= /usr/local
LIBDIR  ?= $(PREFIX)/lib
INCDIR  ?= $(PREFIX)/include

install: $(BUILD)/evv
	@mkdir -p $(DESTDIR)$(PREFIX)/bin
	@cp $(BUILD)/evv $(DESTDIR)$(PREFIX)/bin/evv
	@echo "installed $(DESTDIR)$(PREFIX)/bin/evv"

# And what a program links against, which is a separate target because it is
# a separate build: `make so' first. The real file carries the version and
# the plain name is a link to it, which is what a linker looks for and what a
# dlopen of "libeci.so" finds.
install-lib: $(BUILD)/$(SONAME)
	@mkdir -p $(DESTDIR)$(LIBDIR) $(DESTDIR)$(INCDIR)
	@cp $(BUILD)/$(SONAME) $(DESTDIR)$(LIBDIR)/$(SONAME)
	@ln -sf $(SONAME) $(DESTDIR)$(LIBDIR)/libeci$(SUF).so
	@cp include/eci.h $(DESTDIR)$(INCDIR)/eci.h
	@echo "installed $(DESTDIR)$(LIBDIR)/$(SONAME) and $(DESTDIR)$(INCDIR)/eci.h"

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
# The Win32 porting layer stands in for the POSIX one. src/port/port_win32.c was
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

# The same list for Windows, and RULECODE is named here for the same reason
# it is named above: a wildcard answers with what is on disk when the Makefile
# is read, and on a fresh clone the three files a build writes out of
# lang/<tag>/rules are not there yet. This list had been left to the wildcard,
# so a first build of a clean checkout linked without the bytecode array, the
# rule count and the symbol table, and failed with four undefined references
# that a second build would not reproduce.
SOURCESWIN := $(filter-out %/port_posix.c, \
                $(foreach d,$(SRCDIRS),$(wildcard $(d)/*.c))) \
              $(filter-out $(STALE) $(GENERATED) $(STUBS) $(RULECODE), \
                $(sort $(foreach l,$(LANGS),$(wildcard $(l)/*.c)))) \
              $(filter %.c,$(RULECODE)) \
              $(ROMS) $(RULESRC) $(LANGLIST)
OBJECTSWIN := $(patsubst %.c,$(OBJDIRWIN)/%.o,$(notdir $(SOURCESWIN)))

.PHONY: win win-probe win-dlltest win-stopthread
win: $(BUILD)/evv.exe $(BUILD)/evvspeak.exe $(BUILD)/eci.dll

# The cross-thread stop, for Windows, because that is where it used to fault:
# eight of twelve turns under Wine against none on real Windows, before the
# busy guard. A native Linux run cannot answer for either.
win-stopthread: $(BUILD)/stopthread$(SUF).exe
	@wine $(BUILD)/stopthread$(SUF).exe

$(BUILD)/stopthread$(SUF).exe: test/harness/stopthread.c $(BUILD)/libevv-win$(SUF).a
	@$(CCWIN) $(CFLAGSWIN) test/harness/stopthread.c $(BUILD)/libevv-win$(SUF).a $(LDFLAGSWIN) -o $@
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
# load ours instead. lib/eci_api.c is the whole of the difference: fifty-two
# wrappers and an entry point.
#
# eci.ini goes beside it because add-ons look for one and patch a path inside
# it. Nothing here reads it: the engine carries its own settings in the image.
$(BUILD)/eci.dll: lib/eci_api.c $(OBJDIRWIN)/eci.res $(BUILD)/libevv-win$(SUF).a $(RULESTAMP)
	@$(CCWIN) $(CFLAGSWIN) -shared lib/eci_api.c $(OBJDIRWIN)/eci.res \
	   $(BUILD)/libevv-win$(SUF).a $(LDFLAGSWIN) -o $@
	@cp lib/eci.ini $(BUILD)/eci.ini
	@echo "built $@"

$(OBJDIRWIN)/eci.res: lib/eci.rc
	@mkdir -p $(OBJDIRWIN)
	@$(WINDRES) -I lib lib/eci.rc -O coff -o $@

# Speaks through the library rather than against the engine, by name, the way
# an add-on does. `test/hash.sh build/dlltest.exe' then holds what comes out of
# eci.dll against what comes out of everything else.
win-dlltest: $(BUILD)/dlltest.exe

$(BUILD)/dlltest.exe: test/lib/dll.c $(BUILD)/eci.dll $(RULESTAMP)
	@$(CCWIN) $(CFLAGSWIN) test/lib/dll.c -static -lversion -o $@
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

$(BUILD)/eci32.dll: lib/eci_api.c $(OBJDIRWIN32)/eci.res $(BUILD)/libevv-win32$(SUF).a $(RULESTAMP)
	@$(CCWIN32) $(CFLAGSWIN32) -shared lib/eci_api.c $(OBJDIRWIN32)/eci.res \
	   $(BUILD)/libevv-win32$(SUF).a $(LDFLAGSWIN32) -o $@
	@echo "built $@"

$(BUILD)/dlltest32.exe: test/lib/dll.c $(BUILD)/eci32.dll $(RULESTAMP)
	@$(CCWIN32) $(CFLAGSWIN32) test/lib/dll.c -static -lversion -o $@
	@echo "built $@"

$(OBJDIRWIN32)/eci.res: lib/eci.rc
	@mkdir -p $(OBJDIRWIN32)
	@$(WINDRES32) -I lib lib/eci.rc -O coff -o $@

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
