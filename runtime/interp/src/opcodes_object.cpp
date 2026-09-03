// src/opcodes_object.cpp — Allocation + object model handlers (CRB §18, §19).
//
// Spec citation: DVM-CRB.md §18 (Object Model) and §19 (Allocation).
//
// The minimal interpreter uses a simple object model: each ALLOC creates
// a heap-allocated ObjStorage (a small struct holding a field table). The
// field table maps field indices (uint16_t, from the access site table)
// to Values. OBJ_GET/OBJ_SET read/write fields by index.
//
#include "dvm/opcodes.hpp"

#include <vector>

namespace dvm {

// A simple heap-allocated object: a vector of Values indexed by field
// index. The object's identity is the pointer to this struct.
// Defined in the dvm namespace (not anonymous) so that the forward
// declaration in state.hpp matches and `delete` can see the full type.
struct ObjStorage {
  std::vector<Value> fields;
  explicit ObjStorage(std::size_t n) : fields(n, Value::null()) {}
};

// free_heap() is defined here because ObjStorage is fully defined only
// in this translation unit. `delete` needs the complete type to call the
// vector destructor.
void InterpState::free_heap() noexcept {
  for (auto* obj : heap) {
    delete obj;
  }
  heap.clear();
}

// ---- §19 ALLOC (R_IMM32: dst = s1, field_count = imm32(s2, s3)) ---------
OpResult op_alloc(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t dst = cell.s1();
  std::uint32_t field_count = crb::InstrCell::imm32(cell.s2(), cell.s3());
  auto* obj = new ObjStorage(field_count);
  s.heap.push_back(obj);  // track for cleanup
  s.reg(dst) = Value{static_cast<void*>(obj), TypeTag::ObjRef};
  s.advance();
  return OpResult::Continue;
}

// ---- §18 OBJ_GET (ACCESS: dst = s1, obj = s2, field_idx = s3) ----------
OpResult op_obj_get(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t dst = cell.s1();
  std::uint16_t obj_reg = cell.s2();
  std::uint16_t field_idx = cell.s3();
  Value& obj_val = s.reg(obj_reg);
  if (obj_val.tag != TypeTag::ObjRef) {
    s.exit_value = Value::null();
    return OpResult::Trap;
  }
  auto* obj = static_cast<ObjStorage*>(obj_val.as_ptr());
  if (field_idx >= obj->fields.size()) {
    s.exit_value = Value::null();
    return OpResult::Trap;
  }
  s.reg(dst) = obj->fields[field_idx];
  s.advance();
  return OpResult::Continue;
}

// ---- §18 OBJ_SET (ACCESS: obj = s1, field_idx = s2, value = s3) --------
OpResult op_obj_set(InterpState& s, const crb::InstrCell& cell) noexcept {
  std::uint16_t obj_reg = cell.s1();
  std::uint16_t field_idx = cell.s2();
  std::uint16_t val_reg = cell.s3();
  Value& obj_val = s.reg(obj_reg);
  if (obj_val.tag != TypeTag::ObjRef) {
    s.exit_value = Value::null();
    return OpResult::Trap;
  }
  auto* obj = static_cast<ObjStorage*>(obj_val.as_ptr());
  if (field_idx >= obj->fields.size()) {
    s.exit_value = Value::null();
    return OpResult::Trap;
  }
  obj->fields[field_idx] = s.reg(val_reg);
  s.advance();
  return OpResult::Continue;
}

}  // namespace dvm
