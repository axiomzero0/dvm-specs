// dgw/pass_dce.hpp — Dead Code Elimination (Spec Part 6.2).
//
// Spec citation: DGW-Core-IR.md Part 6.2 "Dead Code Elimination (DCE)".
//
// "1. Seed a worklist with all Observable nodes (RETURN, STORE to global,
//      I/O, DEOPT_TRAP).
//  2. Walk backwards along VALUE, CONTROL, MEMORY, and EFFECT edges,
//      marking nodes as Live.
//  3. Any node not marked Live is passed to weaver.kill_node()."
//
#pragma once

#include <cstdint>

#include "dgw/weaver.hpp"

namespace dgw {

struct DceStats {
  std::uint32_t killed{0};
  std::uint32_t live{0};
};

// Runs DCE over the graph. Returns per-pass statistics.
DceStats pass_dce(Weaver& w);

}  // namespace dgw
