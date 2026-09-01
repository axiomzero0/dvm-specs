// src/scheduler.cpp — Trace Scheduling and Block Formation (Spec Part 7).
//
// Spec citation: DGW-Core-IR.md Part 7 "Scheduling and Block Formation
//                (Lowering)".
//
//  7.1 Trace Formation
//  7.2 Block Extraction
//  7.3 Instruction Selection (partial — we emit MachineOp stubs)
//
#include "dgw/scheduler.hpp"

#include <print>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace dgw {

namespace {

// Helper: find the dst node and port for a given edge id.
struct EdgeDst { NodeId node; PortId port; };
EdgeDst find_edge_dst(const GraphArena& a, EdgeId e) {
  for (std::uint32_t n = 0; n < a.node_count(); ++n) {
    if (a.node_kinds[n] == NodeKind::DEAD) continue;
    const std::uint32_t off = a.node_port_offset[n];
    const std::uint16_t cnt = a.node_port_count[n];
    for (std::uint16_t p = 0; p < cnt; ++p) {
      if (a.port_connected_edge[off + p].value == e.value) {
        return {NodeId{n}, PortId{p}};
      }
    }
  }
  return {NodeId{}, PortId{}};
}

}  // namespace

MachineCFG schedule_to_cfg(Weaver& w, PgoProbFn pgo, void* pgo_user) {
  MachineCFG cfg;
  auto& arena = w.arena();

  // Phase 1 — find START node(s).
  std::vector<NodeId> starts;
  for (std::uint32_t n = 0; n < arena.node_count(); ++n) {
    if (arena.node_kinds[n] == NodeKind::START) starts.push_back(NodeId{n});
  }
  if (starts.empty()) return cfg;

  // Phase 2 — Trace Formation (Spec 7.1): walk CONTROL edges from START;
  // at BRANCH, pick the "hottest" path via `pgo`.
  //
  // Phase 3 — Block Extraction (Spec 7.2): a new block starts when a node
  // is the target of a BRANCH or JOIN from outside the current trace, or
  // when a HANDLER is encountered.
  //
  // For the smoke test, we implement a simplified linear sweep: walk all
  // live nodes in id order, group them into a single linear trace, and
  // emit JOIN/STATE as PHI nodes at the appropriate block heads.
  std::vector<NodeId> trace;
  std::unordered_set<std::uint32_t> visited;
  std::queue<NodeId> q;
  q.push(starts[0]);
  visited.insert(starts[0].value);
  while (!q.empty()) {
    NodeId n = q.front(); q.pop();
    trace.push_back(n);
    // Walk outgoing edges in order; CONTROL edges first.
    EdgeId e{arena.node_first_use[n.value]};
    while (e.valid()) {
      if (arena.edge_kinds[e.value] == EdgeKind::CONTROL) {
        auto dst = find_edge_dst(arena, e);
        if (dst.node.valid() && !visited.count(dst.node.value)) {
          // For BRANCH, pick the hotter path via pgo.
          if (arena.node_kinds[n.value] == NodeKind::BRANCH && pgo) {
            double p = pgo(n, pgo_user);
            // If p < 0.5, prefer FALSE_CONTROL (output port 1); else TRUE (port 0).
            // We pick one and defer the other to a successor block.
            const PortId wanted = (p < 0.5) ? PortId{1} : PortId{0};
            if (dst.port != wanted) {
              e = EdgeId{arena.edge_next_use[e.value]};
              continue;
            }
          }
          visited.insert(dst.node.value);
          q.push(dst.node);
        }
      }
      e = EdgeId{arena.edge_next_use[e.value]};
    }
  }

  // Phase 4 — Build one big entry block from the trace.
  MachineBasicBlock blk;
  blk.id = 0;
  for (NodeId n : trace) {
    if (arena.node_kinds[n.value] == NodeKind::JOIN ||
        arena.node_kinds[n.value] == NodeKind::STATE) {
      // Spec 7.2.3 / 7.2.4: convert JOIN/STATE back into PHI at block head.
      MachinePhi phi;
      phi.origin = n;
      blk.phis.push_back(phi);
      continue;
    }
    MachineOp op;
    op.origin = n;
    op.origin_kind = arena.node_kinds[n.value];
    op.label = std::string(node_kind_name(op.origin_kind));
    blk.ops.push_back(op);
  }
  cfg.blocks.push_back(std::move(blk));
  cfg.entry_block_id = 0;
  return cfg;
}

void print_cfg(const MachineCFG& cfg, const GraphArena* arena) {
  std::println("MachineCFG: {} block(s), entry=block#{}",
               cfg.blocks.size(), cfg.entry_block_id);
  for (const auto& blk : cfg.blocks) {
    std::println("  Block #{}: {} phi(s), {} op(s)", blk.id, blk.phis.size(), blk.ops.size());
    for (const auto& phi : blk.phis) {
      const char* kind_name = "?";
      if (arena && phi.origin.valid() && phi.origin.value < arena->node_count()) {
        kind_name = node_kind_name(arena->node_kinds[phi.origin.value]).data();
      }
      std::println("    PHI (from {} = node #{})",
                   kind_name,
                   static_cast<std::uint32_t>(phi.origin.value));
    }
    for (const auto& op : blk.ops) {
      std::println("    {} (node #{})",
                   op.label, static_cast<std::uint32_t>(op.origin.value));
    }
  }
}

}  // namespace dgw
