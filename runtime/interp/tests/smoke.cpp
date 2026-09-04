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
#include "dvm/trace.hpp"
#include "dvm/hotness.hpp"
#include "dvm/lifter.hpp"

#include "dgw/graph.hpp"
#include "dgw/kinds.hpp"

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

  // ---- Test 3: counting loop (BR_TRUE + JMP back) -----------------------
  // Counts from 0 to 10 using a backward branch.
  // Code layout (7 instructions):
  //   0: MOV_CONST r0, const(0)    ; r0 = 0  (counter)
  //   1: MOV_CONST r1, const(1)    ; r1 = 1  (increment)
  //   2: MOV_CONST r2, const(10)  ; r2 = 10 (limit)
  //   3: ADD_I64_WRAP r0, r0, r1  ; r0 += 1
  //   4: CMP_LT_S r3, r0, r2       ; r3 = (r0 < 10)
  //   5: BR_TRUE r3, -3            ; if r3: goto 3 (delta=-3 from next PC=6 → PC=3)
  //   6: RET r0                    ; return 10
  std::println("\n-- Test 3: counting loop (BR_TRUE + JMP back) --");
  {
    ModuleBuilder b;
    InstrCell code[7] = {
      cell(op::MOV_CONST,    0, 0, 0),
      cell(op::MOV_CONST,    1, 1, 0),
      cell(op::MOV_CONST,    2, 2, 0),
      cell(op::ADD_I64_WRAP, 0, 0, 1),
      cell(op::CMP_LT_S,     3, 0, 2),
      // BR_TRUE r3, delta=-3: delta32_low=0xFFFD, delta32_high=0xFFFF
      cell(op::BR_TRUE,      3, 0xFFFD, 0xFFFF),
      cell(op::RET,          0),
    };
    std::size_t code_sz = sizeof(code);
    ConstantEntry consts[3] = {
      i64_const(0),   // const 0 = counter init
      i64_const(1),   // const 1 = increment
      i64_const(10), // const 2 = limit
    };
    std::size_t cp_sz = sizeof(consts);
    FunctionEntry fns[1] = {
      make_fn(0, 0, static_cast<std::uint32_t>(code_sz), 4),
    };
    std::size_t ft_sz = sizeof(fns);

    std::size_t code_offset = 136;
    std::size_t cp_offset   = code_offset + code_sz;
    std::size_t ft_offset   = cp_offset + cp_sz;

    for (std::size_t i = 0; i < 88; ++i) b.emit_u8(0);
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
    b.emit_bytes(code, code_sz);
    b.emit_bytes(consts, cp_sz);
    b.emit_bytes(fns, ft_sz);
    b.write_header(3, 88);

    auto raw3 = b.raw;
    auto lr3 = load_module(raw3);
    if (!lr3.ok) {
      std::println("FAIL: load_module test 3: {}", lr3.error);
      return 1;
    }
    Value result3 = interpret(lr3.module, 0);
    if (result3.tag != TypeTag::Int64 || result3.as_i64() != 10) {
      std::println("FAIL: result is {} (expected 10)", result3.as_i64());
      return 1;
    }
    std::println("Test 3: count to 10 = {} (PASS)", result3.as_i64());
  }

  // ---- Test 4: object alloc + set + get ----------------------------------
  // Allocates an object with 2 fields, sets field 0 = 42, reads it back.
  // Code:
  //   0: ALLOC r0, 2              ; r0 = new Obj(2 fields)
  //   1: MOV_CONST r1, const(0)  ; r1 = 42
  //   2: OBJ_SET r0, 0, r1       ; r0.field[0] = 42
  //   3: OBJ_GET r2, r0, 0       ; r2 = r0.field[0]
  //   4: RET r2
  std::println("\n-- Test 4: ALLOC + OBJ_SET + OBJ_GET --");
  {
    ModuleBuilder b;
    InstrCell code[5] = {
      cell(op::ALLOC,      0, 2, 0),   // r0 = alloc(2 fields)
      cell(op::MOV_CONST,  1, 0, 0),   // r1 = const(0) = 42
      cell(op::OBJ_SET,    0, 0, 1),   // r0.field[0] = r1
      cell(op::OBJ_GET,    2, 0, 0),   // r2 = r0.field[0]
      cell(op::RET,        2),
    };
    std::size_t code_sz = sizeof(code);
    ConstantEntry consts[1] = { i64_const(42) };
    std::size_t cp_sz = sizeof(consts);
    FunctionEntry fns[1] = {
      make_fn(0, 0, static_cast<std::uint32_t>(code_sz), 3),
    };
    std::size_t ft_sz = sizeof(fns);

    std::size_t code_offset = 136;
    std::size_t cp_offset   = code_offset + code_sz;
    std::size_t ft_offset   = cp_offset + cp_sz;

    for (std::size_t i = 0; i < 88; ++i) b.emit_u8(0);
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
    b.emit_bytes(code, code_sz);
    b.emit_bytes(consts, cp_sz);
    b.emit_bytes(fns, ft_sz);
    b.write_header(3, 88);

    auto raw4 = b.raw;
    auto lr4 = load_module(raw4);
    if (!lr4.ok) {
      std::println("FAIL: load_module test 4: {}", lr4.error);
      return 1;
    }
    Value result4 = interpret(lr4.module, 0);
    if (result4.tag != TypeTag::Int64 || result4.as_i64() != 42) {
      std::println("FAIL: result is {} (expected 42)", result4.as_i64());
      return 1;
    }
    std::println("Test 4: ALLOC + OBJ_SET(42) + OBJ_GET = {} (PASS)", result4.as_i64());
  }

  // ---- Test 5: trace recording of the counting loop ---------------------
  // Runs the Test 3 loop module with a trace recorder attached.
  // The loop starts at PC=3 (the ADD_I64_WRAP inside the loop body);
  // the trace should record one iteration and close when BR_TRUE takes
  // the backedge to PC=3.
  //
  // Since the interpreter's recorder.start() uses entry_pc=0 (the
  // function's first instruction), the trace will record from PC=0
  // through the first loop iteration's backedge. The trace should
  // close with ExitReason::LoopClose at PC=3 (the backedge target).
  std::println("\n-- Test 5: trace recording of counting loop --");
  {
    // Reuse the Test 3 module builder.
    ModuleBuilder b;
    InstrCell code[7] = {
      cell(op::MOV_CONST,    0, 0, 0),
      cell(op::MOV_CONST,    1, 1, 0),
      cell(op::MOV_CONST,    2, 2, 0),
      cell(op::ADD_I64_WRAP, 0, 0, 1),
      cell(op::CMP_LT_S,     3, 0, 2),
      cell(op::BR_TRUE,      3, 0xFFFD, 0xFFFF),
      cell(op::RET,          0),
    };
    std::size_t code_sz = sizeof(code);
    ConstantEntry consts[3] = {
      i64_const(0),
      i64_const(1),
      i64_const(10),
    };
    std::size_t cp_sz = sizeof(consts);
    FunctionEntry fns[1] = {
      make_fn(0, 0, static_cast<std::uint32_t>(code_sz), 4),
    };
    std::size_t ft_sz = sizeof(fns);

    std::size_t code_offset = 136;
    std::size_t cp_offset   = code_offset + code_sz;
    std::size_t ft_offset   = cp_offset + cp_sz;

    for (std::size_t i = 0; i < 88; ++i) b.emit_u8(0);
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
    b.emit_bytes(code, code_sz);
    b.emit_bytes(consts, cp_sz);
    b.emit_bytes(fns, ft_sz);
    b.write_header(3, 88);

    auto raw5 = b.raw;
    auto lr5 = load_module(raw5);
    if (!lr5.ok) {
      std::println("FAIL: load_module test 5: {}", lr5.error);
      return 1;
    }

    // Run with a trace recorder.
    TraceRecorder recorder;
    Value result5 = interpret(lr5.module, 0, &recorder);
    if (result5.tag != TypeTag::Int64 || result5.as_i64() != 10) {
      std::println("FAIL: result is {} (expected 10)", result5.as_i64());
      return 1;
    }

    // Verify the trace fragment.
    const TraceFragment& frag = recorder.fragment();
    std::println("Trace recorded: {} instructions, exit={}, loop={}",
                 frag.length(),
                 static_cast<int>(frag.exit_reason),
                 frag.is_loop() ? "yes" : "no");

    // The trace should have recorded at least 7 instructions (the
    // initial 3 MOV_CONSTs + one full loop iteration of ADD+CMP+BR_TRUE
    // = 6 instructions, plus the BR_TRUE itself which triggers the
    // LoopClose). The trace closes when BR_TRUE takes the backedge to
    // the entry PC=0 — but entry_pc is 0, and the backedge target is
    // PC=3, so it's NOT a LoopClose; it's a BranchTaken. The trace
    // keeps recording until the RET (which is a Return exit).
    //
    // Actually: the recorder starts at entry_pc=0. The BR_TRUE at
    // PC=5 takes the backedge to PC=3 (delta=-3 from next PC=6 →
    // target=3). Since entry_pc=0 and target=3≠0, mark_branch_or_loop
    // marks it as BranchTaken (side exit), and recording stops.
    //
    // So the trace should:
    //   - Record instructions from PC=0 through PC=5 (6 instructions)
    //   - Exit with BranchTaken at PC=6 (after the BR_TRUE advances)
    //   - NOT be a loop (entry_pc=0 ≠ backedge target=3)
    if (frag.length() < 6) {
      std::println("FAIL: trace too short ({} < 6)", frag.length());
      return 1;
    }
    if (frag.exit_reason != ExitReason::BranchTaken) {
      std::println("FAIL: exit reason is {} (expected BranchTaken={})",
                   static_cast<int>(frag.exit_reason),
                   static_cast<int>(ExitReason::BranchTaken));
      return 1;
    }
    std::println("Test 5: trace of {} instructions, exit=BranchTaken (PASS)",
                 frag.length());

    // Print the trace for verification.
    print_trace(frag);
  }

  // ---- Test 6: hot-loop detection + auto-recording -----------------------
  // Runs the counting loop WITHOUT manually starting the recorder.
  // Instead, attaches a HotnessTracker with threshold=3. After 3 backedges
  // to the loop header (PC=3), the hotness tracker triggers the recorder
  // to start at PC=3. The trace should then record one loop iteration
  // (ADD + CMP + BR_TRUE) and close with LoopClose (backedge to entry_pc=3).
  std::println("\n-- Test 6: hot-loop detection + auto-recording --");
  {
    // Reuse the Test 3 module (7 instructions, counting to 10).
    ModuleBuilder b;
    InstrCell code[7] = {
      cell(op::MOV_CONST,    0, 0, 0),
      cell(op::MOV_CONST,    1, 1, 0),
      cell(op::MOV_CONST,    2, 2, 0),
      cell(op::ADD_I64_WRAP, 0, 0, 1),
      cell(op::CMP_LT_S,     3, 0, 2),
      cell(op::BR_TRUE,      3, 0xFFFD, 0xFFFF),
      cell(op::RET,          0),
    };
    std::size_t code_sz = sizeof(code);
    ConstantEntry consts[3] = {
      i64_const(0),
      i64_const(1),
      i64_const(10),
    };
    std::size_t cp_sz = sizeof(consts);
    FunctionEntry fns[1] = {
      make_fn(0, 0, static_cast<std::uint32_t>(code_sz), 4),
    };
    std::size_t ft_sz = sizeof(fns);

    std::size_t code_offset = 136;
    std::size_t cp_offset   = code_offset + code_sz;
    std::size_t ft_offset   = cp_offset + cp_sz;

    for (std::size_t i = 0; i < 88; ++i) b.emit_u8(0);
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
    b.emit_bytes(code, code_sz);
    b.emit_bytes(consts, cp_sz);
    b.emit_bytes(fns, ft_sz);
    b.write_header(3, 88);

    auto raw6 = b.raw;
    auto lr6 = load_module(raw6);
    if (!lr6.ok) {
      std::println("FAIL: load_module test 6: {}", lr6.error);
      return 1;
    }

    // Run with hotness tracker (threshold=3) and a recorder.
    // The recorder is NOT manually started — the hotness tracker will
    // auto-start it when the loop header (PC=3) hits 3 backedges.
    TraceRecorder recorder;
    HotnessTracker hotness(3);  // threshold = 3 backedges
    Value result6 = interpret(lr6.module, 0, &recorder, &hotness);
    if (result6.tag != TypeTag::Int64 || result6.as_i64() != 10) {
      std::println("FAIL: result is {} (expected 10)", result6.as_i64());
      return 1;
    }

    const TraceFragment& frag = recorder.fragment();
    std::println("Hot-loop trace: {} instructions, exit={}, loop={}",
                 frag.length(),
                 static_cast<int>(frag.exit_reason),
                 frag.is_loop() ? "yes" : "no");
    std::println("  entry_pc={}, loop_head_pc={}",
                 frag.entry_pc, frag.loop_head_pc);

    // The hotness tracker triggers at 3 backedges. The recording starts
    // at PC=3 (the loop header). The first recorded instruction is ADD
    // (PC=3), then CMP (PC=4), then BR_TRUE (PC=5). BR_TRUE takes the
    // backedge to PC=3, which equals entry_pc → LoopClose.
    //
    // Expected: 3 instructions, exit=LoopClose, loop=yes, entry_pc=3.
    if (frag.length() < 3) {
      std::println("FAIL: hot-loop trace too short ({} < 3)", frag.length());
      return 1;
    }
    if (frag.exit_reason != ExitReason::LoopClose) {
      std::println("FAIL: exit reason is {} (expected LoopClose={})",
                   static_cast<int>(frag.exit_reason),
                   static_cast<int>(ExitReason::LoopClose));
      return 1;
    }
    if (!frag.is_loop()) {
      std::println("FAIL: trace should be a loop");
      return 1;
    }
    if (frag.entry_pc != 3) {
      std::println("FAIL: entry_pc is {} (expected 3)", frag.entry_pc);
      return 1;
    }
    std::println("Test 6: hot-loop detected at PC=3, trace of {} instrs, "
                 "LoopClose (PASS)", frag.length());
    print_trace(frag);

    // ---- Test 7: lift the recorded trace into a DGW-Core IR graph --------
    // Takes the trace from Test 6 and lifts it into a DGW-Core graph.
    // The trace has 3 instructions: ADD_I64_WRAP, CMP_LT_S, BR_TRUE (loop).
    // The lifted graph should have at least: START, CONST(0), CONST(1),
    // CONST(10), ADD, CMP_LT, STATE, BRANCH nodes.
    std::println("\n-- Test 7: lift trace into DGW-Core IR graph --");
    {
      dgw::Graph* graph = lift_trace(frag);
      if (!graph) {
        std::println("FAIL: lift_trace returned nullptr");
        return 1;
      }
      std::println("Lifted graph created successfully");
      print_lifted_graph(*graph);
      auto& arena = graph->arena();
      if (arena.node_count() < 5) {
        std::println("FAIL: graph has {} nodes (expected at least 5)",
                     arena.node_count());
        delete graph;
        return 1;
      }
      // Verify the graph has at least one ADD and one CMP node.
      bool has_add = false, has_cmp = false;
      for (std::uint32_t n = 0; n < arena.node_count(); ++n) {
        if (arena.node_kinds[n] == dgw::NodeKind::ADD) has_add = true;
        if (arena.node_kinds[n] == dgw::NodeKind::CMP_LT) has_cmp = true;
      }
      if (!has_add) {
        std::println("FAIL: graph has no ADD node");
        delete graph;
        return 1;
      }
      if (!has_cmp) {
        std::println("FAIL: graph has no CMP_LT node");
        delete graph;
        return 1;
      }
      std::println("Test 7: lifted graph has {} nodes, {} edges, ADD+CMP present (PASS)",
                   arena.node_count(), arena.edge_count());
      delete graph;
    }
  }

  std::println("\n== DVM Interpreter smoke test PASSED ==");
  return 0;
}
