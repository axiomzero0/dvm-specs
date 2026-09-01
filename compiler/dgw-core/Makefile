# DGW-Core — C++26 build (GNU Make)
#
# Toolchain: g++ 14+, -std=c++26
# Build:     make -j$(nproc)
# Test:      make test
# Clean:     make clean

CXX      := g++
CXXSTD   := -std=c++26
WARN     := -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type
OPT      := -O2 -g
# `make SAN=1` enables UBSan + ASan.
ifeq ($(SAN),1)
SAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
else
SAN_FLAGS :=
endif
CPPFLAGS := -Iinclude
CXXFLAGS := $(CXXSTD) $(WARN) $(OPT) $(SAN_FLAGS) $(EXTRA_CXXFLAGS)
LDFLAGS  := $(SAN_FLAGS)

# Sources & objects
SRCDIR   := src
TSTDIR   := tests
BUILDDIR := build
BINDIR   := bin

LIB_SRC  := $(wildcard $(SRCDIR)/*.cpp)
LIB_OBJ  := $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(LIB_SRC))

TST_SRC  := $(wildcard $(TSTDIR)/*.cpp)
TST_BIN  := $(patsubst $(TSTDIR)/%.cpp,$(BINDIR)/%,$(TST_SRC))

# Top-level smoke binary
SMOKE    := $(BINDIR)/dgw_smoke
SMOKE_SRC := $(TSTDIR)/smoke.cpp

.PHONY: all clean test lib
.DEFAULT_GOAL := all

all: lib $(SMOKE)

# Compile each .cpp to .o
$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

# Static library
lib: $(BUILDDIR)/libdgwcore.a
$(BUILDDIR)/libdgwcore.a: $(LIB_OBJ)
	ar rcs $@ $^

# Smoke binary (links libdgwcore + smoke.cpp)
$(SMOKE): $(SMOKE_SRC) $(BUILDDIR)/libdgwcore.a | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< -L$(BUILDDIR) -ldgwcore -o $@ $(LDFLAGS)

# Per-test binaries (each test links against the static lib)
$(BINDIR)/%: $(TSTDIR)/%.cpp $(BUILDDIR)/libdgwcore.a | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< -L$(BUILDDIR) -ldgwcore -o $@ $(LDFLAGS)

# Run all tests as a poor-man's ctest
test: all
	@set -e; rc=0; \
	for t in $(SMOKE) $(TST_BIN); do \
	  [ -x "$$t" ] || [ "$$t" = "$(SMOKE)" ] || continue; \
	  [ "$$t" = "$(SMOKE)" ] && [ -x "$(SMOKE)" ] || continue; \
	  echo "==> $$t"; \
	  $$t || rc=$$?; \
	done; \
	exit $$rc

# Directories
$(BUILDDIR):
	mkdir -p $@
$(BINDIR):
	mkdir -p $@

# Clean
clean:
	rm -rf $(BUILDDIR) $(BINDIR)
