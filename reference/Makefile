# IBM's own engine, built from its own objects and driven through the
# published interface, so that there is something to listen against.
#
# This is all that is left of the differential harness. That harness stood our
# objects beside IBM's and compared them call for call, which is how the port
# was made; it is finished -- nothing is borrowed and `make missing' next door
# answers nothing -- and keeping it alive would have meant every object in the
# engine holding the shape a thirty-two bit compiler gave it in 1999. It is in
# the history, at the last commit where it built.
#
# What is left is the reference binary, which constrains nothing: it is IBM's
# code, linked as IBM linked it, run under Wine. `native/suite.sh' speaks each
# case through it and through ours and compares the samples.
#
# Two things the Microsoft toolchain did for it have to be done by hand. Its
# C++ compiler gave several helpers and objects names no C identifier can
# spell, so ours are declared under names that can be and the link is told to
# answer the real names with those. And two of the objects the static
# initialisers construct are file-static, so they are made visible first.

CC      := i686-w64-mingw32-gcc
OBJCOPY := i686-w64-mingw32-objcopy
ENUS    := ../analysis/enus
BUILD   := ../build

.PHONY: all clean
all: $(BUILD)/speak.exe

# The runtime abstraction layer, plus the one file that gives it a machine to
# run on, joined into a single object.
$(BUILD)/ral_shim.obj: ../src/port_ral.c ../src/port_win32.c ../src/evv_port.h
	@mkdir -p $(BUILD)
	@printf '_uni_strlen ?UniStrlen@@YAIPBG@Z\n_uni_strstr ?UniStrstr@@YAPAGPBG0@Z\n' > $(BUILD)/uni.syms
	@$(CC) -O2 -std=gnu99 -w -I../src -c ../src/port_ral.c -o $(BUILD)/ral_shim.tmp.o
	@$(CC) -O2 -std=gnu99 -w -I../src -c ../src/port_win32.c -o $(BUILD)/port.tmp.o
	@i686-w64-mingw32-ld -r $(BUILD)/ral_shim.tmp.o $(BUILD)/port.tmp.o \
	  -o $(BUILD)/ral_both.tmp.o
	@$(OBJCOPY) --redefine-syms=$(BUILD)/uni.syms $(BUILD)/ral_both.tmp.o $@

$(BUILD)/ctors.obj: ../src/port_ctors.c
	@mkdir -p $(BUILD)
	@$(CC) -O2 -std=gnu99 -w -I../src -c $< -o $@

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

clean:
	@rm -f $(BUILD)/speak.exe $(BUILD)/ral_shim.obj $(BUILD)/ctors.obj \
	       $(BUILD)/eci.obj $(BUILD)/*.tmp.o $(BUILD)/uni.syms
