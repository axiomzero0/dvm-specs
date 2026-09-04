# DVM Tier 0 Interpreter — C++26 build (GNU Make)
#
# Toolchain: g++ 14+, -std=c++26
# Build:     make -j$(nproc)
# Test:      make test
# Clean:     make clean

CXX      := g++
CXXSTD   := -std=c++26
# All warnings are errors. No lazy unused-variable suppressions; if a
# variable is set but not used, either use it or delete it. Being lazy
# about warnings hides bugs.
WARN     := -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter
OPT      := -O2 -g
# `make SAN=1` enables UBSan + ASan.
ifeq ($(SAN),1)
SAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
else
SAN_FLAGS :=
endif
CPPFLAGS := -Iinclude -I../../compiler/dgw-core/include
CXXFLAGS := $(CXXSTD) $(WARN) $(OPT) $(SAN_FLAGS) $(EXTRA_CXXFLAGS)
LDFLAGS  := $(SAN_FLAGS) -L../../compiler/dgw-core/build -ldgwcore

# Sources & objects
SRCDIR   := src
TSTDIR   := tests
BUILDDIR := build
BINDIR   := bin

LIB_SRC  := $(wildcard $(SRCDIR)/*.cpp)
LIB_OBJ  := $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(LIB_SRC))

# Top-level smoke binary
SMOKE    := $(BINDIR)/dvm_interp_smoke
SMOKE_SRC := $(TSTDIR)/smoke.cpp

.PHONY: all clean test lib
.DEFAULT_GOAL := all

all: lib $(SMOKE)

# Compile each .cpp to .o
$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

# Per-file flag: interp.cpp uses computed goto (GCC labels-as-values), a
# GNU extension flagged by -Wpedantic. Computed goto is the standard
# interpreter dispatch idiom and the user explicitly required it.
# This is a per-file Makefile exception, not a source-level suppression.
$(BUILDDIR)/interp.o: CXXFLAGS += -Wno-pedantic

# Static library
lib: $(BUILDDIR)/libdvm_interp.a
$(BUILDDIR)/libdvm_interp.a: $(LIB_OBJ)
	ar rcs $@ $^

# Smoke binary (links libdvm_interp + libdgwcore + smoke.cpp)
$(SMOKE): $(SMOKE_SRC) $(BUILDDIR)/libdvm_interp.a ../../compiler/dgw-core/build/libdgwcore.a | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< -L$(BUILDDIR) -ldvm_interp -L../../compiler/dgw-core/build -ldgwcore -o $@ $(LDFLAGS)

# Run smoke binary
test: all
	@set -e; rc=0; \
	for t in $(SMOKE); do \
	  [ -x "$$t" ] || continue; \
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
