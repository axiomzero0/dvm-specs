// src/opcodes_sys.cpp — System opcode handlers (CRB §9).
//
#include "dvm/opcodes.hpp"

namespace dvm {

OpResult op_nop(InterpState& s, const crb::InstrCell& /*cell*/) noexcept {
  s.advance();
  return OpResult::Continue;
}

OpResult op_trap(InterpState& s, const crb::InstrCell& /*cell*/) noexcept {
  s.exited = true;
  s.exit_value = Value::null();
  return OpResult::Trap;
}

OpResult op_unreachable(InterpState& s, const crb::InstrCell& /*cell*/) noexcept {
  // UNREACHABLE executed is a VM bug or invalid module.
  s.exited = true;
  s.exit_value = Value::null();
  return OpResult::Trap;
}

OpResult op_safepoint(InterpState& s, const crb::InstrCell& /*cell*/) noexcept {
  // In Tier 0, safepoints are no-ops (the interpreter is inherently safe).
  s.advance();
  return OpResult::Continue;
}

OpResult op_ret(InterpState& s, const crb::InstrCell& cell) noexcept {
  // RET format: R — single register holding the return value.
  Value ret_val = s.reg(cell.s1());
  s.exit_value = ret_val;
  // Capture the caller's return register before popping.
  std::uint16_t caller_ret = s.frames.back().caller_ret_reg;
  s.pop_frame();
  if (s.exited) return OpResult::Exit;
  // Store the return value into the caller's return register.
  s.frames.back().registers[caller_ret] = ret_val;
  return OpResult::Continue;
}

OpResult op_ret_void(InterpState& s, const crb::InstrCell& /*cell*/) noexcept {
  s.exit_value = Value::null();
  s.pop_frame();
  if (s.exited) return OpResult::Exit;
  return OpResult::Continue;
}

}  // namespace dvm
