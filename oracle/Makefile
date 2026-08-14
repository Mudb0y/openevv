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

.PHONY: all run clean
