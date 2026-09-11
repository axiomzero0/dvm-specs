// dgw/pass_constfold.hpp — Constant Folding (Performance Priorities §14).
//
// Spec citation: DVM-Performance-Priorities.md §14 (Specialize Values,
// Not Just Types) and §10 (Don't Use One Giant Optimization Pipeline —
// Tier 2 pipeline includes const-prop before GVN).
//
// Folds arithmetic on CONST nodes:
//   CONST(2) + CONST(3) → CONST(5)
//   CONST(10) - CONST(4) → CONST(6)
//   CONST(3) * CONST(7) → CONST(21)
//   CONST(0) + x → x (algebraic identity)
//   CONST(1) * x → x (algebraic identity)
//   CONST(0) * x → CONST(0) (zero absorption)
//   CMP_EQ(CONST(5), CONST(5)) → CONST(true)
//   CMP_LT(CONST(3), CONST(7)) → CONST(true)
//
// After folding, the dead CONST inputs become unused and DCE removes them.
// This is critical for traces where loop bounds, increments, and limits
// are constants — the compiler discovers them and eliminates the
// arithmetic entirely.
//
#pragma once

#include <cstdint>

#include "dgw/weaver.hpp"

namespace dgw {

struct ConstFoldStats {
  std::uint32_t folded{0};    // arithmetic operations folded to CONST
  std::uint32_t simplified{0};  // algebraic simplifications (identity, absorption)
  std::uint32_t visited{0};    // nodes examined
};

// Run constant folding over the graph.
ConstFoldStats pass_constfold(Weaver& w);

}  // namespace dgw
