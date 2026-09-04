// dvm/loader.hpp — CRB module loader.
//
// Spec citation: DVM-CRB.md Section 3 "Module Layout".
//
// The loader takes a raw byte buffer containing a CRB binary and produces
// a parsed `Module` view. It validates the magic, version, header flags,
// and section table bounds. It does NOT validate opcode streams — that
// is the verifier's job, not the loader's.
//
#pragma once

#include <span>
#include <string_view>

#include "dvm/crb.hpp"

namespace dvm {

// Load result. `module` is populated only if `ok` is true.
struct LoadResult {
  bool       ok{false};
  std::string_view error{};
  crb::Module module{};
};

// Load a CRB binary from a raw byte buffer. The buffer must outlive the
// returned Module (the Module holds spans into it).
LoadResult load_module(std::span<const std::byte> raw) noexcept;

}  // namespace dvm
