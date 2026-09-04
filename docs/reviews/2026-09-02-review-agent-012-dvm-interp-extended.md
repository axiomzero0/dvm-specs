# REVIEW-012 — DVM Tier 0 Interpreter: Extended Opcode Set

**Task ID:** REVIEW-012
**Reviewer Agent:** review-agent-012
**Scope:** Independent spec-compliance review of HEAD commit `3b84e3a` ("runtime/interp: extend opcode set + loop/object tests + heap cleanup") on the DVM Tier 0 interpreter (`runtime/interp/`).
**Spec corpus reviewed against:** `docs/DVM-CRB.md` §12 (FP arithmetic), §13 (conversion), §14 (comparison), §18 (raw memory — out of scope here), §19 (object model), §7 (instruction formats); `docs/Mandatory-Agent-Review-Rule.md` §3 (review protocol, independence, mandatory verifier, blocking status).
**UTC timestamp:** 2026-09-03T18:03:06Z

---

## 1. Prior reviews summary

The worklog records eleven prior reviews (REVIEW-001 through REVIEW-011). Final statuses:
REVIEW-001 CHANGES_REQUESTED (33/15); REVIEW-002 CHANGES_REQUESTED (12+3 PARTIAL);
REVIEW-003 APPROVED (3 PASS); REVIEW-004 APPROVED (7 PASS); REVIEW-005 CHANGES_REQUESTED (6+1 FAIL);
REVIEW-006 APPROVED (6 PASS); REVIEW-007 CHANGES_REQUESTED (15+2 FAIL); REVIEW-008 CHANGES_REQUESTED (9+2 FAIL);
REVIEW-009 APPROVED (8 PASS); REVIEW-010 CHANGES_REQUESTED (26 PASS / 1 FAIL — ModuleHeader vs spec §3.1 mismatch);
REVIEW-011 APPROVED (8 PASS — ModuleHeader rewritten, ADD/SUB_I64_WRAP signed-overflow UB fixed via wrap_add/wrap_sub helpers).

REVIEW-010 and REVIEW-011 are the two prior reviews of this same interpreter subsystem (the initial commit `3f4f2565` and the fix commit `d2d4145`). The current commit `3b84e3a` under review extends the interpreter's opcode set, adds two smoke tests, and adds heap cleanup.

---

## 2. Verdicts on 10 checks

### Check 1 — New comparison opcodes match spec §14
**Verdict: PASS**

Spec §14 (DVM-CRB.md:1114-1153) requires comparisons to produce boolean results using the `R_R_R` format (§7.1: `opcode | dst | src0 | src1`). All four new handlers in `src/opcodes_arith.cpp` comply:

- `op_cmp_ne` (opcodes_arith.cpp:92-99): `dst=s1, a=s2, b=s3, dst = boolean(!(a == b))`. Uses `operator==` from `value.hpp:87-101`, correctly negated. Int64/Bool/Float64/Null/Undef all handled by the existing equality operator.
- `op_cmp_le_s` (opcodes_arith.cpp:110-117): `dst = boolean(a <= b)` on Int64 signed values — `_s` (signed) variant.
- `op_cmp_gt_s` (opcodes_arith.cpp:119-126): `dst = boolean(a > b)` on Int64 signed values.
- `op_cmp_ge_s` (opcodes_arith.cpp:128-135): `dst = boolean(a >= b)` on Int64 signed values.

All four use `Value::boolean(...)` (value.hpp:62) which constructs a `Value` with `TypeTag::Bool` and payload 0/1. R_R_R slot assignment (`dst=s1, src0=s2, src1=s3`) matches spec §7.1 exactly. Opcodes 0x0601, 0x0604, 0x0606, 0x0608 per `opcodes_def.hpp:64-71`. The signed comparison helpers (`_s` suffix) correctly use `as_i64()` rather than unsigned comparison.

### Check 2 — New FP opcodes match spec §12
**Verdict: PASS**

Spec §12 (DVM-CRB.md:1031-1078) covers FP arithmetic; opcodes 0x0400–0x04FF. All four new handlers in `src/opcodes_arith.cpp` comply:

- `op_fadd_f64` (opcodes_arith.cpp:138-145): `dst = a + b` on doubles.
- `op_fsub_f64` (opcodes_arith.cpp:147-154): `dst = a - b`.
- `op_fmul_f64` (opcodes_arith.cpp:156-163): `dst = a * b`.
- `op_fdiv_f64` (opcodes_arith.cpp:165-172): `dst = a / b` with comment "IEEE-754 division-by-zero is well-defined".

All four use R_R_R format (`dst=s1, src0=s2, src1=s3`) and operate on `double` values via `as_f64()`. The `Value{double}` constructor (value.hpp:51) tags the result as `TypeTag::Float64`. IEEE-754 division-by-zero produces ±inf or NaN rather than a trap, matching the task's "well-defined, not a trap" requirement. The handlers do not enable `.fast` mode (default `.strict` per spec §12.2). Opcodes 0x0400–0x0403 per `opcodes_def.hpp:53-56`.

### Check 3 — Conversion opcodes match spec §13
**Verdict: PASS**

Spec §13 (DVM-CRB.md:1082-1110) covers conversions. The two new handlers comply:

- `op_i64_to_f64` (opcodes_arith.cpp:175-181): R_R format (`dst=s1, src=s2`); `static_cast<double>(a)` converts Int64→Float64. Matches `int_to_float.i64.f64` from the spec list.
- `op_f64_to_i64_wrap` (opcodes_arith.cpp:183-194): R_R format; uses `if (a != a)` to detect NaN (NaN self-comparison is false), returning 0 for NaN, otherwise `static_cast<std::int64_t>(a)` which truncates toward zero per C++26 [expr.static.cast]/3. The `.wrap` mode and NaN→0 behavior follow the task's expected semantic.

Non-blocking concern: `static_cast<std::int64_t>(a)` is implementation-defined (not UB) when `a` is outside the Int64 range (e.g., `1e30` or `+inf`). UBSan's `-fsanitize=float-cast-overflow` would flag this case. Not exercised by the smoke tests (which only convert 42.0). The spec §13 does not formally specify out-of-range behavior, so the implementation is not in spec violation; a future hardening pass could saturate to INT64_MIN/MAX.

### Check 4 — ALLOC matches spec §19
**Verdict: PASS**

Spec §19.2 (DVM-CRB.md:1527-1539) defines `OBJ_NEW` (opcode 0x0B00) with format `R_IMM32` and operand `type_id`. The implementation `op_alloc` (opcodes_object.cpp:36-44) uses R_IMM32 format (`dst=s1, field_count=imm32(s2,s3)`) — the format matches §7.3 exactly. It creates a `new ObjStorage(field_count)` where `ObjStorage`'s constructor (opcodes_object.cpp:20-23) is `explicit ObjStorage(std::size_t n) : fields(n, Value::null())` — so `field_count` null-initialized fields. The result is stored as a Value with `TypeTag::ObjRef` pointing to the heap object, and tracked in `s.heap` via `push_back` for cleanup. This matches the task's stated semantics exactly.

Non-blocking concern: spec §19.2 names this opcode `OBJ_NEW` with operand `type_id`; the implementation renames it `ALLOC` and interprets the 32-bit immediate as `field_count` rather than `type_id`. This is a documented minimal-interpreter simplification (no Type Table, no Shape Hints, no GC). It mirrors the existing simplifications noted in REVIEW-010 (CALL_DIRECT skips call-site table; THROW skips exception table). The task description explicitly accepts this semantic — "ALLOC (0x0B00), format R_IMM32 (dst=s1, field_count=imm32(s2,s3))".

### Check 5 — OBJ_GET/OBJ_SET match spec §18/§19
**Verdict: PASS**

Note: spec §18 is "Raw Memory Opcodes" (LOAD_MEM, STORE_MEM); spec §19 is "Object Model Opcodes" (OBJ_GET at §19.3, OBJ_SET at §19.4). The task description's "spec §18" reference for OBJ_GET/OBJ_SET appears to be a section-number typo; the substance is the object model in §19.

Spec §19.3 OBJ_GET (0x0A00) uses format `ACCESS` (§7.8: `opcode | dst_or_obj | obj_or_value | access_site16`). The implementation `op_obj_get` (opcodes_object.cpp:47-64) uses `dst=s1, obj=s2, field_idx=s3` — matches the ACCESS slot layout (dst/obj/site16), with the implementation interpreting the slot-3 access_site16 as a direct field index. The handler:
1. Type-checks `obj_val.tag == TypeTag::ObjRef` and traps (`OpResult::Trap`) if not — opcodes_object.cpp:52-55.
2. Bounds-checks `field_idx >= obj->fields.size()` and traps if out-of-bounds — opcodes_object.cpp:57-60.
3. Reads `obj->fields[field_idx]` into `dst` — opcodes_object.cpp:61.

Spec §19.4 OBJ_SET (0x0A01) uses format `ACCESS`. The implementation `op_obj_set` (opcodes_object.cpp:67-84) uses `obj=s1, field_idx=s2, value=s3`. The handler:
1. Type-checks `obj_val.tag == TypeTag::ObjRef` and traps if not — opcodes_object.cpp:72-75.
2. Bounds-checks `field_idx >= obj->fields.size()` and traps if not — opcodes_object.cpp:77-80.
3. Writes `obj->fields[field_idx] = s.reg(val_reg)` — opcodes_object.cpp:81.

Both bounds-check and type-check requirements from the task are satisfied. The trap behavior (set `exit_value = Value::null()`, return `OpResult::Trap`) is consistent with the existing `op_add_i64_checked` trap pattern.

Non-blocking concern: For OBJ_SET the spec's ACCESS example (DVM-CRB.md:571-573: `obj.set r2, r6, access(7)`) places value in slot 2 and access_site16 in slot 3. The implementation places field_idx in slot 2 and value in slot 3 — a slot-order deviation from the strict spec ACCESS example for OBJ_SET. The producer's layout mirrors §19.6 IDX_SET's R_R_R (obj/index/value) ordering, treating OBJ_SET effectively as R_R_R rather than strict ACCESS. This is internally consistent with the producer's own Test 4. The task description explicitly accepts this layout ("OBJ_SET: ACCESS format (obj=s1, field_idx=s2, value=s3)"), so this is a documented minimal-interpreter simplification, not a spec violation in the context of the review contract.

### Check 6 — Heap cleanup works
**Verdict: PASS**

- `state.hpp:62` declares `std::vector<ObjStorage*> heap;` as a member of `InterpState`.
- `state.hpp:66` declares `void free_heap() noexcept;`.
- `opcodes_object.cpp:28-33` implements `InterpState::free_heap()` as: iterate `heap`, `delete obj` for each (which calls `vector<Value>` destructor for `ObjStorage::fields` and frees the heap block), then `heap.clear()`.
- `op_alloc` (opcodes_object.cpp:36-44) pushes every allocation into `s.heap`.
- `interp.cpp:331-333` calls `s.free_heap()` at the `interp_exit:` label before `return s.exit_value;`.
- All dispatch loop paths either `DISPATCH_NEXT()` or `goto interp_exit`, ensuring `free_heap` is always reached.

Verifier confirmation: `ASAN_OPTIONS=detect_leaks=1:exitcode=42 ./bin/dvm_interp_smoke` returned exit code 0 (not 42), proving LeakSanitizer found no leaks. No ASan or UBSan runtime errors were printed during the run.

Non-blocking concern: `InterpState` has no destructor that calls `free_heap()`; cleanup relies on every execution path reaching `interp_exit`. This is currently safe because every handler is `noexcept` and no path can throw or longjmp past `interp_exit`. A more robust design would add a destructor for `InterpState` that calls `free_heap()`, providing defense in depth against future code paths that exit `interpret()` early. Not a spec violation.

### Check 7 — All new opcodes registered in the dispatch table
**Verdict: PASS**

Cross-checked `src/interp.cpp` dispatch table initialization (lines 56-98) against the handler labels in the dispatch loop body (lines 109-329). The 13 actually-added opcodes (the task description says "16" but the diff and the §14/§12/§13/§19+§18 breakdown lists only 13 — see Check-7 note below) all have both a dispatch-table entry and a corresponding handler label:

| Opcode | Hex | Dispatch entry | Handler label |
|--------|-----|----------------|---------------|
| CMP_NE | 0x0601 | interp.cpp:77 | interp.cpp:203 (`op_cmp_ne`) |
| CMP_LE_S | 0x0604 | interp.cpp:79 | interp.cpp:213 (`op_cmp_le_s`) |
| CMP_GT_S | 0x0606 | interp.cpp:80 | interp.cpp:218 (`op_cmp_gt_s`) |
| CMP_GE_S | 0x0608 | interp.cpp:81 | interp.cpp:223 (`op_cmp_ge_s`) |
| FADD_F64 | 0x0400 | interp.cpp:82 | interp.cpp:230 (`op_fadd_f64`) |
| FSUB_F64 | 0x0401 | interp.cpp:83 | interp.cpp:235 (`op_fsub_f64`) |
| FMUL_F64 | 0x0402 | interp.cpp:84 | interp.cpp:240 (`op_fmul_f64`) |
| FDIV_F64 | 0x0403 | interp.cpp:85 | interp.cpp:245 (`op_fdiv_f64`) |
| I64_TO_F64 | 0x0500 | interp.cpp:86 | interp.cpp:252 (`op_i64_to_f64`) |
| F64_TO_I64_WRAP | 0x0501 | interp.cpp:87 | interp.cpp:257 (`op_f64_to_i64_wrap`) |
| ALLOC | 0x0B00 | interp.cpp:88 | interp.cpp:264 (`op_alloc`) |
| OBJ_GET | 0x0A00 | interp.cpp:89 | interp.cpp:271 (`op_obj_get`) |
| OBJ_SET | 0x0A01 | interp.cpp:90 | interp.cpp:276 (`op_obj_set`) |

All 13 are present. The dispatch table is `static void* dispatch[65536]` (interp.cpp:56), pre-initialized to `&&op_unknown` for all 65,536 entries (interp.cpp:59) so unknown opcodes fall through to the trap handler.

Non-blocking concern (count discrepancy): The task preamble and Check 7 say "16 new handlers", but the actual diff adds only 13 (4 cmp + 4 fp + 2 conv + 3 obj = 13). The commit message has the same off-by-3 inconsistency ("Adds 16 new opcode handlers" followed by a list of 13). The 3 missing handlers (CMP_EQ 0x0600, CMP_LT_S 0x0602, ADD_I64_CHECKED 0x0229) were already in REVIEW-010/011's interpreter. This is a cosmetic defect in the commit message / task description, not a code defect — all 13 actually-new opcodes are correctly registered and dispatched.

### Check 8 — Test 3 (loop) is correct
**Verdict: PASS**

`tests/smoke.cpp:307-376` constructs a 7-instruction module:
```
0: MOV_CONST r0, const(0)    ; r0 = 0  (counter)
1: MOV_CONST r1, const(1)    ; r1 = 1  (increment)
2: MOV_CONST r2, const(2)    ; r2 = 10 (limit)
3: ADD_I64_WRAP r0, r0, r1   ; r0 += 1
4: CMP_LT_S r3, r0, r2        ; r3 = (r0 < 10)
5: BR_TRUE r3, -3             ; if r3: goto 3
6: RET r0
```

BR_TRUE delta encoding: `cell(op::BR_TRUE, 3, 0xFFFD, 0xFFFF)` (smoke.cpp:327).
- `imm32(0xFFFD, 0xFFFF) = (0xFFFF << 16) | 0xFFFD = 0xFFFFFFFD`
- `static_cast<int32_t>(0xFFFFFFFD) = -3` ✓

Branch arithmetic trace:
- BR_TRUE at PC=5 → `op_br_true` reads `delta=-3`, evaluates `cond=r3.truthy()`, then `s.advance()` advances PC to 6, then if cond `s.branch(-3)` sets `PC = 6 + (-3) = 3` (per `branch()` in state.hpp:96-100).
- Loop iterates while `r0 < 10`; when `r0` reaches 10, CMP_LT_S returns false, BR_TRUE does not branch, advance to PC=6, RET returns `r0=10`.

The expected result is **10** (the counter value, not the sum 1+2+…+10=55). The test asserts `result3.as_i64() == 10` (smoke.cpp:371) — correct counting semantics, not summation. Smoke run confirms: `Test 3: count to 10 = 10 (PASS)`.

The delta=-3 from next PC=6 yielding PC=3 is verified by both the static code review and the actual smoke run (which would otherwise loop forever or return the wrong value).

### Check 9 — Test 4 (object) is correct
**Verdict: PASS**

`tests/smoke.cpp:378-438` constructs a 5-instruction module:
```
0: ALLOC r0, 2              ; r0 = new Obj(2 fields)
1: MOV_CONST r1, const(0)  ; r1 = const(0) = 42
2: OBJ_SET r0, 0, r1       ; r0.field[0] = r1
3: OBJ_GET r2, r0, 0       ; r2 = r0.field[0]
4: RET r2
```

Slot encoding verification:
- `cell(op::ALLOC, 0, 2, 0)` (smoke.cpp:390): s1=0 (dst=r0), s2=2 (imm32_low), s3=0 (imm32_high). `imm32(2,0) = (0<<16)|2 = 2` → field_count=2 ✓
- `cell(op::MOV_CONST, 1, 0, 0)` (smoke.cpp:391): s1=1 (dst=r1), imm32=0 → const[0]. `i64_const(42)` at smoke.cpp:397 → r1 = 42 ✓
- `cell(op::OBJ_SET, 0, 0, 1)` (smoke.cpp:392): s1=0 (obj=r0), s2=0 (field_idx=0), s3=1 (val=r1=42). Matches `op_obj_set` slot contract ✓
- `cell(op::OBJ_GET, 2, 0, 0)` (smoke.cpp:393): s1=2 (dst=r2), s2=0 (obj=r0), s3=0 (field_idx=0). Matches `op_obj_get` slot contract ✓
- `cell(op::RET, 2)` (smoke.cpp:394): returns r2 ✓

Trace: ALLOC creates ObjStorage with 2 null fields, r0 holds ObjRef to it; MOV_CONST loads 42 into r1; OBJ_SET type-checks r0 as ObjRef (passes), bounds-checks field_idx=0 < 2 (passes), writes fields[0]=42; OBJ_GET type-checks (passes), bounds-checks (passes), reads fields[0]=42 into r2; RET returns 42.

Test asserts `result4.tag == TypeTag::Int64 && result4.as_i64() == 42` (smoke.cpp:433). Smoke run confirms: `Test 4: ALLOC + OBJ_SET(42) + OBJ_GET = 42 (PASS)`. Internally consistent end-to-end.

### Check 10 — Build is clean + all 4 smoke tests pass
**Verdict: PASS**

**Mandatory verifier run** (per `Mandatory-Agent-Review-Rule.md` §3.3):

```
$ cd /home/z/my-project/dgw-core-repo/runtime/interp
$ make clean && make SAN=1 -j$(nproc) 2>&1 | tail -5
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow \
    -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter \
    -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Iinclude tests/smoke.cpp -Lbuild -ldvm_interp -o bin/dvm_interp_smoke \
    -fsanitize=address,undefined -fno-omit-frame-pointer
BUILD-EXIT=0
```

Build log analysis: 17 lines total, `grep -icE 'warning:|error:' /tmp/build_final.txt` returns 0 matches. Zero warnings, zero errors. The per-file `-Wno-pedantic` exception for `interp.o` (Makefile:51) is unchanged from REVIEW-010/011 and is the documented GNU computed-goto exception. No new warning suppressions introduced by this commit.

```
$ ./bin/dvm_interp_smoke 2>&1
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

== DVM Interpreter smoke test PASSED ==
SMOKE-EXIT=0
```

All 4 tests PASS; smoke binary exits 0. No ASan / UBSan runtime diagnostics printed during execution. LeakSanitizer confirmation: `ASAN_OPTIONS=detect_leaks=1:exitcode=42 ./bin/dvm_interp_smoke` returns exit code 0 (not 42), proving zero leaks.

---

## 3. Verifier run log

```
$ cd /home/z/my-project/dgw-core-repo/runtime/interp
$ make clean
$ make SAN=1 -j$(nproc) 2>&1 | tail -5
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow \
    -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter \
    -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Iinclude tests/smoke.cpp -Lbuild -ldvm_interp -o bin/dvm_interp_smoke \
    -fsanitize=address,undefined -fno-omit-frame-pointer
BUILD-EXIT=0   (verified via `echo $?` after build)

$ grep -icE 'warning:|error:' /tmp/build_final.txt
0   (zero warnings, zero errors)

$ ./bin/dvm_interp_smoke 2>&1
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

== DVM Interpreter smoke test PASSED ==
SMOKE-EXIT=0

$ ASAN_OPTIONS=detect_leaks=1:exitcode=42 ./bin/dvm_interp_smoke
(same output as above)
EXIT=0   (LeakSanitizer ran; 0 == no leaks detected)
```

---

## 4. Final status

**APPROVED**

10 PASS, 0 FAIL. Verifier green: build exit 0, smoke exit 0, all 4 tests PASS, zero ASan/UBSan runtime errors, zero leaks under LeakSanitizer.

Non-blocking concerns (none of which are spec violations in the context of the review contract; all are documented minimal-interpreter simplifications matching prior REVIEW-010/011 conventions or are robustness suggestions for a future hardening pass):

1. **Count discrepancy in commit message / task description.** Both the commit message and the task preamble say "16 new opcode handlers", but the actual diff adds 13 (4 cmp + 4 fp + 2 conv + 3 obj). The other 3 of the previously-implemented 16 opcodes (CMP_EQ 0x0600, CMP_LT_S 0x0602, ADD_I64_CHECKED 0x0229) were already in the interpreter as of REVIEW-010/011. Cosmetic defect only — all 13 actually-new opcodes are correctly implemented and dispatched.

2. **F64_TO_I64_WRAP out-of-range behavior.** `static_cast<std::int64_t>(a)` for `a` outside Int64 range (e.g., `1e30`, `±inf`) is implementation-defined (not UB), but UBSan's `float-cast-overflow` would flag it. Not exercised by current smoke tests. Spec §13 does not formally specify out-of-range behavior, so no spec violation. Suggested hardening: saturate to INT64_MAX/INT64_MIN for finite-but-out-of-range, keep NaN→0.

3. **ALLOC OOM behavior.** `new ObjStorage(field_count)` would throw `std::bad_alloc` for huge `field_count`; since `op_alloc` is `noexcept`, this would call `std::terminate`. Not exercised by current smoke tests. Spec §19.2 says "May raise a guest memory exception" — a future hardening pass could catch the exception and trap with a guest-visible error.

4. **OBJ_SET slot-order deviation from strict spec §7.8 ACCESS example.** Spec example `obj.set r2, r6, access(7)` places value in slot 2 and access_site16 in slot 3. The implementation places field_idx in slot 2 and value in slot 3. The producer's layout mirrors §19.6 IDX_SET's R_R_R (obj/index/value) ordering, treating OBJ_SET effectively as R_R_R rather than strict ACCESS. Internally consistent with the producer's own Test 4 and explicitly accepted by the task description ("OBJ_SET: ACCESS format (obj=s1, field_idx=s2, value=s3)"). Documented minimal-interpreter simplification, not a spec violation.

5. **ALLOC name and operand semantic simplification.** Spec §19.2 calls this opcode `OBJ_NEW` with operand `type_id`; the implementation renames it `ALLOC` and interprets the 32-bit immediate as `field_count`. Same simplification category as CALL_DIRECT skipping the call-site table (REVIEW-010 noted) and THROW skipping the exception table (REVIEW-010 noted). Documented and accepted by the task description.

6. **InterpState has no destructor calling `free_heap()`.** Cleanup relies on every execution path reaching `interp_exit`. Currently safe because all handlers are `noexcept` and no path can exit `interpret()` without going through `interp_exit`. Defense in depth: a destructor for `InterpState` that calls `free_heap()` would protect against future code paths that return early. Not a spec violation.

Recommendation to producer (optional, non-blocking): fix the "16 vs 13" count in the commit message before merge to avoid future audit confusion; consider adding an `~InterpState()` destructor for robustness; consider saturating `F64_TO_I64_WRAP` to INT64_MAX/MIN for out-of-range finite inputs.

---

## 5. Reviewer agent ID

`review-agent-012`

## 6. UTC timestamp

2026-09-03T18:03:06Z

---

## Appendix A — Spec citations reviewed

- DVM-CRB.md §7.1 (R_R_R), §7.3 (R_IMM32), §7.8 (ACCESS), §7.10 (R), §7.12 (R_R_IMM32)
- DVM-CRB.md §12 (Floating-Point Opcodes — add/sub/mul/div .f64 .strict/.fast)
- DVM-CRB.md §13 (Conversion Opcodes — int_to_float.i64.f64, float_to_int.f64.i64)
- DVM-CRB.md §14 (Comparison Opcodes — cmp_ne/cmp_lt_s/cmp_le_s/cmp_gt_s/cmp_ge_s produce boolean)
- DVM-CRB.md §18 (Raw Memory Opcodes — out of scope; task description's "§18" for OBJ_GET/OBJ_SET is a section-number typo, substance is §19)
- DVM-CRB.md §19.2 (OBJ_NEW / 0x0B00, R_IMM32, operand type_id)
- DVM-CRB.md §19.3 (OBJ_GET / 0x0A00, ACCESS, dst = obj.member)
- DVM-CRB.md §19.4 (OBJ_SET / 0x0A01, ACCESS, obj.member = value)
- DVM-CRB.md §19.6 (IDX_SET / 0x0A11, R_R_R, obj/index/value — referenced for OBJ_SET layout comparison)
- Mandatory-Agent-Review-Rule.md §3 (review protocol — independence, spec-indexed, mandatory verifier, blocking status)

## Appendix B — Files reviewed

- `runtime/interp/include/dvm/opcodes.hpp` (93 lines)
- `runtime/interp/include/dvm/state.hpp` (104 lines)
- `runtime/interp/include/dvm/value.hpp` (107 lines)
- `runtime/interp/include/dvm/crb.hpp` (183 lines)
- `runtime/interp/include/dvm/opcodes_def.hpp` (134 lines)
- `runtime/interp/include/dvm/interp.hpp` (25 lines)
- `runtime/interp/src/interp.cpp` (340 lines)
- `runtime/interp/src/opcodes_arith.cpp` (197 lines)
- `runtime/interp/src/opcodes_object.cpp` (87 lines — new file)
- `runtime/interp/src/opcodes_control.cpp` (65 lines — for BR_TRUE/JMP delta verification)
- `runtime/interp/src/opcodes_move.cpp` (74 lines — for MOV_CONST semantics verification)
- `runtime/interp/src/state.cpp` (48 lines)
- `runtime/interp/src/loader.cpp` (120 lines — for module load path verification)
- `runtime/interp/tests/smoke.cpp` (443 lines)
- `runtime/interp/Makefile` (81 lines)
