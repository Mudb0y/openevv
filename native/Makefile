# The engine, built for the machine it is running on.
#
# `make speak' builds a driver against the archive and `suite.sh' sets what
# comes out against IBM's own binary under Wine, case for case. Nothing is
# borrowed: `make missing' answers nothing, and it is worth re-running
# whenever a source is added, since a name that reappears there is a call
# that has quietly gone back to the original.
#
# Two builds, and both have to speak. `speak' is thirty-two bit and `speak64'
# is not; the second is the one that matters, and the first is kept because a
# difference between them is a layout mistake caught early.

SRC   := ../src
BUILD := ../build/native
CC    := i686-unknown-linux-gnu-gcc
NM    := i686-unknown-linux-gnu-nm
CFLAGS := -O2 -std=gnu99 -w -I$(SRC)

# Everything of ours except the Windows porting layer, which belongs to the
# reference build next door.
SOURCES := $(filter-out $(SRC)/port_win32.c,$(wildcard $(SRC)/*.c))

OBJECTS := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(SOURCES))

.PHONY: all missing clean speak
all: $(BUILD)/libevv.a

# The engine driven on this machine. Nothing is linked beside it, so what
# comes out is ours from end to end.
speak: $(BUILD)/speak

$(BUILD)/speak: speak.c $(BUILD)/libevv.a
	@$(CC) $(CFLAGS) speak.c $(BUILD)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

$(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(BUILD)
	@$(CC) $(CFLAGS) -c $< -o $@

# A source that gets renamed leaves its object behind, and a stale one is
# a duplicate definition waiting to happen, so they go before the archive
# is built rather than at the next link.
$(BUILD)/libevv.a: $(OBJECTS)
	@for o in $(BUILD)/*.o; do \
	   case " $(OBJECTS) " in *" $$o "*) ;; *) rm -f "$$o" ;; esac; \
	 done
	@rm -f $@
	@ar rcs $@ $(OBJECTS)
	@echo "built $@ from $(words $(OBJECTS)) objects"

# What our code asks for and nothing of ours answers. The C library's own
# names are dropped, since those come from the system; what is left is the
# original's, and writing it is the rest of the port.
missing: $(OBJECTS)
	@$(NM) $(OBJECTS) > $(BUILD)/syms.txt
	@python3 missing.py $(BUILD)/syms.txt

clean:
	@rm -rf $(BUILD)

# The same engine built sixty-four bit. It needs the arena: the machine keeps
# addresses in 32-bit values, so everything one can point at has to live
# somewhere a 32-bit value can name.
CC64      := gcc
BUILD64   := ../build/native64
#
# -no-pie is not a preference. It puts the program at four megabytes rather
# than wherever the loader fancies, which is what lets the language's own
# tables be named in a value without being copied anywhere first.
CFLAGS64  := -O1 -std=gnu99 -I$(SRC) -DEVV_ARENA=1 -no-pie \
             -Werror=int-conversion \
             -Werror=incompatible-pointer-types -Wno-implicit-function-declaration
OBJECTS64 := $(patsubst $(SRC)/%.c,$(BUILD64)/%.o,$(SOURCES))

.PHONY: speak64
speak64: $(BUILD64)/speak

$(BUILD64)/speak: speak.c $(BUILD64)/libevv.a
	@$(CC64) $(CFLAGS64) speak.c $(BUILD64)/libevv.a -lpthread -lm -o $@
	@echo "built $@"

$(BUILD64)/%.o: $(SRC)/%.c
	@mkdir -p $(BUILD64)
	@$(CC64) $(CFLAGS64) -c $< -o $@

$(BUILD64)/libevv.a: $(OBJECTS64)
	@rm -f $@
	@ar rcs $@ $(OBJECTS64)
	@echo "built $@ from $(words $(OBJECTS64)) objects"
