# Qt6/CMake developer entry points.
# The legacy qmake-generated Makefile is intentionally not used.

BUILD_DIR ?= build
CMAKE ?= cmake
PYTHON ?= python3

.PHONY: all configure build check clean

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --target vacuumu -j1

check: configure
	$(CMAKE) --build $(BUILD_DIR) --target chatmessagehandler recentcontacts -j1
	$(PYTHON) tests/matrix_protocol_smoke.py
	git diff --check

clean:
	$(CMAKE) --build $(BUILD_DIR) --target clean
