# DGW-Core — the mid-level optimizing IR for the DVM.

This directory implements the `DGW-Core` IR as specified in
[`docs/DGW-Core-IR.md`](../../docs/DGW-Core-IR.md) of this repository. It is the
authoritative C++26 implementation; if any code in this tree disagrees with
the spec, the spec wins (and the code is in violation until fixed, per
[`docs/Mandatory-Agent-Review-Rule.md`](../../docs/Mandatory-Agent-Review-Rule.md)).

## Layout

```
compiler/dgw-core/
├── include/dgw/      # Public headers (graph arena, weaver, passes, verifier)
│   ├── ids.hpp          # Part 1.1 — NodeId / EdgeId / PortId / RegionId / SymbolId / TypeId
│   ├── arena.hpp        # Part 1.2 — GraphArena (SoA)
│   ├── payloads.hpp     # Part 1.3 — ConstPayload / RefPayload / CallPayload / GuardPayload
│   ├── kinds.hpp        # Part 2.1 — EdgeKind / NodeKind / NodeFlags
│   ├── signatures.hpp   # Part 2.2 — PortSignature table (LOAD, GUARD, ...)
│   ├── regions.hpp       # Part 3   — RegionKind / EscapeState / Memory-SSA helpers
│   ├── control.hpp       # Part 4   — BRANCH / JOIN / STATE / EXCEPT / HANDLER helpers
│   ├── weaver.hpp        # Part 5   — Weaver (rewire_uses, FWD, splice, kill)
│   ├── pass_gvn.hpp      # Part 6.1 — Global Value Numbering
│   ├── pass_dce.hpp      # Part 6.2 — Dead Code Elimination
│   ├── pass_licm.hpp     # Part 6.3 — Loop Invariant Code Motion
│   ├── pass_cleanup.hpp  # Part 5.2 — FWD-chain CleanupPass
│   ├── scheduler.hpp     # Part 7   — Trace scheduling + block formation
│   ├── verifier.hpp      # Part 8   — WebVerifier (4 layers)
│   ├── graph.hpp         # Top-level Graph facade wrapping GraphArena + Weaver
│   └── util.hpp          # small helpers (assert, panic, source_location)
├── src/                # Implementations — one .cpp per header, one .cpp per pass
│   ├── control.cpp
│   ├── graph.cpp
│   ├── pass_gvn.cpp      # GVN
│   ├── pass_dce.cpp      # DCE
│   ├── pass_licm.cpp     # LICM
│   ├── pass_cleanup.cpp  # CleanupPass
│   ├── scheduler.cpp
│   ├── signatures.cpp
│   ├── verifier.cpp
│   └── weaver.cpp
├── tests/              # Smoke tests + per-pass tests
│   ├── smoke.cpp         # end-to-end: build a small graph, run GVN+DCE+Cleanup, verify
│   └── ...
└── Makefile            # GNU Make build (no CMake required)
```

**Rule:** every pass has its own `.cpp` AND its own `.hpp`. Do not collapse
multiple passes into a single file — large combined files become
unmaintainable. New passes go in `pass_<name>.cpp` / `pass_<name>.hpp`.

## Build

The toolchain is **g++ 14+ with `-std=c++26`**. No CMake/Ninja needed.

```bash
make -j$(nproc)         # build libdgwcore.a + smoke binary
make test               # run smoke binary; exit code propagates
make SAN=1             # build with UBSan+ASan
make clean              # remove build/ and bin/
```

## Compliance

Every file in this tree carries a spec-citation header pointing to the part
of `docs/DGW-Core-IR.md` it implements. Any change here requires a review per
`docs/Mandatory-Agent-Review-Rule.md` Section 3, including a `WebVerifier`
run on the affected test fixtures (Section 3.3).
