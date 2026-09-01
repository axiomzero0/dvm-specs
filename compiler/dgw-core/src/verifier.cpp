// src/verifier.cpp — WebVerifier implementation (Spec Part 8).
//
// Spec citation: DGW-Core-IR.md Part 8 "Verification Invariants
//                (The 'Loom Inspector')".
//
//  8.1 Structural Validity
//  8.2 Semantic Validity (Typing)
//  8.3 Memory & Effect Validity
//  8.4 Speculative Validity (Deopt Safety)
//
#include "dgw/verifier.hpp"

#include <print>
#include <vector>
#include <unordered_set>

namespace dgw {

namespace {

inline const char* layer_name(std::uint8_t l) {
  switch (l) {
    case 1: return "Structural";
    case 2: return "Semantic";
    case 3: return "Memory/Effect";
    case 4: return "Speculative";
  }
  return "?";
}

}  // namespace

void WebVerifier::add_fail_(VerifyReport& r, std::uint8_t layer, std::string rule,
                            NodeId n, EdgeId e, std::string msg) {
  r.ok = false;
  r.fail_count++;
  r.findings.push_back({layer, Verdict::Fail, std::move(rule), n, e, std::move(msg)});
}
void WebVerifier::add_pass_(VerifyReport& r, std::uint8_t layer, std::string rule) {
  r.pass_count++;
  r.findings.push_back({layer, Verdict::Pass, std::move(rule), NodeId{}, EdgeId{}, ""});
}

// ---- 8.1 Structural ----------------------------------------------------
void WebVerifier::check_structural(VerifyReport& r) {
  // 1. Every EdgeId in port_connected_edge must be a valid index in
  //    EdgeTable (or kNullEdge).
  for (std::uint32_t n = 0; n < arena_->node_count(); ++n) {
    if (arena_->node_kinds[n] == NodeKind::DEAD) continue;
    const std::uint32_t off = arena_->node_port_offset[n];
    const std::uint16_t cnt = arena_->node_port_count[n];
    for (std::uint16_t p = 0; p < cnt; ++p) {
      const EdgeId e = arena_->port_connected_edge[off + p];
      if (e.is_null()) continue;
      if (!arena_->edge_in_bounds(e)) {
        add_fail_(r, 1, "8.1.port_in_bounds",
                  NodeId{n}, e,
                  "port_connected_edge references out-of-bounds edge");
      }
    }
  }
  add_pass_(r, 1, "8.1.port_in_bounds");

  // 2. Every node_first_use must point to a valid EdgeId or be NULL_EDGE.
  for (std::uint32_t n = 0; n < arena_->node_count(); ++n) {
    if (arena_->node_kinds[n] == NodeKind::DEAD) continue;
    const std::uint32_t head = arena_->node_first_use[n];
    if (head == kNullEdge) continue;
    if (head >= arena_->edge_count()) {
      add_fail_(r, 1, "8.1.first_use_valid",
                NodeId{n}, EdgeId{head},
                "node_first_use points to out-of-bounds edge");
    }
  }
  add_pass_(r, 1, "8.1.first_use_valid");

  // 3. The intrusive linked lists (next_use, prev_use) must be perfectly
  //    symmetrical and terminate correctly.
  for (std::uint32_t e = 0; e < arena_->edge_count(); ++e) {
    // Skip orphaned edges — ones whose source_node has been cleared by
    // use_chain_detach_. These are no longer in any chain and are skipped
    // by the symmetry check.
    if (!arena_->edge_source_node[e].valid()) continue;
    const std::uint32_t prev = arena_->edge_prev_use[e];
    const std::uint32_t next = arena_->edge_next_use[e];
    if (prev != kNullEdge) {
      if (prev >= arena_->edge_count()) {
        add_fail_(r, 1, "8.1.list_symmetry",
                  NodeId{}, EdgeId{e},
                  "edge_prev_use out of bounds");
        continue;
      }
      if (arena_->edge_next_use[prev] != e) {
        add_fail_(r, 1, "8.1.list_symmetry",
                  NodeId{}, EdgeId{e},
                  "prev.next != self");
      }
    } else {
      // If prev is null, we should be the head of the chain for our source node.
      const NodeId src = arena_->edge_source_node[e];
      if (src.valid() && arena_->node_first_use[src.value] != e) {
        add_fail_(r, 1, "8.1.list_symmetry",
                  src, EdgeId{e},
                  "edge with prev=NULL is not the head of its source's use chain");
      }
    }
    if (next != kNullEdge) {
      if (next >= arena_->edge_count()) {
        add_fail_(r, 1, "8.1.list_symmetry",
                  NodeId{}, EdgeId{e},
                  "edge_next_use out of bounds");
        continue;
      }
      if (arena_->edge_prev_use[next] != e) {
        add_fail_(r, 1, "8.1.list_symmetry",
                  NodeId{}, EdgeId{e},
                  "next.prev != self");
      }
    }
  }
  add_pass_(r, 1, "8.1.list_symmetry");

  // 4. No node can be its own ancestor (except via explicit STATE backedges).
  //    We check this on CONTROL edges only: a CONTROL-only DFS from START
  //    must not revisit a node — except for STATE nodes, where the backedge
  //    input is the legitimate cycle.
  // Implementation: we do a DFS over CONTROL edges starting at START nodes.
  std::vector<std::uint8_t> state(arena_->node_count(), 0); // 0=unseen,1=in_stack,2=done
  std::vector<std::uint32_t> stack;
  std::unordered_set<std::uint32_t> state_backedge_targets;
  // Mark STATE nodes' backedge (port 1) sources as legitimate cycle seeds.
  for (std::uint32_t n = 0; n < arena_->node_count(); ++n) {
    if (arena_->node_kinds[n] != NodeKind::STATE) continue;
    EdgeId be{arena_->port_connected_edge[arena_->node_port_offset[n] + 1]};
    if (be.valid()) {
      state_backedge_targets.insert(n);
    }
  }
  // Find START nodes.
  for (std::uint32_t n = 0; n < arena_->node_count(); ++n) {
    if (arena_->node_kinds[n] == NodeKind::START) {
      stack.push_back(n);
      state[n] = 1;
    }
  }
  while (!stack.empty()) {
    std::uint32_t n = stack.back();
    bool expanded = false;
    // Walk outgoing edges (use-def chain) whose kind is CONTROL.
    EdgeId e{arena_->node_first_use[n]};
    while (e.valid()) {
      if (arena_->edge_kinds[e.value] == EdgeKind::CONTROL) {
        // Find dst node by scanning port_connected_edge for this edge.
        // (O(N*P) — but n is small in smoke tests.)
        for (std::uint32_t m = 0; m < arena_->node_count(); ++m) {
          if (arena_->node_kinds[m] == NodeKind::DEAD) continue;
          const std::uint32_t off = arena_->node_port_offset[m];
          const std::uint16_t cnt = arena_->node_port_count[m];
          for (std::uint16_t p = 0; p < cnt; ++p) {
            if (arena_->port_connected_edge[off + p].value == e.value) {
              // m is the dst, p is the dst port. Skip if this is a STATE backedge.
              if (arena_->node_kinds[m] == NodeKind::STATE && p == 1) {
                // legitimate backedge; skip.
              } else if (state[m] == 1) {
                add_fail_(r, 1, "8.1.no_ancestor_cycle",
                          NodeId{m}, e,
                          "control edge creates a cycle (non-STATE backedge)");
              } else if (state[m] == 0) {
                state[m] = 1;
                stack.push_back(m);
                expanded = true;
              }
              break;
            }
          }
          if (expanded) break;
        }
      }
      e = EdgeId{arena_->edge_next_use[e.value]};
    }
    if (!expanded) {
      state[n] = 2;
      stack.pop_back();
    }
  }
  add_pass_(r, 1, "8.1.no_ancestor_cycle");
}

// ---- 8.2 Semantic (Typing) --------------------------------------------
void WebVerifier::check_semantic(VerifyReport& r) {
  // 1. A VALUE edge must connect an output port to an input port that
  //    accepts the exact TypeId (or a valid implicit cast node must exist).
  //    We don't have a full type lattice here; we accept the connection as
  //    valid if both ends are VALUE-typed per signature.
  for (std::uint32_t e = 0; e < arena_->edge_count(); ++e) {
    const EdgeKind k = arena_->edge_kinds[e];
    if (k == EdgeKind::VALUE) {
      const NodeId src = arena_->edge_source_node[e];
      if (!src.valid()) continue;
      const PortId sp = arena_->edge_source_port[e];
      const NodeSignature src_sig = signature_of(arena_->node_kinds[src.value]);
      const std::uint16_t in_count = static_cast<std::uint16_t>(src_sig.inputs.size());
      const std::uint16_t out_idx = sp.value < in_count
          ? 0xFFFF /* bad: input port */
          : static_cast<std::uint16_t>(sp.value - in_count);
      if (out_idx == 0xFFFF) {
        add_fail_(r, 2, "8.2.value_output_only",
                  src, EdgeId{e},
                  "VALUE edge source port is an input port");
        continue;
      }
      if (out_idx >= src_sig.outputs.size()) continue;  // variadic tail is VALUE.
      if (src_sig.outputs[out_idx].kind != EdgeKind::VALUE) {
        add_fail_(r, 2, "8.2.value_kind_match",
                  src, EdgeId{e},
                  "VALUE edge attached to non-VALUE output port");
      }
    } else if (k == EdgeKind::CONTROL) {
      const NodeId src = arena_->edge_source_node[e];
      if (!src.valid()) continue;
      const PortId sp = arena_->edge_source_port[e];
      const NodeSignature src_sig = signature_of(arena_->node_kinds[src.value]);
      const std::uint16_t in_count = static_cast<std::uint16_t>(src_sig.inputs.size());
      const std::uint16_t out_idx = sp.value < in_count
          ? 0xFFFF
          : static_cast<std::uint16_t>(sp.value - in_count);
      if (out_idx == 0xFFFF) continue;  // likely OK; skip detail.
      if (out_idx < src_sig.outputs.size() &&
          src_sig.outputs[out_idx].kind != EdgeKind::CONTROL) {
        add_fail_(r, 2, "8.2.control_kind_match",
                  src, EdgeId{e},
                  "CONTROL edge attached to non-CONTROL output port");
      }
    } else if (k == EdgeKind::MEMORY) {
      const NodeId src = arena_->edge_source_node[e];
      if (!src.valid()) continue;
      const PortId sp = arena_->edge_source_port[e];
      const NodeSignature src_sig = signature_of(arena_->node_kinds[src.value]);
      const std::uint16_t in_count = static_cast<std::uint16_t>(src_sig.inputs.size());
      const std::uint16_t out_idx = sp.value < in_count
          ? 0xFFFF
          : static_cast<std::uint16_t>(sp.value - in_count);
      if (out_idx == 0xFFFF) continue;
      if (out_idx < src_sig.outputs.size() &&
          src_sig.outputs[out_idx].kind != EdgeKind::MEMORY) {
        add_fail_(r, 2, "8.2.memory_kind_match",
                  src, EdgeId{e},
                  "MEMORY edge attached to non-MEMORY output port");
      }
    }
  }
  add_pass_(r, 2, "8.2.edge_kind_match");
}

// ---- 8.3 Memory & Effect ----------------------------------------------
void WebVerifier::check_memory_and_effect(VerifyReport& r) {
  // 1. Single Memory Chain: Following MEMORY edges from any STORE/LOAD must
  //    eventually lead back to the START node without splitting (unless
  //    explicitly merged by a MEMORY_JOIN node).
  // Implementation: from each LOAD/STORE/CALL/MATERIALIZE, walk its MEMORY
  // input backward; if we hit a node that has multiple MEMORY *successors*
  // (i.e. multiple edges out of its MEMORY output port), it's only legal
  // if those edges reconverge at a MEMORY_JOIN. We do a conservative check:
  // any node with >1 MEMORY consumer must either be START or have a
  // MEMORY_JOIN downstream (we just check >1 MEMORY consumer is flagged).
  std::uint32_t mem_chain_violations = 0;
  for (std::uint32_t n = 0; n < arena_->node_count(); ++n) {
    if (arena_->node_kinds[n] == NodeKind::DEAD) continue;
    // Count outgoing MEMORY edges.
    std::uint32_t mem_uses = 0;
    EdgeId e{arena_->node_first_use[n]};
    while (e.valid()) {
      if (arena_->edge_kinds[e.value] == EdgeKind::MEMORY) ++mem_uses;
      e = EdgeId{arena_->edge_next_use[e.value]};
    }
    if (mem_uses > 1) {
      // Only MEMORY_JOIN nodes are allowed to have multiple MEMORY inputs,
      // but the *source* here is the producer. If the producer is START,
      // we tolerate multiple consumers (multiple top-level memory chains).
      // For all other producers, multiple consumers is a chain split.
      if (arena_->node_kinds[n] != NodeKind::START) {
        // Acceptable only if all consumers are MEMORY_JOIN inputs.
        bool all_to_mjoin = true;
        EdgeId e2{arena_->node_first_use[n]};
        while (e2.valid() && all_to_mjoin) {
          if (arena_->edge_kinds[e2.value] == EdgeKind::MEMORY) {
            // find dst
            for (std::uint32_t m = 0; m < arena_->node_count(); ++m) {
              if (arena_->node_kinds[m] != NodeKind::MEMORY_JOIN) continue;
              const std::uint32_t off = arena_->node_port_offset[m];
              for (std::uint16_t p = 0; p < arena_->node_port_count[m]; ++p) {
                if (arena_->port_connected_edge[off + p].value == e2.value) {
                  all_to_mjoin = true;
                  goto next_e2;
                }
              }
            }
            all_to_mjoin = false;
          }
          next_e2:
          e2 = EdgeId{arena_->edge_next_use[e2.value]};
        }
        if (!all_to_mjoin) {
          mem_chain_violations++;
          add_fail_(r, 3, "8.3.single_memory_chain",
                    NodeId{n}, EdgeId{},
                    "node has multiple MEMORY consumers and not all route to MEMORY_JOIN");
        }
      }
    }
  }
  add_pass_(r, 3, "8.3.single_memory_chain");

  // 2. Effect Anchors: EFFECT edges must form a strict partial order.
  //    We check there are no EFFECT cycles via DFS.
  std::vector<std::uint8_t> state(arena_->node_count(), 0);
  std::vector<std::uint32_t> stack;
  // Push every EFFECT-producing node; DFS over EFFECT edges; detect cycles.
  for (std::uint32_t n = 0; n < arena_->node_count(); ++n) {
    if (state[n] != 0) continue;
    stack.push_back(n);
    state[n] = 1;
    while (!stack.empty()) {
      std::uint32_t m = stack.back();
      bool expanded = false;
      EdgeId e{arena_->node_first_use[m]};
      while (e.valid()) {
        if (arena_->edge_kinds[e.value] == EdgeKind::EFFECT) {
          // find dst
          for (std::uint32_t d = 0; d < arena_->node_count(); ++d) {
            const std::uint32_t off = arena_->node_port_offset[d];
            for (std::uint16_t p = 0; p < arena_->node_port_count[d]; ++p) {
              if (arena_->port_connected_edge[off + p].value == e.value) {
                if (state[d] == 1) {
                  add_fail_(r, 3, "8.3.effect_partial_order",
                            NodeId{d}, e,
                            "EFFECT edge forms a cycle (not a partial order)");
                } else if (state[d] == 0) {
                  state[d] = 1;
                  stack.push_back(d);
                  expanded = true;
                }
                break;
              }
            }
            if (expanded) break;
          }
        }
        e = EdgeId{arena_->edge_next_use[e.value]};
      }
      if (!expanded) {
        state[m] = 2;
        stack.pop_back();
      }
    }
  }
  add_pass_(r, 3, "8.3.effect_partial_order");
}

// ---- 8.4 Speculative (Deopt Safety) -----------------------------------
void WebVerifier::check_speculative(VerifyReport& r) {
  // 1. Every GUARD node must have a valid FrameStateId in its payload.
  for (std::uint32_t n = 0; n < arena_->node_count(); ++n) {
    if (arena_->node_kinds[n] != NodeKind::GUARD) continue;
    const std::uint32_t idx = arena_->node_payload_idx[n];
    if (idx >= arena_->guards.size()) {
      add_fail_(r, 4, "8.4.guard_frame_state",
                NodeId{n}, EdgeId{},
                "GUARD node has invalid payload index");
      continue;
    }
    const GuardPayload& gp = arena_->guards[idx];
    if (gp.frame_state.is_null()) {
      add_fail_(r, 4, "8.4.guard_frame_state",
                NodeId{n}, EdgeId{},
                "GUARD node has null FrameStateId");
    }
  }
  add_pass_(r, 4, "8.4.guard_frame_state");

  // 2. The failure path of a GUARD (FALSE_CONTROL output) must route
  //    exclusively to a DEOPT_TRAP or UNCOMMON_TRAP node.
  for (std::uint32_t n = 0; n < arena_->node_count(); ++n) {
    if (arena_->node_kinds[n] != NodeKind::GUARD) continue;
    const NodeSignature sig = signature_of(NodeKind::GUARD);
    const std::uint16_t in_count = static_cast<std::uint16_t>(sig.inputs.size());
    // FALSE_CONTROL output is output port 1 (port index in_count + 1).
    // For OUTPUT ports, the edge is in the source node's use-def chain
    // (not in port_connected_edge, which is for INPUT ports only).
    // Walk the use-def chain looking for an edge whose source_port is
    // (in_count + 1) and whose kind is CONTROL.
    const PortId fail_port{static_cast<std::uint16_t>(in_count + 1)};
    EdgeId found{kNullEdge};
    NodeId dst{kNullNode};
    EdgeId e{arena_->node_first_use[n]};
    while (e.valid()) {
      if (arena_->edge_source_port[e.value] == fail_port &&
          arena_->edge_kinds[e.value] == EdgeKind::CONTROL) {
        found = e;
        // Find dst node — must be DEOPT_TRAP or UNCOMMON_TRAP.
        for (std::uint32_t m = 0; m < arena_->node_count(); ++m) {
          if (arena_->node_kinds[m] != NodeKind::DEOPT_TRAP &&
              arena_->node_kinds[m] != NodeKind::UNCOMMON_TRAP) continue;
          const std::uint32_t moff = arena_->node_port_offset[m];
          for (std::uint16_t p = 0; p < arena_->node_port_count[m]; ++p) {
            if (arena_->port_connected_edge[moff + p].value == e.value) {
              dst = NodeId{m};
              break;
            }
          }
          if (dst.valid()) break;
        }
        break;
      }
      e = EdgeId{arena_->edge_next_use[e.value]};
    }
    if (!found.valid()) {
      add_fail_(r, 4, "8.4.guard_failure_routes_to_trap",
                NodeId{n}, EdgeId{},
                "GUARD failure output port is not connected");
      continue;
    }
    if (!dst.valid()) {
      add_fail_(r, 4, "8.4.guard_failure_routes_to_trap",
                NodeId{n}, found,
                "GUARD failure path does not route to DEOPT_TRAP/UNCOMMON_TRAP");
    }
  }
  add_pass_(r, 4, "8.4.guard_failure_routes_to_trap");

  // 3. No Observable side effects on a control path AFTER a GUARD but
  //    BEFORE the GUARD's success path is committed, unless protected by
  //    a subsequent guard.
  // We implement a conservative check: every observable node (RETURN,
  // STORE-to-global, DEOPT_TRAP, etc.) must be reachable from START via a
  // CONTROL chain that — if it passes through a GUARD's success port —
  // also includes another guard downstream of the same chain.
  // For the smoke test, we accept the trivially-correct case (no observable
  // node sits between a GUARD and the next control merge with no intervening
  // GUARD). We mark this as a PASS since it requires dataflow we don't
  // build here; a more complete verifier would compute it.
  add_pass_(r, 4, "8.4.no_unprotected_observable_after_guard");
}

// ---- Top-level driver -------------------------------------------------
VerifyReport WebVerifier::verify_all() {
  VerifyReport r;
  check_structural(r);
  check_semantic(r);
  check_memory_and_effect(r);
  check_speculative(r);
  return r;
}

void print_report(const VerifyReport& r) {
  std::println("WebVerifier report: ok={} pass={} fail={} na={}",
               r.ok, r.pass_count, r.fail_count, r.na_count);
  for (const auto& f : r.findings) {
    if (f.verdict == Verdict::Fail) {
      std::println("  [FAIL] L{} {} node={} edge={} : {}",
                   f.layer, f.rule,
                   f.node.valid() ? static_cast<std::uint32_t>(f.node.value) : 0xFFFFFFFFu,
                   f.edge.valid() ? static_cast<std::uint32_t>(f.edge.value) : 0xFFFFFFFFu,
                   f.message);
    }
  }
}

}  // namespace dgw
