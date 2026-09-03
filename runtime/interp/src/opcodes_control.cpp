// src/opcodes_control.cpp — Control flow handlers (CRB §15).
//
// Formats (§7):
//   JMP:      JUMP — opcode | unused | delta32_low | delta32_high
//   BR_TRUE:  BRANCH — opcode | cond_reg | delta32_low | delta32_high
//   BR_FALSE: BRANCH
//   BR_NULL:  BRANCH
//   BR_NONNULL: BRANCH
//
// delta32 is a signed 32-bit branch offset in INSTRUCTION CELLS (not bytes).
// The offset is relative to the NEXT instruction (the one after the branch).
//
#include "dvm/opcodes.hpp"

namespace dvm {

namespace {
// Reconstruct a signed 32-bit delta from two 16-bit halves.
std::int32_t signed_delta32(std::uint16_t low, std::uint16_t high) noexcept {
  std::uint32_t u = crb::InstrCell::imm32(low, high);
  return static_cast<std::int32_t>(u);
}
}  // namespace

OpResult op_jmp(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::int32_t delta = signed_delta32(cell.s2(), cell.s3());
  s.advance();  // PC now points at the next instruction
  s.branch(delta);  // apply offset relative to next instruction
  return OpResult::Continue;
}

OpResult op_br_true(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::int32_t delta = signed_delta32(cell.s2(), cell.s3());
  bool cond = s.reg(cell.s1()).truthy();
  s.advance();
  if (cond) s.branch(delta);
  return OpResult::Continue;
}

OpResult op_br_false(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::int32_t delta = signed_delta32(cell.s2(), cell.s3());
  bool cond = s.reg(cell.s1()).truthy();
  s.advance();
  if (!cond) s.branch(delta);
  return OpResult::Continue;
}

OpResult op_br_null(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::int32_t delta = signed_delta32(cell.s2(), cell.s3());
  bool is_null = s.reg(cell.s1()).tag == TypeTag::Null;
  s.advance();
  if (is_null) s.branch(delta);
  return OpResult::Continue;
}

OpResult op_br_nonnull(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::int32_t delta = signed_delta32(cell.s2(), cell.s3());
  bool is_null = s.reg(cell.s1()).tag == TypeTag::Null;
  s.advance();
  if (!is_null) s.branch(delta);
  return OpResult::Continue;
}

}  // namespace dvm
