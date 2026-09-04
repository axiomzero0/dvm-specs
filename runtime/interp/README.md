# DVM Tier 0 Interpreter — CRB execution with computed-goto dispatch.

This is the **DVM Tier 0 register interpreter** that executes
[DVM-CRB](../../docs/DVM-CRB.md) — the Common Register Bytecode. It is the
full semantic fallback for deoptimization, the trace-recording baseline, and
the canonical execution state for the DVM.

Per `docs/DVM-Hybrid-Tracing-Architecture.md`, Tier 0 is a
**direct-threaded register interpreter**. This implementation uses
**computed-goto dispatch** (GCC labels-as-values) for the dispatch loop, per
the user's explicit requirement. Computed-goto dispatch is a GNU extension
flagged by `-Wpedantic`; the Makefile adds `-Wno-pedantic` for `interp.cpp`
only (documented per-file exception, not a source-level suppression).

## Layout

```
runtime/interp/
├── include/dvm/
│   ├── value.hpp          # Value union (i64/f64/obj/bool/null/undef)
│   ├── crb.hpp             # CRB module structures (header, section, instruction cell)
│   ├── opcodes.hpp         # Opcode constants (16-bit opcode space)
│   ├── loader.hpp          # Module loader: parse binary, validate header
│   ├── state.hpp           # InterpState: registers, PC, frame stack
│   └── interp.hpp         # Top-level interpret() function
├── src/
│   ├── loader.cpp          # Module loader implementation
│   ├── state.cpp           # State implementation (frame push/pop)
│   ├── interp.cpp          # Main computed-goto dispatch loop
│   ├── opcodes_sys.cpp     # System opcode handlers (NOP, TRAP, RET, etc.)
│   ├── opcodes_move.cpp    # Move/constant opcode handlers
│   ├── opcodes_arith.cpp   # Integer/float arithmetic handlers
│   ├── opcodes_control.cpp # Branch/jump/switch handlers
│   ├── opcodes_calls.cpp   # Call/return handlers
│   ├── opcodes_object.cpp  # Object/index opcode handlers
│   ├── opcodes_except.cpp  # Exception opcode handlers
│   └── opcodes_suspend.cpp # Suspension opcode handlers
├── tests/
│   └── smoke.cpp           # Build a tiny CRB module, run it, verify result
└── Makefile
```

**Rule:** every opcode category gets its own `.cpp` file. No monolithic
`opcodes.cpp`. New categories go in `opcodes_<name>.cpp`.

## Build

Toolchain: **g++ 14+ with `-std=c++26`**.

```bash
make -j$(nproc)         # build libdvm_interp.a + smoke binary
make SAN=1 -j$(nproc)   # build with ASan + UBSan
make test               # run smoke binary
make clean              # remove build/ and bin/
```

## Spec compliance

Per `docs/Mandatory-Agent-Review-Rule.md`, every change here requires
independent review. The CRB spec (`docs/DVM-CRB.md`) is the source of truth
for the bytecode format; the interpreter must execute every opcode per the
spec's semantics.
