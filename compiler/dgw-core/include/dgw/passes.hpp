// dgw/passes.hpp — Optimization Passes (Spec Part 6).
//
// Spec citation: DGW-Core-IR.md Part 6 "Optimization Passes on the Web".
//
//  6.1 Global Value Numbering (GVN)
//  6.2 Dead Code Elimination (DCE)
//  6.3 Loop Invariant Code Motion (LICM)
//
// Each pass is a function `bool pass_X(Weaver& w)` that returns true if it
// changed the graph. Each pass is idempotent: running it twice produces
// the same graph on the second run (no mutations). The WebVerifier must
// pass after every individual pass when DGW_ALWAYS_VERIFY is set.
//
#pragma once

#include <cstdint>

#include "dgw/weaver.hpp"

namespace dgw {

// ---- GVN (Spec 6.1) -----------------------------------------------------
// "1. Iterate over all pure nodes.
//  2. Hash (NodeKind, InputEdges).
//  3. If hash exists in GVN map, call weaver.rewire_uses(CurrentNode, ExistingNode).
//  4. Call weaver.kill_node(CurrentNode)."
//
// Returns the number of nodes eliminated.
struct GvnStats {
  std::uint32_t eliminated{0};
  std::uint32_t visited{0};
};
GvnStats pass_gvn(Weaver& w);

// ---- DCE (Spec 6.2) -----------------------------------------------------
// "1. Seed a worklist with all Observable nodes (RETURN, STORE to global,
//      I/O, DEOPT_TRAP).
//  2. Walk backwards along VALUE, CONTROL, MEMORY, and EFFECT edges,
//      marking nodes as Live.
//  3. Any node not marked Live is passed to weaver.kill_node()."
struct DceStats {
  std::uint32_t killed{0};
  std::uint32_t live{0};
};
DceStats pass_dce(Weaver& w);

// ---- LICM (Spec 6.3) -----------------------------------------------------
// "1. Identify STATE nodes (loop headers).
//  2. For each node inside the loop, check if its VALUE inputs originate
//      from outside the loop (do not depend on the STATE backedge).
//  3. If invariant, and its EFFECT/MEMORY dependencies allow it, detach it
//      from the loop's CONTROL token and reattach it to the CONTROL token
//      preceding the loop."
struct LicmStats {
  std::uint32_t hoisted{0};
  std::uint32_t visited{0};
};
LicmStats pass_licm(Weaver& w);

// ---- Cleanup pass (Part 5.2) --------------------------------------------
// Collapses FWD chains into a single FWD per chain and kills FWD chains
// whose terminus is dead. Returns the number of FWD nodes collapsed.
struct CleanupStats {
  std::uint32_t collapsed{0};
  std::uint32_t killed{0};
};
CleanupStats pass_cleanup(Weaver& w);

}  // namespace dgw
