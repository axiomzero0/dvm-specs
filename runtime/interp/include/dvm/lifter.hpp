// dvm/lifter.hpp — DGW lifter: TraceFragment → DGW-Core IR graph.
//
// Spec citation: DVM-Hybrid-Tracing-Architecture.md §12.3 (trace
// validation), §T-008 (Guest Bytecode Lifting). DGW-Core-IR.md Parts 1-7.
//
// The lifter converts a recorded TraceFragment (a linear sequence of CRB
// instructions) into a DGW-Core IR graph. Each CRB instruction becomes one
// or more DGW nodes connected via VALUE/CONTROL/MEMORY edges.
//
// The lifter maps CRB registers to DGW SSA values: each register write
// creates a new SSA value; each register read uses the most recent SSA
// value for that register. This is the standard "register renaming" pass
// that converts a register machine's linear trace into SSA form.
//
#pragma once

#include <cstdint>
#include <vector>

#include "dvm/crb.hpp"
#include "dvm/trace.hpp"

// Forward-declare the DGW-Core types we need.
namespace dgw {
class Graph;
struct NodeId;
}  // namespace dgw

namespace dvm {

// Lift a TraceFragment into a DGW-Core IR graph. The graph is created
// fresh (the caller owns it). Returns a pointer to the new graph, or
// nullptr if lifting fails.
//
// The lifter creates:
//   - A START node (source of CONTROL + MEMORY)
//   - One node per CRB instruction (CONST, ADD, CMP, BRANCH, RET, etc.)
//   - VALUE edges connecting register definitions to uses (SSA renaming)
//   - CONTROL edges threading through the trace
//   - A STATE node for loop traces (the backedge)
//   - A RETURN node for the exit
//   - A GUARD node for the side-exit condition (if applicable)
dgw::Graph* lift_trace(const TraceFragment& frag);

// Pretty-print the lifted graph for debugging.
void print_lifted_graph(const dgw::Graph& g);

}  // namespace dvm
