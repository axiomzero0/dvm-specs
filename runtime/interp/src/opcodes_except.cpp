// src/opcodes_except.cpp — Exception opcode handlers (CRB §20).
//
#include "dvm/opcodes.hpp"

namespace dvm {

OpResult op_throw(InterpState& s, const crb::InstrCell& cell) noexcept {
  // THROW format: R — single register holding the exception object.
  s.pending_exception = s.reg(cell.s1());
  s.exited = true;
  s.exit_value = s.pending_exception;
  // A full implementation would search for a handler in the current
  // frame's exception table. For the minimal interpreter, an uncaught
  // exception terminates execution.
  return OpResult::Trap;
}

}  // namespace dvm
