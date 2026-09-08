# REVIEW-014 — DVM Hot-Loop Detection + DGW Lifter (Tier 0→1→2 Pipeline)

**Reviewer agent ID:** `review-agent-014`
**Review type:** Spec compliance review (Mandatory-Agent-Review-Rule §3)
**Output under review:** commit `0cd4d73` — `runtime/interp: hot-loop detection + DGW lifter (Tier 0→1→2 pipeline)`
**Files reviewed:**
- `runtime/interp/include/dvm/hotness.hpp` (new, 61 lines — `HotnessTracker`)
- `runtime/interp/include/dvm/lifter.hpp` (new, 48 lines — `lift_trace`/`print_lifted_graph` decls)
- `runtime/interp/src/lifter.cpp` (new, 295 lines — full CRB→DGW lifter)
- `runtime/interp/include/dvm/interp.hpp` (+7 lines: 4-arg `interpret` overload + `HotnessTracker` forward decl)
- `runtime/interp/include/dvm/state.hpp` (+6 lines: `HotnessTracker* hotness` + `current_function_id`)
- `runtime/interp/src/interp.cpp` (+13 lines: 4-arg overload, no-start-when-hotness)
- `runtime/interp/src/opcodes_control.cpp` (+26 lines: hotness check in `mark_branch_or_loop`)
- `runtime/interp/Makefile` (+4 lines: `-I../../compiler/dgw-core/include`, `-ldgwcore`)
- `runtime/interp/tests/smoke.cpp` (+166 lines: Test 6 hot-loop, Test 7 lifter)

**Review timestamp (UTC):** `2026-09-04T22:06:00Z`
**Commit timestamp (UTC):** `2026-09-04T22:01:02Z`

---

## 1. Prior reviews summary

Thirteen prior reviews on this repository were logged in `/home/z/my-project/worklog.md`. Final
verdicts were: REVIEW-001 CHANGES_REQUESTED (33/15), REVIEW-002 CHANGES_REQUESTED (12+3 PARTIAL),
REVIEW-003 APPROVED (3), REVIEW-004 APPROVED (7), REVIEW-005 CHANGES_REQUESTED (6+1), REVIEW-006
APPROVED (6), REVIEW-007 CHANGES_REQUESTED (15+2), REVIEW-008 CHANGES_REQUESTED (9+2), REVIEW-009
APPROVED (8), REVIEW-010 CHANGES_REQUESTED (26/1 — ModuleHeader vs CRB §3.1), REVIEW-011 APPROVED
(8 — ModuleHeader + ADD/SUB_I64_WRAP UB fixes), REVIEW-012 APPROVED (10 — extended opcode set +
heap cleanup), REVIEW-013 APPROVED (10 — Tier 1 trace recorder, 0 FAIL). The current review,
REVIEW-014, is the first review of the hot-loop detector + DGW lifter subsystem, which closes
the Tier 0→1→2 pipeline per DVM-Hybrid-Tracing-Architecture.md §12.1 (hotness trigger) and §T-008
(guest bytecode lifting). The producer agent for commit `0cd4d73` is not this reviewer (independence
satisfied per §3.1), and this reviewer is not review-agent-001 through -013 (fresh reviewer per §3.4).

---

## 2. Verdicts on the 10 checks

### Check 1 — HotnessTracker API — **FAIL**

**Spec:** DVM-Hybrid-Tracing-Architecture.md §12.1 ("backedge count exceeds threshold").

**Producer self-description (commit message):** *"When a counter reaches the threshold (default 3),
auto-starts the TraceRecorder at the loop header PC."*

**Check requirements:** (a) counts backedges per PC; (b) returns true when `count == threshold`
(not `>`); (c) has `reset()`; (d) **default threshold is 3**.

**Evidence (`include/dvm/hotness.hpp:23-59`):**

| Sub-condition | Code line | Verdict |
|---|---|---|
| (a) counts per PC | `std::unordered_map<std::uint32_t,std::uint32_t> counters_;` (line 58) keyed by `target_pc` | PASS |
| (b) `count == threshold` (not `>`) | `if (count == threshold_) { return true; }` (lines 37-40) | PASS |
| (c) `reset()` | `void reset() { counters_.clear(); }` (line 52) | PASS |
| (d) **default threshold = 3** | `static constexpr std::uint32_t kDefaultThreshold = 5;` (line 25) | **FAIL** |

**Reasoning:** The producer's commit message and the review's Check 1 condition both specify
"default 3", but the code defines `kDefaultThreshold = 5` and the constructor
`explicit HotnessTracker(std::uint32_t threshold = kDefaultThreshold)` (line 27) defaults to 5.
This is a producer-self-description vs. code discrepancy. Test 6 sidesteps the issue by
explicitly constructing `HotnessTracker hotness(3);` (smoke.cpp:623), so the test still passes,
but any caller that constructs `HotnessTracker hotness;` (default) gets threshold=5, not 3.
Either the commit message's "default 3" claim is wrong (and the producer should update the
commit message), or the constant should be 3 (and the producer should fix the code). The
review's Check 1 explicitly states "default threshold is 3" as the verification condition;
the code does not satisfy it. Per Mandatory-Agent-Review-Rule §3.2, this is a `FAIL`.

---

### Check 2 — Hotness auto-start at the loop header (branch target PC) — **PASS**

**Spec:** §12.1 — "A root trace usually starts at a loop header"; the recorder must capture
"loop header bytecode offset" as the trace's `entry_pc`.

**Evidence (`src/opcodes_control.cpp:45-68`):**

```cpp
void mark_branch_or_loop(InterpState& s, std::int32_t delta) noexcept {
  if (!s.recorder || !s.recorder->is_recording()) {
    // Not recording. Check hotness on backward branches.
    if (delta < 0 && s.hotness) {
      std::int64_t target = static_cast<std::int64_t>(s.current().pc) + delta;
      auto target_pc = static_cast<std::uint32_t>(target);
      if (s.hotness->on_backedge(target_pc)) {
        if (s.recorder) {
          s.recorder->start(s.current_function_id, target_pc,
                             s.current().registers);
        }
      }
    }
    return;
  }
  ...
}
```

The call `s.recorder->start(s.current_function_id, target_pc, s.current().registers)` uses
`target_pc` (the branch destination, computed as `current().pc + delta` *after* `s.advance()`,
i.e., the post-increment PC + negative delta = the backedge target). For Test 6's BR_TRUE at
PC=5 with delta=-3, the next-PC is 6, and `target_pc = 6 + (-3) = 3`. Test 6's smoke output
confirms `entry_pc=3, loop_head_pc=3` — the recorder was started at the loop header (PC=3),
not at PC=0. PASS.

---

### Check 3 — `mark_branch_or_loop` called unconditionally on every taken branch — **FAIL**

**Spec:** §12.1 — backedge counting must happen while the interpreter is executing the loop, i.e.,
while the recorder is NOT yet recording (the trigger has not fired). The producer's commit
message states: *"all branch handlers now call `mark_branch_or_loop()` unconditionally (not just
when recording), so the hotness tracker can count backedges even when not recording."*

**Check requirement:** all 5 branch handlers (`op_br_true`, `op_br_false`, `op_br_null`,
`op_br_nonnull`, `op_jmp`) call `mark_branch_or_loop()` on EVERY taken branch, regardless of
recording state.

**Evidence (`src/opcodes_control.cpp`):**

| Handler | Line | Call form | Unconditional? |
|---|---|---|---|
| `op_br_true` | 87 | `if (cond) { mark_branch_or_loop(s, delta); ... }` | YES (w.r.t. recording) |
| `op_br_false` | 100 | `if (!cond) { mark_branch_or_loop(s, delta); ... }` | YES |
| `op_br_null` | 113 | `if (is_null) { mark_branch_or_loop(s, delta); ... }` | YES |
| `op_br_nonnull` | 126 | `if (!is_null) { mark_branch_or_loop(s, delta); ... }` | YES |
| **`op_jmp`** | **75-77** | `if (s.recorder && s.recorder->is_recording()) { mark_branch_or_loop(s, delta); }` | **NO** |

**Reasoning:** `op_jmp` (lines 71-80) wraps the `mark_branch_or_loop` call in a recording-state
guard, so when the interpreter is executing a loop with a JMP backedge and the recorder is not
yet recording (the typical hot-loop scenario before the threshold is reached), `op_jmp` never
invokes `mark_branch_or_loop`, and the hotness tracker never counts that backedge. This
directly contradicts the producer's commit-message claim and the review's Check 3 condition.
Test 6 happens to use `BR_TRUE` (not `JMP`) as the loop backedge, so the test does not exercise
this defect — but a loop encoded with `JMP` would never trigger Tier 1 recording under this
interpreter. The fix is trivial: replace lines 75-77 with a single unconditional
`mark_branch_or_loop(s, delta);` (matching the four `op_br_*` handlers). Per
Mandatory-Agent-Review-Rule §3.2, this is a `FAIL`.

---

### Check 4 — 4-arg `interpret` overload does NOT start the recorder immediately — **PASS**

**Spec:** §12.1 — recording must start at the loop header, not at function entry.

**Evidence (`src/interp.cpp:48-76`):**

```cpp
Value interpret(const crb::Module& module, std::uint32_t entry_function_id,
                 TraceRecorder* recorder, HotnessTracker* hotness) {
  ...
  if (recorder && !hotness) {
    s.recorder = recorder;
    recorder->start(entry_function_id, 0, s.current().registers);  // start at PC=0
  }
  if (recorder && hotness) {
    s.recorder = recorder;   // attach recorder, but DO NOT start
  }
  if (hotness) {
    s.hotness = hotness;     // attach hotness tracker
  }
  ...
}
```

When both `recorder` and `hotness` are non-null, the recorder is attached but `recorder->start()`
is NOT called (lines 71-73 set `s.recorder` only). The hotness tracker will auto-start the
recorder via `mark_branch_or_loop` when the threshold is reached. Test 6 confirms: the recorder
starts only at PC=3 (loop header), after 3 backedges, not at PC=0. PASS.

---

### Check 5 — Lifter CRB→DGW opcode mapping — **PASS**

**Spec:** §T-008 ("Guest Bytecode Lifting"); DGW-Core-IR.md Parts 1-7 (IR construction).

**Evidence (`src/lifter.cpp`):**

| Mapping required by Check 5 | Code line | DGW call | Verdict |
|---|---|---|---|
| MOV_CONST → CONST | 102 | `w.create_const(static_cast<std::int64_t>(0))` | PASS (placeholder value — see Note 1) |
| ADD_I64_WRAP → ADD (via `create_arith`) | 109 | `w.create_arith(NodeKind::ADD, a, b)` | PASS |
| CMP_LT_S → CMP_LT (via `create_cmp`) | 147 | `w.create_cmp(NodeKind::CMP_LT, a, b)` | PASS |
| BR_TRUE (loop close) → STATE + BRANCH | 181, 185 | `w.create_state();` + `w.create_branch(ctrl, cond)` | PASS |
| RET → RETURN | 217 | `w.create_return(ctrl, ret_val)` | PASS |
| ALLOC → ALLOC (via `create_alloc`) | 242 | `w.create_alloc(dgw::RegionKind::HEAP, field_count, 8)` | PASS |

All six required mappings are present. PASS.

**Note 1 (non-blocking):** `MOV_CONST` always lowers to `CONST(0)` because the lifter does not
have access to the module's constant pool (lifter.cpp:99-101 comment). A full lifter would look
up the constant from `module.constant_pool[cell.s2()]`. This is acknowledged in the source
comments and is a deliberate scope cut, not a spec violation — the *mapping* is correct
(CRB `MOV_CONST` → DGW `CONST`); only the *payload* is approximate. Non-blocking for this review.

---

### Check 6 — SSA renaming via RegMap — **PASS**

**Spec:** §T-008 — the lifter "maps VM registers to guest registers using the interpreter
frame map"; DGW-Core-IR.md Part 2 (SSA form: each definition creates a new value, each use
reads the most recent value).

**Evidence (`src/lifter.cpp:38-52`):**

```cpp
struct RegMap {
  std::unordered_map<std::uint16_t, NodeId> regs;
  NodeId read(std::uint16_t idx) const {
    auto it = regs.find(idx);
    return it != regs.end() ? it->second : NodeId{};
  }
  void write(std::uint16_t idx, NodeId v) {
    regs[idx] = v;
  }
};
```

Each register *write* stores a new `NodeId` (the freshly-created DGW node from
`create_const`/`create_arith`/`create_cmp`/`create_alloc`/etc.) into `regs[idx]`,
overwriting any previous value. Each register *read* returns the most recent `NodeId`
(or `NodeId{}` = null for uninitialized registers). This is standard SSA renaming for a
linear trace. Test 7's output confirms the renaming produces a well-formed graph (ADD reads
its two operands from `r0` and `r1`'s most recent definitions, CMP_LT reads `r0`'s most recent
definition which is the ADD output). PASS.

---

### Check 7 — Test 6 correctness — **PASS**

**Check requirement:** 3 instructions recorded; exit=LoopClose; loop=yes; entry_pc=3 (loop
header, not 0); result=10 (correct computation).

**Evidence (smoke run output, full log in §3 below):**

```
Hot-loop trace: 3 instructions, exit=5, loop=yes
  entry_pc=3, loop_head_pc=3
Test 6: hot-loop detected at PC=3, trace of 3 instrs, LoopClose (PASS)
TraceFragment: fn=0, entry_pc=3, len=3, exit=LoopClose, loop=yes
  loop_head_pc=3
  entry_registers: 4 values
  [0] pc=3 depth=1 op=0x020C s1=0 s2=0 s3=1     ; ADD_I64_WRAP r0, r0, r1
  [1] pc=4 depth=1 op=0x0602 s1=3 s2=0 s3=2     ; CMP_LT_S r3, r0, r2
  [2] pc=5 depth=1 op=0x0701 s1=3 s2=65533 s3=65535  ; BR_TRUE r3, -3
  exit_pc=0 exit_depth=0
```

- 3 instructions recorded: ✓ (`len=3`)
- exit=LoopClose: ✓ (exit=5 corresponds to `ExitReason::LoopClose` per trace.hpp:36)
- loop=yes: ✓ (`is_loop()` returns `loop_head_pc != 0`; `loop_head_pc=3`)
- entry_pc=3: ✓ (loop header, NOT function-entry PC=0)
- result=10: ✓ (smoke.cpp:625-628 asserts `result6.as_i64() == 10`; the interpreter's
  computed-goto dispatch still produces the correct counting-loop result `10`)

PASS.

**Note 2 (non-blocking):** The test assertion `if (frag.length() < 3) FAIL` checks a lower
bound, not exact equality. Actual length is exactly 3, so the test passes for the right reason
— but a future bug that over-records (e.g., 4 instructions) would slip past this assertion.
Non-blocking.

---

### Check 8 — Test 7 correctness — **PASS**

**Check requirement:** graph has ≥5 nodes; has ADD and CMP_LT nodes; verifier passes (ok=true).

**Evidence (smoke run output):**

```
Lifted DGW graph: 9 nodes, 7 edges
  node #0: kind=START flags=0x1 uses=1
  node #1: kind=CONST flags=0x1 uses=1
  node #2: kind=CONST flags=0x1 uses=1
  node #3: kind=CONST flags=0x1 uses=1
  node #4: kind=CONST flags=0x1 uses=0
  node #5: kind=ADD flags=0x1 uses=1
  node #6: kind=CMP_LT flags=0x1 uses=2
  node #7: kind=STATE flags=0x0 uses=0
  node #8: kind=BRANCH flags=0x0 uses=0
  verifier: ok=true pass=11 fail=0
Test 7: lifted graph has 9 nodes, 7 edges, ADD+CMP present (PASS)
```

- ≥5 nodes: ✓ (9 nodes: 1 START + 4 CONST + 1 ADD + 1 CMP_LT + 1 STATE + 1 BRANCH)
- ADD present: ✓ (node #5)
- CMP_LT present: ✓ (node #6)
- Verifier passes: ✓ (`ok=true pass=11 fail=0` — 11 WebVerifier checks PASS, 0 FAIL)

PASS.

**Note 3 (non-blocking):** `print_lifted_graph` prints `verifier: ok=... pass=... fail=...`
but Test 7 does not programmatically assert `ok==true`. The output shows `ok=true pass=11
fail=0`, so the verifier does pass, but the test would not catch a future regression that
produced `ok=false`. A defensive `if (!report.ok) FAIL` would harden the test. Non-blocking.

---

### Check 9 — Build is clean — **PASS**

**Evidence:**

```
$ cd /home/z/my-project/dgw-core-repo/runtime/interp
$ make clean && make SAN=1 -j$(nproc)
[17 lines of compile commands, no warnings, no errors]
$ echo $?
0
```

- Exit code 0: ✓
- Zero warnings: ✓ (grep over the 17-line build log for `warning:` / `error:` outside the
  `-Werror` flag string returns 0 matches)
- Zero errors: ✓
- Zero leaks: ✓ (`ASAN_OPTIONS=detect_leaks=1:exitcode=42 ./bin/dvm_interp_smoke` exits 0,
  not 42 — LeakSanitizer is clean)
- Links against `libdgwcore.a`: ✓ (Makefile line 23: `LDFLAGS := ... -L../../compiler/dgw-core/build -ldgwcore`;
  line 59: `$(SMOKE): ... ../../compiler/dgw-core/build/libdgwcore.a | $(BINDIR)`;
  line 60: `$(CXX) ... -ldvm_interp -L../../compiler/dgw-core/build -ldgwcore -o $@ $(LDFLAGS)`;
  the final link command includes `-ldgwcore` twice — once from the explicit command and once
  from `$(LDFLAGS)`, harmless duplication)

PASS.

---

### Check 10 — All 7 smoke tests pass — **PASS**

**Evidence (full smoke run, see §3):**

```
$ ./bin/dvm_interp_smoke; echo $?
... Test 1: 20 + 22 = 42 (PASS)
... Test 2: fn1 calls fn0(40 + 2) = 42 (PASS)
... Test 3: count to 10 = 10 (PASS)
... Test 4: ALLOC + OBJ_SET(42) + OBJ_GET = 42 (PASS)
... Test 5: trace of 6 instructions, exit=BranchTaken (PASS)
... Test 6: hot-loop detected at PC=3, trace of 3 instrs, LoopClose (PASS)
... Test 7: lifted graph has 9 nodes, 7 edges, ADD+CMP present (PASS)
== DVM Interpreter smoke test PASSED ==
0
```

- Exit code 0: ✓
- All 7 tests print PASS: ✓ (Tests 1-7 all match `(PASS)` regex; final
  `== DVM Interpreter smoke test PASSED ==` line printed)

PASS.

---

## 3. Verifier run log

Per Mandatory-Agent-Review-Rule §3.3, the WebVerifier was run on the lifted Test 6 trace via
`print_lifted_graph()` inside Test 7. Additionally, the full smoke binary was rebuilt and
executed under ASan+UBSan+LeakSanitizer.

### 3.1 Mandatory build

```
$ cd /home/z/my-project/dgw-core-repo/runtime/interp
$ make clean
$ make SAN=1 -j$(nproc) 2>&1 | tail -5
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -Iinclude -I../../compiler/dgw-core/include tests/smoke.cpp -Lbuild -ldvm_interp -L../../compiler/dgw-core/build -ldgwcore -o bin/dvm_interp_smoke -fsanitize=address,undefined -fno-omit-frame-pointer -L../../compiler/dgw-core/build -ldgwcore
$ echo "EXIT: $?"
EXIT: 0
```

Build log: 17 lines, zero `warning:` or `error:` matches outside `-Werror` flag string.
12 .o files produced + `build/libdvm_interp.a` + `bin/dvm_interp_smoke`. Linked against
`../../compiler/dgw-core/build/libdgwcore.a` (built from `compiler/dgw-core/`).

### 3.2 Mandatory smoke run

```
$ ./bin/dvm_interp_smoke; echo "=== SMOKE EXIT: $? ==="
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
  node #6: kind=CMP_LT flags=0x1 uses=2
  node #7: kind=STATE flags=0x0 uses=0
  node #8: kind=BRANCH flags=0x0 uses=0
  verifier: ok=true pass=11 fail=0
Test 7: lifted graph has 9 nodes, 7 edges, ADD+CMP present (PASS)

== DVM Interpreter smoke test PASSED ==
=== SMOKE EXIT: 0 ===
```

### 3.3 LeakSanitizer confirmation

```
$ ASAN_OPTIONS=detect_leaks=1:exitcode=42 ./bin/dvm_interp_smoke 2>&1 | tail -5
... Test 7: lifted graph has 9 nodes, 7 edges, ADD+CMP present (PASS)
== DVM Interpreter smoke test PASSED ==
$ echo "=== LSan EXIT: $? ==="
=== LSan EXIT: 0 ===
```

Exit code 0 (not 42) confirms zero leaks under LeakSanitizer. (Note: the smoke binary
calls `delete graph` after Test 7's checks — smoke.cpp:705 — and the interpreter calls
`s.free_heap()` on exit — interp.cpp:369 — so all heap allocations are cleaned up.)

---

## 4. Final status: **CHANGES_REQUESTED**

### 4.1 Per-check verdict summary

| Check | Description | Verdict |
|---|---|---|
| 1 | HotnessTracker API (counts per PC, == threshold, reset(), default=3) | **FAIL** (default=5, not 3) |
| 2 | Hotness auto-start at loop header (entry_pc=3) | PASS |
| 3 | `mark_branch_or_loop` unconditional on every taken branch | **FAIL** (`op_jmp` guards on `is_recording()`) |
| 4 | 4-arg `interpret` does NOT start recorder when hotness set | PASS |
| 5 | Lifter CRB→DGW opcode mapping (6 mappings) | PASS |
| 6 | Lifter SSA renaming via RegMap | PASS |
| 7 | Test 6 correctness (3 instrs, LoopClose, entry_pc=3, result=10) | PASS |
| 8 | Test 7 correctness (≥5 nodes, ADD+CMP_LT, verifier ok=true) | PASS |
| 9 | Build is clean (exit 0, 0 warnings, 0 errors, 0 leaks, links libdgwcore.a) | PASS |
| 10 | All 7 smoke tests pass | PASS |

**Total: 8 PASS, 2 FAIL.**

### 4.2 Required fixes before re-review

Per Mandatory-Agent-Review-Rule §3.4, the producer must address every `FAIL` with a new
commit before re-requesting review. The two required fixes:

**FIX-1 (Check 1):** Change `include/dvm/hotness.hpp:25` from
`static constexpr std::uint32_t kDefaultThreshold = 5;` to
`static constexpr std::uint32_t kDefaultThreshold = 3;` — matching the producer's own
commit-message claim ("default 3"). Alternatively, the producer may update the commit
message and the review's Check 1 condition to say "default 5" — but the simpler and
lower-impact fix is to make the constant 3.

**FIX-2 (Check 3):** Change `src/opcodes_control.cpp:71-80` (`op_jmp`) to remove the
recording-state guard around `mark_branch_or_loop`, matching the four `op_br_*` handlers:

```cpp
OpResult op_jmp(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::int32_t delta = signed_delta32(cell.s2(), cell.s3());
  s.advance();
  // A JMP is always "taken" — mark it as a branch exit or loop close.
  mark_branch_or_loop(s, delta);   // <-- unconditional, like op_br_*
  s.branch(delta);
  return OpResult::Continue;
}
```

This is necessary so that a loop encoded with `JMP` as the backedge (instead of `BR_TRUE`)
also triggers hot-loop detection. The hotness path inside `mark_branch_or_loop` (lines 46-57)
already handles the not-recording case correctly; only `op_jmp`'s call site is wrong.

### 4.3 Non-blocking observations (informational, do not block approval)

1. **`MOV_CONST` lowering uses placeholder `CONST(0)`** (lifter.cpp:99-104) — acknowledged
   scope cut; full lifter would look up the constant pool. Mapping is correct, payload is
   approximate.
2. **Test 6 assertion uses lower bound** (`frag.length() < 3`) rather than exact equality
   (`frag.length() == 3`). Test passes for the right reason (length is exactly 3) but a
   future over-recording bug would slip past.
3. **Test 7 does not programmatically assert `report.ok==true`** — `print_lifted_graph`
   prints `verifier: ok=true pass=11 fail=0` but does not fail the test on `ok=false`. A
   defensive assertion would harden the test.
4. **`-ldgwcore` appears twice** in the final link command (Makefile line 60: once explicit,
   once via `$(LDFLAGS)`). Harmless duplication.
5. **`op_jmp` comment is misleading**: line 74 says "A JMP is always 'taken' — mark it as
   a branch exit or loop close" but the guard on line 75 means the marking only happens
   when recording. The comment and code disagree.

### 4.4 Final verdict

**CHANGES_REQUESTED.** Two FAIL conditions (Check 1 default threshold, Check 3 `op_jmp`
guard) must be addressed in a new commit before re-review. Per §3.4, a CHANGES_REQUESTED
review cannot be re-issued by the same reviewer (review-agent-014) until the producing
agent has pushed a new commit addressing FIX-1 and FIX-2. The fixes are localized (one
constant change in `hotness.hpp`, one guard removal in `opcodes_control.cpp`) and should
not regress any of the 8 passing checks. The Verifier is green (build exit 0, smoke exit 0,
all 7 tests PASS, LeakSanitizer clean), so the changes are required for spec-compliance
fidelity (producer claim vs. code consistency), not for runtime correctness on the
existing test fixtures.

---

**Reviewer agent ID:** `review-agent-014`
**Review timestamp (UTC):** `2026-09-04T22:06:00Z`
