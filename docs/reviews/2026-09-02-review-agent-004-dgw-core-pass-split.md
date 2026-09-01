# Spec Compliance Review — DGW-Core Per-Pass File Split (REVIEW-004)

**Task ID:** REVIEW-004
**Reviewer agent ID:** review-agent-004
**UTC timestamp:** 2026-09-01T21:02:55Z
**Output under review:** commit `6da5afba` — `compiler/dgw-core: split passes into one .cpp + .hpp per pass`
**Spec under review:** `docs/DGW-Core-IR.md` Parts 5.2, 6.1, 6.2, 6.3 (unchanged behavior) + `docs/Mandatory-Agent-Review-Rule.md` Section 3 (split is a structural change requiring review)
**Scope:** Structural refactor only. The monolithic `src/passes.cpp` (340 lines) and `include/dgw/passes.hpp` (69 lines) were split into one `.cpp` + one `.hpp` per pass. No behavior change is permitted.

---

## 1. Prior reviews summary

REVIEW-001 (review-agent-001, 2026-09-01T19:24:32Z) audited the initial DGW-Core IR implementation (commit `8dba306d`) against all 48 spec rules in Parts 1–8 of `DGW-Core-IR.md` and returned **CHANGES_REQUESTED** with 33 PASS, 15 FAIL, 0 N/A — the FAILs spanned `create_call` MEMORY wiring, EXCEPT-port plumbing, `splice_into_edge` cost, `reclaim_dead_nodes` being a no-op, LICM-only-counting, PGO endpoint comparison, single-block block extraction, empty JOIN/STATE-to-PHI `incomings`/`block_ids`, the verifier's three input-edge checks, missing alias-disjoint reordering, the GUARD failure-path "exclusively" check breaking early, and the no-observable-after-GUARD check being a no-op. REVIEW-002 (review-agent-002, 2026-09-01T20:00:19Z) re-reviewed commit `7aba802c` and returned **CHANGES_REQUESTED** with 12 PASS, 3 PARTIAL, 0 FAIL — the 3 PARTIALs were R6.3.1 (LICM identifies invariants but does not hoist), R7.2.2 (JOIN-to-PHI populates `incomings`/`block_ids` only on back-edges), and R7.2.3 (STATE-to-PHI has the identical forward-edge defect). REVIEW-003 (review-agent-003, 2026-09-01T20:17:53Z) re-reviewed commit `b5255d6b` and returned **APPROVED** with 3 PASS, 0 PARTIAL, 0 FAIL — the LICM hoist mutation `w.connect_control(start_node, NodeId{n})` was implemented at `src/passes.cpp:276`; `add_to_block` at `src/scheduler.cpp:81-100` was extended to take `(pred_node, pred_block)` and populate `phi.incomings`/`phi.block_ids` immediately on first visit (covering both JOIN and STATE via the same `if` predicate); and smoke Test 6 empirically verified the JOIN PHI has 2 incomings and 2 block_ids. The producer then pushed commit `6da5afba` performing a pure structural refactor that splits `passes.{cpp,hpp}` into per-pass files; this re-review (REVIEW-004) was performed by a fresh reviewer (review-agent-004, distinct from review-agent-001, -002, and -003) per Section 3.4 of the review rule.

---

## 2. Reviewer's verdicts on the split

### Check 1 — No behavior change

**Verdict:** PASS
**Evidence:**
- `git show HEAD~1:compiler/dgw-core/src/passes.cpp` (340 lines) saved to `/tmp/old_passes.cpp`.
- An `awk` extractor (`/tmp/extract_pass.sh`) was used to slice the body of each `XxxStats pass_xxx(Weaver& w) { ... }` function out of both `/tmp/old_passes.cpp` and the corresponding new per-pass file:
  - `diff <(awk ... old_passes.cpp) <(awk ... src/pass_gvn.cpp)` → no differences, `GVN: IDENTICAL`
  - `diff <(awk ... old_passes.cpp) <(awk ... src/pass_dce.cpp)` → no differences, `DCE: IDENTICAL`
  - `diff <(awk ... old_passes.cpp) <(awk ... src/pass_licm.cpp)` → no differences, `LICM: IDENTICAL`
  - `diff <(awk ... old_passes.cpp) <(awk ... src/pass_cleanup.cpp)` → no differences, `CLEANUP: IDENTICAL`
- The GVN-only anonymous-namespace helpers `GvnKey` and `GvnKeyHash` (old passes.cpp lines 27–43) were sliced out and diffed against `src/pass_gvn.cpp:26–42` → no differences, `GvnKey/GvnKeyHash: IDENTICAL`.
- Differences between the new per-pass files and the old monolithic file are confined to: (a) the per-file header comment block (the new files cite only the relevant sub-part of the spec instead of the umbrella Part 6), (b) the `#include` lines (new files include their own per-pass header plus a per-pass-tailored set of std headers — e.g., `pass_dce.cpp` no longer pulls in `<unordered_map>` or `<unordered_set>`), and (c) the removal of the `// === 6.x NAME ===` banner comments that previously separated the four passes in the monolithic file. All three difference categories are explicitly permitted by the task description ("whitespace and includes OK to differ").

**Reasoning:** The diff method is conservative because it captures the entire function body verbatim including inline comments and whitespace, so any logic modification — a single-character change to a condition, a renamed local, a re-ordered Weaver call, a modified `stats` field update, or a different break/continue policy — would have surfaced as a diff line. None did. Every Weaver mutation site is preserved: GVN's `w.rewire_uses` + `w.kill_node` (pass_gvn.cpp:98–99), DCE's `w.kill_node` (pass_dce.cpp:54), LICM's `w.connect_control(start_node, NodeId{n})` hoist (pass_licm.cpp:132), and Cleanup's `w.connect(...EdgeKind::VALUE)` + `w.kill_node(src)` chain collapse (pass_cleanup.cpp:50–53) are all present at the same logical positions. The behavioral surface — control flow, Weaver call sequences, struct-field updates, counters, conditions, BFS traversals, dead-node skips — is preserved exactly.

---

### Check 2 — No dropped code

**Verdict:** PASS
**Evidence:**
- `grep -nE "^[A-Z][a-zA-Z]*Stats|^struct [A-Z]|^namespace" /tmp/old_passes.cpp` lists 9 top-level entities in old passes.cpp: `namespace dgw` (line 17), anonymous `namespace` (line 19), `struct GvnKey` (line 27), `struct GvnKeyHash` (line 36), and the four pass functions `GvnStats pass_gvn` (50), `DceStats pass_dce` (112), `LicmStats pass_licm` (184), `CleanupStats pass_cleanup` (299).
- The same grep against `compiler/dgw-core/src/pass_*.cpp` lists the same 9 entities distributed across the four new files:
  - `pass_gvn.cpp`: `namespace dgw` (16), anonymous `namespace` (18), `struct GvnKey` (26), `struct GvnKeyHash` (35), `GvnStats pass_gvn` (46) — 5 entities.
  - `pass_dce.cpp`: `namespace dgw` (16), `DceStats pass_dce` (18) — 2 entities.
  - `pass_licm.cpp`: `namespace dgw` (38), `LicmStats pass_licm` (40) — 2 entities.
  - `pass_cleanup.cpp`: `namespace dgw` (18), `CleanupStats pass_cleanup` (20) — 2 entities.
  - Total: 5 + 2 + 2 + 2 = 11 entities, of which 4 are the duplicated `namespace dgw` openers (one per file). The 4 `namespace dgw` openers are paired with 4 `}  // namespace dgw` closers (one per file), so the accounting is consistent: 4 `namespace dgw` (closers) + 1 anonymous `namespace` + 2 struct + 4 functions = 11 top-level entities on the new side, matching the 9 on the old side plus the 2 extra `namespace dgw` openers/closers that result from splitting one TU into four.
- `grep -nE "^static" /tmp/old_passes.cpp` → 0 matches (no top-level static helpers in old).
- `grep -nE "^static" compiler/dgw-core/src/pass_*.cpp` → 0 matches (no top-level static helpers in new either).
- The `git show --stat HEAD` summary confirms: `pass_gvn.cpp +105`, `pass_dce.cpp +60`, `pass_licm.cpp +152`, `pass_cleanup.cpp +61`, `passes.cpp -340`. Sum of new .cpp lines: 378; old passes.cpp: 340. Difference of 38 lines is accounted for by the four per-file header comment blocks (~7 lines each = 28) plus the four `#include "dgw/pass_X.hpp"` lines + the four `#include <...>` lines + the four `namespace dgw {`/`}  // namespace dgw` openers/closers that each new file adds. This is consistent with a pure split, no net code added or removed.

**Reasoning:** Every entity present in the old monolithic file is present in exactly one of the new per-pass files. The GvnKey/GvnKeyHash anonymous-namespace helpers were correctly placed in `pass_gvn.cpp` (where they are referenced by `pass_gvn`'s `std::unordered_map<GvnKey, NodeId, GvnKeyHash> table` local) rather than duplicated into the other three files where they would be unused (and would risk ODR-style confusion if placed in a named namespace). No function is duplicated across files (e.g., `pass_gvn` does not also appear in `pass_dce.cpp`), and no function is absent from all four files (e.g., `pass_dce` is present in `pass_dce.cpp` and not anywhere else). No anonymous-namespace helper is missing — the only anonymous-namespace helpers in the old file were the GVN ones, and they are present in the new `pass_gvn.cpp`.

---

### Check 3 — No dangling references

**Verdict:** PASS
**Evidence:**
- `rg -n "passes\.hpp" /home/z/my-project/dgw-core-repo/compiler/` → 0 matches.
- `rg -n "passes\.cpp" /home/z/my-project/dgw-core-repo/compiler/` → 0 matches.
- `ls include/dgw/passes.hpp src/passes.cpp` (in `compiler/dgw-core`) → "No such file or directory" for both.
- `git show --stat HEAD` confirms `include/dgw/passes.hpp` was deleted (-69 lines) and `src/passes.cpp` was deleted (-340 lines).
- `include/dgw/graph.hpp:19-22` now includes the four per-pass headers:
  ```cpp
  #include "dgw/pass_gvn.hpp"
  #include "dgw/pass_dce.hpp"
  #include "dgw/pass_licm.hpp"
  #include "dgw/pass_cleanup.hpp"
  ```
- `tests/smoke.cpp:46` directly includes `#include "dgw/pass_licm.hpp"` (the only pass consumed outside `Graph::optimize_default`).

**Reasoning:** The old monolithic files are gone from the tree, and no surviving source file (or test file) still references either of them via `#include`. `graph.hpp` is the one-stop header that pulls in all four per-pass headers, so any TU that previously included `dgw/passes.hpp` (or transitively got it via `dgw/graph.hpp`) still sees the four `XxxStats` structs and the four `pass_xxx` declarations. `tests/smoke.cpp` calls `pass_licm` directly at lines 260 and 296 (outside `Graph::optimize_default`), so it needs the direct include — which it has. The Makefile uses `LIB_SRC := $(wildcard $(SRCDIR)/*.cpp)`, so the four new .cpp files are auto-discovered; no Makefile edit was needed (the commit message explicitly notes this, and the Makefile confirms it).

---

### Check 4 — Headers match

**Verdict:** PASS
**Evidence:**
| Per-pass .hpp | Stats struct (declared) | Function (declared) | Corresponding .cpp includes its own .hpp? |
|---|---|---|---|
| `pass_gvn.hpp:23-26` | `struct GvnStats { uint32_t eliminated{0}; uint32_t visited{0}; };` | `GvnStats pass_gvn(Weaver& w);` (line 29) | `pass_gvn.cpp:10` → `#include "dgw/pass_gvn.hpp"` ✓ |
| `pass_dce.hpp:19-22` | `struct DceStats { uint32_t killed{0}; uint32_t live{0}; };` | `DceStats pass_dce(Weaver& w);` (line 25) | `pass_dce.cpp:11` → `#include "dgw/pass_dce.hpp"` ✓ |
| `pass_licm.hpp:20-23` | `struct LicmStats { uint32_t hoisted{0}; uint32_t visited{0}; };` | `LicmStats pass_licm(Weaver& w);` (line 30) | `pass_licm.cpp:32` → `#include "dgw/pass_licm.hpp"` ✓ |
| `pass_cleanup.hpp:22-25` | `struct CleanupStats { uint32_t collapsed{0}; uint32_t killed{0}; };` | `CleanupStats pass_cleanup(Weaver& w);` (line 28) | `pass_cleanup.cpp:14` → `#include "dgw/pass_cleanup.hpp"` ✓ |

Each new per-pass .hpp's struct layout matches the corresponding struct in the old `passes.hpp` field-for-field:
- `GvnStats` matches old passes.hpp lines 32–35.
- `DceStats` matches old passes.hpp lines 41–44.
- `LicmStats` matches old passes.hpp lines 51–54.
- `CleanupStats` matches old passes.hpp lines 61–64.

Consumer call sites that depend on these declarations still compile and link:
- `include/dgw/graph.hpp:60-62` constructs `OptStats { GvnStats gvn; DceStats dce; CleanupStats cleanup; VerifyReport post_verify; }`.
- `src/graph.cpp:9-11` assigns `s.gvn = pass_gvn(weaver_); s.dce = pass_dce(weaver_); s.cleanup = pass_cleanup(weaver_);` — pulled in via `graph.hpp`'s includes.
- `tests/smoke.cpp:260, 296` declares `LicmStats ls = pass_licm(w4);` and `LicmStats lsb = pass_licm(w4b);` — pulled in via the direct `pass_licm.hpp` include at smoke.cpp:46.

**Reasoning:** Each .cpp includes its own .hpp as the first include, so the function definition is checked against its declaration by the compiler (and the clean build at Check 5 confirms there are no signature mismatches). Each .hpp's struct layout matches the old `passes.hpp` struct layout field-for-field (same field names, same types, same default-initializers), so any caller that previously accessed `stats.eliminated`, `stats.killed`, `stats.visited`, `stats.collapsed`, `stats.live`, or `stats.hoisted` continues to compile and link identically. The .hpp files declare only the function and the stats struct that the corresponding .cpp defines — no extra declarations, no missing declarations. The `#pragma once` guard and the `dgw/weaver.hpp` dependency are present in each .hpp, matching the old `passes.hpp`'s shape.

---

### Check 5 — Build is clean

**Verdict:** PASS
**Evidence:**
- Command: `cd /home/z/my-project/dgw-core-repo/compiler/dgw-core && make clean && make SAN=1 -j$(nproc)`
- Exit code: **0**
- The Makefile's `LIB_SRC := $(wildcard $(SRCDIR)/*.cpp)` automatically picks up the four new per-pass .cpp files; the compile log shows 10 object files being produced including `build/pass_cleanup.o`, `build/pass_dce.o`, `build/pass_gvn.o`, `build/pass_licm.o` (the four new ones) alongside the six unchanged ones (`control.o`, `graph.o`, `scheduler.o`, `signatures.o`, `verifier.o`, `weaver.o`).
- 7 `-Wunused-but-set-variable` warnings emitted by g++ — all on smoke-test fixture variables (`ret2` at smoke.cpp:177, `true_ret` at :196, `alloc4` at :239, `ret4` at :252, `ret4b` at :294, `ret5` at :327, `ret6` at :412). These are the same warnings REVIEW-003 recorded and are unrelated to the pass split (they predate this commit and are on the smoke-test side, not on any pass file).
- ASan+UBSan compile and link cleanly (`-fsanitize=address,undefined -fno-omit-frame-pointer` is passed to both compile and link stages per the Makefile at lines 13–20).

**Reasoning:** A clean build under SAN=1 with the strict warning flags (`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type`) means: the per-pass .cpp files compile, the per-pass .hpp files are syntactically valid C++26, the ODR is satisfied (one definition of each pass function, in its respective .cpp), no .cpp has a name clash with another (each pass function lives in a separate TU under the `dgw` namespace, and the only anonymous-namespace struct pair GvnKey/GvnKeyHash lives only in `pass_gvn.cpp`'s anonymous namespace — so even if another TU later added its own anonymous-namespace `GvnKey`, there would be no ODR clash because anonymous namespaces give internal linkage), and the smoke binary links against the static lib without missing-symbol errors. The pre-existing `-Wunused-but-set-variable` warnings are not introduced by this commit and were already accepted in REVIEW-003.

---

### Check 6 — Smoke test passes

**Verdict:** PASS
**Evidence:**
- Command: `./bin/dgw_smoke`
- Exit code: **0**
- A byte-for-byte diff between this run's stdout and REVIEW-003's recorded smoke output returned **no differences** (`diff /tmp/smoke_003.txt /tmp/smoke_004.txt` → "SMOKE OUTPUT IDENTICAL TO REVIEW-003").
- The smoke output confirms all 8 test groups green with the numbers REVIEW-003 recorded:
  - Pre-opt WebVerifier: `ok=true pass=11 fail=0 na=0` (REVIEW-003: identical)
  - GVN: `eliminated=1, visited=7` (REVIEW-003: identical)
  - DCE: `killed=1, live=11` (REVIEW-003: identical)
  - Cleanup: `collapsed=0, killed=0` (REVIEW-003: identical)
  - Post-opt WebVerifier: `ok=true pass=11 fail=0 na=0` (REVIEW-003: identical)
  - Scheduler Test 1: `4 block(s)` (Block #0 START/GUARD/RETURN, Block #1 LOAD, Block #2 STORE, Block #3 DEOPT_TRAP) — REVIEW-003: identical 4 blocks.
  - Test 2: `route_except_to: ok (edge id=3)`, `Test 2 graph: 5 nodes, 6 edges`, `ok=true pass=11` (REVIEW-003: identical)
  - Test 3: `2 block(s)` CFG (Block #0 START/BRANCH/RETURN, Block #1 RETURN) — REVIEW-003: identical 2 blocks.
  - Test 4: `LICM: visited=1, hoisted=0` (REVIEW-003: identical)
  - Test 4b: `LICM-4b: visited=1, hoisted=0` (REVIEW-003: identical)
  - Test 5: `single-failure-to-trap verifier ok` (REVIEW-003: identical)
  - Test 5b: `verifier correctly flagged non-trap GUARD failure consumer` (REVIEW-003: identical)
  - Test 6: `1 block(s)` CFG with `PHI (from JOIN = node #6, 2 incomings)` and `JOIN PHI: 2 incomings, 2 block_ids` and `Test 6: JOIN PHI has >= 2 incomings (correct)` (REVIEW-003: identical 2 incomings / 2 block_ids)
  - Final line: `== DGW-Core smoke test PASSED ==` (REVIEW-003: identical)

**Reasoning:** The smoke test exercises all four pass functions in their new file homes: `pass_gvn` and `pass_dce` via `Graph::optimize_default` at `src/graph.cpp:9-11` (which transitively pulls in `pass_gvn.hpp` and `pass_dce.hpp` via `graph.hpp`); `pass_cleanup` similarly via `optimize_default`; and `pass_licm` directly at `tests/smoke.cpp:260` (Test 4) and `:296` (Test 4b) via the direct include at `tests/smoke.cpp:46`. All exit-code-relevant assertions pass. The numerical results — GVN eliminated=1, DCE killed=1, 4 blocks in test 1, 2 blocks in test 3, JOIN PHI 2 incomings in test 6 — are identical to REVIEW-003's recorded numbers, confirming behavior preservation. The pass split is a pure structural refactor with no observable runtime effect, as a behavior-preserving split must be.

---

### Check 7 — README updated

**Verdict:** PASS
**Evidence:** `compiler/dgw-core/README.md` (68 lines total) contains:
- Layout block (lines 11–45) lists the four new per-pass .hpp files under `include/dgw/` at lines 22–25:
  ```
  ├── pass_gvn.hpp      # Part 6.1 — Global Value Numbering
  ├── pass_dce.hpp      # Part 6.2 — Dead Code Elimination
  ├── pass_licm.hpp     # Part 6.3 — Loop Invariant Code Motion
  ├── pass_cleanup.hpp  # Part 5.2 — FWD-chain CleanupPass
  ```
  and the four new per-pass .cpp files under `src/` at lines 33–36:
  ```
  ├── pass_gvn.cpp      # GVN
  ├── pass_dce.cpp      # DCE
  ├── pass_licm.cpp     # LICM
  ├── pass_cleanup.cpp  # CleanupPass
  ```
  The old `passes.hpp` and `passes.cpp` are absent from the layout block.
- A new "Rule:" paragraph at lines 47–49 reads verbatim:
  > **Rule:** every pass has its own `.cpp` AND its own `.hpp`. Do not collapse multiple passes into a single file — large combined files become unmaintainable. New passes go in `pass_<name>.cpp` / `pass_<name>.hpp`.
- The `src/` directory's annotation at line 30 was updated to read `Implementations — one .cpp per header, one .cpp per pass` (the "one .cpp per pass" clause is new).

**Reasoning:** The README's layout block matches the actual on-disk file inventory: no listed file is missing, and no on-disk pass file is omitted from the listing, so a maintainer reading the README sees an accurate map of the per-pass structure. The new "Rule:" paragraph makes the "every pass gets its own file" convention explicit and normative, providing guidance for future contributors who might be tempted to re-collapse passes into a monolithic file (which is exactly the anti-pattern this commit reverses). The commit message at HEAD explicitly claims to have done this README update ("The README's layout block is updated to reflect the new per-pass files, and a one-paragraph 'Rule:' line is added") and the README content verifies that claim. The two-part documentation requirement in the task description — (a) reflect the new per-pass layout, (b) document the "every pass gets its own file" rule — is satisfied.

---

## 3. Verifier run log

### Build

Command: `cd /home/z/my-project/dgw-core-repo/compiler/dgw-core && make clean && make SAN=1 -j$(nproc)`

```
rm -rf build bin
mkdir -p build
mkdir -p bin
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/control.cpp -o build/control.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/graph.cpp -o build/graph.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/pass_cleanup.cpp -o build/pass_cleanup.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/pass_dce.cpp -o build/pass_dce.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/pass_gvn.cpp -o build/pass_gvn.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/pass_licm.cpp -o build/pass_licm.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/scheduler.cpp -o build/scheduler.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/signatures.cpp -o build/signatures.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/verifier.cpp -o build/verifier.o
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -c src/weaver.cpp -o build/weaver.o
ar rcs build/libdgwcore.a build/control.o build/graph.o build/pass_cleanup.o build/pass_dce.o build/pass_gvn.o build/pass_licm.o build/scheduler.o build/signatures.o build/verifier.o build/weaver.o
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -Iinclude tests/smoke.cpp -Lbuild -ldgwcore -o bin/dgw_smoke -fsanitize=address,undefined -fno-omit-frame-pointer
tests/smoke.cpp: In function 'int main()':
tests/smoke.cpp:177:10: warning: variable 'ret2' set but not used [-Wunused-but-set-variable]
  177 |   NodeId ret2 = w2.create_return(handler, c2);
      |          ^~~~
tests/smoke.cpp:196:10: warning: variable 'true_ret' set but not used [-Wunused-but-set-variable]
  196 |   NodeId true_ret = w3.create_return(br, c3);
      |          ^~~~
tests/smoke.cpp:239:10: warning: variable 'alloc4' set but not used [-Wunused-but-set-variable]
  239 |   NodeId alloc4 = w4.create_alloc(RegionKind::STACK, 8, 8);
      |          ^~~~
tests/smoke.cpp:252:10: warning: variable 'ret4' set but not used [-Wunused-but-set-variable]
  252 |   NodeId ret4 = w4.create_return(s4, add4);
      |          ^~~~
tests/smoke.cpp:294:10: warning: variable 'ret4b' set but not used [-Wunused-but-set-variable]
  294 |   NodeId ret4b = w4b.create_return(s4b, add2_b);
      |          ^~~~
tests/smoke.cpp:327:10: warning: variable 'ret5' set but not used [-Wunused-but-set-variable]
  327 |   NodeId ret5 = w5.create_return(guard5, cond5);
      |          ^~~~
tests/smoke.cpp:412:10: warning: variable 'ret6' set but not used [-Wunused-but-set-variable]
  412 |   NodeId ret6 = w6.create_return(join6, join6);
      |          ^~~~
```

**`make` exit code:** **0** — clean build. The four new per-pass .cpp files (`pass_cleanup.cpp`, `pass_dce.cpp`, `pass_gvn.cpp`, `pass_licm.cpp`) all compile cleanly under `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror=return-type` and ASan+UBSan. The 7 `-Wunused-but-set-variable` warnings are all on smoke-test fixture variables (not on any pass file) and are identical to the warnings REVIEW-003 recorded.

### Smoke test

Command: `./bin/dgw_smoke`

```
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
```

**`./bin/dgw_smoke` exit code:** **0** — all 8 test groups green. A byte-for-byte diff against REVIEW-003's recorded smoke output returned no differences; the numbers are identical (GVN eliminated=1, DCE killed=1, 4 blocks in test 1, 2 blocks in test 3, JOIN PHI 2 incomings and 2 block_ids in test 6).

---

## 4. Final review status

**APPROVED**

All 7 checks PASS:

- **Check 1 — No behavior change:** PASS. Per-pass function bodies are byte-for-byte identical to the corresponding sections of old `passes.cpp`; the GvnKey/GvnKeyHash anonymous-namespace helpers are byte-for-byte identical to old passes.cpp lines 27–43. Differences are confined to header comments, `#include` lines, and removed `// === 6.x NAME ===` banners — all explicitly permitted by the task description.
- **Check 2 — No dropped code:** PASS. All 9 top-level entities of old passes.cpp (1 `namespace dgw`, 1 anonymous `namespace`, 2 structs, 4 pass functions) are present in the new per-pass files, distributed exactly as the task requires: `GvnKey`, `GvnKeyHash`, `pass_gvn` in `pass_gvn.cpp`; `pass_dce` in `pass_dce.cpp`; `pass_licm` in `pass_licm.cpp`; `pass_cleanup` in `pass_cleanup.cpp`. No top-level static helpers exist in either old or new.
- **Check 3 — No dangling references:** PASS. `rg "passes\.hpp"` and `rg "passes\.cpp"` over the entire `compiler/` tree return 0 matches. Both old files are confirmed deleted by `git show --stat HEAD` and absent on disk. `graph.hpp:19-22` includes the four per-pass headers; `tests/smoke.cpp:46` includes `pass_licm.hpp` directly.
- **Check 4 — Headers match:** PASS. Each per-pass .hpp declares exactly the function and the stats struct that the corresponding .cpp defines; each .cpp includes its own .hpp as the first include; struct layouts match old passes.hpp field-for-field.
- **Check 5 — Build is clean:** PASS. `make clean && make SAN=1 -j$(nproc)` exits 0. The four new per-pass .cpp files compile cleanly under the strict warning set + ASan + UBSan. The 7 `-Wunused-but-set-variable` warnings are on smoke-test fixture variables (identical to REVIEW-003's recorded warnings) and are unrelated to the pass split.
- **Check 6 — Smoke test passes:** PASS. `./bin/dgw_smoke` exits 0. All 8 test groups green. Byte-for-byte diff against REVIEW-003's recorded smoke output returned no differences; the numerical results (GVN eliminated=1, DCE killed=1, 4 blocks in test 1, 2 blocks in test 3, JOIN PHI 2 incomings and 2 block_ids in test 6) are identical.
- **Check 7 — README updated:** PASS. `compiler/dgw-core/README.md` lists the four new per-pass .hpp files at lines 22–25 and the four new per-pass .cpp files at lines 33–36 in the layout block; the old `passes.{hpp,cpp}` are absent; a new "Rule:" paragraph at lines 47–49 makes the "every pass gets its own .cpp AND .hpp" convention explicit and normative.

Build: clean (exit 0; only the same 7 `-Wunused-but-set-variable` warnings REVIEW-003 recorded, on smoke-test fixture variables, not on any pass file; ASan+UBSan clean). Verifier: green on all 8 test groups (pre/post-opt WebVerifier 11 PASS / 0 FAIL each; Test 2 route_except_to ok; Test 3 2-block BRANCH CFG; Test 4/4b LICM visited=1 hoisted=0; Test 5/5b GUARD failure exclusively to trap; Test 6 JOIN PHI 2 incomings / 2 block_ids). Smoke test: PASSED (exit 0, byte-for-byte identical to REVIEW-003's recorded output).

Per `Mandatory-Agent-Review-Rule.md` Section 3.4, **APPROVED** is the final review status: all 7 checks PASS, the verifier is green, and the build is clean. The split is a pure structural refactor with no observable behavioral effect; the producer's commit message accurately describes what was done and includes the required spec citations (DGW-Core-IR.md Parts 5.2, 6.1, 6.2, 6.3 — unchanged behavior; Mandatory-Agent-Review-Rule.md Section 3 — structural change requiring review). The README's new "Rule:" paragraph ensures future contributors will not re-collapse the per-pass files.

---

**Reviewer agent ID:** review-agent-004
**UTC timestamp:** 2026-09-01T21:02:55Z
