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
  $(ENUS)/link.obj $(ENUS)/capp.obj $(ENUS)/win_etistr.obj \
  $(ENUS)/filio.obj $(ENUS)/win_filename.obj

IBMDELTA := $(wildcard ../analysis/delta-ibm/*.obj)
# Support objects the runtime needs but that define no primitive we shadow,
# so they link unprefixed.
DELTA_SUPPORT := $(ENUS)/win_dynabuf.obj $(ENUS)/logio.obj $(ENUS)/win_hash.obj \
  $(ENUS)/link.obj $(ENUS)/capp.obj $(ENUS)/win_etistr.obj \
  $(ENUS)/filio.obj $(ENUS)/win_filename.obj

delta: $(BUILD)/delta_probe.exe $(BUILD)/delta_diff.exe

$(BUILD)/delta_diff.exe: delta_diff.c delta_stubs.c ../src/delta.c ../src/delta_heap.c ../src/delta_sysmem.c ../src/delta.h
	@mkdir -p $(BUILD)
	@$(CC) $(CFLAGS) delta_diff.c delta_stubs.c ../src/delta.c ../src/delta_heap.c ../src/delta_sysmem.c $(IBMDELTA) $(DELTA_SUPPORT) $(MINGW_LDFLAGS) -o $@ 2> $(BUILD)/link.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/link.err >&2 || true; exit $$s

$(BUILD)/delta_probe.exe: delta_probe.c delta_stubs.c
	@mkdir -p $(BUILD)
	@$(CC) $(CFLAGS) delta_probe.c delta_stubs.c $(DELTA_OBJS) $(MINGW_LDFLAGS) -o $@ 2> $(BUILD)/link.err; \
	  s=$$?; grep -v "corrupt .drectve" $(BUILD)/link.err >&2 || true; exit $$s

delta-run: delta
	wine $(BUILD)/delta_probe.exe
	wine $(BUILD)/delta_diff.exe
