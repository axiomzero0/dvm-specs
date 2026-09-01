# REVIEW-006 — Spec compliance review of the `(void)var` cleanup (REVIEW-005 fix)

**Producer commit:** `02be6559` — `compiler/dgw-core: remove pre-existing (void)var; warning-suppression casts`
**Files touched:** `compiler/dgw-core/include/dgw/arena.hpp` (-1), `compiler/dgw-core/include/dgw/util.hpp` (+3/-1), `compiler/dgw-core/tests/smoke.cpp` (+11/-4), `docs/reviews/2026-09-02-review-agent-005-dgw-core-werror.md` (+378, new file)
**Reviewer agent ID:** `review-agent-006` (fresh reviewer per Mandatory-Agent-Review-Rule.md §3.4; not the producer, not review-agent-001/002/003/004/005)
**UTC timestamp:** `2026-09-01T21:37:37Z`

---

## 1. Prior review (REVIEW-005) summary

REVIEW-005 (review-agent-005) reviewed the `-Werror` warning-policy change introduced by commit `f4322544` and returned **CHANGES_REQUESTED** with 6 PASS / 1 FAIL across 7 checks. The single FAIL was **Check 2 — no warning suppressions slipped in**: the mandatory grep `grep -rn "pragma GCC diagnostic\|__attribute__\|(void)" /home/z/my-project/dgw-core-repo/compiler/dgw-core/` returned four hits, three of which were genuine `(void)var;` casts on actual variables in `tests/smoke.cpp:79` (`(void)add2;`), `tests/smoke.cpp:81` (`(void)alloc;`), and `include/dgw/util.hpp:74` (`(void)cond; (void)msg; (void)loc;`). The fourth hit, `include/dgw/arena.hpp:127` (`DGW_ALWAYS_VERIFY ? (void)0 : (void)0;`), was a degenerate dead-code ternary rather than a `(void)var;` cast on a variable, so the REVIEW-005 reviewer did not count it as a Check 2 violation but noted it should be cleaned up in the same warning-discipline pass. All three pre-existing casts were introduced by commit `8dba306` (REVIEW-001's commit), not by the producer's `f4322544` commit, but Check 2 is scoped to the codebase state, not the diff — so the producer was required to clean them up in a new commit and re-request review.

---

## 2. Reviewer's verdicts on the fix

### Check 1 — No warning suppressions of any kind remain

**Verdict:** PASS

**Evidence:** Mandatory grep run from `/home/z/my-project/dgw-core-repo/compiler/dgw-core/`:

```
$ grep -rn "pragma GCC diagnostic\|__attribute__\|(void)" .
./include/dgw/util.hpp:76:  // -Wno-unused-parameter; no per-cast (void) suppression needed here.
```

The grep returns exactly **one** hit, and that hit is on a line whose leading non-whitespace characters are `//`. The context (from `Read` of `include/dgw/util.hpp` lines 60–84) confirms lines 74–76 form a three-line `//` comment block inside the `#else` branch of `dgw_assert`:

```
inline void dgw_assert(bool cond, std::string_view msg,
                      std::source_location loc = std::source_location::current()) {
#if DGW_ALWAYS_VERIFY
  if (!cond) panic(msg, loc);
#else
  // In release builds with DGW_ALWAYS_VERIFY=0, the assertion is a no-op.
  // The unused-parameter warning is suppressed Makefile-wide via
  // -Wno-unused-parameter; no per-cast (void) suppression needed here.
#endif
}
```

A targeted per-file count confirms zero matches in every source file except the one comment-line hit:

```
$ grep -c "pragma GCC diagnostic\|__attribute__\|(void)" include/dgw/*.hpp src/*.cpp tests/*.cpp
include/dgw/arena.hpp:0
include/dgw/control.hpp:0
include/dgw/graph.hpp:0
include/dgw/ids.hpp:0
include/dgw/kinds.hpp:0
include/dgw/pass_cleanup.hpp:0
include/dgw/pass_dce.hpp:0
include/dgw/pass_gvn.hpp:0
include/dgw/pass_licm.hpp:0
include/dgw/payloads.hpp:0
include/dgw/regions.hpp:0
include/dgw/scheduler.hpp:0
include/dgw/signatures.hpp:0
include/dgw/util.hpp:1
include/dgw/verifier.hpp:0
include/dgw/weaver.hpp:0
src/control.cpp:0
src/graph.cpp:0
src/pass_cleanup.cpp:0
src/pass_dce.cpp:0
src/pass_gvn.cpp:0
src/pass_licm.cpp:0
src/scheduler.cpp:0
src/signatures.cpp:0
src/verifier.cpp:0
src/weaver.cpp:0
tests/smoke.cpp:0
```

**Reasoning:** Per the task instructions, comment-line hits (lines where the match is inside a `//` comment) are OK; everything else is a FAIL. The only grep hit (`util.hpp:76`) is a `//` comment line containing the prose phrase "no per-cast `(void)` suppression needed here" — this is documentation explaining why the casts were removed, not an actual warning suppression. Every other source file (`include/dgw/*.hpp`, `src/*.cpp`, `tests/smoke.cpp`) returns zero matches. No `#pragma GCC diagnostic`, no `__attribute__`, no `__attribute__((unused))`, and no executable `(void)var;` casts remain anywhere in the source code. The Makefile-level `-Wno-unused-parameter` remains the sole warning-suppression mechanism, exactly as the producer's REVIEW-005 commit intended. **PASS.**

---

### Check 2 — The three pre-existing casts are gone

**Verdict:** PASS

**Evidence:** Targeted search for each of the four original grep strings confirms zero matches:

```
$ grep -rn "(void)add2;" .
$ grep -rn "(void)alloc;" .
$ grep -rn "(void)cond;" .
$ grep -rn "(void)msg;" .
$ grep -rn "(void)loc;" .
$ grep -rn "DGW_ALWAYS_VERIFY ? (void)0" .
```

All six searches return empty output (exit code 1 = no matches). Direct reads of the cited file:line locations confirm the removals:

- **`tests/smoke.cpp:79` (`(void)add2;`)** — gone. The current Test 1 fixture at lines 78–84 contains only `NodeId add1 = w.create_arith(NodeKind::ADD, load, c1a);` (line 83) followed by `w.create_arith(NodeKind::ADD, load, c1a);` as a statement (line 84). No `add2` binding, no `(void)add2;` cast.
- **`tests/smoke.cpp:81` (`(void)alloc;`)** — gone. The current Test 1 fixture at line 65 is `w.create_alloc(RegionKind::HEAP, 16, 8);` as a statement. No `alloc` binding, no `(void)alloc;` cast.
- **`include/dgw/util.hpp:74` (`(void)cond; (void)msg; (void)loc;`)** — gone. The current `#else` branch of `dgw_assert` (lines 74–76) contains only the three-line `//` comment block quoted in Check 1's evidence above. No `(void)` casts on `cond`/`msg`/`loc`.
- **`include/dgw/arena.hpp:127` (`DGW_ALWAYS_VERIFY ? (void)0 : (void)0;`)** — gone. The current `port_edge` function at line 126 begins directly with `if (!node_in_bounds(n)) return EdgeId{};`. No dead ternary present.

**Reasoning:** All four original grep hits are confirmed removed. The three pre-existing `(void)var;` casts (`add2`, `alloc`, `cond`/`msg`/`loc`) — which REVIEW-005 counted as the Check 2 FAIL — are completely gone. The fourth hit (the dead `DGW_ALWAYS_VERIFY ? (void)0 : (void)0;` ternary in `arena.hpp`) which REVIEW-005 noted should be cleaned up but did not count as a Check 2 violation is also gone, cleaning up the dead-code defect noted in REVIEW-005. The producer's commit message accurately describes all four removals. **PASS.**

---

### Check 3 — The underlying `create_*()` calls are preserved

**Verdict:** PASS

**Evidence:** `grep -n "create_arith\|create_alloc" tests/smoke.cpp` returns (relevant lines for Test 1):

```
65:  w.create_alloc(RegionKind::HEAP, 16, 8);
83:  NodeId add1 = w.create_arith(NodeKind::ADD, load, c1a);
84:  w.create_arith(NodeKind::ADD, load, c1a);  // identical to add1; GVN eliminates this
```

- **Second ADD exists for GVN to find:** Line 83 (`NodeId add1 = w.create_arith(NodeKind::ADD, load, c1a);`) is immediately followed by line 84 (`w.create_arith(NodeKind::ADD, load, c1a);`) — the second ADD call is preserved exactly as a statement. Both calls use the same inputs (`load`, `c1a`) and the same `NodeKind::ADD`, so they hash identically and the second is eliminated by GVN (the smoke-test output confirms `GVN: eliminated=1`).
- **ALLOC node still created:** Line 65 (`w.create_alloc(RegionKind::HEAP, 16, 8);`) calls `create_alloc` exactly as before, just without the `NodeId alloc =` LHS binding. The Weaver still pushes the ALLOC node into `node_kinds[]` and creates `RegionId{0}` inside the arena. The REF node on line 66 (`NodeId ref0 = w.create_ref(/*region=*/RegionId{0}, /*offset=*/0, AccessPerm::ReadWrite);`) references `RegionId{0}`, which is the region created by the ALLOC, so the region remains reachable via the REF's payload.

**Reasoning:** The fix removes only the `NodeId <name> =` LHS bindings, not the underlying `create_*()` calls. The second ADD still exists in the Weaver's arena (so GVN can find and eliminate it), and the ALLOC node is still constructed (so its region is reachable via the REF node's payload). The smoke-test output in Check 5 confirms GVN eliminated=1, DCE killed=1, live=11 — exactly the same as REVIEW-005's recorded values, proving the underlying nodes were preserved. **PASS.**

---

### Check 4 — Build is clean under both `make` and `make SAN=1`

**Verdict:** PASS

**Evidence:** Both invocations from `/home/z/my-project/dgw-core-repo/compiler/dgw-core/` produce exit code 0 with zero warnings and zero errors (full log in Section 3 below).

```
$ make clean && make -j$(nproc) 2>&1 | tail -5
ar rcs build/libdgwcore.a build/control.o build/graph.o build/pass_cleanup.o build/pass_dce.o build/pass_gvn.o build/pass_licm.o build/scheduler.o build/signatures.o build/verifier.o build/weaver.o
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g   -Iinclude tests/smoke.cpp -Lbuild -ldgwcore -o bin/dgw_smoke
$ echo "EXIT: $?"
EXIT: 0

$ make clean && make SAN=1 -j$(nproc) 2>&1 | tail -5
ar rcs build/libdgwcore.a build/control.o build/graph.o build/pass_cleanup.o build/pass_dce.o build/pass_gvn.o build/pass_licm.o build/scheduler.o build/signatures.o build/verifier.o build/weaver.o
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -Iinclude tests/smoke.cpp -Lbuild -ldgwcore -o bin/dgw_smoke -fsanitize=address,undefined -fno-omit-frame-pointer
$ echo "EXIT: $?"
EXIT: 0
```

Targeted `grep -E "warning:|error:"` against the full output of both invocations returns zero matches:

```
$ make clean >/dev/null 2>&1 && make -j$(nproc) 2>&1 | grep -E "warning:|error:"
$ echo "GREP_EXIT: $?"
GREP_EXIT: 1

$ make clean >/dev/null 2>&1 && make SAN=1 -j$(nproc) 2>&1 | grep -E "warning:|error:"
$ echo "GREP_EXIT: $?"
GREP_EXIT: 1
```

**Reasoning:** The build is completely clean under `-Werror`. Both the plain `make` invocation and the `make SAN=1` (ASan+UBSan) invocation exit 0 with no `warning:` or `error:` messages in their output. The Makefile's WARN line remains `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter` (unchanged by this fix commit; only the source files were edited). Since all four pre-existing `(void)` constructs are now gone, no source-level warning suppressions remain and `-Werror` does not flag anything. ASan+UBSan compile-and-link cleanly. **PASS.**

---

### Check 5 — Smoke test still passes

**Verdict:** PASS

**Evidence:** `./bin/dgw_smoke` (built under `make SAN=1`) exits 0 with all 8 test groups green (full output in Section 3 below):

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

Numerical results compared against REVIEW-005's recorded output:

| Metric | REVIEW-005 | REVIEW-006 | Match |
|--------|-----------|-----------|-------|
| Graph built (nodes, edges, regions) | 13, 17, 1 | 13, 17, 1 | ✓ |
| WebVerifier pre-opt (pass/fail/na) | 11/0/0 | 11/0/0 | ✓ |
| GVN eliminated / visited | 1 / 7 | 1 / 7 | ✓ |
| DCE killed / live | 1 / 11 | 1 / 11 | ✓ |
| Cleanup collapsed / killed | 0 / 0 | 0 / 0 | ✓ |
| WebVerifier post-opt (pass/fail/na) | 11/0/0 | 11/0/0 | ✓ |
| Scheduler Test 1 blocks | 4 | 4 | ✓ |
| Test 1 Block #0 contents | START, GUARD, RETURN | START, GUARD, RETURN | ✓ |
| Test 1 Block #1 / #2 / #3 | LOAD / STORE / DEOPT_TRAP | LOAD / STORE / DEOPT_TRAP | ✓ |
| Test 2 route_except_to edge id | 3 | 3 | ✓ |
| Test 2 graph (nodes, edges) | 5, 6 | 5, 6 | ✓ |
| Test 3 CFG blocks | 2 | 2 | ✓ |
| Test 3 Block #0 / #1 | START+BRANCH+RETURN#3 / RETURN#4 | START+BRANCH+RETURN#3 / RETURN#4 | ✓ |
| Test 4 LICM visited/hoisted | 1 / 0 | 1 / 0 | ✓ |
| Test 4b LICM visited/hoisted | 1 / 0 | 1 / 0 | ✓ |
| Test 5 / 5b verdict | ok / flagged | ok / flagged | ✓ |
| Test 6 CFG blocks | 1 | 1 | ✓ |
| Test 6 PHI incomings / block_ids | 2 / 2 | 2 / 2 | ✓ |
| Exit code | 0 | 0 | ✓ |

**Reasoning:** The smoke test passes with exit code 0 and all 8 test groups green. The numerical results are byte-for-byte identical to REVIEW-005's recorded output (which was itself byte-for-byte identical to REVIEW-003 and REVIEW-004). The four `(void)` removals have no observable effect on the test execution — node identities (13 nodes), edge counts (17 edges), verifier findings (11 PASS / 0 FAIL pre and post opt), GVN elimination (eliminated=1, visited=7), DCE removal (killed=1, live=11), scheduler block counts (4 blocks in Test 1, 2 blocks in Test 3, 1 block in Test 6), and PHI incomings (2 incomings, 2 block_ids) are all unchanged. No ASan or UBSan runtime errors. **PASS.**

---

### Check 6 — No behavior change

**Verdict:** PASS

**Evidence:** `git diff HEAD~1 HEAD --stat` confirms only four files changed:

```
 compiler/dgw-core/include/dgw/arena.hpp            |   1 -
 compiler/dgw-core/include/dgw/util.hpp             |   4 +-
 compiler/dgw-core/tests/smoke.cpp                  |  15 +-
 .../2026-09-02-review-agent-005-dgw-core-werror.md | 378 +++++++++++++++++++++
 4 files changed, 390 insertions(+), 8 deletions(+)
```

`git diff HEAD~1 HEAD --name-status` confirms three source files modified (M) and one review file added (A):

```
M	compiler/dgw-core/include/dgw/arena.hpp
M	compiler/dgw-core/include/dgw/util.hpp
M	compiler/dgw-core/tests/smoke.cpp
A	docs/reviews/2026-09-02-review-agent-005-dgw-core-werror.md
```

The full source diff (excluding the appended REVIEW-005 report) shows exactly the changes Check 6 expects:

| File | Hunk | Change |
|------|------|--------|
| `include/dgw/arena.hpp` | `port_edge` body (line 127) | Removed `DGW_ALWAYS_VERIFY ? (void)0 : (void)0;` dead ternary (1 line deleted) |
| `include/dgw/util.hpp` | `dgw_assert` `#else` branch (lines 74–76) | Replaced `(void)cond; (void)msg; (void)loc;` (1 line) with a 3-line `//` comment block explaining the suppression is no longer needed (3 lines added, 1 deleted) |
| `tests/smoke.cpp` | Test 1 ALLOC (line 65) | Dropped `NodeId alloc = ` binding (now `w.create_alloc(RegionKind::HEAP, 16, 8);`); added a 4-line `//` comment block explaining the binding is unnecessary |
| `tests/smoke.cpp` | Test 1 second ADD (lines 78–84) | Dropped `NodeId add2 = ` binding; dropped `(void)add2;` cast; dropped `(void)alloc;` cast; expanded the existing 2-line comment block to 5 lines; the second `create_arith` is now a statement `w.create_arith(NodeKind::ADD, load, c1a);` (1 line added, 4 lines deleted in this hunk) |
| `docs/reviews/2026-09-02-review-agent-005-dgw-core-werror.md` | New file (378 lines) | The REVIEW-005 report (the prior review's output, which is required to be appended to the repo per Mandatory-Agent-Review-Rule.md §7) |

The full diff content (source files only):

```diff
diff --git a/compiler/dgw-core/include/dgw/arena.hpp b/compiler/dgw-core/include/dgw/arena.hpp
@@ -124,7 +124,6 @@ struct GraphArena {
   EdgeId port_edge(NodeId n, PortId p) const {
-    DGW_ALWAYS_VERIFY ? (void)0 : (void)0;
     if (!node_in_bounds(n)) return EdgeId{};

diff --git a/compiler/dgw-core/include/dgw/util.hpp b/compiler/dgw-core/include/dgw/util.hpp
@@ -71,7 +71,9 @@ inline void dgw_assert(bool cond, std::string_view msg,
 #else
-  (void)cond; (void)msg; (void)loc;
+  // In release builds with DGW_ALWAYS_VERIFY=0, the assertion is a no-op.
+  // The unused-parameter warning is suppressed Makefile-wide via
+  // -Wno-unused-parameter; no per-cast (void) suppression needed here.
 #endif

diff --git a/compiler/dgw-core/tests/smoke.cpp b/compiler/dgw-core/tests/smoke.cpp
@@ -58,7 +58,11 @@ int main() {
   NodeId start = w.create_start();
-  NodeId alloc = w.create_alloc(RegionKind::HEAP, 16, 8);
+  // ALLOC creates a region (RegionId 0) and an ALLOC node that the rest
+  // of the graph references via ref0. We do not need to bind the ALLOC's
+  // NodeId locally — the region is created inside the Weaver's arena and
+  // is reachable via the REF node's payload below.
+  w.create_alloc(RegionKind::HEAP, 16, 8);
   NodeId ref0  = w.create_ref(/*region=*/RegionId{0}, /*offset=*/0, AccessPerm::ReadWrite);
@@ -73,12 +77,11 @@ int main() {
-  // Both use the SAME inputs (load, c1a) so they hash identically and the
-  // second is eliminated.
+  // second is eliminated. We do not bind the second ADD to a local — the
+  // node exists in the Weaver's arena (where GVN will find it) and the
+  // test does not need to refer to it after creation.
   NodeId add1 = w.create_arith(NodeKind::ADD, load, c1a);
-  NodeId add2 = w.create_arith(NodeKind::ADD, load, c1a);  // identical to add1
-  (void)add2;  // will be killed by GVN
-
-  (void)alloc;  // graph retains the ALLOC node; binding suppresses unused warning
+  w.create_arith(NodeKind::ADD, load, c1a);  // identical to add1; GVN eliminates this
```

**Reasoning:** The diff is minimal and surgical. The only executable-code changes are the four warning-suppression removals (the three `(void)var;` casts that REVIEW-005 flagged as the Check 2 FAIL, plus the dead `DGW_ALWAYS_VERIFY ? (void)0 : (void)0;` ternary REVIEW-005 noted for cleanup) and the two `NodeId <name> =` LHS binding drops for `add2` and `alloc` (which is how the producer eliminated the need for the casts — the variables are no longer bound, so they cannot be unused). The underlying `create_alloc()` and second `create_arith()` calls are byte-for-byte identical (verified by inspection of lines 65 and 84 in the current `tests/smoke.cpp`). All other changes are prose-only comment-line expansions explaining why the bindings and casts were removed. No executable statements were added or modified beyond the four cast removals and the two LHS binding drops. No edge rewiring, no port index changes, no other source files touched. The new `docs/reviews/2026-09-02-review-agent-005-dgw-core-werror.md` file is the REVIEW-005 report, which is required to be appended to the repo per Mandatory-Agent-Review-Rule.md §7 ("Every review must be recorded in `docs/reviews/`"). No other logic changes. **PASS.**

---

## 3. Verifier run log

### 3.1 `make` (clean build, no sanitizers)

```
$ cd /home/z/my-project/dgw-core-repo/compiler/dgw-core
$ make clean
rm -rf build bin
$ make -j$(nproc) 2>&1 | tail -5
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g   -c src/weaver.cpp -o build/weaver.o
ar rcs build/libdgwcore.a build/control.o build/graph.o build/pass_cleanup.o build/pass_dce.o build/pass_gvn.o build/pass_licm.o build/scheduler.o build/signatures.o build/verifier.o build/weaver.o
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g   -Iinclude tests/smoke.cpp -Lbuild -ldgwcore -o bin/dgw_smoke
$ echo "EXIT: $?"
EXIT: 0
```

Targeted warning/error scan:

```
$ make clean >/dev/null 2>&1 && make -j$(nproc) 2>&1 | grep -E "warning:|error:"
$ echo "GREP_EXIT: $?"
GREP_EXIT: 1
```

**Exit code:** 0. **Warnings:** 0. **Errors:** 0.

### 3.2 `make SAN=1` (clean build, ASan + UBSan)

```
$ make clean
rm -rf build bin
$ make SAN=1 -j$(nproc) 2>&1 | tail -5
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/weaver.cpp -o build/weaver.o
ar rcs build/libdgwcore.a build/control.o build/graph.o build/pass_cleanup.o build/pass_dce.o build/pass_gvn.o build/pass_licm.o build/scheduler.o build/signatures.o build/verifier.o build/weaver.o
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -Iinclude tests/smoke.cpp -Lbuild -ldgwcore -o bin/dgw_smoke -fsanitize=address,undefined -fno-omit-frame-pointer
$ echo "EXIT: $?"
EXIT: 0
```

Targeted warning/error scan:

```
$ make clean >/dev/null 2>&1 && make SAN=1 -j$(nproc) 2>&1 | grep -E "warning:|error:"
$ echo "GREP_EXIT: $?"
GREP_EXIT: 1
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

**Exit code:** 0. No ASan or UBSan runtime errors. All 8 test groups green. Numerical results byte-for-byte identical to REVIEW-005's recorded output (which was itself identical to REVIEW-003 and REVIEW-004).

---

## 4. Final review status

**APPROVED**

The producer's fix commit `02be6559` cleanly addresses REVIEW-005's single Check 2 FAIL by removing all four pre-existing `(void)` constructs that REVIEW-005 identified:

1. **`tests/smoke.cpp:79` (`(void)add2;`)** — removed. The `NodeId add2 = ` binding was also dropped; the second `create_arith` is now a statement (`w.create_arith(NodeKind::ADD, load, c1a);` at line 84), so the underlying ADD node still exists in the Weaver's arena for GVN to find and eliminate (smoke-test output confirms `GVN: eliminated=1`).
2. **`tests/smoke.cpp:81` (`(void)alloc;`)** — removed. The `NodeId alloc = ` binding was also dropped; the underlying `create_alloc(RegionKind::HEAP, 16, 8)` is now a statement at line 65, so the region (`RegionId{0}`) is still created and reachable via the REF node's payload on line 66.
3. **`include/dgw/util.hpp:74` (`(void)cond; (void)msg; (void)loc;`)** — removed. Replaced with a 3-line `//` comment block explaining that the Makefile-level `-Wno-unused-parameter` now covers this case.
4. **`include/dgw/arena.hpp:127` (`DGW_ALWAYS_VERIFY ? (void)0 : (void)0;`)** — removed (the dead-code ternary REVIEW-005 noted for cleanup).

The mandatory grep `grep -rn "pragma GCC diagnostic\|__attribute__\|(void)" /home/z/my-project/dgw-core-repo/compiler/dgw-core/` now returns exactly one hit, and that hit is on a `//` comment line (`util.hpp:76` — the prose phrase "no per-cast `(void)` suppression needed here"), which the task instructions explicitly permit. Every source file (`include/dgw/*.hpp`, `src/*.cpp`, `tests/smoke.cpp`) returns zero executable-code matches.

The build is clean under both `make` and `make SAN=1` (exit code 0, zero `warning:` messages, zero `error:` messages, ASan+UBSan compile-and-link clean). The smoke test passes with exit code 0, all 8 test groups green, and numerical results byte-for-byte identical to REVIEW-005's recorded output. The diff is minimal and surgical: four warning-suppression removals, two `NodeId` LHS binding drops (the mechanism by which the casts became unnecessary), three comment-block expansions explaining the change, and the REVIEW-005 report file appended to `docs/reviews/` as required by Mandatory-Agent-Review-Rule.md §7. No other source files were touched, no other executable statements were added, removed, or modified, and no edge rewiring or port index changes occurred.

This review re-verifies REVIEW-005's Check 2 (now PASS) and runs a regression check on the other checks (all remain PASS). **All 6 checks PASS, 0 FAIL.**

Per `Mandatory-Agent-Review-Rule.md` §3.4, the fix commit `02be6559` satisfies REVIEW-005's CHANGES_REQUESTED and is hereby **APPROVED**. The `-Werror` warning-policy change (commit `f4322544`) together with this cleanup commit (`02be6559`) establishes a clean warning-discipline baseline for the DGW-Core IR: every warning is treated as an error, the only Makefile-level suppression is `-Wno-unused-parameter` (for interface signatures with intentionally-unused parameters), and zero source-level `(void)var;` casts or other lazy suppressions remain in the codebase.

**Counts:** 6 PASS, 0 FAIL (of 6 checks).

| # | Check | Verdict |
|---|-------|---------|
| 1 | No warning suppressions of any kind remain | PASS |
| 2 | The three pre-existing casts are gone | PASS |
| 3 | The underlying `create_*()` calls are preserved | PASS |
| 4 | Build is clean under `make` and `make SAN=1` | PASS |
| 5 | Smoke test still passes | PASS |
| 6 | No behavior change | PASS |

---

**Reviewer agent ID:** review-agent-006
**UTC timestamp:** 2026-09-01T21:37:37Z
