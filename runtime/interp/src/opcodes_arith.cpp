// src/opcodes_arith.cpp — Integer arithmetic + comparison handlers.
//
// Spec citation: DVM-CRB.md §11 (integer arithmetic) and §14 (comparisons).
//
#include "dvm/opcodes.hpp"

namespace dvm {

OpResult op_add_i64_wrap(InterpState& s, const crb::InstrCell& cell) noexcept {
  // R_R_R: dst = s1, src0 = s2, src1 = s3.
  std::uint16_t dst = cell.s1();
  std::int64_t a = s.reg(cell.s2()).as_i64();
  std::int64_t b = s.reg(cell.s3()).as_i64();
  s.reg(dst) = Value{a + b};  // wrap: no overflow check
  s.advance();
  return OpResult::Continue;
}

OpResult op_sub_i64_wrap(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t dst = cell.s1();
  std::int64_t a = s.reg(cell.s2()).as_i64();
  std::int64_t b = s.reg(cell.s3()).as_i64();
  s.reg(dst) = Value{a - b};
  s.advance();
  return OpResult::Continue;
}

OpResult op_mul_i64_wrap(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t dst = cell.s1();
  std::int64_t a = s.reg(cell.s2()).as_i64();
  std::int64_t b = s.reg(cell.s3()).as_i64();
  s.reg(dst) = Value{a * b};
  s.advance();
  return OpResult::Continue;
}

OpResult op_neg_i64_wrap(InterpState& s, const crb::InstrCell& cell) noexcept {
  // R_R: dst = s1, src = s2.
  std::uint16_t dst = cell.s1();
  std::int64_t a = s.reg(cell.s2()).as_i64();
  s.reg(dst) = Value{-a};
  s.advance();
  return OpResult::Continue;
}

OpResult op_add_i64_checked(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t dst = cell.s1();
  std::int64_t a = s.reg(cell.s2()).as_i64();
  std::int64_t b = s.reg(cell.s3()).as_i64();
  // Check for signed overflow.
  if (b > 0 && a > INT64_MAX - b) { s.exit_value = Value::null(); return OpResult::Trap; }
  if (b < 0 && a < INT64_MIN - b) { s.exit_value = Value::null(); return OpResult::Trap; }
  s.reg(dst) = Value{a + b};
  s.advance();
  return OpResult::Continue;
}

// ---- §14 Comparisons (R_R_R: dst = s1, src0 = s2, src1 = s3) -----------
OpResult op_cmp_eq(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t dst = cell.s1();
  const Value& a = s.reg(cell.s2());
  const Value& b = s.reg(cell.s3());
  s.reg(dst) = Value::boolean(a == b);
  s.advance();
  return OpResult::Continue;
}

OpResult op_cmp_lt_s(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t dst = cell.s1();
  std::int64_t a = s.reg(cell.s2()).as_i64();
  std::int64_t b = s.reg(cell.s3()).as_i64();
  s.reg(dst) = Value::boolean(a < b);
  s.advance();
  return OpResult::Continue;
}

}  // namespace dvm
