# Native (host) build for tests/*.c - see CLAUDE.md's "Native (host)
# tests" section for what these prove and how they fit into the project.
#
# Power of 10, Rule 10: our own hand-written sources (this directory's
# *_test.c files, firmware/emu41gcc_compat/{nut_stubs,nut_globals,
# nut_rom}.c, firmware/hp41_*.c) are compiled with -Wall -Wextra
# -Wpedantic -Werror. Vendored/generated sources (emu41gcc/*.c,
# roms/rom_images.c, font-tables/hp41_display_tables.c) are compiled
# with only the flags they functionally need (-fcommon, or the
# nut_stubs.h force-include nutcpu.c needs) - see CLAUDE.md's "Does NOT
# apply to" list and firmware/CMakeLists.txt, whose per-file flag
# scoping this mirrors exactly. Splitting this per source file (rather
# than one flag set for the whole build) is the actual point: strict
# warnings must never leak onto code we don't own and can't fix.
#
# Usage:
#   make -C tests            # build every test binary into tests/build/
#   make -C tests run        # build, then run every test, stop on first failure
#   make -C tests clean

CC ?= cc
ROOT := ..
BUILD := build

STD := -std=gnu11
STRICT := -Wall -Wextra -Wpedantic -Werror
INC := -I$(ROOT)/emu41gcc -I$(ROOT)/firmware/emu41gcc_compat -I$(ROOT)/firmware -I$(ROOT)/font-tables
NUT_STUBS_INC := -include $(ROOT)/firmware/emu41gcc_compat/nut_stubs.h

TESTS := nut_smoke_test display_bridge_test key_bridge_test key_hold_test hold_trace_test persist_state_test

.PHONY: all run clean
all: $(addprefix $(BUILD)/,$(TESTS))

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

# --- Our own compat shims and bridge files: strict warnings apply.
# -fcommon is still needed on a few of these (see firmware/CMakeLists.txt's
# comment on tabpage[]/tabbank[] common-symbol linking) but that's a
# functional requirement, not a warnings exemption. ---

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

$(BUILD)/hp41_persist_state.o: $(ROOT)/firmware/hp41_persist_state.c | $(BUILD)
	$(CC) $(STD) -fcommon $(STRICT) $(INC) -c -o $@ $<

# --- Our own test files: strict warnings apply. ---

$(BUILD)/%_test.o: %_test.c | $(BUILD)
	$(CC) $(STD) $(STRICT) $(INC) -c -o $@ $<

# --- Link recipes: one per binary, listing exactly the objects each
# test actually needs. ---

$(BUILD)/nut_smoke_test: $(BUILD)/nut_smoke_test.o $(BUILD)/nutcpu.o $(BUILD)/display.o \
                          $(BUILD)/nut_stubs.o $(BUILD)/nut_globals.o $(BUILD)/nut_rom.o \
                          $(BUILD)/rom_images.o
	$(CC) -o $@ $^

$(BUILD)/display_bridge_test: $(BUILD)/display_bridge_test.o $(BUILD)/nutcpu.o $(BUILD)/display.o \
                               $(BUILD)/nut_stubs.o $(BUILD)/nut_globals.o $(BUILD)/nut_rom.o \
                               $(BUILD)/hp41_display_bridge.o $(BUILD)/rom_images.o \
                               $(BUILD)/hp41_display_tables.o
	$(CC) -o $@ $^

$(BUILD)/key_bridge_test: $(BUILD)/key_bridge_test.o $(BUILD)/nut_globals.o \
                          $(BUILD)/hp41_key_bridge.o $(BUILD)/hp41_key_hold_bridge.o
	$(CC) -o $@ $^

$(BUILD)/key_hold_test: $(BUILD)/key_hold_test.o $(BUILD)/nut_globals.o \
                         $(BUILD)/hp41_key_bridge.o $(BUILD)/hp41_key_hold_bridge.o
	$(CC) -o $@ $^

$(BUILD)/hold_trace_test: $(BUILD)/hold_trace_test.o $(BUILD)/nutcpu.o $(BUILD)/display.o \
                          $(BUILD)/nut_stubs.o $(BUILD)/nut_globals.o $(BUILD)/nut_rom.o \
                          $(BUILD)/hp41_key_bridge.o $(BUILD)/hp41_key_hold_bridge.o \
                          $(BUILD)/rom_images.o
	$(CC) -o $@ $^

$(BUILD)/persist_state_test: $(BUILD)/persist_state_test.o $(BUILD)/nut_globals.o \
                              $(BUILD)/hp41_persist_state.o
	$(CC) -o $@ $^

run: all
	@for t in $(TESTS); do \
		echo "=== $$t ==="; \
		./$(BUILD)/$$t || exit 1; \
		echo; \
	done

clean:
	rm -rf $(BUILD)
