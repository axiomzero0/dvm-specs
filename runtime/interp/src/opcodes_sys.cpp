// src/opcodes_sys.cpp — System opcode handlers (CRB §9).
//
#include "dvm/opcodes.hpp"
#include "dvm/trace.hpp"

namespace dvm {

OpResult op_nop(InterpState& s, const crb::InstrCell& /*cell*/) noexcept {
  s.advance();
  return OpResult::Continue;
}

OpResult op_trap(InterpState& s, const crb::InstrCell& /*cell*/) noexcept {
  if (s.recorder && s.recorder->is_recording()) {
    s.recorder->mark_exit(ExitReason::Trap, s.current().pc,
                           static_cast<std::uint32_t>(s.frames.size()));
  }
  s.exited = true;
  s.exit_value = Value::null();
  return OpResult::Trap;
}

OpResult op_unreachable(InterpState& s, const crb::InstrCell& /*cell*/) noexcept {
  if (s.recorder && s.recorder->is_recording()) {
    s.recorder->mark_exit(ExitReason::Trap, s.current().pc,
                           static_cast<std::uint32_t>(s.frames.size()));
  }
  s.exited = true;
  s.exit_value = Value::null();
  return OpResult::Trap;
}

OpResult op_safepoint(InterpState& s, const crb::InstrCell& /*cell*/) noexcept {
  s.advance();
  return OpResult::Continue;
}

OpResult op_ret(InterpState& s, const crb::InstrCell& cell) noexcept {
  Value ret_val = s.reg(cell.s1());
  s.exit_value = ret_val;
  std::uint16_t caller_ret = s.frames.back().caller_ret_reg;
  // Mark Return exit before popping (so the recorder captures the PC).
  if (s.recorder && s.recorder->is_recording()) {
    s.recorder->mark_exit(ExitReason::Return, s.current().pc,
                           static_cast<std::uint32_t>(s.frames.size()));
  }
  s.pop_frame();
  if (s.exited) return OpResult::Exit;
  s.frames.back().registers[caller_ret] = ret_val;
  return OpResult::Continue;
}

OpResult op_ret_void(InterpState& s, const crb::InstrCell& /*cell*/) noexcept {
  s.exit_value = Value::null();
  if (s.recorder && s.recorder->is_recording()) {
    s.recorder->mark_exit(ExitReason::Return, s.current().pc,
                           static_cast<std::uint32_t>(s.frames.size()));
  }
  s.pop_frame();
  if (s.exited) return OpResult::Exit;
  return OpResult::Continue;
}

}  // namespace dvm
