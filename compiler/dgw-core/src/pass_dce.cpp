// src/pass_dce.cpp — Dead Code Elimination (Spec Part 6.2).
//
// Spec citation: DGW-Core-IR.md Part 6.2 "Dead Code Elimination (DCE)".
//
// "1. Seed a worklist with all Observable nodes (RETURN, STORE to global,
//      I/O, DEOPT_TRAP).
//  2. Walk backwards along VALUE, CONTROL, MEMORY, and EFFECT edges,
//      marking nodes as Live.
//  3. Any node not marked Live is passed to weaver.kill_node()."
//
#include "dgw/pass_dce.hpp"

#include <cstdint>
#include <vector>

namespace dgw {

DceStats pass_dce(Weaver& w) {
  DceStats stats;
  auto& arena = w.arena();

  // 1. Seed a worklist with all Observable nodes.
  std::vector<std::uint32_t> worklist;
  std::vector<std::uint8_t> live(arena.node_count(), 0);
  for (std::uint32_t n = 0; n < arena.node_count(); ++n) {
    if (arena.node_kinds[n] == NodeKind::DEAD) continue;
    if (w.is_observable(NodeId{n})) {
      live[n] = 1;
      worklist.push_back(n);
    }
  }
  // 2. Walk backwards along VALUE/CONTROL/MEMORY/EFFECT edges.
  while (!worklist.empty()) {
    std::uint32_t n = worklist.back();
    worklist.pop_back();
    const std::uint32_t off = arena.node_port_offset[n];
    const std::uint16_t cnt = arena.node_port_count[n];
    for (std::uint16_t p = 0; p < cnt; ++p) {
      EdgeId e{arena.port_connected_edge[off + p]};
      if (!e.valid()) continue;
      const EdgeKind k = arena.edge_kinds[e.value];
      if (k != EdgeKind::VALUE && k != EdgeKind::CONTROL &&
          k != EdgeKind::MEMORY && k != EdgeKind::EFFECT) continue;
      const std::uint32_t src = arena.edge_source_node[e.value].value;
      if (live[src]) continue;
      live[src] = 1;
      worklist.push_back(src);
    }
  }
  // 3. Any node not marked live is killed.
  for (std::uint32_t n = 0; n < arena.node_count(); ++n) {
    if (arena.node_kinds[n] == NodeKind::DEAD) continue;
    if (live[n]) { stats.live++; continue; }
    w.kill_node(NodeId{n});
    stats.killed++;
  }
  return stats;
}

}  // namespace dgw
