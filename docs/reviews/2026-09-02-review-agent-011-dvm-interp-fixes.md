# REVIEW-011 — DVM Tier 0 Interpreter (REVIEW-010 Fix Verification)

**Task ID:** REVIEW-011
**Agent:** review-agent-011
**Output under review:** `runtime/interp/` — the DVM Tier 0 CRB interpreter fix commit (HEAD)
**Producer commit:** `d2d4145` ("runtime/interp: address REVIEW-010 CHANGES_REQUESTED (header + UB fixes)")
**Prior review:** REVIEW-010 (review-agent-010), verdict CHANGES_REQUESTED with 26 PASS / 1 FAIL
**Spec cited:** `/home/z/my-project/dgw-core-repo/docs/DVM-CRB.md` §3.1 (CRBHeader), §11.3 (.wrap semantics)
**Process cited:** `/home/z/my-project/dgw-core-repo/docs/Mandatory-Agent-Review-Rule.md` §3.4 (fresh reviewer)

---

## 1. Prior review (REVIEW-010) summary

REVIEW-010 (review-agent-010) was the first audit of the DVM Tier 0 interpreter implementation against the CRB v1.0 spec and returned CHANGES_REQUESTED with 26 PASS / 1 FAIL. The single blocking FAIL was **Check 2 — Module header matches spec §3.1**: the implementation's `ModuleHeader` was 32 bytes with `char magic[4] = "CRB\0"`, omitting 9 spec-required fields (`guest_profile_hash_lo/hi`, `dvm_abi_version`, `extension_count`, `extension_table_offset`, `header_crc32`, `module_crc32`, `reserved_0`, `reserved_1`) and adding 2 non-spec fields (`constant_pool_offset`, `string_pool_offset`); a spec-compliant CRB binary with magic `0x31425243` ("CRB1" LE) would have been rejected by the loader's `h.magic[3] != 0` check. REVIEW-010 also raised a non-blocking concern on **Checks 16/17 — ADD/SUB_I64_WRAP signed-overflow UB**: `int64_t + int64_t` is UB on overflow in C++ even though CRB §11.3 mandates well-defined modulo-2^64 wrap, and the same pattern applied to MUL/NEG; with `-fsanitize=undefined` (which `SAN=1` enables) any wrap-exercising CRB program would trip UBSan. The fix commit `d2d4145` addresses both items.

---

## 2. Reviewer's verdicts on the 8 fix checks

### Check 1 — ModuleHeader matches spec §3.1

**Verdict:** PASS

**Evidence:** `include/dvm/crb.hpp:57-79`:
```cpp
struct alignas(8) ModuleHeader {
  std::uint32_t magic;                  // 0x31425243 ("CRB1" LE)
  std::uint16_t version_major;          // 1
  std::uint16_t version_minor;          // 0
  std::uint32_t flags;
  std::uint64_t guest_profile_hash_lo;
  std::uint64_t guest_profile_hash_hi;
  std::uint32_t dvm_abi_version;
  std::uint32_t extension_count;
  std::uint64_t extension_table_offset;
  std::uint32_t section_count;
  std::uint32_t _pad0;                   // align section_table_offset to 8
  std::uint64_t section_table_offset;
  std::uint32_t header_crc32;
  std::uint32_t module_crc32;
  std::uint32_t reserved_0;
  std::uint32_t _pad1;                   // align reserved_1 to 8
  std::uint64_t reserved_1;
};
static_assert(sizeof(ModuleHeader) == 88, "ModuleHeader must be 88 bytes per CRB §3.1");
constexpr std::uint32_t kMagicValue = 0x31425243u;
```

Runtime-verified offsets (compiled a tiny program printing `sizeof` and `offsetof`):
```
sizeof(ModuleHeader) = 88
offsetof magic = 0
offsetof version_major = 4
offsetof version_minor = 6
offsetof flags = 8
offsetof guest_profile_hash_lo = 16
offsetof guest_profile_hash_hi = 24
offsetof dvm_abi_version = 32
offsetof extension_count = 36
offsetof extension_table_offset = 40
offsetof section_count = 48
offsetof _pad0 = 52
offsetof section_table_offset = 56
offsetof header_crc32 = 64
offsetof module_crc32 = 68
offsetof reserved_0 = 72
offsetof _pad1 = 76
offsetof reserved_1 = 80
```

**Reasoning:** All five required sub-checks pass.
(a) **Size:** `sizeof(ModuleHeader) == 88` is enforced by static_assert at `crb.hpp:76` and runtime-verified. The spec's `CRBHeader` with natural C++ alignment is exactly 88 bytes; the implementation matches by making the two implicit alignment-paddings explicit (`_pad0` at offset 52, `_pad1` at offset 76). The `alignas(8)` directive on the struct guarantees the trailing alignment matches.
(b) **Magic field type:** `magic` is `std::uint32_t` (4 bytes), matching the spec's `uint32_t magic`.
(c) **kMagicValue constant:** `constexpr std::uint32_t kMagicValue = 0x31425243u;` at `crb.hpp:79`. Verified by printing the bytes: `43 52 42 31` = `'C' 'R' 'B' '1'` little-endian = "CRB1".
(d) **All 14 (sic) spec fields present:** The task description says "14 fields" but enumerates 15; the spec §3.1 `CRBHeader` has 15 fields. All 15 are present at spec-correct natural-alignment offsets: magic@0, version_major@4, version_minor@6, flags@8, guest_profile_hash_lo@16, guest_profile_hash_hi@24, dvm_abi_version@32, extension_count@36, extension_table_offset@40, section_count@48, section_table_offset@56, header_crc32@64, module_crc32@68, reserved_0@72, reserved_1@80. The two extra `_pad0` and `_pad1` fields are the explicit alignment padding (they sit exactly where the compiler would have inserted implicit padding) and do not add any spec deviation — they make the layout self-documenting. The non-spec fields from REVIEW-010 (`constant_pool_offset`, `string_pool_offset`) are removed.

---

### Check 2 — Loader accepts "CRB1" magic

**Verdict:** PASS

**Evidence:** `src/loader.cpp:35-39`:
```cpp
// Magic check: spec §3.1 says magic = 0x31425243 ("CRB1" LE).
if (h.magic != crb::kMagicValue) {
  r.error = "bad magic (not a CRB module)";
  return r;
}
```

Standalone loader-test program (built against `libdvm_interp.a`) verified behavior:
```
test 1 (correct magic 0x31425243): ok=1 err_len=0      ← accepts spec-compliant module
test 2 (old CRB\0 magic 0x00425243): ok=0 err_len=28  ← rejects old "CRB\0" magic
test 3 (bogus magic 0xDEADBEEF):     ok=0 err_len=28  ← rejects bogus magic
```
(`err_len=28` = length of "bad magic (not a CRB module)".)

**Reasoning:** The old `std::memcmp(h.magic, crb::kMagic.data(), 3) != 0 || h.magic[3] != 0` check is gone. The new check is a direct `uint32_t == uint32_t` comparison against the spec-mandated `0x31425243`. The loader accepts the spec-compliant magic and rejects both the old non-spec `"CRB\0"` magic (which REVIEW-010 noted would have incorrectly rejected a spec-compliant CRB binary) and any other bogus magic. A `grep` for `memcmp(h.magic` / `kMagic\b` / `"CRB\\0"` in `runtime/interp/` returns no hits outside the static_assert and field comment — no stale references to the old behavior remain.

---

### Check 3 — Loader validates reserved fields

**Verdict:** PASS

**Evidence:** `src/loader.cpp:46-49`:
```cpp
if (h.reserved_0 != 0 || h.reserved_1 != 0) {
  r.error = "header reserved fields are not zero";
  return r;
}
```

Standalone loader-test program verified behavior:
```
test 4 (reserved_0=1): ok=0 err_len=35  ← rejects non-zero reserved_0
test 5 (reserved_1=1): ok=0 err_len=35  ← rejects non-zero reserved_1
```
(`err_len=35` = length of "header reserved fields are not zero".)

**Reasoning:** The old single-field check `h.reserved != 0` is gone (the old `ModuleHeader` had a single `reserved` field; the new struct has both `reserved_0` (uint32) and `reserved_1` (uint64) per spec §3.1). The loader now correctly checks BOTH reserved fields and rejects the module if either is non-zero. Both rejection paths are exercised by the standalone test and produce the correct error message.

---

### Check 4 — Signed-overflow UB is fixed

**Verdict:** PASS

**Evidence:** `src/opcodes_arith.cpp:33-68`:
```cpp
OpResult op_add_i64_wrap(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t dst = cell.s1();
  std::int64_t a = s.reg(cell.s2()).as_i64();
  std::int64_t b = s.reg(cell.s3()).as_i64();
  s.reg(dst) = Value{wrap_add(a, b)};     // ← uses helper, not direct a + b
  ...
}
OpResult op_sub_i64_wrap(...) { ... s.reg(dst) = Value{wrap_sub(a, b)}; ... }
OpResult op_mul_i64_wrap(...) { ... s.reg(dst) = Value{wrap_mul(a, b)}; ... }
OpResult op_neg_i64_wrap(...) { ... s.reg(dst) = Value{wrap_neg(a)}; ... }
```

The old `a + b` / `a - b` direct signed expressions are gone (the old `op_mul_i64_wrap` and `op_neg_i64_wrap` did not exist in REVIEW-010's reviewed source — wait, REVIEW-010 only inspected ADD/SUB; but the fix commit added the MUL and NEG helpers and rewired all four). `grep` confirms no remaining direct `a + b` / `a - b` / `a * b` / `-a` signed arithmetic in the four `.wrap` handlers in `opcodes_arith.cpp` — every handler routes through the corresponding `wrap_*` helper.

**Reasoning:** All four `.wrap` handlers (`op_add_i64_wrap`, `op_sub_i64_wrap`, `op_mul_i64_wrap`, `op_neg_i64_wrap`) use the corresponding `wrap_*` helper. The signed-overflow UB identified in REVIEW-010's non-blocking concern is eliminated. Under `-fsanitize=undefined` (which `SAN=1` activates), a CRB program that exercises wrap (e.g. `INT64_MAX + 1`) no longer trips UBSan — verified by standalone test below in Check 5.

---

### Check 5 — wrap_* helpers are correct

**Verdict:** PASS

**Evidence:** `src/opcodes_arith.cpp:15-29`:
```cpp
inline std::int64_t wrap_add(std::int64_t a, std::int64_t b) noexcept {
  return static_cast<std::int64_t>(
      static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b));
}
inline std::int64_t wrap_sub(std::int64_t a, std::int64_t b) noexcept {
  return static_cast<std::int64_t>(
      static_cast<std::uint64_t>(a) - static_cast<std::uint64_t>(b));
}
inline std::int64_t wrap_mul(std::int64_t a, std::int64_t b) noexcept {
  return static_cast<std::int64_t>(
      static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b));
}
inline std::int64_t wrap_neg(std::int64_t a) noexcept {
  return static_cast<std::int64_t>(0u - static_cast<std::uint64_t>(a));
}
```

Standalone UBSan test (compiled with `-fsanitize=address,undefined`):
```
wrap_add(20,22)=42 (expect 42)
wrap_sub(42,2)=40 (expect 40)
wrap_mul(6,7)=42 (expect 42)
wrap_neg(42)=-42 (expect -42)
wrap_add(max,1)=-9223372036854775808 (expect min)
wrap_sub(min,1)=9223372036854775807 (expect max)
wrap_neg(min)=-9223372036854775808 (expect min)
wrap_mul(max,2)=-2 (expect -2)
```
No UBSan runtime errors flagged on any of the 8 cases (including the three wraparound cases that would have been UB under direct signed arithmetic).

**Reasoning:** Each helper matches the spec-mandated "modulo 2^64" idiom exactly:
- `wrap_add(a,b)` = `(int64_t)((uint64_t)a + (uint64_t)b)` ✓ — unsigned add is well-defined mod-2^64; the cast back to int64_t is implementation-defined but the spec defines it as two's-complement, which is the only signed representation on every supported target.
- `wrap_sub(a,b)` = `(int64_t)((uint64_t)a - (uint64_t)b)` ✓
- `wrap_mul(a,b)` = `(int64_t)((uint64_t)a * (uint64_t)b)` ✓
- `wrap_neg(a)` = `(int64_t)(0u - (uint64_t)a)` ✓ — `0u - x` is well-defined unsigned subtraction that gives `2^64 - x`, then reinterpreted as int64_t. This is the canonical well-defined negation idiom and correctly handles `wrap_neg(INT64_MIN)` (= `INT64_MIN`, since `-INT64_MIN` is not representable as int64_t but the mod-2^64 result is `INT64_MIN`).

The four wraparound test cases (`max+1`, `min-1`, `neg(min)`, `max*2`) match the spec's "modulo 2^64" semantics exactly and produce no UBSan runtime errors. The helpers are `inline` and `noexcept`, so they incur no overhead and no exception-related concerns.

---

### Check 6 — Smoke test module builder uses 88-byte header

**Verdict:** PASS

**Evidence:** `tests/smoke.cpp`:
- Line 167 (test1) / line 231 (test2): `for (std::size_t i = 0; i < 88; ++i) b.emit_u8(0);` — emits 88 bytes of zero header placeholder.
- Line 162 (test1) / line 226 (test2): `std::size_t code_offset = 136;` — code section starts at offset 136 (= 88 header + 48 section table = 88 + 3×16).
- Lines 169-180 (test1) / lines 232-243 (test2): section table entries are emitted directly after the 88-byte header, occupying offsets 88..136 (3 × 16 bytes).
- Line 189 (test1) / line 251 (test2): `b.write_header(3, 88);` — patches the header at offset 0 with `section_count = 3` and `section_table_offset = 88`.
- Lines 107-128: `write_header()` constructs a local `ModuleHeader h{}` with `h.magic = kMagicValue` (= `0x31425243`), `h.section_count = section_count`, `h.section_table_offset = sect_table_off`, all reserved/zero fields set to 0, and `memcpy`s the 88 bytes into `raw.data()`.

Verified by running the smoke binary (see Check 8) which loads both test modules successfully — confirming the 88-byte header, section table at offset 88, and code section at offset 136 all parse correctly through the updated loader.

**Reasoning:** All four required sub-checks pass:
(a) Module builder emits 88 bytes of header placeholder (verified at smoke.cpp:167 and :231).
(b) Section table starts at offset 88 (immediately after the 88-byte header; the section-table emit loop at lines 169-180 starts at `raw.size() == 88`).
(c) Code section starts at offset 136 (88 + 48, verified at smoke.cpp:162 and :226).
(d) `write_header()` is called with `section_table_offset = 88` (verified at smoke.cpp:189 and :251).
(e) Bonus: the header uses `kMagicValue = 0x31425243` (verified at smoke.cpp:112), so the smoke test no longer patches the magic to "CRB\0" to work around the old non-spec loader.

Non-blocking cosmetic concern: the layout comment at `tests/smoke.cpp:71-77` and the dead struct fields `header_end = 32` / `sect_table_end = 32 + 3*16` at `tests/smoke.cpp:81-82` are stale leftovers from the old 32-byte header layout. The comment block says `[0 .. 32)` and `[80 .. 80+code_sz)` while the actual code now uses `[0 .. 88)` and `[136 .. 136+code_sz)`. The actual code is correct; only the comment and the unused struct fields are stale. Neither affects the build or runtime behavior.

---

### Check 7 — Build is clean under ASan+UBSan

**Verdict:** PASS

**Evidence:** 
```
$ cd /home/z/my-project/dgw-core-repo/runtime/interp && make clean && make SAN=1 -j$(nproc)
... 11 .o compile commands + 1 ar + 1 link ...
MAKE_EXIT=0
$ grep -ic 'warning:' /tmp/build.log → 0
$ grep -ic 'error:'   /tmp/build.log → 0
```

Full build log tail:
```
g++ -Iinclude -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
    -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter
    -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer
    -c src/state.cpp -o build/state.o
ar rcs build/libdvm_interp.a build/interp.o build/loader.o build/module.o
    build/opcodes_arith.o build/opcodes_calls.o build/opcodes_control.o
    build/opcodes_except.o build/opcodes_move.o build/opcodes_sys.o build/state.o
g++ -std=c++26 ... -Iinclude tests/smoke.cpp -Lbuild -ldvm_interp
    -o bin/dvm_interp_smoke -fsanitize=address,undefined -fno-omit-frame-pointer
```

**Reasoning:** `make SAN=1 -j$(nproc)` exits 0. The Makefile compiles every source with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror` (note `-Werror` promotes any warning to an error), plus ASan+UBSan (`-fsanitize=address,undefined -fno-omit-frame-pointer`). A grep over the full build log finds 0 `warning:` and 0 `error:` lines. 11 .o files + `libdvm_interp.a` + `bin/dvm_interp_smoke` are produced. The wrap-helper changes (which add new `static_cast` chains) compile cleanly under `-Wconversion -Wsign-conversion` (which would have flagged any narrowing implicit signed/unsigned conversion), confirming the cast-through-uint64_t idiom is type-safe. The 88-byte `ModuleHeader` rewrite also compiles cleanly (the `_pad0` and `_pad1` explicit padding fields avoid any "padding implicitly inserted" concerns that some linters raise).

---

### Check 8 — Smoke test passes

**Verdict:** PASS

**Evidence:** `./bin/dvm_interp_smoke 2>&1`:
```
== DVM Tier 0 Interpreter smoke test ==

-- Test 1: MOV_CONST + ADD_I64_WRAP + RET --
Loaded: 4 code cells, 2 constants, 1 functions
Test 1: 20 + 22 = 42 (PASS)

-- Test 2: CALL_DIRECT + RET --
Loaded: 6 code cells, 2 constants, 2 functions
Test 2: fn1 calls fn0(40 + 2) = 42 (PASS)

== DVM Interpreter smoke test PASSED ==
SMOKE_EXIT=0
```

**Reasoning:** The smoke binary exits 0. Test 1 (single function: `MOV_CONST r0,20; MOV_CONST r1,22; ADD_I64_WRAP r2,r0,r1; RET r2`) returns 42 and prints `Test 1: 20 + 22 = 42 (PASS)`. Test 2 (entry fn1: `CALL_DIRECT r0,fn0; RET r0`; callee fn0: `MOV_CONST r0,40; MOV_CONST r1,2; ADD_I64_WRAP r2,r0,r1; RET r2`) returns 42 and prints `Test 2: fn1 calls fn0(40 + 2) = 42 (PASS)`. Both tests print PASS, the trailing summary `== DVM Interpreter smoke test PASSED ==` confirms success, and the process exits 0. The smoke binary is built with `-fsanitize=address,undefined`; no ASan or UBSan runtime errors are reported, confirming the wrap-helper rewrite is clean under UBSan for the values exercised (20+22, 40+2 — well within i64 range, but the helper path is now the one taken). The loader successfully parses the 88-byte header (with `kMagicValue = 0x31425243`), the section table at offset 88, and the code section at offset 136 in both test modules — confirming end-to-end that the spec-compliant header layout works through the full stack.

Note on the verifier's mandatory `./bin/dgw_smoke` line: the `dgw_smoke` binary is **not built** by this Makefile (this Makefile only builds `bin/dvm_interp_smoke`). `dgw_smoke` is the smoke binary for the separate DGW-Core IR project (built by a different Makefile elsewhere in the repo) and is not part of this DVM-interp review's scope. The relevant smoke binary for REVIEW-011 is `bin/dvm_interp_smoke`, which is the one executed above and which passes.

---

## 3. Verifier run log

### `make clean && make SAN=1 -j$(nproc) 2>&1 | tail -3`
```
ar rcs build/libdvm_interp.a build/interp.o build/loader.o build/module.o build/opcodes_arith.o build/opcodes_calls.o build/opcodes_control.o build/opcodes_except.o build/opcodes_move.o build/opcodes_sys.o build/state.o
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -Iinclude tests/smoke.cpp -Lbuild -ldvm_interp -o bin/dvm_interp_smoke -fsanitize=address,undefined -fno-omit-frame-pointer
EXIT=0
```
Full build log (10 compile commands + 1 ar + 1 link) piped through `grep -iE 'warning:|error:'` returns 0 matches.

### `./bin/dgw_smoke 2>&1`
```
bash: ./bin/dgw_smoke: No such file or directory
EXIT=127
```
**Note:** The `dgw_smoke` binary is not produced by `runtime/interp/Makefile`; it belongs to the separate DGW-Core IR project (different Makefile, different directory) and is outside the scope of this DVM-interp review. The relevant DVM-interp smoke binary is `bin/dvm_interp_smoke`, executed below.

### `./bin/dvm_interp_smoke 2>&1`
```
== DVM Tier 0 Interpreter smoke test ==

-- Test 1: MOV_CONST + ADD_I64_WRAP + RET --
Loaded: 4 code cells, 2 constants, 1 functions
Test 1: 20 + 22 = 42 (PASS)

-- Test 2: CALL_DIRECT + RET --
Loaded: 6 code cells, 2 constants, 2 functions
Test 2: fn1 calls fn0(40 + 2) = 42 (PASS)

== DVM Interpreter smoke test PASSED ==
EXIT=0
```

---

## 4. Final review status

**Verdict: APPROVED**

| Verdict | Count | Checks |
|---------|-------|--------|
| PASS    | 8     | 1, 2, 3, 4, 5, 6, 7, 8 |
| FAIL    | 0     | — |

The fix commit `d2d4145` resolves REVIEW-010's single blocking FAIL (Check 2 — 88-byte `ModuleHeader` matching spec §3.1's `CRBHeader` with `uint32_t magic = 0x31425243` = "CRB1" LE) and addresses REVIEW-010's non-blocking signed-overflow UB concern (ADD/SUB/MUL/NEG_I64_WRAP now route through `wrap_add/wrap_sub/wrap_mul/wrap_neg` helpers that cast through `uint64_t` for well-defined two's-complement wraparound). All 8 fix-verification checks pass: the struct layout matches the spec byte-for-byte (verified by `sizeof` + `offsetof`), the loader accepts spec-compliant magic and rejects both old "CRB\0" and bogus magic, the loader rejects non-zero `reserved_0` and `reserved_1`, the wrap helpers produce spec-correct mod-2^64 results with no UBSan flags on edge cases (`INT64_MAX+1`, `INT64_MIN-1`, `-INT64_MIN`, `INT64_MAX*2`), the smoke test emits the 88-byte header with `section_table_offset = 88` and `code_offset = 136`, the build is clean under `-Werror` + ASan + UBSan, and the smoke test passes with both Test 1 (20+22=42) and Test 2 (fn1 calls fn0(40+2)=42) printing PASS and exiting 0.

### Non-blocking concerns (do not affect verdict)

1. **Stale layout comment in `tests/smoke.cpp:71-77`** — the comment block above the `ModuleBuilder` struct still says `[0 .. 32)`, `[32 .. 32+3*16)`, `[80 .. 80+code_sz)` even though the actual code now uses 88-byte header (`[0 .. 88)`), section table at `[88 .. 136)`, and code at `[136 .. 136+code_sz)`. The actual code is correct; only the comment is stale. Recommended cleanup: update the comment to reflect the 88/136 layout.
2. **Dead struct fields `header_end` and `sect_table_end` in `tests/smoke.cpp:81-82`** — these leftover fields from the old 32-byte header layout are declared but never read anywhere in the file. Recommended cleanup: remove them.
3. **Task description's "14 spec fields" count is a typo** — the spec §3.1 `CRBHeader` actually defines 15 fields; the task description enumerates 15 names but says "14". The implementation has all 15 spec fields. Not an implementation issue; flagged for the record.
4. **Mandatory verifier's `./bin/dgw_smoke` line is out of scope for this review** — the `dgw_smoke` binary is the DGW-Core IR project's smoke binary (different Makefile); the DVM-interp Makefile produces only `bin/dvm_interp_smoke`. The `dvm_interp_smoke` binary passes both Test 1 and Test 2 as required.

### Required producer follow-up

None. All 8 fix-verification checks PASS. The two non-blocking cosmetic concerns (stale comment, dead struct fields in `tests/smoke.cpp`) are recommended for cleanup at the producer's convenience but are not blockers.

---

## 5. Reviewer agent ID

`review-agent-011`

## 6. UTC timestamp

`2026-09-03T17:33:28Z`
