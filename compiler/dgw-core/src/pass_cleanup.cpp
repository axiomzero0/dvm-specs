// src/pass_cleanup.cpp — FWD-chain CleanupPass (Spec Part 5.2 helper).
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
#include "dgw/pass_cleanup.hpp"

#include <cstdint>

namespace dgw {

CleanupStats pass_cleanup(Weaver& w) {
  CleanupStats stats;
  auto& arena = w.arena();

  // For each FWD node, follow its single input. If the input is also a FWD,
  // collapse the chain.
  for (std::uint32_t n = 0; n < arena.node_count(); ++n) {
    if (arena.node_kinds[n] != NodeKind::FWD) continue;
    NodeId cur{n};
    while (true) {
      NodeId src = w.input_node(cur, PortId{0});
      if (!src.valid()) break;
      if (arena.node_kinds[src.value] != NodeKind::FWD) break;
      // Collapse: cur forwards to src's input instead of src.
      NodeId srcs_input = w.input_node(src, PortId{0});
      if (!srcs_input.valid()) break;
      // Rewire cur's port 0 to point at srcs_input (rewire_uses on the edge
      // is complex; easier: rewire_uses(src, srcs_input) — but that changes
      // src's users. We want cur's users to see srcs_input directly.
      // Easier approach: forward_node(cur, srcs_input) — but cur is already
      // FWD; forward_node expects a non-FWD old_node.
      //
      // Cleanest: detach cur's input edge from src, connect it to srcs_input.
      EdgeId e = w.input_edge(cur, PortId{0});
      if (!e.valid()) break;
      // Connect srcs_input's VALUE output into cur's input port 0.
      // We can't use w.connect because the existing slot needs to be
      // detached first; w.connect handles detach internally.
      const NodeSignature src_sig = signature_of(arena.node_kinds[srcs_input.value]);
      const std::uint16_t src_in_count = static_cast<std::uint16_t>(src_sig.inputs.size());
      w.connect(srcs_input, PortId{src_in_count}, cur, PortId{0}, EdgeKind::VALUE);
      stats.collapsed++;
      // src is now unused; kill it.
      w.kill_node(src);
      stats.killed++;
      cur = cur;  // stay on this node; loop continues to chase further FWDs.
    }
  }
  return stats;
}

}  // namespace dgw
