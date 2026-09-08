// src/trace_compiler.cpp — Trace compiler implementation.
//
// Spec citation: DVM-Hybrid-Tracing-Architecture.md §12.3, §T-009.
// DGW-Core-IR.md Parts 6 (GVN, DCE, Cleanup) and 7 (scheduling).
//
// The trace compiler ties together the DGW-Core optimization passes
// and the scheduler to produce a compiled trace from a lifted DGW graph.
//
#include "dvm/trace_compiler.hpp"

#include <print>
#include <utility>

#include "dgw/graph.hpp"
#include "dgw/pass_gvn.hpp"
#include "dgw/pass_dce.hpp"
#include "dgw/pass_cleanup.hpp"
#include "dgw/scheduler.hpp"
#include "dgw/verifier.hpp"

namespace dvm {

// Default PGO callback: always prefers the TRUE path (probability 0.9).
// Used when no PGO data is available.
namespace {
double default_pgo(dgw::NodeId, void*) { return 0.9; }
}  // namespace

CompiledTrace compile_trace(dgw::Graph* graph,
                              dgw::PgoProbFn pgo,
                              void* pgo_user) {
  CompiledTrace ct;
  ct.graph.reset(graph);

  auto& w = ct.graph->weaver();
  auto& arena = ct.graph->arena();

  // Record pre-optimization graph size.
  ct.stats.nodes_before = arena.node_count();
  ct.stats.edges_before = arena.edge_count();

  // ---- Step 1-3: Run optimization passes (GVN + DCE + Cleanup) -----------
  // Graph::optimize_default() runs all three passes and verifies after.
  auto opt_stats = ct.graph->optimize_default();
  ct.stats.gvn     = opt_stats.gvn;
  ct.stats.dce     = opt_stats.dce;
  ct.stats.cleanup = opt_stats.cleanup;

  // Record post-optimization graph size.
  ct.stats.nodes_after = arena.node_count();
  ct.stats.edges_after = arena.edge_count();

  // ---- Step 4: Schedule to MachineCFG ------------------------------------
  // Use the provided PGO callback or the default.
  dgw::PgoProbFn pgo_fn = pgo ? pgo : default_pgo;
  ct.cfg = dgw::schedule_to_cfg(w, pgo_fn, pgo_user);

  // Count blocks and ops.
  ct.stats.blocks = static_cast<std::uint32_t>(ct.cfg.blocks.size());
  for (const auto& blk : ct.cfg.blocks) {
    ct.stats.ops += static_cast<std::uint32_t>(blk.ops.size());
  }

  // ---- Step 5: Verify the optimized graph --------------------------------
  auto report = ct.graph->verify();
  ct.stats.verifier_ok   = report.ok;
  ct.stats.verifier_pass = report.pass_count;
  ct.stats.verifier_fail = report.fail_count;

  return ct;
}

void print_compiled_trace(const CompiledTrace& ct) {
  const auto& s = ct.stats;
  std::println("CompiledTrace:");
  std::println("  GVN: eliminated={}, visited={}", s.gvn.eliminated, s.gvn.visited);
  std::println("  DCE: killed={}, live={}", s.dce.killed, s.dce.live);
  std::println("  Cleanup: collapsed={}, killed={}", s.cleanup.collapsed, s.cleanup.killed);
  std::println("  Graph: {}→{} nodes, {}→{} edges",
               s.nodes_before, s.nodes_after,
               s.edges_before, s.edges_after);
  std::println("  Scheduled: {} blocks, {} ops", s.blocks, s.ops);
  std::println("  Verifier: ok={} pass={} fail={}",
               s.verifier_ok, s.verifier_pass, s.verifier_fail);

  if (s.verifier_fail > 0) {
    std::println("  WARNING: verifier reported failures — compiled trace may be invalid");
  }

  // Print the MachineCFG.
  if (s.blocks > 0) {
    dgw::print_cfg(ct.cfg, &ct.graph->arena());
  }
}

}  // namespace dvm
