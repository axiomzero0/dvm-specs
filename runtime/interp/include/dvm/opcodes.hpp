// dvm/opcodes.hpp — Opcode handler declarations.
//
// Spec citation: DVM-CRB.md Sections 9–24 — each opcode's semantics.
//
// Each opcode handler is a free function taking (InterpState& state, const
// InstrCell& cell) and returning an OpResult. The dispatch loop in
// interp.cpp uses computed goto to jump to the right handler label, which
// calls the appropriate function.
//
// Per the project rule "every pass gets its own file", opcode handlers
// are organized by category — one .cpp per category (sys, move, arith,
// control, calls, object, except, suspend). No monolithic opcodes.cpp.
//
#pragma once

#include <cstdint>

#include "dvm/crb.hpp"
#include "dvm/state.hpp"
#include "dvm/value.hpp"
#include "dvm/opcodes_def.hpp"  // opcode constants

namespace dvm {

// Result of an opcode handler.
enum class OpResult : std::uint8_t {
  Continue,  // advance PC and dispatch the next instruction
  Exit,      // the interpreter should return exit_value
  Trap,      // a trap occurred; the interpreter should return with error
};

// ---- §9 System opcodes (opcodes_sys.cpp) --------------------------------
OpResult op_nop(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_trap(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_unreachable(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_safepoint(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_ret(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_ret_void(InterpState& s, const crb::InstrCell& cell) noexcept;

// ---- §10 Move/constant opcodes (opcodes_move.cpp) ------------------------
OpResult op_mov_const(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_mov_null(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_mov_true(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_mov_false(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_mov_undef(InterpState& s, const crb::InstrCell& cell) noexcept;

// ---- §11 Integer arithmetic (opcodes_arith.cpp) -------------------------
OpResult op_add_i64_wrap(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_sub_i64_wrap(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_mul_i64_wrap(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_neg_i64_wrap(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_add_i64_checked(InterpState& s, const crb::InstrCell& cell) noexcept;

// ---- §14 Comparison (opcodes_arith.cpp) ---------------------------------
OpResult op_cmp_eq(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_cmp_lt_s(InterpState& s, const crb::InstrCell& cell) noexcept;

// ---- §15 Control flow (opcodes_control.cpp) ------------------------------
OpResult op_jmp(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_br_true(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_br_false(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_br_null(InterpState& s, const crb::InstrCell& cell) noexcept;
OpResult op_br_nonnull(InterpState& s, const crb::InstrCell& cell) noexcept;

// ---- §16 Calls (opcodes_calls.cpp) ---------------------------------------
OpResult op_call_direct(InterpState& s, const crb::InstrCell& cell) noexcept;

// ---- §20 Exceptions (opcodes_except.cpp) ---------------------------------
OpResult op_throw(InterpState& s, const crb::InstrCell& cell) noexcept;

}  // namespace dvm
