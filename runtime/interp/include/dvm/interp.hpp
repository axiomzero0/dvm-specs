// dvm/interp.hpp — Top-level interpreter entry point.
//
// Spec citation: DVM-CRB.md Section 4 "Execution Model" and Section 29
// "Interpreter Implementation Notes" (direct threading).
//
// `interpret()` runs the module starting from the function with the
// given ID. It uses computed-goto dispatch (GCC labels-as-values) for
// the dispatch loop — the standard interpreter dispatch idiom.
//
#pragma once

#include <cstdint>

#include "dvm/crb.hpp"
#include "dvm/state.hpp"

namespace dvm {

// Forward declarations
class TraceRecorder;
class HotnessTracker;

// Run the module starting from the function with the given ID. The
// interpreter runs until the outermost frame returns (or TRAP fires).
// Returns the exit value (the value returned from the outermost frame).
Value interpret(const crb::Module& module, std::uint32_t entry_function_id);

// Run the module with an attached trace recorder (or nullptr to disable
// recording). The recorder is started at the entry function's first
// instruction (PC=0) and records every executed instruction until a side
// exit (branch, return, trap) or the trace closes (backedge to entry PC).
// After execution, the recorder's fragment holds the recorded trace.
Value interpret(const crb::Module& module, std::uint32_t entry_function_id,
                 TraceRecorder* recorder);

// Run the module with a trace recorder AND a hotness tracker. The hotness
// tracker counts backward branches; when a loop header's backedge count
// exceeds the threshold, it auto-starts the recorder at that loop header.
// This implements the Tier 0 → Tier 1 meta-tracing trigger.
// Pass nullptr for either to disable.
Value interpret(const crb::Module& module, std::uint32_t entry_function_id,
                 TraceRecorder* recorder, HotnessTracker* hotness);

}  // namespace dvm
