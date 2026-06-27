# Flipper-app compatibility PoC.
#
#   make run      build + run the Flipper "Hello World" app on the desktop,
#                 drawn through esp32bios (ASCII, 128x64 = Flipper's screen)
#   make xtensa   cross-compile the same app + the shim for ESP32, and show the
#                 app's imports (the symbols a loader resolves against the host)
#   make clean
#
# The app (apps/hello_world.c) is standard Flipper source. The shim
# (furi_compat.cpp) maps the Flipper API onto BiosTable. Neither the app nor the
# BIOS knows about the other.

OUT      := build
CXXFLAGS := -std=c++17 -Wall -Iinclude -I../include
CCFLAGS  := -Wall -Iinclude

XTENSA := $(HOME)/.platformio/packages/toolchain-xtensa-esp32/bin
XCC    := $(XTENSA)/xtensa-esp32-elf-gcc
XPP    := $(XTENSA)/xtensa-esp32-elf-g++
XR     := $(XTENSA)/xtensa-esp32-elf-readelf

# Flags that make the app loadable by the on-device ELF loader (see ../INFO.md).
XAPPFLAGS := -c -Os -mlongcalls -mtext-section-literals -fno-common -Iinclude

run: $(OUT)/fap_demo
	./$(OUT)/fap_demo

# Run a Flipper IR app over a mock IR backend (exercises the peripheral stack).
ir: $(OUT)/ir_demo
	./$(OUT)/ir_demo

$(OUT)/fap_demo: apps/hello_world.c furi_compat.cpp host_native_fap.cpp | $(OUT)
	cc  -c $(CCFLAGS)  apps/hello_world.c    -o $(OUT)/hello_world.o      # app as C
	g++ -c $(CXXFLAGS) furi_compat.cpp       -o $(OUT)/furi_compat.o      # shim as C++
	g++ -c $(CXXFLAGS) host_native_fap.cpp   -o $(OUT)/host.o
	g++ $(OUT)/hello_world.o $(OUT)/furi_compat.o $(OUT)/host.o -o $@

$(OUT)/ir_demo: apps/ir_blast.c furi_compat.cpp test_ir.cpp | $(OUT)
	cc  -c $(CCFLAGS)  apps/ir_blast.c -o $(OUT)/ir_blast.o
	g++ -c $(CXXFLAGS) furi_compat.cpp -o $(OUT)/furi_compat.o
	g++ $(CXXFLAGS) test_ir.cpp $(OUT)/ir_blast.o $(OUT)/furi_compat.o -o $@

xtensa: | $(OUT)
	@test -x $(XCC) || { echo "Xtensa toolchain not found; run 'pio run -e esp32-serial' once."; exit 1; }
	$(XCC) $(XAPPFLAGS) apps/hello_world.c -o $(OUT)/hello_world.elf
	$(XCC) $(XAPPFLAGS) apps/ir_blast.c    -o $(OUT)/ir_blast.elf
	$(XPP) -c -Os -mlongcalls -fno-rtti -fno-exceptions -Iinclude -I../include \
	    furi_compat.cpp -o $(OUT)/furi_compat_xtensa.o
	@echo "app + shim cross-compiled for ESP32."
	@echo "app imports (resolved against the host shim at load):"
	@$(XR) -s $(OUT)/hello_world.elf | awk '/UND/ && /furi|gui|canvas|view_port/ {print "  " $$8}' | sort -u

# Verify the real on-device pipeline: load a Xtensa Flipper app through the ELF
# loader, resolving its imports against the shim. (Loads/relocates/resolves on the
# desktop; executing the Xtensa code needs the chip.)
load: xtensa
	g++ -std=c++17 -Iinclude -I../include test_fap_load.cpp ../src/elfload.cpp furi_compat.cpp -o $(OUT)/test_fap_load
	./$(OUT)/test_fap_load $(OUT)/hello_world.elf hello_world_app
	@echo
	./$(OUT)/test_fap_load $(OUT)/ir_blast.elf ir_blast_app

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT)

.PHONY: run ir xtensa load clean
