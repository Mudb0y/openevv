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

SRC   := src
LANG  := lang/enus
BUILD := build

CC  ?= cc
NM  ?= nm

# Which rules the interpreter finds already written as C.
#
# `bytecode' links an empty table, so every rule runs as bytecode. `c' links
# the thirteen megabytes tools/delta-decompile.py writes out of that same
# bytecode, which the interpreter then prefers for every rule it has. Both
# speak the same samples; the difference is seven minutes of compiler and
# Python to write the file first. See docs/building.md.
RULES ?= bytecode

GENERATED := $(LANG)/delta_rules_c.c
STUB      := $(SRC)/delta_rules_none.c

ifeq ($(RULES),bytecode)
RULESRC := $(STUB)
else ifeq ($(RULES),c)
RULESRC := $(GENERATED)
else
$(error RULES is bytecode or c, not $(RULES))
endif

# The engine, plus the language beside it. port_win32.c is the Windows
# porting layer and belongs to the reference build; the two rule tables are
# chosen between above rather than both linked.
SOURCES := $(filter-out $(SRC)/port_win32.c $(STUB),$(wildcard $(SRC)/*.c)) \
           $(filter-out $(GENERATED),$(sort $(wildcard $(LANG)/*.c))) \
           $(RULESRC)

# Every header, because a struct that changed shape and an object that was
# not rebuilt is a link that succeeds and an engine that writes over itself.
HEADERS := $(wildcard $(SRC)/*.h) $(wildcard $(LANG)/*.h)

vpath %.c $(SRC) $(LANG)

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
ALL_CFLAGS := $(OPT) -std=gnu99 -I$(SRC) -I$(LANG) $(WARN) $(LOW) $(CFLAGS)

OBJDIR  := $(BUILD)/obj
OBJECTS := $(patsubst %.c,$(OBJDIR)/%.o,$(notdir $(SOURCES)))

.PHONY: all probe rules missing install clean evv32 probe32
all: $(BUILD)/evv

$(BUILD)/evv: cli/evv.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) cli/evv.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

probe: $(BUILD)/probe

$(BUILD)/probe: cli/probe.c $(BUILD)/libevv.a
	@$(CC) $(ALL_CFLAGS) cli/probe.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

$(OBJDIR)/%.o: %.c $(HEADERS)
	@mkdir -p $(OBJDIR)
	@$(CC) $(ALL_CFLAGS) -c $< -o $@

# A source that gets renamed leaves its object behind, and a stale one is a
# duplicate definition waiting to happen, so they go before the archive is
# built rather than at the next link. Switching RULES leaves one the same way.
$(BUILD)/libevv.a: $(OBJECTS)
	@for o in $(OBJDIR)/*.o; do \
	   case " $(OBJECTS) " in *" $$o "*) ;; *) rm -f "$$o" ;; esac; \
	 done
	@rm -f $@
	@ar rcs $@ $(OBJECTS)
	@echo "built $@ from $(words $(OBJECTS)) objects"

# The rules as C. Thirteen megabytes written out of the bytecode beside it,
# so it is made here rather than kept in the tree, where every change to the
# decompiler would rewrite the whole of it.
rules: $(GENERATED)

$(GENERATED): $(LANG)/delta_rules_enus.c $(LANG)/delta_consts_enus.c \
              tools/delta-decompile.py tools/delta-census.py
	@python3 tools/delta-decompile.py all

# What our code asks for and nothing of ours answers. The C library's own
# names are dropped, since those come from the system; what is left is the
# original's, and writing it is the rest of the port.
missing: $(OBJECTS)
	@$(NM) $(OBJECTS) > $(BUILD)/syms.txt
	@python3 tools/missing.py $(BUILD)/syms.txt

clean:
	@rm -rf $(OBJDIR) $(OBJDIR32) $(OBJDIRWIN) $(BUILD)/evv $(BUILD)/probe \
	        $(BUILD)/evv32 $(BUILD)/probe32 \
	        $(BUILD)/libevv.a $(BUILD)/libevv32.a $(BUILD)/libevv-win.a \
	        $(BUILD)/evv.exe $(BUILD)/evvspeak.exe $(BUILD)/eci.dll \
	        $(BUILD)/eci.ini $(BUILD)/dlltest.exe $(BUILD)/syms.txt

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
OBJDIR32  := $(BUILD)/obj32
CFLAGS32  := $(OPT) -std=gnu99 -I$(SRC) -I$(LANG) $(WARN) $(CFLAGS)
OBJECTS32 := $(patsubst %.c,$(OBJDIR32)/%.o,$(notdir $(SOURCES)))

evv32: $(BUILD)/evv32
probe32: $(BUILD)/probe32

$(BUILD)/evv32: cli/evv.c $(BUILD)/libevv32.a
	@$(CC32) $(CFLAGS32) cli/evv.c $(BUILD)/libevv32.a -lpthread -lm -o $@
	@echo "built $@"

$(BUILD)/probe32: cli/probe.c $(BUILD)/libevv32.a
	@$(CC32) $(CFLAGS32) cli/probe.c $(BUILD)/libevv32.a -lpthread -lm -o $@
	@echo "built $@"

$(OBJDIR32)/%.o: %.c $(HEADERS)
	@mkdir -p $(OBJDIR32)
	@$(CC32) $(CFLAGS32) -c $< -o $@

$(BUILD)/libevv32.a: $(OBJECTS32)
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
OBJDIRWIN  := $(BUILD)/objwin

CFLAGSWIN  := $(OPT) -std=gnu99 -I$(SRC) -I$(LANG) $(WARN) -DEVV_ARENA=1 \
              $(CFLAGS)
# Static, so what ships is one file. MINGW64_LDFLAGS is where the cross gcc's
# thread runtime is; the flake sets it, since nothing puts it on the link path
# outside a real cross stdenv.
LDFLAGSWIN := -static $(MINGW64_LDFLAGS)

SOURCESWIN := $(filter-out $(SRC)/port_posix.c $(STUB),$(wildcard $(SRC)/*.c)) \
              $(filter-out $(GENERATED),$(sort $(wildcard $(LANG)/*.c))) \
              $(RULESRC)
OBJECTSWIN := $(patsubst %.c,$(OBJDIRWIN)/%.o,$(notdir $(SOURCESWIN)))

.PHONY: win win-probe win-dlltest
win: $(BUILD)/evv.exe $(BUILD)/evvspeak.exe $(BUILD)/eci.dll

# The probe, for Windows, which is how a fault there gets localised: it says
# what the engine answered at every step, so the last line it prints is where
# to look.
win-probe: $(BUILD)/probe.exe

$(BUILD)/probe.exe: cli/probe.c $(BUILD)/libevv-win.a
	@$(CCWIN) $(CFLAGSWIN) cli/probe.c $(BUILD)/libevv-win.a $(LDFLAGSWIN) -o $@
	@echo "built $@"

$(BUILD)/evv.exe: cli/evv.c $(BUILD)/libevv-win.a
	@$(CCWIN) $(CFLAGSWIN) cli/evv.c $(BUILD)/libevv-win.a $(LDFLAGSWIN) -o $@
	@echo "built $@"

# -mwindows because a speak window with a console behind it looks like a
# mistake. winmm is the sound: waveOut is all this needs and every Windows
# since 1995 has it.
$(BUILD)/evvspeak.exe: win/speak.c $(OBJDIRWIN)/speak.res $(BUILD)/libevv-win.a
	@$(CCWIN) $(CFLAGSWIN) -mwindows win/speak.c $(OBJDIRWIN)/speak.res \
	   $(BUILD)/libevv-win.a $(LDFLAGSWIN) -lwinmm -lcomdlg32 -o $@
	@echo "built $@"

# The engine as a library, under the names IBM published, so that a program
# written against IBM's eci.dll -- a screen reader add-on, most likely -- can
# load ours instead. win/eci_api.c is the whole of the difference: fifty-two
# wrappers and an entry point.
#
# eci.ini goes beside it because add-ons look for one and patch a path inside
# it. Nothing here reads it: the engine carries its own settings in the image.
$(BUILD)/eci.dll: win/eci_api.c $(BUILD)/libevv-win.a
	@$(CCWIN) $(CFLAGSWIN) -shared win/eci_api.c $(BUILD)/libevv-win.a \
	   $(LDFLAGSWIN) -o $@
	@cp win/eci.ini $(BUILD)/eci.ini
	@echo "built $@"

# Speaks through the library rather than against the engine, by name, the way
# an add-on does. `test/hash.sh build/dlltest.exe' then holds what comes out of
# eci.dll against what comes out of everything else.
win-dlltest: $(BUILD)/dlltest.exe

$(BUILD)/dlltest.exe: test/dll.c $(BUILD)/eci.dll
	@$(CCWIN) $(CFLAGSWIN) test/dll.c -static -o $@
	@echo "built $@"

$(OBJDIRWIN)/speak.res: win/speak.rc win/speak.h
	@mkdir -p $(OBJDIRWIN)
	@$(WINDRES) -I win win/speak.rc -O coff -o $@

$(OBJDIRWIN)/%.o: %.c $(HEADERS)
	@mkdir -p $(OBJDIRWIN)
	@$(CCWIN) $(CFLAGSWIN) -c $< -o $@

$(BUILD)/libevv-win.a: $(OBJECTSWIN)
	@for o in $(OBJDIRWIN)/*.o; do \
	   case " $(OBJECTSWIN) " in *" $$o "*) ;; *) rm -f "$$o" ;; esac; \
	 done
	@rm -f $@
	@ar rcs $@ $(OBJECTSWIN)
	@echo "built $@ from $(words $(OBJECTSWIN)) objects"
