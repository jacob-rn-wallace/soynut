# Native (host) build for tools/*.c - diagnostic utilities (see each
# file's own header comment for what it does).
#
# Power of 10, Rule 10: this project's own sources (nut_disasm.c,
# powoff_trace.c, and the shared emu41gcc_compat/*.c shims) are compiled
# with -Wall -Wextra -Wpedantic -Werror; vendored (emu41gcc/*.c) and
# generated (roms/rom_images.c) sources are compiled leniently - see
# tests/Makefile, which this mirrors exactly.
#
# Usage:
#   make -C tools            # build both tools into tools/build/
#   make -C tools clean

CC ?= cc
ROOT := ..
BUILD := build

STD := -std=gnu11
STRICT := -Wall -Wextra -Wpedantic -Werror
INC := -I$(ROOT)/emu41gcc -I$(ROOT)/firmware/emu41gcc_compat -I$(ROOT)/firmware
NUT_STUBS_INC := -include $(ROOT)/firmware/emu41gcc_compat/nut_stubs.h
DESAS_PROTO_INC := -include desas_proto.h

.PHONY: all clean
all: $(BUILD)/nut_disasm $(BUILD)/powoff_trace

$(BUILD):
	mkdir -p $(BUILD)

# --- Vendored/generated sources: lenient. ---

$(BUILD)/desas41.o: $(ROOT)/emu41gcc/desas41.c | $(BUILD)
	$(CC) $(STD) $(DESAS_PROTO_INC) -c -o $@ $<

$(BUILD)/nutcpu.o: $(ROOT)/emu41gcc/nutcpu.c | $(BUILD)
	$(CC) $(STD) -fcommon $(INC) $(NUT_STUBS_INC) -c -o $@ $<

$(BUILD)/display.o: $(ROOT)/emu41gcc/display.c | $(BUILD)
	$(CC) $(STD) -fcommon $(INC) -c -o $@ $<

$(BUILD)/rom_images.o: $(ROOT)/roms/rom_images.c | $(BUILD)
	$(CC) $(STD) $(INC) -c -o $@ $<

# --- Our own compat shims: strict. ---

$(BUILD)/nut_stubs.o: $(ROOT)/firmware/emu41gcc_compat/nut_stubs.c | $(BUILD)
	$(CC) $(STD) $(STRICT) $(INC) -c -o $@ $<

$(BUILD)/nut_globals.o: $(ROOT)/firmware/emu41gcc_compat/nut_globals.c | $(BUILD)
	$(CC) $(STD) -fcommon $(STRICT) $(INC) -c -o $@ $<

$(BUILD)/nut_rom.o: $(ROOT)/firmware/emu41gcc_compat/nut_rom.c | $(BUILD)
	$(CC) $(STD) -fcommon $(STRICT) $(INC) -c -o $@ $<

# --- Our own tool sources: strict. ---

$(BUILD)/nut_disasm.o: nut_disasm.c | $(BUILD)
	$(CC) $(STD) $(STRICT) $(DESAS_PROTO_INC) -c -o $@ $<

$(BUILD)/powoff_trace.o: powoff_trace.c | $(BUILD)
	$(CC) $(STD) $(STRICT) $(INC) -c -o $@ $<

# --- Link recipes. ---

$(BUILD)/nut_disasm: $(BUILD)/nut_disasm.o $(BUILD)/desas41.o $(BUILD)/rom_images.o
	$(CC) -o $@ $^

$(BUILD)/powoff_trace: $(BUILD)/powoff_trace.o $(BUILD)/nutcpu.o $(BUILD)/display.o \
                       $(BUILD)/nut_stubs.o $(BUILD)/nut_globals.o $(BUILD)/nut_rom.o \
                       $(BUILD)/rom_images.o
	$(CC) -o $@ $^

clean:
	rm -rf $(BUILD)
