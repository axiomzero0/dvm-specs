// src/opcodes_arith.cpp — Integer arithmetic + comparison handlers.
//
// Spec citation: DVM-CRB.md §11 (integer arithmetic) and §14 (comparisons).
//
// Per CRB §11.3, .wrap means "modulo 2^64" — well-defined two's-complement
// wraparound. C++ signed arithmetic is UB on overflow, so we cast through
// uint64_t to get well-defined wraparound, then back to int64_t.
//
#include "dvm/opcodes.hpp"

namespace dvm {

namespace {

inline std::int64_t wrap_add(std::int64_t a, std::int64_t b) noexcept {
  return static_cast<std::int64_t>(
      static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b));
}
inline std::int64_t wrap_sub(std::int64_t a, std::int64_t b) noexcept {
  return static_cast<std::int64_t>(
      static_cast<std::uint64_t>(a) - static_cast<std::uint64_t>(b));
}
inline std::int64_t wrap_mul(std::int64_t a, std::int64_t b) noexcept {
  return static_cast<std::int64_t>(
      static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b));
}
inline std::int64_t wrap_neg(std::int64_t a) noexcept {
  return static_cast<std::int64_t>(0u - static_cast<std::uint64_t>(a));
}

}  // namespace

OpResult op_add_i64_wrap(InterpState& s, const crb::InstrCell& cell) noexcept {
  // R_R_R: dst = s1, src0 = s2, src1 = s3.
  std::uint16_t dst = cell.s1();
  std::int64_t a = s.reg(cell.s2()).as_i64();
  std::int64_t b = s.reg(cell.s3()).as_i64();
  s.reg(dst) = Value{wrap_add(a, b)};
  s.advance();
  return OpResult::Continue;
}

OpResult op_sub_i64_wrap(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t dst = cell.s1();
  std::int64_t a = s.reg(cell.s2()).as_i64();
  std::int64_t b = s.reg(cell.s3()).as_i64();
  s.reg(dst) = Value{wrap_sub(a, b)};
  s.advance();
  return OpResult::Continue;
}

OpResult op_mul_i64_wrap(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t dst = cell.s1();
  std::int64_t a = s.reg(cell.s2()).as_i64();
  std::int64_t b = s.reg(cell.s3()).as_i64();
  s.reg(dst) = Value{wrap_mul(a, b)};
  s.advance();
  return OpResult::Continue;
}

OpResult op_neg_i64_wrap(InterpState& s, const crb::InstrCell& cell) noexcept {
  // R_R: dst = s1, src = s2.
  std::uint16_t dst = cell.s1();
  std::int64_t a = s.reg(cell.s2()).as_i64();
  s.reg(dst) = Value{wrap_neg(a)};
  s.advance();
  return OpResult::Continue;
}

OpResult op_add_i64_checked(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t dst = cell.s1();
  std::int64_t a = s.reg(cell.s2()).as_i64();
  std::int64_t b = s.reg(cell.s3()).as_i64();
  // Check for signed overflow (well-defined, no UB).
  if (b > 0 && a > INT64_MAX - b) { s.exit_value = Value::null(); return OpResult::Trap; }
  if (b < 0 && a < INT64_MIN - b) { s.exit_value = Value::null(); return OpResult::Trap; }
  s.reg(dst) = Value{a + b};  // safe: no overflow possible here
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
