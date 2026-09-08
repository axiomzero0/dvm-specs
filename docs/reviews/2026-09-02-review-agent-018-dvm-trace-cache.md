# REVIEW-018 — DVM Trace Cache + Lifter Fix + Test 10

- **Reviewer agent ID:** `review-agent-018`
- **UTC timestamp:** `2026-09-08T19:49:21Z`
- **Producer commit under review:** `deeb19db43897c2e227c9a8db668c3b0aa945edc`
  (`runtime/interp: trace cache + fixed lifter + Test 10 (native dispatch)`)
- **Files touched (per `git show --stat deeb19d`):**
  - `runtime/interp/include/dvm/trace_cache.hpp` (NEW, 99 lines)
  - `runtime/interp/include/dvm/lifter.hpp` (1-line signature change)
  - `runtime/interp/src/lifter.cpp` (27 lines, +23/-4)
  - `runtime/interp/src/native_codegen.cpp` (35 lines, +25/-13)
  - `runtime/interp/tests/smoke.cpp` (143 lines, +142/-1)
  - 8 other files touched with 0-line mode changes (mode bits only)
- **Spec corpus cited by producer:** `DVM-Hybrid-Tracing-Architecture.md §12.4`
  (trace cache), `§13` (execution); `Mandatory-Agent-Review-Rule.md §3`.
- **Spec corpus reviewed by reviewer:** `DVM-Hybrid-Tracing-Architecture.md §4.6`
  (the actual trace-cache section), `§12.4` (recording calls), `§13.1`–`§13.4`
  (side exits + bridges), `Mandatory-Agent-Review-Rule.md §3` in full.

---

## 0. Producer Self-Audit (from commit message)

The producer's self-audit checklist (quoted verbatim from the commit message):

> TraceCache (include/dvm/trace_cache.hpp):
>   - Maps (function_id, loop_pc) → CachedTrace (fragment + compiled + native)
>   - call_native(): calls the cached native code directly
>   - lookup()/has()/size() for cache queries
>   - execution_count tracks how many times native code was called
>
> Lifter fix (src/lifter.cpp):
>   - MOV_CONST now looks up actual constant values from the module's
>     constant pool instead of using placeholder CONST(0)
>   - lift_trace() accepts an optional crb::Module* parameter
>   - Integer/bool/float constants are properly resolved
>
> Native codegen fix (src/native_codegen.cpp):
>   - Entry-register CONST nodes (mapped to rdi/rsi/rdx/rcx) are NOT
>     overwritten with mov imm64 — the function arguments provide the
>     actual values. Non-entry constants ARE emitted as mov imm64.
>
> Test 9/10 guard:
>   - Tests 9+10 are conditionally compiled with #if !defined(__SANITIZE_ADDRESS__)
>   - Under SAN=1: Tests 9+10 are SKIPPED (with a print message)
>   - Without SAN: all 10 tests run
>
> Smoke Test 10: trace caching + native dispatch
>   - Run 1: interpreter records + compiles + caches the trace → result=10
>   - Run 2: cache hit → calls native x86-64 code directly → result=10
>   - Run 3: cache hit again → result=10, execution_count=2
>
> Build (without SAN): make → exit 0, zero warnings, 10/10 tests pass.
> Build (with SAN=1): make SAN=1 → exit 0, zero warnings, 8/8 tests pass
>   (Tests 9-10 SKIPPED).

The reviewer independently verified each item below.

---

## 1. Per-Check Verdicts (Section 3.2 of the Mandatory-Agent-Review-Rule)

### Check 1 — `TraceCache` API surface (PASS)

**Spec:** `DVM-Hybrid-Tracing-Architecture.md §4.6` (the *actual* trace-cache
spec; the producer's `§12.4` citation is wrong — §12.4 in the live doc is
"Recording calls"). §4.6 lists what a trace cache entry must contain
(trace ID, kind, entry point, entry state signature, dependencies, guard
metadata, side exits, bridges, FrameState tables, GC maps, telemetry,
compilation version).

**Code under review:** `runtime/interp/include/dvm/trace_cache.hpp` (99 lines).

Verdict: **PASS** on the API surface that the check actually enumerates
(`lookup`, `store`, `has`, `call_native`, `CachedTrace` holds
`fragment + compiled + native + execution_count`):

- `lookup(function_id, loop_pc)` → `const CachedTrace*` (line 60–64):
  `unordered_map::find`, returns `nullptr` on miss. **PASS.**
- `store(function_id, loop_pc, frag, CompiledTrace&&, NativeTrace&&)`
  (line 67–74): moves into a `unique_ptr<CachedTrace>` and inserts.
  **PASS.**
- `has(function_id, loop_pc)` → `bool` (line 77–79): `find != end`.
  **PASS.**
- `size()` → `std::size_t` (line 82). **PASS.**
- `call_native(function_id, loop_pc, r0, r1, r2, r3)` (line 86–93):
  finds entry, increments `it->second->execution_count`, returns
  `it->second->native.entry(r0, r1, r2, r3)`. Returns `0` on miss.
  **PASS.**
- `CachedTrace` struct (line 47–52) holds `TraceFragment fragment`,
  `CompiledTrace compiled`, `NativeTrace native`,
  `std::int64_t execution_count{0}`. **PASS.**

Non-blocking spec-gap (informational only): §4.6 lists 12 fields per trace
cache entry (trace ID, kind, entry point, entry state signature,
dependencies, guard metadata, side exits, bridges, FrameState tables,
GC maps, telemetry, compilation version). The implementation only holds
fragment + compiled + native + execution_count — a minimal subset
consistent with the producer's documented "minimal JIT" scope (see
REVIEW-017's prior acceptance of the same scope for native codegen).
Not a `FAIL` because the producer has consistently scoped the trace cache
as a minimal cache by (function_id, loop_pc) → compiled artifact, and
neither the commit under review nor any prior commit added bridges /
FrameState tables / GC maps. The minimal subset is the *whole* current
trace-cache scope; broader spec compliance is deferred to a future
"trace-tree + side-exit" commit per the producer's stated roadmap.

Non-blocking citation concern: `trace_cache.hpp:3` cites
`DVM-Hybrid-Tracing-Architecture.md §12.4 (trace cache)` — but §12.4 of
the live document is "Recording calls", not "trace cache". The trace
cache is specified in §4.6 of the same document. The producer should fix
the citation to `§4.6 (trace cache)` for §3.5 (CI parses commit-message
spec citations) compliance. Same wrong citation appears in the commit
message body.

---

### Check 2 — Lifter fix: `MOV_CONST` resolves actual constant (PASS)

**Spec:** `DVM-Hybrid-Tracing-Architecture.md §12.2` (recording
operations) — MOV_CONST is a CRB §10 move/constant opcode;
`opcodes_move.cpp:op_mov_const` defines the interpreter's reference
behaviour: `dst = s1`, `const_idx = imm32(s2, s3)`, value loaded from
`module->constants[const_idx]` by kind (I8/I16/I32/I64/U8/U16/U32/U64 →
`int64_t(payload_lo)`; F32/F64 → `memcpy`+`Value{double}`).

**Code under review:** `runtime/interp/src/lifter.cpp:55,98–121`.

Verdict: **PASS**.

- `lift_trace()` signature now `dgw::Graph* lift_trace(const TraceFragment&
  frag, const crb::Module* module)` (`lifter.hpp:43`), `module` defaults
  to `nullptr`. The 2-arg call sites from REVIEW-014 still compile (Test 7
  at `smoke.cpp:676` passes `&lr6.module`).
- `MOV_CONST` handler (`lifter.cpp:98–121`) computes
  `const_idx = InstrCell::imm32(cell.s2(), cell.s3())`, bounds-checks
  against `module->constants.size()`, reads `ce.kind`, and:
  - `I64/I32/I16/I8/U64/U32/U16/U8/Bool` →
    `const_val = static_cast<int64_t>(ce.payload_lo)` (matches
    `opcodes_move.cpp:30` for I* / U* kinds, and `opcodes_move.cpp:21`
    for `Bool`).
  - `F64/F32` → `memcpy` to a `double`, then
    `const_val = static_cast<int64_t>(d)` (this differs from the
    interpreter's `Value{d}` which keeps the double as a Float64-typed
    Value, but the lifter's IR CONST payload is `std::variant<int64_t,
    double, SymbolId>` and `w.create_const(int64_t)` is selected here;
    the truncation to `int64_t` matches the lifter's `MOV_CONST` IR
    intent — Test 10's loop has only `i64` constants, so the float path
    is not exercised).
  - Falls through to `const_val = 0` if `module == nullptr` or
    `const_idx >= constants.size()` or kind unrecognised — the
    interpreter's `op_mov_const` instead returns `OpResult::Trap` on
    missing module / OOB index. This is a behavioural divergence between
    interpreter (traps) and lifter (substitutes 0); it is **non-blocking**
    here because the trace lifter is only invoked for traces that the
    interpreter successfully executed, so the constant pool index is
    guaranteed in-range and the module is guaranteed non-null. The Test
    10 call site (`smoke.cpp:909`) passes `&lr10.module` explicitly.
- Calls `w.create_const(const_val)` and writes `regmap[cell.s1()] = n`.
  Mirrors `opcodes_move.cpp:44` (`s.reg(dst) = v; s.advance();`).

Coverage gap (non-blocking): the Test 10 recorded trace starts at PC=3
(ADD) — the recording begins after the 3 MOV_CONST opcodes at PC=0..2
have already executed, so the recorded `frag.instructions` is
{ADD, CMP_LT_S, BR_TRUE} (smoke output: "Recorded: 3 instructions,
exit=5, loop=yes"). No MOV_CONST opcode is ever recorded, so the
lifter's MOV_CONST branch (`lifter.cpp:98–121`) is **not exercised by
any test in the smoke suite**. The branch is verified by code reading
only. The producer should add a test that records a trace containing a
MOV_CONST (e.g. a trace starting at PC=0 with a hot inner loop) to
cover this code path. Not a `FAIL` — the code is correct.

---

### Check 3 — Native codegen fix: entry-CONSTs skip `mov imm64` (PASS, with caveats)

**Spec:** `DGW-Core-IR.md Part 7.3` (Instruction Selection). The fix is
implementation-level: the previous commit (REVIEW-017) emitted `mov
imm64` for *every* CONST node, including the entry-register CONSTs
created by the lifter from `frag.entry_registers`. With the lifter fix
in this commit, those CONSTs now hold the *snapshot* values (e.g.
`r0=3`, `r1=1`, `r2=10`, `r3=1` for Test 9/10's loop), and emitting
`mov rdi, 3; mov rsi, 1; mov rdx, 10; mov rcx, 1` would *overwrite* the
caller's actual function-argument values. The fix is to skip `mov
imm64` for CONSTs allocated to argument registers (RDI/RSI/RDX/RCX).

**Code under review:** `runtime/interp/src/native_codegen.cpp:98–122`.

Verdict: **PASS** (with two non-blocking concerns).

- For every non-DEAD `CONST` node, `Reg r = ra.alloc(n)` is called first
  (line 106). `RegAllocator::alloc` returns arg_regs in order
  (`RDI, RSI, RDX, RCX`) before extra_regs (`R8, R9, R10, R11`) and
  finally `RAX`. So the *first four* CONST nodes the walker visits get
  the four arg registers; later CONSTs get extra_regs.
- The `if (r != Reg::RDI && r != Reg::RSI && r != Reg::RDX && r != Reg::RCX)`
  guard at line 112 skips the `mov imm64` for any CONST that landed on an
  argument register. **PASS** on the "skip" half of the check.
- For non-entry CONSTs, the code reads `arena.consts[pidx].value`
  (lines 114–120) and emits `encode_mov_imm64(buf, r, std::get<int64_t>(cp.value))`
  if the variant holds `int64_t`. **PASS** on the "non-entry constants emit
  mov imm64 with actual value" half of the check (verified by code
  reading — see coverage-gap note below).
- Test 9 smoke output: "Native code: 40 bytes" (down from REVIEW-017's
  70 bytes). The 30-byte savings are exactly the 3 × 10-byte
  `movabs rdi,3 / movabs rsi,1 / movabs rdx,10` that the old code emitted
  for the entry-CONSTs. Hex dump from smoke output:
  ```
  40 55 48 89 E5 48 01 F7 48 B9 00 00 00 00 00 00
  00 00 48 39 D7 40 0F 9C C1 48 85 C9 0F 85 E3 FF
  FF FF 48 89 F8 40 5D C3
  ```
  Disassembly (Intel syntax):
  ```
  push rbp
  mov rbp, rsp
  add rdi, rsi        ; loop_start (offset 5)
  movabs rcx, 0      ; zero-extend for setCC (CMP_LT result), NOT a non-entry CONST
  cmp rdi, rdx
  setl cl
  test rcx, rcx
  jne -29            ; backedge to loop_start at offset 5
  mov rax, rdi
  pop rbp
  ret
  ```
  Hand-trace: rdi=3, rsi=1, rdx=10 (caller's args, *not* overwritten).
  Loop: rdi=3 → 4 → 5 → 6 → 7 → 8 → 9 → 10, cmp(10,10)=0, jne not
  taken, fall through to `mov rax, rdi; ret` → returns 10. **PASS.**

Concern (1) — non-blocking: the *only* `movabs` in the Test 9/10 native
code is `movabs rcx, 0` from the CMP_LT case (`native_codegen.cpp:161`,
zero-extend before `setCC`). The "non-entry constants emit mov imm64
with actual value" path (`native_codegen.cpp:113–120`) is **not
exercised** by any test, because the Test 10 trace contains no MOV_CONST
opcode and the lifted graph contains no non-entry CONSTs. The code is
correct by inspection; coverage gap.

Concern (2) — non-blocking regression: REVIEW-017's `native_codegen.cpp`
handled both `int64_t` and `double` CONST payloads:
```cpp
if (std::holds_alternative<std::int64_t>(cp.value)) {
  encode_mov_imm64(buf, r, std::get<std::int64_t>(cp.value));
} else if (std::holds_alternative<double>(cp.value)) {
  double d = std::get<double>(cp.value);
  std::int64_t raw;
  std::memcpy(&raw, &d, sizeof(raw));
  encode_mov_imm64(buf, r, raw);
}
```
The new code drops the `double` branch:
```cpp
if (std::holds_alternative<std::int64_t>(cp.value)) {
  encode_mov_imm64(buf, r, std::get<std::int64_t>(cp.value));
}
```
This is unreachable in practice (the lifter's MOV_CONST converts
F32/F64 to `int64_t` via `static_cast<int64_t>(d)`, and the
entry-CONSTs from `frag.entry_registers` use the `Int64/Float64/Null/Bool`
switch at `lifter.cpp:72–88` which creates `int64_t` CONSTs for all
branches except `Float64` — which calls `w.create_const(v.as_f64())`,
creating a `double`-typed CONST). So the dropped `double` branch is a
real regression on paper but unreachable in the current test corpus.
Not a `FAIL` — but the producer should either re-add the `double`
branch (defensive) or document that the lifter guarantees int64-only
CONSTs (which it currently does not — the entry-CONST for a `Float64`
entry register produces a `double`-typed CONST).

---

### Check 4 — `#if !defined(__SANITIZE_ADDRESS__)` guard (PASS)

**Spec:** producer's own scope statement in the commit message; no
direct spec rule, but `Mandatory-Agent-Review-Rule.md §3.3` requires a
verifier run, and the test suite is the runtime/interp's verifier
substitute.

**Code under review:** `runtime/interp/tests/smoke.cpp:764,826–828,836,958–960`.

Verdict: **PASS**.

- Test 9 block opens at `smoke.cpp:764`:
  `#if !defined(__SANITIZE_ADDRESS__)`
- Test 9 `#else` branch at `smoke.cpp:826–827`:
  ```cpp
  #else
          std::println("\n-- Test 9: SKIPPED (JIT + ASan incompatible) --");
  #endif
  ```
  Skipped message present. ✓
- Test 10 block opens at `smoke.cpp:836`:
  `#if !defined(__SANITIZE_ADDRESS__)`
- Test 10 `#else` branch at `smoke.cpp:958–959`:
  ```cpp
  #else
    std::println("\n-- Test 10: SKIPPED (JIT + ASan incompatible) --");
  #endif
  ```
  Skipped message present. ✓

Runtime verification:
- SAN=1 smoke output contains exactly the lines:
  - `-- Test 9: SKIPPED (JIT + ASan incompatible) --`
  - `-- Test 10: SKIPPED (JIT + ASan incompatible) --`
  - (no `Test 9:` or `Test 10:` PASS line — Tests 9+10 are correctly
    excluded from the PASS output)
- No-SAN smoke output contains:
  - `Test 9: native x86-64 code executed, result = 10 (PASS)`
  - `Test 10: trace cache: 3 runs (1 interp + 2 native), all results = 10, execution_count = 2 (PASS)`
  - (no `SKIPPED` line)
- Both builds finish with `== DVM Interpreter smoke test PASSED ==`.

The guard works in both directions.

---

### Check 5 — Test 10 correctness (PASS)

**Spec:** producer's Test 10 contract from the commit message (3 runs,
1 interp + 2 native, all return 10, execution_count=2).

**Code under review:** `runtime/interp/tests/smoke.cpp:834–957`.

Verdict: **PASS**.

Smoke output (no-SAN, from `/tmp/smoke_nosan.log`):
```
-- Test 10: trace caching + native dispatch --
  Run 1: recording + compiling trace...
  Run 1: interpreter result = 10
  Recorded: 3 instructions, exit=5, loop=yes
  Compiled: 10→10 nodes, 1 blocks, verifier=ok
  Native: 40 bytes
  Cached: 1 traces in cache
  Run 2: cache hit → calling native code...
  Run 2: native result = 10
  Run 3: cache hit again...
  Run 3: native result = 10
Test 10: trace cache: 3 runs (1 interp + 2 native), all results = 10, execution_count = 2 (PASS)
```

Verified against the check's three sub-conditions:
- Run 1 (interpreter): `interpret(lr10.module, 0, &recorder10, &hotness10)`
  returns `Value` with `as_i64() == 10`. Asserted at `smoke.cpp:897`.
  Output: `Run 1: interpreter result = 10`. ✓
- Run 2 (cache hit → native):
  `cache.call_native(0, 3, 3, 1, 10, 1)` returns `10`. Asserted at
  `smoke.cpp:932`. Output: `Run 2: native result = 10`. ✓
- Run 3 (cache hit again → native + execution_count):
  `cache.call_native(0, 3, 3, 1, 10, 1)` returns `10`. Asserted at
  `smoke.cpp:942`. Then `cache.lookup(0, 3)->execution_count == 2`
  asserted at `smoke.cpp:949`. Output: `Run 3: native result = 10` +
  `execution_count = 2 (PASS)`. ✓

`call_native` increments `execution_count` *before* invoking
`native.entry` (`trace_cache.hpp:91–92`), so after 2 calls
`execution_count == 2`. **PASS.**

---

### Check 6 — Build clean without SAN (PASS)

Verdict: **PASS**.

```
$ cd /home/z/my-project/dgw-core-repo/compiler/dgw-core && make clean && make -j$(nproc)
EXIT=0
$ grep -cE "warning:|error:" /tmp/build_compiler_nosan.log
0
$ cd /home/z/my-project/dgw-core-repo/runtime/interp && make clean && make -j$(nproc)
EXIT=0
$ grep -cE "warning:|error:" /tmp/build_runtime_nosan.log
0
$ timeout 10 ./bin/dvm_interp_smoke
EXIT=0
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
== DVM Interpreter smoke test PASSED ==
```

Zero warnings, zero errors, all 10 tests pass, no SKIPPED messages,
final PASSED line printed.

---

### Check 7 — Build clean with SAN (PASS)

Verdict: **PASS**.

```
$ cd /home/z/my-project/dgw-core-repo/compiler/dgw-core && make clean && make SAN=1 -j$(nproc)
EXIT=0
$ grep -cE "warning:|error:" /tmp/build_compiler_san.log
0
$ cd /home/z/my-project/dgw-core-repo/runtime/interp && make clean && make SAN=1 -j$(nproc)
EXIT=0
$ grep -cE "warning:|error:" /tmp/build_runtime_san.log
0
$ timeout 10 ./bin/dvm_interp_smoke
EXIT=0
Test 1: 20 + 22 = 42 (PASS)
Test 2: fn1 calls fn0(40 + 2) = 42 (PASS)
Test 3: count to 10 = 10 (PASS)
Test 4: ALLOC + OBJ_SET(42) + OBJ_GET = 42 (PASS)
Test 5: trace of 6 instructions, exit=BranchTaken (PASS)
Test 6: hot-loop detected at PC=3, trace of 3 instrs, LoopClose (PASS)
Test 7: lifted graph has 10 nodes, 8 edges, ADD+CMP present (PASS)
Test 8: compiled trace: 10→10 nodes, 1 blocks, 3 ops, verifier ok (PASS)
-- Test 9: SKIPPED (JIT + ASan incompatible) --
-- Test 10: SKIPPED (JIT + ASan incompatible) --
== DVM Interpreter smoke test PASSED ==
$ ASAN_OPTIONS=detect_leaks=1:exitcode=42 ./bin/dvm_interp_smoke
EXIT=0   (not 42 — zero leaks)
```

Zero warnings, zero errors, Tests 1–8 PASS, Tests 9+10 SKIPPED (with
explicit print messages), final PASSED line printed. Explicit
LeakSanitizer run: exit code 0 (not 42) — zero leaks.

---

### Check 8 — No warning suppressions (FAIL)

**Spec:** the project's own `runtime/interp/Makefile:10–13` policy
(quoted verbatim):
> All warnings are errors. No lazy unused-variable suppressions; if a
> variable is set but not used, either use it or delete it. Being lazy
> about warnings hides bugs.

And the task-description's explicit check text:
> grep for pragma/attribute/(void) in runtime/interp/ returns nothing
> (excluding Makefile -Wno-pedantic).

**Verdict: FAIL.**

Grep results (`grep -nE "pragma|__attribute__|\(void\)" -r
runtime/interp/`, after filtering `#pragma once` which are benign
header guards, not warning suppressions):

```
runtime/interp/tests/smoke.cpp:858:    FunctionEntry fns10[1] = { make_fn(0, 0, static_cast<std::uint32_t>(code_sz10), 4) }; (void)fns10;
runtime/interp/tests/smoke.cpp:859:    std::size_t ft_sz10 = sizeof(fns10); (void)ft_sz10;
runtime/interp/tests/smoke.cpp:863:    std::size_t ft_offset10 = cp_offset10 + cp_sz10; (void)ft_offset10;
runtime/interp/src/lifter.cpp:270:      (void)regmap.read(cell.s2()); // obj (unused in minimal lifter)
runtime/interp/src/lifter.cpp:281:      (void)regmap.read(cell.s1()); // obj (unused in minimal lifter)
```

Filtering further to *only the lines added by commit deeb19d* (verified
via `git blame -L 858,863 -- runtime/interp/tests/smoke.cpp` — all 3
lines blame to `deeb19db 2026-09-08`):

```
smoke.cpp:858:  ... ; (void)fns10;
smoke.cpp:859:  ... ; (void)ft_sz10;
smoke.cpp:863:  ... ; (void)ft_offset10;
```

Three new `(void)` casts are added by this commit — exactly the
"lazy unused-variable suppressions" the Makefile policy forbids. The
grep does **not** return nothing.

**Root cause — a copy-paste bug in the Test 10 module setup that the
`(void)` casts hide:**

The Test 10 block (`smoke.cpp:843–957`) declares a fresh
`ModuleBuilder b10;` at line 845 and local arrays
`code10 / consts10 / fns10` plus offsets
`code_offset10 / cp_offset10 / ft_offset10` plus sizes
`code_sz10 / cp_sz10 / ft_sz10`. The intent is to build a fresh module
into `b10` and load it. The first half of the section table is emitted
to `b10` (`smoke.cpp:865–870`):

```cpp
for (std::size_t i = 0; i < 88; ++i) b10.emit_u8(0);
b10.emit_u32(static_cast<std::uint32_t>(SectionType::Code));
b10.emit_u32(static_cast<std::uint32_t>(code_offset10));
b10.emit_u32(static_cast<std::uint32_t>(code_sz10));
b10.emit_u32(0);
b10.emit_u32(static_cast<std::uint32_t>(SectionType::ConstantPool));
```

… but the second half of the section table and the body bytes are
emitted to `b`, **not `b10`** (`smoke.cpp:871–881`):

```cpp
b.emit_u32(static_cast<std::uint32_t>(cp_offset));    // BUG: cp_offset, not cp_offset10
b.emit_u32(static_cast<std::uint32_t>(cp_sz));         // BUG: cp_sz, not cp_sz10
b.emit_u32(0);
b.emit_u32(static_cast<std::uint32_t>(SectionType::FunctionTable));
b.emit_u32(static_cast<std::uint32_t>(ft_offset));    // BUG: ft_offset, not ft_offset10
b.emit_u32(static_cast<std::uint32_t>(ft_sz));         // BUG: ft_sz, not ft_sz10
b.emit_u32(0);
b.emit_bytes(code, code_sz);                           // BUG: code, not code10
b.emit_bytes(consts, cp_sz);                           // BUG: consts, not consts10
b.emit_bytes(fns, ft_sz);                              // BUG: fns, not fns10
b.write_header(3, 88);
auto raw10 = b.raw;                                    // BUG: b.raw, not b10.raw
```

`b` is `ModuleBuilder b;` declared at `smoke.cpp:571` inside the Test 6
block (`smoke.cpp:569 {`). The Test 6 block does **not** close until
`smoke.cpp:964` — it encloses Tests 7, 8, 9 (all nested), and Test 10
is also nested inside it (Test 10 opens at line 843, closes at line
957). So `b` is the Test 6 builder, still in scope, with Test 6's
fully-built module already in `b.raw`. Test 10's `b.emit_*` calls
**append** to Test 6's existing module bytes; then `b.write_header`
overwrites bytes 0–87 with the same header (no-op since Test 6 already
wrote that header); then `auto raw10 = b.raw` takes Test 6's module
plus appended junk. `load_module(raw10)` then parses using the header's
`section_table_offset=88` and `section_count=3` — which point at Test
6's original section table entries (at offset 88), which point at Test
6's original code/consts/functions (at offsets 136/192/264 — unchanged
by the appended junk). So `lr10.module` is byte-identical to Test 6's
`lr6.module` (a counting-loop module). The Test 10 trace thus records
the same `ADD / CMP_LT_S / BR_TRUE` sequence as Test 6, lifts to the
same graph, compiles to the same 40-byte native code, and produces the
same `result = 10` on every run.

The Test 10 variables `fns10`, `ft_sz10`, `ft_offset10` are computed
but **never read** — because the corresponding `b.emit_u32(ft_offset10)`
/ `b.emit_u32(ft_sz10)` / `b.emit_bytes(fns10, ft_sz10)` lines were
mistakenly written as `b.emit_u32(ft_offset)` / `b.emit_u32(ft_sz)` /
`b.emit_bytes(fns, ft_sz)` (referencing the Test 6 scope names). With
`-Werror -Wall -Wextra`, these three variables trigger
`-Wunused-variable` — and the producer silenced each one with a
`(void)` cast at the end of its declaration line, instead of either
fixing the copy-paste bug (s/b/b10/ on lines 871–883) or deleting the
unused variables.

This is exactly the "lazy unused-variable suppressions … hide bugs"
failure mode the Makefile warns about: the `(void)` casts hide a real
copy-paste bug that, if fixed, would make `b10` the actual builder used
for Test 10's module (giving a clean separation from Test 6). The test
"passes" only because Test 6's leftover module happens to be the same
counting loop.

The two pre-existing `(void)` casts in `lifter.cpp:270,281` were
introduced by commit `0cd4d73` (the original lifter commit, REVIEW-014)
— they predate this commit. They are the same kind of suppression
(cast-to-void of `regmap.read(cell.s2())` to silence
`-Wunused-result`/`-Wunused-value` on the `obj` operand that the
minimal lifter doesn't use). Strictly the check text says "returns
nothing" — so even pre-existing hits would fail — but the new
3 `(void)` casts added by this commit are the clear, in-scope failure.

---

## 2. Verifier Run (Section 3.3 of the Mandatory-Agent-Review-Rule)

The `WebVerifier` (DGW-Core-IR.md Part 8) is exercised by Test 7 (lifted
graph) and Test 8 (compiled/optimised graph). Both runs are green.

From `/tmp/smoke_nosan.log` (no-SAN run, all 10 tests):
```
Test 7: lifted graph has 10 nodes, 8 edges, ADD+CMP present (PASS)
  verifier: ok=true pass=11 fail=0     (lifted graph)

Test 8: compiled trace: 10→10 nodes, 1 blocks, 3 ops, verifier ok (PASS)
  Verifier: ok=true pass=11 fail=0     (optimised graph)
```

From `/tmp/smoke_san.log` (SAN=1 run, Tests 1–8 only):
```
Test 7: lifted graph has 10 nodes, 8 edges, ADD+CMP present (PASS)
  verifier: ok=true pass=11 fail=0

Test 8: compiled trace: 10→10 nodes, 1 blocks, 3 ops, verifier ok (PASS)
  Verifier: ok=true pass=11 fail=0
```

The WebVerifier is green on both the lifted and the optimised graphs,
under both build configurations. §3.3 satisfied.

---

## 3. Final Verdict (Section 3.4 of the Mandatory-Agent-Review-Rule)

| Check | Verdict |
|-------|---------|
| 1. `TraceCache` API surface | **PASS** |
| 2. Lifter fix: `MOV_CONST` resolves actual constant | **PASS** |
| 3. Native codegen fix: entry-CONSTs skip `mov imm64` | **PASS** (with non-blocking concerns) |
| 4. `#if !defined(__SANITIZE_ADDRESS__)` guard | **PASS** |
| 5. Test 10 correctness | **PASS** |
| 6. Build clean without SAN | **PASS** |
| 7. Build clean with SAN | **PASS** |
| 8. No warning suppressions | **FAIL** |

**Final status: CHANGES_REQUESTED.**

One rule `FAIL` (Check 8 — three new `(void)` casts in
`runtime/interp/tests/smoke.cpp:858,859,863` that hide a copy-paste bug
in the Test 10 module setup). Per `Mandatory-Agent-Review-Rule.md §3.4`,
a `CHANGES_REQUESTED` verdict requires the producing agent to address
every `FAIL` with a new commit and re-request review. The producing
agent may **not** re-request review from `review-agent-018` until a new
commit has been pushed (ping-pong without new commits is forbidden).

**Required fix (minimum):**

Either of:

1. **Fix the copy-paste bug** (preferred): change `smoke.cpp:871–883`
   to use `b10` and the `*10` variables consistently:
   ```cpp
   b10.emit_u32(static_cast<std::uint32_t>(cp_offset10));
   b10.emit_u32(static_cast<std::uint32_t>(cp_sz10));
   b10.emit_u32(0);
   b10.emit_u32(static_cast<std::uint32_t>(SectionType::FunctionTable));
   b10.emit_u32(static_cast<std::uint32_t>(ft_offset10));
   b10.emit_u32(static_cast<std::uint32_t>(ft_sz10));
   b10.emit_u32(0);
   b10.emit_bytes(code10, code_sz10);
   b10.emit_bytes(consts10, cp_sz10);
   b10.emit_bytes(fns10, ft_sz10);
   b10.write_header(3, 88);
   auto raw10 = b10.raw;
   ```
   Then delete the three `(void)` casts on lines 858, 859, 863 — they
   become unnecessary because `fns10`/`ft_sz10`/`ft_offset10` are now
   used.

2. **Delete the unused Test 10-local variables**: replace the Test 10
   module setup with a single comment explaining that Test 10 reuses
   Test 6's module (since they're both the same counting loop), and
   use `lr6.module` (or rebuild via a fresh, properly-named builder).
   This is a smaller diff but loses the "build a fresh module for
   Test 10" intent.

The producer must also address (non-blocking, but recommended):

3. **Fix the spec citation**: `trace_cache.hpp:3` and the commit
   message both cite `§12.4 (trace cache)`. The actual trace-cache
   section is `§4.6`. Change to `§4.6 (trace cache), §12.4 (recording
   calls), §13 (execution)`.

4. **Add a test that exercises the lifter's MOV_CONST branch and the
   native codegen's non-entry-CONST mov imm64 path**: e.g. a trace
   that starts at PC=0 with a hot inner loop, so that the recorded
   `frag.instructions` includes a `MOV_CONST` opcode. The current
   Test 10 trace starts at PC=3 (ADD) and contains no `MOV_CONST`,
   so neither code path is covered.

5. **Re-add the `double` CONST branch in `native_codegen.cpp:113–120`**
   (defensive — currently unreachable because the lifter converts
   F32/F64 constants to `int64_t` via `static_cast`), *or* document
   in `lifter.hpp` that the lifter guarantees int64-only CONST
   payloads (currently it does not — the `Float64` entry-register
   branch at `lifter.cpp:77` creates a `double`-typed CONST).

---

## 4. Non-Blocking Concerns (informational only, do not affect verdict)

| # | Concern | Severity | Recommendation |
|---|---------|----------|----------------|
| N1 | Trace-cache spec citation: `§12.4` is "Recording calls", not "trace cache". Trace cache is specified in `§4.6`. | Non-blocking | Fix citation in `trace_cache.hpp:3` and in the commit message. |
| N2 | `CachedTrace` is a minimal subset of §4.6's 12 required fields (no trace ID, no kind, no entry state signature, no dependencies, no guard metadata, no side exits, no bridges, no FrameState tables, no GC maps, no telemetry, no compilation version). | Non-blocking | Consistent with producer's "minimal JIT" scope accepted in REVIEW-017. Broader spec compliance deferred to a future "trace-tree + side-exit" commit. |
| N3 | Lifter's MOV_CONST branch is not exercised by any test — Test 10's recorded trace starts at PC=3 (ADD) and contains no MOV_CONST opcodes. | Non-blocking | Add a test that records a trace containing a MOV_CONST (e.g. a hot inner loop starting at PC=0). |
| N4 | Native codegen's non-entry-CONST `mov imm64` path is not exercised by any test — Test 10's lifted graph has only the 4 entry-CONSTs (from `frag.entry_registers`); the 4th is DCE'd as DEAD, so the path is unreachable in the current test corpus. | Non-blocking | Add a test with a non-entry CONST (e.g. a trace whose body contains a `MOV_CONST` opcode, exercising both the lifter fix and the codegen path). |
| N5 | Native codegen dropped the `double` CONST branch (was in REVIEW-017). Currently unreachable because the lifter converts F32/F64 to `int64_t`, but the entry-CONST for a `Float64` entry register (`lifter.cpp:77`) creates a `double`-typed CONST, which the new codegen silently drops. | Non-blocking | Re-add the `double` branch (defensive) or document that the lifter guarantees int64-only CONSTs. |
| N6 | Test 10's CMP_LT result reuses the 4th arg register `RCX` because the 4th entry-CONST (for `r3`) was DCE'd as DEAD — so the caller's `r3` argument is overwritten by `movabs rcx, 0` (zero-extend before `setCC`). The trace's semantics ignore `r3` from the snapshot (the new CMP_LT result is read by BR_TRUE), so this is correct, but it's a subtle interaction between DCE and register allocation that should be documented. | Non-blocking | Add a comment in `native_codegen.cpp` explaining the DCE/register-alloc interaction. |
| N7 | Pre-existing `(void)` casts in `lifter.cpp:270,281` (introduced by commit `0cd4d73`, REVIEW-014) are the same kind of warning suppression as the new ones in this commit. They were not flagged in REVIEW-014/REVIEW-017 (grandfathered in). | Non-blocking | Should be cleaned up in a separate "lifter cleanup" commit per the Makefile policy. |

---

## 5. Reviewer Sign-off

- **Reviewer agent ID:** `review-agent-018`
- **UTC timestamp:** `2026-09-08T19:49:21Z`
- **Final status:** `CHANGES_REQUESTED`
- **Counts:** 7 PASS, 1 FAIL, 0 N/A
- **Verifier:** WebVerifier green on both lifted (Test 7) and optimised
  (Test 8) graphs under both no-SAN and SAN=1 builds.
- **Build:** Both `make` (no-SAN) and `make SAN=1` exit 0 with zero
  warnings and zero errors; no-SAN runs all 10 tests PASS, SAN=1 runs
  Tests 1–8 PASS + Tests 9+10 SKIPPED; LSan confirms zero leaks.
- **Blocking issue:** Three new `(void)` casts in
  `runtime/interp/tests/smoke.cpp:858,859,863` that hide a copy-paste
  bug in the Test 10 module setup (lines 871–883 mistakenly emit to
  `b` — the Test 6 leftover builder — instead of `b10`).
