// dgw/weaver.hpp — The Weaver (Graph Mutation API) (Spec Part 5).
//
// Spec citation: DGW-Core-IR.md Part 5 "The Weaver (Graph Mutation API)".
//
// "DVM Rule 10 demands idempotent, fast passes. We do not use 'rollback'
//  mechanisms. The Weaver class provides O(1) or O(U) (where U is use count)
//  mutation primitives."
//
// This file declares the Weaver. The implementation lives in src/weaver.cpp
// because it is large enough to warrant its own translation unit.
//
// The Weaver is the ONLY entity that mutates a GraphArena's columns. Passes
// request mutations via the Weaver; the Weaver updates the intrusive
// use-def chains and the PORT TABLE atomically (per-op) so that the
// WebVerifier (Part 8) can run between any two operations.
//
#pragma once

#include <cstdint>

#include "dgw/ids.hpp"
#include "dgw/kinds.hpp"
#include "dgw/payloads.hpp"
#include "dgw/regions.hpp"
#include "dgw/arena.hpp"
#include "dgw/signatures.hpp"
#include "dgw/util.hpp"

namespace dgw {

// The Weaver operates on a GraphArena. The Graph facade (graph.hpp) owns
// the arena, the Weaver, the epoch counter, and the memory_resource; the
// Weaver is constructed from a non-owning pointer to the arena.
class Weaver {
 public:
  explicit Weaver(GraphArena* arena) noexcept : arena_(arena) {}

  // ---- Node creation --------------------------------------------------
  // create_node allocates a node, assigns it a NodeKind, sets its port
  // slots from the canonical signature, and returns its NodeId. The node
  // has no connected edges initially; all port slots hold kNullEdge.
  //
  // For variadic node kinds (JOIN, CALL, STATE), the caller must pass
  // `extra_inputs` / `extra_outputs` to extend the canonical port layout;
  // the Weaver copies the canonical prefix and extends with VALUE-typed
  // slots (the only legal extension kind per the spec).
  NodeId create_node(NodeKind kind,
                     std::uint16_t extra_inputs = 0,
                     std::uint16_t extra_outputs = 0);

  // Convenience overloads for typed node creation. Each one calls
  // create_node then attaches the payload (if any).
  NodeId create_const(std::int64_t v);
  NodeId create_const(double v);
  NodeId create_const(SymbolId v);
  NodeId create_ref(RegionId r, std::int64_t offset, AccessPerm perms);
  NodeId create_alloc(RegionKind rk, std::uint32_t size, std::uint8_t align);
  NodeId create_load(NodeId ctrl, NodeId mem, NodeId ref_value);
  NodeId create_store(NodeId ctrl, NodeId mem, NodeId ref_value, NodeId value);
  NodeId create_call(SymbolId target, CallConv conv,
                     std::uint16_t arg_count, bool can_throw,
                     NodeId ctrl, NodeId mem);

  // Connect argument #i (zero-indexed) of a CALL node to the given VALUE.
  // The CALL's port layout is: [0]=CONTROL, [1]=MEMORY, [2+i]=VALUE arg i.
  // Use after create_call() to fill in each argument.
  void call_connect_arg(NodeId call_node, std::uint16_t arg_index, NodeId value);
  NodeId create_branch(NodeId ctrl, NodeId cond);
  NodeId create_join(std::uint16_t n_inputs);  // n_inputs control + n_inputs value
  NodeId create_state();
  NodeId create_handler();
  NodeId create_return(NodeId ctrl, NodeId value);
  NodeId create_deopt_trap();
  NodeId create_uncommon_trap();
  NodeId create_guard(FrameStateId fs, DeoptReason reason);
  NodeId create_materialize(NodeId ctrl, NodeId mem, NodeId virtual_ref);
  NodeId create_arith(NodeKind kind, NodeId lhs, NodeId rhs);  // ADD/SUB/MUL/...
  NodeId create_cmp(NodeKind kind, NodeId lhs, NodeId rhs);     // CMP_EQ/...
  NodeId create_cast(NodeId in);
  NodeId create_memory_join(NodeId mem0, NodeId mem1);
  NodeId create_start();

  // ---- Edge creation --------------------------------------------------
  // connect plugs an output port of `src` into an input port of `dst`.
  // The Weaver checks the EdgeKind compatibility against the canonical
  // signature; mismatch returns kNullEdge and sets last_error().
  //
  // The port_connected_edge[] entry on `dst` is set to the new edge id.
  // The use-def chain on `src` is updated: the new edge is inserted at the
  // head of node_first_use[src].
  //
  // If `dst`'s input port is already connected, the existing edge is
  // detached first (and its slot in edge_* is left dangling but no longer
  // reachable from any node).
  EdgeId connect(NodeId src, PortId src_port,
                 NodeId dst, PortId dst_port,
                 EdgeKind kind);

  // Convenience: connect the single VALUE output of `src` (port 0) into
  // input port `dst_port` of `dst` as a VALUE edge.
  EdgeId connect_value(NodeId src, NodeId dst, PortId dst_port);

  // Convenience: connect the CONTROL output of `src` (port 0) into input
  // port 0 of `dst` as a CONTROL edge.
  EdgeId connect_control(NodeId src, NodeId dst);

  // Convenience: connect the MEMORY output of `src` into the MEMORY input
  // (port 1) of `dst` as a MEMORY edge. Used by LOAD/STORE/CALL chains.
  EdgeId connect_memory(NodeId src, NodeId dst);

  // ---- Mutation primitives (Spec Part 5) ------------------------------

  // 5.1  rewire_uses(old, new_id):
  //   Replace all uses of `old` with `new_id`. O(U) where U is use count.
  //   Walks node_first_use[old], updates each edge's source_node to new_id,
  //   moves the list head to node_first_use[new_id].
  void rewire_uses(NodeId old_node, NodeId new_node);

  // 5.2  forward_node(old_node, new_node):
  //   The O(1) forwarding trick. Mutates `old_node` in-place: sets its
  //   NodeKind to FWD, removes all its inputs, attaches `new_node` as its
  //   single VALUE input. The users of `old_node` continue to read it as
  //   `old_node` (no edge mutation required); a later CleanupPass collapses
  //   FWD chains in a single linear sweep.
  //
  //   ** Complexity: O(1) ** (spec Part 5.2).
  void forward_node(NodeId old_node, NodeId new_node);

  // 5.3  splice_into_edge(edge, dst, dst_port, new_node, new_node_in_port, new_node_out_port):
  //   Insert `new_node` into the middle of `edge`: the edge's source now
  //   feeds new_node's input port `new_node_in_port`, and new_node's output
  //   port `new_node_out_port` feeds `dst`'s input port `dst_port`.
  //
  //   The caller supplies (dst, dst_port) so the Weaver does not need to scan
  //   the arena to find the edge's destination (which would be O(N*P)).
  //   With the dst supplied, this operation is **O(1)** per spec Part 5.3.
  //
  //   Precondition: `edge` is currently connected to (dst, dst_port). The
  //   Weaver asserts this in debug builds.
  void splice_into_edge(EdgeId edge,
                        NodeId dst, PortId dst_port,
                        NodeId new_node,
                        PortId new_node_in_port,
                        PortId new_node_out_port);

  // 5.4  kill_node(node):
  //   Marks `node` as dead. Disconnects it from the graph (clears its
  //   input ports; the use-def chain of its former inputs is repaired).
  //   Memory is NOT freed; reclaimed in bulk at epoch end (DVM Rules 7, 14).
  void kill_node(NodeId node);

  // ---- Epoch reclamation (DVM Rules 7, 14) ---------------------------
  // At the end of a compilation epoch, walk the arena and physically remove
  // dead nodes. This is the only operation that changes the NodeId
  // *space* — and it does so by compacting. Passes that hold NodeId
  // values across reclaim_dead_nodes() must consult a remap table that
  // the Weaver produces. By default, passes do not hold NodeIds across an
  // epoch boundary; the typical pattern is:
  //     weaver.kill_node(...);          // logical death
  //     ... pass continues ...
  //     weaver.reclaim_dead_nodes();    // physical compaction at epoch end
  struct RemapEntry { NodeId old_id; NodeId new_id; };
  std::vector<RemapEntry> reclaim_dead_nodes();

  // ---- Read-only introspection --------------------------------------
  // Used by passes; do NOT mutate through these.
  NodeKind kind_of(NodeId n) const;
  TypeId  type_of(NodeId n) const;
  NodeFlags flags_of(NodeId n) const;
  bool    is_pure(NodeId n) const;
  bool    is_observable(NodeId n) const;
  bool    is_dead(NodeId n) const;

  EdgeKind edge_kind(EdgeId e) const;
  NodeId   edge_source(EdgeId e) const;
  PortId   edge_source_port(EdgeId e) const;

  EdgeId   first_use(NodeId n) const;        // head of use-def chain
  EdgeId   next_use(EdgeId e) const;         // next edge in same chain
  EdgeId   prev_use(EdgeId e) const;
  std::uint32_t use_count(NodeId n) const;   // O(U) — walk the chain.

  EdgeId   input_edge(NodeId n, PortId p) const;
  NodeId   input_node(NodeId n, PortId p) const;  // convenience: source of input_edge.

  // ---- Diagnostics ---------------------------------------------------
  std::string_view last_error() const noexcept { return last_error_; }
  void clear_error() noexcept { last_error_ = {}; }

  GraphArena& arena() noexcept { return *arena_; }
  const GraphArena& arena() const noexcept { return *arena_; }

 private:
  // Internal: allocate N port slots and return the offset into port_connected_edge.
  std::uint32_t alloc_ports_(std::uint16_t count);

  // Internal: insert `e` at the head of node_first_use[src].
  void use_chain_push_front_(NodeId src, EdgeId e);
  // Internal: detach `e` from its current use-def chain.
  void use_chain_detach_(EdgeId e);

  GraphArena* arena_;
  std::string_view last_error_{};

  // Free-list of dead node slots, populated by reclaim_dead_nodes and
  // consumed by create_node. Each entry is the NodeId of a DEAD node whose
  // port array is still allocated and may be reused.
  std::vector<NodeId> node_free_list_{};
};

}  // namespace dgw
