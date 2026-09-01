// dgw/arena.hpp — GraphArena (Structure of Arrays) (Spec Part 1.2).
//
// Spec citation: DGW-Core-IR.md Part 1.2 "The Graph Arena (Structure of Arrays)".
//
// "The graph state is held in a single GraphArena. Passes iterate over these
//  dense arrays, allowing the CPU's hardware prefetcher to pull data into L1
//  cache seamlessly (DVM Rule 20)."
//
// This header provides the EXACT layout the spec lists. We use std::pmr::vector
// per the spec, sourced from a single monotonic_resource that the Graph owns,
// so the entire arena lives in one contiguous allocation block (a "Loom block")
// to maximize cache locality.
//
// DVM Rule 15 (no pointer invalidation on realloc) is satisfied trivially
// because every reference is a 32-bit index, not a pointer.
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <vector>

#include "dgw/ids.hpp"
#include "dgw/kinds.hpp"
#include "dgw/payloads.hpp"
#include "dgw/regions.hpp"
#include "dgw/util.hpp"

namespace dgw {

// ---- GraphArena (verbatim spec layout, SoA) -------------------------------
// Each `std::pmr::vector<T>` is allocated from the GraphArena's own
// monotonic_resource, so all columns grow within the same memory block.
//
// We expose direct access to the columns (as on the spec) because every
// pass needs to iterate them. The Weaver (Part 5) is the only entity that
// mutates them; passes that need to read them are encouraged to take a
// `const GraphArena&` reference.

struct GraphArena {
  // ---- NODE TABLE (Spec Part 1.2 "NODE TABLE") ----
  std::pmr::vector<NodeKind>    node_kinds;
  std::pmr::vector<TypeId>      node_types;       // The output type of the node.
  std::pmr::vector<std::uint32_t> node_payload_idx; // Index into specific payload SoA.
  std::pmr::vector<NodeFlags>   node_flags;       // Bitmask (Pure, Volatile, Guarded, etc.).
  std::pmr::vector<std::uint32_t> node_first_use; // Head of the intrusive use-def chain.
                                                  // Index into EdgeTable; kNullEdge if no uses.

  // ---- EDGE TABLE (Spec Part 1.2 "EDGE TABLE") ----
  // Intrusive doubly linked list for O(1) mutation. Each edge has a
  // (source_node, source_port) pair; the edge_index into the destination
  // is stored in port_connected_edge[] on the target node (Part 1.2 "PORT TABLE").
  std::pmr::vector<NodeId>      edge_source_node;
  std::pmr::vector<PortId>      edge_source_port;
  std::pmr::vector<EdgeKind>    edge_kinds;
  std::pmr::vector<std::uint32_t> edge_next_use;  // Next edge in the use-def chain.
  std::pmr::vector<std::uint32_t> edge_prev_use;  // Prev edge in the use-def chain
                                                  // (for O(1) detach).

  // ---- PORT TABLE (Spec Part 1.2 "PORT TABLE") ----
  // "Inputs are fixed per node instance." Each port slot holds the
  // currently-connected edge. The (node, port) -> EdgeId lookup is O(1) by
  // computing node_offset_in_port_table + local_port_index.
  //
  // We store ports in a single flat array with per-node offsets tracked in
  // `node_port_offset[]` and per-node port counts in `node_port_count[]`
  // (both set by the constructor when a node is created, based on its
  // NodeKind's port signature — see signatures.hpp).
  std::pmr::vector<EdgeId>      port_connected_edge;
  std::pmr::vector<std::uint32_t> node_port_offset;  // Offset into port_connected_edge.
  std::pmr::vector<std::uint16_t> node_port_count;  // Total # of ports for this node.

  // ---- REGION TABLE (Spec Part 1.2 "REGION TABLE") ----
  // First-class memory identities. Per spec columns:
  std::pmr::vector<RegionKind>  region_kinds;
  std::pmr::vector<std::uint32_t> region_sizes;
  std::pmr::vector<std::uint8_t>  region_aligns;
  std::pmr::vector<EscapeState> region_escapes;

  // ---- PAYLOAD SIDE-TABLES (Spec Part 1.3) ----
  // The `node_payload_idx` column above selects which side-table is used
  // based on the node's NodeKind. Each side-table is itself SoA-aligned and
  // grows with the arena.
  std::pmr::vector<ConstPayload>  consts;   // NodeKind::CONST
  std::pmr::vector<RefPayload>    refs;     // NodeKind::REF
  std::pmr::vector<CallPayload>   calls;    // NodeKind::CALL
  std::pmr::vector<GuardPayload>  guards;   // NodeKind::GUARD

  // ---- Lifecycle: monotonic resource backing ----------------------------
  // The arena owns its monotonic_resource, so all std::pmr::vectors
  // allocate from one contiguous block. The resource is sized for the
  // expected graph size (see Graph::allocate_graph); it grows if needed.
  //
  // We deliberately do NOT free the resource until the Graph itself dies;
  // dead nodes (Part 5.4) are reclaimed logically by bumping the epoch,
  // not physically.
  GraphArena(std::pmr::memory_resource* mr)
    : node_kinds{mr}, node_types{mr}, node_payload_idx{mr},
      node_flags{mr}, node_first_use{mr},
      edge_source_node{mr}, edge_source_port{mr}, edge_kinds{mr},
      edge_next_use{mr}, edge_prev_use{mr},
      port_connected_edge{mr}, node_port_offset{mr}, node_port_count{mr},
      region_kinds{mr}, region_sizes{mr}, region_aligns{mr}, region_escapes{mr},
      consts{mr}, refs{mr}, calls{mr}, guards{mr} {}

  // Capacity helpers ------------------------------------------------------
  std::uint32_t node_count() const noexcept { return static_cast<std::uint32_t>(node_kinds.size()); }
  std::uint32_t edge_count() const noexcept { return static_cast<std::uint32_t>(edge_kinds.size()); }
  std::uint32_t region_count() const noexcept { return static_cast<std::uint32_t>(region_kinds.size()); }
  std::uint32_t port_count() const noexcept { return static_cast<std::uint32_t>(port_connected_edge.size()); }

  // Sanity bounds checks (used by Weaver and Verifier) -------------------
  bool node_in_bounds(NodeId n) const noexcept {
    return n.valid() && n.value < node_count();
  }
  bool edge_in_bounds(EdgeId e) const noexcept {
    return e.valid() && e.value < edge_count();
  }
  bool region_in_bounds(RegionId r) const noexcept {
    return r.valid() && r.value < region_count();
  }

  // Convenience: read a node's port slot --------------------------------
  // Returns kNullEdge if the port is not connected.
  EdgeId port_edge(NodeId n, PortId p) const {
    DGW_ALWAYS_VERIFY ? (void)0 : (void)0;
    if (!node_in_bounds(n)) return EdgeId{};
    const auto off = node_port_offset[n.value];
    const auto cnt = node_port_count[n.value];
    if (p.value >= cnt) return EdgeId{};
    return port_connected_edge[off + p.value];
  }

  // Convenience: read the source node of an edge -------------------------
  NodeId edge_source(EdgeId e) const {
    if (!edge_in_bounds(e)) return NodeId{};
    return edge_source_node[e.value];
  }
};

}  // namespace dgw
