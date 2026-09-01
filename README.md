# DVM Specifications

Normative specification documents for the **DVM (Dynamic Virtual Machine)** —
a multi-language dynamic virtual machine built around the DVM Portable Bytecode
and Guest Language Profiles.

This repository is the **authoritative source of truth** for the DVM compiler,
runtime, and IR subsystems. Every commit to the `compiler/`, `runtime/`,
`tools/`, and `tests/` trees must comply with these documents. CI verifies
compliance. There are no exceptions.

---

## Documents

| # | Document | Subsystem | Status |
|---|----------|----------|--------|
| 1 | [DVM Compiler Laws & Architecture Specification](docs/DVM-Compiler-Laws.md) | Cross-cutting / Foundational | Stable |
| 2 | [DVM Deoptimization and FrameState Machinery](docs/DVM-Deopt-FrameState.md) | Deopt / Speculation Safety | Stable |
| 3 | [DVM Hybrid Tracing Architecture](docs/DVM-Hybrid-Tracing-Architecture.md) | Tiering / Meta-Tracing | Stable |
| 4 | [DGW-Core IR (Dynamic Graph Web)](docs/DGW-Core-IR.md) | Tier 2 / Tier 3 Optimizing IR | Stable |

### Document Dependency Order

If you are reading these specs for the first time, the recommended order is:

1. **DVM Compiler Laws** — establishes the universal rules (DVM Rules 1–112)
   that every other subsystem must satisfy.
2. **DVM Hybrid Tracing Architecture** — describes the four-tier execution
   model (Tier 0–3) and how meta-tracing is mixed with normal tracing for
   higher tiers.
3. **DGW-Core IR** — the mid-level optimizing IR. Defines the Woven Arena
   memory layout, port/edge typing system, Memory SSA, Partial Escape
   Analysis, the Weaver mutation API, and trace scheduling/lowering.
4. **DVM Deopt & FrameState Machinery** — the deoptimization system that lets
   DGW-Core speculate aggressively while preserving exact guest-language
   semantics. Every `GUARD` node in DGW-Core depends on this subsystem.

---

## Subsystem Map

```
                    ┌────────────────────────────────────────┐
                    │   DVM Compiler Laws (Rules 1–112)      │
                    │   Universal invariants & CI gates      │
                    └─────────────────┬──────────────────────┘
                                      │ governs
              ┌───────────────────────┼───────────────────────┐
              ▼                       ▼                       ▼
   ┌────────────────────┐  ┌──────────────────────┐  ┌─────────────────────┐
   │  Hybrid Tracing     │  │   DGW-Core IR        │  │  Deopt & FrameState  │
   │  (Tier 0 → Tier 3)  │  │   (Woven Arena SoA)  │  │  (Speculation Safe) │
   └─────────┬───────────┘  └──────────┬───────────┘  └──────────┬──────────┘
             │  records traces           │  emits GUARD nodes     │
             └──────────────┬────────────┘────────────────────────┘
                            ▼
                   ┌────────────────────────┐
                   │  DVM Portable Bytecode  │
                   │  + Guest Profiles       │
                   └────────────────────────┘
```

---

## Key Concepts (Glossary)

*   **DVM** — Dynamic Virtual Machine. A multi-language VM built around a
    portable bytecode and per-language guest profiles.
*   **DGW-Core** — Dynamic Graph Web, the mid-level optimizing IR. A blockless
    Sea-of-Nodes-style graph stored in a Structure-of-Arrays arena.
*   **Woven Arena** — The SoA memory layout backing a DGW-Core graph. Optimized
    for cache locality and O(1) mutation via the `Weaver`.
*   **Weaver** — The graph mutation API. Provides `rewire_uses`,
    `splice_into_edge`, `kill_node`, and the O(1) `FWD` forwarding-node trick.
*   **Region / Ref** — First-class memory identities and projections. Make
    alias analysis O(1) and Escape Analysis a graph-reachability problem.
*   **Memory SSA** — The `MEMORY` edge chain that prevents illegal
    reordering of loads and stores.
*   **GUARD** — A speculative assumption node. Its failure path routes
    exclusively to a `DEOPT_TRAP` (or `UNCOMMON_TRAP`).
*   **FrameState** — A snapshot of guest-level execution state attached to
    every `GUARD`, enabling deoptimization to reconstruct the interpreter
    state observationally indistinguishable from Tier 0.
*   **PEA** — Partial Escape Analysis. Promoted by the `MATERIALIZE` node
    that converts a `VIRTUAL` region into a real heap pointer only on the
    paths that actually escape.
*   **Trace Scheduling** — The Phase-K lowering step that walks `CONTROL`
    edges using PGO probabilities to form linear traces, then groups them
    into `MachineBasicBlock`s for instruction selection.

---

## Compliance

Every pull request touching `compiler/`, `runtime/`, `tools/`, or `tests/`
must pass the CI gate that verifies compliance with these specs. The
`WebVerifier` (defined in Part 8 of DGW-Core-IR.md) runs after every
optimization pass in debug builds to enforce structural, semantic,
memory/effect, and speculative-deopt safety invariants.

Non-compliance is a blocking CI failure.

---

## License

Proprietary — DVM Systems Dev Team. See [`LICENSE`](LICENSE) for details.
