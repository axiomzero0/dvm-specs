// src/module.cpp — Module lookup helpers.
//
#include "dvm/crb.hpp"

namespace dvm::crb {

const FunctionEntry* Module::find_function(std::uint32_t id) const noexcept {
  for (const auto& fn : functions) {
    if (fn.function_id == id) return &fn;
  }
  return nullptr;
}

std::span<const InstrCell> Module::function_code(const FunctionEntry& fn) const noexcept {
  // The code section is a span of InstrCells. fn.code_offset is a byte
  // offset into the Code section; convert to a cell index.
  if (code.empty()) return {};
  const auto cell_offset = fn.code_offset / sizeof(InstrCell);
  const auto cell_count  = fn.code_length / sizeof(InstrCell);
  if (cell_offset >= code.size()) return {};
  const auto avail = code.size() - cell_offset;
  const auto take = cell_count <= avail ? cell_count : avail;
  return code.subspan(cell_offset, take);
}

}  // namespace dvm::crb
