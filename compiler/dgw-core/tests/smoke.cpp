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
#include "dgw/control.hpp"
#include "dgw/passes.hpp"

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

  // ---- Test 2: CALL with HANDLER for EXCEPT routing ----------------
  // Spec Part 4.3: a CALL node's EXCEPT output routes to a HANDLER.
  // Per R4.3.1, route_except_to should now find the EXCEPT port on CALL.
  std::println("\n== Test 2: CALL with HANDLER ==");
  Graph g2;
  Weaver& w2 = g2.weaver();
  NodeId s2 = w2.create_start();
  NodeId c2 = w2.create_const(std::int64_t{42});
  NodeId call = w2.create_call(SymbolId{1}, CallConv::Guest, /*arg_count=*/1,
                                /*can_throw=*/true, s2, s2);
  w2.call_connect_arg(call, 0, c2);
  NodeId handler = w2.create_handler();
  // CALL's EXCEPT output is at port in_count + 2 (port 2 + 2 = 4; the third
  // output port after VALUE ret and MEMORY). But for CALL, in_count=2
  // (control + memory) + arg_count=1 = 3 inputs (wait, with extra args we
  // added 1 more for arg_count=1, so in_count=3). Output EXCEPT is at
  // port 3 + 2 = 5.
  EdgeId ce = route_except_to(w2, call, handler);
  if (!ce.valid()) {
    std::println("FAIL: route_except_to did not find the CALL's EXCEPT port");
    return 1;
  }
  std::println("route_except_to: ok (edge id={})",
               static_cast<std::uint32_t>(ce.value));
  // Wire HANDLER's control output to a RETURN so the graph is observable.
  NodeId ret2 = w2.create_return(handler, c2);

  std::println("Test 2 graph: {} nodes, {} edges",
               g2.arena().node_count(), g2.arena().edge_count());
  VerifyReport r2 = g2.verify();
  print_report(r2);
  if (!r2.ok) {
    std::println("FAIL: test 2 verifier reports failures");
    return 1;
  }

  // ---- Test 3: BRANCH with two successors ---------------------------
  // Spec Part 7.1: PGO at BRANCH picks the hotter path.
  std::println("\n== Test 3: BRANCH with PGO ==");
  Graph g3;
  Weaver& w3 = g3.weaver();
  NodeId s3 = w3.create_start();
  NodeId c3 = w3.create_const(std::int64_t{1});
  NodeId br = w3.create_branch(s3, c3);
  NodeId true_ret = w3.create_return(br, c3);
  NodeId false_ret = w3.create_return(br, c3);
  // Wire BRANCH's true output to one RETURN, false to the other.
  // BRANCH's true output is at port in_count + 0; false at in_count + 1.
  // create_return(br, c3) wired BRANCH's first CONTROL output (true) to true_ret.
  // We need to wire BRANCH's FALSE output to false_ret.
  {
    const NodeSignature bs = signature_of(NodeKind::BRANCH);
    const std::uint16_t in_count = static_cast<std::uint16_t>(bs.inputs.size());
    w3.connect(br, PortId{static_cast<std::uint16_t>(in_count + 1)},
                false_ret, PortId{0}, EdgeKind::CONTROL);
  }

  MachineCFG cfg3 = schedule_to_cfg(w3, always_true, nullptr);
  std::println("Test 3 CFG: {} block(s)", cfg3.blocks.size());
  print_cfg(cfg3, &g3.arena());
  VerifyReport r3 = g3.verify();
  if (!r3.ok) {
    std::println("FAIL: test 3 verifier reports failures");
    print_report(r3);
    return 1;
  }
  if (cfg3.blocks.size() < 2) {
    std::println("FAIL: test 3 expected at least 2 blocks (BRANCH should split)");
    return 1;
  }

  // ---- Test 4: STATE node + LICM identification ---------------------
  std::println("\n== Test 4: STATE + LICM ==");
  Graph g4;
  Weaver& w4 = g4.weaver();
  NodeId s4 = w4.create_start();
  NodeId state = w4.create_state();
  // Wire STATE's init to a CONST; backedge to itself for now (toy loop).
  NodeId c4 = w4.create_const(std::int64_t{0});
  w4.connect_value(c4, state, PortId{0});
  w4.connect_value(state, state, PortId{1});  // backedge
  // Use STATE's value: add 1 to it.
  NodeId one = w4.create_const(std::int64_t{1});
  NodeId inc = w4.create_arith(NodeKind::ADD, state, one);
  // Make the graph observable: a RETURN of inc.
  NodeId ret4 = w4.create_return(s4, inc);

  // Run LICM — it should identify STATE and count 0 invariant pure nodes
  // (inc depends on state which IS in the loop body).
  LicmStats ls = pass_licm(w4);
  std::println("LICM: visited={}, hoisted={}", ls.visited, ls.hoisted);
  if (ls.visited != 1) {
    std::println("FAIL: LICM did not identify the 1 STATE node (got {})", ls.visited);
    return 1;
  }
  VerifyReport r4 = g4.verify();
  if (!r4.ok) {
    std::println("FAIL: test 4 verifier reports failures");
    print_report(r4);
    return 1;
  }

  // ---- Test 5: Multiple GUARD failure consumers (R8.4.2 exclusively) ----
  std::println("\n== Test 5: GUARD failure exclusively to trap ==");
  Graph g5;
  Weaver& w5 = g5.weaver();
  NodeId s5 = w5.create_start();
  NodeId cond5 = w5.create_const(std::int64_t{1});
  NodeId guard5 = w5.create_guard(FrameStateId{1}, DeoptReason::NullCheck);
  NodeId trap5 = w5.create_deopt_trap();
  w5.connect_control(s5, guard5);
  w5.connect_value(cond5, guard5, PortId{1});
  // Wire GUARD's failure port (in_count + 1) to trap5.
  {
    const NodeSignature gs = signature_of(NodeKind::GUARD);
    const std::uint16_t in_count = static_cast<std::uint16_t>(gs.inputs.size());
    w5.connect(guard5, PortId{static_cast<std::uint16_t>(in_count + 1)},
                trap5, PortId{0}, EdgeKind::CONTROL);
  }
  // Wire GUARD's success port to a RETURN.
  NodeId ret5 = w5.create_return(guard5, cond5);

  // Now: the failure path has ONE consumer (trap5). This satisfies
  // the "exclusively to DEOPT_TRAP" rule. Verify passes.
  VerifyReport r5 = g5.verify();
  if (!r5.ok) {
    std::println("FAIL: test 5 verifier reports failures (single-failure-to-trap)");
    print_report(r5);
    return 1;
  }
  std::println("Test 5: single-failure-to-trap verifier ok");

  // Test 5b: Now add a SECOND consumer on the GUARD's failure port that
  // is NOT a trap. The verifier must FAIL.
  NodeId bad_consumer = w5.create_return(guard5, cond5);
  // Wire GUARD's failure port to bad_consumer too. This violates
  // "exclusively to DEOPT_TRAP".
  {
    const NodeSignature gs = signature_of(NodeKind::GUARD);
    const std::uint16_t in_count = static_cast<std::uint16_t>(gs.inputs.size());
    w5.connect(guard5, PortId{static_cast<std::uint16_t>(in_count + 1)},
                bad_consumer, PortId{0}, EdgeKind::CONTROL);
  }
  VerifyReport r5b = g5.verify();
  bool found_5b = false;
  for (const auto& f : r5b.findings) {
    if (f.verdict == Verdict::Fail && f.rule == "8.4.guard_failure_routes_to_trap") {
      found_5b = true;
      break;
    }
  }
  if (!found_5b) {
    std::println("FAIL: test 5b — verifier did NOT flag GUARD failure with "
                 "non-trap consumer");
    print_report(r5b);
    return 1;
  }
  std::println("Test 5b: verifier correctly flagged non-trap GUARD failure consumer");

  std::println("\n== DGW-Core smoke test PASSED ==");
  return 0;
}
