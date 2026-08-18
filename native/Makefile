# The engine built for the machine it is running on, rather than linked
# beside the original under Wine.
#
# Thirty-two bit, and it has to be: the machine keeps every value in an
# int32 and puts host addresses in some of them, so a sixty-four bit build
# needs the arena work before it can be anything but a crash.
#
# Nothing here speaks yet. What our own sources need from the original is
# still a long list, and `make missing' writes it out: that list is the
# work left, and it shrinks as objects are transcribed.

SRC   := ../src
BUILD := ../build/native
CC    := i686-unknown-linux-gnu-gcc
NM    := i686-unknown-linux-gnu-nm
CFLAGS := -O2 -std=gnu99 -w -I$(SRC)

# Everything of ours except the two that belong to another target: the
# Windows porting layer, and the traced copy of the rules the differential
# build uses to follow the original step for step.
SOURCES := $(filter-out $(SRC)/port_win32.c $(SRC)/delta_rules_trace.c \
                        $(SRC)/delta_rules_trace_prim.c $(SRC)/delta_rules_ref.c, \
             $(wildcard $(SRC)/*.c))

# port_files.c answers two things the differential build gets from the
# platform it is linked beside, so it belongs to this target alone.
OBJECTS := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(SOURCES))

.PHONY: all missing clean
all: $(BUILD)/libevv.a

$(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(BUILD)
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libevv.a: $(OBJECTS)
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
