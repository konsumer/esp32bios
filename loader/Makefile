# Separate-binary loader demo (desktop).
#
#   make run      build host + app as INDEPENDENT binaries, then run 90 frames
#   make          just build them
#   make clean
#
# The whole point: host and app are compiled and linked separately. They share
# only include/bios.h + include/bios_vector.h. The app never names the host; the
# host never names the app.

CXX      ?= g++
CXXFLAGS := -std=c++17 -I../include -Wall
OUT      := build

# Host is built with -rdynamic so the loaded app can resolve bios_vector_discover().
$(OUT)/host_loader: host_loader.cpp ../src/bios_vector.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) -rdynamic $^ -ldl -o $@

# App is a standalone shared object. Note: it does NOT compile bios_vector.cpp --
# that symbol stays unresolved and is bound to the host at load time.
$(OUT)/app.so: guest_app.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) -shared -fPIC $< -o $@

$(OUT):
	mkdir -p $(OUT)

run: $(OUT)/host_loader $(OUT)/app.so
	./$(OUT)/host_loader $(OUT)/app.so 90

clean:
	rm -rf $(OUT)

.PHONY: run clean
