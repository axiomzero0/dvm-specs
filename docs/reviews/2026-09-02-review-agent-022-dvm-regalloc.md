# REVIEW-022 — Linear-Scan Register Allocator Library + Native Codegen Fix (commit 2d27dce)

**Agent ID:** review-agent-022
**Task ID:** REVIEW-022
**Producer commit under review:** `2d27dce8888418321f1a1fb1fc68b7591ee7baab` — `runtime/interp: linear-scan register allocator library + native codegen fix`
**Commit author date:** 2026-09-11T21:36:11Z
**Review timestamp (UTC):** 2026-09-11T21:43:00Z
**Reviewer independence:** §3.1 satisfied — review-agent-022 has no authorship on any line under review (fresh reviewer per §3.4).

---

## 1. Pre-Work Log

1. **Read `/home/z/my-project/worklog.md`** (625 lines) — REVIEW-001 through REVIEW-021 entries present. Most recent prior review is REVIEW-021 APPROVED for the constant folding pass + Test 11 (commit 67c36a4). Prior pipeline context: REVIEW-016 APPROVED (trace compiler), REVIEW-017 APPROVED (native codegen, Test 9), REVIEW-018 CHANGES_REQUESTED (trace cache, Test 10) → REVIEW-019 APPROVED (fix), REVIEW-020 APPROVED (perf priorities spec), REVIEW-021 APPROVED (constfold + Test 11). Established guard: Tests 9–10 SKIPPED under `SAN=1` (JIT + ASan incompatible).
2. **Read `docs/DVM-Performance-Priorities.md`** §22 "Register Allocation Deserves Obscene Amounts of Attention" (lines 507–515). Spec text: *"For traces: linear scan first, because traces are naturally linear. For larger compiled regions: PBQP / graph coloring / optimized linear scan depending on complexity. Critically: allocate registers **after** trace scheduling, not before."* Priority Table §22 line 544: `Register allocation` listed as **S-tier (Massive payoff)**. The commit's linear-scan-over-DGW-graph approach matches §22's "linear scan first" guidance for traces.
3. **Read `docs/Mandatory-Agent-Review-Rule.md`** §3 in full — §3.1 independence, §3.2 spec-indexed review with PASS/FAIL/N/A verdicts, §3.3 mandatory verifier run, §3.4 blocking status (APPROVED / CHANGES_REQUESTED / REJECTED). §7 records format observed.

---

## 2. Files Under Review (per task description)

| # | File | Purpose | Verified |
|---|------|---------|----------|
| 1 | `runtime/interp/include/dvm/reg_alloc.hpp` | `LinearScanAllocator` class — live intervals, 9-register pool, spill support, entry-arg coalescing | Read in full (266 lines) |
| 2 | `runtime/interp/src/native_codegen.cpp` | Reverted to trivial `RegAllocator` (in-place update for ADD); LinearScanAllocator implemented but not yet activated | Read in full (284 lines) |

Supporting context read:
- `runtime/interp/include/dvm/x64_encoder.hpp` (180 lines) — defines `enum class Reg : std::uint8_t` (RAX=0, RCX=1, RDX=2, RBX=3, RSP=4, RBP=5, RSI=6, RDI=7, R8–R15=8–15) plus all encoder helpers (`encode_mov_imm64`, `encode_add_reg`, `encode_sub_reg`, `encode_cmp_reg`, `encode_setl`, etc.).
- `compiler/dgw-core/include/dgw/kinds.hpp` (113 lines) — `NodeKind` taxonomy (CONST, ADD, SUB, MUL, ..., STATE, FWD, DEAD), `EdgeKind` taxonomy (VALUE, CONTROL, EXCEPT, MEMORY, EFFECT, GUARD).
- `compiler/dgw-core/include/dgw/signatures.hpp` (312 lines) — `PortSlot.kind` is `EdgeKind`; `NodeSignature.inputs` is `std::span<const PortSlot>`; `signature_of(NodeKind)` returns canonical signature.
- `compiler/dgw-core/include/dgw/arena.hpp` (line 53, 70–72) — `port_connected_edge[]` stores input edges on the target node; `edge_source_node[e.value]` returns source `NodeId`; `node_port_offset[]`/`node_port_count[]` per-node port layout.
- `compiler/dgw-core/include/dgw/ids.hpp` — `EdgeId.value` is `std::uint32_t`, `valid()` checks non-null.
- `compiler/dgw-core/include/dgw/weaver.hpp` (lines 181–184) — `Weaver::input_node(NodeId, PortId)` returns source `NodeId` of input edge.
- `runtime/interp/Makefile` (81 lines) — `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter`; per-file exception `-Wno-pedantic` only for `interp.cpp` (computed-goto GNU extension).
- `runtime/interp/tests/smoke.cpp` (1072 lines) — Test 1–11, including Test 9 (native codegen + execution) and Test 10 (trace cache + native dispatch) under `#if !defined(__SANITIZE_ADDRESS__)`.

---

## 3. Producer Self-Audit Checklist (per §5.1)

The producer's commit message (extracted via `git log -1 2d27dce`) claims:
- ✅ Computes live intervals from the DGW graph's use-def chains
- ✅ Walks intervals in definition order, assigning free registers
- ✅ Expires intervals whose last-use has passed
- ✅ Spills to stack when all registers exhausted
- ✅ Coalesces entry-arg CONSTs to System V argument registers
- ✅ 9-register pool: rdi, rsi, rdx, rcx, r8-r11, rax
- ✅ Native codegen falls back to trivial `RegAllocator` for correctness (lifter doesn't model STATE backedges in loop traces)
- ✅ LinearScanAllocator builds clean, no warnings

Spec citations in the commit message:
- `DVM-Performance-Priorities.md §22 (Register Allocation)`
- `Mandatory-Agent-Review-Rule.md §3 (new code requires review)`

The in-file header comment on `reg_alloc.hpp:2–4` cites §22 verbatim and quotes the spec text "For traces: linear scan first, because traces are naturally linear." This satisfies §5.2's "include spec citations" intent. (Non-blocking observation: the commit-message `Spec citation:` prefix format from REVIEW-021 N6 is still not adopted — would help CI machine-parseability.)

---

## 4. Reviewer Spec-Indexed Verdicts (§3.2)

### Check 1 — LinearScanAllocator exists (PASS)

**Spec:** Task-description Check 1; Performance Priorities §22 ("linear scan first").

**Verdict: PASS.**

`runtime/interp/include/dvm/reg_alloc.hpp:54` declares `class LinearScanAllocator`. Verified all required public members:
- `static constexpr Reg kRegisters[]` at line 58 — register pool.
- `static constexpr int kNumRegisters = 9` at line 63 — pool size constant.
- `explicit LinearScanAllocator(const GraphArena& arena, const Weaver& w)` at line 66 — constructor runs `compute_live_intervals()` then `allocate()`.
- `Reg get_reg(std::uint32_t node_id) const` at line 73 — returns assigned register or `Reg::RAX` if not found / spilled.
- `bool is_spilled(std::uint32_t node_id) const` at line 79 — spill flag.
- `std::int32_t get_spill_offset(std::uint32_t node_id) const` at line 85 — stack offset if spilled.
- `std::int32_t frame_size() const noexcept` at line 91 — total spill frame size.
- `const std::vector<LiveInterval>& intervals() const noexcept` at line 94 — debug accessor.

Private state (lines 99–103): `arena_`, `w_`, `intervals_` (unordered_map node_id→LiveInterval), `interval_list_` (sorted vector), `frame_size_`. Two private methods: `compute_live_intervals()` (line 106) and `allocate()` (line 161); spill helper `spill(LiveInterval&, std::int32_t&)` at line 259. The `LiveInterval` struct (lines 43–51) carries `node_id`, `def_point`, `last_use`, `is_entry_arg`, `assigned_reg`, `spilled`, `spill_offset`.

Empirical verification: compiled a small driver (`/tmp/test_regalloc.cpp`) that constructs a 5-node graph (START + 4 CONSTs) and instantiates `LinearScanAllocator`. The build succeeded under the project's full warning set; the runtime produced well-formed interval records. See §5 for the verifier log.

### Check 2 — Live intervals computed from use-def chains (PASS)

**Spec:** Task-description Check 2; Performance Priorities §22 (live intervals as the prerequisite for linear scan).

**Verdict: PASS.**

`compute_live_intervals()` (reg_alloc.hpp:106–158) implements the three required behaviors:

1. **Walks all non-DEAD/non-FWD nodes** — line 109: `for (std::uint32_t n = 0; n < arena_.node_count(); ++n)`. Line 111 skips DEAD and FWD: `if (kind == NodeKind::DEAD || kind == NodeKind::FWD) continue;`. ✓

2. **Creates intervals** — lines 114–124: if the node has no interval yet, default-constructs a `LiveInterval` with `def_point = n`, `last_use = n` (conservative default — assumes used immediately or not at all), `is_entry_arg = false`, `assigned_reg = Reg::RAX`, `spilled = false`, `spill_offset = 0`. Inserts into `intervals_[n]`. ✓

3. **Updates `last_use` when a node reads from another node via VALUE edges** — lines 126–143: For each non-DEAD/non-FWD node `n`, walks its VALUE inputs:
   - Looks up the node's port signature via `signature_of(kind)` (line 127).
   - Reads `node_port_offset[n]` and `node_port_count[n]` (lines 128–129).
   - For each input port `p` in `[0, min(sig.inputs.size(), cnt))` (line 131), skips non-VALUE ports (line 132): `if (sig.inputs[p].kind != dgw::EdgeKind::VALUE) continue;`.
   - Reads the connected edge: `EdgeId e{arena_.port_connected_edge[off + p]}` (line 133); skips if not valid (line 134).
   - Reads the source node: `std::uint32_t src = arena_.edge_source_node[e.value].value` (line 135).
   - Updates `src`'s interval: `if (n > it->second.last_use) it->second.last_use = n` (line 139) — `last_use` is monotonically increasing (max of current and `n`). Silently skips if src has no interval (DEAD/FWD source — line 140–142).

Empirical verification: compiled a 5-node graph `START → CONST(1), CONST(2) → ADD → RETURN` and ran the allocator. Output:
```
node 0 (START):  def=0 last_use=0
node 1 (CONST): def=1 last_use=3   ← used by ADD at node 3
node 2 (CONST): def=2 last_use=3   ← used by ADD at node 3
node 3 (ADD):   def=3 last_use=4   ← used by RETURN at node 4
node 4 (RET):   def=4 last_use=4
```
All `last_use` values are correct: each non-START/non-RETURN node's `last_use` equals the highest index of a node that reads from it via a VALUE edge. ✓

(Non-blocking N1: When the source of a VALUE edge is a `FWD` node (rather than the underlying value), `intervals_.find(src)` returns `end()` and the FWD's *target's* `last_use` is NOT updated. In the current pipeline FWDs appear only after `pass_cleanup` collapses them, and the allocator is not yet activated, so this is latent. When the allocator is activated post-lifter-fix, the implementer should chase FWD chains via `Weaver::forward_node()` or run after `pass_cleanup`.)

### Check 3 — Register pool (PASS)

**Spec:** Task-description Check 3; System V AMD64 calling convention (rdi/rsi/rdx/rcx = first 4 integer args, r8–r11 = caller-saved temps, rax = return value).

**Verdict: PASS.**

reg_alloc.hpp:58–62 defines the register pool:
```cpp
static constexpr Reg kRegisters[] = {
  Reg::RDI, Reg::RSI, Reg::RDX, Reg::RCX,  // args (entry regs)
  Reg::R8,  Reg::R9,  Reg::R10, Reg::R11,   // caller-saved temps
  Reg::RAX,                                   // return value / temp
};
```
Line 63: `static constexpr int kNumRegisters = 9;` ✓

The 9 registers exactly match the task specification: rdi, rsi, rdx, rcx, r8, r9, r10, r11, rax. No callee-saved registers (rbx, r12–r15) are in the pool — they would require prologue/epilogue saves. rsp/rbp are correctly excluded (frame management registers). ✓

Verified `Reg` enum values against `x64_encoder.hpp:29–34`: `RDI=7, RSI=6, RDX=2, RCX=1, R8=8, R9=9, R10=10, R11=11, RAX=0`. All distinct, all valid x86-64 general-purpose registers.

### Check 4 — Entry-arg coalescing (PASS)

**Spec:** Task-description Check 4; System V AMD64 calling convention.

**Verdict: PASS.**

Two-stage implementation:

**Stage 1 — Marking entry-arg CONSTs (reg_alloc.hpp:146–157):**
```cpp
int entry_count = 0;
for (std::uint32_t n = 0; n < arena_.node_count(); ++n) {
  if (arena_.node_kinds[n] == NodeKind::CONST) {
    if (entry_count < 4) {
      intervals_[n].is_entry_arg = true;
      entry_count++;
    } else {
      break;  // first 4 only
    }
  }
}
```
First 4 CONST nodes (in node-index order) are marked `is_entry_arg = true`. ✓

**Stage 2 — Assigning argument registers (reg_alloc.hpp:197–218):**
```cpp
if (li.is_entry_arg && free_regs.size() >= 4) {
  int arg_idx = 0;
  for (std::size_t j = 0; j < i; ++j) {
    if (interval_list_[j].is_entry_arg) arg_idx++;
  }
  if (arg_idx < 4) {
    Reg want = kRegisters[arg_idx];  // RDI, RSI, RDX, RCX in order
    auto fit = std::find(free_regs.begin(), free_regs.end(), want);
    if (fit != free_regs.end()) free_regs.erase(fit);
    li.assigned_reg = want;
  }
  ...
}
```
The Nth entry-arg CONST (0-indexed) is assigned `kRegisters[N]`:
- arg_idx=0 → `kRegisters[0]` = `Reg::RDI` ✓
- arg_idx=1 → `kRegisters[1]` = `Reg::RSI` ✓
- arg_idx=2 → `kRegisters[2]` = `Reg::RDX` ✓
- arg_idx=3 → `kRegisters[3]` = `Reg::RCX` ✓

These are exactly the System V AMD64 integer-argument registers in the calling-convention order. ✓

Empirical verification: 5-node graph (START + 4 CONSTs) allocator output:
```
node 1 (CONST): reg=7 (RDI), entry=1
node 2 (CONST): reg=6 (RSI), entry=1
node 3 (CONST): reg=2 (RDX), entry=1
node 4 (CONST): reg=1 (RCX), entry=1
```
All four entry-arg CONSTs map to the correct argument registers. ✓

### Check 5 — Native codegen uses trivial allocator (PASS)

**Spec:** Task-description Check 5; Mandatory-Agent-Review-Rule §3 (new code requires review — the codegen decision must be explicit).

**Verdict: PASS.**

**5a — Uses `RegAllocator`, not `LinearScanAllocator`:**
- `runtime/interp/src/native_codegen.cpp:35–70` defines a local anonymous-namespace `struct RegAllocator` (the trivial round-robin allocator).
- Line 77 instantiates it: `RegAllocator ra;`.
- `grep -E 'reg_alloc\.hpp|LinearScanAllocator' runtime/interp/` confirms:
  - `reg_alloc.hpp` is included by NO source file (only the header self-references in its own file comment at line 1).
  - `LinearScanAllocator` is referenced in `native_codegen.cpp:39` only as a comment (`// This is replaced by LinearScanAllocator (reg_alloc.hpp) for future use`).
- So the codegen does NOT use the linear-scan allocator. ✓

**5b — In-place update for arithmetic ops:**
`native_codegen.cpp:119–139` (ADD/SUB/MUL case):
```cpp
case NodeKind::ADD:
case NodeKind::SUB:
case NodeKind::MUL: {
  if (loop_start_offset == 0) {
    loop_start_offset = buf.offset();
  }
  NodeId src0 = w.input_node(NodeId{n}, PortId{0});
  NodeId src1 = w.input_node(NodeId{n}, PortId{1});
  // Coalesce: the ADD writes in-place to src0's register (matches
  // CRB register-machine semantics: r0 = r0 + r1). ...
  Reg dst = ra.has(src0.value) ? ra.get(src0.value) : ra.alloc(n); if (!ra.has(n)) ra.map[n] = dst;
  Reg r1 = ra.has(src1.value) ? ra.get(src1.value) : Reg::RSI;
  if (kind == NodeKind::ADD) encode_add_reg(buf, dst, r1);
  else if (kind == NodeKind::SUB) encode_sub_reg(buf, dst, r1);
  else if (kind == NodeKind::MUL) encode_add_reg(buf, dst, r1);
  break;
}
```
Line 133: `Reg dst = ra.has(src0.value) ? ra.get(src0.value) : ra.alloc(n);` — when src0 already has a register assigned (the common case for entry-arg CONSTs already processed by the CONST case), the ADD result is written to src0's register. This is the in-place update behavior matching CRB register-machine semantics (`r0 = r0 + r1`). The result is then also bound to node `n` itself: `if (!ra.has(n)) ra.map[n] = dst`. ✓

The comment at lines 127–132 explicitly explains the rationale: *"Coalesce: the ADD writes in-place to src0's register (matches CRB register-machine semantics: r0 = r0 + r1). This is essential for loop traces where the ADD's output feeds back through the STATE node into the ADD's input on the next iteration."* ✓

The native codegen falls back to the trivial allocator because, as the commit message explains, the lifter doesn't yet model STATE backedges in loop traces: the linear-scan allocator would assign the ADD's result to a different register than src0, but the CMP at the next iteration reads from src0's register (the original counter) — breaking the loop update. The trivial allocator's in-place coalesce sidesteps this lifter limitation. ✓

### Check 6 — LinearScanAllocator builds clean (PASS)

**Spec:** Task-description Check 6; Mandatory-Agent-Review-Rule §3.2 (the new code under review must satisfy the project's own warning policy).

**Verdict: PASS.**

The header is **not included by any source file** in the build (verified via `grep -r 'reg_alloc\.hpp' runtime/interp/src/ runtime/interp/tests/` returning no matches), so the standard `make` cannot catch errors in it. To exercise the header explicitly, ran:

```bash
$ g++ -Iinclude -I../../compiler/dgw-core/include -std=c++26 \
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion \
    -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef \
    -Werror -Wno-unused-parameter -O2 -g -fsyntax-only -x c++ - <<EOF
#include "dvm/reg_alloc.hpp"
int main() { return 0; }
EOF
$ echo "exit=$?"
exit=0
```
The header parses cleanly under the project's full warning set + `-Werror`. ✓

Additionally, an end-to-end functional driver was compiled (`/tmp/test_regalloc.cpp` and `/tmp/test_regalloc2.cpp`) that:
- Constructs a `GraphArena` + `Weaver`
- Builds a 5-node graph (START + 4 CONSTs) — and a 5-node graph (START + 2 CONSTs + ADD + RETURN)
- Instantiates `LinearScanAllocator lsa(arena, w)`
- Iterates `lsa.intervals()` and prints the result

Both drivers compiled clean (zero warnings, zero errors under the full warning set + `-Werror`) and linked against `libdgwcore.a`. Output confirmed correct live-interval computation and entry-arg coalescing (see §5 verifier log). ✓

### Check 7 — Build clean + all 11 tests pass (PASS)

**Spec:** Task-description Check 7; Mandatory-Agent-Review-Rule §3.3 (verifier run mandatory).

**Verdict: PASS.**

**7a — `make -j$(nproc)` exit 0, zero warnings:**

```bash
$ cd /home/z/my-project/dgw-core-repo/compiler/dgw-core && make clean && make -j$(nproc) 2>&1 | tail -3
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion \
    -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror \
    -Wno-unused-parameter -O2 -g   -c src/weaver.cpp -o build/weaver.o
ar rcs build/libdgwcore.a build/control.o build/graph.o build/pass_cleanup.o \
    build/pass_constfold.o build/pass_dce.o build/pass_gvn.o build/pass_licm.o \
    build/scheduler.o build/signatures.o build/verifier.o build/weaver.o
g++ -std=c++26 -Wall -Wextra ... -Iinclude tests/smoke.cpp -Lbuild -ldgwcore \
    -o bin/dgw_smoke
$ cd /home/z/my-project/dgw-core-repo/runtime/interp && make clean && make -j$(nproc) 2>&1 | tail -3
g++ -Iinclude -I../../compiler/dgw-core/include -std=c++26 -Wall -Wextra -Wpedantic \
    -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast \
    -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g \
    -c src/trace_compiler.cpp -o build/trace_compiler.o
ar rcs build/libdvm_interp.a build/interp.o build/jit_buffer.o build/lifter.o \
    build/loader.o build/module.o build/native_codegen.o build/opcodes_arith.o \
    build/opcodes_calls.o build/opcodes_control.o build/opcodes_except.o \
    build/opcodes_move.o build/opcodes_object.o build/opcodes_sys.o build/state.o \
    build/trace.o build/trace_compiler.o
g++ -std=c++26 -Wall -Wextra ... -Iinclude -I../../compiler/dgw-core/include \
    tests/smoke.cpp -Lbuild -ldvm_interp -L../../compiler/dgw-core/build -ldgwcore \
    -o bin/dvm_interp_smoke  -L../../compiler/dgw-core/build -ldgwcore
```
Both builds exit 0 with zero warning/error output. ✓

**7b — `./bin/dvm_interp_smoke` exit 0, all 11 tests PASS:**

```bash
$ timeout 10 ./bin/dvm_interp_smoke 2>&1 | tail -10

-- Test 11: constant folding pass (non-loop trace) --
  Trace: 6 instructions, exit=2, loop=no
  ConstFold: folded=1, simplified=0, visited=13
  DCE: killed=6, live=7
  Graph: 12→13 nodes
  Verifier: ok=true
Test 11: constant folding: 1 operations folded, verifier ok (PASS)

== DVM Interpreter smoke test PASSED ==
```

Full per-test results (`grep -E '^Test [0-9]+|PASSED|FAIL'`):
```
Test 1: 20 + 22 = 42 (PASS)
Test 2: fn1 calls fn0(40 + 2) = 42 (PASS)
Test 3: count to 10 = 10 (PASS)
Test 4: ALLOC + OBJ_SET(42) + OBJ_GET = 42 (PASS)
Test 5: trace of 6 instructions, exit=BranchTaken (PASS)
Test 6: hot-loop detected at PC=3, trace of 3 instrs, LoopClose (PASS)
Test 7: lifted graph has 10 nodes, 8 edges, ADD+CMP present (PASS)
Test 8: compiled trace: 10→10 nodes, 1 blocks, 3 ops, verifier ok (PASS)
Test 9: native x86-64 code executed, result = 10 (PASS)
Test 10: trace cache: 3 runs (1 interp + 2 native), all results = 10, execution_count = 2 (PASS)
Test 11: constant folding: 1 operations folded, verifier ok (PASS)
== DVM Interpreter smoke test PASSED ==
```
Tests 9 and 10 (native codegen + trace-cache-dispatch) are wrapped in `#if !defined(__SANITIZE_ADDRESS__)` per REVIEW-018/019 established guard; the no-SAN build runs all 11. ✓

**7c — WebVerifier green:** Test 8 output `Verifier: ok=true pass=11 fail=0` (post-opt) and Test 11 output `Verifier: ok=true` confirm the WebVerifier (Part 8) runs green on all DGW graphs touched. ✓

### Check 8 — No warning suppressions (FAIL)

**Spec:** Task-description Check 8; project Makefile policy at `runtime/interp/Makefile:10–13`:
> All warnings are errors. **No lazy unused-variable suppressions**; if a variable is set but not used, either use it or delete it. Being lazy about warnings hides bugs.

**Verdict: FAIL.**

Grep results (filtering the 16 `#pragma once` header guards, which are not warning suppressions per REVIEW-018 §Check 8's established interpretation):

```bash
$ grep -rnE '\(void\)|#pragma GCC|__attribute__' runtime/interp/
runtime/interp/tests/smoke.cpp:1025:    (void)result11;
```

One match: `(void)result11;` at `tests/smoke.cpp:1025`. This is a textbook "lazy unused-variable suppression" — exactly the pattern the Makefile policy explicitly forbids.

`git blame` localizes the regression:
```bash
$ git blame runtime/interp/tests/smoke.cpp -L 1025,1025
67c36a42 (Z User 2026-09-08 22:05:44 +0000 1025)     (void)result11;
```

The `(void)result11;` cast was introduced by commit `67c36a4` (the Test 11 / constant folding commit, REVIEW-021) — NOT by the commit under review (2d27dce). REVIEW-021's Check 8 was scoped to "Build clean + all 11 tests pass" and did not include the warning-suppressions grep, so this regression slipped past REVIEW-021 and is now caught by REVIEW-022's broader Check 8.

**Root cause** (smoke.cpp:1023–1027):
```cpp
TraceRecorder recorder11;
Value result11 = interpret(lr11.module, 0, &recorder11);
(void)result11;

TraceFragment frag11 = recorder11.stop();
```
`result11` is the interpreter's return value (the program result for the side-exit trace), but Test 11 cares only about the *recorded trace* (`frag11`), not the runtime result. The `interpret()` call is needed for its side effect (recording), but its return value is genuinely unused.

The Makefile policy is explicit: "either use it or delete it." The minimal fix is to delete the binding:
```cpp
interpret(lr11.module, 0, &recorder11);  // result unused; record only
```
Or to actually use the result (e.g., assert against an expected value).

**Severity:** low (cosmetic + style-policy violation; not a runtime correctness bug — Test 11 PASSES because the assertion at line 1052 only checks `s11.constfold.folded >= 1`, not the interpreter result). But per the project's own Makefile policy, this is a FAIL — the producer must address it with a new commit before this review can move to APPROVED.

---

## 5. Verifier Run Log (§3.3)

### 5.1 — Mandatory run

Per the task's "Mandatory: run" block:

```bash
$ cd /home/z/my-project/dgw-core-repo/compiler/dgw-core && make clean && make -j$(nproc) 2>&1 | tail -3
g++ -Iinclude -std=c++26 ... -c src/weaver.cpp -o build/weaver.o
ar rcs build/libdgwcore.a build/control.o build/graph.o build/pass_cleanup.o ...
g++ -std=c++26 ... -Iinclude tests/smoke.cpp -Lbuild -ldgwcore -o bin/dgw_smoke

$ cd /home/z/my-project/dgw-core-repo/runtime/interp && make clean && make -j$(nproc) 2>&1 | tail -3
g++ ... -c src/trace_compiler.cpp -o build/trace_compiler.o
ar rcs build/libdvm_interp.a build/interp.o build/jit_buffer.o ...
g++ ... tests/smoke.cpp -Lbuild -ldvm_interp -L../../compiler/dgw-core/build -ldgwcore -o bin/dvm_interp_smoke

$ timeout 10 ./bin/dvm_interp_smoke 2>&1 | tail -10

-- Test 11: constant folding pass (non-loop trace) --
  Trace: 6 instructions, exit=2, loop=no
  ConstFold: folded=1, simplified=0, visited=13
  DCE: killed=6, live=7
  Graph: 12→13 nodes
  Verifier: ok=true
Test 11: constant folding: 1 operations folded, verifier ok (PASS)

== DVM Interpreter smoke test PASSED ==
```
Both builds exit 0, zero warnings. dvm_interp_smoke exits 0 with all 11 tests passing. ✓

### 5.2 — WebVerifier (Part 8 of DGW-Core-IR.md) results

The smoke harness invokes the WebVerifier on:
- Test 7 lifted graph: `verifier: ok=true pass=11 fail=0` (smoke.cpp output line for Test 7).
- Test 8 compiled graph: `Verifier: ok=true pass=11 fail=0` (post-GVN+DCE+Cleanup+Schedule).
- Test 11 constfold graph: `Verifier: ok=true` (post-const-fold + DCE).

WebVerifier green across all touched graphs. ✓

### 5.3 — LinearScanAllocator functional verification (additional, beyond mandatory)

To exercise the otherwise-unused `reg_alloc.hpp` header (no source file includes it), compiled and ran two functional drivers against `libdgwcore.a`:

**Driver 1:** 5-node graph (START + 4 CONSTs, no edges between them):
```
kNumRegisters=9
frame_size=8                  ← see N2
interval_count=5
  node 0 (START): def=0 last_use=0 reg=0 (RAX)  entry=0
  node 1 (CONST): def=1 last_use=1 reg=7 (RDI)  entry=1
  node 2 (CONST): def=2 last_use=2 reg=6 (RSI)  entry=1
  node 3 (CONST): def=3 last_use=3 reg=2 (RDX)  entry=1
  node 4 (CONST): def=4 last_use=4 reg=1 (RCX)  entry=1
```
Entry-arg coalescing verified: first 4 CONSTs map to RDI/RSI/RDX/RCX in calling-convention order. ✓

**Driver 2:** 5-node graph `START → CONST(1), CONST(2) → ADD → RETURN`:
```
kNumRegisters=9
frame_size=8                  ← see N2
interval_count=5
  node 0 (START): def=0 last_use=0 reg=0 (RAX)  entry=0
  node 1 (CONST): def=1 last_use=3 reg=7 (RDI)  entry=1
  node 2 (CONST): def=2 last_use=3 reg=6 (RSI)  entry=1
  node 3 (ADD):   def=3 last_use=4 reg=0 (RAX)  entry=0
  node 4 (RET):   def=4 last_use=4 reg=6 (RSI)  entry=0
```
Live-interval computation verified:
- CONST(1) used by ADD at node 3 → last_use=3 ✓
- CONST(2) used by ADD at node 3 → last_use=3 ✓
- ADD used by RETURN at node 4 → last_use=4 ✓

Both drivers compiled clean under the project's full warning set + `-Werror` and ran without ASan/UBSan reports. ✓

---

## 6. Non-Blocking Observations (informational, not FAILs)

These are observations for the producer's future cleanup; none are blocking for this review (Check 8 alone is the blocking FAIL).

- **N1 (live-interval FWD chase):** `compute_live_intervals` (reg_alloc.hpp:135–142) looks up `intervals_.find(src)` and silently skips when the source is a FWD or DEAD node (the `else` branch is a no-op comment). In a graph where a node reads from a FWD (rather than the underlying value), the FWD's target's `last_use` is NOT updated — the interval ends prematurely at the FWD's own def_point, potentially freeing the register too early. In the current pipeline FWDs are introduced by `pass_gvn`/`pass_constfold` and collapsed by `pass_cleanup`; since the allocator is not yet activated, this is latent. Fix at activation time: either run after `pass_cleanup`, or chase FWD chains via `Weaver::forward_node()`.

- **N2 (frame_size over-reports by 8 bytes):** reg_alloc.hpp:256 `frame_size_ = (-next_spill_offset - 8) + 8;`. The `-8` and `+8` cancel, so this simplifies to `frame_size_ = -next_spill_offset`. With `next_spill_offset` initialized to `-8` (line 179) and decremented by 8 per spill, this reports 8 bytes even with zero spills (Driver 1 above shows `frame_size=8` with zero spills in a 5-CONST graph), 16 with one spill, 24 with two, etc. The correct formula would be `frame_size_ = -next_spill_offset - 8` (returns 0 with zero spills, 8 per spill). Latent — the allocator isn't activated, but the formula should be fixed before activation to avoid wasting 8 bytes of stack per native trace.

- **N3 (MUL is still ADD):** `native_codegen.cpp:137` `else if (kind == NodeKind::MUL) encode_add_reg(buf, dst, r1);` — the MUL case still calls `encode_add_reg` (i.e., MUL produces `dst + r1` rather than `dst * r1`). The 2d27dce diff removed the `// placeholder` comment but kept the actual placeholder behavior. This is a latent bug — no smoke test exercises MUL, so Test 1–11 all pass. Should be fixed when a proper `encode_imul_reg` is added to `x64_encoder.hpp`. Note: the prior REVIEW-017 review already noted this as an accepted limitation; the 2d27dce commit does not regress it but also does not fix it.

- **N4 (compound statement on one line):** `native_codegen.cpp:133` packs a declaration, conditional expression, and an `if` statement onto a single line: `Reg dst = ra.has(src0.value) ? ra.get(src0.value) : ra.alloc(n); if (!ra.has(n)) ra.map[n] = dst;`. This compiles cleanly but is hard to read; should be split into two lines for clarity. Style-only, not a spec violation.

- **N5 (reg_alloc.hpp is unused by the build):** No source file in `runtime/interp/src/` or `runtime/interp/tests/` includes `dvm/reg_alloc.hpp`. The header is therefore never compiled by `make`. This is consistent with the producer's stated intent ("library is ready for activation once the lifter is improved"), but it means latent bugs in the allocator wouldn't be caught by the smoke tests. Mitigation: my Check 6 verification explicitly syntax-checked the header (`-fsyntax-only`) and exercised it via `/tmp/test_regalloc*.cpp` drivers linked against `libdgwcore.a` — both clean.

- **N6 (commit-message spec citation format):** The commit message's spec citations use the form `Spec citations:` (lowercase 'c') rather than the `Spec citation:` prefix suggested in REVIEW-021 N6. Adopting a single canonical prefix (`Spec citation:`) would help CI parse machine-readable citations per Mandatory-Agent-Review-Rule §6.3.

- **N7 (allocator picks RAX before RDX/RCX/R8-R11 for non-entry-arg nodes):** reg_alloc.hpp:219–221 `li.assigned_reg = free_regs.back(); free_regs.pop_back();` — for non-entry-arg nodes, takes the BACK of the free-pool vector, which is `RAX` (the last entry of `kRegisters`). This means RAX gets used before RDX/RCX/R8–R11 in low-pressure graphs (see Driver 2 above: ADD at node 3 gets RAX even though RDX, RCX, R8–R11 are still free). For low-pressure traces this is harmless, but `RAX` should ideally be reserved as the last-resort scratch (it's also the return-value register at RETURN). Consider `free_regs.front()` for non-entry nodes, or use a priority queue that prefers non-RAX. Latent — allocator not yet activated.

- **N8 (const count scan for entry-arg marking is O(N×K)):** reg_alloc.hpp:200–203 — for each entry-arg interval `i`, walks all `j < i` to count prior entry args (`arg_idx`). This is O(N) per interval, O(N²) total in the worst case (though N is small in practice — trace length). Could be replaced with a single counter incremented per-entry-arg-processed. Latent — allocator not yet activated, and trace sizes are small.

---

## 7. Final Review Status (§3.4)

| Check | Subject | Verdict |
|-------|---------|---------|
| 1 | LinearScanAllocator class with required API | **PASS** |
| 2 | Live intervals computed from use-def chains | **PASS** |
| 3 | 9-register pool (rdi/rsi/rdx/rcx/r8-r11/rax) | **PASS** |
| 4 | Entry-arg CONSTs coalesced to System V arg regs | **PASS** |
| 5 | Native codegen uses trivial RegAllocator with in-place update | **PASS** |
| 6 | reg_alloc.hpp builds clean under -Werror | **PASS** |
| 7 | make + smoke: 0 warnings, 11/11 tests pass | **PASS** |
| 8 | No warning suppressions in runtime/interp/ | **FAIL** |

**Final verdict: CHANGES_REQUESTED.**

7 PASS, 1 FAIL, 0 N/A.

**WebVerifier:** green (ok=true, pass=11, fail=0 on all DGW graphs touched by Tests 7, 8, 11).

**Build:** clean (0 warnings, 0 errors under `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter`) for both `compiler/dgw-core` and `runtime/interp`.

**Smoke:** `dvm_interp_smoke` exits 0 with 11/11 PASS (no-SAN build; Tests 9+10 under `#if !defined(__SANITIZE_ADDRESS__)` per REVIEW-018/019 established guard).

**The single FAIL (Check 8):** a `(void)result11;` cast at `runtime/interp/tests/smoke.cpp:1025` is a "lazy unused-variable suppression" — exactly the pattern the project's own `runtime/interp/Makefile:10–13` policy explicitly forbids. This regression was introduced by commit `67c36a4` (the constant-folding + Test 11 commit, REVIEW-021), not by the commit under review (`2d27dce`). REVIEW-021's Check 8 was scoped to "Build clean + all 11 tests pass" and did not include the warning-suppressions grep; REVIEW-022's broader Check 8 catches the regression.

Per Mandatory-Agent-Review-Rule §3.4 and §5.4, the producer must address this FAIL with a new commit (not an in-place edit). The minimal fix is to drop the binding at smoke.cpp:1024 (call `interpret(...)` directly without assigning to `result11`), or to actually use the result. Ping-pong without a new commit is forbidden; the same reviewer (review-agent-022) cannot re-review until a new commit is pushed.

**Library assessment:** The `LinearScanAllocator` library itself (reg_alloc.hpp, 266 lines) is correctly implemented per Performance Priorities §22: live intervals are computed correctly from the use-def chains (verified empirically), the 9-register pool is correctly sized, entry-arg coalescing correctly maps the first 4 CONSTs to RDI/RSI/RDX/RCX, the header builds clean under the full warning set + `-Werror`, and the spill path is implemented (though the `frame_size` formula has an off-by-8 bug — N2). The library is correctly NOT activated in `native_codegen.cpp` because the lifter doesn't yet model STATE backedges in loop traces; the trivial `RegAllocator` with in-place ADD→src0 coalescing correctly sidesteps the lifter limitation. Once the lifter is fixed, the library is ready for activation (with the N1 FWD-chase and N2 frame_size fixes applied first).

---

**Reviewer agent ID:** review-agent-022
**Review timestamp (UTC):** 2026-09-11T21:43:00Z
**Report path:** `/home/z/my-project/dgw-core-repo/docs/reviews/2026-09-02-review-agent-022-dvm-regalloc.md`
