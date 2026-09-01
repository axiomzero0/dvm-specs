// dgw/scheduler.hpp — Trace Scheduling and Block Formation (Spec Part 7).
//
// Spec citation: DGW-Core-IR.md Part 7 "Scheduling and Block Formation
//                (Lowering)".
//
//  7.1 Trace Formation
//  7.2 Block Extraction
//  7.3 Instruction Selection
//
// In this implementation, Part 7 produces a MachineCFG: a vector of
// MachineBasicBlock where each block has a sequence of scheduled DGW nodes
// and (at block heads) the PHI nodes reconstructed from JOIN/STATE nodes
// (per spec 7.2.3 and 7.2.4).
//
// The actual MachineInstr representation (7.3) is delegated to a downstream
// backend not in this directory; we expose a `MachineOp` enum that is
// sufficient for the smoke test to demonstrate the lowering pipeline.
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dgw/ids.hpp"
#include "dgw/kinds.hpp"
#include "dgw/weaver.hpp"

namespace dgw {

// ---- Scheduling output --------------------------------------------------
struct MachineOp {
  NodeId origin;          // DGW node this op came from (or kNullNode for PHIs).
  NodeKind origin_kind;   // Cached for the backend.
  std::string label;      // Human-readable, e.g. "MOV r1, [r0+8]".
};

struct MachinePhi {
  NodeId origin;          // JOIN/STATE this PHI came from.
  std::vector<NodeId> incomings;          // One per predecessor block, in order.
  std::vector<std::uint32_t> block_ids;    // Block each incoming comes from.
};

struct MachineBasicBlock {
  std::uint32_t id;
  std::vector<MachinePhi>    phis;  // At block head, per spec 7.2.3/7.2.4.
  std::vector<MachineOp>    ops;   // Straight-line scheduled ops.
  std::vector<std::uint32_t> preds; // Block IDs of predecessors.
  std::vector<std::uint32_t> succs; // Block IDs of successors.
};

struct MachineCFG {
  std::vector<MachineBasicBlock> blocks;
  std::uint32_t entry_block_id{};
};

// ---- PGO probabilities ---------------------------------------------------
// Spec 7.1: "At a BRANCH, use PGO (Profile-Guided Optimization) probabilities
//  to pick the 'hottest' path."
// In this implementation, PGO is fed as a callback the scheduler calls on
// each BRANCH. Returning 0.5 means "indifferent"; the scheduler falls back to
// the FALSE path in that case. The smoke test supplies a fixed 0.9 callback.
using PgoProbFn = double (*)(NodeId branch_node, void* user);

// ---- Top-level API ------------------------------------------------------
// Build a MachineCFG from the current graph. Traces start at the START node
// and follow CONTROL edges using `pgo` to pick at BRANCHes.
MachineCFG schedule_to_cfg(Weaver& w, PgoProbFn pgo, void* pgo_user);

// Pretty-print a MachineCFG for the smoke test. The Weaver/arena is needed
// only to look up the origin node kind for PHI labels; pass `nullptr` to
// skip that lookup.
void print_cfg(const MachineCFG& cfg, const GraphArena* arena = nullptr);

}  // namespace dgw
