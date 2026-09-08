// src/pass_constfold.cpp — Constant Folding implementation.
//
// Spec citation: DVM-Performance-Priorities.md §14, §10.
//
// Walks the graph's node table. For each arithmetic/comparison node whose
// both VALUE inputs are CONST nodes, folds the operation into a new CONST
// node and replaces the arithmetic node with a FWD to the new CONST.
// Also applies algebraic identities (0+x→x, 1*x→x, 0*x→0).
//
#include "dgw/pass_constfold.hpp"

#include <cstdint>
#include <variant>

#include "dgw/signatures.hpp"

namespace dgw {

namespace {

// Read a CONST node's int64 payload. Returns true and sets `out` on success.
bool read_const_i64(const GraphArena& arena, NodeId n, std::int64_t& out) noexcept {
  if (!arena.node_in_bounds(n)) return false;
  if (arena.node_kinds[n.value] != NodeKind::CONST) return false;
  const std::uint32_t pidx = arena.node_payload_idx[n.value];
  if (pidx >= arena.consts.size()) return false;
  const auto& cp = arena.consts[pidx];
  if (std::holds_alternative<std::int64_t>(cp.value)) {
    out = std::get<std::int64_t>(cp.value);
    return true;
  }
  return false;
}

}  // namespace

ConstFoldStats pass_constfold(Weaver& w) {
  ConstFoldStats stats;
  auto& arena = w.arena();

  // Safety check: if the graph has a STATE node (loop header), skip
  // constant folding. In loop traces, the lifter represents loop-variant
  // registers as CONST nodes from the entry snapshot, but these values
  // change each iteration. Folding them would incorrectly eliminate
  // the loop's increment/compare, turning the loop into dead code.
  // Non-loop traces (side exits) are safe because all values are truly
  // constant throughout the trace's single execution.
  bool has_state = false;
  for (std::uint32_t i = 0; i < arena.node_count(); ++i) {
    if (arena.node_kinds[i] == NodeKind::STATE) { has_state = true; break; }
  }
  if (has_state) return stats;  // skip folding for loop traces

  for (std::uint32_t n = 0; n < arena.node_count(); ++n) {
    if (arena.node_kinds[n] == NodeKind::DEAD) continue;
    if (arena.node_kinds[n] == NodeKind::FWD) continue;

    NodeKind kind = arena.node_kinds[n];
    stats.visited++;

    // Only fold arithmetic and comparison nodes (pure, R_R_R format).
    bool is_arith = (kind == NodeKind::ADD || kind == NodeKind::SUB ||
                     kind == NodeKind::MUL || kind == NodeKind::DIV ||
                     kind == NodeKind::AND || kind == NodeKind::OR ||
                     kind == NodeKind::XOR || kind == NodeKind::SHL ||
                     kind == NodeKind::SHR || kind == NodeKind::SHR);  // SHR_S or SHR_U
    bool is_cmp = (kind == NodeKind::CMP_EQ || kind == NodeKind::CMP_NE ||
                   kind == NodeKind::CMP_LT || kind == NodeKind::CMP_LE ||
                   kind == NodeKind::CMP_GT || kind == NodeKind::CMP_GE);
    if (!is_arith && !is_cmp) continue;

    // Read the two VALUE inputs.
    NodeId src0 = w.input_node(NodeId{n}, PortId{0});
    NodeId src1 = w.input_node(NodeId{n}, PortId{1});
    if (!src0.valid() || !src1.valid()) continue;

    std::int64_t a, b;
    if (!read_const_i64(arena, src0, a)) {
      // Try algebraic identities even when only one operand is CONST.
      if (read_const_i64(arena, src1, b)) {
        // src1 is CONST(b), src0 is non-CONST.
        if (kind == NodeKind::ADD && b == 0) {
          // x + 0 → x
          w.forward_node(NodeId{n}, src0);
          stats.simplified++;
          continue;
        }
        if (kind == NodeKind::MUL && b == 1) {
          // x * 1 → x
          w.forward_node(NodeId{n}, src0);
          stats.simplified++;
          continue;
        }
        if (kind == NodeKind::MUL && b == 0) {
          // x * 0 → 0
          NodeId zero = w.create_const(static_cast<std::int64_t>(0));
          w.forward_node(NodeId{n}, zero);
          stats.simplified++;
          continue;
        }
        if (kind == NodeKind::AND && b == 0) {
          // x & 0 → 0
          NodeId zero = w.create_const(static_cast<std::int64_t>(0));
          w.forward_node(NodeId{n}, zero);
          stats.simplified++;
          continue;
        }
        if (kind == NodeKind::OR && b == 0) {
          // x | 0 → x
          w.forward_node(NodeId{n}, src0);
          stats.simplified++;
          continue;
        }
        if (kind == NodeKind::XOR && b == 0) {
          // x ^ 0 → x
          w.forward_node(NodeId{n}, src0);
          stats.simplified++;
          continue;
        }
        if (kind == NodeKind::SUB) {
          // x - 0 → x
          if (b == 0) {
            w.forward_node(NodeId{n}, src0);
            stats.simplified++;
            continue;
          }
        }
      }
      continue;  // Can't fold without both operands constant.
    }
    if (!read_const_i64(arena, src1, b)) {
      // src0 is CONST(a), src1 is non-CONST. Try identities.
      if (kind == NodeKind::ADD && a == 0) {
        // 0 + x → x
        w.forward_node(NodeId{n}, src1);
        stats.simplified++;
        continue;
      }
      if (kind == NodeKind::MUL && a == 1) {
        // 1 * x → x
        w.forward_node(NodeId{n}, src1);
        stats.simplified++;
        continue;
      }
      if (kind == NodeKind::MUL && a == 0) {
        // 0 * x → 0
        NodeId zero = w.create_const(static_cast<std::int64_t>(0));
        w.forward_node(NodeId{n}, zero);
        stats.simplified++;
        continue;
      }
      if (kind == NodeKind::AND && a == 0) {
        NodeId zero = w.create_const(static_cast<std::int64_t>(0));
        w.forward_node(NodeId{n}, zero);
        stats.simplified++;
        continue;
      }
      if (kind == NodeKind::OR && a == 0) {
        w.forward_node(NodeId{n}, src1);
        stats.simplified++;
        continue;
      }
      if (kind == NodeKind::XOR && a == 0) {
        w.forward_node(NodeId{n}, src1);
        stats.simplified++;
        continue;
      }
      // SUB: 0 - x → -x (NEG), but we don't have a NEG node in the
      // arithmetic set; skip for now.
      continue;
    }

    // Both operands are CONST. Fold the operation.
    std::int64_t result = 0;
    bool ok = true;

    switch (kind) {
      case NodeKind::ADD:  result = a + b; break;
      case NodeKind::SUB:  result = a - b; break;
      case NodeKind::MUL:  result = a * b; break;
      case NodeKind::DIV:
        if (b == 0) { ok = false; break; }
        result = a / b; break;
      case NodeKind::AND:  result = a & b; break;
      case NodeKind::OR:   result = a | b; break;
      case NodeKind::XOR:  result = a ^ b; break;
      case NodeKind::SHL:  result = a << (b & 63); break;
      case NodeKind::SHR:  result = a >> (b & 63); break;  // arithmetic shift
      case NodeKind::CMP_EQ: result = (a == b) ? 1 : 0; break;
      case NodeKind::CMP_NE: result = (a != b) ? 1 : 0; break;
      case NodeKind::CMP_LT: result = (a <  b) ? 1 : 0; break;
      case NodeKind::CMP_LE: result = (a <= b) ? 1 : 0; break;
      case NodeKind::CMP_GT: result = (a >  b) ? 1 : 0; break;
      case NodeKind::CMP_GE: result = (a >= b) ? 1 : 0; break;
      default: ok = false; break;
    }

    if (!ok) continue;

    // Create a new CONST node with the folded result and FWD the old node.
    NodeId folded = w.create_const(result);
    w.forward_node(NodeId{n}, folded);
    stats.folded++;
  }

  return stats;
}

}  // namespace dgw
