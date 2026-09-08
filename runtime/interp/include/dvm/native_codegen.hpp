// dvm/native_codegen.hpp — Native code generation: MachineCFG → x86-64.
//
// Spec citation: DGW-Core-IR.md Part 7.3 (Instruction Selection).
//
// The native code generator takes a scheduled MachineCFG and emits
// x86-64 machine code into a JitBuffer. The result is a callable
// function pointer that executes the compiled trace natively.
//
// The calling convention is: int64_t fn(int64_t r0, int64_t r1, int64_t r2, ...)
// — the trace's entry registers are passed as function arguments, and the
// result is returned in rax.
//
#pragma once

#include <cstdint>
#include <functional>

#include "dvm/jit_buffer.hpp"
#include "dgw/scheduler.hpp"
#include "dgw/graph.hpp"

namespace dvm {

// A compiled native trace — holds the JitBuffer and the entry pointer.
struct NativeTrace {
  JitBuffer code;
  std::function<std::int64_t(std::int64_t, std::int64_t, std::int64_t, std::int64_t)> entry;
  std::size_t code_size() const noexcept { return code.size(); }
};

// Compile a DGW graph into native x86-64 code.
// Walks the graph's node table in order (which is already in SSA order
// from the lifter) and emits x86-64 for each non-DEAD node.
NativeTrace compile_to_native(const dgw::Graph& graph);

void print_native_trace(const NativeTrace& nt);

}  // namespace dvm
