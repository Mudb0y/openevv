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
  $(ENUS)/filio.obj $(ENUS)/win_filename.obj

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
	  $(filter-out $(ENUS)/eci.obj $(patsubst %,$(ENUS)/%.obj,$(PROBE_OBJS)),\
	    $(wildcard $(ENUS)/*.obj)) \
	  $(MINGW_LDFLAGS) -lwinmm -o $@ 2> $(BUILD)/link.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/link.err >&2 || true; exit $$s

.PHONY: speak speak-probe
