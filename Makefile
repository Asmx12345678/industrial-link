CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread
CPPFLAGS ?= -Igateway/include
LDFLAGS ?=

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  DL_LIBS :=
  PLUGIN_FLAGS := -dynamiclib
  PLUGIN_EXT := dylib
  # Some beta/newer Command Line Tools ship a .tbd format unsupported by the
  # active linker. Prefer the stable SDK when it is present; callers may
  # override MACOS_SDK explicitly.
  MACOS_SDK ?= $(firstword $(wildcard /Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk))
  ifneq ($(MACOS_SDK),)
    CXXFLAGS += -isysroot $(MACOS_SDK)
  endif
else ifeq ($(OS),Windows_NT)
  DL_LIBS :=
  PLUGIN_FLAGS := -shared
  PLUGIN_EXT := dll
else
  DL_LIBS := -ldl
  PLUGIN_FLAGS := -shared -fPIC
  PLUGIN_EXT := so
endif

GATEWAY_SOURCES := gateway/src/main.cpp gateway/src/gateway.cpp gateway/src/plugin_manager.cpp gateway/src/storage.cpp
GATEWAY_BINARY := build/industrial-gateway
PLUGIN_BINARY := build/libvendor_pipe.$(PLUGIN_EXT)

.PHONY: all clean test demo benchmark plugin

all: $(GATEWAY_BINARY) $(PLUGIN_BINARY)

$(GATEWAY_BINARY): $(GATEWAY_SOURCES) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(GATEWAY_SOURCES) $(LDFLAGS) $(DL_LIBS) -lsqlite3 -o $@

$(PLUGIN_BINARY): plugins/sample_adapter.cpp gateway/include/industrial_link/plugin_api.h | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(PLUGIN_FLAGS) plugins/sample_adapter.cpp -o $@

build:
	mkdir -p build

test: all
	python3 -m unittest discover -s tests -p 'test_*.py' -v

demo: all
	./scripts/run_demo.sh

benchmark: all
	python3 scripts/benchmark.py

clean:
	rm -rf build data
