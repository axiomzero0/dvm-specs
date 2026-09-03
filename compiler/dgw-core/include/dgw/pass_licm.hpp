// dgw/pass_licm.hpp — Loop Invariant Code Motion (Spec Part 6.3).
//
// Spec citation: DGW-Core-IR.md Part 6.3 "Loop Invariant Code Motion (LICM)".
//
// "1. Identify STATE nodes (loop headers).
//  2. For each node inside the loop, check if its VALUE inputs originate
//      from outside the loop (do not depend on the STATE backedge).
//  3. If invariant, and its EFFECT/MEMORY dependencies allow it, detach it
//      from the loop's CONTROL token and reattach it to the CONTROL token
//      preceding the loop."
//
#pragma once

#include <cstdint>

#include "dgw/weaver.hpp"

namespace dgw {

struct LicmStats {
  std::uint32_t hoisted{0};
  std::uint32_t visited{0};
};

// Runs LICM over the graph. Returns per-pass statistics.
// The pass identifies STATE nodes, computes the loop body, identifies
// invariant pure nodes, and — for those that have a CONTROL input from
// inside the loop body — reattaches that CONTROL input to START's CONTROL
// output (the loop-preheader substitute).
LicmStats pass_licm(Weaver& w);

}  // namespace dgw
