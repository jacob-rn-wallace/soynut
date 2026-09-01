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
#   make -C tools            # build all tools into tools/build/
#   make -C tools clean

CC ?= cc
ROOT := ..
BUILD := build

STD := -std=gnu11
STRICT := -Wall -Wextra -Wpedantic -Werror
INC := -I$(ROOT)/emu41gcc -I$(ROOT)/firmware/emu41gcc_compat -I$(ROOT)/firmware -I$(ROOT)/ilvideo-native/core
NUT_STUBS_INC := -include $(ROOT)/firmware/emu41gcc_compat/nut_stubs.h
DESAS_PROTO_INC := -include desas_proto.h

# hpil_wr()/hpil_rd() are real now (firmware/hp41_hpil_controller.c),
# not the old no-op stubs - any tool that links nutcpu.o (which calls
# them from execp()/exec0()) needs this whole chain, whether or not
# that tool itself exercises any HP-IL opcode - see tests/Makefile's
# identical HPIL_OBJS.
HPIL_OBJS := $(BUILD)/hp41_hpil_controller.o $(BUILD)/hp41_hpil_video_bridge.o \
             $(BUILD)/hpil_device.o $(BUILD)/ilvideo_term.o $(BUILD)/ilvideo_device.o

.PHONY: all clean
all: $(BUILD)/nut_disasm $(BUILD)/powoff_trace $(BUILD)/flag_array_trace $(BUILD)/hpil_opcode_trace

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

$(BUILD)/hp41_hpil_controller.o: $(ROOT)/firmware/hp41_hpil_controller.c | $(BUILD)
	$(CC) $(STD) $(STRICT) $(INC) -c -o $@ $<

$(BUILD)/hp41_hpil_video_bridge.o: $(ROOT)/firmware/hp41_hpil_video_bridge.c | $(BUILD)
	$(CC) $(STD) $(STRICT) $(INC) -c -o $@ $<

# --- ilvideo-native (git submodule, ../ilvideo-native/ - a separate
# repo, see its own README) core/: not ours, compiled leniently, same
# reasoning as the vendored/generated sources above. ---

$(BUILD)/hpil_device.o: $(ROOT)/ilvideo-native/core/hpil_device.c | $(BUILD)
	$(CC) $(STD) $(INC) -c -o $@ $<

$(BUILD)/ilvideo_term.o: $(ROOT)/ilvideo-native/core/ilvideo_term.c | $(BUILD)
	$(CC) $(STD) $(INC) -c -o $@ $<

$(BUILD)/ilvideo_device.o: $(ROOT)/ilvideo-native/core/ilvideo_device.c | $(BUILD)
	$(CC) $(STD) $(INC) -c -o $@ $<

# --- Our own tool sources: strict. ---

$(BUILD)/nut_disasm.o: nut_disasm.c | $(BUILD)
	$(CC) $(STD) $(STRICT) $(DESAS_PROTO_INC) -c -o $@ $<

$(BUILD)/powoff_trace.o: powoff_trace.c | $(BUILD)
	$(CC) $(STD) $(STRICT) $(INC) -c -o $@ $<

$(BUILD)/flag_array_trace.o: flag_array_trace.c | $(BUILD)
	$(CC) $(STD) $(STRICT) $(INC) -c -o $@ $<

$(BUILD)/hpil_opcode_trace.o: hpil_opcode_trace.c | $(BUILD)
	$(CC) $(STD) $(STRICT) $(INC) -c -o $@ $<

# --- Link recipes. ---

$(BUILD)/nut_disasm: $(BUILD)/nut_disasm.o $(BUILD)/desas41.o $(BUILD)/rom_images.o
	$(CC) -o $@ $^

$(BUILD)/powoff_trace: $(BUILD)/powoff_trace.o $(BUILD)/nutcpu.o $(BUILD)/display.o \
                       $(BUILD)/nut_stubs.o $(BUILD)/nut_globals.o $(BUILD)/nut_rom.o \
                       $(BUILD)/rom_images.o $(HPIL_OBJS)
	$(CC) -o $@ $^

$(BUILD)/flag_array_trace: $(BUILD)/flag_array_trace.o $(BUILD)/nutcpu.o $(BUILD)/display.o \
                       $(BUILD)/nut_stubs.o $(BUILD)/nut_globals.o $(BUILD)/nut_rom.o \
                       $(BUILD)/rom_images.o $(HPIL_OBJS)
	$(CC) -o $@ $^

$(BUILD)/hpil_opcode_trace: $(BUILD)/hpil_opcode_trace.o $(BUILD)/nutcpu.o $(BUILD)/display.o \
                       $(BUILD)/nut_stubs.o $(BUILD)/nut_globals.o $(BUILD)/nut_rom.o \
                       $(BUILD)/rom_images.o $(HPIL_OBJS)
	$(CC) -o $@ $^

clean:
	rm -rf $(BUILD)
