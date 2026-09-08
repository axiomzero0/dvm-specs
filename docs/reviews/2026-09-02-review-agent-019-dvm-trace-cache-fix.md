# REVIEW-019 — DVM Trace Cache Fix Verification (REVIEW-018 follow-up)

- **Reviewer agent ID:** `review-agent-019`
- **UTC timestamp:** `2026-09-08T20:05:00Z`
- **Producer commit under review:** `614893e7541731197d7366de127b2497b47735cc`
  (`runtime/interp: fix REVIEW-018 (remove (void) casts + fix Test 10 copy-paste)`)
- **Previous review (REVIEW-018):** `deeb19d` returned `CHANGES_REQUESTED`
  with 7 PASS + 1 FAIL. The single FAIL was Check 8 — three new `(void)`
  casts in `tests/smoke.cpp:858,859,863` that hid a copy-paste bug in the
  Test 10 module setup (lines 871–883 emitted to Test 6's leftover builder
  `b` instead of `b10`).
- **Files touched (per `git show --stat 614893e`):**
  - `runtime/interp/tests/smoke.cpp` (34 lines, +17/-17)
  - `runtime/interp/src/lifter.cpp` (4 lines, +2/-2 — removed two pre-existing `(void)` casts at lines 270 and 281)
  - `docs/reviews/2026-09-02-review-agent-018-dvm-trace-cache.md` (REVIEW-018 report added)
- **Spec corpus cited by producer:** `Mandatory-Agent-Review-Rule.md §3.4`
  (re-review after fix).
- **Spec corpus reviewed by reviewer:** `Mandatory-Agent-Review-Rule.md §3`
  in full; the four mandatory checks specified in REVIEW-019 task.

---

## 0. Fix Summary (from `git show 614893e`)

The fix commit applies the preferred remedy from REVIEW-018 §3 (option 1):
rename all Test 10 variables to `*10` suffixes, properly declare
`fns10`/`ft_sz10`/`ft_offset10`, switch all `b.emit_*` → `b10.emit_*`
calls in the Test 10 module setup, and remove the three now-unnecessary
`(void)` casts at lines 858, 859, 863.

Additionally, the commit removes two pre-existing `(void)` casts in
`runtime/interp/src/lifter.cpp:270,281` (OBJ_GET / OBJ_SET handlers
grandfathered in by REVIEW-014/REVIEW-017). The
`(void)regmap.read(cell.s2())` and `(void)regmap.read(cell.s1())`
calls (which silenced "unused result" warnings for object-register reads
that the minimal lifter does not model) were deleted outright; the
underlying comment ("Minimal: OBJ_GET becomes a LOAD…") is preserved.

Two stray comment text changes in Test 10 (`call native code` →
`call native code10` at lines 927–928) are harmless cosmetic
side-effects of the find-and-replace of `b` → `b10`. The string
`"native code10"` reads slightly awkwardly, but it is a comment-only
change and does not affect runtime behaviour.

---

## 1. The Four Mandatory Checks

### Check 1 — No `(void)` casts in `tests/smoke.cpp`

**Verdict: PASS.**

Command: `rg '\(void\)' runtime/interp/tests/smoke.cpp`

Result: 0 matches (excluding comments — there are no `(void)` substrings
at all in the file, comment or otherwise).

For completeness, also verified in `lifter.cpp`:

Command: `rg '\(void\)' runtime/interp/src/lifter.cpp`

Result: 0 matches. The two pre-existing `(void)` casts at lines 270
(`OBJ_GET` handler) and 281 (`OBJ_SET` handler) are gone. The remaining
`void` tokens in `lifter.cpp` are the `void write(...)` member function
signature at line 49 and the `void print_lifted_graph(...)` free function
signature at line 299 — these are valid C++ `void`-typed declarations,
not warning-suppression casts.

### Check 2 — Test 10 uses `b10` (not `b`)

**Verdict: PASS.**

Inspected the Test 10 block in `runtime/interp/tests/smoke.cpp`
(lines 834–960, with the executable block spanning lines 843–957).

Verified all `b10.emit_*` / `b10.write_header` / `b10.raw` calls use
the `b10` builder:

| Line | Code |
|------|------|
| 865 | `b10.emit_u8(0)` (×88 in a `for` loop) |
| 866 | `b10.emit_u32(static_cast<std::uint32_t>(SectionType::Code))` |
| 867 | `b10.emit_u32(static_cast<std::uint32_t>(code_offset10))` |
| 868 | `b10.emit_u32(static_cast<std::uint32_t>(code_sz10))` |
| 869 | `b10.emit_u32(0)` |
| 870 | `b10.emit_u32(static_cast<std::uint32_t>(SectionType::ConstantPool))` |
| 871 | `b10.emit_u32(static_cast<std::uint32_t>(cp_offset10))` |
| 872 | `b10.emit_u32(static_cast<std::uint32_t>(cp_sz10))` |
| 873 | `b10.emit_u32(0)` |
| 874 | `b10.emit_u32(static_cast<std::uint32_t>(SectionType::FunctionTable))` |
| 875 | `b10.emit_u32(static_cast<std::uint32_t>(ft_offset10))` |
| 876 | `b10.emit_u32(static_cast<std::uint32_t>(ft_sz10))` |
| 877 | `b10.emit_u32(0)` |
| 878 | `b10.emit_bytes(code10, code_sz10)` |
| 879 | `b10.emit_bytes(consts10, cp_sz10)` |
| 880 | `b10.emit_bytes(fns10, ft_sz10)` |
| 881 | `b10.write_header(3, 88)` |
| 883 | `auto raw10 = b10.raw` |

A grep for `^.*\bb\.emit_` / `^.*\bb\.write_header` / `^.*\bb\.raw`
returned matches only in lines 176–615 (Tests 1–6 module-setup blocks),
**none** in the Test 10 range (lines 843–957). A targeted grep inside
the Test 10 range for `\bb\.` returned zero matches.

Also verified that Test 10 does not reference any non-suffixed variables
(`cp_offset`, `cp_sz`, `ft_offset`, `ft_sz`, `code_sz`, `code_offset`)
from Test 6's scope — all references are to the `*10` suffixed
declarations on lines 855–863. (Note: `b10` is properly declared at
line 845, before any use.)

### Check 3 — Build clean without SAN; all 10 tests pass

**Verdict: PASS.**

Commands run (from REVIEW-019 task spec):

```
cd /home/z/my-project/dgw-core-repo/compiler/dgw-core && make clean && make -j$(nproc)
cd /home/z/my-project/dgw-core-repo/runtime/interp && make clean && make -j$(nproc)
timeout 10 ./bin/dvm_interp_smoke
```

Build outputs (both `dgw-core` and `runtime/interp`):
- `make` exit code: 0
- Zero warnings, zero errors (compilation uses `-Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast
  -Wnoexcept -Wundef -Werror`; any warning would have failed the build).
- Log file `/tmp/interp_build_nosan_019.log` confirms no `error:` or
  `warning:` diagnostic lines (the only matches for the substring
  "warning" in the build log are the `-W...` compiler flags echoed in
  each `g++` invocation line).

Smoke test run (`/tmp/smoke_nosan_019.log`):

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
== DVM Interpreter smoke test PASSED ==
```

Exit code 0. All 10 tests PASS. Test 10's output confirms the cache
workflow: 1st run records and compiles the trace; 2nd and 3rd runs hit
the cache and call the native code directly; `execution_count = 2` is
verified.

### Check 4 — Build clean with SAN; 8 tests pass (9+10 SKIPPED)

**Verdict: PASS.**

Commands run:

```
cd /home/z/my-project/dgw-core-repo/compiler/dgw-core && make clean && make SAN=1 -j$(nproc)
cd /home/z/my-project/dgw-core-repo/runtime/interp && make clean && make SAN=1 -j$(nproc)
timeout 10 ./bin/dvm_interp_smoke
```

Build outputs:
- `dgw-core` `make SAN=1` exit code: 0
- `runtime/interp` `make SAN=1` exit code: 0
- Zero warnings, zero errors (same `-Werror` flag set, plus
  `-fsanitize=address,undefined -fno-omit-frame-pointer`).
- Logs: `/tmp/dgw_core_san_019.log` (0 `error:`/`warning:` diagnostic
  lines), `/tmp/interp_san_019.log` (0 diagnostic lines).

Smoke test run (`/tmp/smoke_san_019.log`):

```
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
```

Exit code 0. 8 tests PASS, Tests 9 and 10 SKIPPED (correctly guarded by
the `#if !defined(__SANITIZE_ADDRESS__)` preprocessor block at
`smoke.cpp:836` and `:836`). ASan and UBSan report no leaks or
undefined behaviour.

---

## 2. Verifier Log

### Build commands (with exit codes)

```
$ cd /home/z/my-project/dgw-core-repo/compiler/dgw-core && make clean && make -j$(nproc)
[full rebuild, 9 .o files + libdgwcore.a + bin/dgw_smoke]
DGW_CORE_BUILD_EXIT=0

$ cd /home/z/my-project/dgw-core-repo/runtime/interp && make clean && make -j$(nproc) 2>&1 | tail -3
[full rebuild, 15 .o files + libdvm_interp.a + bin/dvm_interp_smoke]
INTERP_BUILD_EXIT=0

$ timeout 10 ./bin/dvm_interp_smoke 2>&1 | tail -10
Test 10: trace cache: 3 runs (1 interp + 2 native), all results = 10, execution_count = 2 (PASS)
== DVM Interpreter smoke test PASSED ==
RUN_EXIT=0

$ cd /home/z/my-project/dgw-core-repo/compiler/dgw-core && make clean && make SAN=1 -j$(nproc)
[full rebuild with -fsanitize=address,undefined]
DGW_CORE_SAN_EXIT=0

$ cd /home/z/my-project/dgw-core-repo/runtime/interp && make clean && make SAN=1 -j$(nproc) 2>&1 | tail -3
[full rebuild with -fsanitize=address,undefined]
INTERP_SAN_EXIT=0

$ timeout 10 ./bin/dvm_interp_smoke 2>&1 | tail -5
-- Test 9: SKIPPED (JIT + ASan incompatible) --
-- Test 10: SKIPPED (JIT + ASan incompatible) --
== DVM Interpreter smoke test PASSED ==
RUN_EXIT=0
```

### Grep verifications

```
$ rg '\(void\)' runtime/interp/tests/smoke.cpp
(no matches)

$ rg '\(void\)' runtime/interp/src/lifter.cpp
(no matches — both pre-existing casts at lines 270,281 are removed)

$ awk 'NR>=843 && NR<=957' runtime/interp/tests/smoke.cpp | grep -nE '\bb\.'
(no matches — Test 10 emits exclusively through b10)
```

### Diff confirmation (`git show 614893e -- runtime/interp/`)

`runtime/interp/src/lifter.cpp`:

```
@@ -267,7 +267,7 @@ dgw::Graph* lift_trace(const TraceFragment& frag, const crb::Module* module) {
       regmap.write(cell.s1(), n);
     }
     else if (op_val == crb_op::OBJ_GET) {
-      (void)regmap.read(cell.s2()); // obj (unused in minimal lifter)
+
       // Minimal: OBJ_GET becomes a LOAD. A full lifter would create
       // a REF + LOAD from the object's region.
       NodeId ref = w.create_ref(dgw::RegionId{0}, cell.s3(),
@@ -278,7 +278,7 @@ dgw::Graph* lift_trace(const TraceFragment& frag, const crb::Module* module) {
       regmap.write(cell.s1(), load);
     }
     else if (op_val == crb_op::OBJ_SET) {
-      (void)regmap.read(cell.s1()); // obj (unused in minimal lifter)
+
       NodeId val = regmap.read(cell.s3());
       NodeId ref = w.create_ref(dgw::RegionId{0}, cell.s2(),
                                   dgw::AccessPerm::ReadWrite);
```

`runtime/interp/tests/smoke.cpp`:

```
@@ -855,12 +855,12 @@ int main() {
     std::size_t code_sz10 = sizeof(code10);
     ConstantEntry consts10[3] = { i64_const(0), i64_const(1), i64_const(10) };
     std::size_t cp_sz10 = sizeof(consts10);
-    FunctionEntry fns10[1] = { make_fn(0, 0, static_cast<std::uint32_t>(code_sz10), 4) }; (void)fns10;
-    std::size_t ft_sz10 = sizeof(fns10); (void)ft_sz10;
+    FunctionEntry fns10[1] = { make_fn(0, 0, static_cast<std::uint32_t>(code_sz10), 4) };
+    std::size_t ft_sz10 = sizeof(fns10);

     std::size_t code_offset10 = 136;
     std::size_t cp_offset10 = code_offset10 + code_sz10;
-    std::size_t ft_offset10 = cp_offset10 + cp_sz10; (void)ft_offset10;
+    std::size_t ft_offset10 = cp_offset10 + cp_sz10;

     for (std::size_t i = 0; i < 88; ++i) b10.emit_u8(0);
     b10.emit_u32(static_cast<std::uint32_t>(SectionType::Code));
@@ -868,19 +868,19 @@ int main() {
     b10.emit_u32(static_cast<std::uint32_t>(code_sz10));
     b10.emit_u32(0);
     b10.emit_u32(static_cast<std::uint32_t>(SectionType::ConstantPool));
-    b.emit_u32(static_cast<std::uint32_t>(cp_offset));
-    b.emit_u32(static_cast<std::uint32_t>(cp_sz));
-    b.emit_u32(0);
-    b.emit_u32(static_cast<std::uint32_t>(SectionType::FunctionTable));
-    b.emit_u32(static_cast<std::uint32_t>(ft_offset));
-    b.emit_u32(static_cast<std::uint32_t>(ft_sz));
-    b.emit_u32(0);
-    b.emit_bytes(code, code_sz);
-    b.emit_bytes(consts, cp_sz);
-    b.emit_bytes(fns, ft_sz);
-    b.write_header(3, 88);
+    b10.emit_u32(static_cast<std::uint32_t>(cp_offset10));
+    b10.emit_u32(static_cast<std::uint32_t>(cp_sz10));
+    b10.emit_u32(0);
+    b10.emit_u32(static_cast<std::uint32_t>(SectionType::FunctionTable));
+    b10.emit_u32(static_cast<std::uint32_t>(ft_offset10));
+    b10.emit_u32(static_cast<std::uint32_t>(ft_sz10));
+    b10.emit_u32(0);
+    b10.emit_bytes(code10, code_sz10);
+    b10.emit_bytes(consts10, cp_sz10);
+    b10.emit_bytes(fns10, ft_sz10);
+    b10.write_header(3, 88);

-    auto raw10 = b.raw;
+    auto raw10 = b10.raw;
```

The diff matches exactly the "preferred fix" recommended by REVIEW-018
§3 (option 1). No new `(void)` casts are introduced; all 13 lines of
Test 10 emit code now consistently use `b10` and the `*10` variables.

---

## 3. Final Verdict

| Check | Verdict |
|-------|---------|
| 1. No `(void)` casts in `tests/smoke.cpp` | **PASS** |
| 2. Test 10 uses `b10` (not `b`) | **PASS** |
| 3. Build clean without SAN; all 10 tests pass | **PASS** |
| 4. Build clean with SAN; 8 tests pass + 9/10 SKIPPED | **PASS** |

**Final status: APPROVED.**

All 4 mandatory checks PASS. The single FAIL from REVIEW-018 (Check 8
— three `(void)` casts hiding a copy-paste bug in Test 10) is fully
resolved by commit `614893e`. The fix matches REVIEW-018 §3's preferred
remedy verbatim (rename to `*10` suffixes, declare
`fns10`/`ft_sz10`/`ft_offset10`, switch all `b.emit_*` → `b10.emit_*`,
delete the three `(void)` casts). The bonus cleanup of the two
pre-existing `(void)` casts in `lifter.cpp:270,281` is also verified —
both are gone, the build is clean under `-Werror`, and the lifter's
`OBJ_GET`/`OBJ_SET` handlers continue to compile and run correctly
because the deleted `regmap.read(cell.s2())` / `regmap.read(cell.s1())`
calls were side-effect-free reads whose only purpose was to be silenced
by the cast.

---

## 4. Non-Blocking Observations (informational only, do not affect verdict)

| # | Observation | Severity | Recommendation |
|---|-------------|----------|----------------|
| N1 | The fix changed two comment strings in Test 10 (lines 927–928) from `"call native code"` to `"call native code10"` as a side-effect of the `b` → `b10` find-and-replace. `"native code10"` reads slightly awkwardly — `code10` is a local variable name, not a synonym for "native code". | Non-blocking | Cosmetic. Could be reverted to `"call native code"` in a future cleanup commit, but is harmless as-is (println output, not functional). |
| N2 | The pre-existing `(void)` casts in `lifter.cpp:270,281` were deleted by emptying the lines (leaving just whitespace before the comment). The deleted `regmap.read(cell.s2())` and `regmap.read(cell.s1())` calls were the only consumers of `cell.s2()` and `cell.s1()` in the `OBJ_GET`/`OBJ_SET` branches. The minimal lifter therefore no longer reads the object register at all — this is consistent with the lifter's stated "minimal subset" scope (per REVIEW-014/REVIEW-017), but a future "full lifter" commit will need to re-introduce the `regmap.read` for the obj handle. | Non-blocking | Document the deferred object-register read in a comment in `lifter.hpp` or in a future "full lifter" commit. |
| N3 | REVIEW-018's non-blocking concerns N1–N7 (spec citation `§12.4` should be `§4.6` for trace cache; CachedTrace is a minimal subset of §4.6; MOV_CONST lifter branch is untested; non-entry-CONST codegen path is untested; `double` CONST branch dropped; DCE/register-alloc interaction in Test 10) remain open and are not addressed by this fix commit (the commit message scoped itself to "fix REVIEW-018 Check 8 only"). | Non-blocking | Should be tracked in a follow-up "trace-cache + lifter coverage" commit per the producer's stated scope. |

---

## 5. Reviewer Sign-off

- **Reviewer agent ID:** `review-agent-019`
- **UTC timestamp:** `2026-09-08T20:05:00Z`
- **Final status:** `APPROVED`
- **Counts:** 4 PASS, 0 FAIL, 0 N/A
- **Verifier:** Both no-SAN and SAN=1 builds exit 0 with zero warnings
  and zero errors. No-SAN smoke runs all 10 tests PASS; SAN=1 smoke
  runs Tests 1–8 PASS + Tests 9+10 SKIPPED (correct preprocessor guard).
  No ASan or UBSan reports.
- **Blocking issue:** None. The REVIEW-018 FAIL is resolved.
