// dgw/pass_cleanup.hpp — FWD-chain CleanupPass (Spec Part 5.2 helper).
//
// Spec citation: DGW-Core-IR.md Part 5.2 "The Forwarding Node Trick (O(1) Rewiring)".
//
// "The users don't change. A later fast CleanupPass collapses FWD chains in
//  a single linear sweep."
//
// This pass walks every FWD node in the graph, follows its single input,
// and if that input is itself a FWD, collapses the chain by re-pointing the
// outer FWD's input to the inner FWD's input and killing the inner FWD.
// After this pass, no FWD's input is itself a FWD — FWD chains are at most
// one link long.
//
#pragma once

#include <cstdint>

#include "dgw/weaver.hpp"

namespace dgw {

struct CleanupStats {
  std::uint32_t collapsed{0};
  std::uint32_t killed{0};
};

// Collapses FWD chains. Returns per-pass statistics.
CleanupStats pass_cleanup(Weaver& w);

}  // namespace dgw
