# IBM's own engine, built from its own objects and driven through the
# published interface, so that there is something to listen against.
#
# This is all that is left of the differential harness. That harness stood our
# objects beside IBM's and compared them call for call, which is how the port
# was made; it is finished -- nothing is borrowed and `make missing' at the top
# answers nothing -- and keeping it alive would have meant every object in the
# engine holding the shape a thirty-two bit compiler gave it in 1999. It is in
# the history, at the last commit where it built.
#
# What is left is the reference binary, which constrains nothing: it is IBM's
# code, linked as IBM linked it. `test/suite.sh' speaks each case through it
# and through ours and compares the samples. On Linux it runs under Wine; on
# Windows it runs as it stands, which is faster and is what the test scripts
# work out for themselves.
#
# `make TAG=dede BUILD=../build/reference-dede' builds the same thing from
# another language's objects, which is the oracle for that language. Both have
# to be given: the default output directory is the English one because that is
# what test/compare.sh looks in when nothing says otherwise.
#
# Three things the Microsoft toolchain did for it have to be done by hand. Its
# C++ compiler gave several helpers and objects names no C identifier can
# spell, so ours are declared under names that can be and the link is told to
# answer the real names with those. Two of the objects the static initialisers
# construct are file-static, so they are made visible first. And the tables
# those initialisers are listed in are taken out, which is next.

CC      := i686-w64-mingw32-gcc
OBJCOPY := i686-w64-mingw32-objcopy

# Which language's objects to build from, and where the binary goes.
TAG     ?= enus
OBJS    ?= ../analysis/$(TAG)

# The engine's headers, which are in groups under ../src and are included by
# bare name, so every group has to be on the path. Worked out rather than
# listed, as in the top-level Makefile.
SRCDIRS := $(patsubst %/,%,$(wildcard ../src/ ../src/*/ ../src/*/*/))
INCS    := $(addprefix -I,$(SRCDIRS))
BUILD   ?= ../build/reference

# The objects are copied here and worked on rather than in place, because
# ../analysis is what the lifters read and nothing should be rewriting it.
WORK    := $(BUILD)/obj

.PHONY: all tap clean
all: $(BUILD)/speak.exe

# IBM's objects, with the static-initialiser tables taken out.
#
# Five of them carry a .CRT$XCU section: a list of pointers to the initialisers
# Microsoft's runtime used to walk on the way in. Every one of those pointers
# names a local symbol called $E1 in a COMDAT called .text$yc, and the GNU
# linker keeps one copy of a COMDAT and points all of them at it -- so a
# runtime that walks the list calls the same initialiser five times with the
# wrong object each time and the program dies before main. Some mingw runtimes
# walk it and some do not, which is why this went unnoticed until the binary
# was first run outside Wine.
#
# Nothing is lost by taking the tables out. src/port/port_ctors.c exists precisely
# to run those initialisers by hand, in the right order, on the right objects,
# and speak.c calls it before anything else.
$(WORK)/.stamp: $(wildcard $(OBJS)/*.obj)
	@mkdir -p $(WORK)
	@cp $(OBJS)/*.obj $(WORK)/
	@for o in $(WORK)/*.obj; do \
	   if $(OBJCOPY) --remove-section='.CRT$$XCU' "$$o" "$$o.tmp" \
	      2>/dev/null; then mv "$$o.tmp" "$$o"; else rm -f "$$o.tmp"; fi; \
	 done
	@$(OBJCOPY) --globalize-symbol=_protectFirstTime \
	  --globalize-symbol=_initializeStandardVoices \
	  $(WORK)/eci.obj $(WORK)/eci.tmp && mv $(WORK)/eci.tmp $(WORK)/eci.obj
# ProsCtrl works out how long its user-index escape is with a strlen in a
# dynamic initialiser, and the rule above has just removed the section that
# would have run it -- so the length stays nought and the one method that
# divides by it faults. The symbol is a file static, so it is globalized here
# and test/harness/romprims.c sets it before sweeping. Only that harness
# needs it; nothing else in this tree reaches the prosody chain yet.
	@if [ -f $(WORK)/PCWriteESPR2.obj ]; then \
	   $(OBJCOPY) --globalize-symbol=_s_nLenUserIdx \
	     $(WORK)/PCWriteESPR2.obj $(WORK)/pc.tmp \
	     && mv $(WORK)/pc.tmp $(WORK)/PCWriteESPR2.obj; \
	 fi
	@touch $@

# The runtime abstraction layer, plus the one file that gives it a machine to
# run on, joined into a single object.
$(BUILD)/ral_shim.obj: ../src/port/port_ral.c ../src/port/port_win32.c ../src/port/evv_port.h
	@mkdir -p $(BUILD)
	@printf '_uni_strlen ?UniStrlen@@YAIPBG@Z\n_uni_strstr ?UniStrstr@@YAPAGPBG0@Z\n' > $(BUILD)/uni.syms
	@$(CC) -O2 -std=gnu99 -w -DEVV_IBM_NAMES $(INCS) -c ../src/port/port_ral.c -o $(BUILD)/ral_shim.tmp.o
	@$(CC) -O2 -std=gnu99 -w -DEVV_IBM_NAMES $(INCS) -c ../src/port/port_win32.c -o $(BUILD)/port.tmp.o
	@i686-w64-mingw32-ld -r $(BUILD)/ral_shim.tmp.o $(BUILD)/port.tmp.o \
	  -o $(BUILD)/ral_both.tmp.o
	@$(OBJCOPY) --redefine-syms=$(BUILD)/uni.syms $(BUILD)/ral_both.tmp.o $@

$(BUILD)/ctors.obj: ../src/port/port_ctors.c
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w -DEVV_IBM_NAMES $(INCS) -c $< -o $@

# Japanese's objects want two names their own set does not carry, so that one
# module gets an extra object. Every other module defines one of them itself.
SHIM :=
ifeq ($(TAG),jajp)
SHIM := $(BUILD)/jajp_shim.obj
endif

# The Japanese driver, which is how the target for a romanizer was found: what
# IBM's own engine does with Japanese script, and which way of asking makes it
# do it. `make TAG=jajp jptry' builds it beside the reference.
.PHONY: jptry
jptry: $(BUILD)/jptry.exe

$(BUILD)/jptry.exe: jptry.c $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj $(SHIM) $(WORK)/.stamp
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w -DEVV_IBM_NAMES $(INCS) jptry.c \
	  $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj $(SHIM) $(wildcard $(WORK)/*.obj) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/jptry.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/jptry.err >&2 || true; exit $$s

# What IBM's own SSML reader turns a document into, which is the oracle the
# SSML transcription was written against. See the head of ssmltry.c: the
# filter is in these objects and nothing in them registers it, so this does.
.PHONY: ssmltry
ssmltry: $(BUILD)/ssmltry.exe

$(BUILD)/ssmltry.exe: ssmltry.c $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj \
                      $(SHIM) $(WORK)/.stamp
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w -DEVV_IBM_NAMES $(INCS) ssmltry.c \
	  $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj $(SHIM) $(wildcard $(WORK)/*.obj) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/ssmltry.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/ssmltry.err >&2 || true; exit $$s
	@echo "built $@"

# What IBM's own eciGeneratePhonemes delivers, which is the oracle that call
# was transcribed against. See the head of phontry.c.
.PHONY: phontry
phontry: $(BUILD)/phontry.exe

$(BUILD)/phontry.exe: phontry.c $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj \
                      $(SHIM) $(WORK)/.stamp
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w -DEVV_IBM_NAMES $(INCS) phontry.c \
	  $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj $(SHIM) $(wildcard $(WORK)/*.obj) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/phontry.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/phontry.err >&2 || true; exit $$s
	@echo "built $@"

# The eight dictionary calls as IBM answers them, which is the oracle the
# glue over them was written against. See the head of dicttry.c.
.PHONY: dicttry
dicttry: $(BUILD)/dicttry.exe

$(BUILD)/dicttry.exe: dicttry.c $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj \
                      $(SHIM) $(WORK)/.stamp
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w -DEVV_IBM_NAMES $(INCS) dicttry.c \
	  $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj $(SHIM) $(wildcard $(WORK)/*.obj) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/dicttry.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/dicttry.err >&2 || true; exit $$s
	@echo "built $@"

$(BUILD)/jajp_shim.obj: jajp_shim.c
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w $(INCS) -c $< -o $@

$(BUILD)/speak.exe: speak.c $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj $(SHIM) $(WORK)/.stamp
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w -DEVV_IBM_NAMES $(INCS) speak.c $(BUILD)/ral_shim.obj \
	  $(BUILD)/ctors.obj $(SHIM) $(wildcard $(WORK)/*.obj) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/link.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/link.err >&2 || true; exit $$s

# The same binary with tap.c standing in front of four of IBM's functions, so
# that what its engine computes can be read out and set against what ours
# computes at the same point. See the head of tap.c; it writes nothing unless
# one of the EVV_TAP_ variables names a file, and the audio is unchanged either
# way, which is the first thing to check before believing a dump.
#
# The renames are what put the wrappers in the path, and each has to be done in
# the object that defines the function. A rename reaches that object's own
# relocations too, so a caller sitting in the same object follows it and never
# meets the wrapper: only a function called from elsewhere can be tapped.
tap: $(BUILD)/speak-tap.exe

# The renamed objects are made by a rule of their own rather than in the link's
# own recipe, because make expands a whole recipe before running any of it and
# a wildcard over a directory the recipe is about to fill comes back empty.
TAPDIR := $(BUILD)/tap

$(TAPDIR)/.stamp: $(WORK)/.stamp
	@mkdir -p $(TAPDIR)
	@cp $(WORK)/*.obj $(TAPDIR)/
	@$(OBJCOPY) --redefine-sym _callSynthesizeArray=_callSynthesizeArray_ibm \
	  $(TAPDIR)/runklatt.obj $(TAPDIR)/t.tmp && mv $(TAPDIR)/t.tmp $(TAPDIR)/runklatt.obj
	@$(OBJCOPY) --redefine-sym _KlattSynth=_KlattSynth_ibm \
	  $(TAPDIR)/clsyn.obj $(TAPDIR)/t.tmp && mv $(TAPDIR)/t.tmp $(TAPDIR)/clsyn.obj
	@$(OBJCOPY) --redefine-sym _addStreamArrayPtValC=_addStreamArrayPtValC_ibm \
	  --redefine-sym _addStreamArraySsValC=_addStreamArraySsValC_ibm \
	  $(TAPDIR)/stmarray.obj $(TAPDIR)/t.tmp && mv $(TAPDIR)/t.tmp $(TAPDIR)/stmarray.obj
	@touch $@

$(BUILD)/speak-tap.exe: speak.c tap.c $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj \
                        $(TAPDIR)/.stamp
	@$(CC) -O2 -std=gnu99 -w -DEVV_IBM_NAMES $(INCS) speak.c tap.c \
	  $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj $(wildcard $(TAPDIR)/*.obj) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/taplink.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/taplink.err >&2 || true; exit $$s
	@echo "built $@"

# IBM's machine primitives called one at a time, which is the only way to
# reach the ones no rule calls. The same file is built against our engine by
# the top-level `make prims', and test/harness/prims.sh diffs what the two print.
# It links the whole set as speak.exe does, because these objects are objects
# and not a library: every name one of them wants has to be answered whether
# this harness reaches it or not.
.PHONY: prims
prims: $(BUILD)/prims.exe

$(BUILD)/prims.exe: ../test/harness/prims.c $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj \
                    $(SHIM) $(WORK)/.stamp
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w $(INCS) ../test/harness/prims.c \
	  $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj $(SHIM) $(wildcard $(WORK)/*.obj) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/prims.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/prims.err >&2 || true; exit $$s
	@echo "built $@"

# The IPA converters out of IBM's own object, for test/harness/ipa.sh. Same
# arrangement as prims above: one file compiled twice, once here and once by
# the top-level `make ipa'.
.PHONY: ipa
ipa: $(BUILD)/ipa.exe

$(BUILD)/ipa.exe: ../test/harness/ipa.c $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj \
                  $(SHIM) $(WORK)/.stamp
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w -DEVV_IBM_NAMES $(INCS) ../test/harness/ipa.c \
	  $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj $(SHIM) $(wildcard $(WORK)/*.obj) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/ipa.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/ipa.err >&2 || true; exit $$s
	@echo "built $@"

# And the XML scanner out of the same objects, for test/harness/xmltok.sh.
.PHONY: xmltok
xmltok: $(BUILD)/xmltok.exe

$(BUILD)/xmltok.exe: ../test/harness/xmltok.c $(BUILD)/ral_shim.obj \
                     $(BUILD)/ctors.obj $(SHIM) $(WORK)/.stamp
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w -DEVV_IBM_NAMES $(INCS) ../test/harness/xmltok.c \
	  $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj $(SHIM) $(wildcard $(WORK)/*.obj) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/xmltok.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/xmltok.err >&2 || true; exit $$s
	@echo "built $@"

# And the whole reader, driven through IBM's own published filter interface,
# for test/harness/ssml.sh. Same file the top-level `make ssml' compiles.
.PHONY: ssml
ssml: $(BUILD)/ssml.exe

$(BUILD)/ssml.exe: ../test/harness/ssml.c $(BUILD)/ral_shim.obj \
                   $(BUILD)/ctors.obj $(SHIM) $(WORK)/.stamp
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w -DEVV_IBM_NAMES $(INCS) ../test/harness/ssml.c \
	  $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj $(SHIM) $(wildcard $(WORK)/*.obj) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/ssml.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/ssml.err >&2 || true; exit $$s
	@echo "built $@"

# The romanizer's converters called directly, which is the other half of
# test/harness/romprims.sh. Only Japanese has a romanizer, so this wants TAG=jajp, and
# it reads the maps in rom/jajp as well as the engine's own headers: the state
# a half-written class is handed is built at IBM's own offsets, and those are
# what say where they are.
.PHONY: romprims
romprims: $(BUILD)/romprims.exe

$(BUILD)/romprims.exe: ../test/harness/romprims.c $(BUILD)/ral_shim.obj \
                       $(BUILD)/ctors.obj $(SHIM) $(WORK)/.stamp
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w -DEVV_IBM_NAMES $(INCS) -I../rom/jajp \
	  ../test/harness/romprims.c \
	  $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj $(SHIM) \
	  $(wildcard $(WORK)/*.obj) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/romprims.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/romprims.err >&2 || true; exit $$s
	@echo "built $@"

clean:
	@rm -rf $(WORK) $(TAPDIR) $(ROMDIR) $(JPDIR)
	@rm -f $(BUILD)/speak.exe $(BUILD)/speak-tap.exe \
	       $(BUILD)/speak-romtap.exe $(BUILD)/speak-jptap.exe \
	       $(BUILD)/romlink.err $(BUILD)/jptaplink.err \
	       $(BUILD)/jptry.exe $(BUILD)/jptry.err $(BUILD)/ral_shim.obj \
	       $(BUILD)/ctors.obj $(BUILD)/eci.obj $(BUILD)/*.tmp.o \
	       $(BUILD)/prims.exe $(BUILD)/prims.err \
	       $(BUILD)/ipa.exe $(BUILD)/ipa.err \
	       $(BUILD)/xmltok.exe $(BUILD)/xmltok.err \
	       $(BUILD)/ssml.exe $(BUILD)/ssml.err \
	       $(BUILD)/uni.syms $(BUILD)/link.err $(BUILD)/taplink.err

# The romanizer's own seam, tapped. Six of RomanizerManager's public methods
# renamed and romtap.c standing in front of them, so that what IBM's romanizer
# is handed and what it hands back can be written down. See the head of that
# file. `make TAG=jajp BUILD=../build/reference-jajp romtap'.
.PHONY: romtap
romtap: $(BUILD)/speak-romtap.exe

ROMDIR := $(BUILD)/romtap

$(ROMDIR)/.stamp: $(WORK)/.stamp
	@mkdir -p $(ROMDIR)
	@cp $(WORK)/*.obj $(ROMDIR)/
	@$(OBJCOPY) \
	  --redefine-sym '?addText@RomanizerManager@@QAEHPBDHH@Z=_rmAddText_ibm' \
	  --redefine-sym '?addParam@RomanizerManager@@QAEHPBDH@Z=_rmAddParam_ibm' \
	  --redefine-sym '?insertIndex@RomanizerManager@@QAEHXZ=_rmInsertIndex_ibm' \
	  --redefine-sym '?processSentence@RomanizerManager@@QAEHPAPADH@Z=_rmProcessSentence_ibm' \
	  --redefine-sym '?processRemaining@RomanizerManager@@QAEHPAPAD@Z=_rmProcessRemaining_ibm' \
	  --redefine-sym '?setParam@RomanizerManager@@QAEHJH@Z=_rmSetParam_ibm' \
	  --redefine-sym '?stop@RomanizerManager@@QAEHXZ=_rmStop_ibm' \
	  --redefine-sym '?resume@RomanizerManager@@QAEHXZ=_rmResume_ibm' \
	  $(ROMDIR)/rommanager.obj $(ROMDIR)/r.tmp \
	  && mv $(ROMDIR)/r.tmp $(ROMDIR)/rommanager.obj
	@touch $@

$(BUILD)/speak-romtap.exe: speak.c romtap.c $(BUILD)/ral_shim.obj \
                           $(BUILD)/ctors.obj $(SHIM) $(ROMDIR)/.stamp
	@$(CC) -O2 -std=gnu99 -w -DEVV_IBM_NAMES $(INCS) speak.c romtap.c \
	  $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj $(SHIM) \
	  $(wildcard $(ROMDIR)/*.obj) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/romlink.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/romlink.err >&2 || true; exit $$s
	@echo "built $@"

# One seam further in. The romanizer's tap says whether the two romanizers
# agree on a sentence; this one says whether they agree on the lattice of
# dictionary entries and the paths over it, which is where a sentence that
# comes out as one phrase instead of two parts company. See jptap.c.
# `make TAG=jajp BUILD=../build/reference-jajp jptap'.
.PHONY: jptap
jptap: $(BUILD)/speak-jptap.exe

JPDIR := $(BUILD)/jptap

$(JPDIR)/.stamp: $(WORK)/.stamp
	@mkdir -p $(JPDIR)
	@cp $(WORK)/*.obj $(JPDIR)/
	@$(OBJCOPY) --redefine-sym '?Make@JPath@@QAEXF@Z=_jpMake_ibm' \
	  $(JPDIR)/jpath.obj $(JPDIR)/j.tmp && mv $(JPDIR)/j.tmp $(JPDIR)/jpath.obj
	@$(OBJCOPY) \
	  --redefine-sym '?LookupEngWordDict@DictSearch@@QAEFPAEFFFH@Z=_engLookup_ibm' \
	  $(JPDIR)/dictapi.obj $(JPDIR)/d.tmp \
	  && mv $(JPDIR)/d.tmp $(JPDIR)/dictapi.obj
	@touch $@

$(BUILD)/speak-jptap.exe: speak.c jptap.c $(BUILD)/ral_shim.obj \
                          $(BUILD)/ctors.obj $(SHIM) $(JPDIR)/.stamp
	@$(CC) -O2 -std=gnu99 -w -DEVV_IBM_NAMES $(INCS) speak.c jptap.c \
	  $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj $(SHIM) \
	  $(wildcard $(JPDIR)/*.obj) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/jptaplink.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/jptaplink.err >&2 || true; exit $$s
	@echo "built $@"
