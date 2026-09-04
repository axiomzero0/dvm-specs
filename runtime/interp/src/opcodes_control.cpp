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
// When a TraceRecorder is attached to InterpState, branch handlers mark
// side exits: a taken branch ends the trace (the taken path is the side
// exit), and a not-taken branch also ends the trace (the fall-through is
// a different path). For loop tracing, a back-branch to the trace head
// is marked as LoopClose instead of BranchTaken.
//
#include "dvm/opcodes.hpp"
#include "dvm/trace.hpp"

namespace dvm {

namespace {
// Reconstruct a signed 32-bit delta from two 16-bit halves.
std::int32_t signed_delta32(std::uint16_t low, std::uint16_t high) noexcept {
  std::uint32_t u = crb::InstrCell::imm32(low, high);
  return static_cast<std::int32_t>(u);
}

// Mark a side exit on the recorder (if attached and recording).
void mark_branch_exit(InterpState& s, ExitReason reason) noexcept {
  if (s.recorder && s.recorder->is_recording()) {
    s.recorder->mark_exit(reason, s.current().pc,
                           static_cast<std::uint32_t>(s.frames.size()));
  }
}

// Check if a taken branch is a backedge to the trace head (loop close).
// If so, mark LoopClose; otherwise mark BranchTaken.
void mark_branch_or_loop(InterpState& s, std::int32_t delta) noexcept {
  if (!s.recorder || !s.recorder->is_recording()) return;
  // Compute the target PC (after advance, so current.pc is the next instr).
  std::int64_t target = static_cast<std::int64_t>(s.current().pc) + delta;
  if (target == static_cast<std::int64_t>(s.recorder->fragment().entry_pc)) {
    s.recorder->mark_loop_close(static_cast<std::uint32_t>(target));
  } else {
    s.recorder->mark_exit(ExitReason::BranchTaken, s.current().pc,
                           static_cast<std::uint32_t>(s.frames.size()));
  }
}
}  // namespace

OpResult op_jmp(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::int32_t delta = signed_delta32(cell.s2(), cell.s3());
  s.advance();
  // A JMP is always "taken" — mark it as a branch exit or loop close.
  if (s.recorder && s.recorder->is_recording()) {
    mark_branch_or_loop(s, delta);
  }
  s.branch(delta);
  return OpResult::Continue;
}

OpResult op_br_true(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::int32_t delta = signed_delta32(cell.s2(), cell.s3());
  bool cond = s.reg(cell.s1()).truthy();
  s.advance();
  if (cond) {
    if (s.recorder && s.recorder->is_recording()) {
      mark_branch_or_loop(s, delta);
    }
    s.branch(delta);
  } else {
    // Branch not taken → side exit (fall-through is a different path).
    mark_branch_exit(s, ExitReason::BranchNotTaken);
  }
  return OpResult::Continue;
}

OpResult op_br_false(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::int32_t delta = signed_delta32(cell.s2(), cell.s3());
  bool cond = s.reg(cell.s1()).truthy();
  s.advance();
  if (!cond) {
    if (s.recorder && s.recorder->is_recording()) {
      mark_branch_or_loop(s, delta);
    }
    s.branch(delta);
  } else {
    mark_branch_exit(s, ExitReason::BranchNotTaken);
  }
  return OpResult::Continue;
}

OpResult op_br_null(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::int32_t delta = signed_delta32(cell.s2(), cell.s3());
  bool is_null = s.reg(cell.s1()).tag == TypeTag::Null;
  s.advance();
  if (is_null) {
    if (s.recorder && s.recorder->is_recording()) {
      mark_branch_or_loop(s, delta);
    }
    s.branch(delta);
  } else {
    mark_branch_exit(s, ExitReason::BranchNotTaken);
  }
  return OpResult::Continue;
}

OpResult op_br_nonnull(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::int32_t delta = signed_delta32(cell.s2(), cell.s3());
  bool is_null = s.reg(cell.s1()).tag == TypeTag::Null;
  s.advance();
  if (!is_null) {
    if (s.recorder && s.recorder->is_recording()) {
      mark_branch_or_loop(s, delta);
    }
    s.branch(delta);
  } else {
    mark_branch_exit(s, ExitReason::BranchNotTaken);
  }
  return OpResult::Continue;
}

}  // namespace dvm
