CC      := i686-w64-mingw32-gcc
CFLAGS  := -O2 -std=c99 -Wall -Wextra -I../src
IBM     := ../analysis/obj/clsyn-ibm.obj
BUILD   := ../build

all: $(BUILD)/diff.exe

$(BUILD)/diff.exe: diff.c ../src/klatt_fx.c ../src/klatt_state.c ../src/klatt_synth.c ../src/klatt_fx.h ../src/klatt_state.h $(IBM)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) diff.c ../src/klatt_fx.c ../src/klatt_state.c ../src/klatt_tables.c ../src/klatt_synth.c $(IBM) $(MINGW_LDFLAGS) -o $@

$(IBM):
	@echo "run tools/extract.sh first" >&2; exit 1

run: $(BUILD)/diff.exe
	wine $(BUILD)/diff.exe

clean:
	rm -rf $(BUILD)

.PHONY: all run clean delta delta-run

# The Delta runtime, linked from IBM's own objects so our primitives can be
# compared against them the way the Klatt ones were.
ENUS    := ../analysis/enus

# Objects of the original the link does without. deltio is one we replaced:
# everything in it that anything called is ours. The rest are ones a spoken
# sentence never goes through at all, each one taken out on its own, relinked
# and listened to: the Delta debugger's display, the licence expiry and
# registry, the trace and plot helpers, and the user dictionary's own reader.
#
# Dropping a name that something still calls is a link error rather than a
# quiet change, so this list cannot go stale without saying so.
DEAD    := deltio ut_dict ddelta dutil win_cryptstdio expire win_reg \
           win_searchlib win_dbgtest win_ecitrace win_hrtime win_debug \
           debug plot test
DELTA_OBJS := \
  $(ENUS)/assign.obj $(ENUS)/debug.obj $(ENUS)/delta.obj $(ENUS)/dfault.obj \
  $(ENUS)/extra.obj $(ENUS)/for.obj $(ENUS)/io.obj $(ENUS)/misc.obj \
  $(ENUS)/optimize.obj $(ENUS)/pointer.obj $(ENUS)/stack.obj $(ENUS)/test.obj \
  $(ENUS)/init.obj $(ENUS)/gener.obj \
  $(ENUS)/access.obj $(ENUS)/ctxt.obj $(ENUS)/ddelta.obj $(ENUS)/deltaimp.obj \
  $(ENUS)/deltio.obj $(ENUS)/dttime.obj $(ENUS)/dutil.obj $(ENUS)/heap.obj \
  $(ENUS)/mem.obj $(ENUS)/prdelta.obj $(ENUS)/prdeltao.obj $(ENUS)/rectbuf.obj \
  $(ENUS)/win_dynabuf.obj $(ENUS)/logio.obj $(ENUS)/win_hash.obj \
  $(ENUS)/link.obj $(ENUS)/win_etistr.obj \
  $(ENUS)/filio.obj $(ENUS)/win_filename.obj $(ENUS)/capp.obj

IBMDELTA := $(wildcard ../analysis/delta-ibm/*.obj)
# Support objects the runtime needs but that define no primitive we shadow,
# so they link unprefixed.
DELTA_SUPPORT := $(ENUS)/win_dynabuf.obj $(ENUS)/logio.obj $(ENUS)/win_hash.obj \
  $(ENUS)/link.obj $(ENUS)/win_etistr.obj \
  $(ENUS)/filio.obj $(ENUS)/win_filename.obj

delta: $(BUILD)/delta_probe.exe $(BUILD)/delta_diff.exe

$(BUILD)/delta_diff.exe: delta_diff.c delta_stubs.c ../src/delta.c ../src/delta_heap.c ../src/delta_sysmem.c ../src/delta_tables.c ../src/delta_trace.c ../src/delta.h
	@mkdir -p $(BUILD)
	@$(CC) $(CFLAGS) delta_diff.c delta_stubs.c ../src/delta.c ../src/delta_heap.c ../src/delta_sysmem.c ../src/delta_tables.c ../src/delta_trace.c $(IBMDELTA) $(DELTA_SUPPORT) $(MINGW_LDFLAGS) -o $@ 2> $(BUILD)/link.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/link.err >&2 || true; exit $$s

$(BUILD)/delta_probe.exe: delta_probe.c delta_stubs.c
	@mkdir -p $(BUILD)
	@$(CC) $(CFLAGS) delta_probe.c delta_stubs.c $(DELTA_OBJS) $(MINGW_LDFLAGS) -o $@ 2> $(BUILD)/link.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/link.err >&2 || true; exit $$s

delta-run: delta
	wine $(BUILD)/delta_probe.exe
	wine $(BUILD)/delta_diff.exe

# The whole engine, linked from IBM's own objects and driven through the
# published interface, so we can hear what we are aiming at.
#
# Two things the Microsoft toolchain did for it have to be done by hand. Its
# C++ compiler gave several helpers and objects names no C identifier can
# spell, so ours are declared under names that can be and the link is told to
# answer the real names with those. And two of the objects the static
# initialisers construct are file-static, so they are made visible first.
OBJCOPY := i686-w64-mingw32-objcopy

speak: $(BUILD)/speak.exe

$(BUILD)/ral_shim.obj: ral_shim.c
	@mkdir -p $(BUILD)
	@printf '_uni_strlen ?UniStrlen@@YAIPBG@Z\n_uni_strstr ?UniStrstr@@YAPAGPBG0@Z\n' > $(BUILD)/uni.syms
	@$(CC) -O2 -std=gnu99 -w -c $< -o $(BUILD)/ral_shim.tmp.o
	@$(OBJCOPY) --redefine-syms=$(BUILD)/uni.syms $(BUILD)/ral_shim.tmp.o $@

$(BUILD)/ctors.obj: ctors.c
	@mkdir -p $(BUILD)
	@printf '%s\n' \
	  '_evv_mutex_ctor ??0Mutex@@QAE@H@Z' \
	  '_evv_isv_ctor ??0InitializeStandardVoices@@QAE@XZ' \
	  '_evv_filter_load_mutex ?m_protectFilterLoad@FilterManager@@0VMutex@@A' \
	  '_evv_ssml_lexer_mutex ?lexerMutex@SSMLFilter@@1VMutex@@A' \
	  '_evv_synth_init_mutex ?m_protectInitialization@SynthThread@@0VMutex@@A' \
	  '_evv_protect_first_time _protectFirstTime' \
	  '_evv_standard_voices _initializeStandardVoices' > $(BUILD)/ctors.syms
	@$(CC) -O2 -std=gnu99 -w -c $< -o $(BUILD)/ctors.tmp.o
	@$(OBJCOPY) --redefine-syms=$(BUILD)/ctors.syms $(BUILD)/ctors.tmp.o $@

$(BUILD)/eci.obj: $(ENUS)/eci.obj
	@mkdir -p $(BUILD)
	@$(OBJCOPY) --globalize-symbol=_protectFirstTime \
	  --globalize-symbol=_initializeStandardVoices $< $@

$(BUILD)/speak.exe: speak.c $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj $(BUILD)/eci.obj
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w -I../src speak.c $(BUILD)/ral_shim.obj \
	  $(BUILD)/ctors.obj $(BUILD)/eci.obj \
	  $(filter-out $(ENUS)/eci.obj,$(wildcard $(ENUS)/*.obj)) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/link.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/link.err >&2 || true; exit $$s

# The same thing with a handful of the engine's own functions wrapped, so a
# run says what each stage was asked and what it answered. Each wrapped
# function has its definition renamed out of the way in its own object while
# the probe takes the original name, which only works where the caller lives
# in a different object from the callee.
PROBE_OBJS := eci2 soundfil clsyn runklatt engarray filtermanager rommanager \
              dll_link glob

speak-probe: $(BUILD)/speak_probe.exe

$(BUILD)/probe/%.obj: $(ENUS)/%.obj probe.syms
	@mkdir -p $(BUILD)/probe
	@$(OBJCOPY) --redefine-syms=probe.syms $< $@

$(BUILD)/probe.obj: probe.c probe-take.syms
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w -c $< -o $(BUILD)/probe.tmp.o
	@$(OBJCOPY) --redefine-syms=probe-take.syms $(BUILD)/probe.tmp.o $@

$(BUILD)/speak_probe.exe: speak.c $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj \
    $(BUILD)/eci.obj $(BUILD)/probe.obj \
    $(patsubst %,$(BUILD)/probe/%.obj,$(PROBE_OBJS))
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w -I../src speak.c $(BUILD)/ral_shim.obj \
	  $(BUILD)/ctors.obj $(BUILD)/eci.obj $(BUILD)/probe.obj \
	  $(patsubst %,$(BUILD)/probe/%.obj,$(PROBE_OBJS)) \
	  $(filter-out $(ENUS)/eci.obj $(patsubst %,$(ENUS)/%.obj,$(DEAD)) $(patsubst %,$(ENUS)/%.obj,$(PROBE_OBJS)),\
	    $(wildcard $(ENUS)/*.obj)) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/link.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/link.err >&2 || true; exit $$s

.PHONY: speak speak-probe

# The same engine again with every rule run by our interpreter instead of by
# the code the language's compiler generated for it. Each object that defines
# rules has its own definitions renamed out of the way, so everything that
# reaches a rule by name reaches the stand-in; the rename is per object
# because renaming a rule everywhere would take the calls other objects make
# to it along with it, and those are the calls that have to arrive.
RULE_OBJS := $(shell cat ../src/rules/objects 2>/dev/null)
RULE_SWAP := $(patsubst %,$(BUILD)/rules/%.obj,$(RULE_OBJS))

speak-rules: $(BUILD)/speak_rules.exe

$(BUILD)/rules/%.obj: $(ENUS)/%.obj ../src/delta_rules_enus.c
	@mkdir -p $(BUILD)/rules
	@g=""; [ -s ../src/rules/$*.glob ] \
	  && g="--globalize-symbols=../src/rules/$*.glob"; \
	  $(OBJCOPY) --redefine-syms=../src/rules/$*.ren $$g $< $@

$(BUILD)/delta_rules.obj: ../src/delta_rules.c ../src/delta_rules.h
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=c99 -w -I../src -c $< -o $@

$(BUILD)/delta_rules_enus.obj: ../src/delta_rules_enus.c ../src/delta_rules.h
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=c99 -w -I../src -c $< -o $@

$(BUILD)/delta_consts_enus.obj: ../src/delta_consts_enus.c
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=c99 -w -I../src -c $< -o $@

$(BUILD)/delta_rules_shim.obj: ../src/delta_rules_shim.c ../src/delta_rules.h
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=c99 -w -I../src -c $< -o $@

$(BUILD)/speak_rules.exe: speak.c $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj \
    $(BUILD)/eci.obj $(BUILD)/delta_rules.obj $(BUILD)/delta_rules_enus.obj $(BUILD)/delta_consts_enus.obj \
    $(BUILD)/delta_rules_shim.obj $(RULE_SWAP)
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w -I../src speak.c $(BUILD)/ral_shim.obj \
	  $(BUILD)/ctors.obj $(BUILD)/eci.obj $(BUILD)/delta_rules.obj \
	  $(BUILD)/delta_rules_enus.obj $(BUILD)/delta_consts_enus.obj $(BUILD)/delta_rules_shim.obj \
	  $(RULE_SWAP) \
	  $(filter-out $(ENUS)/eci.obj $(patsubst %,$(ENUS)/%.obj,$(DEAD)) $(patsubst %,$(ENUS)/%.obj,$(RULE_OBJS)),\
	    $(wildcard $(ENUS)/*.obj)) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/link.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/link.err >&2 || true; exit $$s

.PHONY: speak-rules

# The original rules again, each saying that it ran before handing over, so
# that a run of the engine as it was and a run with the interpreter can be
# set side by side and the first place they part company found.
speak-ref: $(BUILD)/speak_ref.exe

$(BUILD)/delta_rules_ref.obj: ../src/delta_rules_ref.c ../src/delta_rules.h
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=c99 -w -I../src -c $< -o $@

$(BUILD)/speak_ref.exe: speak.c $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj \
    $(BUILD)/eci.obj $(BUILD)/delta_rules_ref.obj $(RULE_SWAP)
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w -I../src speak.c $(BUILD)/ral_shim.obj \
	  $(BUILD)/ctors.obj $(BUILD)/eci.obj $(BUILD)/delta_rules_ref.obj \
	  $(RULE_SWAP) \
	  $(filter-out $(ENUS)/eci.obj $(patsubst %,$(ENUS)/%.obj,$(DEAD)) $(patsubst %,$(ENUS)/%.obj,$(RULE_OBJS)),\
	    $(wildcard $(ENUS)/*.obj)) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/link.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/link.err >&2 || true; exit $$s

.PHONY: speak-ref

# Both engines again with every runtime entry saying what it was asked. The
# two runs then say the same things about themselves and can be set side by
# side; where they first differ is where to look.
TRACE_OBJS := $(sort $(RULE_OBJS) $(shell cat ../src/trace/objects 2>/dev/null))
TRACE_SWAP := $(patsubst %,$(BUILD)/traced/%.obj,$(TRACE_OBJS))

$(BUILD)/traced/%.obj: $(ENUS)/%.obj ../src/delta_rules_enus.c
	@mkdir -p $(BUILD)/traced
	@cat ../src/rules/$*.ren ../src/trace/$*.ren 2>/dev/null \
	  > $(BUILD)/traced/$*.ren || true
	@g=""; [ -s ../src/rules/$*.glob ] \
	  && g="--globalize-symbols=../src/rules/$*.glob"; \
	  $(OBJCOPY) --redefine-syms=$(BUILD)/traced/$*.ren $$g $< $@

$(BUILD)/delta_rules_trace.obj: ../src/delta_rules_trace.c
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=c99 -w -I../src -c $< -o $@

$(BUILD)/delta_rules_trace_prim.obj: ../src/delta_rules_trace_prim.c
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=c99 -w -I../src -c $< -o $@

TRACE_REST = $(filter-out $(ENUS)/eci.obj \
               $(patsubst %,$(ENUS)/%.obj,$(TRACE_OBJS)),$(wildcard $(ENUS)/*.obj))

trace: $(BUILD)/trace_ref.exe $(BUILD)/trace_rules.exe

$(BUILD)/trace_ref.exe: speak.c $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj \
    $(BUILD)/eci.obj $(BUILD)/delta_rules_ref.obj \
    $(BUILD)/delta_rules_trace.obj $(TRACE_SWAP)
	@$(CC) -O2 -std=gnu99 -w -I../src speak.c $(BUILD)/ral_shim.obj \
	  $(BUILD)/ctors.obj $(BUILD)/eci.obj $(BUILD)/delta_rules_ref.obj \
	  $(BUILD)/delta_rules_trace.obj $(TRACE_SWAP) $(TRACE_REST) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/link.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/link.err >&2 || true; exit $$s

$(BUILD)/trace_rules.exe: speak.c $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj \
    $(BUILD)/eci.obj $(BUILD)/delta_rules.obj $(BUILD)/delta_rules_enus.obj $(BUILD)/delta_consts_enus.obj \
    $(BUILD)/delta_rules_shim.obj $(BUILD)/delta_rules_trace.obj $(TRACE_SWAP)
	@$(CC) -O2 -std=gnu99 -w -I../src speak.c $(BUILD)/ral_shim.obj \
	  $(BUILD)/ctors.obj $(BUILD)/eci.obj $(BUILD)/delta_rules.obj \
	  $(BUILD)/delta_rules_enus.obj $(BUILD)/delta_consts_enus.obj $(BUILD)/delta_rules_shim.obj \
	  $(BUILD)/delta_rules_trace.obj $(TRACE_SWAP) $(TRACE_REST) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/link.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/link.err >&2 || true; exit $$s

.PHONY: trace

# The same engine again with our own transcribed primitives underneath the
# interpreted rules, so that what runs is the port rather than the original.
# Each object that defines a primitive we have has its own definition stood
# aside, the same way the rules were, and our sources supply the name.
# Which objects hand their primitives over to us. Narrowing this is how a
# fault gets pinned to one of them.
PRIM_ONLY  ?= $(shell cat ../src/prim/objects 2>/dev/null)
PRIM_OBJS  := $(PRIM_ONLY)
SWAP_OBJS  := $(sort $(RULE_OBJS) $(PRIM_OBJS))
SWAP_SWAP  := $(patsubst %,$(BUILD)/swap/%.obj,$(SWAP_OBJS))
OUR_DELTA  := ../src/delta.c ../src/delta_heap.c ../src/delta_sysmem.c \
              ../src/delta_tables.c ../src/delta_trace.c \
              ../src/delta_savefile.c ../src/eci_dynabuf.c \
              ../src/klatt_fx.c ../src/klatt_state.c ../src/klatt_synth.c \
              ../src/klatt_tables.c ../src/delta_link_enus.c \
              ../src/delta_sets_enus.c

speak-prim: $(BUILD)/speak_prim.exe

$(BUILD)/swap/%.obj: $(ENUS)/%.obj ../src/delta_rules_enus.c ../src/prim/objects
	@mkdir -p $(BUILD)/swap
	@cat ../src/rules/$*.ren ../src/prim/$*.ren 2>/dev/null \
	  > $(BUILD)/swap/$*.ren || true
	@g=""; [ -s ../src/rules/$*.glob ] \
	  && g="--globalize-symbols=../src/rules/$*.glob"; \
	  $(OBJCOPY) --redefine-syms=$(BUILD)/swap/$*.ren $$g $< $@

$(BUILD)/speak_prim.exe: speak.c $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj \
    $(BUILD)/eci.obj $(BUILD)/delta_rules.obj $(BUILD)/delta_rules_enus.obj $(BUILD)/delta_consts_enus.obj \
    $(BUILD)/delta_rules_shim.obj $(SWAP_SWAP) $(OUR_DELTA)
	@$(CC) -O2 -std=gnu99 -w -I../src speak.c $(BUILD)/ral_shim.obj \
	  $(BUILD)/ctors.obj $(BUILD)/eci.obj $(BUILD)/delta_rules.obj \
	  $(BUILD)/delta_rules_enus.obj $(BUILD)/delta_consts_enus.obj $(BUILD)/delta_rules_shim.obj \
	  $(OUR_DELTA) $(SWAP_SWAP) \
	  $(filter-out $(ENUS)/eci.obj $(patsubst %,$(ENUS)/%.obj,$(DEAD)) $(patsubst %,$(ENUS)/%.obj,$(SWAP_OBJS)),\
	    $(wildcard $(ENUS)/*.obj)) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/link.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/link.err >&2 || true; exit $$s

# The statement table we lifted set against the one it came from, linked
# exactly as the engine links it so that what is compared is what runs.
stmt-diff: $(BUILD)/stmt_diff.exe

$(BUILD)/stmt_diff.exe: stmt_diff.c $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj \
    $(BUILD)/eci.obj $(BUILD)/delta_rules.obj $(BUILD)/delta_rules_enus.obj $(BUILD)/delta_consts_enus.obj \
    $(BUILD)/delta_rules_shim.obj $(SWAP_SWAP) $(OUR_DELTA)
	@$(CC) -O2 -std=gnu99 -w -I../src stmt_diff.c $(BUILD)/ral_shim.obj \
	  $(BUILD)/ctors.obj $(BUILD)/eci.obj $(BUILD)/delta_rules.obj \
	  $(BUILD)/delta_rules_enus.obj $(BUILD)/delta_consts_enus.obj $(BUILD)/delta_rules_shim.obj \
	  $(OUR_DELTA) $(SWAP_SWAP) \
	  $(filter-out $(ENUS)/eci.obj $(patsubst %,$(ENUS)/%.obj,$(DEAD)) $(patsubst %,$(ENUS)/%.obj,$(SWAP_OBJS)),\
	    $(wildcard $(ENUS)/*.obj)) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/link.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/link.err >&2 || true; exit $$s

.PHONY: speak-prim stmt-diff

# And the same again with a trace, so that where our primitives and the
# original's part company can be found the way the rules' were.
TSWAP_OBJS := $(sort $(RULE_OBJS) $(PRIM_OBJS) \
                $(shell cat ../src/trace_prim/objects 2>/dev/null))
TSWAP_SWAP := $(patsubst %,$(BUILD)/tswap/%.obj,$(TSWAP_OBJS))

trace-prim: $(BUILD)/trace_prim.exe

$(BUILD)/tswap/%.obj: $(ENUS)/%.obj ../src/delta_rules_enus.c ../src/prim/objects
	@mkdir -p $(BUILD)/tswap
	@cat ../src/rules/$*.ren ../src/prim/$*.ren \
	  ../src/trace_prim/$*.ren 2>/dev/null > $(BUILD)/tswap/$*.ren || true
	@g=""; [ -s ../src/rules/$*.glob ] \
	  && g="--globalize-symbols=../src/rules/$*.glob"; \
	  $(OBJCOPY) --redefine-syms=$(BUILD)/tswap/$*.ren $$g $< $@

# Our own primitives under a name of their own, so the wrapper in between
# can take the plain one and say what it was asked.
$(BUILD)/our_delta.obj: $(OUR_DELTA) ../src/prim/ours.ren
	@mkdir -p $(BUILD)
	@for f in $(OUR_DELTA); do \
	  $(CC) -O2 -std=c99 -w -I../src -c $$f \
	    -o $(BUILD)/our_$$(basename $$f .c).o; done
	@i686-w64-mingw32-ld -r -o $(BUILD)/our_delta.tmp.o \
	  $(patsubst ../src/%.c,$(BUILD)/our_%.o,$(OUR_DELTA))
	@$(OBJCOPY) --redefine-syms=../src/prim/ours.ren \
	  $(BUILD)/our_delta.tmp.o $@

$(BUILD)/trace_prim.exe: speak.c $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj \
    $(BUILD)/eci.obj $(BUILD)/delta_rules.obj $(BUILD)/delta_rules_enus.obj $(BUILD)/delta_consts_enus.obj \
    $(BUILD)/delta_rules_shim.obj $(BUILD)/delta_rules_trace_prim.obj \
    $(TSWAP_SWAP) $(BUILD)/our_delta.obj
	@$(CC) -O2 -std=gnu99 -w -I../src speak.c $(BUILD)/ral_shim.obj \
	  $(BUILD)/ctors.obj $(BUILD)/eci.obj $(BUILD)/delta_rules.obj \
	  $(BUILD)/delta_rules_enus.obj $(BUILD)/delta_consts_enus.obj $(BUILD)/delta_rules_shim.obj \
	  $(BUILD)/delta_rules_trace_prim.obj $(BUILD)/our_delta.obj \
	  $(TSWAP_SWAP) \
	  $(filter-out $(ENUS)/eci.obj $(patsubst %,$(ENUS)/%.obj,$(DEAD)) $(patsubst %,$(ENUS)/%.obj,$(TSWAP_OBJS)),\
	    $(wildcard $(ENUS)/*.obj)) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/link.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/link.err >&2 || true; exit $$s

.PHONY: trace-prim
