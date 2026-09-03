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

// Run the module starting from the function with the given ID. The
// interpreter runs until the outermost frame returns (or TRAP fires).
// Returns the exit value (the value returned from the outermost frame).
Value interpret(const crb::Module& module, std::uint32_t entry_function_id);

}  // namespace dvm
