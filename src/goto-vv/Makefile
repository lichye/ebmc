BUILD ?= build
.PHONY: all clean
all:
	cmake -S . -B "$(BUILD)" $(if $(SLANG_SOURCE),-DSLANG_SOURCE="$(abspath $(SLANG_SOURCE))") $(CMAKE_ARGS)
	+cmake --build "$(BUILD)" --target goto-vv
clean:
	cmake -E rm -rf "$(BUILD)"
