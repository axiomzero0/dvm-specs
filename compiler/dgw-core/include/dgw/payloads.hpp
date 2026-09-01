// dgw/payloads.hpp — Node Payloads (Spec Part 1.3).
//
// Spec citation: DGW-Core-IR.md Part 1.3 "Node Payloads (Union of Specific Data)".
//
// "Nodes that require extra data (like constants, field offsets, or call
//  signatures) do not bloat the base NodeTable. They reference an index into
//  specialized payload arrays."
//
// Each payload type here corresponds to one specialized side-table owned by
// the GraphArena. The arena's `node_payload_idx` column (Part 1.2) is the
// index into the appropriate side-table; which table is determined by the
// node's `NodeKind`.
//
#pragma once

#include <cstdint>
#include <variant>

#include "dgw/ids.hpp"

namespace dgw {

// ---- AccessPerm (used by RefPayload) ---------------------------------------
// Spec Part 3.2: a REF node projects a view into a Region. The view carries
// an access permission. This is the standard set: immutable, read-write,
// and "raw" (no-alias-assumption) — used to express pointer casts through
// FFI / unsafe guest ops.
enum class AccessPerm : std::uint8_t {
  ReadOnly,
  ReadWrite,
  WriteOnly,
  Raw,          // No aliasing guarantees (FFI / unsafe).
};

// ---- CallConv (used by CallPayload) ----------------------------------------
enum class CallConv : std::uint8_t {
  Guest,        // Call into another guest function (follows DVM ABI).
  Native,       // Call into DVM runtime (C ABI).
  FFI,          // Call into arbitrary foreign code (must materialize refs).
  Tail,         // Tail call — no FrameState on the caller's stack.
};

// ---- DeoptReason (used by GuardPayload) ------------------------------------
// Matches the deopt-reason enum used by the deopt machinery (DVM-Deopt-FrameState.md).
// We keep the values stable here so the verifier (Part 8.4) can check that
// every Guard's failure path routes to a DEOPT_TRAP/UNCOMMON_TRAP with a
// compatible reason.
enum class DeoptReason : std::uint8_t {
  Unreachable,          // Speculative path proved unreachable.
  NullCheck,           // receiver was null.
  BoundsCheck,         // array index out of bounds.
  TypeCheck,           // speculative type assumption failed.
  ClassCheck,          // speculative class test failed.
  DivByZero,           // integer divide by zero.
  Overflow,            // integer overflow on a speculative "no-overflow" guard.
  StackOverflow,       // recursion limit exceeded.
  MaterializeFailed,   // PEA needed to materialize a virtual region (Part 3.4).
  IntrinsicMismatch,   // intrinsic signature mismatch at call site.
  ProfileMismatch,     // PGO profile drifted from observed execution.
  UnstableDeopt,       // deopt triggered during deopt (panic).
};

// ---- ConstPayload ----------------------------------------------------------
// Spec Part 1.3 example:
//   struct ConstPayload { std::variant<int64_t, double, SymbolId> value; };
struct ConstPayload {
  std::variant<std::int64_t, double, SymbolId> value;
};

// ---- RefPayload ------------------------------------------------------------
// Spec Part 1.3 example:
//   struct RefPayload { RegionId region; int64_t offset; AccessPerm perms; };
struct RefPayload {
  RegionId    region{};
  std::int64_t offset{};
  AccessPerm  perms{AccessPerm::ReadOnly};
};

// ---- CallPayload -----------------------------------------------------------
// Spec Part 1.3 example:
//   struct CallPayload { SymbolId target; CallConv conv; uint16_t arg_count; };
struct CallPayload {
  SymbolId     target{};
  CallConv     conv{CallConv::Guest};
  std::uint16_t arg_count{};
};

// ---- GuardPayload ----------------------------------------------------------
// Spec Part 1.3 example:
//   struct GuardPayload { FrameStateId frame_state; DeoptReason reason; };
struct GuardPayload {
  FrameStateId frame_state{};
  DeoptReason  reason{DeoptReason::Unreachable};
};

}  // namespace dgw
