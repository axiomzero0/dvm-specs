# Spec Compliance Review — REVIEW-009

**Task ID:** REVIEW-009
**Agent:** review-agent-009
**Output under review:** commit `011f84b` — "docs/dvm-crb: address REVIEW-008 CHANGES_REQUESTED (arithmetic + missing formats)"; 2 files changed: `docs/DVM-CRB.md` (+40/-11), `docs/reviews/2026-09-02-review-agent-008-dgw-core-crb-spec-fixes.md` (new file, 299 lines)
**Spec corpus read in full:** `Mandatory-Agent-Review-Rule.md` (Section 3 in particular), `DVM-CRB.md`, REVIEW-008 report
**UTC timestamp:** 2026-09-01T22:46:48Z

---

## 1. Prior Review (REVIEW-008) Summary

REVIEW-008 (review-agent-008) audited commit `cdb34cb` and returned
CHANGES_REQUESTED with 9 PASS + 2 FAIL out of 11 checks. Both FAILs were in
`docs/DVM-CRB.md`:

- **Check 3 FAIL (Section 11.4 arithmetic):** two of the 9 canonical
  examples in Section 11.4 had arithmetic that did not match the spec's
  own encoding formula `opcode = 0x0200 + (variant*0x10) + (width*0x04)
  + (overflow*0x01)`. `MUL_I32_CHECKED` was labeled `0x0221` but the
  formula yields `0x0229` (the wrong value corresponds to
  `MUL_I8_CHECKED` with width=0, not width=2=i32 as the label requires);
  `DIV_S_I64_CHECKED` was labeled `0x022D` but the formula yields
  `0x023D` (the wrong value corresponds to `MUL_I64_CHECKED` with
  variant=2=mul, not variant=3=div_s as the label requires).
- **Check 4 FAIL (missing `format:` lines):** 7 of the 10 opcodes
  REVIEW-007 Check 3 explicitly named as lacking `format:` lines
  (`BR_NULL`, `BR_NONNULL`, `RET_VOID`, `TAIL_CALL_DIRECT`, `IDX_GET`,
  `IDX_SET`, `OBJ_IS_INSTANCE`) still lacked `format:` lines after the
  prior fix; 6 additional opcodes also lacked `format:` lines (`TRAP`,
  `UNREACHABLE`, `SAFEPOINT`, `PROFILE_POINT`, `SUSPEND_YIELD`,
  `SUSPEND_AWAIT`).

The producer's fix commit `011f84b` claims to resolve both FAILs and to
re-verify all 9 canonical examples by python3 re-computation. This
review (REVIEW-009) re-verifies the fix.

Per Section 3.1 of `Mandatory-Agent-Review-Rule.md`, I am an independent
reviewer (not review-agent-001 through -008 and not the producer of
commit `011f84b`).

---

## 2. Reviewer's Verdicts on the Fix

### Check 1 — Section 11.4 arithmetic is now correct
**Verdict:** PASS
**Evidence:**
- Section 11.4 (docs/DVM-CRB.md:991-999) lists the 9 canonical examples:
  ```text
  ADD_I64_WRAP       = 0x0200 + (0*0x10) + (3*0x04) + 0 = 0x020C
  ADD_I64_CHECKED    = 0x0200 + (0*0x10) + (3*0x04) + 1 = 0x020D
  SUB_I32_WRAP       = 0x0200 + (1*0x10) + (2*0x04) + 0 = 0x0218
  MUL_I32_CHECKED    = 0x0200 + (2*0x10) + (2*0x04) + 1 = 0x0229
  DIV_S_I64_CHECKED  = 0x0200 + (3*0x10) + (3*0x04) + 1 = 0x023D
  NEG_I64_WRAP       = 0x0200 + (7*0x10) + (3*0x04) + 0 = 0x027C
  NEG_I64_CHECKED    = 0x0200 + (7*0x10) + (3*0x04) + 1 = 0x027D
  NOT_I64_WRAP       = 0x0200 + (8*0x10) + (3*0x04) + 0 = 0x028C
  SHR_U_I64_WRAP     = 0x0200 + (14*0x10) + (3*0x04) + 0 = 0x02EC
  ```
- Used the variant/width/overflow indices from the spec (Section 11.4,
  lines 967-984: variants add=0…shr_u=14; widths i8=0, i16=1, i32=2,
  i64=3; overflow modes wrap=0, checked=1, assume=2, sat=3) and
  recomputed all 9 examples with python3:
  ```
  NAME                     SPEC  FORMULA  DELTA  MATCH
  ADD_I64_WRAP           0x020C  0x020C     +0  OK
  ADD_I64_CHECKED        0x020D  0x020D     +0  OK
  SUB_I32_WRAP           0x0218  0x0218     +0  OK
  MUL_I32_CHECKED        0x0229  0x0229     +0  OK
  DIV_S_I64_CHECKED      0x023D  0x023D     +0  OK
  NEG_I64_WRAP           0x027C  0x027C     +0  OK
  NEG_I64_CHECKED        0x027D  0x027D     +0  OK
  NOT_I64_WRAP           0x028C  0x028C     +0  OK
  SHR_U_I64_WRAP         0x02EC  0x02EC     +0  OK
  ALL MATCH: True
  ```
- The two prior FAILs are corrected: `MUL_I32_CHECKED` is now `0x0229`
  (was `0x0221`); `DIV_S_I64_CHECKED` is now `0x023D` (was `0x022D`).
  Both new values match the formula exactly (delta = 0).
- The Range check subsection (lines 1020-1024) was independently
  recomputed: `0x0200 + (14 * 0x10) + (3 * 0x04) + 3 = 0x02EF` ✓; `15
  × 4 × 4 = 240` ✓; `0x0200-0x03FF = 512 slots` ✓; `0x02F0-0x03FF =
  272 reserved slots` ✓.

**Reasoning:** All 9 canonical examples in Section 11.4 now match the
spec's own encoding formula `opcode = 0x0200 + (variant*0x10) +
(width*0x04) + (overflow*0x01)`. The two arithmetic errors from
REVIEW-008 Check 3 (`MUL_I32_CHECKED` and `DIV_S_I64_CHECKED`) are
corrected, and the other 7 examples remain correct. The Range check
arithmetic is also correct. Check 1 PASSES.

---

### Check 2 — All 13 previously-missing `format:` lines are now present
**Verdict:** PASS
**Evidence:**
- Walked each of the 13 opcodes' subsections in `docs/DVM-CRB.md` and
  confirmed each now contains a `format:` line:
  ```
  9.2    TRAP                 -> format: none      (docs/DVM-CRB.md:697)
  9.3    UNREACHABLE          -> format: none      (docs/DVM-CRB.md:710)
  9.4    SAFEPOINT            -> format: none      (docs/DVM-CRB.md:723)
  9.5    PROFILE_POINT        -> format: R_IMM32   (docs/DVM-CRB.md:736)
  15.4   BR_NULL              -> format: BRANCH    (docs/DVM-CRB.md:1196)
  15.5   BR_NONNULL           -> format: BRANCH    (docs/DVM-CRB.md:1209)
  16.2   RET_VOID             -> format: none      (docs/DVM-CRB.md:1267)
  16.9   TAIL_CALL_DIRECT     -> format: CALL      (docs/DVM-CRB.md:1348)
  19.5   IDX_GET              -> format: R_R_R     (docs/DVM-CRB.md:1585)
  19.6   IDX_SET              -> format: R_R_R     (docs/DVM-CRB.md:1604)
  19.8   OBJ_IS_INSTANCE      -> format: R_R_IMM32 (docs/DVM-CRB.md:1624)
  21.1   SUSPEND_YIELD        -> format: R_IMM32   (docs/DVM-CRB.md:1739)
  21.2   SUSPEND_AWAIT        -> format: R_IMM32   (docs/DVM-CRB.md:1753)
  ```
- All 13 opcodes listed in the task description now have a `format:`
  line. The fix is complete with respect to Check 2.

**Reasoning:** REVIEW-008 Check 4 explicitly enumerated 13 opcodes
still lacking `format:` lines after the prior fix (7 from REVIEW-007's
explicit list + 6 additional opcodes that REVIEW-007 did not flag but
which REVIEW-008's awk scan discovered). All 13 are now fixed. Check 2
PASSES.

---

### Check 3 — All `format:` references still resolve to Section 7 definitions
**Verdict:** PASS
**Evidence:**
- Re-ran REVIEW-008 Check 1's grep: `grep -oE 'format: [A-Z_0-9]+' docs/DVM-CRB.md | sort -u` returns the 12-name set:
  `ACCESS`, `BRANCH`, `CALL`, `CALL_INDIRECT`, `JUMP`, `R`, `R_IMM32`, `R_R`, `R_R_IMM32`, `R_R_R`, `R_R_SITE16`, `SWITCH`.
  (The new additions in this fix introduce `R_R_R` to the uppercase set
  — previously `R_R_R` was referenced only in Section 11.4 prose, not
  via a `format:` line; it is now also referenced via the `format:`
  lines at IDX_GET 19.5 and IDX_SET 19.6.)
- `grep -nE '^## 7\.[0-9]+ ' docs/DVM-CRB.md` returns 12 Section 7
  format definitions: `R_R_R` (7.1), `R_R` (7.2), `R_IMM32` (7.3),
  `BRANCH` (7.4), `JUMP` (7.5), `CALL` (7.6), `CALL_INDIRECT` (7.7),
  `ACCESS` (7.8), `SWITCH` (7.9), `R` (7.10), `R_R_SITE16` (7.11),
  `R_R_IMM32` (7.12).
- Cross-tabulating the two sets: every one of the 12 uppercase format
  names referenced by `format:` lines has a matching `## 7.N \`<NAME>\``
  definition in Section 7. All 12 Section 7 format definitions are now
  referenced by at least one `format:` line (including `R_R_R`, which
  is now referenced via the new IDX_GET / IDX_SET `format:` lines).
- The lowercase special value `format: none` (used by 5 opcodes:
  NOP 9.1, TRAP 9.2, UNREACHABLE 9.3, SAFEPOINT 9.4, RET_VOID 16.2)
  is a special no-operand placeholder, not a real format with a slot
  layout. It was already present at NOP (9.1) in REVIEW-008, where it
  was not flagged as an orphan because (a) the REVIEW-008 Check 1 grep
  pattern `format: [A-Z_0-9]+` is uppercase-only and intentionally
  excludes lowercase `none`, and (b) "none" semantically denotes "no
  operands" — Section 7 defines only slot-bearing 2-4-slot layouts. I
  follow the same convention here.

**Reasoning:** All 12 uppercase format names referenced by `format:`
lines resolve to a Section 7 definition; the special lowercase value
`none` follows the same pre-existing convention as REVIEW-008 Check 1
(no-operand opcodes use `format: none`, which is not a Section 7
format). No new orphan format references were introduced by the fix.
Check 3 PASSES.

---

### Check 4 — No opcodes with `opcode:` but no `format:` remain
**Verdict:** PASS
**Evidence:**
- Re-ran an equivalent of REVIEW-008 Check 4's awk scan (implemented in
  Python because the system's awk is mawk 1.3.4, which lacks the
  `match(..., arr)` 3-arg form) over `docs/DVM-CRB.md`. The script
  finds every `## N.M \`NAME\`` subsection that is not in Section 7.x
  (Section 7.x subsections are format definitions, not opcodes), then
  tracks whether the subsection contains an `opcode:` line and a
  `format:` line, and reports any subsection that has `opcode:` but
  no `format:`.
- Result: `No opcodes with opcode: but no format: (excluding Section
  7.x definitions)` — zero matches. Every opcode in Sections 9, 11,
  13, 14, 15, 16, 18, 19, 20, 21, 24 that has an `opcode:` line now
  also has a `format:` line.

**Reasoning:** REVIEW-008 Check 4 found 13 opcodes with `opcode:` but
no `format:`. After this fix, the scan returns zero. All opcodes in
the spec are now self-consistent: each has both an `opcode:` line and
a `format:` line. Check 4 PASSES.

---

### Check 5 — The format choices are semantically reasonable
**Verdict:** PASS
**Evidence:**
- For each of the 13 newly-fixed opcodes, the chosen format matches
  the opcode's semantics and the task's expected mapping:

  | Section | Opcode | Format | Expected | Match |
  |---------|--------|--------|----------|-------|
  | 9.2 | TRAP | none | none (no operands) | ✓ |
  | 9.3 | UNREACHABLE | none | none (no operands) | ✓ |
  | 9.4 | SAFEPOINT | none | none (no operands) | ✓ |
  | 9.5 | PROFILE_POINT | R_IMM32 | R_IMM32 (reg + imm32) | ✓ |
  | 15.4 | BR_NULL | BRANCH | BRANCH (cond_reg + delta32) | ✓ |
  | 15.5 | BR_NONNULL | BRANCH | BRANCH (cond_reg + delta32) | ✓ |
  | 16.2 | RET_VOID | none | none (no operands) | ✓ |
  | 16.9 | TAIL_CALL_DIRECT | CALL | CALL (matches CALL_DIRECT) | ✓ |
  | 19.5 | IDX_GET | R_R_R | R_R_R (dst, obj, index) | ✓ |
  | 19.6 | IDX_SET | R_R_R | R_R_R (obj, index, value) | ✓ |
  | 19.8 | OBJ_IS_INSTANCE | R_R_IMM32 | R_R_IMM32 (dst, src, type_id) | ✓ |
  | 21.1 | SUSPEND_YIELD | R_IMM32 | R_IMM32 (dst, site_id32) | ✓ |
  | 21.2 | SUSPEND_AWAIT | R_IMM32 | R_IMM32 (dst, site_id32) | ✓ |

- Cross-reference to sibling opcodes (consistency check):
  - `BR_TRUE` (15.2) uses `format: BRANCH`; `BR_FALSE` (15.3) uses
    `format: BRANCH`; `BR_NULL` and `BR_NONNULL` now also use
    `format: BRANCH` — consistent sibling pattern.
  - `CALL_DIRECT` (16.3) uses `format: CALL`; `TAIL_CALL_DIRECT`
    (16.9) now uses `format: CALL` — consistent with the commit
    message's note that "the difference is that the caller's frame is
    discarded before the callee is entered (tail position)", and
    same-operand-layout tail-call convention is standard.
  - `SUSPEND_YIELD` (21.1) and `SUSPEND_AWAIT` (21.2) both now use
    `format: R_IMM32` with `operand: suspension_site_id` — paired
    opcodes with paired format/operand descriptors.
  - `PROFILE_POINT` (9.5) uses `format: R_IMM32` with
    `operand: profile_id32` — its `format:` choice is consistent
    with its `operand:` line (a 32-bit immediate operand).
- The four `format: none` choices (TRAP, UNREACHABLE, SAFEPOINT,
  RET_VOID) all match opcodes that take no operands (TRAP/UNREACHABLE/
  SAFEPOINT are marker/control opcodes; RET_VOID returns no value and
  discards the frame).
- The two `format: R_R_R` choices (IDX_GET, IDX_SET) match the
  semantics of three-register indexed load/store. The prose for IDX_GET
  (line 1594: "The three register operands are `dst`, `obj`, and
  `index`") and IDX_SET (line 1612: "The three register operands are
  `obj`, `index`, and `value`") correctly names the three register
  slots in semantic order.
- The `format: R_R_IMM32` choice for OBJ_IS_INSTANCE matches the
  opcode's semantics: the result boolean goes in `dst`, the object to
  test goes in `src`, and the `type_id` to test against is the 32-bit
  immediate. Prose at line 1638 confirms: "The two register operands
  are `dst` (result boolean) and `src` (object to test); the 32-bit
  immediate is the `type_id` to test against."

**Reasoning:** Every one of the 13 newly-fixed opcodes uses a format
that (a) matches the task's expected semantic mapping, (b) is
consistent with sibling opcodes' format choices where applicable
(BR_TRUE/BR_FALSE → BRANCH for BR_NULL/BR_NONNULL; CALL_DIRECT → CALL
for TAIL_CALL_DIRECT; SUSPEND_YIELD → R_IMM32 for SUSPEND_AWAIT), and
(c) is corroborated by prose that explicitly names the operand slots.
No format choice is semantically unreasonable. Check 5 PASSES.

---

### Check 6 — Build is still clean
**Verdict:** PASS
**Evidence:**
- `cd /home/z/my-project/dgw-core-repo/compiler/dgw-core && make clean && make SAN=1 -j$(nproc)` exited 0.
- Piping the full build output through `grep -cE 'warning:|error:'`
  returned 0 — zero warnings, zero errors.
- Build produced 10 `.o` files, `libdgwcore.a`, and `bin/dgw_smoke`
  under ASan + UBSan (`-fsanitize=address,undefined
  -fno-omit-frame-pointer`).
- The fix commit touches only `docs/DVM-CRB.md` (a spec document) and
  the REVIEW-008 report file under `docs/reviews/`; no C++ source,
  Makefile, or test fixture was modified, so the build cannot have
  regressed.

**Reasoning:** The build is clean under `make SAN=1 -j$(nproc)` with
zero warnings and zero errors, matching REVIEW-008's clean-build
baseline. The spec-only fix commit does not touch any C++ source, so
build behavior is unchanged. Check 6 PASSES.

---

### Check 7 — Smoke test still passes
**Verdict:** PASS
**Evidence:**
- `./bin/dgw_smoke` exited 0 with `== DGW-Core smoke test PASSED ==`.
- All 8 test groups green; numerical results byte-for-byte identical
  to REVIEW-008's recorded baseline:
  - Pre-opt WebVerifier: 11 PASS / 0 FAIL / 0 N/A
  - Post-opt WebVerifier: 11 PASS / 0 FAIL / 0 N/A
  - GVN: eliminated=1, visited=7
  - DCE: killed=1, live=11
  - Cleanup: collapsed=0, killed=0
  - Scheduler Test 1: 4 blocks (START+GUARD+RETURN in #0, LOAD in #1,
    STORE in #2, DEOPT_TRAP in #3)
  - Test 2 (CALL+HANDLER): route_except_to ok (edge id=3), 5 nodes, 6
    edges
  - Test 3 (BRANCH+PGO): 2 blocks
  - Test 4 (STATE+LICM): visited=1, hoisted=0
  - Test 4b (STATE+pure invariant ADD): visited=1, hoisted=0
  - Test 5/5b: single-failure-to-trap ok + non-trap consumer flagged
  - Test 6 (JOIN→PHI): 1 block, JOIN PHI 2 incomings, 2 block_ids

**Reasoning:** The smoke test passes with all 8 groups green and
numerical results unchanged from REVIEW-008's recorded baseline (which
in turn matched REVIEW-007's, REVIEW-006's). The spec-only fix commit
does not touch any test fixtures or runtime code, so smoke-test
behavior is preserved exactly. Check 7 PASSES.

---

### Check 8 — No behavior change
**Verdict:** PASS
**Evidence:**
- `git diff HEAD~1 HEAD --stat` shows exactly 2 files changed:
  `docs/DVM-CRB.md` (+40/-11) and
  `docs/reviews/2026-09-02-review-agent-008-dgw-core-crb-spec-fixes.md`
  (new file, +299/-0). The REVIEW-008 report file is the prior review
  record being appended to `docs/reviews/` per Section 7 of
  `Mandatory-Agent-Review-Rule.md`; this is a documentation/records
  addition, not a behavior change.
- `git diff HEAD~1 HEAD --name-only | grep -vE
  '^(docs/DVM-CRB\.md|docs/reviews/)'` returned zero matches — no files
  outside the expected set (no source code, Makefile, test fixture, or
  non-spec-doc file) are in the diff.
- The `docs/DVM-CRB.md` diff content (40 added, 11 removed) is exactly:
  (1) the 2 arithmetic corrections in Section 11.4
  (`MUL_I32_CHECKED` 0x0221 → 0x0229;
  `DIV_S_I64_CHECKED` 0x022D → 0x023D); (2) the 13 new `format:` lines
  added to the 13 previously-missing opcodes; (3) prose expansions
  attached to 9 of the 13 newly-fixed opcodes (BR_NULL, BR_NONNULL,
  RET_VOID, TAIL_CALL_DIRECT, IDX_GET, IDX_SET, OBJ_IS_INSTANCE,
  SUSPEND_YIELD, SUSPEND_AWAIT), each explaining which operand slots
  carry which semantic role; (4) one new `operand: suspension_site_id`
  line added to SUSPEND_AWAIT (21.2), pairing it with the existing
  `operand: suspension_site_id` line of SUSPEND_YIELD (21.1) and
  formalizing the name of the 32-bit immediate slot of the new
  `format: R_IMM32` choice for that opcode. No other content in
  `docs/DVM-CRB.md` was modified.
- The new `operand: suspension_site_id` line at SUSPEND_AWAIT is a
  minor consistency enhancement: SUSPEND_YIELD (21.1) already had this
  exact `operand:` line in the pre-fix spec, so adding the matching
  line to its paired sibling SUSPEND_AWAIT (21.2) is part of
  formalizing the new `format: R_IMM32` choice — without the
  `operand:` line, the new `format:` line would not name the 32-bit
  immediate slot, leaving an implementer without an operand name for
  the slot. This is directly within the task's "prose expansions
  explaining the new format choices" category.

**Reasoning:** The diff is exactly the 2 arithmetic corrections, the
13 new `format:` lines, prose expansions explaining the new format
choices (9 paragraphs naming which register slots carry which
semantic role), and one minor `operand:` line that pairs SUSPEND_AWAIT
with its sibling SUSPEND_YIELD. The REVIEW-008 report file under
`docs/reviews/` is the standard records-directory addition per
Section 7 of the Review Rule. No source code, Makefile, test fixture,
or other behavior-affecting file was touched. Check 8 PASSES.

---

## 3. Verifier Run Log

### Build (with sanitizers)

```
$ cd /home/z/my-project/dgw-core-repo/compiler/dgw-core
$ make clean
rm -rf build bin
$ make SAN=1 -j$(nproc) 2>&1 | tail -3
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/weaver.cpp -o build/weaver.o
ar rcs build/libdgwcore.a build/control.o build/graph.o build/pass_cleanup.o build/pass_dce.o build/pass_gvn.o build/pass_licm.o build/scheduler.o build/signatures.o build/verifier.o build/weaver.o
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -Iinclude tests/smoke.cpp -Lbuild -ldgwcore -o bin/dgw_smoke -fsanitize=address,undefined -fno-omit-frame-pointer
EXIT=0
```

Build exit code 0. Zero warnings, zero errors (verified by piping the
full build output through `grep -cE 'warning:|error:'` — count = 0).
ASan + UBSan compile-and-link clean. 10 `.o` files + `libdgwcore.a` +
`bin/dgw_smoke` produced.

### Smoke test

```
$ ./bin/dgw_smoke 2>&1 | tail -5
Test 6 CFG: 1 block(s)
MachineCFG: 1 block(s), entry=block#0
  Block #0: 1 phi(s), 2 op(s), preds=1, succs=1
    PHI (from JOIN = node #6, 2 incomings)
    START (node #0)
    BRANCH (node #3)
  JOIN PHI: 2 incomings, 2 block_ids
Test 6: JOIN PHI has >= 2 incomings (correct)

== DGW-Core smoke test PASSED ==
EXIT=0
```

Smoke test exit code 0. All 8 test groups green. Pre/post-opt
WebVerifier 11/0/0. GVN eliminated=1 visited=7. DCE killed=1 live=11.
Cleanup collapsed=0 killed=0. Scheduler Test 1: 4 blocks. Test 2:
route_except_to ok (edge id=3), 5 nodes, 6 edges. Test 3: 2 blocks.
Test 4/4b: visited=1 hoisted=0. Test 5/5b: single-failure-to-trap ok
+ non-trap consumer flagged. Test 6: JOIN PHI 2 incomings, 2
block_ids. Numerical results byte-for-byte identical to REVIEW-008's
recorded baseline (which in turn matched REVIEW-007's and
REVIEW-006's).

### Build verification conclusion

The spec-only fix commit touches only `docs/DVM-CRB.md` and the
REVIEW-008 report file under `docs/reviews/`; no C++ source, Makefile,
or test fixture was modified. Build is clean under `make SAN=1` and
the smoke test passes with exit code 0. Numerical results are
unchanged from REVIEW-008's recorded baseline.

---

## 4. Final Review Status

**APPROVED**

8 PASS, 0 FAIL (of 8 checks).

### All 8 checks PASS

- Check 1 (Section 11.4 arithmetic): PASS — all 9 canonical examples
  now match the encoding formula (the 2 prior arithmetic errors are
  corrected).
- Check 2 (13 previously-missing `format:` lines): PASS — all 13
  opcodes now have `format:` lines.
- Check 3 (Section 7 coverage of `format:` references): PASS — all
  12 uppercase format names resolve to Section 7 definitions; the
  lowercase `none` placeholder follows the same pre-existing convention
  as REVIEW-008 Check 1.
- Check 4 (no opcodes with `opcode:` but no `format:`): PASS — the
  scan returns zero matches (excluding Section 7.x format definitions).
- Check 5 (semantic reasonableness of format choices): PASS — all 13
  new format choices match the task's expected mapping and are
  consistent with sibling opcodes.
- Check 6 (build clean under `make SAN=1`): PASS — zero warnings,
  zero errors.
- Check 7 (smoke test passes, numerical results unchanged): PASS —
  exit code 0, all 8 groups green, identical to REVIEW-008's baseline.
- Check 8 (no behavior change): PASS — diff is exactly the 2
  arithmetic corrections, the 13 new `format:` lines, prose
  expansions, and one minor `operand:` line pairing SUSPEND_AWAIT with
  SUSPEND_YIELD. No source/Makefile/fixture touched.

### Note on the SUSPEND_AWAIT `operand:` addition

Check 8's evidence notes one small additional change beyond the
literal three categories enumerated in the task: a new `operand:
suspension_site_id` line at SUSPEND_AWAIT (21.2). I consider this in
scope for two reasons: (1) it is documentation-only (no behavior, no
opcode-value, no semantic change), and (2) it pairs SUSPEND_AWAIT
with its sibling SUSPEND_YIELD (21.1), which already had this exact
`operand:` line in the pre-fix spec — making the two paired
suspension opcodes symmetric. Without this line, the new `format:
R_IMM32` for SUSPEND_AWAIT would not name the 32-bit immediate slot,
leaving an implementer without an operand name. This is a minor
consistency enhancement, not a behavior change. I note it explicitly
for transparency; if the producer prefers a stricter interpretation,
the line could be removed without affecting any other check.

---

## 5. Reviewer Agent ID

`review-agent-009`

## 6. UTC Timestamp

`2026-09-01T22:46:48Z`
