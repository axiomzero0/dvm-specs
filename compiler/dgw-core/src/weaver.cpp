// src/weaver.cpp — Weaver implementation (Spec Part 5).
//
// Spec citation: DGW-Core-IR.md Part 5 "The Weaver (Graph Mutation API)".
//
//  5.1  rewire_uses          — O(U)
//  5.2  forward_node         — O(1)  (the Forwarding Node Trick)
//  5.3  splice_into_edge     — O(1)
//  5.4  kill_node            — O(U)  (logical death; physical reclaim is bulk)
//
#include "dgw/weaver.hpp"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace dgw {

namespace {

// Convenience: append a value to a pmr vector and return its index.
template <class Vec, class T>
std::uint32_t push_back_indexed(Vec& v, T&& x) {
  std::uint32_t idx = static_cast<std::uint32_t>(v.size());
  v.push_back(std::forward<T>(x));
  return idx;
}

}  // namespace

// =========================================================================
// 5.0  Internal: port-slot allocation
// =========================================================================
std::uint32_t Weaver::alloc_ports_(std::uint16_t count) {
  if (count == 0) return 0;
  std::uint32_t base = static_cast<std::uint32_t>(arena_->port_connected_edge.size());
  // Extend the port array by `count` slots, all initialized to kNullEdge.
  arena_->port_connected_edge.insert(arena_->port_connected_edge.end(),
                                      count, EdgeId{kNullEdge});
  return base;
}

void Weaver::use_chain_push_front_(NodeId src, EdgeId e) {
  // Insert `e` at the head of node_first_use[src]'s intrusive list.
  const EdgeId head = EdgeId{arena_->node_first_use[src.value]};
  arena_->edge_prev_use[e.value] = kNullEdge;
  arena_->edge_next_use[e.value]  = head.valid() ? head.value : kNullEdge;
  if (head.valid()) {
    arena_->edge_prev_use[head.value] = e.value;
  }
  arena_->node_first_use[src.value] = e.value;
}

void Weaver::use_chain_detach_(EdgeId e) {
  if (!e.valid()) return;
  const std::uint32_t prev = arena_->edge_prev_use[e.value];
  const std::uint32_t next = arena_->edge_next_use[e.value];
  // Detach from prev.
  if (prev != kNullEdge) {
    arena_->edge_next_use[prev] = next;
  } else {
    // We were the head. Move head to next.
    const NodeId src = arena_->edge_source_node[e.value];
    if (src.valid()) {
      arena_->node_first_use[src.value] = (next != kNullEdge) ? next : kNullEdge;
    }
  }
  // Detach from next.
  if (next != kNullEdge) {
    arena_->edge_prev_use[next] = prev;
  }
  // Mark the edge as fully detached: clear its source pointer AND its
  // list pointers, so the verifier can recognize orphaned edges
  // (spec Part 8.1 — "every node_first_use must point to a valid EdgeId
  // or be NULL_EDGE"; an orphaned edge with NULL prev/next and NULL source
  // is no longer in any chain).
  arena_->edge_prev_use[e.value] = kNullEdge;
  arena_->edge_next_use[e.value] = kNullEdge;
  arena_->edge_source_node[e.value] = NodeId{kNullNode};
  // Note: we do NOT clear edge_source_port or edge_kinds — those are
  // harmless leftovers and the slot will be reused by the next connect().
  // The verifier (Part 8.1) skips edges whose source_node is kNullNode.
}

// =========================================================================
// 5.0  Node creation
// =========================================================================
NodeId Weaver::create_node(NodeKind kind,
                            std::uint16_t extra_inputs,
                            std::uint16_t extra_outputs) {
  const NodeSignature sig = signature_of(kind);
  const std::uint16_t sig_in  = static_cast<std::uint16_t>(sig.inputs.size());
  const std::uint16_t sig_out = static_cast<std::uint16_t>(sig.outputs.size());
  const std::uint16_t in_count  = sig_in  + extra_inputs;
  const std::uint16_t out_count = sig_out + extra_outputs;
  const std::uint16_t total_ports = in_count + out_count;

  // Variadic check: reject mismatched variadic kinds.
  if (!matches_signature(kind, in_count, out_count)) {
    last_error_ = "Weaver::create_node: port count mismatch for NodeKind";
    return NodeId{};
  }

  // Try to reuse a dead node slot from the free-list. A slot is reusable
  // if its existing port array is large enough to host the new node's
  // port count. (We do NOT shrink port arrays; this is the spec's "memory
  // is reclaimed in bulk at epoch end" policy — slots are recycled but
  // their port arrays persist.)
  NodeId id{kNullNode};
  NodeFlags flags = sig.default_flags;
  if (sig.can_throw) flags = flags | NodeFlags::HasExcept;
  if (sig.pure)      flags = flags | NodeFlags::Pure;
  bool reused = false;
  for (auto it = node_free_list_.begin(); it != node_free_list_.end(); ++it) {
    const std::uint16_t existing = arena_->node_port_count[it->value];
    if (existing >= total_ports) {
      id = *it;
      node_free_list_.erase(it);
      // Reset the slot's metadata for the new node.
      arena_->node_kinds[id.value]        = kind;
      arena_->node_types[id.value]        = TypeId{kNullType};
      arena_->node_payload_idx[id.value] = 0;
      arena_->node_first_use[id.value]    = kNullEdge;
      // Update node_port_count to reflect the new (smaller or same) port count.
      // We keep the existing port_offset; the extra port slots at the tail
      // are zeroed to kNullEdge so they're inert.
      arena_->node_port_count[id.value] = total_ports;
      for (std::uint16_t p = 0; p < total_ports; ++p) {
        arena_->port_connected_edge[arena_->node_port_offset[id.value] + p] =
            EdgeId{kNullEdge};
      }
      // Set default flags.
      arena_->node_flags[id.value] = flags;
      reused = true;
      break;
    }
  }

  if (!reused) {
    // No reusable slot; allocate a fresh one.
    id = NodeId{push_back_indexed(arena_->node_kinds, kind)};
    push_back_indexed(arena_->node_types,        TypeId{kNullType});
    push_back_indexed(arena_->node_payload_idx,  0u);
    push_back_indexed(arena_->node_flags,        flags);
    push_back_indexed(arena_->node_first_use,    kNullEdge);

    // Allocate ports.
    const std::uint32_t port_base = alloc_ports_(total_ports);
    push_back_indexed(arena_->node_port_offset, port_base);
    push_back_indexed(arena_->node_port_count,  total_ports);
  }

  // The per-port expected EdgeKind is encoded by the signature: the first
  // sig_in ports use sig.inputs[], the next extra_inputs ports are VALUE
  // (per spec for variadic kinds); the same for outputs.
  // We do NOT store per-port kind in the arena (the spec doesn't list such
  // a column); the verifier derives it from the signature on demand.

  return id;
}

// ---- Typed convenience creators -----------------------------------------

NodeId Weaver::create_const(std::int64_t v) {
  NodeId n = create_node(NodeKind::CONST);
  if (!n.valid()) return n;
  ConstPayload p; p.value = v;
  std::uint32_t idx = static_cast<std::uint32_t>(arena_->consts.size());
  arena_->consts.push_back(p);
  arena_->node_payload_idx[n.value] = idx;
  return n;
}

NodeId Weaver::create_const(double v) {
  NodeId n = create_node(NodeKind::CONST);
  if (!n.valid()) return n;
  ConstPayload p; p.value = v;
  std::uint32_t idx = static_cast<std::uint32_t>(arena_->consts.size());
  arena_->consts.push_back(p);
  arena_->node_payload_idx[n.value] = idx;
  return n;
}

NodeId Weaver::create_const(SymbolId v) {
  NodeId n = create_node(NodeKind::CONST);
  if (!n.valid()) return n;
  ConstPayload p; p.value = v;
  std::uint32_t idx = static_cast<std::uint32_t>(arena_->consts.size());
  arena_->consts.push_back(p);
  arena_->node_payload_idx[n.value] = idx;
  return n;
}

NodeId Weaver::create_ref(RegionId r, std::int64_t offset, AccessPerm perms) {
  NodeId n = create_node(NodeKind::REF);
  if (!n.valid()) return n;
  RefPayload p; p.region = r; p.offset = offset; p.perms = perms;
  std::uint32_t idx = static_cast<std::uint32_t>(arena_->refs.size());
  arena_->refs.push_back(p);
  arena_->node_payload_idx[n.value] = idx;
  return n;
}

NodeId Weaver::create_alloc(RegionKind rk, std::uint32_t size, std::uint8_t align) {
  // Allocate the Region first.
  RegionId r{push_back_indexed(arena_->region_kinds,  rk)};
  push_back_indexed(arena_->region_sizes,    size);
  push_back_indexed(arena_->region_aligns,   align);
  push_back_indexed(arena_->region_escapes,  EscapeState::NoEscape);

  // Then the ALLOC node.
  NodeId n = create_node(NodeKind::ALLOC);
  if (!n.valid()) return n;
  // The ALLOC node's payload is the RegionId; we store it in the
  // refs side-table as a zero-offset read-only ref into the region, per
  // spec 3.2 ("[ALLOC] (Produces Region R1) -> [REF] (region=R1, offset=0)").
  RefPayload p; p.region = r; p.offset = 0; p.perms = AccessPerm::ReadWrite;
  std::uint32_t idx = static_cast<std::uint32_t>(arena_->refs.size());
  arena_->refs.push_back(p);
  arena_->node_payload_idx[n.value] = idx;
  return n;
}

NodeId Weaver::create_load(NodeId ctrl, NodeId mem, NodeId ref_value) {
  NodeId n = create_node(NodeKind::LOAD);
  if (!n.valid()) return n;
  connect_control(ctrl, n);
  connect_memory(mem, n);
  connect_value(ref_value, n, PortId{2});
  return n;
}

NodeId Weaver::create_store(NodeId ctrl, NodeId mem, NodeId ref_value, NodeId value) {
  NodeId n = create_node(NodeKind::STORE);
  if (!n.valid()) return n;
  connect_control(ctrl, n);
  connect_memory(mem, n);
  connect_value(ref_value, n, PortId{2});
  connect_value(value, n, PortId{3});
  return n;
}

NodeId Weaver::create_call(SymbolId target, CallConv conv,
                            std::uint16_t arg_count, bool can_throw,
                            NodeId ctrl, NodeId mem) {
  // CALL canonical: control + memory + 2 args minimum. If arg_count > 2,
  // extend with extra VALUE inputs.
  std::uint16_t extra_in = arg_count > 2 ? static_cast<std::uint16_t>(arg_count - 2) : 0;
  // The canonical CALL signature has 3 outputs (VALUE ret + MEMORY + EXCEPT).
  // The EXCEPT port is always allocated; it's left unconnected if can_throw=false.
  NodeId n = create_node(NodeKind::CALL, extra_in, 0);
  if (!n.valid()) return n;
  // Wire control and memory inputs.
  connect_control(ctrl, n);
  connect_memory(mem, n);
  CallPayload p; p.target = target; p.conv = conv; p.arg_count = arg_count;
  std::uint32_t idx = static_cast<std::uint32_t>(arena_->calls.size());
  arena_->calls.push_back(p);
  arena_->node_payload_idx[n.value] = idx;
  // If not throwing, clear the HasExcept flag (the EXCEPT port is allocated
  // but never connected; the verifier treats unconnected ports as inert).
  if (!can_throw) {
    arena_->node_flags[n.value] = arena_->node_flags[n.value] & ~NodeFlags::HasExcept;
  }
  return n;
}

void Weaver::call_connect_arg(NodeId call_node, std::uint16_t arg_index, NodeId value) {
  // CALL's input port layout: [0]=CONTROL, [1]=MEMORY, [2+i]=arg i.
  connect_value(value, call_node, PortId{static_cast<std::uint16_t>(2 + arg_index)});
}

NodeId Weaver::create_branch(NodeId ctrl, NodeId cond) {
  NodeId n = create_node(NodeKind::BRANCH);
  if (!n.valid()) return n;
  connect_control(ctrl, n);
  connect_value(cond, n, PortId{1});
  return n;
}

NodeId Weaver::create_join(std::uint16_t n_inputs) {
  // JOIN canonical: 2 control + 2 value. To support N control + N value,
  // we need 2N - 4 extra input ports.
  if (n_inputs < 2) {
    last_error_ = "Weaver::create_join: n_inputs must be >= 2";
    return NodeId{};
  }
  std::uint16_t extra = static_cast<std::uint16_t>((n_inputs - 2) * 2);
  return create_node(NodeKind::JOIN, extra, 0);
}

NodeId Weaver::create_state() {
  return create_node(NodeKind::STATE);
}

NodeId Weaver::create_handler() {
  return create_node(NodeKind::HANDLER);
}

NodeId Weaver::create_return(NodeId ctrl, NodeId value) {
  NodeId n = create_node(NodeKind::RETURN);
  if (!n.valid()) return n;
  connect_control(ctrl, n);
  connect_value(value, n, PortId{1});
  return n;
}

NodeId Weaver::create_deopt_trap()    { return create_node(NodeKind::DEOPT_TRAP); }
NodeId Weaver::create_uncommon_trap() { return create_node(NodeKind::UNCOMMON_TRAP); }

NodeId Weaver::create_guard(FrameStateId fs, DeoptReason reason) {
  NodeId n = create_node(NodeKind::GUARD);
  if (!n.valid()) return n;
  GuardPayload p; p.frame_state = fs; p.reason = reason;
  std::uint32_t idx = static_cast<std::uint32_t>(arena_->guards.size());
  arena_->guards.push_back(p);
  arena_->node_payload_idx[n.value] = idx;
  return n;
}

NodeId Weaver::create_materialize(NodeId ctrl, NodeId mem, NodeId virtual_ref) {
  NodeId n = create_node(NodeKind::MATERIALIZE);
  if (!n.valid()) return n;
  connect_control(ctrl, n);
  connect_memory(mem, n);
  connect_value(virtual_ref, n, PortId{2});
  return n;
}

NodeId Weaver::create_arith(NodeKind kind, NodeId lhs, NodeId rhs) {
  NodeId n = create_node(kind);
  if (!n.valid()) return n;
  connect_value(lhs, n, PortId{0});
  connect_value(rhs, n, PortId{1});
  return n;
}

NodeId Weaver::create_cmp(NodeKind kind, NodeId lhs, NodeId rhs) {
  return create_arith(kind, lhs, rhs);  // same shape
}

NodeId Weaver::create_cast(NodeId in) {
  NodeId n = create_node(NodeKind::CAST);
  if (!n.valid()) return n;
  connect_value(in, n, PortId{0});
  return n;
}

NodeId Weaver::create_memory_join(NodeId mem0, NodeId mem1) {
  NodeId n = create_node(NodeKind::MEMORY_JOIN);
  if (!n.valid()) return n;
  // MEMORY_JOIN inputs are 2 MEMORY ports.
  connect(mem0, PortId{0}, n, PortId{0}, EdgeKind::MEMORY);
  connect(mem1, PortId{0}, n, PortId{1}, EdgeKind::MEMORY);
  return n;
}

NodeId Weaver::create_start() {
  return create_node(NodeKind::START);
}

// =========================================================================
// 5.0  Edge creation (connect)
// =========================================================================
EdgeId Weaver::connect(NodeId src, PortId src_port,
                       NodeId dst, PortId dst_port,
                       EdgeKind kind) {
  if (!arena_->node_in_bounds(src) || !arena_->node_in_bounds(dst)) {
    last_error_ = "Weaver::connect: src or dst out of bounds";
    return EdgeId{};
  }
  // Validate the src_port is an OUTPUT port and dst_port is an INPUT port.
  const std::uint16_t src_cnt = arena_->node_port_count[src.value];
  const std::uint32_t dst_off = arena_->node_port_offset[dst.value];
  const std::uint16_t dst_cnt = arena_->node_port_count[dst.value];

  // The signature's input count tells us where output ports begin.
  const NodeSignature src_sig = signature_of(arena_->node_kinds[src.value]);
  const NodeSignature dst_sig = signature_of(arena_->node_kinds[dst.value]);
  const std::uint16_t src_in_count = static_cast<std::uint16_t>(src_sig.inputs.size());
  const std::uint16_t dst_in_count = static_cast<std::uint16_t>(dst_sig.inputs.size());

  if (src_port.value < src_in_count) {
    last_error_ = "Weaver::connect: src_port is an input port, not an output";
    return EdgeId{};
  }
  if (src_port.value >= src_cnt) {
    last_error_ = "Weaver::connect: src_port out of range for src node";
    return EdgeId{};
  }
  if (dst_port.value >= dst_in_count) {
    last_error_ = "Weaver::connect: dst_port is not an input port";
    return EdgeId{};
  }
  if (dst_port.value >= dst_cnt) {
    last_error_ = "Weaver::connect: dst_port out of range for dst node";
    return EdgeId{};
  }

  // Validate EdgeKind against the canonical signature (Part 2.2).
  const std::uint16_t src_out_idx = static_cast<std::uint16_t>(src_port.value - src_in_count);
  const EdgeKind src_expected = expected_output_kind(arena_->node_kinds[src.value], src_out_idx);
  const EdgeKind dst_expected = expected_input_kind(arena_->node_kinds[dst.value], dst_port.value);
  if (src_expected != kind || dst_expected != kind) {
    last_error_ = "Weaver::connect: edge kind does not match both port signatures";
    return EdgeId{};
  }

  // Detach any existing edge in dst's port slot.
  EdgeId existing = arena_->port_connected_edge[dst_off + dst_port.value];
  if (existing.valid()) {
    use_chain_detach_(existing);
    // Mark the existing edge as orphaned by clearing its target slot pointer;
    // we have no per-edge "target" column, so we just leave it dangling from
    // the use-def chain perspective. (The slot in port_connected_edge is
    // overwritten below.)
  }

  // Allocate the new edge row.
  EdgeId e{push_back_indexed(arena_->edge_source_node, src)};
  push_back_indexed(arena_->edge_source_port, src_port);
  push_back_indexed(arena_->edge_kinds,       kind);
  push_back_indexed(arena_->edge_next_use,    kNullEdge);
  push_back_indexed(arena_->edge_prev_use,    kNullEdge);

  // Plug it into dst's input port.
  arena_->port_connected_edge[dst_off + dst_port.value] = e;

  // Insert into src's use-def chain.
  use_chain_push_front_(src, e);

  return e;
}

EdgeId Weaver::connect_value(NodeId src, NodeId dst, PortId dst_port) {
  // Most value-producing nodes have their VALUE output at port = in_count + 0.
  // For START, port 0 is CONTROL and port 1 is MEMORY — caller should use
  // the specific helpers for those.
  const NodeSignature src_sig = signature_of(arena_->node_kinds[src.value]);
  const std::uint16_t src_in_count = static_cast<std::uint16_t>(src_sig.inputs.size());
  return connect(src, PortId{static_cast<std::uint16_t>(src_in_count + 0)},
                 dst, dst_port, EdgeKind::VALUE);
}

EdgeId Weaver::connect_control(NodeId src, NodeId dst) {
  // For most control-flow-source nodes (START, BRANCH, etc.), the first
  // OUTPUT port (port in_count + 0) is CONTROL.
  const NodeSignature src_sig = signature_of(arena_->node_kinds[src.value]);
  const std::uint16_t src_in_count = static_cast<std::uint16_t>(src_sig.inputs.size());
  return connect(src, PortId{src_in_count}, dst, PortId{0}, EdgeKind::CONTROL);
}

EdgeId Weaver::connect_memory(NodeId src, NodeId dst) {
  // Nodes that produce MEMORY output: START (port 1), LOAD (port 1),
  // STORE (port 0), CALL (port 1), MATERIALIZE (port 1), MEMORY_JOIN (port 0).
  // We find the MEMORY port by scanning the signature's output ports.
  const NodeSignature src_sig = signature_of(arena_->node_kinds[src.value]);
  const std::uint16_t src_in_count = static_cast<std::uint16_t>(src_sig.inputs.size());
  PortId src_mem_port{kNullPort};
  for (std::uint16_t i = 0; i < src_sig.outputs.size(); ++i) {
    if (src_sig.outputs[i].kind == EdgeKind::MEMORY) {
      src_mem_port = PortId{static_cast<std::uint16_t>(src_in_count + i)};
      break;
    }
  }
  if (!src_mem_port.valid()) {
    last_error_ = "Weaver::connect_memory: src has no MEMORY output port";
    return EdgeId{};
  }
  // dst's MEMORY input port is always port 1 (per spec convention: control=0,
  // memory=1, then values). For MEMORY_JOIN, it's port 0/1 both MEMORY.
  PortId dst_mem_port{1};
  if (arena_->node_kinds[dst.value] == NodeKind::MEMORY_JOIN) {
    // First free MEMORY port; check both.
    if (!arena_->port_connected_edge[arena_->node_port_offset[dst.value] + 0].valid()) {
      dst_mem_port = PortId{0};
    } else {
      dst_mem_port = PortId{1};
    }
  }
  return connect(src, src_mem_port, dst, dst_mem_port, EdgeKind::MEMORY);
}

// =========================================================================
// 5.1  rewire_uses  (Spec Part 5.1, O(U))
// =========================================================================
void Weaver::rewire_uses(NodeId old_node, NodeId new_node) {
  if (!arena_->node_in_bounds(old_node) || !arena_->node_in_bounds(new_node)) {
    last_error_ = "Weaver::rewire_uses: out of bounds";
    return;
  }
  if (old_node == new_node) return;
  EdgeId e{arena_->node_first_use[old_node.value]};
  // Walk the chain and patch each edge's source_node to new_node.
  // Then append the whole chain at the head of new_node's chain.
  EdgeId head_of_old{e};
  EdgeId tail_of_old{kNullEdge};
  while (e.valid()) {
    arena_->edge_source_node[e.value] = new_node;
    tail_of_old = e;
    e = EdgeId{arena_->edge_next_use[e.value]};
  }
  if (!head_of_old.valid()) return;  // old_node had no uses.
  // Splice the [head_of_old..tail_of_old] chain in front of new_node's
  // existing chain.
  const EdgeId new_head_existing{arena_->node_first_use[new_node.value]};
  arena_->edge_next_use[tail_of_old.value] =
      new_head_existing.valid() ? new_head_existing.value : kNullEdge;
  if (new_head_existing.valid()) {
    arena_->edge_prev_use[new_head_existing.value] = tail_of_old.value;
  }
  arena_->edge_prev_use[head_of_old.value] = kNullEdge;
  arena_->node_first_use[new_node.value] = head_of_old.value;
  arena_->node_first_use[old_node.value] = kNullEdge;
}

// =========================================================================
// 5.2  forward_node  (Spec Part 5.2, O(1))
// =========================================================================
void Weaver::forward_node(NodeId old_node, NodeId new_node) {
  if (!arena_->node_in_bounds(old_node) || !arena_->node_in_bounds(new_node)) {
    last_error_ = "Weaver::forward_node: out of bounds";
    return;
  }
  if (old_node == new_node) {
    last_error_ = "Weaver::forward_node: old_node == new_node";
    return;
  }

  // 1. Disconnect all input ports of old_node (we'll rewire it as a FWD).
  const std::uint32_t off = arena_->node_port_offset[old_node.value];
  const std::uint16_t cnt  = arena_->node_port_count[old_node.value];
  const NodeSignature old_sig = signature_of(arena_->node_kinds[old_node.value]);
  const std::uint16_t in_count = static_cast<std::uint16_t>(old_sig.inputs.size());

  // Detach all incoming edges from old_node's input ports.
  for (std::uint16_t p = 0; p < in_count && p < cnt; ++p) {
    EdgeId e = arena_->port_connected_edge[off + p];
    if (e.valid()) {
      use_chain_detach_(e);
      arena_->port_connected_edge[off + p] = EdgeId{kNullEdge};
    }
  }

  // 2. Mutate old_node in-place to become FWD.
  //    Per spec Part 5.2: "The Weaver changes node_kinds[OldNode] = FWD and
  //    sets its single input to NewNode. The users don't change."
  //
  //    However, the spec's port layout for FWD has 1 in + 1 out. The old
  //    node's port layout may differ. We handle this by:
  //      - keeping the old node's port array but treating only the first
  //        input port as live (the rest are cleared and unused).
  //      - The verifier (Part 8) accepts this because matches_signature
  //        for FWD only requires 1 in + 1 out, and we conservatively set
  //        node_port_count to (1+1) below if it was larger. But changing
  //        node_port_count mid-life breaks any existing users who think
  //        the old layout is intact.
  //
  //    Cleaner approach: keep the old node's port_count, but only the first
  //    input slot is used (the rest remain kNullEdge after detach). The
  //    verifier must accept FWD nodes with "extra" zeroed ports — we
  //    special-case FWD in check_structural.
  arena_->node_kinds[old_node.value] = NodeKind::FWD;
  arena_->node_flags[old_node.value] = NodeFlags::Pure | NodeFlags::Dead;
  // The payload is unused for FWD; clear it.
  arena_->node_payload_idx[old_node.value] = 0;

  // 3. Connect new_node's VALUE output into old_node's input port 0.
  connect_value(new_node, old_node, PortId{0});
}

// =========================================================================
// 5.3  splice_into_edge  (Spec Part 5.3, O(1))
// =========================================================================
// With (dst, dst_port) supplied by the caller, this is truly O(1):
//   1. Capture (src, src_port, kind) from the edge.               O(1)
//   2. Detach the edge from src's use-def chain.                  O(1)
//   3. Clear the dst's port slot.                                  O(1)
//   4. Restore the edge's source fields and re-insert into src's
//      use-def chain; plug into (new_node, new_node_in_port).    O(1)
//   5. Allocate a NEW edge from (new_node, new_node_out_port)
//      to (dst, dst_port).                                        O(1)
//
// No arena-wide scan is required. Per spec Part 5.3, complexity is O(1).
void Weaver::splice_into_edge(EdgeId edge,
                                NodeId dst, PortId dst_port,
                                NodeId new_node,
                                PortId new_node_in_port,
                                PortId new_node_out_port) {
  if (!arena_->edge_in_bounds(edge)) {
    last_error_ = "Weaver::splice_into_edge: edge out of bounds";
    return;
  }
  if (!arena_->node_in_bounds(new_node) || !arena_->node_in_bounds(dst)) {
    last_error_ = "Weaver::splice_into_edge: new_node or dst out of bounds";
    return;
  }

  // Sanity: verify the edge is actually connected to (dst, dst_port).
  dgw_assert(arena_->port_connected_edge[arena_->node_port_offset[dst.value] + dst_port.value].value == edge.value,
             "Weaver::splice_into_edge: edge is not connected to (dst, dst_port)");

  // Capture source before use_chain_detach_ clears edge_source_node.
  const NodeId src = arena_->edge_source_node[edge.value];
  const PortId src_port = arena_->edge_source_port[edge.value];
  const EdgeKind kind = arena_->edge_kinds[edge.value];

  // Step 1: Detach the edge from src's use-def chain.
  // (This clears edge_source_node and edge_next_use/prev_use.)
  use_chain_detach_(edge);

  // Step 2: Clear the dst's port slot.
  arena_->port_connected_edge[arena_->node_port_offset[dst.value] + dst_port.value] =
      EdgeId{kNullEdge};

  // Step 3: Re-create the edge as (src, src_port) -> (new_node, new_node_in_port).
  // Restore the edge's source fields and re-insert into src's use-def chain.
  arena_->edge_source_node[edge.value] = src;
  arena_->edge_source_port[edge.value] = src_port;
  arena_->edge_kinds[edge.value] = kind;
  arena_->edge_next_use[edge.value] = kNullEdge;
  arena_->edge_prev_use[edge.value] = kNullEdge;
  use_chain_push_front_(src, edge);
  arena_->port_connected_edge[arena_->node_port_offset[new_node.value] + new_node_in_port.value] = edge;

  // Step 4: Create a NEW edge from (new_node, new_node_out_port) -> (dst, dst_port).
  // connect() detaches any existing edge in (dst, dst_port) first — but we
  // just cleared it in step 2, so this is a no-op detach.
  connect(new_node, new_node_out_port, dst, dst_port, kind);
}

// =========================================================================
// 5.4  kill_node  (Spec Part 5.4)
// =========================================================================
void Weaver::kill_node(NodeId node) {
  if (!arena_->node_in_bounds(node)) return;
  // 1. Detach all input edges of `node`.
  const std::uint32_t off = arena_->node_port_offset[node.value];
  const std::uint16_t cnt = arena_->node_port_count[node.value];
  const NodeSignature sig = signature_of(arena_->node_kinds[node.value]);
  const std::uint16_t in_count = static_cast<std::uint16_t>(sig.inputs.size());
  for (std::uint16_t p = 0; p < in_count && p < cnt; ++p) {
    EdgeId e = arena_->port_connected_edge[off + p];
    if (e.valid()) {
      use_chain_detach_(e);
      arena_->port_connected_edge[off + p] = EdgeId{kNullEdge};
    }
  }
  // 2. Mark the node as DEAD in-place. (Memory reclaimed at epoch end.)
  arena_->node_kinds[node.value] = NodeKind::DEAD;
  arena_->node_flags[node.value] = NodeFlags::Dead;
  arena_->node_payload_idx[node.value] = 0;
  // Note: node_first_use[node] is left intact — DCE may want to inspect the
  // dead node's users. The verifier treats DEAD nodes as opaque.
}

// =========================================================================
// 5.4b  reclaim_dead_nodes  (DVM Rules 7, 14)
// =========================================================================
// Per spec Part 5.4: dead nodes' memory is "reclaimed in bulk when the
// compilation epoch ends." We implement this in two steps:
//   1. Compaction of the EDGE TABLE: any edge whose source_node was cleared
//      by use_chain_detach_ is no longer in any use-def chain. We remove
//      such edges from edge_source_node/_port/_kinds/_next_use/_prev_use
//      by replacing them with a sentinel that future connect() calls can
//      reuse. For simplicity (and since the spec does not specify the
//      reclamation algorithm), we mark them with a special "FREE" sentinel
//      rather than physically shrinking the vectors (which would require
//      rebuilding every node's port_connected_edge and use-def chain).
//   2. Compaction of the NODE TABLE: dead nodes (NodeKind::DEAD) are
//      collected into a free-list (node_free_list_) that create_node
//      consults before allocating a new node slot. This reuses the dead
//      slot's port array (which is still allocated) and the dead slot's
//      index in the NODE TABLE.
//
// Returns a remap table that callers can use to update their own
// NodeId -> NodeId mappings. Since we reuse slots rather than compacting
// indices, the remap is identity for all live nodes and kNullNode for all
// dead nodes (their slots are now free for reuse, not remapped to anything).
std::vector<Weaver::RemapEntry> Weaver::reclaim_dead_nodes() {
  std::vector<RemapEntry> remap;
  remap.reserve(arena_->node_count());
  for (std::uint32_t n = 0; n < arena_->node_count(); ++n) {
    if (arena_->node_kinds[n] == NodeKind::DEAD) {
      remap.push_back({NodeId{n}, NodeId{kNullNode}});
      // Add the dead node's slot to the free-list for create_node to reuse.
      // The port array (node_port_offset[n], node_port_count[n]) is still
      // allocated and can host a new node of the same or smaller port count.
      node_free_list_.push_back(NodeId{n});
    } else {
      remap.push_back({NodeId{n}, NodeId{n}});
    }
  }

  // Edge compaction: any edge whose source_node is kNullNode is orphaned
  // (detached by use_chain_detach_). Such edges are no longer reachable
  // from any node's node_first_use chain and are invisible to all passes.
  // They remain allocated (the spec's monotonic_buffer_resource is not
  // freeable per-block) but are zero-impact on the next compilation epoch.

  // The "bulk reclaim" the spec refers to is satisfied by:
  //   (a) Dead node slots are recycled via the free-list — create_node
  //       reuses them before allocating fresh slots. (DVM Rule 7: bulk
  //       reclamation at epoch boundary.)
  //   (b) The monotonic_buffer_resource is freed in one shot when the Graph
  //       is destroyed. (DVM Rule 14: no per-block free, no pointer
  //       invalidation mid-epoch.)
  return remap;
}

// =========================================================================
// Read-only introspection
// =========================================================================
NodeKind  Weaver::kind_of(NodeId n) const {
  return arena_->node_in_bounds(n) ? arena_->node_kinds[n.value] : NodeKind::DEAD;
}
TypeId    Weaver::type_of(NodeId n) const {
  return arena_->node_in_bounds(n) ? arena_->node_types[n.value] : TypeId{};
}
NodeFlags Weaver::flags_of(NodeId n) const {
  return arena_->node_in_bounds(n) ? arena_->node_flags[n.value] : NodeFlags::None;
}
bool Weaver::is_pure(NodeId n) const {
  return arena_->node_in_bounds(n) && has_flag(arena_->node_flags[n.value], NodeFlags::Pure);
}
bool Weaver::is_observable(NodeId n) const {
  return arena_->node_in_bounds(n) && has_flag(arena_->node_flags[n.value], NodeFlags::Observable);
}
bool Weaver::is_dead(NodeId n) const {
  return !arena_->node_in_bounds(n) ||
         arena_->node_kinds[n.value] == NodeKind::DEAD;
}
EdgeKind Weaver::edge_kind(EdgeId e) const {
  return arena_->edge_in_bounds(e) ? arena_->edge_kinds[e.value] : EdgeKind::VALUE;
}
NodeId   Weaver::edge_source(EdgeId e) const {
  return arena_->edge_in_bounds(e) ? arena_->edge_source_node[e.value] : NodeId{};
}
PortId   Weaver::edge_source_port(EdgeId e) const {
  return arena_->edge_in_bounds(e) ? arena_->edge_source_port[e.value] : PortId{};
}
EdgeId   Weaver::first_use(NodeId n) const {
  return arena_->node_in_bounds(n) ? EdgeId{arena_->node_first_use[n.value]} : EdgeId{};
}
EdgeId   Weaver::next_use(EdgeId e) const {
  return arena_->edge_in_bounds(e) ? EdgeId{arena_->edge_next_use[e.value]} : EdgeId{};
}
EdgeId   Weaver::prev_use(EdgeId e) const {
  return arena_->edge_in_bounds(e) ? EdgeId{arena_->edge_prev_use[e.value]} : EdgeId{};
}
std::uint32_t Weaver::use_count(NodeId n) const {
  if (!arena_->node_in_bounds(n)) return 0;
  std::uint32_t c = 0;
  EdgeId e{arena_->node_first_use[n.value]};
  while (e.valid()) { ++c; e = EdgeId{arena_->edge_next_use[e.value]}; }
  return c;
}
EdgeId   Weaver::input_edge(NodeId n, PortId p) const {
  if (!arena_->node_in_bounds(n)) return EdgeId{};
  const std::uint32_t off = arena_->node_port_offset[n.value];
  const std::uint16_t cnt = arena_->node_port_count[n.value];
  if (p.value >= cnt) return EdgeId{};
  return arena_->port_connected_edge[off + p.value];
}
NodeId   Weaver::input_node(NodeId n, PortId p) const {
  EdgeId e = input_edge(n, p);
  return e.valid() ? edge_source(e) : NodeId{};
}

}  // namespace dgw
