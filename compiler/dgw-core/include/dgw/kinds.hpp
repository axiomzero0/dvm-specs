// dgw/kinds.hpp — EdgeKind, NodeKind, NodeFlags (Spec Parts 2.1 and 1.2).
//
// Spec citation: DGW-Core-IR.md Part 2.1 "Edge Kinds (The Threads of the Web)"
// and Part 1.2 "Graph Arena (Structure of Arrays)" (NodeFlags column).
//
// EdgeKind must remain a uint8_t per spec. NodeKind is the discriminant for
// node_payload_idx side-table selection (Part 1.3) and for port-signature
// lookup (Part 2.2). NodeFlags is the bitmask stored in node_flags[].
//
#pragma once

#include <cstdint>

namespace dgw {

// ---- EdgeKind (Spec Part 2.1) ----------------------------------------------
enum class EdgeKind : std::uint8_t {
  VALUE,    // Pure data dependency (SSA). Carries TypeId.
  CONTROL,  // Execution flow (Success path). Carries no data, just ordering.
  EXCEPT,   // Execution flow (Exception/Failure path).
  MEMORY,   // Memory state token (Memory SSA). Ensures Load/Store ordering.
  EFFECT,   // Side-effect ordering (I/O, GC barriers, Tracing).
  GUARD,    // Speculative assumption dependency.
};
static_assert(sizeof(EdgeKind) == 1, "EdgeKind must be 1 byte per spec Part 2.1.");

// ---- NodeKind --------------------------------------------------------------
// The full node taxonomy. Each entry's port signature is defined in
// signatures.hpp. The ordering is stable for the lifetime of DGW-Core —
// passes use these as switch cases, and the verifier (Part 8) checks them.
//
// Naming convention: verbs (LOAD, STORE) for ops; explicit names for control.
enum class NodeKind : std::uint16_t {
  // ---- Constants / data sources ----
  CONST,            // payload_idx -> ConstPayload (Part 1.3). Output[0]: VALUE.
  REF,              // payload_idx -> RefPayload. Output[0]: VALUE (a REF type).
  ALLOC,            // Allocates a Region. Output[0]: REGION (a RegionId view).
                    // Does NOT output a pointer; outputs a Region identity (Part 3.1).
  MATERIALIZE,      // Part 3.4 — converts a virtual Region to a real heap ptr.
                    // Input: VALUE (region view); Output[0]: VALUE (heap ptr).
                    // Output[1]: MEMORY (new memory state, because materialization
                    //              may write to the heap).

  // ---- Memory ops ----
  LOAD,             // See signatures.hpp for the full port map.
  STORE,            // ditto.
  MEMORY_JOIN,      // Merges two MEMORY chains (used where the spec allows
                    // explicit merging in Part 8.3).

  // ---- Arithmetic / pure ops (small canonical set) ----
  // These are intentionally minimal — DGW-Core is a small canonical IR, the
  // backend lowers from this set. Adding more would not violate the spec, but
  // these are the operations the spec text exercises.
  ADD, SUB, MUL, DIV, MOD,        // integer arithmetic
  FADD, FSUB, FMUL, FDIV,         // float arithmetic
  AND, OR, XOR, SHL, SHR, SAR,    // bit ops
  CMP_EQ, CMP_NE, CMP_LT, CMP_LE, CMP_GT, CMP_GE,  // comparisons
  CAST,                            // type cast (requires a no-op implicit cast
                                   // node — see Part 8.2 typing rule).

  // ---- Control flow (Part 4) ----
  START,            // Single source of all CONTROL/MEMORY tokens.
  BRANCH,           // Input: CONTROL + VALUE; Output: TRUE_CONTROL + FALSE_CONTROL.
  JOIN,             // Merges multiple CONTROL + multiple VALUE tokens.
  STATE,            // Loop header. Inputs: init value + backedge value. (Part 4.2)
  HANDLER,          // Exception landing pad. Input: EXCEPT; Output: CONTROL.
  RETURN,           // Observable. Input: CONTROL + VALUE (the return value).
  DEOPT_TRAP,       // Spec Part 8.4 — guard failure terminal.
  UNCOMMON_TRAP,    // Spec Part 8.4 — soft-deopt terminal (e.g. profile-mismatch).
  DEAD,             // Sentinel; weaver.kill_node transitions to this in-place.

  // ---- Function call ----
  CALL,             // payload_idx -> CallPayload.

  // ---- Speculation ----
  GUARD,            // payload_idx -> GuardPayload. Spec Part 2.2 GUARD signature.

  // ---- Internal: FWD forwarding node (Part 5.2) ----
  FWD,              // Internal-only. Single input; users transparently see
                    // the forwarded value. CleanupPass collapses FWD chains.
};
static_assert(sizeof(NodeKind) == 2, "NodeKind is uint16_t by spec Part 1.2.");

// ---- NodeFlags (Spec Part 1.2: NodeFlags bitmask) -------------------------
// Spec text: "Bitmask (Pure, Volatile, Guarded, etc.)". We expand the
// abbreviated list into the full set the rest of the spec references.
enum class NodeFlags : std::uint32_t {
  None        = 0,
  Pure        = 1u << 0,  // No side effects; safe for GVN (Part 6.1).
  Volatile    = 1u << 1,  // Cannot be reordered, hoisted, or CSE'd.
  Guarded     = 1u << 2,  // Has a speculative assumption attached.
  Observable  = 1u << 3,  // DCE seed (Part 6.2): RETURN, STORE-to-global, I/O, DEOPT_TRAP.
  Dead        = 1u << 4,  // kill_node marked this; will be reclaimed at epoch end.
  HasExcept   = 1u << 5,  // Node may throw; EXCEPT port is live (Part 4.3).
  Materialized= 1u << 6,  // ALLOC has been physically emitted (Part 3.4 PEA).
  NoThrow     = 1u << 7,  // CALL is guaranteed not to throw.
  NoDeopt     = 1u << 8,  // Code path cannot reach a guard failure.
};
[[maybe_unused]] constexpr NodeFlags operator|(NodeFlags a, NodeFlags b) {
  return static_cast<NodeFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
[[maybe_unused]] constexpr NodeFlags operator&(NodeFlags a, NodeFlags b) {
  return static_cast<NodeFlags>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
[[maybe_unused]] constexpr NodeFlags operator~(NodeFlags a) {
  return static_cast<NodeFlags>(~static_cast<std::uint32_t>(a));
}
[[maybe_unused]] constexpr bool has_flag(NodeFlags flags, NodeFlags bit) {
  return (flags & bit) != NodeFlags::None;
}

}  // namespace dgw
