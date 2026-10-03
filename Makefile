# Qt6/CMake developer entry points.
# The legacy qmake-generated Makefile is intentionally not used.

BUILD_DIR ?= build-nix
CMAKE ?= cmake
PYTHON ?= python3
GIT ?= git

.PHONY: all configure build check clean

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DVACUUM_PYTHON_EXECUTABLE="$(PYTHON)" -DVACUUM_GIT_EXECUTABLE="$(GIT)"

build: configure
	$(CMAKE) --build $(BUILD_DIR) --target vacuumu -j1

check: configure
	$(CMAKE) --build $(BUILD_DIR) --target check --parallel 1

clean:
	$(CMAKE) --build $(BUILD_DIR) --target clean
