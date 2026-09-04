// src/opcodes_except.cpp — Exception opcode handlers (CRB §20).
//
#include "dvm/opcodes.hpp"
#include "dvm/trace.hpp"

namespace dvm {

OpResult op_throw(InterpState& s, const crb::InstrCell& cell) noexcept {
  s.pending_exception = s.reg(cell.s1());
  s.exited = true;
  s.exit_value = s.pending_exception;
  if (s.recorder && s.recorder->is_recording()) {
    s.recorder->mark_exit(ExitReason::Trap, s.current().pc,
                           static_cast<std::uint32_t>(s.frames.size()));
  }
  return OpResult::Trap;
}

}  // namespace dvm
