// dgw/pass_gvn.hpp — Global Value Numbering (Spec Part 6.1).
//
// Spec citation: DGW-Core-IR.md Part 6.1 "Global Value Numbering (GVN)".
//
// "1. Iterate over all pure nodes.
//  2. Hash (NodeKind, InputEdges).
//  3. If hash exists in GVN map, call weaver.rewire_uses(CurrentNode, ExistingNode).
//  4. Call weaver.kill_node(CurrentNode)."
//
// This pass is a graph transformer operating via the Weaver. It is idempotent
// (running it twice yields the same graph on the second run, no mutations).
// The WebVerifier (Part 8) must pass after this pass when DGW_ALWAYS_VERIFY
// is set.
//
#pragma once

#include <cstdint>

#include "dgw/weaver.hpp"

namespace dgw {

struct GvnStats {
  std::uint32_t eliminated{0};
  std::uint32_t visited{0};
};

// Runs GVN over the graph. Returns per-pass statistics.
GvnStats pass_gvn(Weaver& w);

}  // namespace dgw
