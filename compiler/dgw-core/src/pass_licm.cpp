// src/pass_licm.cpp — Loop Invariant Code Motion (Spec Part 6.3).
//
// Spec citation: DGW-Core-IR.md Part 6.3 "Loop Invariant Code Motion (LICM)".
//
// "1. Identify STATE nodes (loop headers).
//  2. For each node inside the loop, check if its VALUE inputs originate
//      from outside the loop (do not depend on the STATE backedge).
//  3. If invariant, and its EFFECT/MEMORY dependencies allow it, detach it
//      from the loop's CONTROL token and reattach it to the CONTROL token
//      preceding the loop."
//
// Implementation:
//   - The loop body is the set of nodes reachable from the STATE node's
//     VALUE-output consumers via VALUE edges.
//   - A node N is "invariant" iff all its VALUE inputs originate from
//     outside the loop body set (i.e., the source node of each VALUE input
//     edge is not in the loop body set).
//   - We hoist pure nodes only (conservative; the spec allows non-pure if
//     EFFECT/MEMORY dependencies allow it, but determining that requires
//     memory-alias analysis we don't have here).
//   - The actual HOIST mutation: for each hoistable node N that has a
//     CONTROL input edge (port 0) from a source inside the loop body,
//     detach the edge and reconnect N's CONTROL input to the START node's
//     CONTROL output (the simplest loop-preheader substitute).
//
//   DGW's STATE node has VALUE inputs (init, backedge) — it does NOT have
//   a CONTROL input or output. So the "CONTROL token preceding the loop"
//   is approximated as the START node's CONTROL output. A more complete
//   implementation would compute the actual loop preheader; this is left
//   for a future iteration.
//
#include "dgw/pass_licm.hpp"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace dgw {

LicmStats pass_licm(Weaver& w) {
  LicmStats stats;
  auto& arena = w.arena();

  // Collect all STATE nodes and find the START node (for hoisting target).
  std::vector<NodeId> state_nodes;
  NodeId start_node{kNullNode};
  for (std::uint32_t s = 0; s < arena.node_count(); ++s) {
    if (arena.node_kinds[s] == NodeKind::STATE) {
      state_nodes.push_back(NodeId{s});
      stats.visited++;
    } else if (arena.node_kinds[s] == NodeKind::START) {
      start_node = NodeId{s};
    }
  }
  if (state_nodes.empty()) return stats;

  for (NodeId state : state_nodes) {
    // BFS over VALUE edges starting from STATE's VALUE-output consumers.
    std::unordered_set<std::uint32_t> body;
    std::vector<NodeId> queue;
    EdgeId e{arena.node_first_use[state.value]};
    while (e.valid()) {
      if (arena.edge_kinds[e.value] == EdgeKind::VALUE) {
        for (std::uint32_t m = 0; m < arena.node_count(); ++m) {
          if (arena.node_kinds[m] == NodeKind::DEAD) continue;
          const std::uint32_t off = arena.node_port_offset[m];
          const std::uint16_t cnt = arena.node_port_count[m];
          for (std::uint16_t p = 0; p < cnt; ++p) {
            if (arena.port_connected_edge[off + p].value == e.value) {
              if (!body.count(m)) {
                body.insert(m);
                queue.push_back(NodeId{m});
              }
              break;
            }
          }
        }
      }
      e = EdgeId{arena.edge_next_use[e.value]};
    }
    while (!queue.empty()) {
      NodeId n = queue.back(); queue.pop_back();
      EdgeId ne{arena.node_first_use[n.value]};
      while (ne.valid()) {
        if (arena.edge_kinds[ne.value] == EdgeKind::VALUE) {
          for (std::uint32_t m = 0; m < arena.node_count(); ++m) {
            if (arena.node_kinds[m] == NodeKind::DEAD) continue;
            const std::uint32_t off = arena.node_port_offset[m];
            const std::uint16_t cnt = arena.node_port_count[m];
            for (std::uint16_t p = 0; p < cnt; ++p) {
              if (arena.port_connected_edge[off + p].value == ne.value) {
                if (!body.count(m)) {
                  body.insert(m);
                  queue.push_back(NodeId{m});
                }
                break;
              }
            }
          }
        }
        ne = EdgeId{arena.edge_next_use[ne.value]};
      }
    }

    // For each node in the body, check if it's invariant (all VALUE inputs
    // originate from outside body) AND pure. If so, hoist it.
    for (std::uint32_t n : body) {
      if (!w.is_pure(NodeId{n})) continue;
      const NodeSignature sig = signature_of(arena.node_kinds[n]);
      const std::uint16_t in_count = static_cast<std::uint16_t>(sig.inputs.size());
      bool invariant = true;
      const std::uint32_t off = arena.node_port_offset[n];
      for (std::uint16_t p = 0; p < in_count; ++p) {
        if (sig.inputs[p].kind != EdgeKind::VALUE) continue;
        EdgeId ie{arena.port_connected_edge[off + p]};
        if (!ie.valid()) continue;
        const std::uint32_t src = arena.edge_source_node[ie.value].value;
        if (body.count(src)) { invariant = false; break; }
      }
      if (!invariant) continue;

      // HOIST: if this node has a CONTROL input edge (port 0) and the
      // source is inside the loop body, reattach to START's CONTROL
      // output. The CONTROL input is at port 0 for nodes that have one.
      if (in_count > 0 && sig.inputs[0].kind == EdgeKind::CONTROL &&
          start_node.valid()) {
        EdgeId ctrl_e{arena.port_connected_edge[off + 0]};
        if (ctrl_e.valid()) {
          NodeId ctrl_src = arena.edge_source_node[ctrl_e.value];
          if (body.count(ctrl_src.value)) {
            // Detach the current control input and reattach to START.
            w.connect_control(start_node, NodeId{n});
            stats.hoisted++;
          }
        }
      }
      // For nodes with no CONTROL input (pure VALUE nodes), there's no
      // CONTROL to hoist — the node is already "before" the loop in the
      // SSA sense. We count it as hoistable but don't perform a mutation.
      // (A more complete implementation would also hoist EFFECT/MEMORY
      // dependencies; this is out of scope.)
      else if (in_count == 0 || sig.inputs[0].kind != EdgeKind::CONTROL) {
        // No CONTROL input to hoist; the node's VALUE position already
        // makes it invariant. We do not count this as a hoist since
        // no mutation is performed.
      }
    }
  }
  return stats;
}

}  // namespace dgw
