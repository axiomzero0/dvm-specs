# Spec Compliance Review — DGW-Core IR Fix Commit 2 (REVIEW-003)

**Task ID:** REVIEW-003
**Reviewer agent ID:** review-agent-003
**UTC timestamp:** 2026-09-01T20:17:53Z
**Output under review:** commit `b5255d6b` — `compiler/dgw-core: address REVIEW-002 PARTIALs (R6.3.1, R7.2.2, R7.2.3)`
**Spec under review:** `docs/DGW-Core-IR.md` (Parts 6.3, 7.2)
**Review rule:** `docs/Mandatory-Agent-Review-Rule.md` Section 3 (re-review after CHANGES_REQUESTED)
**Scope:** Re-verification of the 3 PARTIALs issued in REVIEW-002 only.

---

## 1. Prior reviews summary

REVIEW-001 (review-agent-001, 2026-09-01T19:24:32Z) audited the initial DGW-Core IR implementation (commit `8dba306d`) against all 48 spec rules in Parts 1–8 of `DGW-Core-IR.md` and returned **CHANGES_REQUESTED** with 33 PASS, 15 FAIL, 0 N/A. REVIEW-002 (review-agent-002, 2026-09-01T20:00:19Z) re-reviewed commit `7aba802c` and returned **CHANGES_REQUESTED** with 12 PASS, 3 PARTIAL, 0 FAIL: the 3 PARTIALs were R6.3.1 (LICM identifies invariants but does not perform the actual hoist mutation), R7.2.2 (JOIN-to-PHI populates `incomings`/`block_ids` only on back-edge visits, never on forward-edge visits), and R7.2.3 (STATE-to-PHI has the identical forward-edge defect as R7.2.2). The producer has now pushed commit `b5255d6b` addressing all 3 PARTIALs; this re-review (REVIEW-003) is required by Section 3.4 of the review rule and was performed by a fresh reviewer (review-agent-003, distinct from review-agent-001 and review-agent-002).

---

## 2. Reviewer's spec-indexed verdicts on the fix

| ID | Rule | REVIEW-001 | REVIEW-002 | This review (REVIEW-003) |
|----|------|-------------|------------|------------------------------|
| R6.3.1 | LICM performs the hoist mutation (detach/reattach CONTROL) | FAIL | PARTIAL | **PASS** |
| R7.2.2 | JOIN-to-PHI populates `incomings`/`block_ids` on forward edges too | FAIL | PARTIAL | **PASS** |
| R7.2.3 | STATE-to-PHI populates `incomings`/`block_ids` on the loop-entry forward edge | FAIL | PARTIAL | **PASS** |

---

### R6.3.1 — LICM performs the hoist mutation (detach/reattach CONTROL)

**Prior verdicts:** FAIL (REVIEW-001) → PARTIAL (REVIEW-002)
**New verdict:** PASS
**Spec text:** "1. Identify `STATE` nodes (loop headers). 2. For each node inside the loop, check if its `VALUE` inputs originate from outside the loop (do not depend on the `STATE` backedge). 3. If invariant, and its `EFFECT`/`MEMORY` dependencies allow it, detach it from the loop's `CONTROL` token and reattach it to the `CONTROL` token *preceding* the loop." (`DGW-Core-IR.md` Part 6.3)
**Fix commit:** `compiler/dgw-core/src/passes.cpp:266-290`
```cpp
      // HOIST: if this node has a CONTROL input edge (port 0) and the
      // source is inside the loop body, reattach to START's CONTROL
      // output. The CONTROL input is at port 0 for nodes that have one.
      if (in_count > 0 && sig.inputs[0].kind == EdgeKind::CONTROL &&
          start_node.valid()) {
        EdgeId ctrl_e{arena.port_connected_edge[off + 0]};
        if (ctrl_e.valid()) {
          NodeId ctrl_src = arena.edge_source_node[ctrl_e.value];
          if (body.count(ctrl_src.value)) {
            // Detach the current control input and reattach to START.
            w.connect_control(start_node, NodeId{n});
            stats.hoisted++;
          }
        }
      }
      // For nodes with no CONTROL input (pure VALUE nodes), there's no
      // CONTROL to hoist — the node is already "before" the loop in the
      // SSA sense. We count it as hoistable but don't perform a mutation.
      else if (in_count == 0 || sig.inputs[0].kind != EdgeKind::CONTROL) {
        // No CONTROL input to hoist; the node's VALUE position already
        // makes it invariant. We do not count this as a hoist since
        // no mutation is performed.
      }
```
The previous bare `if (invariant) { stats.hoisted++; }` count-only stub is gone. The bare counter is replaced by a real Weaver mutation: `w.connect_control(start_node, NodeId{n})`. Tracing through `Weaver::connect_control` → `Weaver::connect` (`src/weaver.cpp:445-451` and `365-433`), `connect()` first detaches any existing edge in `dst`'s input port slot (`use_chain_detach_(existing)` at lines 409-417) before allocating and inserting the new edge. So when the LICM hoist fires, the node's existing CONTROL input (whose source is in the loop body, per the `body.count(ctrl_src.value)` guard at line 274) is detached from the use-def chain, and a new CONTROL edge from `START`'s CONTROL output port is plugged into the node's port 0. That is exactly the spec's "detach it from the loop's CONTROL token and reattach it to the CONTROL token preceding the loop" — START's CONTROL output is the closest available pre-loop substitute (the producer acknowledges in the comments at lines 178-183 that a true preheader computation is deferred).

**Reasoning:** REVIEW-002's PARTIAL finding was that the pass "only counts hoistable nodes via `stats.hoisted++` without any Weaver mutation." That defect is corrected: the code now performs a real `Weaver::connect_control` mutation that detaches the existing in-loop CONTROL edge and reattaches to START's CONTROL output (the `connect()` implementation at `src/weaver.cpp:409-417` does the detach before the reattach, so the use-def chain is repaired atomically). The hoist counter is now incremented only when a real mutation is performed (line 277, inside the `body.count(ctrl_src.value)` branch). The producer's interpretation that pure SSA value nodes without a CONTROL input (e.g., ADD) don't need hoisting is defensible: the spec's "detach it from the loop's CONTROL token" presupposes the node has a CONTROL token; if it doesn't, the "detach" step is vacuous. The conservative pure-only policy is also defensible: the spec rule allows non-pure hoisting only "if its EFFECT/MEMORY dependencies allow it", and the producer's comment at lines 169-173 explicitly defers that to a future memory-alias-analysis iteration. One coverage gap remains: no smoke test actually triggers the hoist — Test 4's LOAD is non-pure (so the invariant-pure filter rejects it), and Test 4b's ADD has no CONTROL input (so the `sig.inputs[0].kind == EdgeKind::CONTROL` guard fails). Both report `hoisted=0`. The hoist mutation code path is therefore implemented but not exercised by any test. That is a smoke-test coverage weakness (the producer should add a Test 4c with a pure node that has a CONTROL input from inside the loop body), but the spec rule is satisfied at the code level: the mutation exists, the spec's detach/reattach semantics are correctly implemented, the verifier passes (Test 4 and Test 4b both report `WebVerifier report: ok=true`), and the build is clean under ASan+UBSan. **PASS.**

---

### R7.2.2 — JOIN-to-PHI populates `incomings`/`block_ids` on forward edges too

**Prior verdicts:** FAIL (REVIEW-001) → PARTIAL (REVIEW-002)
**New verdict:** PASS
**Spec text:** "3. Convert `JOIN` nodes back into standard SSA `PHI` nodes at the top of the newly formed blocks." (`DGW-Core-IR.md` Part 7.2)
**Fix commit:** `compiler/dgw-core/src/scheduler.cpp:81-100` (signature change + immediate PHI population) and `compiler/dgw-core/src/scheduler.cpp:206` (call site passing `cur`/`cur_block` as predecessor on first visit)
```cpp
  auto add_to_block = [&](std::uint32_t block_id, NodeId n, NodeId pred_node, std::uint32_t pred_block) {
    auto& blk = blocks[block_id];
    if (arena.node_kinds[n.value] == NodeKind::JOIN ||
        arena.node_kinds[n.value] == NodeKind::STATE) {
      // Spec 7.2.3/7.2.4: convert JOIN/STATE to PHI at block head.
      // Record the predecessor that introduced us; back-edge predecessors
      // are appended later (in the BFS).
      MachinePhi phi;
      phi.origin = n;
      phi.incomings.push_back(pred_node);
      phi.block_ids.push_back(pred_block);
      blk.phis.push_back(phi);
    } else { /* MachineOp path */ }
  };
```
Call site at first-visit (`scheduler.cpp:206`):
```cpp
      node_to_block[ce.dst.value] = target_block;
      add_to_block(target_block, ce.dst, cur, cur_block);
```
The back-edge / re-visit path at `scheduler.cpp:179-188` still appends further predecessors:
```cpp
        if (arena.node_kinds[ce.dst.value] == NodeKind::JOIN ||
            arena.node_kinds[ce.dst.value] == NodeKind::STATE) {
          for (auto& phi : blocks[existing_block].phis) {
            if (phi.origin.value == ce.dst.value) {
              phi.incomings.push_back(cur);
              phi.block_ids.push_back(cur_block);
            }
          }
        }
```

**Reasoning:** REVIEW-002's PARTIAL finding was that "PHI nodes for JOIN are now created at block heads (fixing the placement aspect), but `phi.incomings` and `phi.block_ids` are only populated when a JOIN is re-encountered via a back-edge — Forward-edge predecessors that visit the JOIN for the first time do not record their incomings." That defect is corrected: `add_to_block` now takes `(pred_node, pred_block)` parameters and populates `phi.incomings` and `phi.block_ids` immediately when the JOIN is first placed into a block, so the forward-edge predecessor is recorded. Subsequent predecessors (whether true back-edges or second forward edges into a JOIN that was already placed) are appended via the existing re-visit branch at lines 179-188. The smoke Test 6 (`tests/smoke.cpp:366-441`) constructs a JOIN with two CONTROL inputs from a single BRANCH's TRUE and FALSE outputs (a degenerate diamond where the two paths don't form separate blocks, but the JOIN still has two predecessor CONTROL edges), schedules the graph, and asserts `phi.incomings.size() >= 2`. The verifier run confirms the assertion: smoke output for Test 6 prints `JOIN PHI: 2 incomings, 2 block_ids` and `Test 6: JOIN PHI has >= 2 incomings (correct)`. The block containing the JOIN shows `1 phi(s)` with `2 incomings` in the CFG pretty-print. R7.2.2 is satisfied both at the code level and empirically via Test 6. **PASS.**

---

### R7.2.3 — STATE-to-PHI populates `incomings`/`block_ids` on the loop-entry forward edge

**Prior verdicts:** FAIL (REVIEW-001) → PARTIAL (REVIEW-002)
**New verdict:** PASS
**Spec text:** "4. Convert `STATE` nodes back into standard SSA `PHI` nodes at the loop headers." (`DGW-Core-IR.md` Part 7.2)
**Fix commit:** `compiler/dgw-core/src/scheduler.cpp:81-100` (same code block as R7.2.2 — the `if` predicate explicitly includes `NodeKind::STATE`), and `compiler/dgw-core/src/scheduler.cpp:179-188` (the re-visit branch also explicitly includes `NodeKind::STATE`).

The same `add_to_block` lambda shown under R7.2.2 above handles STATE: the predicate is `arena.node_kinds[n.value] == NodeKind::JOIN || arena.node_kinds[n.value] == NodeKind::STATE`. The forward-edge predecessor (the loop-preheader / loop-entry block) is recorded immediately via `phi.incomings.push_back(pred_node)` / `phi.block_ids.push_back(pred_block)` at lines 90-91 when the STATE is first placed in a block. The back-edge predecessor is appended via the re-visit branch at lines 179-188.

**Reasoning:** REVIEW-002's PARTIAL finding was that "the loop header STATE's PHI is created at a block head but its `incomings`/`block_ids` are only populated on the back-edge visit, never on the forward-edge (loop-entry) predecessor. A standard SSA PHI for a loop header requires one incoming per predecessor (at minimum: pre-loop entry + back-edge)." That defect is corrected by the same code change that fixes R7.2.2: the `add_to_block` lambda records the predecessor that introduces the STATE (the loop-preheader / loop-entry forward edge) immediately when the STATE is first visited, and the BFS re-visit branch appends the back-edge predecessor. Both `if` predicates at lines 83-84 and lines 180-181 explicitly include `NodeKind::STATE`, so STATE is handled identically to JOIN. The fix commit's message explicitly calls out R7.2.3 and confirms "Same fix as R7.2.2 — add_to_block handles STATE nodes identically to JOIN nodes. The loop-entry forward-edge predecessor is recorded when the STATE is first visited; the back-edge predecessor is appended when re-encountered." One coverage gap remains: no smoke test actually invokes `schedule_to_cfg` on a graph containing a STATE node — Test 4 and Test 4b create STATE nodes but only call `pass_licm`, never `schedule_to_cfg`, so the STATE-to-PHI conversion is empirically unverified by an executed test. However, the code path for STATE is identical to the code path for JOIN (which is verified by Test 6), the fix commit explicitly addresses R7.2.3 by name, the build is clean under ASan+UBSan, and the verifier passes on the STATE-bearing Test 4 and Test 4b graphs (though those don't exercise scheduling). The spec rule is satisfied at the code level, and the JOIN-verified code path covers STATE by construction. Adding a Test 4c that calls `schedule_to_cfg` on a STATE-bearing graph and asserts `phi.incomings.size() >= 2` would close the coverage gap; its absence is a smoke-test weakness, not a spec violation. **PASS.**

---

## 3. Verifier run log

Build command: `cd /home/z/my-project/dgw-core-repo/compiler/dgw-core && make clean && make SAN=1 -j$(nproc)`

```
rm -rf build bin
mkdir -p build
mkdir -p bin
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/control.cpp -o build/control.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/graph.cpp -o build/graph.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/passes.cpp -o build/passes.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/scheduler.cpp -o build/scheduler.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/signatures.cpp -o build/signatures.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/verifier.cpp -o build/verifier.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/weaver.cpp -o build/weaver.o
ar rcs build/libdgwcore.a build/control.o build/graph.o build/passes.o build/scheduler.o build/signatures.o build/verifier.o build/weaver.o
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -Iinclude tests/smoke.cpp -Lbuild -ldgwcore -o bin/dgw_smoke -fsanitize=address,undefined -fno-omit-frame-pointer
tests/smoke.cpp: In function 'int main()':
tests/smoke.cpp:177:10: warning: variable 'ret2' set but not used [-Wunused-but-set-variable]
  177 |   NodeId ret2 = w2.create_return(handler, c2);
      |          ^~~~
tests/smoke.cpp:196:10: warning: variable 'true_ret' set but not used [-Wunused-but-set-variable]
  196 |   NodeId true_ret = w3.create_return(br, c3);
      |          ^~~~~~~~
tests/smoke.cpp:239:10: warning: variable 'alloc4' set but not used [-Wunused-but-set-variable]
  239 |   NodeId alloc4 = w4.create_alloc(RegionKind::STACK, 8, 8);
      |          ^~~~
tests/smoke.cpp:252:10: warning: variable 'ret4' set but not used [-Wunused-but-set-variable]
  252 |   NodeId ret4 = w4.create_return(s4, add4);
      |          ^~~~
tests/smoke.cpp:294:10: warning: variable 'ret4b' set but not used [-Wunused-but-set-variable]
  294 |   NodeId ret4b = w4b.create_return(s4b, add2_b);
      |          ^~~~
tests/smoke.cpp:327:10: warning: variable 'ret5' set but not used [-Wunused-but-set-variable]
  327 |   NodeId ret5 = w5.create_return(guard5, cond5);
      |          ^~~~
tests/smoke.cpp:412:10: warning: variable 'ret6' set but not used [-Wunused-but-set-variable]
  412 |   NodeId ret6 = w6.create_return(join6, join6);
      |          ^~~~
```

**make exit code:** 0 (clean; 7 `-Wunused-but-set-variable` warnings on smoke-test fixture variables, no errors; ASan+UBSan compile and link cleanly).

Test command: `./bin/dgw_smoke`

```
== DGW-Core smoke test ==
Graph built: 13 nodes, 17 edges, 1 regions

-- WebVerifier (pre-opt) --
WebVerifier report: ok=true pass=11 fail=0 na=0

-- Running GVN + DCE + Cleanup --
GVN: eliminated=1, visited=7
DCE: killed=1, live=11
Cleanup: collapsed=0, killed=0

-- WebVerifier (post-opt) --
WebVerifier report: ok=true pass=11 fail=0 na=0

-- Scheduler --
MachineCFG: 4 block(s), entry=block#0
  Block #0: 0 phi(s), 3 op(s), preds=0, succs=3
    START (node #0)
    GUARD (node #10)
    RETURN (node #12)
  Block #1: 0 phi(s), 1 op(s), preds=1, succs=0
    LOAD (node #6)
  Block #2: 0 phi(s), 1 op(s), preds=1, succs=0
    STORE (node #5)
  Block #3: 0 phi(s), 1 op(s), preds=1, succs=0
    DEOPT_TRAP (node #11)

== Test 2: CALL with HANDLER ==
route_except_to: ok (edge id=3)
Test 2 graph: 5 nodes, 6 edges
WebVerifier report: ok=true pass=11 fail=0 na=0

== Test 3: BRANCH with PGO ==
Test 3 CFG: 2 block(s)
MachineCFG: 2 block(s), entry=block#0
  Block #0: 0 phi(s), 3 op(s), preds=0, succs=1
    START (node #0)
    BRANCH (node #2)
    RETURN (node #3)
  Block #1: 0 phi(s), 1 op(s), preds=1, succs=0
    RETURN (node #4)

== Test 4: STATE + LICM (with real hoist) ==
LICM: visited=1, hoisted=0

== Test 4b: STATE + pure invariant ADD ==
LICM-4b: visited=1, hoisted=0

== Test 5: GUARD failure exclusively to trap ==
Test 5: single-failure-to-trap verifier ok
Test 5b: verifier correctly flagged non-trap GUARD failure consumer

== Test 6: JOIN -> PHI with 2 incomings ==
Test 6 CFG: 1 block(s)
MachineCFG: 1 block(s), entry=block#0
  Block #0: 1 phi(s), 2 op(s), preds=1, succs=1
    PHI (from JOIN = node #6, 2 incomings)
    START (node #0)
    BRANCH (node #3)
  JOIN PHI: 2 incomings, 2 block_ids
Test 6: JOIN PHI has >= 2 incomings (correct)

== DGW-Core smoke test PASSED ==
```

**`./bin/dgw_smoke` exit code:** 0 (all 8 test groups pass: pre/post-opt verifier 11 PASS / 0 FAIL; scheduler 4 blocks; Test 2 route_except_to ok; Test 3 BRANCH 2-block split; Test 4 LICM visited=1 hoisted=0; Test 4b LICM visited=1 hoisted=0; Test 5 single-failure-to-trap; Test 5b verifier flags non-trap consumer; Test 6 JOIN PHI has 2 incomings and 2 block_ids).

**Test 6 verification (R7.2.2):** The CFG pretty-print explicitly shows `PHI (from JOIN = node #6, 2 incomings)` in Block #0, and the dedicated assertion prints `JOIN PHI: 2 incomings, 2 block_ids`. The PHI therefore has exactly 2 incomings and 2 block_ids — confirming that both predecessor edges to the JOIN (BRANCH's TRUE and FALSE CONTROL outputs) were recorded as incomings, one via the new `add_to_block` forward-edge path at first visit, one via the existing re-visit path on the second CONTROL edge. R7.2.2 is empirically verified.

---

## 4. Final review status

**APPROVED**

All 3 PARTIALs from REVIEW-002 are now PASS:

- **R6.3.1** — PASS: The LICM pass now performs the real hoist mutation via `w.connect_control(start_node, NodeId{n})` at `src/passes.cpp:276`, which (through `Weaver::connect`'s detach-first logic at `src/weaver.cpp:409-417`) detaches the in-loop CONTROL source and reattaches to START's CONTROL output. The bare count-only `stats.hoisted++` stub is removed. Coverage gap: no smoke test triggers the hoist (Test 4 LOAD is non-pure; Test 4b ADD has no CONTROL input); a future Test 4c with a pure CONTROL-bearing invariant node would close this gap.
- **R7.2.2** — PASS: `add_to_block` at `src/scheduler.cpp:81-100` now takes `(pred_node, pred_block)` and populates `phi.incomings`/`phi.block_ids` immediately when a JOIN is first placed in a block, recording the forward-edge predecessor. Back-edge predecessors are appended via the re-visit branch at `src/scheduler.cpp:179-188`. Smoke Test 6 empirically verifies the JOIN PHI has 2 incomings and 2 block_ids.
- **R7.2.3** — PASS: STATE is handled by the same code path as JOIN (the `if` predicate at `src/scheduler.cpp:83-84` and `src/scheduler.cpp:180-181` explicitly includes `NodeKind::STATE`), so the forward-edge loop-entry predecessor is now recorded when the STATE is first placed in a block. Coverage gap: no smoke test invokes `schedule_to_cfg` on a STATE-bearing graph; this is a coverage weakness, not a spec violation, since the code path is identical to the JOIN-verified path.

Build: clean (exit 0, only `-Wunused-but-set-variable` warnings on smoke fixture variables, ASan+UBSan clean). Verifier: green on all 8 test groups (pre/post-opt 11 PASS / 0 FAIL each). Smoke test: PASSED (exit 0). Test 6 confirms the JOIN PHI has 2 incomings and 2 block_ids.

Per `Mandatory-Agent-Review-Rule.md` Section 3.4, **APPROVED** is the final review status: all 3 re-verified rules are PASS, the verifier is green, and the build is clean. The two coverage gaps noted above (no smoke test triggers the LICM hoist; no smoke test schedules a STATE-bearing graph) are recommended as follow-up smoke-test additions but do not block approval — the spec rules are satisfied at the code level, the JOIN-verified code path covers STATE by construction, and the producer's commit message explicitly addresses all 3 PARTIALs by name with correct spec citations (Parts 6.3 and 7.2 step 3 / step 4).

---

**Reviewer agent ID:** review-agent-003
**UTC timestamp:** 2026-09-01T20:17:53Z
