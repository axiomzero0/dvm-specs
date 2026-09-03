# REVIEW-005 — Spec compliance review of the `-Werror` warning-policy change

**Producer commit:** `f4322544` — `compiler/dgw-core: all warnings are errors (-Werror); fix unused-but-set`
**Files touched:** `compiler/dgw-core/Makefile` (+5/-2), `compiler/dgw-core/tests/smoke.cpp` (+18/-11)
**Reviewer agent ID:** `review-agent-005` (fresh reviewer per Mandatory-Agent-Review-Rule.md §3.4; not the producer, not review-agent-001/002/003/004)
**UTC timestamp:** `2026-09-01T21:26:47Z`

---

## 1. Prior reviews summary

REVIEW-001 (review-agent-001) reviewed the initial DGW-Core IR implementation (commit `8dba306d`) and returned **CHANGES_REQUESTED** with 33 PASS / 15 FAIL / 0 N/A across 48 spec rules from Parts 1–8 of `DGW-Core-IR.md`. REVIEW-002 (review-agent-002) reviewed the fix commit `7aba802c` and returned **CHANGES_REQUESTED** with 12 PASS / 3 PARTIAL / 0 FAIL (PARTIALs: R6.3.1 LICM no hoist mutation, R7.2.2 JOIN-to-PHI forward-edge incomings missing, R7.2.3 STATE-to-PHI forward-edge incomings missing). REVIEW-003 (review-agent-003) reviewed the fix commit `b5255d6b` for those 3 PARTIALs and returned **APPROVED** with 3 PASS / 0 PARTIAL / 0 FAIL. REVIEW-004 (review-agent-004) reviewed the structural per-pass file split (commit `6da5afba`, +523/-415 across 13 files) and returned **APPROVED** with 7 PASS / 0 FAIL (per-pass function bodies byte-for-byte identical to old `passes.cpp`; only header comments and `#include` lines differ; smoke output byte-for-byte identical to REVIEW-003's recorded output). This REVIEW-005 is the first review of the `-Werror` warning-policy change introduced by commit `f4322544`.

---

## 2. Reviewer's verdicts on the warning-policy change

### Check 1 — Makefile change is correct

**Verdict:** PASS
**Evidence:** `compiler/dgw-core/Makefile:13`:
```
WARN     := -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter
```
`git diff HEAD~1 HEAD -- compiler/dgw-core/Makefile` confirms the only change is `-Werror=return-type` → `-Werror -Wno-unused-parameter` (plus a 3-line comment block at lines 10–12 explaining the policy). No other warning-related flags were added, removed, or modified. The `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef` set is unchanged from the previous commit.

**Reasoning:** The change satisfies the task's specification exactly: `-Werror` (turning all enabled warnings into errors) replaces `-Werror=return-type` (which only elevated missing-return), and `-Wno-unused-parameter` is the only exception — explicitly permitted because interface signatures like `PgoProbFn` callbacks frequently have intentionally-unused parameters. The comment block added at lines 10–12 makes the policy normative. No spurious flags were introduced (no `-Wno-foo` for any other warning class, no `-w`, no `-Wno-error=foo`).

### Check 2 — No warning suppressions slipped in

**Verdict:** FAIL
**Evidence:** Mandatory grep `grep -rn "pragma GCC diagnostic\|__attribute__\|__attribute__((unused))\|(void)" /home/z/my-project/dgw-core-repo/compiler/dgw-core/` returns four hits:

| File:line | Code | Type |
|-----------|------|------|
| `tests/smoke.cpp:79` | `(void)add2;  // will be killed by GVN` | `(void)var;` cast on local variable |
| `tests/smoke.cpp:81` | `(void)alloc;  // graph retains the ALLOC node; binding suppresses unused warning` | `(void)var;` cast on local variable |
| `include/dgw/util.hpp:74` | `(void)cond; (void)msg; (void)loc;` (inside `#else` branch of `#if DGW_ALWAYS_VERIFY`) | `(void)param;` casts on function parameters |
| `include/dgw/arena.hpp:127` | `DGW_ALWAYS_VERIFY ? (void)0 : (void)0;` | degenerate `(void)0` ternary (dead code; not a `(void)var;` cast on a variable) |

`git log -S` confirms all four were introduced by commit `8dba306` (REVIEW-001's commit) and are NOT introduced by the producer's fix commit `f4322544`. The producer's diff against `HEAD~1` does not add, modify, or delete any of these four lines.

**Reasoning:** The check's literal text is unambiguous: "there must be no `#pragma GCC diagnostic ignored`, no `__attribute__((unused))`, no `(void)var;` casts, no other lazy warning-suppression in `tests/smoke.cpp` or any `src/*.cpp` or `include/dgw/*.hpp`." The task's closing instruction is equally decisive: "any warning suppression that isn't the Makefile-level `-Wno-unused-parameter` is a FAIL." Three of the four grep hits match the forbidden `(void)var;` pattern on actual variables (`add2`, `alloc`, and the three parameters `cond`/`msg`/`loc` in `dgw_assert`):

- **`tests/smoke.cpp:79` (`(void)add2;`)** and **`tests/smoke.cpp:81` (`(void)alloc;`)** are textbook lazy warning-suppressions on local variables bound from `create_*()` calls in Test 1 — exactly the anti-pattern the producer's own commit message rails against ("No lazy unused-variable suppressions; if a variable is set but not used, either use it or delete it"). These are in the very file the producer modified, were known to be problematic under the new `-Werror` policy (they survive only because the `(void)` cast marks the variables as "used" and silences `-Wunused-but-set-variable`), and the producer had the perfect opportunity to clean them up while fixing the 7 sibling cases — but did not. The producer's commit message normative claim ("No lazy unused-variable suppressions") is therefore false for `tests/smoke.cpp`.

- **`include/dgw/util.hpp:74` (`(void)cond; (void)msg; (void)loc;`)** is the classic `(void)param;` pattern for unused function parameters inside `#if DGW_ALWAYS_VERIFY ... #else ... #endif`. With `-Wno-unused-parameter` now in the Makefile, these casts are fully redundant — the warning would not fire even without them. Their continued presence in `include/dgw/*.hpp` violates the literal scope of Check 2 ("any `src/*.cpp` or `include/dgw/*.hpp`"). The reviewer notes that this pattern was previously defensible (when `-Wno-unused-parameter` was not set, the cast was the only way to silence the warning); however, the producer's commit is precisely the warning-policy change that should have retired it.

- **`include/dgw/arena.hpp:127` (`DGW_ALWAYS_VERIFY ? (void)0 : (void)0;`)** is not a `(void)var;` cast on a variable — it is a degenerate ternary with both branches being `(void)0` (a cast of the literal `0`), which is dead code rather than a warning suppression. The reviewer notes this as a pre-existing dead-code defect but does not count it as a Check 2 violation; however, it should be cleaned up in the same warning-discipline pass.

The producer's commit did not "sneak in" any *new* warning suppressions (the diff adds zero new `(void)var;` casts, zero new `#pragma GCC diagnostic ignored`, zero new `__attribute__((unused))`). But Check 2 is not scoped to "diff-introduced suppressions"; it is scoped to the codebase state ("there must be no ... in `tests/smoke.cpp` or any `src/*.cpp` or `include/dgw/*.hpp`"). The codebase still contains three pre-existing `(void)var;` casts that the producer's warning-discipline commit was the natural moment to remove. Per the task's closing instruction, this is a FAIL.

### Check 3 — All 7 unused-but-set fixes are real

**Verdict:** PASS
**Evidence:** All 7 binding drops verified in `tests/smoke.cpp` at the following lines, with the underlying `create_*()` call preserved (so the node is still wired into the graph):

| Test | Line | New code (call as statement) | Was |
|------|------|------------------------------|-----|
| Test 2 | 177 | `w2.create_return(handler, c2);` | `NodeId ret2 = w2.create_return(handler, c2);` |
| Test 3 (true_ret) | 199 | `w3.create_return(br, c3);` | `NodeId true_ret = w3.create_return(br, c3);` |
| Test 4 (alloc4) | 243 | `w4.create_alloc(RegionKind::STACK, 8, 8);` | `NodeId alloc4 = w4.create_alloc(RegionKind::STACK, 8, 8);` |
| Test 4 (ret4) | 256 | `w4.create_return(s4, add4);` | `NodeId ret4 = w4.create_return(s4, add4);` |
| Test 4b (ret4b) | 298 | `w4b.create_return(s4b, add2_b);` | `NodeId ret4b = w4b.create_return(s4b, add2_b);` |
| Test 5 (ret5) | 331 | `w5.create_return(guard5, cond5);` | `NodeId ret5 = w5.create_return(guard5, cond5);` |
| Test 6 (ret6) | 416 | `w6.create_return(join6, join6);` | `NodeId ret6 = w6.create_return(join6, join6);` |

Test 3's `false_ret` is preserved at line 202 as `NodeId false_ret = w3.create_return(br, c3);` and is referenced on line 210 in `w3.connect(br, PortId{static_cast<std::uint16_t>(in_count + 1)}, false_ret, PortId{0}, EdgeKind::CONTROL);` — confirmed via `grep -n "false_ret\|true_ret" tests/smoke.cpp` returning 3 matches (lines 202, 203, 210), all for `false_ret`; `true_ret` is fully gone.

**Reasoning:** Each of the 7 binding drops removes the `NodeId <name> =` left-hand side while keeping the `w.create_*()` call intact, so the Weaver still pushes the new node into `node_kinds[]` and `edge_source_node[]` via `create_node()` and the auto-wire via `connect_control()` / `connect_value()`. The verifier and scheduler walk the arena, not the test locals, so removing the locals does not change observable behavior. Test 3's `false_ret` is correctly preserved because the test needs to wire BRANCH's FALSE output (port `in_count + 1`) to it on the next line — the producer correctly distinguished "genuinely unused" (drop) from "used on the next line" (keep).

### Check 4 — Test 3 still produces 2 RETURN nodes

**Verdict:** PASS
**Evidence:** `tests/smoke.cpp` Test 3 contains two separate `create_return` calls:
- Line 199: `w3.create_return(br, c3);` — unbound, creates RETURN node #3, auto-wires BRANCH's TRUE output (port `in_count + 0`) to RETURN #3's CONTROL input (via `Weaver::create_return` → `Weaver::connect_control` → `Weaver::connect` at `src/weaver.cpp:300-306` and `:445-451`).
- Line 202: `NodeId false_ret = w3.create_return(br, c3);` — bound, creates RETURN node #4, auto-wires BRANCH's TRUE output to RETURN #4's CONTROL input (same code path).
- Line 210: `w3.connect(br, PortId{in_count + 1}, false_ret, PortId{0}, EdgeKind::CONTROL);` — explicitly wires BRANCH's FALSE output (port `in_count + 1`) to RETURN #4 (`false_ret`).

Smoke-test output at runtime confirms 2 blocks for Test 3 (`Test 3 CFG: 2 block(s)`), with Block #0 holding `START` + `BRANCH` + `RETURN (node #3)` (1 op + 2 terminator-class), and Block #1 holding `RETURN (node #4)` — i.e. the BRANCH successfully splits the CFG into two blocks because it has two distinct RETURN successors. If the producer had accidentally deleted one RETURN, the BRANCH would have only one CONTROL-successor and `schedule_to_cfg` would not split, giving 1 block, and the test's `cfg3.blocks.size() < 2` assertion at line 222 would fire.

**Reasoning:** The fix preserves Test 3's two-RETURN-node semantics exactly. The unbound first `create_return(br, c3)` (line 199) is not a deletion of a RETURN; it is a deletion of the *local binding* `NodeId true_ret =`, while the underlying `create_return` call still executes and still constructs RETURN node #3 inside the Weaver arena. RETURN #4 (`false_ret`) is still bound (line 202) and is still explicitly wired to BRANCH's FALSE output (line 210). The BRANCH's TRUE output is auto-wired to RETURN #3 by `create_return → connect_control`, and the BRANCH's FALSE output is explicitly wired to RETURN #4 — same as before the fix.

### Check 5 — Build is clean under both `make` and `make SAN=1`

**Verdict:** PASS
**Evidence:**

```
$ cd /home/z/my-project/dgw-core-repo/compiler/dgw-core
$ make clean
rm -rf build bin
$ make -j$(nproc) 2>&1 | tail -5
ar rcs build/libdgwcore.a build/control.o build/graph.o build/pass_cleanup.o build/pass_dce.o build/pass_gvn.o build/pass_licm.o build/scheduler.o build/signatures.o build/verifier.o build/weaver.o
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g   -Iinclude tests/smoke.cpp -Lbuild -ldgwcore -o bin/dgw_smoke 
$ echo "EXIT: $?"
EXIT: 0

$ make clean
$ make SAN=1 -j$(nproc) 2>&1 | tail -5
ar rcs build/libdgwcore.a build/control.o build/graph.o build/pass_cleanup.o build/pass_dce.o build/pass_gvn.o build/pass_licm.o build/scheduler.o build/signatures.o build/verifier.o build/weaver.o
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -Iinclude tests/smoke.cpp -Lbuild -ldgwcore -o bin/dgw_smoke -fsanitize=address,undefined -fno-omit-frame-pointer
$ echo "EXIT: $?"
EXIT: 0
```

Both compile-and-link invocations show zero `-Werror`-promoted warnings and zero errors. The 7 prior `-Wunused-but-set-variable` warnings that REVIEW-003 and REVIEW-004 recorded (on `ret2`, `true_ret`, `alloc4`, `ret4`, `ret4b`, `ret5`, `ret6`) are gone — the producer's 7 binding drops removed them. The remaining `(void)add2;` and `(void)alloc;` casts in Test 1 (lines 79, 81) still silence the corresponding warnings via the cast-as-use idiom and do not trigger `-Werror`. ASan+UBSan (`-fsanitize=address,undefined`) compiles and links cleanly under both invocations.

**Reasoning:** The Makefile change elevates all warnings to errors and the producer's 7 binding drops fix every warning that would have been elevated. Both build modes (`make` and `make SAN=1`) exit 0 with zero warnings and zero errors. The build is clean.

### Check 6 — Smoke test still passes

**Verdict:** PASS
**Evidence:** `./bin/dgw_smoke` exits 0; full output:

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

Exit code: 0. All 8 test groups green (Test 1 = initial graph + verifier + passes + scheduler; Test 2 = CALL+HANDLER+EXCEPT; Test 3 = BRANCH+PGO 2-block CFG; Test 4 = STATE+LICM; Test 4b = pure invariant ADD; Test 5 = GUARD failure exclusively-to-trap; Test 5b = non-trap consumer flagged; Test 6 = JOIN PHI 2 incomings). Numerical results match REVIEW-003's and REVIEW-004's recorded values exactly:
- GVN eliminated=1, visited=7 ✓
- DCE killed=1, live=11 ✓
- Cleanup collapsed=0, killed=0 ✓
- WebVerifier pre-opt 11 PASS / 0 FAIL ✓
- WebVerifier post-opt 11 PASS / 0 FAIL ✓
- Scheduler Test 1: 4 blocks ✓
- Test 3: 2 blocks ✓
- Test 6: JOIN PHI 2 incomings / 2 block_ids ✓

**Reasoning:** All 8 smoke test groups pass with the same numerical results as prior reviews. The 7 binding drops have no observable effect on the test execution — node identities, edge counts, verifier findings, scheduler block counts, and PHI incomings are all unchanged. The exit code 0 confirms no smoke-test assertion fired.

### Check 7 — No behavior change

**Verdict:** PASS
**Evidence:** `git diff HEAD~1 HEAD -- compiler/dgw-core/tests/smoke.cpp` shows exactly the 7 binding drops and the Test 3 comment-block expansion, and nothing else:

| Hunk location | Change |
|---------------|--------|
| Line 177 (Test 2) | Drop `NodeId ret2 = ` from `w2.create_return(handler, c2);` |
| Lines 196–205 (Test 3) | Drop `NodeId true_ret = ` from first `w3.create_return(br, c3);`; expand comment block to explain auto-wire vs. explicit-wire; preserve `NodeId false_ret = w3.create_return(br, c3);` on line 202 |
| Line 243 (Test 4) | Drop `NodeId alloc4 = ` from `w4.create_alloc(RegionKind::STACK, 8, 8);` |
| Line 256 (Test 4) | Drop `NodeId ret4 = ` from `w4.create_return(s4, add4);` |
| Line 298 (Test 4b) | Drop `NodeId ret4b = ` from `w4b.create_return(s4b, add2_b);` |
| Line 331 (Test 5) | Drop `NodeId ret5 = ` from `w5.create_return(guard5, cond5);` |
| Line 416 (Test 6) | Drop `NodeId ret6 = ` from `w6.create_return(join6, join6);` |

`git diff HEAD~1 HEAD --stat` confirms only two files changed: `Makefile` (+5/-2) and `tests/smoke.cpp` (+18/-11). No other source files were touched. The Makefile diff is exactly the WARN line change + 3-line comment block (verified in Check 1). The smoke.cpp diff is exactly the 7 binding drops + the Test 3 comment block expansion (the comment block on lines 196–205 was rewritten to clarify that the first `create_return` auto-wires BRANCH's TRUE output and only the second `create_return` needs an explicit name for the FALSE-output wiring on line 210).

**Reasoning:** The diff is minimal and surgical. The only executable-code changes are the 7 `NodeId <name> =` LHS drops; the underlying `create_*()` calls are byte-for-byte identical (verified by `git show HEAD~1:.../smoke.cpp | diff` line-by-line). The comment-block expansion in Test 3 (lines 196–205) is prose-only — no executable statements added, removed, or modified, no edge rewiring, no port index changes. The `false_ret` binding is preserved because it is referenced on line 210; the other 6 bindings (`ret2`, `true_ret`, `alloc4`, `ret4`, `ret4b`, `ret5`, `ret6`) are dropped because they are never referenced after creation. No other logic changes.

---

## 3. Verifier run log

### 3.1 `make` (clean build, no sanitizers)

```
$ cd /home/z/my-project/dgw-core-repo/compiler/dgw-core
$ make clean
rm -rf build bin
$ make -j$(nproc) 2>&1 | tail -30
mkdir -p build
mkdir -p bin
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g   -c src/control.cpp -o build/control.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g   -c src/graph.cpp -o build/graph.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g   -c src/pass_cleanup.cpp -o build/pass_cleanup.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g   -c src/pass_dce.cpp -o build/pass_dce.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g   -c src/pass_gvn.cpp -o build/pass_gvn.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g   -c src/pass_licm.cpp -o build/pass_licm.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g   -c src/scheduler.cpp -o build/scheduler.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g   -c src/signatures.cpp -o build/signatures.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g   -c src/verifier.cpp -o build/verifier.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g   -c src/weaver.cpp -o build/weaver.o
ar rcs build/libdgwcore.a build/control.o build/graph.o build/pass_cleanup.o build/pass_dce.o build/pass_gvn.o build/pass_licm.o build/scheduler.o build/signatures.o build/verifier.o build/weaver.o
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g   -Iinclude tests/smoke.cpp -Lbuild -ldgwcore -o bin/dgw_smoke 
$ echo "EXIT: $?"
EXIT: 0
```

**Exit code:** 0. **Warnings:** 0. **Errors:** 0.

### 3.2 `make SAN=1` (clean build, ASan + UBSan)

```
$ make clean
rm -rf build bin
$ make SAN=1 -j$(nproc) 2>&1 | tail -30
mkdir -p build
mkdir -p bin
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/control.cpp -o build/control.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/graph.cpp -o build/graph.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/pass_cleanup.cpp -o build/pass_cleanup.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/pass_dce.cpp -o build/pass_dce.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/pass_gvn.cpp -o build/pass_gvn.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/pass_licm.cpp -o build/pass_licm.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/scheduler.cpp -o build/scheduler.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/signatures.cpp -o build/signatures.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/verifier.cpp -o build/verifier.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/weaver.cpp -o build/weaver.o
ar rcs build/libdgwcore.a build/control.o build/graph.o build/pass_cleanup.o build/pass_dce.o build/pass_gvn.o build/pass_licm.o build/scheduler.o build/signatures.o build/verifier.o build/weaver.o
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -Iinclude tests/smoke.cpp -Lbuild -ldgwcore -o bin/dgw_smoke -fsanitize=address,undefined -fno-omit-frame-pointer
$ echo "EXIT: $?"
EXIT: 0
```

**Exit code:** 0. **Warnings:** 0. **Errors:** 0. ASan+UBSan compile-and-link clean.

### 3.3 `./bin/dgw_smoke` (built under `make SAN=1`)

```
$ ./bin/dgw_smoke
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
$ echo "EXIT: $?"
EXIT: 0
```

**Exit code:** 0. No ASan or UBSan runtime errors. All 8 test groups green. WebVerifier pre/post-opt 11 PASS / 0 FAIL each. Numerical results match REVIEW-003's and REVIEW-004's recorded values exactly (GVN eliminated=1 visited=7; DCE killed=1 live=11; Cleanup collapsed=0 killed=0; scheduler Test 1: 4 blocks; Test 3: 2 blocks; Test 6: JOIN PHI 2 incomings / 2 block_ids).

---

## 4. Final review status

**CHANGES_REQUESTED**

The producer's warning-policy change commit `f4322544` correctly implements the Makefile change (Check 1 PASS), correctly drops all 7 unused-but-set NodeId bindings in `tests/smoke.cpp` while preserving the call sites and the `false_ret` binding that is referenced on the next line (Checks 3, 4, 7 PASS), builds clean under both `make` and `make SAN=1` (Check 5 PASS), and the smoke test passes with identical numerical results to the prior approved reviews (Check 6 PASS). The Test 3 BRANCH still produces 2 separate RETURN nodes (Check 4 PASS).

However, **Check 2 FAILs**: the mandatory `grep -rn "pragma GCC diagnostic\|__attribute__\|__attribute__((unused))\|(void)"` over `compiler/dgw-core/` returns four hits, three of which are genuine `(void)var;` casts on actual variables:

1. `tests/smoke.cpp:79` — `(void)add2;` — lazy warning-suppression cast on the `add2` local in Test 1
2. `tests/smoke.cpp:81` — `(void)alloc;` — lazy warning-suppression cast on the `alloc` local in Test 1
3. `include/dgw/util.hpp:74` — `(void)cond; (void)msg; (void)loc;` — `(void)param;` casts on `dgw_assert`'s parameters (now redundant given `-Wno-unused-parameter`)

All three were introduced by the very first commit `8dba306` (REVIEW-001's commit) and are NOT added by the producer's fix commit. The producer's commit did not "sneak in" any *new* warning suppressions (the diff adds zero new `(void)var;` casts, zero new `#pragma GCC diagnostic ignored`, zero new `__attribute__((unused))`). However, the task's Check 2 is scoped to the codebase state, not the diff: "there must be no ... `(void)var;` casts ... in `tests/smoke.cpp` or any `src/*.cpp` or `include/dgw/*.hpp`", and the closing instruction is decisive — "any warning suppression that isn't the Makefile-level `-Wno-unused-parameter` is a FAIL." Three pre-existing `(void)var;` casts remain in `tests/smoke.cpp` (the file the producer modified) and `include/dgw/util.hpp`. Two of them (`(void)add2;` and `(void)alloc;`) are in the very Test 1 fixture that the producer's commit message normative claim ("No lazy unused-variable suppressions; if a variable is set but not used, either use it or delete it") directly contradicts — the producer had the perfect opportunity to clean them up while fixing the 7 sibling cases in the same file, and did not. The `(void)cond; (void)msg; (void)loc;` casts in `util.hpp:74` are now fully redundant given the Makefile-level `-Wno-unused-parameter` and should be removed for the same warning-discipline hygiene.

A fourth grep hit, `include/dgw/arena.hpp:127` (`DGW_ALWAYS_VERIFY ? (void)0 : (void)0;`), is a degenerate ternary with `(void)0` literal casts on both branches — it is dead code, not a `(void)var;` cast on a variable, and the reviewer does not count it as a Check 2 violation, but notes that it should be cleaned up in the same warning-discipline pass.

Per `Mandatory-Agent-Review-Rule.md` §3.4, **CHANGES_REQUESTED**: the producing agent must address the Check 2 FAIL with a new commit that removes the three pre-existing `(void)var;` casts (drop the bindings of `add2` and `alloc` in `tests/smoke.cpp` the same way the 7 siblings were dropped; remove the now-redundant `(void)cond; (void)msg; (void)loc;` casts in `include/dgw/util.hpp:74` since `-Wno-unused-parameter` already covers that case) and optionally also clean up the dead `DGW_ALWAYS_VERIFY ? (void)0 : (void)0;` ternary at `include/dgw/arena.hpp:127`. The producing agent must then re-request review (REVIEW-006) from a fresh reviewer.

**Counts:** 6 PASS, 1 FAIL (of 7 checks).

| # | Check | Verdict |
|---|-------|---------|
| 1 | Makefile change is correct | PASS |
| 2 | No warning suppressions slipped in | **FAIL** |
| 3 | All 7 unused-but-set fixes are real | PASS |
| 4 | Test 3 still produces 2 RETURN nodes | PASS |
| 5 | Build is clean under `make` and `make SAN=1` | PASS |
| 6 | Smoke test still passes | PASS |
| 7 | No behavior change | PASS |

---

**Reviewer agent ID:** review-agent-005
**UTC timestamp:** 2026-09-01T21:26:47Z
