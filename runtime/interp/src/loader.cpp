// src/loader.cpp — CRB module loader implementation.
//
// Spec citation: DVM-CRB.md Section 3 "Module Layout" and Section 3.1
// "CRB Header".
//
#include "dvm/loader.hpp"

#include <cstring>
#include <string_view>

namespace dvm {

namespace {

// Check that `offset + size` is within `raw` without overflow.
bool in_bounds(std::span<const std::byte> raw, std::uint32_t offset,
               std::size_t size) noexcept {
  if (offset > raw.size()) return false;
  if (size > raw.size() - offset) return false;
  return true;
}

}  // namespace

LoadResult load_module(std::span<const std::byte> raw) noexcept {
  LoadResult r;
  if (raw.size() < sizeof(crb::ModuleHeader)) {
    r.error = "module too small for header";
    return r;
  }
  // Copy out the header (it's POD-aligned; we memcpy to be safe with
  // alignment of the underlying byte buffer).
  crb::ModuleHeader h;
  std::memcpy(&h, raw.data(), sizeof(h));
  // Magic check: spec §3.1 says magic = 0x31425243 ("CRB1" LE).
  if (h.magic != crb::kMagicValue) {
    r.error = "bad magic (not a CRB module)";
    return r;
  }
  // Version check. We support v1.0 only for now.
  if (h.version_major != crb::kVersionMajor ||
      h.version_minor != crb::kVersionMinor) {
    r.error = "unsupported CRB version";
    return r;
  }
  if (h.reserved_0 != 0 || h.reserved_1 != 0) {
    r.error = "header reserved fields are not zero";
    return r;
  }
  // Section table bounds. section_table_offset is now uint64_t.
  std::uint64_t st_off = h.section_table_offset;
  std::size_t st_size = std::size_t{h.section_count} * sizeof(crb::SectionEntry);
  if (st_off > raw.size() || st_size > raw.size() - st_off) {
    r.error = "section table out of bounds";
    return r;
  }
  // Build the section span.
  const auto* sec_arr = reinterpret_cast<const crb::SectionEntry*>(
      raw.data() + st_off);
  r.module.sections = std::span<const crb::SectionEntry>(sec_arr, h.section_count);

  // Walk the section table and fill in the section-specific views.
  for (const auto& s : r.module.sections) {
    if (!in_bounds(raw, s.offset, s.size)) {
      r.error = "section out of bounds";
      return r;
    }
    if (s.reserved != 0) {
      r.error = "section entry reserved field is not zero";
      return r;
    }
    auto section_bytes = std::span<const std::byte>(
        raw.data() + s.offset, s.size);
    switch (static_cast<crb::SectionType>(s.type)) {
      case crb::SectionType::Code:
        // Code section: must be a multiple of 8 bytes (InstrCell size).
        if (s.size % sizeof(crb::InstrCell) != 0) {
          r.error = "code section size not a multiple of 8";
          return r;
        }
        r.module.code = std::span<const crb::InstrCell>(
            reinterpret_cast<const crb::InstrCell*>(raw.data() + s.offset),
            s.size / sizeof(crb::InstrCell));
        break;
      case crb::SectionType::FunctionTable:
        if (s.size % sizeof(crb::FunctionEntry) != 0) {
          r.error = "function table size not a multiple of FunctionEntry";
          return r;
        }
        r.module.functions = std::span<const crb::FunctionEntry>(
            reinterpret_cast<const crb::FunctionEntry*>(raw.data() + s.offset),
            s.size / sizeof(crb::FunctionEntry));
        break;
      case crb::SectionType::ConstantPool:
        r.module.constant_pool = section_bytes;
        if (s.size % sizeof(crb::ConstantEntry) != 0) {
          r.error = "constant pool size not a multiple of ConstantEntry";
          return r;
        }
        r.module.constants = std::span<const crb::ConstantEntry>(
            reinterpret_cast<const crb::ConstantEntry*>(raw.data() + s.offset),
            s.size / sizeof(crb::ConstantEntry));
        break;
      case crb::SectionType::StringPool:
        r.module.string_pool = section_bytes;
        break;
      default:
        // Other sections are loaded but not directly exposed by the
        // minimal Module view. The interpreter does not need them yet.
        break;
    }
  }
  r.module.raw = raw;
  r.module.header = h;
  r.ok = true;
  return r;
}

}  // namespace dvm
