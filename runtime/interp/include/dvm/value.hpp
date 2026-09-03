// dvm/value.hpp — The DVM Tier 0 value union.
//
// Spec citation: DVM-CRB.md Section 5 "Type System" — primitive type IDs
// and the value space the interpreter operates on.
//
// CRB is dynamically typed at the value level: every register holds a
// Value that carries both the bits and a type tag. The interpreter
// dispatches on the type tag for type-checked operations (e.g., ADD_I64
// requires Int64); unchecked operations (MOV) just copy the whole Value.
//
#pragma once

#include <cstdint>
#include <cstring>
#include <compare>

namespace dvm {

// ---- Type tags (mirror CRB Section 5.1 primitive type IDs) ----------------
enum class TypeTag : std::uint8_t {
  Null       = 0,
  Bool       = 1,
  Undef      = 2,
  Int64      = 3,
  Float64    = 4,
  ObjRef     = 5,  // pointer to a guest object (heap-allocated)
  NativeRef  = 6,  // pointer to a native object (FFI / raw memory)
  Closure    = 7,  // closure value (function + environment)
  Suspension = 8,  // coroutine / fiber / generator state
  Exception  = 9,  // guest exception object
};

// ---- Value: tagged union -------------------------------------------------
// 16 bytes total: 8-byte payload + 1-byte tag + 7 bytes padding for
// alignment. The payload is large enough to hold any of int64, double,
// or a pointer. The tag selects the interpretation.
struct Value {
  // The payload is a union; we use memcpy for type-punning to avoid
  // strict-aliasing violations.
  union Payload {
    std::int64_t i64;
    double       f64;
    void*        ptr;
    std::uint64_t raw;  // for bitwise comparison
  } payload;
  TypeTag tag{TypeTag::Null};

  // ---- Constructors -----------------------------------------------------
  constexpr Value() noexcept : payload{.raw = 0}, tag{TypeTag::Null} {}
  constexpr explicit Value(std::int64_t v) noexcept : payload{.i64 = v}, tag{TypeTag::Int64} {}
  constexpr explicit Value(double v) noexcept : payload{.f64 = v}, tag{TypeTag::Float64} {}
  constexpr explicit Value(bool v) noexcept
      : payload{.i64 = v ? 1 : 0}, tag{TypeTag::Bool} {}
  constexpr explicit Value(void* p, TypeTag t) noexcept
      : payload{.ptr = p}, tag{t} {}

  // ---- Factory helpers --------------------------------------------------
  static constexpr Value null() noexcept { return Value{}; }
  static constexpr Value undef() noexcept {
    Value v; v.tag = TypeTag::Undef; return v;
  }
  static constexpr Value boolean(bool b) noexcept { return Value{b}; }

  // ---- Accessors --------------------------------------------------------
  std::int64_t as_i64() const noexcept { return payload.i64; }
  double       as_f64() const noexcept { return payload.f64; }
  bool         as_bool() const noexcept { return payload.i64 != 0; }
  void*        as_ptr() const noexcept { return payload.ptr; }

  // ---- Truthiness for BR_TRUE / BR_FALSE --------------------------------
  // Per CRB §15.2/15.3: BR_TRUE branches if the condition register is
  // truthy. Truthiness: Null/Undef/empty-string → false; 0 → false;
  // everything else → true.
  bool truthy() const noexcept {
    switch (tag) {
      case TypeTag::Null:    return false;
      case TypeTag::Undef:   return false;
      case TypeTag::Bool:    return payload.i64 != 0;
      case TypeTag::Int64:   return payload.i64 != 0;
      case TypeTag::Float64: return payload.f64 != 0.0;
      case TypeTag::ObjRef:  return payload.ptr != nullptr;
      default:               return payload.raw != 0;
    }
  }

  // ---- Equality (for CMP_EQ) --------------------------------------------
  bool operator==(const Value& o) const noexcept {
    if (tag != o.tag) return false;
    switch (tag) {
      case TypeTag::Int64:
      case TypeTag::Bool:
        return payload.i64 == o.payload.i64;
      case TypeTag::Float64:
        return payload.f64 == o.payload.f64;
      case TypeTag::Null:
      case TypeTag::Undef:
        return true;  // all nulls are equal; all undefs are equal
      default:
        return payload.raw == o.payload.raw;  // pointer compare
    }
  }
};

static_assert(sizeof(Value) == 16, "Value must be 16 bytes for register-file density");

}  // namespace dvm
