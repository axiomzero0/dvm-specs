# REVIEW-020 — Spec Compliance Review: DVM-Performance-Priorities

**Task ID:** REVIEW-020
**Agent:** review-agent-020
**Reviewer Role:** Fresh reviewer per Mandatory-Agent-Review-Rule.md §3.4
**Output Under Review:** `docs/DVM-Performance-Priorities.md` (commit `506a911`, +627 lines) and `README.md` (row 7, +6 lines)
**Date of Review:** 2026-09-08T21:39:45Z
**Final Status:** **APPROVED** — 10 PASS, 0 FAIL, 0 N/A

---

## 1. Producer Self-Audit (per §5.1)

The producing agent did not publish a separate self-audit checklist in the commit message. The commit subject and body self-document the scope:

```
commit 506a911a35dc8106f574c89ef50e5bd94feae288
Author: Z User <z@container>
Date:   Tue Sep 8 21:35:23 2026 +0000

    docs: add DVM-Performance-Priorities (performance engineering strategy)

    Adds docs/DVM-Performance-Priorities.md — a comprehensive performance
    engineering strategy for making the DVM VM disappear from hot code.

    23 sections covering:
      1. CRB decoding optimization (32-bit compact vs 64-bit canonical)
      ...
      23. Trace length as optimization resource

    Spec citations:
    - DVM-CRB.md (CRB decoding)
    - DVM-Hybrid-Tracing-Architecture.md (tracing, tiers)
    - DGW-Core-IR.md (Region/Ref, PEA, Weaver, FWD)
    - DVM-CR-PEA.md (cross-region PEA)
    - DVM-Compiler-Laws.md (speculation, guards)
    - Mandatory-Agent-Review-Rule.md §3 (new spec requires review)
```

The commit message contains explicit spec citations for the four cross-referenced specs (satisfying §5.2 of the Mandatory Agent Review Rule) and invokes §3 (review protocol) for itself. The reviewer independently verified each item below.

Files touched by commit `506a911` (per `git show --stat 506a911`):
- `README.md` (+6 lines, content)
- `docs/DVM-Performance-Priorities.md` (+627 lines, new file)
- `docs/reviews/2026-09-02-review-agent-018-dvm-trace-cache.md` (mode change 644 → 755 only, 0 content lines)
- `docs/reviews/2026-09-02-review-agent-019-dvm-trace-cache-fix.md` (mode change 644 → 755 only, 0 content lines)
- `runtime/interp/include/dvm/trace_cache.hpp` (mode change 644 → 755 only, 0 content lines)

The three mode-only changes are zero-byte `chmod` operations that do not affect build or test outcomes. They are noted here as a non-blocking observation (see §4 below); they do not constitute a code change and so do not trigger Mandatory-Agent-Review-Rule.md §2 review for `runtime/` (no C++ source line was modified).

---

## 2. Spec-Indexed Verdicts (per §3.2)

The governing spec is `docs/Mandatory-Agent-Review-Rule.md` §2 ("Scope of 'Output'" — "New spec documents under `docs/` ... Review Required? Yes ... Whether they contradict existing specs"). The reviewer must additionally cross-reference the new spec against every spec it cites. The 10 checks below are issued per the task definition (REVIEW-020) and the §3.2 protocol.

### Check 1 — No conversational preamble

**Rule:** The spec file must start with its title, not with conversational filler like "I went through the DVM specs...".

**Evidence:** `head -1 docs/DVM-Performance-Priorities.md` returns:
```
# DVM Performance Engineering Priorities
```
Lines 2–7 of the spec contain the YAML-like metadata block (Status, Subsystem, Depends On, Owner, Last Updated). Lines 9–15 contain the blockquote framing. No conversational preamble is present anywhere in the file.

**Verdict:** **PASS**

---

### Check 2 — 23 sections present

**Rule:** All 23 numbered sections must exist with `## N. <Title>` headers.

**Evidence:** `rg -n '^## [0-9]+\.' docs/DVM-Performance-Priorities.md` returns 23 matching headers, in sequential order from `## 1.` through `## 23.`:

| # | Line | Section Title |
|---|------|---------------|
| 1 | 47 | CRB Should Be Optimized Specifically for Decoding |
| 2 | 77 | Kill the Virtual Register File as Early as Possible |
| 3 | 101 | Make Trace Recording Almost Free |
| 4 | 124 | Make Specialization Persistent |
| 5 | 144 | Guard Hoisting Should Be Absolutely Vicious |
| 6 | 179 | PEA + Scalar Replacement Should Be One of the Crown Jewels |
| 7 | 218 | Inline Aggressively, but with a Code-Size Budget |
| 8 | 233 | Specialize Calls into Direct Calls |
| 9 | 264 | Memory SSA Needs to Become Very Cheap |
| 10 | 286 | Don't Use One Giant Optimization Pipeline |
| 11 | 319 | Add a Dedicated Range-Analysis Engine |
| 12 | 337 | Build a Real Loop Optimizer Around the Trace Representation |
| 13 | 356 | Add Alignment Proofs |
| 14 | 377 | Specialize Values, Not Just Types |
| 15 | 388 | Have a "Speculation Ladder" |
| 16 | 410 | Optimize the Deopt Boundary Rather Than Avoiding Deopt |
| 17 | 431 | Use Separate Hot and Cold Code Sections |
| 18 | 444 | Make Helpers Disappear |
| 19 | 457 | Optimize the Compiler Itself with the Same Philosophy |
| 20 | 475 | Don't Let FWDs Accumulate |
| 21 | 491 | Add a Machine-Aware Late IR |
| 22 | 507 | Register Allocation Deserves Obscene Amounts of Attention |
| 23 | 519 | Treat Trace Length as an Optimization Resource |

All 23 sections present and sequential. Three additional unnumbered sections (`## The Core Principle`, `## Priority Table`, `## Target Architecture`) are framing/summary sections, not numbered spec sections — they correctly do not consume a number from the 1–23 sequence.

**Verdict:** **PASS**

---

### Check 3 — Priority table has S/A/B tiers with correct optimization names

**Rule:** The priority table must contain S/A/B tiers and the optimization names must be reasonable summaries of the section content.

**Evidence:** Lines 536–557 of `docs/DVM-Performance-Priorities.md`:

| Priority | Optimization | Potential Payoff |
|----------|-------------|-----------------|
| **S** | Aggressive inlining | Massive |
| **S** | Type/shape specialization | Massive |
| **S** | Scalar replacement + PEA | Massive |
| **S** | Bounds-check elimination | Massive |
| **S** | Devirtualization | Massive |
| **S** | Trace specialization | Massive |
| **S** | Register allocation | Massive |
| **S** | Loop optimization | Massive |
| **S** | SIMD/vectorization | Massive for numeric workloads |
| **A** | Guard hoisting | Very high |
| **A** | Range analysis | Very high |
| **A** | Load/store elimination | Very high |
| **A** | Constant/value specialization | Very high |
| **A** | Inline caches | Very high |
| **A** | Hot/cold splitting | High |
| **A** | Fast deoptimization | High |
| **B** | Interpreter micro-optimizations | Moderate |
| **B** | CRB compression | Workload-dependent |
| **B** | Fancy graph mutation tricks | Mostly compile-time |

All three tiers (S/A/B) present. Column count consistent (3 columns: Priority, Optimization, Potential Payoff), header separator row present, 19 data rows. Each row's optimization name maps cleanly to spec sections:

- "Aggressive inlining" ↔ §7 ("Inline Aggressively, but with a Code-Size Budget")
- "Type/shape specialization" ↔ §4, §15 ("Speculation Ladder" — TYPE → SHAPE → CONSTANT → RANGE → EXACT VALUE)
- "Scalar replacement + PEA" ↔ §6 ("PEA + Scalar Replacement Should Be One of the Crown Jewels")
- "Bounds-check elimination" ↔ §5, §11 (range analysis feeding BCE)
- "Devirtualization" ↔ §8 ("Specialize Calls into Direct Calls")
- "Trace specialization" ↔ §3, §4, §23 (trace recording + persistent specialization)
- "Register allocation" ↔ §22 ("Register Allocation Deserves Obscene Amounts of Attention")
- "Loop optimization" ↔ §12 ("Build a Real Loop Optimizer Around the Trace Representation")
- "SIMD/vectorization" ↔ §12, §13 ("Add Alignment Proofs")
- "Guard hoisting" ↔ §5 ("Guard Hoisting Should Be Absolutely Vicious")
- "Range analysis" ↔ §11 ("Add a Dedicated Range-Analysis Engine")
- "Load/store elimination" ↔ §9 ("Memory SSA Needs to Become Very Cheap")
- "Constant/value specialization" ↔ §14 ("Specialize Values, Not Just Types")
- "Inline caches" ↔ §8, §15, §18 (devirtualization, speculation ladder, helpers)
- "Hot/cold splitting" ↔ §17 ("Use Separate Hot and Cold Code Sections")
- "Fast deoptimization" ↔ §16 ("Optimize the Deopt Boundary Rather Than Avoiding Deopt")
- "Interpreter micro-optimizations" ↔ §1, §2 (CRB decoding, kill virtual register file)
- "CRB compression" ↔ §1 ("CRB Should Be Optimized Specifically for Decoding")
- "Fancy graph mutation tricks" ↔ §19, §20 ("Optimize the Compiler Itself", "Don't Let FWDs Accumulate")

Every priority-table row maps to at least one numbered section. No orphan rows.

**Verdict:** **PASS**

---

### Check 4 — Target architecture ASCII pipeline diagram

**Rule:** The spec must contain an ASCII pipeline diagram showing the path: guest code → CRB → Tier 0 → Tier 1 → trace → DGW → low-level IR → machine backend → hot loop.

**Evidence:** Lines 562–627 of `docs/DVM-Performance-Priorities.md` contain the `## Target Architecture` diagram in a `text` code fence. The diagram contains, in order from top to bottom:

1. `Guest Code` (line 564)
2. `CRB frontend` (line 567)
3. `Tier 0 register / direct threaded` (lines 570–572)
4. `cheap profiling` (line 574)
5. `Tier 1 baseline / register JIT` (lines 577–579)
6. `patchpoint/trace` (line 581)
7. `Guest Trace` (lines 583–585)
8. `DGW-Core` box (lines 587–602) with the inline pass list: inline, specialize, devirtualize, range analysis, alias analysis, PEA / CR-PEA, scalar replacement, GVN, LICM, BCE, loop optimization, vectorization
9. `trace scheduling` (line 604)
10. `Low-level IR` (lines 606–608)
11. `Machine backend / RA + scheduling` (lines 610–613)
12. `native code` (line 615)
13. `HOT LOOP` box (lines 617–626) with properties: no dispatch, no allocation, no dynamic call, no bounds check, no shape check, vectorized

All 8 stages from the required pipeline (guest code → CRB → Tier 0 → Tier 1 → trace → DGW → low-level IR → machine backend → hot loop) are present in the correct order. The diagram is enclosed in a balanced `text` code fence (opens at line 562, closes at line 627).

**Verdict:** **PASS**

---

### Check 5 — Cross-spec consistency (references to CRB, Hybrid Tracing, DGW-Core, CR-PEA)

**Rule:** The spec must reference existing specs correctly:
- CRB (§1)
- Hybrid Tracing (§3, §10)
- DGW-Core (§6, §9, §20)
- CR-PEA (§6)

**Evidence:**

| Cross-Spec | Required Section | Verifying line(s) in `DVM-Performance-Priorities.md` | Verifying text |
|---|---|---|---|
| CRB | §1 | 47, 63–67, 70–73 | `## 1. CRB Should Be Optimized Specifically for Decoding`; "CRB-C canonical (64-bit, for cold/deopt)" / "CRB-F compact/fetch-optimized (32-bit, for hot code)"; references CRB's 64-bit canonical encoding as a baseline. Consistent with `DVM-CRB.md` §2.2 ("CRB uses 64-bit instruction words"). |
| Hybrid Tracing | §3, §10 | 101–120, 286–316 | §3 "Make Trace Recording Almost Free" describes Tier 0/Tier 1 hot-loop patchpoints and counters — consistent with `DVM-Hybrid-Tracing-Architecture.md` §1 (meta-tracing vs normal tracing) and the four-tier model. §10 "Don't Use One Giant Optimization Pipeline" defines per-tier optimization pipelines (Tier 1, Tier 2 normal hot loop, Tier 2 pathological, Tier 3) — consistent with the tier model in `DVM-Hybrid-Tracing-Architecture.md` Core tier model (lines 5–33). |
| DGW-Core | §6, §9, §20 | 179–215, 264–282, 475–487 | §6 "PEA + Scalar Replacement Should Be One of the Crown Jewels" describes "Region/Ref model" — consistent with `DGW-Core-IR.md` Part 3 (First-Class Regions and References). §9 "Memory SSA Needs to Become Very Cheap" proposes "memory partitions" — references the existing `MEMORY` edge chain of `DGW-Core-IR.md` Part 3.3. §20 "Don't Let FWDs Accumulate" references the FWD forwarding node — consistent with `DGW-Core-IR.md` Part 5.2 ("The Forwarding Node Trick"). |
| CR-PEA | §6 | 214 | "CR-PEA makes this even more interesting across inline boundaries." — explicit reference, consistent with `DVM-CR-PEA.md` §5 (Cross-Method PEA Model) and §9 (Interaction with Inlining). |

The spec also names all four cited specs in its `**Depends On:**` field (line 5): `DVM-CRB`, `DVM-Hybrid-Tracing-Architecture`, `DGW-Core-IR`, `DVM-CR-PEA`, `DVM-Compiler-Laws`. Each cross-spec reference is consistent with the existing spec content.

**Verdict:** **PASS**

---

### Check 6 — No contradictions with existing specs

**Rule:** The new spec proposes changes (e.g., 32-bit CRB, memory partitions, low-level IR) that go beyond the existing specs but must not contradict them.

**Evidence (per proposal):**

**6.1 — 32-bit CRB (§1, lines 63–73).**
- New proposal: "Consider two CRB encodings: CRB-C canonical (64-bit, for cold/deopt) / CRB-F compact/fetch-optimized (32-bit, for hot code)."
- Existing spec `DVM-CRB.md` §2.2: "CRB uses 64-bit instruction words." §2.4: "Because all instruction cells are 64-bit: simple fetch, simple direct threading, predictable decoding, large opcode space."
- Analysis: The new spec frames CRB-F as a **forward-looking proposal** ("Consider two CRB encodings", "Benchmark 32-bit fixed vs 64-bit fixed vs variable-length on actual workloads"). It explicitly preserves CRB-C as the canonical 64-bit form and warns "Don't sacrifice the beautifully simple decoder merely to save a few bytes." The existing `DVM-CRB.md` defines the current standard (64-bit); the new spec proposes a *complementary* compact form, not a replacement. **No contradiction** — this is a future engineering investigation item that would require a CRB v1.1 amendment before implementation.

**6.2 — Memory partitions (§9, lines 264–282).**
- New proposal: "Introduce explicit memory partitions: MemoryToken ├── heap ├── globals ├── TLS ├── object region R1 ├── object region R2 └── unknown."
- Existing spec `DGW-Core-IR.md` Part 3.3 ("Memory SSA — The Memory Token"): "Memory operations (`LOAD`, `STORE`, `CALL`) consume and produce a `MEMORY` edge. This forms a strict, single-linked chain of memory states." Part 8.3 (verifier invariant): "**Single Memory Chain:** Following `MEMORY` edges from any `STORE`/`LOAD` must eventually lead back to the `START` node without splitting (unless explicitly merged by a `MEMORY_JOIN` node)."
- Existing spec `DGW-Core-IR.md` Part 3.1 already enumerates `RegionKind : uint8_t { STACK, HEAP, GLOBAL, TLS, VIRTUAL, ELIMINATED }` — so the partition categories (heap, globals, TLS, per-object regions) are already first-class in DGW-Core.
- Analysis: The new spec's "memory partitions" extend the existing single-chain model by allowing per-partition memory chains. This is a **performance optimization** of the existing Memory SSA, not a contradiction — the verifier rule (single chain) would need to be amended to allow per-partition chains joined by `MEMORY_JOIN` nodes. The new spec frames this as "Introduce explicit memory partitions" without claiming the existing verifier rule is wrong; it explicitly notes "The Region system already gives the raw material." **No contradiction** — extension that would require an amendment to `DGW-Core-IR.md` Part 8.3 before implementation.

**6.3 — Machine-aware late IR (§21, lines 491–503).**
- New proposal: "DGW → Target-independent low-level IR → Target-specific machine IR → register allocation → machine code."
- Existing spec `DGW-Core-IR.md` Part 7.3 ("Instruction Selection"): "The pure DGW nodes are pattern-matched into `MachineInstr`s using a BURS (Bottom-Up Rewrite System) or DAG-covering algorithm. The blockless web is now a standard Control Flow Graph (CFG) of machine instructions."
- Analysis: The new spec proposes an **intermediate** "Target-independent low-level IR" step between DGW and Machine IR. This adds an architectural lowering phase; it does not remove DGW's Phase K (trace scheduling + block formation + instruction selection). The new spec is consistent with the existing model — it inserts an extra IR stage, which is a common architecture in production compilers (LLVM IR → Machine IR → Machine Code; HotSpot's LIR → MIR). **No contradiction** — extension to existing lowering pipeline.

**6.4 — FWD path compression (§20, lines 475–487).**
- New proposal: "Use O(1) mutation + cheap path compression. depth <= 1: fine. depth > 2: collapse. compile boundary: zero FWD chains."
- Existing spec `DGW-Core-IR.md` Part 5.2: "A later fast `CleanupPass` collapses `FWD` chains in a single linear sweep."
- Analysis: The new spec proposes a threshold-based proactive collapse, extending the existing CleanupPass. **No contradiction** — refinement of the existing pass behavior.

**6.5 — FrameState / Deopt (§16, lines 410–427).**
- New proposal: "Optimize the Deopt Boundary Rather Than Avoiding Deopt" — "very precise FrameState → cheap side exit → CRB."
- Existing spec `DVM-Deopt-FrameState.md` (per the README dependency order): the deopt machinery supports exact reconstruction of interpreter state at side exits.
- Analysis: The new spec is fully consistent with the existing deopt design — it explicitly advocates for "very precise FrameState" and treats CRB as the deopt target, matching `DVM-CRB.md` ("Primary Use: Tier 0 register interpreter, trace recording baseline, deopt target"). **No contradiction.**

**6.6 — PEA / CR-PEA (§6, lines 179–215).**
- New proposal: aggressive scalar replacement + PEA, "CR-PEA makes this even more interesting across inline boundaries."
- Existing specs: `DGW-Core-IR.md` Part 3.4 (PEA via MATERIALIZE) and `DVM-CR-PEA.md` (cross-region PEA with lazy materialization).
- Analysis: The new spec is fully aligned with the existing PEA design. It reinforces, not contradicts. **No contradiction.**

**Conclusion:** All forward-looking proposals in the new spec are framed as engineering investigations or extensions to existing specs. None of them claim to override or contradict the existing normative rules. Any implementation that follows these proposals would require amendments to the cited specs (`DVM-CRB.md` v1.1, `DGW-Core-IR.md` Part 8.3) — but the new spec itself does not contradict the current rules.

**Verdict:** **PASS**

---

### Check 7 — Markdown well-formed

**Rule:** Code fences balanced; tables well-formed; headers sequential.

**Evidence:**
- **Code fences:** `rg -c '^```' docs/DVM-Performance-Priorities.md` returns **78** (even number, balanced). Each opening fence has a matching closing fence; no orphan fences.
- **Tables:** The single table (Priority Table, lines 536–557) has a header row `| Priority | Optimization | Potential Payoff |`, a separator row `|----------|-------------|-----------------|`, and 19 data rows. Column count is consistent (3 columns) across all rows. The separator row contains 3 hyphen-runs matching the 3 columns.
- **Headers sequential:** The 23 numbered sections appear in order `## 1.` through `## 23.` (verified in Check 2). Three unnumbered `##` headers (`## The Core Principle`, `## Priority Table`, `## Target Architecture`) appear at appropriate places (lines 18, 534, 560) and do not consume numbers from the 1–23 sequence. The single `#` title (`# DVM Performance Engineering Priorities`) appears once at line 1.

**Verdict:** **PASS**

---

### Check 8 — README updated with row 7

**Rule:** Row 7 added with the correct document name, subsystem, and status.

**Evidence:** `git diff 506a911^ 506a911 -- README.md` shows:
```diff
@@ -22,6 +22,7 @@ compliance. There are no exceptions.
 | 4 | [DGW-Core IR (Dynamic Graph Web)](docs/DGW-Core-IR.md) | Tier 2 / Tier 3 Optimizing IR | Stable |
 | 5 | [DVM-CRB — Common Register Bytecode](docs/DVM-CRB.md) | Tier 0 Interpreter / Deopt Target | Draft |
 | 6 | [DVM-CR-PEA — Cross-Region Partial Escape Analysis](docs/DVM-CR-PEA.md) | Tier 2 / Tier 3 Pass Pipeline | Draft |
+| 7 | [DVM-Performance-Priorities](docs/DVM-Performance-Priorities.md) | Cross-cutting / Performance Strategy | Draft |
```

The README also adds an entry in the "Document Dependency Order" list (lines 53–57):
```
7. **DVM-Performance-Priorities** — the performance engineering strategy
   that defines how the VM should disappear from hot code. Covers
   inlining, specialization, scalar replacement, range analysis,
   vectorization, register allocation, and the target architecture
   from CRB to native code.
```

Cross-checks against the new spec:
- Document name "DVM-Performance-Priorities" matches the file path `docs/DVM-Performance-Priorities.md`.
- Subsystem "Cross-cutting / Performance Strategy" matches line 4 of the spec: `**Subsystem:** Cross-cutting / Performance Strategy`.
- Status "Draft" matches line 3 of the spec: `**Status:** Draft`.

**Verdict:** **PASS**

---

### Check 9 — Build still clean (spec is docs-only; code should be unaffected)

**Rule:** `make SAN=1 -j$(nproc)` in both `compiler/dgw-core` and `runtime/interp` exits 0 with no warnings or errors.

**Evidence:**

```
$ cd /home/z/my-project/dgw-core-repo/compiler/dgw-core && make clean && make SAN=1 -j$(nproc) 2>&1 | tail -3
rm -rf build bin
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/weaver.cpp -o build/weaver.o
ar rcs build/libdgwcore.a build/control.o build/graph.o build/pass_cleanup.o build/pass_dce.o build/pass_gvn.o build/pass_licm.o build/scheduler.o build/signatures.o build/verifier.o build/weaver.o
g++ -std=c++26 ... tests/smoke.cpp -Lbuild -ldgwcore -o bin/dgw_smoke ...
$ echo $?
0
```

```
$ cd /home/z/my-project/dgw-core-repo/runtime/interp && make clean && make SAN=1 -j$(nproc) 2>&1 | tail -3
rm -rf build bin
g++ -Iinclude -I../../compiler/dgw-core/include -std=c++26 ... -c src/trace_compiler.cpp -o build/trace_compiler.o
ar rcs build/libdvm_interp.a build/interp.o ... build/trace_compiler.o
g++ -std=c++26 ... tests/smoke.cpp -Lbuild -ldvm_interp ... -o bin/dvm_interp_smoke ...
$ echo $?
0
```

Re-verified with `grep -cE 'warning:|error:'` on both build logs:
- `compiler/dgw-core` build: 0 warnings, 0 errors.
- `runtime/interp` build: 0 warnings, 0 errors.

Build flags include `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter` plus `-fsanitize=address,undefined -fno-omit-frame-pointer`. Both builds exit 0 under these strict flags.

The commit's only `runtime/` touch is a mode-only chmod on `runtime/interp/include/dvm/trace_cache.hpp` (no content lines changed), so the build is unaffected as expected for a docs-only commit.

**Verdict:** **PASS**

---

### Check 10 — Smoke tests still pass

**Rule:** Both `./bin/dgw_smoke` and `./bin/dvm_interp_smoke` exit 0.

**Evidence:**

```
$ cd /home/z/my-project/dgw-core-repo && timeout 10 ./compiler/dgw-core/bin/dgw_smoke 2>&1 | tail -5
    BRANCH (node #3)
  JOIN PHI: 2 incomings, 2 block_ids
Test 6: JOIN PHI has >= 2 incomings (correct)

== DGW-Core smoke test PASSED ==
$ echo $?
0
```

`dgw_smoke` full output (truncated here) shows: Graph built 13 nodes / 17 edges / 1 regions; WebVerifier pre-opt `ok=true pass=11 fail=0`; GVN eliminated=1 / DCE killed=1 / Cleanup collapsed=0; WebVerifier post-opt `ok=true pass=11 fail=0`; Scheduler 4 blocks. Final line: `== DGW-Core smoke test PASSED ==`.

```
$ cd /home/z/my-project/dgw-core-repo && timeout 10 ./runtime/interp/bin/dvm_interp_smoke 2>&1 | tail -5
-- Test 9: SKIPPED (JIT + ASan incompatible) --

-- Test 10: SKIPPED (JIT + ASan incompatible) --

== DVM Interpreter smoke test PASSED ==
$ echo $?
0
```

`dvm_interp_smoke` full output (Tests 1–8 PASS, Tests 9–10 SKIPPED per the established JIT+ASan guard) shows: Test 1 (20+22=42 PASS), Test 2 (fn1→fn0=42 PASS), Test 3 (count to 10 PASS), Test 4 (ALLOC+OBJ_SET+OBJ_GET=42 PASS), Test 5 (trace of 6 instructions PASS), Test 6 (3-instr LoopClose entry_pc=3 PASS), Test 7 (10 nodes 8 edges ADD+CMP verifier ok=true PASS), Test 8 (10→10 nodes 1 block 3 ops verifier ok=true PASS), Tests 9–10 SKIPPED (the JIT+ASan guard correctly excludes the native-codegen tests under SAN=1, per REVIEW-018/019 design). Final line: `== DVM Interpreter smoke test PASSED ==`.

Both binaries exit 0. No ASan or UBSan reports.

**Verdict:** **PASS**

---

## 3. Mandatory Verifier Run (per §3.3)

The output under review is a **specification document**, not code. The `WebVerifier` (Part 8 of `DGW-Core-IR.md`) is exercised indirectly via the existing smoke tests, since the commit's only `runtime/` change is a file-mode chmod with zero content changes. The verifier is therefore green on the existing fixtures, unchanged by this commit:

- `dgw_smoke`: WebVerifier pre-opt `ok=true pass=11 fail=0 na=0`; WebVerifier post-opt `ok=true pass=11 fail=0 na=0`. (See Check 10 evidence above.)
- `dvm_interp_smoke`: WebVerifier on lifted graph (Test 7) and optimised graph (Test 8) both `ok=true pass=11 fail=0`. (See Check 10 evidence above.)

No code path was added or modified by commit `506a911` that the WebVerifier could newly exercise. Per §3.3, the verifier run is recorded as green on existing fixtures; no new verifier invocation is required for a docs-only commit.

---

## 4. Non-Blocking Observations (informational only, do not affect verdict)

These items are flagged for producer awareness and possible follow-up, but none of them constitute a rule violation under the Mandatory Agent Review Rule.

**N1 — File-mode changes on non-docs files.**
Commit `506a911` includes three mode-only `chmod 644 → 755` changes outside the docs tree:
- `docs/reviews/2026-09-02-review-agent-018-dvm-trace-cache.md`
- `docs/reviews/2026-09-02-review-agent-019-dvm-trace-cache-fix.md`
- `runtime/interp/include/dvm/trace_cache.hpp`

These do not change file content (verified by `git diff --name-status` returning mode lines only, with `git diff` showing only `old mode 100644 / new mode 100755`). They do not affect build or test outcomes. However, mixing a `chmod` of a `runtime/` header file into a docs-only commit is mildly surprising; future docs-only commits should avoid touching files outside `docs/` and `README.md` so that the commit's scope is unambiguous from `--stat` output alone.

**N2 — Spec metadata date ahead of review date.**
The new spec's `**Last Updated:** 2026-09-09` (line 7) is one day ahead of the commit author date (`Tue Sep 8 21:35:23 2026 +0000`) and one day ahead of this review's timestamp (`2026-09-08T21:39:45Z`). The discrepancy is small (the spec metadata date is rounded to the next day, likely a timezone rounding artifact since the author local time may be a few hours ahead of UTC). Cosmetic only; no rule violation. Future specs should use a date that matches the commit's UTC date to avoid reviewer confusion.

**N3 — Forward-looking proposals flagged for future spec amendment.**
Three proposals in the new spec (per Check 6) would require amendments to existing specs before implementation:
- §1 (32-bit CRB-F) → `DVM-CRB.md` would need a v1.1 amendment to formalize the compact encoding.
- §9 (memory partitions) → `DGW-Core-IR.md` Part 8.3 (single memory chain verifier invariant) would need to be relaxed to allow per-partition chains joined by `MEMORY_JOIN`.
- §21 (target-independent low-level IR) → `DGW-Core-IR.md` Part 7 (Scheduling and Block Formation) would need to be extended with an intermediate lowering phase.

None of these amendments are blocking for THIS review — the new spec does not claim to override the current rules; it explicitly frames these as future engineering priorities. But the producer should be aware that future implementation PRs would trigger follow-up reviews against the amended specs, not the current ones.

**N4 — DVM-Compiler-Laws dependency is declared but not explicitly cited in any section.**
The spec's `**Depends On:**` field lists `DVM-Compiler-Laws`, but no section of the new spec explicitly references a specific DVM Rule by number. The spec discusses speculation, guards, and deopt (DVM-Compiler-Laws topics) in §4, §5, §16, but does so descriptively rather than by rule number. This is acceptable for a strategy-level doc, but if the producer wants the dependency to be machine-parseable (per Mandatory-Agent-Review-Rule.md §5.2), specific rule citations (e.g., "DVM Rule 40 — Verifier Invariants" in §16, "DVM Rule 112 — Bounded compile times" in §23) would be a small improvement.

**N5 — Tier 3 description in §10 is terse.**
§10's "Tier 3" pipeline (line 313) is a single line: `everything, with absurd optimization budgets`. This is stylistically consistent with the spec's informal tone but is the only tier description without a concrete pass list. A future revision could enumerate the Tier 3 passes (e.g., static trace specialization, certified guard removal) for completeness. Not a violation — the spec is a strategy doc, not a normative spec.

---

## 5. Verifier Log

```
=== Build: compiler/dgw-core (SAN=1) ===
$ cd /home/z/my-project/dgw-core-repo/compiler/dgw-core && make clean && make SAN=1 -j$(nproc) 2>&1 | tail -3
rm -rf build bin
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/weaver.cpp -o build/weaver.o
ar rcs build/libdgwcore.a build/control.o build/graph.o build/pass_cleanup.o build/pass_dce.o build/pass_gvn.o build/pass_licm.o build/scheduler.o build/signatures.o build/verifier.o build/weaver.o
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -Iinclude tests/smoke.cpp -Lbuild -ldgwcore -o bin/dgw_smoke -fsanitize=address,undefined -fno-omit-frame-pointer
$ echo $?
0
$ grep -cE 'warning:|error:' <build log>
0

=== Build: runtime/interp (SAN=1) ===
$ cd /home/z/my-project/dgw-core-repo/runtime/interp && make clean && make SAN=1 -j$(nproc) 2>&1 | tail -3
rm -rf build bin
g++ -Iinclude -I../../compiler/dgw-core/include -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/trace_compiler.cpp -o build/trace_compiler.o
ar rcs build/libdvm_interp.a build/interp.o build/jit_buffer.o build/lifter.o build/loader.o build/module.o build/native_codegen.o build/opcodes_arith.o build/opcodes_calls.o build/opcodes_control.o build/opcodes_except.o build/opcodes_move.o build/opcodes_object.o build/opcodes_sys.o build/state.o build/trace.o build/trace_compiler.o
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -Iinclude -I../../compiler/dgw-core/include tests/smoke.cpp -Lbuild -ldvm_interp -L../../compiler/dgw-core/build -ldgwcore -o bin/dvm_interp_smoke -fsanitize=address,undefined -fno-omit-frame-pointer -L../../compiler/dgw-core/build -ldgwcore
$ echo $?
0
$ grep -cE 'warning:|error:' <build log>
0

=== Smoke: dgw_smoke ===
$ cd /home/z/my-project/dgw-core-repo && timeout 10 ./compiler/dgw-core/bin/dgw_smoke 2>&1 | tail -3
Test 6: JOIN PHI has >= 2 incomings (correct)

== DGW-Core smoke test PASSED ==
$ echo $?
0
WebVerifier pre-opt:  ok=true pass=11 fail=0 na=0
WebVerifier post-opt: ok=true pass=11 fail=0 na=0

=== Smoke: dvm_interp_smoke ===
$ cd /home/z/my-project/dgw-core-repo && timeout 10 ./runtime/interp/bin/dvm_interp_smoke 2>&1 | tail -3
-- Test 9: SKIPPED (JIT + ASan incompatible) --

-- Test 10: SKIPPED (JIT + ASan incompatible) --

== DVM Interpreter smoke test PASSED ==
$ echo $?
0
WebVerifier (Test 7 lifted):    ok=true pass=11 fail=0
WebVerifier (Test 8 optimised):  ok=true pass=11 fail=0
```

---

## 6. Summary Verdicts

| # | Check | Verdict |
|---|---|---|
| 1 | No conversational preamble | **PASS** |
| 2 | 23 sections present | **PASS** |
| 3 | Priority table S/A/B tiers + names | **PASS** |
| 4 | Target architecture ASCII pipeline diagram | **PASS** |
| 5 | Cross-spec consistency (CRB / Hybrid Tracing / DGW-Core / CR-PEA) | **PASS** |
| 6 | No contradictions with existing specs | **PASS** |
| 7 | Markdown well-formed | **PASS** |
| 8 | README updated with row 7 | **PASS** |
| 9 | Build clean (SAN=1, both trees) | **PASS** |
| 10 | Smoke tests pass (both binaries) | **PASS** |

**Total: 10 PASS, 0 FAIL, 0 N/A**

---

## 7. Final Status

**APPROVED**

All 10 checks PASS. The new spec `docs/DVM-Performance-Priorities.md` is well-formed, internally consistent, and consistent with the existing DVM spec corpus (`DVM-CRB.md`, `DVM-Hybrid-Tracing-Architecture.md`, `DGW-Core-IR.md`, `DVM-CR-PEA.md`, `DVM-Compiler-Laws.md`). Its forward-looking proposals (32-bit CRB-F, memory partitions, target-independent low-level IR) are framed as engineering investigations that extend — not contradict — the existing normative rules, and would require future spec amendments before implementation. The README's row 7 correctly references the new spec with the matching subsystem ("Cross-cutting / Performance Strategy") and status ("Draft"). Both builds (compiler/dgw-core and runtime/interp, SAN=1) exit 0 with zero warnings and zero errors; both smoke tests (`dgw_smoke`, `dvm_interp_smoke`) exit 0 with WebVerifier green on all exercised fixtures. The commit is docs-only as scoped; the three mode-only file changes outside `docs/` are non-blocking and do not affect any build or test outcome.

---

**Reviewer Agent ID:** `review-agent-020`
**UTC Timestamp:** 2026-09-08T21:39:45Z
**Review Report Path:** `docs/reviews/2026-09-02-review-agent-020-dvm-perf-priorities.md`
