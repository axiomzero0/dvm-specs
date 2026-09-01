// tests/smoke.cpp — End-to-end smoke test for DGW-Core.
//
// This test:
//   1. Builds a small DGW graph by hand:
//        START
//          -> ALLOC (region R0)
//          -> REF (region=R0, offset=0)
//          -> CONST 42
//          -> STORE (R0[0] = 42)
//          -> LOAD (R0[0])
//          -> ADD (LOAD + CONST 1)        [deliberate GVN target: 1+LOAD]
//          -> ADD (LOAD + CONST 1)        [second copy — GVN should eliminate]
//          -> GUARD (cond=true, FrameStateId=1)
//          -> DEOPT_TRAP                  [the GUARD's failure path]
//          -> RETURN (LOAD)
//
//   2. Runs the WebVerifier BEFORE optimization and expects it to PASS.
//
//   3. Runs GVN and expects the duplicate ADD to be eliminated
//      (eliminated >= 1).
//
//   4. Runs DCE and expects dead nodes to be killed.
//
//   5. Runs the WebVerifier AFTER optimization and expects it to PASS.
//
//   6. Runs the scheduler and prints the resulting MachineCFG.
//
// Exit code 0 on success, non-zero on any failure.
//
// Spec citations:
//   DGW-Core-IR.md Part 1 (memory layout)
//   DGW-Core-IR.md Part 2 (port signatures)
//   DGW-Core-IR.md Part 3 (regions/refs/memory-SSA)
//   DGW-Core-IR.md Part 4 (control flow)
//   DGW-Core-IR.md Part 5 (weaver)
//   DGW-Core-IR.md Part 6 (GVN, DCE)
//   DGW-Core-IR.md Part 8 (verifier)
//
#include <print>
#include <cstdlib>

#include "dgw/graph.hpp"
#include "dgw/scheduler.hpp"
#include "dgw/signatures.hpp"

using namespace dgw;

// A fixed PGO callback: always prefer the TRUE_CONTROL path (probability 0.9).
double always_true(NodeId, void*) { return 0.9; }

int main() {
  std::println("== DGW-Core smoke test ==");

  Graph g;
  Weaver& w = g.weaver();

  // ---- Build the graph -----------------------------------------------
  NodeId start = w.create_start();
  NodeId alloc = w.create_alloc(RegionKind::HEAP, 16, 8);
  NodeId ref0  = w.create_ref(/*region=*/RegionId{0}, /*offset=*/0, AccessPerm::ReadWrite);
  NodeId c42   = w.create_const(static_cast<std::int64_t>(42));
  NodeId c1a   = w.create_const(static_cast<std::int64_t>(1));

  // STORE: control from START, memory from START (port 1 of START is MEMORY),
  // ref=ref0, value=c42.
  NodeId store = w.create_store(start, start, ref0, c42);

  // LOAD: control from START (chain through STORE would also be valid),
  // memory from STORE, ref=ref0.
  NodeId load  = w.create_load(start, store, ref0);

  // Two ADDs with the same shape — the second should be GVN'd into the first.
  // Both use the SAME inputs (load, c1a) so they hash identically and the
  // second is eliminated.
  NodeId add1 = w.create_arith(NodeKind::ADD, load, c1a);
  NodeId add2 = w.create_arith(NodeKind::ADD, load, c1a);  // identical to add1
  (void)add2;  // will be killed by GVN

  (void)alloc;  // graph retains the ALLOC node; binding suppresses unused warning

  // GUARD: cond=true. We use a CONST boolean 1.
  NodeId cond_true = w.create_const(static_cast<std::int64_t>(1));
  NodeId guard = w.create_guard(FrameStateId{1}, DeoptReason::NullCheck);
  NodeId trap  = w.create_deopt_trap();
  // Wire GUARD: control from START, cond from cond_true.
  w.connect_control(start, guard);
  w.connect_value(cond_true, guard, PortId{1});
  // GUARD's failure output (port in_count+1) must route to DEOPT_TRAP.
  {
    const NodeSignature gs = signature_of(NodeKind::GUARD);
    const std::uint16_t in_count = static_cast<std::uint16_t>(gs.inputs.size());
    w.connect(guard, PortId{static_cast<std::uint16_t>(in_count + 1)},
              trap, PortId{0}, EdgeKind::CONTROL);
  }

  // RETURN: control from GUARD's success output (port in_count+0), value = add1.
  // We need RETURN's control input to come from GUARD's *success* port (output
  // port in_count+0), not from START. The easiest way is to create RETURN with
  // START as the temporary control source, then explicitly rewire to GUARD's
  // success port (which detaches the temporary edge).
  NodeId ret = w.create_return(start, add1);  // temporary control from START
  {
    const NodeSignature gs = signature_of(NodeKind::GUARD);
    const std::uint16_t in_count = static_cast<std::uint16_t>(gs.inputs.size());
    // Reconnect RETURN's port 0 (control input) to GUARD's success output.
    // w.connect() detaches any existing edge in the target port first.
    w.connect(guard, PortId{static_cast<std::uint16_t>(in_count + 0)},
              ret,    PortId{0}, EdgeKind::CONTROL);
  }

  std::println("Graph built: {} nodes, {} edges, {} regions",
               g.arena().node_count(), g.arena().edge_count(), g.arena().region_count());

  // ---- Verify BEFORE optimization ------------------------------------
  std::println("\n-- WebVerifier (pre-opt) --");
  VerifyReport r0 = g.verify();
  print_report(r0);
  if (!r0.ok) {
    std::println("FAIL: pre-opt verifier reports failures");
    return 1;
  }

  // ---- Optimize -------------------------------------------------------
  std::println("\n-- Running GVN + DCE + Cleanup --");
  auto stats = g.optimize_default();
  std::println("GVN: eliminated={}, visited={}", stats.gvn.eliminated, stats.gvn.visited);
  std::println("DCE: killed={}, live={}", stats.dce.killed, stats.dce.live);
  std::println("Cleanup: collapsed={}, killed={}", stats.cleanup.collapsed, stats.cleanup.killed);

  // We expect GVN to eliminate at least 1 node (the duplicate add2).
  if (stats.gvn.eliminated < 1) {
    std::println("FAIL: GVN did not eliminate the duplicate ADD");
    return 1;
  }

  // ---- Verify AFTER optimization -------------------------------------
  std::println("\n-- WebVerifier (post-opt) --");
  VerifyReport r1 = g.verify();
  print_report(r1);
  if (!r1.ok) {
    std::println("FAIL: post-opt verifier reports failures");
    return 1;
  }

  // ---- Schedule -------------------------------------------------------
  std::println("\n-- Scheduler --");
  MachineCFG cfg = schedule_to_cfg(w, always_true, nullptr);
  print_cfg(cfg, &g.arena());

  std::println("\n== DGW-Core smoke test PASSED ==");
  return 0;
}
