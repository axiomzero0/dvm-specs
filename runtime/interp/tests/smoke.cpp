// tests/smoke.cpp — Build a tiny CRB module in memory, run it, verify.
//
// This test constructs a CRB module binary in memory, loads it with the
// DVM loader, runs it through the computed-goto interpreter, and verifies
// the result. The module implements:
//
//   function 0 (entry):
//     MOV_CONST r0, const(0)    ; r0 = 20
//     MOV_CONST r1, const(1)    ; r1 = 22
//     ADD_I64_WRAP r2, r0, r1   ; r2 = 42
//     RET r2                     ; return 42
//
// And a second test with a function call:
//
//   function 1 (entry):
//     CALL_DIRECT r0, fn(0)     ; r0 = call fn(0)
//     RET r0                     ; return r0
//
//   function 0 (callee):
//     MOV_CONST r0, const(0)    ; r0 = 40
//     MOV_CONST r1, const(1)    ; r1 = 2
//     ADD_I64_WRAP r2, r0, r1   ; r2 = 42
//     RET r2
//
#include <algorithm>
#include <cstring>
#include <print>
#include <vector>

#include "dvm/crb.hpp"
#include "dvm/interp.hpp"
#include "dvm/loader.hpp"
#include "dvm/opcodes_def.hpp"

using namespace dvm;
using namespace dvm::crb;

namespace {

// ---- Build an instruction cell ------------------------------------------
constexpr InstrCell cell(std::uint16_t op, std::uint16_t s1 = 0,
                          std::uint16_t s2 = 0, std::uint16_t s3 = 0) {
  return InstrCell{op, s1, s2, s3};
}

// ---- Build a constant pool entry for an I64 value -----------------------
ConstantEntry i64_const(std::int64_t v) {
  ConstantEntry e{};
  e.kind = static_cast<std::uint32_t>(ConstantKind::I64);
  e.flags = 0;
  e.payload_lo = static_cast<std::uint64_t>(v);
  e.payload_hi = 0;
  return e;
}

// ---- Build a function table entry ---------------------------------------
FunctionEntry make_fn(std::uint32_t id, std::uint32_t code_off,
                      std::uint32_t code_len, std::uint16_t n_regs) {
  FunctionEntry f{};
  f.function_id = id;
  f.name_string_id = 0;
  f.code_offset = code_off;
  f.code_length = code_len;
  f.param_count = 0;
  f.register_count = n_regs;
  f.return_count = 1;
  f.flags = 0;
  return f;
}

// ---- Serialize a module binary ------------------------------------------
// Layout:
//   [0 .. 32)              ModuleHeader
//   [32 .. 32+3*16)       SectionEntry × 3 (Code, ConstantPool, FunctionTable)
//   [80 .. 80+code_sz)    Code section
//   [80+code_sz .. +cp_sz) ConstantPool section
//   [80+code_sz+cp_sz ..) FunctionTable section
//
struct ModuleBuilder {
  std::vector<std::byte> raw;
  std::size_t header_end = 32;
  std::size_t sect_table_end = 32 + 3 * 16;  // 3 sections

  void emit_u8(std::uint8_t v) { raw.push_back(static_cast<std::byte>(v)); }
  void emit_u16(std::uint16_t v) {
    raw.push_back(static_cast<std::byte>(v & 0xFF));
    raw.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
  }
  void emit_u32(std::uint32_t v) {
    emit_u16(static_cast<std::uint16_t>(v & 0xFFFF));
    emit_u16(static_cast<std::uint16_t>((v >> 16) & 0xFFFF));
  }
  void emit_u64(std::uint64_t v) {
    emit_u32(static_cast<std::uint32_t>(v & 0xFFFFFFFF));
    emit_u32(static_cast<std::uint32_t>((v >> 32) & 0xFFFFFFFF));
  }
  void emit_bytes(const void* p, std::size_t n) {
    const auto* b = static_cast<const std::byte*>(p);
    raw.insert(raw.end(), b, b + n);
  }

  void pad_to(std::size_t alignment) {
    while (raw.size() % alignment != 0) emit_u8(0);
  }

  // Write the 88-byte CRB §3.1 header at offset 0.
  void write_header(std::uint32_t section_count, std::uint64_t sect_table_off) {
    // Zero the 88 bytes first.
    std::fill(raw.begin(), raw.begin() + 88, std::byte{0});
    // Construct the header in a local and memcpy it in.
    ModuleHeader h{};
    h.magic = kMagicValue;           // "CRB1" LE = 0x31425243
    h.version_major = kVersionMajor;
    h.version_minor = kVersionMinor;
    h.flags = 0;
    h.guest_profile_hash_lo = 0;
    h.guest_profile_hash_hi = 0;
    h.dvm_abi_version = 0;
    h.extension_count = 0;
    h.extension_table_offset = 0;
    h.section_count = section_count;
    h.section_table_offset = sect_table_off;
    h.header_crc32 = 0;
    h.module_crc32 = 0;
    h.reserved_0 = 0;
    h.reserved_1 = 0;
    std::memcpy(raw.data(), &h, sizeof(h));
  }
};

// Build a module for Test 1: a single function that adds two constants.
std::vector<std::byte> build_module_test1() {
  ModuleBuilder b;
  // Code section: 4 instructions = 32 bytes
  InstrCell code1[4] = {
    cell(op::MOV_CONST,    0, 0, 0),   // r0 = const(0) = 20
    cell(op::MOV_CONST,    1, 1, 0),   // r1 = const(1) = 22
    cell(op::ADD_I64_WRAP, 2, 0, 1),   // r2 = r0 + r1 = 42
    cell(op::RET,          2),          // return r2
  };
  std::size_t code_sz = sizeof(code1);

  // Constant pool: 2 entries × 24 bytes = 48 bytes
  ConstantEntry consts[2] = {
    i64_const(20),
    i64_const(22),
  };
  std::size_t cp_sz = sizeof(consts);

  // Function table: 1 entry × 24 bytes = 24 bytes
  FunctionEntry fns[1] = {
    make_fn(0, 0, static_cast<std::uint32_t>(code_sz), 3),
  };
  std::size_t ft_sz = sizeof(fns);

  // Layout:
  //   [0 .. 88)    header (88 bytes, patched last)
  //   [88 .. 136)  section table (3 entries × 16 bytes = 48 bytes)
  //   [136 .. +code_sz)  code
  //   [136+code_sz .. +cp_sz)  constant pool
  //   [136+code_sz+cp_sz .. +ft_sz) function table
  std::size_t code_offset = 136;
  std::size_t cp_offset   = code_offset + code_sz;
  std::size_t ft_offset   = cp_offset + cp_sz;

  // Emit header placeholder (88 bytes of zeros — patched later).
  for (std::size_t i = 0; i < 88; ++i) b.emit_u8(0);
  // Emit section table entries (3 × 16 bytes).
  b.emit_u32(static_cast<std::uint32_t>(SectionType::Code));
  b.emit_u32(static_cast<std::uint32_t>(code_offset));
  b.emit_u32(static_cast<std::uint32_t>(code_sz));
  b.emit_u32(0);
  b.emit_u32(static_cast<std::uint32_t>(SectionType::ConstantPool));
  b.emit_u32(static_cast<std::uint32_t>(cp_offset));
  b.emit_u32(static_cast<std::uint32_t>(cp_sz));
  b.emit_u32(0);
  b.emit_u32(static_cast<std::uint32_t>(SectionType::FunctionTable));
  b.emit_u32(static_cast<std::uint32_t>(ft_offset));
  b.emit_u32(static_cast<std::uint32_t>(ft_sz));
  b.emit_u32(0);
  // Emit code section.
  b.emit_bytes(code1, code_sz);
  // Emit constant pool.
  b.emit_bytes(consts, cp_sz);
  // Emit function table.
  b.emit_bytes(fns, ft_sz);

  // Patch the header at offset 0 (88 bytes per CRB §3.1).
  b.write_header(3, 88);

  return b.raw;
}

// Build a module for Test 2: function 1 calls function 0.
std::vector<std::byte> build_module_test2() {
  ModuleBuilder b;
  // Function 0 (callee): loads 40 + 2 = 42, returns 42
  InstrCell code_fn0[4] = {
    cell(op::MOV_CONST,    0, 0, 0),   // r0 = const(0) = 40
    cell(op::MOV_CONST,    1, 1, 0),   // r1 = const(1) = 2
    cell(op::ADD_I64_WRAP, 2, 0, 1),   // r2 = 42
    cell(op::RET,          2),          // return r2
  };
  // Function 1 (entry): calls fn(0), returns result
  InstrCell code_fn1[2] = {
    cell(op::CALL_DIRECT, 0, 0, 0),   // r0 = call fn(0)
    cell(op::RET,         0),          // return r0
  };
  std::size_t code_fn0_sz = sizeof(code_fn0);
  std::size_t code_fn1_sz = sizeof(code_fn1);
  std::size_t code_total = code_fn0_sz + code_fn1_sz;

  ConstantEntry consts[2] = {
    i64_const(40),
    i64_const(2),
  };
  std::size_t cp_sz = sizeof(consts);

  FunctionEntry fns[2] = {
    make_fn(0, 0,  static_cast<std::uint32_t>(code_fn0_sz), 3),
    make_fn(1, static_cast<std::uint32_t>(code_fn0_sz),
              static_cast<std::uint32_t>(code_fn1_sz), 1),
  };
  std::size_t ft_sz = sizeof(fns);

  std::size_t code_offset = 136;
  std::size_t cp_offset   = code_offset + code_total;
  std::size_t ft_offset   = cp_offset + cp_sz;

  // Emit header placeholder (88 bytes of zeros — patched later).
  for (std::size_t i = 0; i < 88; ++i) b.emit_u8(0);
  b.emit_u32(static_cast<std::uint32_t>(SectionType::Code));
  b.emit_u32(static_cast<std::uint32_t>(code_offset));
  b.emit_u32(static_cast<std::uint32_t>(code_total));
  b.emit_u32(0);
  b.emit_u32(static_cast<std::uint32_t>(SectionType::ConstantPool));
  b.emit_u32(static_cast<std::uint32_t>(cp_offset));
  b.emit_u32(static_cast<std::uint32_t>(cp_sz));
  b.emit_u32(0);
  b.emit_u32(static_cast<std::uint32_t>(SectionType::FunctionTable));
  b.emit_u32(static_cast<std::uint32_t>(ft_offset));
  b.emit_u32(static_cast<std::uint32_t>(ft_sz));
  b.emit_u32(0);

  b.emit_bytes(code_fn0, code_fn0_sz);
  b.emit_bytes(code_fn1, code_fn1_sz);
  b.emit_bytes(consts, cp_sz);
  b.emit_bytes(fns, ft_sz);

  // Patch the 88-byte header at offset 0.
  b.write_header(3, 88);

  return b.raw;
}

}  // namespace

int main() {
  std::println("== DVM Tier 0 Interpreter smoke test ==");

  // ---- Test 1: simple arithmetic ---------------------------------------
  std::println("\n-- Test 1: MOV_CONST + ADD_I64_WRAP + RET --");
  auto raw1 = build_module_test1();
  auto lr1 = load_module(raw1);
  if (!lr1.ok) {
    std::println("FAIL: load_module test 1: {}", lr1.error);
    return 1;
  }
  std::println("Loaded: {} code cells, {} constants, {} functions",
               lr1.module.code.size(), lr1.module.constants.size(),
               lr1.module.functions.size());

  Value result1 = interpret(lr1.module, 0);
  if (result1.tag != TypeTag::Int64) {
    std::println("FAIL: result tag is {} (expected Int64)", static_cast<int>(result1.tag));
    return 1;
  }
  if (result1.as_i64() != 42) {
    std::println("FAIL: result is {} (expected 42)", result1.as_i64());
    return 1;
  }
  std::println("Test 1: 20 + 22 = {} (PASS)", result1.as_i64());

  // ---- Test 2: function call -------------------------------------------
  std::println("\n-- Test 2: CALL_DIRECT + RET --");
  auto raw2 = build_module_test2();
  auto lr2 = load_module(raw2);
  if (!lr2.ok) {
    std::println("FAIL: load_module test 2: {}", lr2.error);
    return 1;
  }
  std::println("Loaded: {} code cells, {} constants, {} functions",
               lr2.module.code.size(), lr2.module.constants.size(),
               lr2.module.functions.size());

  Value result2 = interpret(lr2.module, 1);  // entry = function 1
  if (result2.tag != TypeTag::Int64) {
    std::println("FAIL: result tag is {} (expected Int64)", static_cast<int>(result2.tag));
    return 1;
  }
  if (result2.as_i64() != 42) {
    std::println("FAIL: result is {} (expected 42)", result2.as_i64());
    return 1;
  }
  std::println("Test 2: fn1 calls fn0(40 + 2) = {} (PASS)", result2.as_i64());

  std::println("\n== DVM Interpreter smoke test PASSED ==");
  return 0;
}
