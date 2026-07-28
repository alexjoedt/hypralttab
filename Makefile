BUILD_DIR ?= build-cmake
BUILD_TYPE ?= Release
JOBS ?= $(shell nproc 2>/dev/null || echo 4)
PLUGIN := $(BUILD_DIR)/libhypr-alt-tab.so

CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

.PHONY: all configure build test clean distclean install reload unload load help

all: build

configure: $(BUILD_DIR)/CMakeCache.txt

$(BUILD_DIR)/CMakeCache.txt:
	cmake $(CMAKE_FLAGS) -B $(BUILD_DIR)

build: configure
	cmake --build $(BUILD_DIR) -j$(JOBS)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	cmake --build $(BUILD_DIR) --target clean 2>/dev/null || true

distclean:
	cmake -E rm -rf $(BUILD_DIR)

install: build
	cmake --install $(BUILD_DIR)

unload:
	hyprctl plugin unload "$(CURDIR)/$(PLUGIN)" || true

load: build
	hyprctl plugin load "$(CURDIR)/$(PLUGIN)"

reload: unload load

help:
	@echo "targets:"
	@echo "  all / build  configure + build plugin ($(PLUGIN))"
	@echo "  configure    cmake -B $(BUILD_DIR) ($(BUILD_TYPE))"
	@echo "  test         build + ctest"
	@echo "  clean        remove build artifacts"
	@echo "  distclean    remove $(BUILD_DIR)"
	@echo "  install      cmake --install"
	@echo "  load         hyprctl plugin load"
	@echo "  unload       hyprctl plugin unload"
	@echo "  reload       unload then load"
	@echo ""
	@echo "vars: BUILD_DIR=$(BUILD_DIR) BUILD_TYPE=$(BUILD_TYPE) JOBS=$(JOBS)"
