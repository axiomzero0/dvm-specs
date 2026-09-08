// dvm/trace_compiler.hpp — Trace compiler: DGW graph → optimized + scheduled.
//
// Spec citation: DVM-Hybrid-Tracing-Architecture.md §12.3 (trace
// validation + compilation), §T-009 (trace compilation).
// DGW-Core-IR.md Parts 6 (optimization passes), 7 (scheduling).
//
// The trace compiler takes a lifted DGW-Core IR graph (from the lifter)
// and compiles it into an optimized, scheduled form:
//
//   1. Run GVN (Global Value Numbering) — eliminate duplicate computations
//   2. Run DCE (Dead Code Elimination) — remove unused nodes
//   3. Run Cleanup — collapse FWD chains
//   4. Schedule to MachineCFG — form basic blocks for code generation
//   5. Verify — run the WebVerifier on the optimized graph
//
// The result is a CompiledTrace containing:
//   - The optimized DGW graph (with optimization stats)
//   - The scheduled MachineCFG (basic blocks + ops)
//   - The verifier report on the optimized graph
//
// In a full DVM, the MachineCFG would then be lowered to native machine
// code (instruction selection + register allocation + code emission).
// For the initial implementation, the MachineCFG IS the compiled trace
// — the interpreter can execute it directly by walking the blocks.
//
#pragma once

#include <cstdint>
#include <memory>

#include "dgw/graph.hpp"
#include "dgw/scheduler.hpp"

namespace dvm {

// ---- Optimization stats from the trace compiler -------------------------
struct TraceCompileStats {
  dgw::GvnStats       gvn;
  dgw::DceStats        dce;
  dgw::CleanupStats    cleanup;
  dgw::ConstFoldStats  constfold;

  // Graph size before and after optimization
  std::uint32_t nodes_before{0};
  std::uint32_t nodes_after{0};
  std::uint32_t edges_before{0};
  std::uint32_t edges_after{0};

  // Scheduling result
  std::uint32_t blocks{0};
  std::uint32_t ops{0};

  // Verifier result on the optimized graph
  bool verifier_ok{false};
  std::uint32_t verifier_pass{0};
  std::uint32_t verifier_fail{0};
};

// ---- A compiled trace ---------------------------------------------------
struct CompiledTrace {
  // The optimized DGW-Core graph (owned by the CompiledTrace)
  std::unique_ptr<dgw::Graph> graph;

  // The scheduled machine CFG (basic blocks + ops)
  dgw::MachineCFG cfg;

  // Compilation statistics
  TraceCompileStats stats;
};

// ---- The trace compiler --------------------------------------------------
// Takes ownership of the input graph (from lift_trace). Returns a
// CompiledTrace containing the optimized graph + scheduled MachineCFG.
// Returns nullptr if compilation fails (e.g., verifier fails).
//
// The `pgo` callback is used by the scheduler to pick hot paths at
// branches. Pass nullptr for a default "always true" PGO.
CompiledTrace compile_trace(dgw::Graph* graph,
                              dgw::PgoProbFn pgo = nullptr,
                              void* pgo_user = nullptr);

// Print a compiled trace for debugging.
void print_compiled_trace(const CompiledTrace& ct);

}  // namespace dvm
