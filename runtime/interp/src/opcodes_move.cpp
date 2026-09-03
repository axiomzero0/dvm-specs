// src/opcodes_move.cpp — Move/constant opcode handlers (CRB §10).
//
#include "dvm/opcodes.hpp"

namespace dvm {

OpResult op_mov_const(InterpState& s, const crb::InstrCell& cell) noexcept {
  // MOV_CONST format: R_IMM32 — dst = s1, constant pool index = imm32(s2, s3).
  std::uint16_t dst = cell.s1();
  std::uint32_t const_idx = crb::InstrCell::imm32(cell.s2(), cell.s3());
  // Load the constant from the module's constant pool.
  if (!s.module || const_idx >= s.module->constants.size()) {
    s.exit_value = Value::null();
    return OpResult::Trap;
  }
  const auto& ce = s.module->constants[const_idx];
  using CK = crb::ConstantKind;
  Value v;
  switch (static_cast<CK>(ce.kind)) {
    case CK::Null:    v = Value::null(); break;
    case CK::Bool:    v = Value::boolean(ce.payload_lo != 0); break;
    case CK::I8:
    case CK::I16:
    case CK::I32:
    case CK::I64:
    case CK::U8:
    case CK::U16:
    case CK::U32:
    case CK::U64:
      v = Value{static_cast<std::int64_t>(ce.payload_lo)};
      break;
    case CK::F32:
    case CK::F64: {
      double d;
      std::memcpy(&d, &ce.payload_lo, sizeof(d));
      v = Value{d};
      break;
    }
    default:
      // For now, other kinds are treated as null.
      v = Value::null();
      break;
  }
  s.reg(dst) = v;
  s.advance();
  return OpResult::Continue;
}

OpResult op_mov_null(InterpState& s, const crb::InstrCell& cell) noexcept {
  s.reg(cell.s1()) = Value::null();
  s.advance();
  return OpResult::Continue;
}

OpResult op_mov_true(InterpState& s, const crb::InstrCell& cell) noexcept {
  s.reg(cell.s1()) = Value::boolean(true);
  s.advance();
  return OpResult::Continue;
}

OpResult op_mov_false(InterpState& s, const crb::InstrCell& cell) noexcept {
  s.reg(cell.s1()) = Value::boolean(false);
  s.advance();
  return OpResult::Continue;
}

OpResult op_mov_undef(InterpState& s, const crb::InstrCell& cell) noexcept {
  s.reg(cell.s1()) = Value::undef();
  s.advance();
  return OpResult::Continue;
}

}  // namespace dvm
