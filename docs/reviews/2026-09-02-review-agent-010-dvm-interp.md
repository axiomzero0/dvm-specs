# REVIEW-010 — DVM Tier 0 Interpreter (CRB Spec Compliance)

**Task ID:** REVIEW-010
**Agent:** review-agent-010
**Output under review:** `runtime/interp/` — the DVM Tier 0 CRB interpreter
**Producer commit:** 3f4f2565 ("runtime/interp: DVM Tier 0 interpreter with computed-goto dispatch")
**Spec cited:** `/home/z/my-project/dgw-core-repo/docs/DVM-CRB.md` (CRB v1.0 Standard)
**Process cited:** `/home/z/my-project/dgw-core-repo/docs/Mandatory-Agent-Review-Rule.md` §3

---

## 1. Prior reviews summary

REVIEW-001 (review-agent-001) audited the initial DGW-Core IR implementation against DGW-Core-IR.md Parts 1–8; verdict CHANGES_REQUESTED with 33 PASS / 15 FAIL. REVIEW-002 (review-agent-002) re-audited the fix commit; verdict CHANGES_REQUESTED with 12 PASS / 3 PARTIAL. REVIEW-003 (review-agent-003) re-verified the three REVIEW-002 PARTIALs (R6.3.1 LICM hoist, R7.2.2/R7.2.3 PHI incomings); verdict APPROVED with 3 PASS. REVIEW-004 (review-agent-004) audited the per-pass file split (no behavior change, headers match, build clean, smoke green); verdict APPROVED with 7 PASS. REVIEW-005 (review-agent-005) audited the -Werror warning-policy change; verdict CHANGES_REQUESTED with 6 PASS / 1 FAIL (three pre-existing `(void)var;` casts remained). REVIEW-006 (review-agent-006) audited the (void) cleanup; verdict APPROVED with 6 PASS. REVIEW-007 (review-agent-007) audited the DVM-CRB and DVM-CR-PEA spec additions; verdict CHANGES_REQUESTED with 15 PASS / 2 FAIL (orphan format references, incomplete opcode definitions). REVIEW-008 (review-agent-008) audited the CRB spec fixes; verdict CHANGES_REQUESTED with 9 PASS / 2 FAIL (Section 11.4 arithmetic errors, 7 opcodes still lacking `format:`). REVIEW-009 (review-agent-009) audited the second CRB spec fix; verdict APPROVED with 8 PASS / 0 FAIL. REVIEW-010 (this review) is the first audit of the DVM Tier 0 interpreter implementation (a new artifact, not a fix to a prior review).

---

## 2. Reviewer's verdicts

### Check 1 — Instruction cell layout matches spec §4.2

**Verdict:** PASS
**Evidence:** `include/dvm/crb.hpp:34-52`:
```cpp
struct alignas(8) InstrCell {
  std::uint16_t slot0;  // opcode
  std::uint16_t slot1;
  std::uint16_t slot2;
  std::uint16_t slot3;
  ...
};
static_assert(sizeof(InstrCell) == 8, "InstrCell must be 8 bytes");
```
Spec §4.2:
```cpp
struct CRBInst { uint64_t bits; };
uint16_t opcode = bits & 0xFFFF;
uint16_t op0    = (bits >> 16) & 0xFFFF;
uint16_t op1    = (bits >> 32) & 0xFFFF;
uint16_t op2    = (bits >> 48) & 0xFFFF;
```
**Reasoning:** The spec defines a single `uint64_t bits` field with bit-shift extraction. The implementation chooses to express the same layout as 4 × `uint16_t` slots. On a little-endian platform (which the spec mandates per "Encoding: Little-endian"), slot0 sits at byte offset 0 (= `bits & 0xFFFF`), slot1 at offset 2 (= `(bits >> 16) & 0xFFFF`), slot2 at offset 4 (= `(bits >> 32) & 0xFFFF`), slot3 at offset 6 (= `(bits >> 48) & 0xFFFF`). `sizeof(InstrCell) == 8` is enforced by static_assert and `alignas(8)`. The cell is 64-bit fixed-width with 4 × 16-bit slots as required.

---

### Check 2 — Module header matches spec §3.1

**Verdict:** FAIL
**Evidence:**
- Spec §3.1 (`docs/DVM-CRB.md:150-166`):
```cpp
struct CRBHeader {
    uint32_t magic;              // 'CRB1' little-endian: 0x31425243
    uint16_t version_major;      // 1
    uint16_t version_minor;      // 0
    uint32_t flags;
    uint64_t guest_profile_hash_lo;
    uint64_t guest_profile_hash_hi;
    uint32_t dvm_abi_version;
    uint32_t extension_count;
    uint64_t extension_table_offset;
    uint32_t section_count;
    uint64_t section_table_offset;
    uint32_t header_crc32;
    uint32_t module_crc32;
    uint32_t reserved_0;
    uint64_t reserved_1;
};
```
- Implementation (`include/dvm/crb.hpp:56-67`):
```cpp
struct alignas(8) ModuleHeader {
  char          magic[4];        // "CRB\0"
  std::uint16_t version_major;
  std::uint16_t version_minor;
  std::uint32_t flags;
  std::uint32_t section_count;
  std::uint32_t section_table_offset;
  std::uint32_t constant_pool_offset;
  std::uint32_t string_pool_offset;
  std::uint32_t reserved;
};
static_assert(sizeof(ModuleHeader) == 32, "ModuleHeader must be 32 bytes");
```
- Loader magic check (`src/loader.cpp:36-39`):
```cpp
if (std::memcmp(h.magic, crb::kMagic.data(), 3) != 0 || h.magic[3] != 0) {
  r.error = "bad magic (not a CRB module)";
  return r;
}
```
- `crb::kMagic` (`include/dvm/crb.hpp:167`): `constexpr std::string_view kMagic = "CRB";`

**Reasoning:** The implementation's `ModuleHeader` deviates from the spec's `CRBHeader` in three material ways. (a) **Size**: the spec's `CRBHeader` (with natural C++ alignment) is 88 bytes — magic(4) + version_major(2) + version_minor(2) + flags(4) + [pad 4] + guest_profile_hash_lo(8) + guest_profile_hash_hi(8) + dvm_abi_version(4) + extension_count(4) + extension_table_offset(8) + section_count(4) + [pad 4] + section_table_offset(8) + header_crc32(4) + module_crc32(4) + reserved_0(4) + [pad 4] + reserved_1(8) = 88. The implementation's `ModuleHeader` is 32 bytes — it omits 9 spec fields (guest_profile_hash_lo/hi, dvm_abi_version, extension_count, extension_table_offset, header_crc32, module_crc32, reserved_0, reserved_1) and adds 2 non-spec fields (constant_pool_offset, string_pool_offset, which per spec should be derived from the section table, not stored in the header). (b) **Magic value**: spec says `uint32_t magic; // 'CRB1' little-endian: 0x31425243` — i.e. bytes 0x43, 0x52, 0x42, 0x31 = `'C','R','B','1'`. The implementation uses `char magic[4]` initialized to `"CRB\0"` (bytes 0x43, 0x52, 0x42, 0x00) and the loader rejects any module whose `magic[3] != 0`. A spec-compliant CRB module written with magic `0x31425243` would be **rejected** by this loader. (c) **Field width**: spec has `uint64_t section_table_offset`; implementation has `uint32_t section_table_offset` — truncates section tables beyond 4 GiB. The smoke test only passes because `tests/smoke.cpp:165-166` writes `magic = "CRB\0"` to match the implementation's (non-spec) expectation. The task description's parenthetical field list (which lists `constant_pool_offset`, `string_pool_offset`, and a 32-byte size) does not match the actual spec §3.1; the implementation matches the task's parenthetical but not the spec.

---

### Check 3 — Constant pool entry matches spec §6

**Verdict:** PASS
**Evidence:** `include/dvm/crb.hpp:114-143`:
```cpp
struct alignas(8) ConstantEntry {
  std::uint32_t kind;
  std::uint32_t flags;
  std::uint64_t payload_lo;
  std::uint64_t payload_hi;
};
static_assert(sizeof(ConstantEntry) == 24, "ConstantEntry must be 24 bytes");

enum class ConstantKind : std::uint32_t {
  Null=0, Bool=1, I8=2, I16=3, I32=4, I64=5, U8=6, U16=7, U32=8, U64=9,
  F32=10, F64=11, Symbol=12, String=13, Bytes=14, TypeId=15, FunctionId=16,
  ObjectHandle=17, VectorDesc=18,
};
```
Spec §6 (`docs/DVM-CRB.md:408-439`):
```cpp
struct CRBConstantEntry {
    uint32_t kind;
    uint32_t flags;
    uint64_t payload_lo;
    uint64_t payload_hi;
};
enum CRBConstantKind : uint32_t {
    CRB_CONST_NULL, CRB_CONST_BOOL, CRB_CONST_I8, CRB_CONST_I16, CRB_CONST_I32,
    CRB_CONST_I64, CRB_CONST_U8, CRB_CONST_U16, CRB_CONST_U32, CRB_CONST_U64,
    CRB_CONST_F32, CRB_CONST_F64, CRB_CONST_SYMBOL, CRB_CONST_STRING,
    CRB_CONST_BYTES, CRB_CONST_TYPE_ID, CRB_CONST_FUNCTION_ID,
    CRB_CONST_OBJECT_HANDLE, CRB_CONST_VECTOR_DESC,
};
```
**Reasoning:** Field layout matches the spec exactly (4+4+8+8 = 24 bytes, verified by static_assert). All 19 `ConstantKind` enum values are present, in the same order, with the same underlying numeric values (0–18). The implementation uses `enum class` (scoped) instead of plain `enum`, which is a C++ style improvement and doesn't change the ABI.

---

### Check 4 — Function table entry matches spec §13.1

**Verdict:** PASS (with caveat)
**Evidence:** `include/dvm/crb.hpp:100-110`:
```cpp
struct alignas(4) FunctionEntry {
  std::uint32_t function_id;
  std::uint32_t name_string_id;
  std::uint32_t code_offset;
  std::uint32_t code_length;
  std::uint16_t param_count;
  std::uint16_t register_count;
  std::uint16_t return_count;
  std::uint16_t flags;
};
static_assert(sizeof(FunctionEntry) == 24, "FunctionEntry must be 24 bytes");
```
**Reasoning:** The task asks to verify `FunctionEntry` has function_id, name_string_id, code_offset, code_length, param_count, register_count, return_count, flags — the implementation has all 8 fields, and the size is 24 bytes (4+4+4+4+2+2+2+2). Caveat: the spec's §13 is "Conversion Opcodes" and does not define a `FunctionEntry` struct; the only spec reference to a function table is in §3 (Module Layout, where "Function Table" is listed as a section) and §4.4 (Function Entry Convention, prose only). The implementation is therefore a self-designed struct, not a verbatim copy of a spec struct. Since the task's parenthetical field list is fully matched and the size is reasonable, this is PASS.

---

### Check 5 — Computed-goto dispatch is used

**Verdict:** PASS
**Evidence:** `src/interp.cpp:56-86, 93-94`:
```cpp
static void* dispatch[65536] = {};
...
dispatch[crb::op::NOP] = &&op_nop;
dispatch[crb::op::TRAP] = &&op_trap;
...
#define DISPATCH_NEXT() \
  goto *dispatch[s.fetch().opcode()]
DISPATCH_NEXT();
```
**Reasoning:** The dispatch loop uses GCC labels-as-values (`&&op_nop`, `&&op_unknown`, etc.) and computed goto (`goto *dispatch[...]`), which is the standard direct-threaded interpreter idiom and the user's explicit requirement. The Makefile adds `-Wno-pedantic` for this file only because computed goto is a GNU extension (see Check 7).

---

### Check 6 — Dispatch table covers all 65536 opcode values

**Verdict:** PASS
**Evidence:** `src/interp.cpp:56, 59, 238-243`:
```cpp
static void* dispatch[65536] = {};
...
for (int i = 0; i < 65536; ++i) dispatch[i] = &&op_unknown;
...
op_unknown: {
  s.exit_value = Value::null();
  goto interp_exit;
}
```
**Reasoning:** The dispatch table is declared as `void* dispatch[65536]` (exactly 2^16 entries). The init loop sets every entry to `&&op_unknown` first, then known opcodes are overridden with their specific handler labels (NOP, TRAP, UNREACHABLE, SAFEPOINT, RET, RET_VOID, MOV_CONST, MOV_NULL, MOV_TRUE, MOV_FALSE, MOV_UNDEF, ADD_I64_WRAP, SUB_I64_WRAP, MUL_I64_WRAP, NEG_I64_WRAP, ADD_I64_CHECKED, CMP_EQ, CMP_LT_S, JMP, BR_TRUE, BR_FALSE, BR_NULL, BR_NONNULL, CALL_DIRECT, THROW — 25 handlers). Any opcode value not in the known set routes to `op_unknown`, which terminates the interpreter. All 65536 possible 16-bit opcode values are covered.

---

### Check 7 — Per-file -Wno-pedantic exception is documented

**Verdict:** PASS
**Evidence:** `Makefile:47-51`:
```make
# Per-file flag: interp.cpp uses computed goto (GCC labels-as-values), a
# GNU extension flagged by -Wpedantic. Computed goto is the standard
# interpreter dispatch idiom and the user explicitly required it.
# This is a per-file Makefile exception, not a source-level suppression.
$(BUILDDIR)/interp.o: CXXFLAGS += -Wno-pedantic
```
**Reasoning:** The Makefile adds `-Wno-pedantic` only to `interp.o`'s compile rule (per-file target-specific variable), and the 4-line comment above it explicitly explains why (computed goto is a GNU extension flagged by `-Wpedantic`; standard interpreter idiom; user's explicit requirement). No other source file receives this exception. This is a documented Makefile-level exception, not a source-level `#pragma GCC diagnostic` or `__attribute__` suppression (verified by Check 26's grep).

---

### Check 8 — NOP (§9.1)

**Verdict:** PASS
**Evidence:** `src/opcodes_sys.cpp:7-10`:
```cpp
OpResult op_nop(InterpState& s, const crb::InstrCell& /*cell*/) noexcept {
  s.advance();
  return OpResult::Continue;
}
```
**Reasoning:** Spec §9.1: opcode 0x0000, format: none, "Does nothing." The handler ignores `cell` (consistent with format: none), calls `s.advance()` to advance PC by one cell, and returns `Continue` so the dispatch loop fetches the next instruction. Format and semantics match.

---

### Check 9 — TRAP (§9.2)

**Verdict:** PASS
**Evidence:** `src/opcodes_sys.cpp:12-16`:
```cpp
OpResult op_trap(InterpState& s, const crb::InstrCell& /*cell*/) noexcept {
  s.exited = true;
  s.exit_value = Value::null();
  return OpResult::Trap;
}
```
**Reasoning:** Spec §9.2: opcode 0x0001, format: none, "Raises a guest-visible trap or runtime error according to the Guest Language Profile." The handler sets `exited = true`, sets the exit value to null, and returns `OpResult::Trap`, which causes the dispatch loop to exit. Format matches (none). Semantics: the minimal interpreter models a trap as immediate interpreter termination; a full implementation would invoke the guest trap handler. Acceptable for Tier 0 minimal.

---

### Check 10 — RET (§16.1)

**Verdict:** PASS
**Evidence:** `src/opcodes_sys.cpp:31-42`:
```cpp
OpResult op_ret(InterpState& s, const crb::InstrCell& cell) noexcept {
  Value ret_val = s.reg(cell.s1());
  s.exit_value = ret_val;
  std::uint16_t caller_ret = s.frames.back().caller_ret_reg;
  s.pop_frame();
  if (s.exited) return OpResult::Exit;
  s.frames.back().registers[caller_ret] = ret_val;
  return OpResult::Continue;
}
```
**Reasoning:** Spec §16.1: opcode 0x0800, format: R, "Returns the value in `dst` register." Format R: `opcode | dst | unused | unused` (§7.10). The handler reads `cell.s1()` as the dst (return value) register, copies the value into `exit_value`, pops the frame, and if there's a caller, writes the return value into the caller's return register (`caller_ret_reg`, set by `op_call_direct` at `opcodes_calls.cpp:28`). The R format is honored: `s1` is the single register, `s2` and `s3` are unused. Semantics match.

---

### Check 11 — RET_VOID (§16.2)

**Verdict:** PASS
**Evidence:** `src/opcodes_sys.cpp:44-49`:
```cpp
OpResult op_ret_void(InterpState& s, const crb::InstrCell& /*cell*/) noexcept {
  s.exit_value = Value::null();
  s.pop_frame();
  if (s.exited) return OpResult::Exit;
  return OpResult::Continue;
}
```
**Reasoning:** Spec §16.2: opcode 0x0801, format: none, "Returns no value. The caller's frame is discarded." The handler ignores `cell` (consistent with format: none), sets the exit value to null (no value returned), pops the frame (discarding the callee's frame), and returns Continue (or Exit if this was the outermost frame). Format and semantics match.

---

### Check 12 — MOV_CONST (§10.3)

**Verdict:** PASS
**Evidence:** `src/opcodes_move.cpp:7-47`:
```cpp
OpResult op_mov_const(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t dst = cell.s1();
  std::uint32_t const_idx = crb::InstrCell::imm32(cell.s2(), cell.s3());
  if (!s.module || const_idx >= s.module->constants.size()) {
    s.exit_value = Value::null();
    return OpResult::Trap;
  }
  const auto& ce = s.module->constants[const_idx];
  ... // switch on ce.kind → construct Value v
  s.reg(dst) = v;
  s.advance();
  return OpResult::Continue;
}
```
**Reasoning:** Spec §10.3: opcode 0x0102, format: R_IMM32, operand: constant_pool_index. R_IMM32 (§7.3): `opcode | dst | imm32_low | imm32_high`. The handler reads `cell.s1()` as dst and `InstrCell::imm32(cell.s2(), cell.s3())` as the constant pool index. The 32-bit immediate is reconstructed little-endian from the two 16-bit halves via `InstrCell::imm32(low, high) = (high << 16) | low` — correct. Bounds-checks the index against the constant pool size (returns Trap on out-of-range, defensive). Switch on `ce.kind` covers Null, Bool, I8/I16/I32/I64/U8/U16/U32/U64 (all → Int64), F32/F64 (→ Float64 via memcpy), and default → null. Loads from the constant pool by index as required.

---

### Check 13 — MOV_NULL (§10.4)

**Verdict:** PASS
**Evidence:** `src/opcodes_move.cpp:49-53`:
```cpp
OpResult op_mov_null(InterpState& s, const crb::InstrCell& cell) noexcept {
  s.reg(cell.s1()) = Value::null();
  s.advance();
  return OpResult::Continue;
}
```
**Reasoning:** Spec §10.4: opcode 0x0103, format: R, "Loads the null reference value." Format R: `opcode | dst | unused | unused`. The handler reads `cell.s1()` as the destination register and writes `Value::null()` (tag = Null, payload = 0). Format and semantics match.

---

### Check 14 — MOV_TRUE (§10.5)

**Verdict:** PASS
**Evidence:** `src/opcodes_move.cpp:55-59`:
```cpp
OpResult op_mov_true(InterpState& s, const crb::InstrCell& cell) noexcept {
  s.reg(cell.s1()) = Value::boolean(true);
  s.advance();
  return OpResult::Continue;
}
```
**Reasoning:** Spec §10.5: opcode 0x0104, format: R. The handler writes `Value::boolean(true)` (tag = Bool, payload = 1) to `cell.s1()`. Format and semantics match.

---

### Check 15 — MOV_FALSE (§10.6)

**Verdict:** PASS
**Evidence:** `src/opcodes_move.cpp:61-65`:
```cpp
OpResult op_mov_false(InterpState& s, const crb::InstrCell& cell) noexcept {
  s.reg(cell.s1()) = Value::boolean(false);
  s.advance();
  return OpResult::Continue;
}
```
**Reasoning:** Spec §10.6: opcode 0x0105, format: R. The handler writes `Value::boolean(false)` (tag = Bool, payload = 0) to `cell.s1()`. Format and semantics match.

---

### Check 16 — ADD_I64_WRAP (§11)

**Verdict:** PASS (with concern)
**Evidence:** `src/opcodes_arith.cpp:9-17`:
```cpp
OpResult op_add_i64_wrap(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t dst = cell.s1();
  std::int64_t a = s.reg(cell.s2()).as_i64();
  std::int64_t b = s.reg(cell.s3()).as_i64();
  s.reg(dst) = Value{a + b};  // wrap: no overflow check
  s.advance();
  return OpResult::Continue;
}
```
Spec §11.3: `dst = (src0 + src1) modulo 2^64`.
**Reasoning:** Format R_R_R (§7.1): `opcode | dst | src0 | src1`. The handler reads `cell.s1()` as dst, `cell.s2()` as src0, `cell.s3()` as src1 — correct. The semantics match the spec's "modulo 2^64" wrap *in practice* on GCC/Clang (two's-complement add). **Concern (non-blocking):** in strict C++, signed `int64_t + int64_t` is undefined behavior on overflow; the spec mandates well-defined modulo-2^64 wrapping. The correct idiom is `static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b))`. With `-fsanitize=undefined` (which the Makefile's `SAN=1` enables), any actual overflow would be flagged as UB even though the spec says it's well-defined. The smoke test (20+22=42, 40+2=42) does not trigger overflow, so the build and test pass cleanly. Format and intent match the spec; the implementation has a latent UB-on-overflow bug that would manifest under UBSan if a CRB program exercised wrap.

---

### Check 17 — SUB_I64_WRAP (§11)

**Verdict:** PASS (with concern)
**Evidence:** `src/opcodes_arith.cpp:19-26`:
```cpp
OpResult op_sub_i64_wrap(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t dst = cell.s1();
  std::int64_t a = s.reg(cell.s2()).as_i64();
  std::int64_t b = s.reg(cell.s3()).as_i64();
  s.reg(dst) = Value{a - b};
  s.advance();
  return OpResult::Continue;
}
```
**Reasoning:** Same analysis as Check 16. Format R_R_R: `cell.s1()` = dst, `cell.s2()` = src0, `cell.s3()` = src1. Semantics: `dst = src0 - src1` (wrapping). Same UB-on-overflow concern as ADD_I64_WRAP — `int64_t - int64_t` is UB on overflow in C++; the spec requires well-defined modulo-2^64 wrap. The smoke test does not exercise overflow, so the build and test pass. Format and intent match; latent UB concern noted.

---

### Check 18 — CMP_EQ (§14)

**Verdict:** PASS
**Evidence:** `src/opcodes_arith.cpp:59-66`:
```cpp
OpResult op_cmp_eq(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t dst = cell.s1();
  const Value& a = s.reg(cell.s2());
  const Value& b = s.reg(cell.s3());
  s.reg(dst) = Value::boolean(a == b);
  s.advance();
  return OpResult::Continue;
}
```
`Value::operator==` (`include/dvm/value.hpp:87-101`): returns false if tags differ; for matching tags, compares i64/bool by `.i64`, f64 by `.f64`, Null/Undef returns true, default compares `.raw`.
**Reasoning:** Spec §14: comparisons produce boolean results. Format R_R_R: `opcode | dst | src0 | src1`. The handler reads `cell.s1()` as dst, `cell.s2()` as src0, `cell.s3()` as src1, and writes `Value::boolean(a == b)` to dst. The equality semantics are reasonable: same-type comparisons compare the payload; cross-type comparisons return false; all nulls are equal; all undefs are equal. Format and semantics match.

---

### Check 19 — JMP (§15.1)

**Verdict:** PASS
**Evidence:** `src/opcodes_control.cpp:25-30`:
```cpp
OpResult op_jmp(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::int32_t delta = signed_delta32(cell.s2(), cell.s3());
  s.advance();  // PC now points at the next instruction
  s.branch(delta);  // apply offset relative to next instruction
  return OpResult::Continue;
}
```
`InterpState::branch` (`include/dvm/state.hpp:84-88`): `current().pc = current().pc + delta_cells`.
`signed_delta32` (`opcodes_control.cpp:19-22`): `static_cast<std::int32_t>(InstrCell::imm32(low, high))`.
**Reasoning:** Spec §15.1: opcode 0x0700, format: JUMP. JUMP (§7.5): `opcode | unused | delta32_low | delta32_high`. The handler reads `cell.s2()` and `cell.s3()` as the delta halves (slot1 = unused, correct per JUMP format). The 32-bit immediate is reconstructed and reinterpreted as signed. Spec §4.1: `target_pc = current_pc + 1 + delta`. The handler advances PC first (pc → pc+1), then applies delta via `branch(delta)` (pc → (pc+1) + delta = pc + 1 + delta). Matches the spec formula exactly. Format and semantics match.

---

### Check 20 — BR_TRUE (§15.2)

**Verdict:** PASS
**Evidence:** `src/opcodes_control.cpp:32-38`:
```cpp
OpResult op_br_true(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::int32_t delta = signed_delta32(cell.s2(), cell.s3());
  bool cond = s.reg(cell.s1()).truthy();
  s.advance();
  if (cond) s.branch(delta);
  return OpResult::Continue;
}
```
`Value::truthy` (`include/dvm/value.hpp:74-84`): Null→false, Undef→false, Bool→i64!=0, Int64→i64!=0, Float64→f64!=0.0, ObjRef→ptr!=nullptr, default→raw!=0.
**Reasoning:** Spec §15.2: opcode 0x0701, format: BRANCH. BRANCH (§7.4): `opcode | cond_reg | delta32_low | delta32_high`. The handler reads `cell.s1()` as the condition register and `cell.s2()/cell.s3()` as the delta halves. Advances PC, then if `cond` is truthy, applies the branch delta (yielding target = pc + 1 + delta, matching §4.1). Truthiness policy is reasonable (Null/Undef/0/0.0/nullptr → false; everything else → true). Format and semantics match.

---

### Check 21 — BR_FALSE (§15.3)

**Verdict:** PASS
**Evidence:** `src/opcodes_control.cpp:40-46`:
```cpp
OpResult op_br_false(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::int32_t delta = signed_delta32(cell.s2(), cell.s3());
  bool cond = s.reg(cell.s1()).truthy();
  s.advance();
  if (!cond) s.branch(delta);
  return OpResult::Continue;
}
```
**Reasoning:** Spec §15.3: opcode 0x0702, format: BRANCH, "Branch if condition register is false." Same format as BR_TRUE; the handler branches when the condition is *not* truthy (i.e., falsy). Format and semantics match.

---

### Check 22 — CALL_DIRECT (§16.3)

**Verdict:** PASS (with simplification)
**Evidence:** `src/opcodes_calls.cpp:12-31`:
```cpp
OpResult op_call_direct(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t ret_reg = cell.s1();
  std::uint32_t call_site = crb::InstrCell::imm32(cell.s2(), cell.s3());
  std::uint32_t fn_id = call_site;  // Minimal: call_site is the function_id
  ...
  s.advance();
  s.push_frame(*fn);
  s.frames.back().caller_ret_reg = ret_reg;
  return OpResult::Continue;
}
```
Spec §16.3: opcode 0x0810, format: CALL. CALL (§7.6): `opcode | ret_reg | call_site32_low | call_site32_high`. Spec §17: "Every call instruction references a call site" with `CRBCallSite { kind, flags, target_id, arg_base, arg_count, ret_count, receiver_reg, dependency_set, trace_hint_id }`.
**Reasoning:** Format CALL matches: `cell.s1()` = ret_reg, `cell.s2()/cell.s3()` = call_site32. The handler advances the caller's PC past the CALL, pushes a new frame for the callee, and records `caller_ret_reg = ret_reg` so the eventual RET knows where in the caller to write the return value (consumed at `opcodes_sys.cpp:36,40`). The basic semantics ("calls a function") match the task's check. **Simplification (non-blocking):** the spec requires the 32-bit immediate to be a *call site index* into a Call Site Table (§17), with the actual function_id stored in `CRBCallSite.target_id`. The implementation skips the call site table and interprets the immediate directly as `fn_id`. This is documented in the code comment ("For the minimal interpreter, we interpret the 32-bit immediate directly as a function_id (no call site table indirection)") and the smoke test (Test 2) emits the immediate as `0, 0, 0` (= fn_id 0). The format and basic call semantics match; the call-site-table indirection is a known minimal-interpreter simplification.

---

### Check 23 — THROW (§20.1)

**Verdict:** PASS (with simplification)
**Evidence:** `src/opcodes_except.cpp:7-16`:
```cpp
OpResult op_throw(InterpState& s, const crb::InstrCell& cell) noexcept {
  s.pending_exception = s.reg(cell.s1());
  s.exited = true;
  s.exit_value = s.pending_exception;
  return OpResult::Trap;
}
```
Spec §20.1: opcode 0x0C00, format: R, "Throws the exception object in the register. The runtime searches for a handler. If no handler exists, the exception propagates according to guest semantics."
**Reasoning:** Format R: `opcode | dst | unused | unused`. The handler reads `cell.s1()` as the exception register, stores it in `pending_exception`, sets `exited = true`, sets `exit_value` to the exception, and returns `Trap` (which exits the interpreter). The format and the basic "throws the exception object" semantics match the task's check. **Simplification (non-blocking):** the spec requires the runtime to search for a handler in the current frame's exception table (§20.2) and only propagate if no handler exists. The implementation does not implement handler search — every THROW terminates the interpreter as an uncaught exception. This is documented in the code comment ("For the minimal interpreter, an uncaught exception terminates execution"). Format and basic semantics match; handler-table search is a known minimal-interpreter simplification.

---

### Check 24 — Build is clean

**Verdict:** PASS
**Evidence:** `cd /home/z/my-project/dgw-core-repo/runtime/interp && make clean && make SAN=1 -j$(nproc) 2>&1 | tail -5`:
```
ar rcs build/libdvm_interp.a build/interp.o build/loader.o build/module.o build/opcodes_arith.o build/opcodes_calls.o build/opcodes_control.o build/opcodes_except.o build/opcodes_move.o build/opcodes_sys.o build/state.o
g++ -std=c++26 ... tests/smoke.cpp -Lbuild -ldvm_interp -o bin/dvm_interp_smoke -fsanitize=address,undefined -fno-omit-frame-pointer
EXIT_CODE=0
```
Piping the full build log through `grep -iE 'warning:|error:'` returns 0 matches.
**Reasoning:** `make SAN=1 -j$(nproc)` exits 0 with zero warnings and zero errors under `-Werror` + ASan + UBSan. 11 .o files + `libdvm_interp.a` + `bin/dvm_interp_smoke` are produced. The `-Wno-pedantic` exception is applied only to `interp.o` (visible in the build line as `-Wno-pedantic -c src/interp.cpp`).

---

### Check 25 — Smoke test passes

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
EXIT_CODE=0
```
**Reasoning:** The smoke binary exits 0. Test 1 (single function: MOV_CONST r0,20; MOV_CONST r1,22; ADD_I64_WRAP r2,r0,r1; RET r2) returns 42. Test 2 (entry fn1: CALL_DIRECT r0,fn0; RET r0; callee fn0: MOV_CONST r0,40; MOV_CONST r1,2; ADD_I64_WRAP r2,r0,r1; RET r2) returns 42. Both pass. The test exercises MOV_CONST, ADD_I64_WRAP, RET, CALL_DIRECT, and the frame-stack push/pop/return-value convention.

---

### Check 26 — No warning suppressions

**Verdict:** PASS
**Evidence:** `grep -rn 'pragma GCC diagnostic\|__attribute__\|(void)' /home/z/my-project/dgw-core-repo/runtime/interp/` returns 0 matches. (Verified via Grep tool.)
**Reasoning:** No source-level warning suppressions exist anywhere in `runtime/interp/` — no `#pragma GCC diagnostic`, no `__attribute__`, no `(void)var;` casts. The only allowed suppression (Makefile-level per-file `-Wno-pedantic` for `interp.cpp`) is documented in `Makefile:47-51` (see Check 7) and is a Makefile-level exception, not a source-level suppression.

---

### Check 27 — Every opcode category has its own .cpp file

**Verdict:** PASS
**Evidence:** `ls /home/z/my-project/dgw-core-repo/runtime/interp/src/`:
```
interp.cpp
loader.cpp
module.cpp
opcodes_arith.cpp
opcodes_calls.cpp
opcodes_control.cpp
opcodes_except.cpp
opcodes_move.cpp
opcodes_sys.cpp
state.cpp
```
**Reasoning:** The six opcode-category files are present and separate:
- `opcodes_sys.cpp` (§9: NOP, TRAP, UNREACHABLE, SAFEPOINT, RET, RET_VOID)
- `opcodes_move.cpp` (§10: MOV_CONST, MOV_NULL, MOV_TRUE, MOV_FALSE, MOV_UNDEF)
- `opcodes_arith.cpp` (§11+14: ADD/SUB/MUL/NEG_I64_WRAP, ADD_I64_CHECKED, CMP_EQ, CMP_LT_S)
- `opcodes_control.cpp` (§15: JMP, BR_TRUE, BR_FALSE, BR_NULL, BR_NONNULL)
- `opcodes_calls.cpp` (§16: CALL_DIRECT)
- `opcodes_except.cpp` (§20: THROW)

No monolithic `opcodes.cpp` exists. The split matches the project rule "every pass gets its own file" (adapted to opcode categories per `opcodes.hpp:10-12`).

---

## 3. Verifier run log

### `make clean && make SAN=1 -j$(nproc) 2>&1 | tail -5`

```
ar rcs build/libdvm_interp.a build/interp.o build/loader.o build/module.o build/opcodes_arith.o build/opcodes_calls.o build/opcodes_control.o build/opcodes_except.o build/opcodes_move.o build/opcodes_sys.o build/state.o
g++ -std=c++26 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wnoexcept -Wundef -Werror -Wno-unused-parameter -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer  -Iinclude tests/smoke.cpp -Lbuild -ldvm_interp -o bin/dvm_interp_smoke -fsanitize=address,undefined -fno-omit-frame-pointer
EXIT_CODE=0
```

Full build log (10 compile commands + 1 ar + 1 link) produced no `warning:` or `error:` lines (verified by `grep -iE 'warning:|error:'` returning 0 matches).

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
EXIT_CODE=0
```

The interpreter exits 0, both Test 1 (20+22=42) and Test 2 (fn1 calls fn0(40+2)=42) print PASS, and the trailing summary line confirms `== DVM Interpreter smoke test PASSED ==`.

---

## 4. Final review status

**Verdict: CHANGES_REQUESTED**

| Verdict | Count | Checks |
|---------|-------|--------|
| PASS    | 26    | 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 (with concern), 17 (with concern), 18, 19, 20, 21, 22 (with simplification), 23 (with simplification), 24, 25, 26, 27 |
| FAIL    | 1     | 2 |

### Blocking FAIL

**Check 2 — Module header matches spec §3.1.** The implementation's `ModuleHeader` (32 bytes, `char magic[4] = "CRB\0"`) does not match the spec's `CRBHeader` (88 bytes, `uint32_t magic = 0x31425243` = "CRB1"). The implementation omits 9 spec-required fields (`guest_profile_hash_lo`, `guest_profile_hash_hi`, `dvm_abi_version`, `extension_count`, `extension_table_offset`, `header_crc32`, `module_crc32`, `reserved_0`, `reserved_1`), adds 2 non-spec fields (`constant_pool_offset`, `string_pool_offset`), and uses the wrong magic value (the 4th byte is `0x00` instead of `0x31`). A spec-compliant CRB binary written with magic `0x31425243` would be **rejected** by the loader's `h.magic[3] != 0` check at `src/loader.cpp:36`. The smoke test only passes because it patches the magic to `"CRB\0"` to match the implementation's (non-spec) expectation. The producer must reconcile the implementation to spec §3.1 (or amend the spec to define a 32-byte header — but the spec is the authority per the task's pre-work step 2).

### Non-blocking concerns (do not affect verdict, but should be tracked)

1. **ADD_I64_WRAP / SUB_I64_WRAP signed-overflow UB** (Checks 16, 17). The C++ expression `int64_t + int64_t` / `int64_t - int64_t` is undefined behavior on overflow, but the spec mandates well-defined modulo-2^64 wrapping. With `-fsanitize=undefined` enabled (which `SAN=1` activates), any CRB program exercising wrap would trip UBSan. Fix: cast through `uint64_t` (`static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b))`). The smoke test does not trigger overflow, so the build and test pass.
2. **CALL_DIRECT skips the Call Site Table** (Check 22). Spec §17 requires the 32-bit immediate to be a *call site index* into a `CRBCallSite` table whose `target_id` field holds the actual function_id. The implementation interprets the immediate directly as `fn_id`. Documented minimal-interpreter simplification. Acceptable for Tier 0 baseline; should be addressed before the interpreter is used with real CRB binaries.
3. **THROW does not search the exception table** (Check 23). Spec §20.1/§20.2 require the runtime to search the current frame's exception table for a handler. The implementation terminates the interpreter on every THROW (treats all throws as uncaught). Documented minimal-interpreter simplification. Acceptable for Tier 0 baseline.

### Required producer follow-up

1. **Address Check 2 FAIL**: align `ModuleHeader` with spec §3.1's `CRBHeader` (88-byte layout, `uint32_t magic = 0x31425243` = `"CRB1"`, all 14 spec fields). Update `loader.cpp`'s magic check to accept `0x31425243`. Update `tests/smoke.cpp` to emit magic `"CRB1"`. Re-request review with a new commit.
2. **Recommended**: address the signed-overflow UB concern in `op_add_i64_wrap` and `op_sub_i64_wrap` (and the same pattern in `op_mul_i64_wrap` and `op_neg_i64_wrap`) so the wrap semantics are well-defined under UBSan.

---

## 5. Reviewer agent ID

`review-agent-010`

## 6. UTC timestamp

`2026-09-03T17:17:36Z`
