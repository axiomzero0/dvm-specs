# REVIEW-015 — DVM Hot-Loop + Lifter Fix Verification (REVIEW-014 CHANGES_REQUESTED)

**Reviewer agent ID:** `review-agent-015`
**Review type:** Spec compliance review (Mandatory-Agent-Review-Rule §3 + §3.4 — fresh reviewer for re-review after CHANGES_REQUESTED)
**Output under review:** commit `ad772828` — `runtime/interp: fix REVIEW-014 CHANGES_REQUESTED (threshold + op_jmp guard)`
**Files reviewed:**
- `runtime/interp/include/dvm/hotness.hpp` (1 line changed — line 25: `kDefaultThreshold 5 → 3`)
- `runtime/interp/src/opcodes_control.cpp` (3 lines changed — lines 71-80: removed recording-state guard around `mark_branch_or_loop` in `op_jmp`)

**Review timestamp (UTC):** `2026-09-04T22:11:00Z`
**Commit timestamp (UTC):** `2026-09-04T22:09:49Z`

---

## 1. Prior review context

REVIEW-014 (`review-agent-014`) reviewed commit `0cd4d73` (the hot-loop + lifter subsystem) and returned
**CHANGES_REQUESTED** with 8 PASS + 2 FAIL:

- **Check 1 FAIL** — `kDefaultThreshold = 5` in `hotness.hpp:25`, but producer's commit message said
  "default 3" and Check 1's verification condition said "default threshold is 3".
- **Check 3 FAIL** — `op_jmp` wrapped `mark_branch_or_loop()` in a `if (s.recorder && s.recorder->is_recording())`
  guard, so JMP backedges were never counted by the hotness tracker while the recorder was not yet
  recording. The other 4 branch handlers (`op_br_true`, `op_br_false`, `op_br_null`, `op_br_nonnull`)
  called `mark_branch_or_loop()` unconditionally.

Per Mandatory-Agent-Review-Rule §3.4, a CHANGES_REQUESTED review cannot be re-issued by the same
reviewer. This review (REVIEW-015) is performed by a fresh reviewer (`review-agent-015`) on the
producer's fix commit `ad772828`, which addresses both FAILs. The producer agent for commit
`ad772828` is not this reviewer (independence satisfied per §3.1), and this reviewer is not
`review-agent-001` through `-014` (fresh reviewer per §3.4).

---

## 2. Verdicts on the 4 checks

### Check 1 — `kDefaultThreshold` is 3 — **PASS**

**Spec:** DVM-Hybrid-Tracing-Architecture.md §12.1 ("backedge count exceeds threshold"); producer
commit message for `0cd4d73` states "default 3".

**Evidence (`include/dvm/hotness.hpp:25`):**

```cpp
  static constexpr std::uint32_t kDefaultThreshold = 3;
```

**Verification:**
- The line reads exactly `static constexpr std::uint32_t kDefaultThreshold = 3;` ✓
- Constructor (line 27) `explicit HotnessTracker(std::uint32_t threshold = kDefaultThreshold)`
  defaults to `kDefaultThreshold`, i.e., a default-constructed `HotnessTracker` now has threshold=3.
- This matches the producer's original commit-message claim ("default 3") and satisfies Check 1.
- Test 6 (`smoke.cpp:623`) constructs `HotnessTracker hotness(3);` explicitly — this still works
  identically (explicit `3` was already correct; only the default value changed).
- A default-constructed `HotnessTracker hotness;` now also yields threshold=3, consistent with the
  producer's claim and Check 1.

PASS.

---

### Check 2 — `op_jmp` calls `mark_branch_or_loop` unconditionally — **PASS**

**Spec:** DVM-Hybrid-Tracing-Architecture.md §12.1 (hotness counting must occur on every backedge,
including those taken before the recorder starts); producer commit message for `0cd4d73` states
"all branch handlers now call `mark_branch_or_loop()` unconditionally".

**Evidence (`src/opcodes_control.cpp:71-80`):**

```cpp
OpResult op_jmp(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::int32_t delta = signed_delta32(cell.s2(), cell.s3());
  s.advance();
  // A JMP is always "taken" — mark it as a branch exit or loop close.
  // Called unconditionally so the hotness tracker can count backedges
  // even when not recording.
  mark_branch_or_loop(s, delta);
  s.branch(delta);
  return OpResult::Continue;
}
```

**Verification:**
- No recording-state guard surrounds the `mark_branch_or_loop(s, delta)` call ✓
- The previous `if (s.recorder && s.recorder->is_recording())` guard (REVIEW-014, lines 75-77) has
  been removed entirely.
- All 5 branch handlers now have identical call structure for `mark_branch_or_loop` (called
  unconditionally on every taken branch):
  - `op_jmp` (line 77): `mark_branch_or_loop(s, delta);` — unconditional
  - `op_br_true` (line 87): `mark_branch_or_loop(s, delta);` inside `if (cond)` (taken-branch)
  - `op_br_false` (line 100): `mark_branch_or_loop(s, delta);` inside `if (!cond)` (taken-branch)
  - `op_br_null` (line 113): `mark_branch_or_loop(s, delta);` inside `if (is_null)` (taken-branch)
  - `op_br_nonnull` (line 126): `mark_branch_or_loop(s, delta);` inside `if (!is_null)` (taken-branch)
- The hotness path inside `mark_branch_or_loop` (lines 46-57) already handles the not-recording
  case correctly: `if (!s.recorder || !s.recorder->is_recording()) { ... if (delta < 0 && s.hotness) { ... } }`.
  Removing the `op_jmp` guard means a loop encoded with `JMP` as the backedge (instead of `BR_TRUE`)
  now triggers Tier 1 recording correctly.
- Comment on lines 74-76 was updated from the misleading version in `0cd4d73` and now accurately
  describes the unconditional-call rationale: *"Called unconditionally so the hotness tracker can
  count backedges even when not recording."* — comment and code now agree.

PASS.

---

### Check 3 — Build is clean — **PASS**

**Mandatory command executed:**

```
$ cd /home/z/my-project/dgw-core-repo/runtime/interp
$ make clean
$ make SAN=1 -j$(nproc) 2>&1 | tail -3
ar rcs build/libdvm_interp.a build/interp.o build/lifter.o build/loader.o build/module.o build/opcodes_arith.o build/opcodes_calls.o build/opcodes_control.o build/opcodes_except.o build/opcodes_move.o build/opcodes_object.o build/opcodes_sys.o build/state.o build/trace.o
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -Iinclude -I../../compiler/dgw-core/include tests/smoke.cpp -Lbuild -ldvm_interp -L../../compiler/dgw-core/build -ldgwcore -o bin/dvm_interp_smoke -fsanitize=address,undefined -fno-omit-frame-pointer -L../../compiler/dgw-core/build -ldgwcore
$ echo "EXIT: ${PIPESTATUS[0]}"
EXIT: 0
```

**Warning/error scan:**

```
$ make SAN=1 -j$(nproc) 2>&1 | grep -iE "warning|error" | grep -v "Werror"
(empty output)
$ echo "EXIT: $?"
EXIT: 1   (no matches found)
```

**Verification:**
- `make SAN=1 -j$(nproc)` exit code: 0 ✓
- Zero `warning:` matches in build log (outside the `-Werror` flag string) ✓
- Zero `error:` matches in build log ✓
- ASan + UBSan enabled via `-fsanitize=address,undefined` ✓
- Links against `../../compiler/dgw-core/build/libdgwcore.a` ✓
- 12 .o files produced + `build/libdvm_interp.a` + `bin/dvm_interp_smoke`

PASS.

---

### Check 4 — All 7 smoke tests pass — **PASS**

**Mandatory command executed:**

```
$ cd /home/z/my-project/dgw-core-repo/runtime/interp
$ ./bin/dvm_interp_smoke 2>&1; echo "=== SMOKE EXIT: $? ==="
== DVM Tier 0 Interpreter smoke test ==

-- Test 1: MOV_CONST + ADD_I64_WRAP + RET --
Loaded: 4 code cells, 2 constants, 1 functions
Test 1: 20 + 22 = 42 (PASS)

-- Test 2: CALL_DIRECT + RET --
Loaded: 6 code cells, 2 constants, 2 functions
Test 2: fn1 calls fn0(40 + 2) = 42 (PASS)

-- Test 3: counting loop (BR_TRUE + JMP back) --
Test 3: count to 10 = 10 (PASS)

-- Test 4: ALLOC + OBJ_SET + OBJ_GET --
Test 4: ALLOC + OBJ_SET(42) + OBJ_GET = 42 (PASS)

-- Test 5: trace recording of counting loop --
Trace recorded: 6 instructions, exit=2, loop=no
Test 5: trace of 6 instructions, exit=BranchTaken (PASS)
TraceFragment: fn=0, entry_pc=0, len=6, exit=BranchTaken, loop=no
  entry_registers: 4 values
  [0] pc=0 depth=1 op=0x0102 s1=0 s2=0 s3=0
  [1] pc=1 depth=1 op=0x0102 s1=1 s2=1 s3=0
  [2] pc=2 depth=1 op=0x0102 s1=2 s2=2 s3=0
  [3] pc=3 depth=1 op=0x020C s1=0 s2=0 s3=1
  [4] pc=4 depth=1 op=0x0602 s1=3 s2=0 s3=2
  [5] pc=5 depth=1 op=0x0701 s1=3 s2=65533 s3=65535
  exit_pc=6 exit_depth=1

-- Test 6: hot-loop detection + auto-recording --
Hot-loop trace: 3 instructions, exit=5, loop=yes
  entry_pc=3, loop_head_pc=3
Test 6: hot-loop detected at PC=3, trace of 3 instrs, LoopClose (PASS)
TraceFragment: fn=0, entry_pc=3, len=3, exit=LoopClose, loop=yes
  loop_head_pc=3
  entry_registers: 4 values
  [0] pc=3 depth=1 op=0x020C s1=0 s2=0 s3=1
  [1] pc=4 depth=1 op=0x0602 s1=3 s2=0 s3=2
  [2] pc=5 depth=1 op=0x0701 s1=3 s2=65533 s3=65535
  exit_pc=0 exit_depth=0

-- Test 7: lift trace into DGW-Core IR graph --
Lifted graph created successfully
Lifted DGW graph: 9 nodes, 7 edges
  node #0: kind=START flags=0x1 uses=1
  node #1: kind=CONST flags=0x1 uses=1
  node #2: kind=CONST flags=0x1 uses=1
  node #3: kind=CONST flags=0x1 uses=1
  node #4: kind=CONST flags=0x1 uses=0
  node #5: kind=ADD flags=0x1 uses=1
  node #6: kind=CMP_LT flags=0x2 uses=0
  node #7: kind=STATE flags=0x0 uses=0
  node #8: kind=BRANCH flags=0x0 uses=0
  verifier: ok=true pass=11 fail=0
Test 7: lifted graph has 9 nodes, 7 edges, ADD+CMP present (PASS)

== DVM Interpreter smoke test PASSED ==
=== SMOKE EXIT: 0 ===
```

**Verification:**
- `./bin/dvm_interp_smoke` exit code: 0 ✓
- Test 1 PASS ✓ (20 + 22 = 42)
- Test 2 PASS ✓ (fn1 calls fn0(40 + 2) = 42)
- Test 3 PASS ✓ (count to 10 = 10)
- Test 4 PASS ✓ (ALLOC + OBJ_SET(42) + OBJ_GET = 42)
- Test 5 PASS ✓ (trace of 6 instructions, exit=BranchTaken)
- Test 6 PASS ✓ (hot-loop detected at PC=3, trace of 3 instrs, LoopClose)
- Test 7 PASS ✓ (lifted graph has 9 nodes, 7 edges, ADD+CMP present)
- Final `== DVM Interpreter smoke test PASSED ==` line printed ✓
- 7/7 PASS, 0 FAIL.

**LeakSanitizer confirmation (additional, non-blocking):**

```
$ ASAN_OPTIONS=detect_leaks=1:exitcode=42 ./bin/dvm_interp_smoke 2>&1 | tail -3
Test 7: lifted graph has 9 nodes, 7 edges, ADD+CMP present (PASS)

== DVM Interpreter smoke test PASSED ==
$ echo "=== LSan EXIT: $? ==="
=== LSan EXIT: 0 ===
```

Exit code 0 (not 42) confirms zero leaks under LeakSanitizer.

PASS.

---

## 3. Verifier run log

Per Mandatory-Agent-Review-Rule §3.3, the WebVerifier was run on the lifted Test 6 trace via
`print_lifted_graph()` inside Test 7. Output: `verifier: ok=true pass=11 fail=0` — 11 WebVerifier
checks PASS, 0 FAIL. Additionally, the full smoke binary was rebuilt and executed under
ASan+UBSan+LeakSanitizer (`make SAN=1`).

---

## 4. Final status: **APPROVED**

### 4.1 Per-check verdict summary

| Check | Description | Verdict |
|---|---|---|
| 1 | `kDefaultThreshold` is 3 in `hotness.hpp:25` | **PASS** |
| 2 | `op_jmp` calls `mark_branch_or_loop` unconditionally (no recording-state guard) | **PASS** |
| 3 | Build is clean (`make SAN=1 -j$(nproc)` exit 0, zero warnings/errors) | **PASS** |
| 4 | All 7 smoke tests pass (`./bin/dvm_interp_smoke` exit 0, 7/7 PASS) | **PASS** |

**Total: 4 PASS, 0 FAIL.**

### 4.2 Diff summary (commit `ad772828`)

**File 1 — `runtime/interp/include/dvm/hotness.hpp` (1 line):**

```diff
-  static constexpr std::uint32_t kDefaultThreshold = 5;
+  static constexpr std::uint32_t kDefaultThreshold = 3;
```

**File 2 — `runtime/interp/src/opcodes_control.cpp` (3 lines in `op_jmp`):**

```diff
 OpResult op_jmp(InterpState& s, const crb::InstrCell& cell) noexcept {
   std::int32_t delta = signed_delta32(cell.s2(), cell.s3());
   s.advance();
   // A JMP is always "taken" — mark it as a branch exit or loop close.
-  if (s.recorder && s.recorder->is_recording()) {
-    mark_branch_or_loop(s, delta);
-  }
+  // Called unconditionally so the hotness tracker can count backedges
+  // even when not recording.
+  mark_branch_or_loop(s, delta);
   s.branch(delta);
   return OpResult::Continue;
 }
```

Both fixes are localized, minimal, and match exactly the FIX-1 and FIX-2 prescriptions from
REVIEW-014 §4.2. Neither fix touches any of the 8 previously-passing checks (HotnessTracker per-PC
counting, `==` threshold check, `reset()`, hotness auto-start at loop header, 4-arg `interpret`
no-start behavior, lifter opcode mapping, lifter SSA renaming, Test 6/7 correctness). All
previously-passing behaviors remain intact (Test 6 entry_pc=3, 3 instructions, LoopClose, result=10;
Test 7 9 nodes, 7 edges, ADD+CMP_LT present, verifier ok=true).

### 4.3 Non-blocking observations (informational, do not block approval)

These observations are inherited from REVIEW-014 §4.3 and remain non-blocking; they are noted
here for completeness but require no fix from the producer:

1. **`MOV_CONST` lowering uses placeholder `CONST(0)`** in `lifter.cpp` — acknowledged scope cut,
   not a spec violation. Mapping is correct, payload is approximate.
2. **Test 6 assertion uses lower bound** (`frag.length() < 3`) — test passes for the right reason
   (length is exactly 3) but a future over-recording bug would slip past.
3. **Test 7 does not programmatically assert `report.ok==true`** — `print_lifted_graph` prints
   `verifier: ok=true pass=11 fail=0` but does not fail the test on `ok=false`. A defensive
   assertion would harden the test.
4. **`-ldgwcore` appears twice** in the final link command (Makefile). Harmless duplication.

### 4.4 Final verdict

**APPROVED.** All 4 verification checks PASS. The fix commit `ad772828` correctly addresses both
FAILs from REVIEW-014:

- **FIX-1 (Check 1):** `kDefaultThreshold` changed from 5 to 3 in `hotness.hpp:25`, matching the
  producer's original "default 3" claim and the spec condition. Default-constructed
  `HotnessTracker` objects now have threshold=3.
- **FIX-2 (Check 3):** The recording-state guard around `mark_branch_or_loop` in `op_jmp`
  (lines 75-77 of the original `0cd4d73`) was removed. All 5 branch handlers now call
  `mark_branch_or_loop()` unconditionally on every taken branch, so loops encoded with `JMP` as
  the backedge now trigger hot-loop detection correctly.

The build is clean (exit 0, zero warnings, zero errors, ASan+UBSan+LeakSanitizer clean), and all
7 smoke tests pass (7/7 PASS, exit 0). The WebVerifier on the lifted Test 6 trace reports
`ok=true pass=11 fail=0`. The Tier 0→1→2 pipeline (interpreter → trace recorder → DGW lifter)
is now spec-compliant per DVM-Hybrid-Tracing-Architecture.md §12.1 and §T-008.

Per Mandatory-Agent-Review-Rule §3.4, the CHANGES_REQUESTED status from REVIEW-014 is now
resolved; the producer's fix commit `ad772828` is approved, and no further re-review is required
for these two specific issues.

---

**Reviewer agent ID:** `review-agent-015`
**Review timestamp (UTC):** `2026-09-04T22:11:00Z`
