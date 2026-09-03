# Spec Compliance Review — REVIEW-008

**Task ID:** REVIEW-008
**Agent:** review-agent-008
**Output under review:** commit `cdb34cb` — "docs/dvm-crb: address REVIEW-007 CHANGES_REQUESTED (CRB spec completeness)"; 3 files changed: `docs/DVM-CRB.md` (+216), `README.md` (+11/-3), `docs/reviews/2026-09-02-review-agent-007-dgw-core-crb-crpea-specs.md` (new, 357 lines)
**Spec corpus read in full:** `Mandatory-Agent-Review-Rule.md`, `DGW-Core-IR.md` (Part 3.4 in particular), `DVM-CRB.md`, `README.md`, REVIEW-007 report
**UTC timestamp:** 2026-09-01T22:31:59Z

---

## 1. Prior Review (REVIEW-007) Summary

REVIEW-007 (review-agent-007) audited commit `c6453b9`, which introduced the
DVM-CRB (Common Register Bytecode) and DVM-CR-PEA (Cross-Region Partial Escape
Analysis) specs, and returned CHANGES_REQUESTED with 15 PASS + 2 FAIL out of 17
checks. The 2 FAILs were both in `docs/DVM-CRB.md`:

- **Check 1 FAIL (internal consistency):** three instruction-format names
  referenced by opcode `format:` lines — `R`, `R_R_SITE16`, `R_R_IMM32` — were
  not defined in Section 7, which defined a closed set of nine canonical
  formats (`R_R_R`, `R_R`, `R_IMM32`, `BRANCH`, `JUMP`, `CALL`, `CALL_INDIRECT`,
  `ACCESS`, `SWITCH`). These were orphan references.
- **Check 3 FAIL (instruction set completeness):** two Section 7 example
  instructions (`add.i64.wrap`, `neg.i64`) lacked formal opcode assignments
  (Section 11 described integer arithmetic generically without specific opcodes);
  `neg.i64` additionally lacked formal semantics; and roughly a dozen opcodes
  across Sections 9, 16, 18, 19, 21, 24 lacked either `format:` references or
  `opcode:` hex assignments.

REVIEW-007 also issued 5 non-blocking recommendations (header-level consistency
in DVM-CRB.md, README subsystem-map qualifier, README glossary `Unknown`
addition, commit-message "Six new passes" wording, CR-PEA class↔sink mapping
table). The producer's fix commit `cdb34cb` claims to "Resolve the 2 FAILs
from REVIEW-007" and to apply 3 of the 5 non-blocking recommendations. This
review (REVIEW-008) re-verifies the fix.

Per Section 3.1 of `Mandatory-Agent-Review-Rule.md`, I am an independent
reviewer (not review-agent-001 through -007 and not the producer of commit
`cdb34cb`).

---

## 2. Reviewer's Verdicts on the Fix

### Check 1 — All format names referenced by `format:` lines are defined in Section 7
**Verdict:** PASS
**Evidence:**
- `grep -oE 'format: [A-Z_0-9]+' docs/DVM-CRB.md | sort -u` returns 11 unique names: `ACCESS`, `BRANCH`, `CALL`, `CALL_INDIRECT`, `JUMP`, `R`, `R_IMM32`, `R_R`, `R_R_IMM32`, `R_R_SITE16`, `SWITCH`.
- `grep -nE '^## 7\.[0-9]+ ' docs/DVM-CRB.md` returns 12 Section 7 format definitions: `R_R_R` (7.1), `R_R` (7.2), `R_IMM32` (7.3), `BRANCH` (7.4), `JUMP` (7.5), `CALL` (7.6), `CALL_INDIRECT` (7.7), `ACCESS` (7.8), `SWITCH` (7.9), `R` (7.10), `R_R_SITE16` (7.11), `R_R_IMM32` (7.12).
- Cross-tabulating the two sets: every one of the 11 format names referenced by `format:` lines has a matching `## 7.N \`<NAME>\`` definition in Section 7. The only Section 7 format not referenced by any `format:` line is `R_R_R` (7.1), which is referenced indirectly by Section 11.4 ("All integer-arithmetic opcodes use the `R_R_R` format (§7.1) for binary operations") — that is a prose reference, not a `format:` line, and it correctly points at Section 7.1.

**Reasoning:** All three previously-undefined formats (`R`, `R_R_SITE16`, `R_R_IMM32`) flagged in REVIEW-007 Check 1 are now defined in Section 7.10, 7.11, and 7.12 respectively, and all 11 format names used by `format:` lines now have matching Section 7 definitions. Check 1's FAIL is resolved.

---

### Check 2 — The three previously-undefined formats are now properly defined
**Verdict:** PASS
**Evidence:**
- Section 7.10 `R` (docs/DVM-CRB.md:591-611): (a) section header `## 7.10 \`R\`` ✓; (b) code block with operand layout `opcode | dst | unused | unused` (lines 593-595) ✓; (c) prose description "Single-register format. Used for opcodes that operate on exactly one register: loading sentinel values (`MOV_NULL`, `MOV_TRUE`, `MOV_FALSE`, `MOV_UNDEF`), returning a value (`RET`), throwing an exception (`THROW`)." plus the encode/decode requirement for the two `unused` slots (lines 597-603) ✓; (d) three canonical examples `mov_null r3`, `ret r8`, `throw r2` (lines 607-611) ✓.
- Section 7.11 `R_R_SITE16` (lines 615-630): (a) header ✓; (b) code block `opcode | dst | src | site16` ✓; (c) prose description "Two-register plus 16-bit site-descriptor format. Used for raw memory operations that carry an access-site descriptor for runtime checks, tracing, and bound recording. The `site16` field is an index into the module's Site Table (see §17)." ✓; (d) example `load_mem r3, r1, site(7)    ; r3 := *r1 at site #7` ✓.
- Section 7.12 `R_R_IMM32` (lines 634-650): (a) header ✓; (b) code block `opcode | dst | src | imm32_low | imm32_high` ✓; (c) prose description "Two-register plus 32-bit immediate format. Used for opcodes that take two register operands plus a 32-bit immediate payload (e.g., a `type_id` or `class_id` for checked casts). The 32-bit immediate is split across two 16-bit slots in the same little-endian convention as `R_IMM32` (§7.3) and `BRANCH` (§7.4)." ✓; (d) example `obj_cast_checked r3, r1, type(0x42)   ; r3 := cast r1 to type 0x42` ✓.

**Reasoning:** All three new format definitions satisfy the four required elements (header, layout code block, prose description, canonical example). The prose for 7.11 explicitly cross-references §17 (Site Table), and the prose for 7.12 explicitly cross-references §7.3 (R_IMM32) and §7.4 (BRANCH), so the new definitions are consistent with the existing spec's little-endian immediate-splitting convention.

---

### Check 3 — All integer-arithmetic opcodes have formal opcode assignments
**Verdict:** FAIL
**Evidence:**
- Section 11.4 exists (docs/DVM-CRB.md:953-1024). (a) The encoding formula `opcode = 0x0200 + (variant_index * 0x10) + (width_index * 0x04) + (overflow_index * 0x01)` is present (lines 957-963) and matches the formula required by the task verbatim. ✓
- (b) The variant/width/mode index tables are present (lines 967-984): 15 variants (`0:add … 14:shr_u`), 4 widths (`0:i8 … 3:i64`), 4 overflow modes (`0:.wrap … 3:.sat`). ✓
- (c) 9 canonical examples are listed (lines 988-998): ADD_I64_WRAP (0x020C), ADD_I64_CHECKED (0x020D), SUB_I32_WRAP (0x0218), MUL_I32_CHECKED (0x0221), DIV_S_I64_CHECKED (0x022D), NEG_I64_WRAP (0x027C), NEG_I64_CHECKED (0x027D), NOT_I64_WRAP (0x028C), SHR_U_I64_WRAP (0x02EC). All 9 values lie within `0x0200-0x03FF` ✓ (count ≥ 5 ✓).
- (d) Arithmetic verification — recomputed all 9 examples with `python3`:
  - `ADD_I64_WRAP      = 0x0200 + 0 + 0xC + 0 = 0x020C` ✓ (spec matches)
  - `ADD_I64_CHECKED   = 0x0200 + 0 + 0xC + 1 = 0x020D` ✓
  - `SUB_I32_WRAP      = 0x0200 + 0x10 + 0x08 + 0 = 0x0218` ✓
  - `MUL_I32_CHECKED   = 0x0200 + 0x20 + 0x08 + 1 = 0x0229` ✗ — **spec says 0x0221, but the formula yields 0x0229** (the value 0x0221 would correspond to `MUL_I8_CHECKED` with width_index=0, not width_index=2 as the label "I32" requires).
  - `DIV_S_I64_CHECKED = 0x0200 + 0x30 + 0x0C + 1 = 0x023D` ✗ — **spec says 0x022D, but the formula yields 0x023D** (the value 0x022D would correspond to `MUL_I64_CHECKED` with variant_index=2 (mul), not variant_index=3 (div_s) as the label requires).
  - `NEG_I64_WRAP      = 0x0200 + 0x70 + 0xC + 0 = 0x027C` ✓
  - `NEG_I64_CHECKED   = 0x0200 + 0x70 + 0xC + 1 = 0x027D` ✓
  - `NOT_I64_WRAP      = 0x0200 + 0x80 + 0xC + 0 = 0x028C` ✓
  - `SHR_U_I64_WRAP    = 0x0200 + 0xE0 + 0xC + 0 = 0x02EC` ✓
- The Range check subsection's claim `0x0200 + (14 * 0x10) + (3 * 0x04) + 3 = 0x02EF` was independently recomputed and is correct ✓; `15 × 4 × 4 = 240` ✓; `0x0200-0x03FF = 512 slots` ✓; `0x02F0-0x03FF = 272 reserved slots` ✓.

**Reasoning:** Section 11.4 is structurally complete: the encoding formula matches the task's required formula, the variant/width/mode index tables are present, and the 9 canonical examples all lie within `0x0200-0x03FF`. However, recomputing all 9 canonical examples reveals that **two of them have arithmetic errors**: `MUL_I32_CHECKED` is labeled with `0x0221` but the formula yields `0x0229`, and `DIV_S_I64_CHECKED` is labeled with `0x022D` but the formula yields `0x023D`. The wrong value `0x0221` corresponds to `MUL_I8_CHECKED` (width=0=i8), not `MUL_I32_CHECKED` (width=2=i32); the wrong value `0x022D` corresponds to `MUL_I64_CHECKED` (variant=2=mul), not `DIV_S_I64_CHECKED` (variant=3=div_s). Either the producer mislabeled two opcodes or made two arithmetic typos; either way, the spec's canonical examples are internally inconsistent with the spec's own encoding formula. The task requires that "the arithmetic of the canonical examples is correct"; it is not correct for 2 of the 9 examples. Check 3 FAILS.

---

### Check 4 — All previously-missing `format:` and `opcode:` lines are now present
**Verdict:** FAIL
**Evidence:**
- Re-running an awk script (mirroring REVIEW-007's approach) over `docs/DVM-CRB.md` to find opcode subsections (## N.M with backticked name) that have an `opcode:` line but no `format:` line, the following 13 opcodes still lack `format:` lines:
  - 9.2 `TRAP` (0x0001)
  - 9.3 `UNREACHABLE` (0x0002)
  - 9.4 `SAFEPOINT` (0x0003)
  - 9.5 `PROFILE_POINT` (0x0004)
  - 15.4 `BR_NULL` (0x0703)
  - 15.5 `BR_NONNULL` (0x0704)
  - 16.2 `RET_VOID` (0x0801)
  - 16.9 `TAIL_CALL_DIRECT` (0x0820)
  - 19.5 `IDX_GET` (0x0A10)
  - 19.6 `IDX_SET` (0x0A11)
  - 19.8 `OBJ_IS_INSTANCE` (0x0A21)
  - 21.1 `SUSPEND_YIELD` (0x0D00)
  - 21.2 `SUSPEND_AWAIT` (0x0D01)
- REVIEW-007 Check 3 explicitly named 10 opcodes as lacking `format:` lines: `BR_NULL` (15.4), `BR_NONNULL` (15.5), `RET_VOID` (16.2), `TAIL_CALL_DIRECT` (16.9), `TAIL_CALL_INDIRECT` (16.10), `STORE_MEM` (18.2), `IDX_GET` (19.5), `IDX_SET` (19.6), `OBJ_IS_INSTANCE` (19.8), `CLOSURE_NEW` (19.9). REVIEW-007 also flagged all four Section 24 opcodes (`TRACE_PROMOTE_HINT`, `TRACE_VIRTUAL_HINT`, `DEBUG_BREAKPOINT`, `MONITOR_EVENT`) as lacking both `opcode:` and `format:`.
- The fix addressed 3 of the 10 missing-`format:` opcodes (`TAIL_CALL_INDIRECT` → `format: CALL_INDIRECT` ✓, `STORE_MEM` → `format: R_R_SITE16` ✓, `CLOSURE_NEW` → `format: R_R_IMM32` ✓), plus all 4 Section 24 opcodes (now each has both `opcode:` and `format: R_IMM32` ✓), plus `DEBUG_PROBE` (now `format: R_IMM32` ✓) and `SUSPEND_CLOSE` (now `format: R` ✓) as enhancements.
- The 7 REVIEW-007-flagged opcodes that are still missing `format:` lines: **`BR_NULL` (15.4), `BR_NONNULL` (15.5), `RET_VOID` (16.2), `TAIL_CALL_DIRECT` (16.9), `IDX_GET` (19.5), `IDX_SET` (19.6), `OBJ_IS_INSTANCE` (19.8)`**. Direct verification by Read of each subsection confirms each has only an `opcode:` line and prose, with no `format:` line.

**Reasoning:** The Check 4 task description's explicit verification scope (the 6 opcodes DEBUG_PROBE, TAIL_CALL_INDIRECT, STORE_MEM, CLOSURE_NEW, SUSPEND_CLOSE, MONITOR_EVENT plus the 4 Section 24 opcodes) is fully satisfied — all 10 of those opcodes now have the required `format:` lines, and all 4 Section 24 opcodes have both `opcode:` and `format:` lines. However, the broader Check 4 title claim — "**All** previously-missing `format:` and `opcode:` lines are now present" — is FALSE: 7 of the 10 opcodes that REVIEW-007 Check 3 explicitly flagged as lacking `format:` lines (`BR_NULL`, `BR_NONNULL`, `RET_VOID`, `TAIL_CALL_DIRECT`, `IDX_GET`, `IDX_SET`, `OBJ_IS_INSTANCE`) are still missing their `format:` lines after the fix. The producer's commit message says "Resolves the 2 FAILs from REVIEW-007" but REVIEW-007 Check 3's FAIL is only partially resolved: the integer-arithmetic opcode-assignment portion is addressed (modulo the arithmetic errors in Check 3), and the Section 24 opcodes are addressed, but 7 of the 10 individually-named "no `format:` line" opcodes remain unfixed. Per the task's overall instruction ("Be willing to FAIL if … any opcode is still missing `format:` or `opcode:`"), Check 4 FAILS.

---

### Check 5 — No new orphan format references
**Verdict:** PASS
**Evidence:**
- `grep -oE 'format: [A-Z_0-9]+' docs/DVM-CRB.md | sort -u` returns the 11-name set: `ACCESS`, `BRANCH`, `CALL`, `CALL_INDIRECT`, `JUMP`, `R`, `R_IMM32`, `R_R`, `R_R_IMM32`, `R_R_SITE16`, `SWITCH`.
- Each of these 11 names has a matching `## 7.N \`<NAME>\`` definition (see Check 1 evidence for the 12 Section 7 definitions; the 12th, `R_R_R`, is referenced in prose in Section 11.4 not via a `format:` line).
- No `format:` line references any name not in Section 7.

**Reasoning:** After the fix, every `format:` reference resolves to a Section 7 definition. The three previously-orphan names (`R`, `R_R_SITE16`, `R_R_IMM32`) now resolve to Sections 7.10, 7.11, 7.12 respectively. No new orphan format references were introduced by the fix.

---

### Check 6 — No new contradictions with DGW-Core-IR.md Part 3.4
**Verdict:** PASS
**Evidence:**
- DGW-Core-IR.md Part 3.4 (lines 172-189) defines the IR-level `MATERIALIZE` node: "If EA proves `Region R1` has `EscapeState::NO_ESCAPE`, the optimizer deletes the `ALLOC` node and marks the region as `VIRTUAL` … the optimizer splices in a `MATERIALIZE` node on *only that control path*" with the inline diagram `[MATERIALIZE] (Consumes Virtual R1, outputs real Heap Pointer)`.
- `git diff HEAD~1 HEAD -- docs/DVM-CRB.md | grep -iE 'MATERIALIZE|PEA|escape'` returns zero matches — the fix diff does not mention MATERIALIZE, PEA, or escape analysis at all. The fix is purely bytecode-level: instruction-format definitions (Section 7.10-7.12), opcode assignments (Section 11.4), and `format:`/`opcode:` line additions to existing opcode subsections.
- REVIEW-007's Checks 9, 10, 11 (CR-PEA's reference to Part 3.4 accurate, CR-PEA's MATERIALIZE consistent with DGW-Core's, CRB's Tier 0 role consistent with Hybrid Tracing) all PASSED, and the fix commit touches neither `docs/DVM-CR-PEA.md` nor any cross-references between the CRB spec and the IR-level MATERIALIZE definition.

**Reasoning:** The CRB spec fix is strictly bytecode-level and does not touch the IR-level MATERIALIZE definition in Part 3.4 or any cross-spec reference to it. The fix cannot have introduced a new contradiction because it does not modify any of the relevant text. No regression of REVIEW-007's PASS verdicts on cross-spec consistency.

---

### Check 7 — Header level consistency in DVM-CRB.md
**Verdict:** PASS
**Evidence:**
- `grep -nE '^# [0-9]+\.' docs/DVM-CRB.md` returns 39 level-1 numbered section headers — sections 1 through 39, all present, all using `#`. Sections 1 (`# 1. Purpose of CRB` at line 20) and 2 (`# 2. Core Design Principles` at line 40) are now at `#` level (previously at `##` per REVIEW-007 Check 17 note).
- `grep -nE '^## [0-9]+\.[0-9]' docs/DVM-CRB.md` returns 91 level-2 numbered subsection headers — all in the `## N.M` form. Includes `## 2.1` through `## 2.6` (previously `### 2.1` through `### 2.6` per REVIEW-007 Check 17 note, now correctly demoted to `##`).
- `grep -nE '^## [0-9]+\. ' docs/DVM-CRB.md` returns 0 matches — no `## N.` (subsection-as-top-level) headers remain.
- `grep -nE '^### [0-9]+\.[0-9]+' docs/DVM-CRB.md` returns 0 matches — no `### N.M` (numbered sub-subsection) headers remain.
- `grep -nE '^### ' docs/DVM-CRB.md` returns 16 level-3 named headers — all are non-numbered names: `### Header flags`, `### Section types`, `### Constant kinds`, `### \`.wrap\``, `### \`.checked\``, `### \`.assume\``, `### \`.sat\``, ``### `ADD_I64_WRAP` ``, ``### `ADD_I64_CHECKED` ``, ``### `DIV_I64_CHECKED` ``, `### Canonical examples`, `### Format`, `### Range check`, `### \`.strict\``, `### \`.fast\``. The named-example backticked headers (``### `ADD_I64_WRAP` ``, etc.) are the case explicitly permitted by the task ("except for named examples like `### \`ADD_I64_WRAP\``"). The non-backticked named headers (`### Header flags`, `### Canonical examples`, etc.) are not numbered sub-subsections and were pre-existing in the spec; they are not new inconsistencies introduced by the fix.

**Reasoning:** The fix correctly promoted Sections 1 and 2 from `##` to `#` and demoted Subsections 2.1-2.6 from `###` to `##`, matching the level convention used by Sections 3-39. No `## N.` (subsection-as-top-level) or `### N.M` (numbered sub-subsection) headers remain. The 16 existing level-3 named headers are all non-numbered and were pre-existing; the 3 new level-3 named headers added by the fix (`### Canonical examples`, `### Format`, `### Range check`, all under Section 11.4) follow the same convention. No new header-level inconsistencies were introduced.

---

### Check 8 — README updates are correct
**Verdict:** PASS
**Evidence:**
- (a) Subsystem map diagram (README.md:58-63): the `PEA-X1..X6` line now reads `│   + PEA-X1..X6 (CR-PEA capability gates,│ / │   proposed in DVM-CR-PEA.md, pending    │ / │   amendment after review)               │` — qualified with "(proposed in DVM-CR-PEA.md, pending amendment after review)". ✓
- (b) Escape Lattice glossary entry (README.md:127-135): the value list now starts with ``(`Unknown`, `NonEscape`, `LocalEscape`, `` — `Unknown` is the first value. ✓
- (c) Same glossary entry's sentinel description (README.md:133-135): "`Unknown` is the start / unevaluated state; `NonEscape` is the empty set (least escaping, most virtualizable); `BottomEscape` is the universal set (most escaping, least virtualizable)." — describes `Unknown`, `NonEscape`, and `BottomEscape` as the lattice sentinels. ✓
- Cross-referenced against `docs/DVM-CR-PEA.md` lines 153-171 (EscapeClass enum): the 17 values listed there are `Unknown, NonEscape, LocalEscape, CallerEscape, ArgEscape, ReturnEscape, StoreEscape, GlobalEscape, NativeEscape, IdentityEscape, MonitorEscape, WeakRefEscape, FinalizerEscape, IntrospectionEscape, SuspensionEscape, ThrowableEscape, BottomEscape` — the README's 17-value list now matches the spec's 17-value list in order and content. ✓

**Reasoning:** All three README updates required by Check 8 are present and correct. The subsystem map diagram's qualifier is verbatim as specified. The glossary entry now lists `Unknown` first (matching the spec's enum order) and describes all three lattice sentinels (`Unknown` = start, `NonEscape` = empty set, `BottomEscape` = universal set). The README's 17-value list now matches the CR-PEA spec's 17-value enum.

---

### Check 9 — Build is still clean
**Verdict:** PASS
**Evidence:**
- `cd /home/z/my-project/dgw-core-repo/compiler/dgw-core && make clean && make SAN=1 -j$(nproc)` exited 0.
- Piping the full build output through `grep -iE 'warning:|error:'` returned zero matches — zero warnings, zero errors.
- Build produced 10 `.o` files, `libdgwcore.a`, and `bin/dgw_smoke`.
- ASan + UBSan compile-and-link clean (`-fsanitize=address,undefined -fno-omit-frame-pointer`).

**Reasoning:** The build is clean under `make SAN=1 -j$(nproc)` with zero warnings and zero errors, matching REVIEW-007's clean-build baseline. The spec-only fix commit does not touch any C++ source, so the build behavior is unchanged.

---

### Check 10 — Smoke test still passes
**Verdict:** PASS
**Evidence:**
- `./bin/dgw_smoke` exited 0 with `== DGW-Core smoke test PASSED ==`.
- All 8 test groups green:
  - Pre-opt WebVerifier: 11 PASS / 0 FAIL / 0 N/A
  - Post-opt WebVerifier: 11 PASS / 0 FAIL / 0 N/A
  - GVN: eliminated=1, visited=7
  - DCE: killed=1, live=11
  - Cleanup: collapsed=0, killed=0
  - Scheduler Test 1: 4 blocks (START+GUARD+RETURN in #0, LOAD in #1, STORE in #2, DEOPT_TRAP in #3)
  - Test 2 (CALL+HANDLER): route_except_to ok (edge id=3), 5 nodes, 6 edges
  - Test 3 (BRANCH+PGO): 2 blocks
  - Test 4 (STATE+LICM): visited=1, hoisted=0
  - Test 4b (STATE+pure invariant ADD): visited=1, hoisted=0
  - Test 5/5b: single-failure-to-trap ok + non-trap consumer flagged
  - Test 6 (JOIN→PHI): 1 block, JOIN PHI 2 incomings, 2 block_ids
- Numerical results byte-for-byte identical to REVIEW-007's recorded output (which in turn matched REVIEW-006's recorded output).

**Reasoning:** The smoke test passes with all 8 groups green and numerical results unchanged. The spec-only fix commit does not touch any test fixtures or runtime code, so smoke-test behavior is preserved exactly.

---

### Check 11 — No behavior change
**Verdict:** PASS
**Evidence:**
- `git diff HEAD~1 HEAD --stat` shows exactly 3 files changed: `README.md` (+11/-3), `docs/DVM-CRB.md` (+216/-0 net; pure additions to existing sections), `docs/reviews/2026-09-02-review-agent-007-dgw-core-crb-crpea-specs.md` (new file, 357 lines).
- `git diff HEAD~1 HEAD --name-only | grep -vE '^(README\.md|docs/DVM-CRB\.md|docs/reviews/)'` returned zero matches — no files outside the expected set are in the diff.
- The `docs/DVM-CRB.md` diff content is exactly: (1) promotion of `## 1.` → `# 1.` and `## 2.` → `# 2.`; (2) demotion of `### 2.1` through `### 2.6` to `## 2.1` through `## 2.6`; (3) insertion of Section 7.10 (`R`), 7.11 (`R_R_SITE16`), 7.12 (`R_R_IMM32`); (4) insertion of Section 11.4 (Opcode Assignments with encoding formula, index tables, 9 canonical examples, format subsection, range check subsection); (5) addition of `format: R_IMM32` to `DEBUG_PROBE` (9.6); (6) addition of `format: CALL_INDIRECT` to `TAIL_CALL_INDIRECT` (16.10) with prose; (7) addition of `format: R_R_SITE16` to `STORE_MEM` (18.2) with prose; (8) addition of `format: R_R_IMM32` to `CLOSURE_NEW` (19.9) with prose; (9) addition of `format: R` to `SUSPEND_CLOSE` (21.3) with prose; (10) insertion of `opcode: 0x1000` + `format: R_IMM32` + `operand: hint_id32` + prose to `TRACE_PROMOTE_HINT` (24.1); (11) same pattern for `TRACE_VIRTUAL_HINT` (24.2), `DEBUG_BREAKPOINT` (24.3), `MONITOR_EVENT` (24.4); (12) one-line note added to Section 24 intro about the `0x1000-0x10FF` range.
- The `README.md` diff content is exactly: (1) the subsystem-map diagram PEA-X1..X6 qualifier (3-line wrap); (2) the Escape Lattice glossary `Unknown` insertion and the lattice-sentinels description.
- The `docs/reviews/...` addition is the REVIEW-007 report file (which the task explicitly allows: "the REVIEW-007 report file added under `docs/reviews/`").
- No source code, Makefile, test fixture, or non-spec-doc file was touched.

**Reasoning:** The diff contains exactly the changes the task enumerates: the new Section 7.10/7.11/7.12, the new Section 11.4, the format/opcode additions to the previously-incomplete opcodes, the header-level promotions/demotions in Sections 1/2, the README subsystem map qualifier, the README glossary `Unknown` addition, and the REVIEW-007 report file added under `docs/reviews/`. No other files were touched and no other content within the touched files was modified.

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

Build exit code 0. Zero warnings, zero errors (verified by piping the full build output through `grep -iE 'warning:|error:'` — zero matches). ASan + UBSan compile-and-link clean. 10 `.o` files + `libdgwcore.a` + `bin/dgw_smoke` produced.

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

Smoke test exit code 0. All 8 test groups green. Pre/post-opt WebVerifier 11/0/0. GVN eliminated=1 visited=7. DCE killed=1 live=11. Cleanup collapsed=0 killed=0. Scheduler Test 1: 4 blocks. Test 2: route_except_to ok (edge id=3). Test 3: 2 blocks. Test 4/4b: visited=1 hoisted=0. Test 5/5b: single-failure-to-trap ok + non-trap consumer flagged. Test 6: JOIN PHI 2 incomings, 2 block_ids. Numerical results byte-for-byte identical to REVIEW-007's recorded output (which in turn matched REVIEW-006's).

### Build verification conclusion

The spec-only fix commit does not touch any C++ source, Makefile, or test fixture. Build is clean under `make SAN=1` and the smoke test passes with exit code 0. Numerical results are unchanged from REVIEW-007's recorded baseline.

---

## 4. Final Review Status

**CHANGES_REQUESTED**

9 PASS, 2 FAIL (of 11 checks).

### Failed checks (must be addressed in a new commit)

- **Check 3 (Section 11.4 arithmetic) — FAIL.** Two of the 9 canonical examples in Section 11.4 have arithmetic that does not match the spec's own encoding formula `opcode = 0x0200 + (variant_index * 0x10) + (width_index * 0x04) + (overflow_index * 0x01)`:
  - `MUL_I32_CHECKED`: spec says `= 0x0221`, but `0x0200 + (2*0x10) + (2*0x04) + 1 = 0x0229`. The value `0x0221` would correspond to `MUL_I8_CHECKED` (width=0=i8), not `MUL_I32_CHECKED` (width=2=i32). The producer must either correct the value to `0x0229` or correct the label to `MUL_I8_CHECKED` (and likely add a separate `MUL_I32_CHECKED` example with the value `0x0229`).
  - `DIV_S_I64_CHECKED`: spec says `= 0x022D`, but `0x0200 + (3*0x10) + (3*0x04) + 1 = 0x023D`. The value `0x022D` would correspond to `MUL_I64_CHECKED` (variant=2=mul), not `DIV_S_I64_CHECKED` (variant=3=div_s). The producer must either correct the value to `0x023D` or correct the label to `MUL_I64_CHECKED` (and likely add a separate `DIV_S_I64_CHECKED` example with the value `0x023D`).

- **Check 4 (previously-missing `format:` lines) — FAIL.** 7 of the 10 opcodes that REVIEW-007 Check 3 explicitly named as lacking `format:` lines still lack `format:` lines after the fix:
  - `BR_NULL` (15.4, opcode 0x0703)
  - `BR_NONNULL` (15.5, opcode 0x0704)
  - `RET_VOID` (16.2, opcode 0x0801)
  - `TAIL_CALL_DIRECT` (16.9, opcode 0x0820)
  - `IDX_GET` (19.5, opcode 0x0A10)
  - `IDX_SET` (19.6, opcode 0x0A11)
  - `OBJ_IS_INSTANCE` (19.8, opcode 0x0A21)

  The producer's commit message claims to "Resolve the 2 FAILs from REVIEW-007" but REVIEW-007 Check 3's FAIL is only partially resolved. The 7 listed opcodes each need a `format:` line added. Suggested mappings (for the producer's consideration): `BR_NULL` and `BR_NONNULL` should use `format: BRANCH` (one register operand + branch target, same as `BR_TRUE`/`BR_FALSE`); `RET_VOID` should use `format: none` or `format: R` with `dst` reserved (no value to return); `TAIL_CALL_DIRECT` should use `format: CALL` (same operand layout as `CALL_DIRECT`); `IDX_GET` and `IDX_SET` should use `format: ACCESS` or a new `R_R_R` variant with index register; `OBJ_IS_INSTANCE` should use `format: R_R_IMM32` (object register + class-id immediate) or `format: ACCESS`. Six additional opcodes that REVIEW-007 did not explicitly flag also still lack `format:` lines: `TRAP` (9.2), `UNREACHABLE` (9.3), `SAFEPOINT` (9.4), `PROFILE_POINT` (9.5), `SUSPEND_YIELD` (21.1), `SUSPEND_AWAIT` (21.2) — the producer should consider adding `format: none` to those that take no operands (matching `NOP`'s `format: none` convention at 9.1) and `format: R_IMM32` to `PROFILE_POINT` (which has `operand: profile_id32`).

### PASSing checks (9)

- Check 1 (Section 7 format coverage of `format:` references): PASS
- Check 2 (Sections 7.10, 7.11, 7.12 properly defined): PASS
- Check 5 (no new orphan format references): PASS
- Check 6 (no new contradictions with DGW-Core Part 3.4): PASS
- Check 7 (header level consistency in DVM-CRB.md): PASS
- Check 8 (README updates correct): PASS
- Check 9 (build clean under `make SAN=1`): PASS
- Check 10 (smoke test passes, numerical results unchanged): PASS
- Check 11 (no behavior change): PASS

### Note on partial-fix scoping

The producer's commit message states a narrower scope than REVIEW-007 Check 3 actually flagged: the producer claims to have added `format:` lines to "DEBUG_PROBE, TAIL_CALL_INDIRECT, STORE_MEM, CLOSURE_NEW, SUSPEND_CLOSE, and all four Section 24 opcodes" (9 opcodes total), but REVIEW-007 Check 3 explicitly named 10 missing-`format:` opcodes plus 4 missing-opcode-and-format Section 24 opcodes (14 opcodes total). The Check 4 task description's explicit verification list (6 + 4 Section 24 opcodes) matches the producer's stated scope exactly, and those 10 opcodes are all correctly fixed; but the broader Check 4 title claim ("All previously-missing `format:` and `opcode:` lines are now present") is not satisfied. Per the task's overall instruction ("Be willing to FAIL if … any opcode is still missing `format:` or `opcode:`"), Check 4 is recorded as FAIL.

---

## 5. Reviewer Agent ID

`review-agent-008`

## 6. UTC Timestamp

`2026-09-01T22:31:59Z`
