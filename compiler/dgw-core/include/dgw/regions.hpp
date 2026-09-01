// dgw/regions.hpp — First-class Regions, References, and Memory-SSA helpers.
//
// Spec citation: DGW-Core-IR.md Part 3 "The Memory and Region Model
// (Structural Aliasing)".
//
// Part 3.1 RegionKind, Part 3.2 Refs, Part 3.3 Memory-SSA, Part 3.4 PEA via
// MATERIALIZE.
//
#pragma once

#include <cstdint>

#include "dgw/ids.hpp"
#include "dgw/kinds.hpp"
#include "dgw/payloads.hpp"

namespace dgw {

// ---- RegionKind (Spec Part 3.1) -------------------------------------------
enum class RegionKind : std::uint8_t {
  STACK,      // Allocated on the stack; lifetime bounded by current frame.
  HEAP,       // Heap allocation; lifetime bounded by GC.
  GLOBAL,     // Process-wide singleton (e.g. globals table).
  TLS,        // Thread-local storage.
  VIRTUAL,    // Part 3.4 — region has been proven NO_ESCAPE; ALLOC deleted
              // and replaced with a VIRTUAL marker. Stores/Loads to it are
              // intercepted and stored in a side-table by the optimizer.
  ELIMINATED, // Region has been fully eliminated; all refs to it are dead.
};
static_assert(sizeof(RegionKind) == 1);

// ---- EscapeState (Spec Part 1.2 region_escapes column) --------------------
enum class EscapeState : std::uint8_t {
  NoEscape,     // Region is provably local; safe to virtualize.
  ArgEscape,    // Region is passed to a callee but not stored globally.
                // Callee may or may not escape it; PEA handles per-path.
  GlobalEscape, // Region escapes to globals, I/O, or unknown code.
                // Must be materialized on every path that observes it.
};
static_assert(sizeof(EscapeState) == 1);

// ---- RegionInfo: full per-Region descriptor --------------------------------
// The arena (Part 1.2) stores the four columns separately; this struct is
// just a convenient read view returned by `Graph::region(r)`.
struct RegionInfo {
  RegionKind    kind{RegionKind::STACK};
  std::uint32_t size{};
  std::uint8_t  align{};
  EscapeState   escape{EscapeState::NoEscape};
};

}  // namespace dgw
