// src/scheduler.cpp — Trace Scheduling and Block Formation (Spec Part 7).
//
// Spec citation: DGW-Core-IR.md Part 7 "Scheduling and Block Formation
//                (Lowering)".
//
//  7.1 Trace Formation — start at START, follow CONTROL edges, use PGO at
//      BRANCH to pick the hotter successor.
//  7.2 Block Extraction — form a new MachineBasicBlock at:
//        (a) the target of a BRANCH/JOIN from outside the current trace,
//        (b) a HANDLER landing pad.
//      JOIN/STATE nodes convert to PHI at the top of the newly formed block.
//  7.3 Instruction Selection — emit MachineOp placeholders.
//
#include "dgw/scheduler.hpp"

#include <algorithm>
#include <print>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dgw {

namespace {

// Helper: find the dst node and port for a given edge id by scanning
// port_connected_edge. O(N*P) but bounded by the graph size.
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

  // Phase 2 — Build the CFG by walking CONTROL edges.
  //
  // For each node, we record which block it belongs to. A new block is
  // created when:
  //   (a) A BRANCH's TRUE or FALSE successor is first visited — the
  //       successor becomes the head of a new block.
  //   (b) A HANDLER node is encountered.
  //   (c) A JOIN or STATE node is encountered — it becomes a PHI at the
  //       head of a new block.
  //
  // We do a BFS over CONTROL edges starting at START. At BRANCH, we use
  // the PGO callback to choose which successor to schedule first (the
  // hotter path); the other successor still becomes a block.

  std::unordered_map<std::uint32_t, std::uint32_t> node_to_block;
  std::vector<MachineBasicBlock> blocks;

  auto new_block = [&]() -> std::uint32_t {
    MachineBasicBlock blk;
    blk.id = static_cast<std::uint32_t>(blocks.size());
    blocks.push_back(std::move(blk));
    return blk.id;
  };

  auto add_to_block = [&](std::uint32_t block_id, NodeId n) {
    auto& blk = blocks[block_id];
    if (arena.node_kinds[n.value] == NodeKind::JOIN ||
        arena.node_kinds[n.value] == NodeKind::STATE) {
      // Spec 7.2.3/7.2.4: convert JOIN/STATE to PHI at block head.
      MachinePhi phi;
      phi.origin = n;
      blk.phis.push_back(phi);
    } else {
      MachineOp op;
      op.origin = n;
      op.origin_kind = arena.node_kinds[n.value];
      op.label = std::string(node_kind_name(op.origin_kind));
      blk.ops.push_back(op);
    }
  };

  // Phase 3 — Walk the CONTROL graph.
  // For each BRANCH, schedule the hotter successor first into the current
  // block; schedule the cooler successor as a new block head.
  std::queue<NodeId> queue;
  std::unordered_set<std::uint32_t> visited;

  // Start node goes in block 0.
  std::uint32_t entry_id = new_block();
  cfg.entry_block_id = entry_id;
  node_to_block[starts[0].value] = entry_id;
  add_to_block(entry_id, starts[0]);
  queue.push(starts[0]);
  visited.insert(starts[0].value);

  while (!queue.empty()) {
    NodeId cur = queue.front(); queue.pop();
    std::uint32_t cur_block = node_to_block[cur.value];

    // Collect outgoing CONTROL edges from cur.
    struct ControlEdge { EdgeId e; NodeId dst; PortId dst_port; PortId src_port; };
    std::vector<ControlEdge> ctrl_edges;
    EdgeId e{arena.node_first_use[cur.value]};
    while (e.valid()) {
      if (arena.edge_kinds[e.value] == EdgeKind::CONTROL) {
        auto dst = find_edge_dst(arena, e);
        if (dst.node.valid()) {
          ctrl_edges.push_back({e, dst.node, dst.port, arena.edge_source_port[e.value]});
        }
      }
      e = EdgeId{arena.edge_next_use[e.value]};
    }
    if (ctrl_edges.empty()) continue;

    // For BRANCH: choose the hotter path via PGO. The BRANCH's TRUE_CONTROL
    // output is at src_port = in_count + 0; FALSE_CONTROL at in_count + 1.
    // We map src_port to true/false.
    if (arena.node_kinds[cur.value] == NodeKind::BRANCH && pgo && ctrl_edges.size() == 2) {
      double p = pgo(cur, pgo_user);
      const NodeSignature bs = signature_of(NodeKind::BRANCH);
      const std::uint16_t in_count = static_cast<std::uint16_t>(bs.inputs.size());
      const PortId true_port{static_cast<std::uint16_t>(in_count + 0)};
      const PortId false_port{static_cast<std::uint16_t>(in_count + 1)};
      // Sort: hotter successor first.
      std::sort(ctrl_edges.begin(), ctrl_edges.end(),
                [&](const ControlEdge& a, const ControlEdge& b) {
                  bool a_true = (a.src_port == true_port);
                  bool b_true = (b.src_port == true_port);
                  // Hotter = (true if p >= 0.5, false otherwise).
                  bool want_true = (p >= 0.5);
                  if (a_true && b_true) return false;  // both true (shouldn't happen)
                  if (!a_true && !b_true) return false;  // both false
                  if (a_true && want_true) return true;   // a is hotter
                  if (b_true && want_true) return false;  // b is hotter
                  if (a_true && !want_true) return false;  // a is cooler
                  return true;  // b is cooler
                });
    }

    // Schedule the first (hotter) successor into the current block;
    // schedule the rest into new blocks.
    bool first = true;
    for (const auto& ce : ctrl_edges) {
      if (visited.count(ce.dst.value)) {
        // Already visited — this is a join back-edge. Record the
        // predecessor for the existing block.
        std::uint32_t existing_block = node_to_block[ce.dst.value];
        // Add cur's block as a predecessor of existing_block.
        auto& blk = blocks[existing_block];
        if (std::find(blk.preds.begin(), blk.preds.end(), cur_block) == blk.preds.end()) {
          blk.preds.push_back(cur_block);
        }
        // Add the existing block as a successor of cur's block.
        auto& cur_blk = blocks[cur_block];
        if (std::find(cur_blk.succs.begin(), cur_blk.succs.end(), existing_block) == cur_blk.succs.end()) {
          cur_blk.succs.push_back(existing_block);
        }
        // Populate the JOIN/STATE PHI's incoming for this predecessor.
        if (arena.node_kinds[ce.dst.value] == NodeKind::JOIN ||
            arena.node_kinds[ce.dst.value] == NodeKind::STATE) {
          for (auto& phi : blocks[existing_block].phis) {
            if (phi.origin.value == ce.dst.value) {
              // Add this predecessor's incoming.
              phi.incomings.push_back(cur);
              phi.block_ids.push_back(cur_block);
            }
          }
        }
        continue;
      }
      std::uint32_t target_block;
      if (first && arena.node_kinds[ce.dst.value] != NodeKind::HANDLER) {
        // First successor continues the current block (unless it's a HANDLER,
        // which always starts a new block per spec 7.2).
        target_block = cur_block;
        first = false;
      } else {
        // Subsequent successors start new blocks.
        target_block = new_block();
        // The current block has this new block as a successor.
        blocks[cur_block].succs.push_back(target_block);
        blocks[target_block].preds.push_back(cur_block);
      }
      node_to_block[ce.dst.value] = target_block;
      add_to_block(target_block, ce.dst);
      queue.push(ce.dst);
      visited.insert(ce.dst.value);
    }
  }

  cfg.blocks = std::move(blocks);
  return cfg;
}

void print_cfg(const MachineCFG& cfg, const GraphArena* arena) {
  std::println("MachineCFG: {} block(s), entry=block#{}",
               cfg.blocks.size(), cfg.entry_block_id);
  for (const auto& blk : cfg.blocks) {
    std::println("  Block #{}: {} phi(s), {} op(s), preds={}, succs={}",
                 blk.id, blk.phis.size(), blk.ops.size(),
                 blk.preds.size(), blk.succs.size());
    for (const auto& phi : blk.phis) {
      const char* kind_name = "?";
      if (arena && phi.origin.valid() && phi.origin.value < arena->node_count()) {
        kind_name = node_kind_name(arena->node_kinds[phi.origin.value]).data();
      }
      std::println("    PHI (from {} = node #{}, {} incomings)",
                   kind_name,
                   static_cast<std::uint32_t>(phi.origin.value),
                   phi.incomings.size());
    }
    for (const auto& op : blk.ops) {
      std::println("    {} (node #{})",
                   op.label, static_cast<std::uint32_t>(op.origin.value));
    }
  }
}

}  // namespace dgw
