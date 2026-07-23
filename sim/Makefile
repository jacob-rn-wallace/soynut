# Host-native "virtual Pico 2 + LCD" simulator - see CLAUDE.md's
# "Host-native simulator" section for what this is and how it fits.
#
# Modeled directly on tests/Makefile: plain host cc, no pico-sdk. This
# project's own sources (this directory's sim_*.c files, plus the exact
# same firmware/hp41_*.c and firmware/emu41gcc_compat/*.c compat shims
# tests/Makefile already builds natively) get -Wall -Wextra -Wpedantic
# -Werror -UNDEBUG; vendored/generated sources (emu41gcc/*.c,
# roms/rom_images.c, font-tables/*.c) get only the flags they
# functionally need - see tests/Makefile's identical comment for why
# this split matters: strict warnings must never leak onto code we
# don't own and can't fix.
#
# Requires SDL2 (macOS: brew install sdl2) - resolved via pkg-config,
# scoped only to this project's own sim_*.c objects and the final link;
# nothing else in this build needs it.
#
# Requires roms/rom_images.c to already exist (gitignored, user-
# generated - see roms/README.md) - same requirement tests/ and
# firmware/ already have, no new ROM-handling logic here.
#
# Usage:
#   make -C sim           # build sim/build/soynut_sim
#   make -C sim run       # build, then run it (from sim/, so its
#                          # continuous-memory file lands at
#                          # sim/soynut_sim_persist.bin)
#   make -C sim run-gui   # build, run it, AND launch
#                          # tools/hp41_keyboard_gui.py already attached
#                          # to its virtual serial port (run_with_gui.sh)
#   make -C sim clean

CC ?= cc
ROOT := ..
BUILD := build

STD := -std=gnu11
STRICT := -Wall -Wextra -Wpedantic -Werror -UNDEBUG
INC := -I$(ROOT)/emu41gcc -I$(ROOT)/firmware/emu41gcc_compat -I$(ROOT)/firmware -I$(ROOT)/font-tables
NUT_STUBS_INC := -include $(ROOT)/firmware/emu41gcc_compat/nut_stubs.h

SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)

.PHONY: all run run-gui clean
all: $(BUILD)/soynut_sim

$(BUILD):
	mkdir -p $(BUILD)

# --- Vendored (emu41gcc) and generated (roms/, font-tables/) sources:
# not ours, compiled leniently - only the flags actually required to
# build them at all. ---

$(BUILD)/nutcpu.o: $(ROOT)/emu41gcc/nutcpu.c | $(BUILD)
	$(CC) $(STD) -fcommon $(INC) $(NUT_STUBS_INC) -c -o $@ $<

$(BUILD)/display.o: $(ROOT)/emu41gcc/display.c | $(BUILD)
	$(CC) $(STD) -fcommon $(INC) -c -o $@ $<

$(BUILD)/rom_images.o: $(ROOT)/roms/rom_images.c | $(BUILD)
	$(CC) $(STD) $(INC) -c -o $@ $<

$(BUILD)/hp41_display_tables.o: $(ROOT)/font-tables/hp41_display_tables.c | $(BUILD)
	$(CC) $(STD) $(INC) -c -o $@ $<

$(BUILD)/hp41_elite_font_table.o: $(ROOT)/font-tables/hp41_elite_font_table.c | $(BUILD)
	$(CC) $(STD) $(INC) -c -o $@ $<

# --- Reused firmware/ compat shims and pure-logic bridge files -
# unmodified from firmware/, same objects tests/Makefile already builds
# natively. Strict warnings apply; -fcommon is a functional requirement
# on several of these (tabpage[]/tabbank[] common-symbol linking - see
# firmware/CMakeLists.txt's comment), not a warnings exemption. ---

$(BUILD)/nut_stubs.o: $(ROOT)/firmware/emu41gcc_compat/nut_stubs.c | $(BUILD)
	$(CC) $(STD) $(STRICT) $(INC) -c -o $@ $<

$(BUILD)/nut_globals.o: $(ROOT)/firmware/emu41gcc_compat/nut_globals.c | $(BUILD)
	$(CC) $(STD) -fcommon $(STRICT) $(INC) -c -o $@ $<

$(BUILD)/nut_rom.o: $(ROOT)/firmware/emu41gcc_compat/nut_rom.c | $(BUILD)
	$(CC) $(STD) -fcommon $(STRICT) $(INC) -c -o $@ $<

$(BUILD)/hp41_key_bridge.o: $(ROOT)/firmware/hp41_key_bridge.c | $(BUILD)
	$(CC) $(STD) -fcommon $(STRICT) $(INC) -c -o $@ $<

$(BUILD)/hp41_key_hold_bridge.o: $(ROOT)/firmware/hp41_key_hold_bridge.c | $(BUILD)
	$(CC) $(STD) -fcommon $(STRICT) $(INC) -c -o $@ $<

$(BUILD)/hp41_display_bridge.o: $(ROOT)/firmware/hp41_display_bridge.c | $(BUILD)
	$(CC) $(STD) $(STRICT) $(INC) -c -o $@ $<

$(BUILD)/hp41_elite_display_bridge.o: $(ROOT)/firmware/hp41_elite_display_bridge.c | $(BUILD)
	$(CC) $(STD) -fcommon $(STRICT) $(INC) -c -o $@ $<

$(BUILD)/hp41_persist_state.o: $(ROOT)/firmware/hp41_persist_state.c | $(BUILD)
	$(CC) $(STD) -fcommon $(STRICT) $(INC) -c -o $@ $<

# --- This directory's own sources: strict warnings + SDL2 where needed.
# sim_main.o needs -fcommon for the same reason firmware/main.c does
# (see firmware/CMakeLists.txt) - both include nutcpu.h. ---

$(BUILD)/sim_main.o: sim_main.c | $(BUILD)
	$(CC) $(STD) -fcommon $(STRICT) $(INC) $(SDL_CFLAGS) -c -o $@ $<

$(BUILD)/sim_display.o: sim_display.c | $(BUILD)
	$(CC) $(STD) $(STRICT) $(INC) $(SDL_CFLAGS) -c -o $@ $<

$(BUILD)/sim_persist_file.o: sim_persist_file.c | $(BUILD)
	$(CC) $(STD) $(STRICT) $(INC) -c -o $@ $<

$(BUILD)/sim_keyboard.o: sim_keyboard.c | $(BUILD)
	$(CC) $(STD) $(STRICT) $(INC) $(SDL_CFLAGS) -c -o $@ $<

$(BUILD)/sim_clock.o: sim_clock.c | $(BUILD)
	$(CC) $(STD) $(STRICT) $(INC) $(SDL_CFLAGS) -c -o $@ $<

$(BUILD)/sim_pty.o: sim_pty.c | $(BUILD)
	$(CC) $(STD) $(STRICT) $(INC) -c -o $@ $<

# --- Link: everything above, plus SDL2's libs. ---

$(BUILD)/soynut_sim: $(BUILD)/sim_main.o $(BUILD)/sim_display.o $(BUILD)/sim_persist_file.o \
                      $(BUILD)/sim_keyboard.o $(BUILD)/sim_clock.o $(BUILD)/sim_pty.o \
                      $(BUILD)/nutcpu.o $(BUILD)/display.o \
                      $(BUILD)/nut_stubs.o $(BUILD)/nut_globals.o $(BUILD)/nut_rom.o \
                      $(BUILD)/hp41_key_bridge.o $(BUILD)/hp41_key_hold_bridge.o \
                      $(BUILD)/hp41_display_bridge.o $(BUILD)/hp41_elite_display_bridge.o \
                      $(BUILD)/hp41_persist_state.o \
                      $(BUILD)/rom_images.o $(BUILD)/hp41_display_tables.o $(BUILD)/hp41_elite_font_table.o
	$(CC) -o $@ $^ $(SDL_LIBS)

run: all
	./$(BUILD)/soynut_sim

run-gui: all
	bash run_with_gui.sh

clean:
	rm -rf $(BUILD)
