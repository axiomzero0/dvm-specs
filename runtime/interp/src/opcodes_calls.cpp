// src/opcodes_calls.cpp — Call opcode handlers (CRB §16).
//
// CALL_DIRECT format (§7.6): CALL — opcode | ret_reg | call_site32_low | call_site32_high
// The call site 32-bit immediate is an index into the Call Site Table
// (§17). For the minimal interpreter, we interpret the 32-bit immediate
// directly as a function_id (no call site table indirection).
//
#include "dvm/opcodes.hpp"

namespace dvm {

OpResult op_call_direct(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t ret_reg = cell.s1();
  std::uint32_t call_site = crb::InstrCell::imm32(cell.s2(), cell.s3());

  // Minimal calling convention: the call_site is the function_id.
  std::uint32_t fn_id = call_site;
  if (!s.module) { s.exit_value = Value::null(); return OpResult::Trap; }
  const crb::FunctionEntry* fn = s.module->find_function(fn_id);
  if (!fn) { s.exit_value = Value::null(); return OpResult::Trap; }

  // Advance the caller's PC past the CALL instruction.
  s.advance();

  // Push a new frame for the callee. The new frame's caller_ret_reg
  // records where the return value should go in the caller.
  s.push_frame(*fn);
  s.frames.back().caller_ret_reg = ret_reg;

  return OpResult::Continue;
}

}  // namespace dvm
