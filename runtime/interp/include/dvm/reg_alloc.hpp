// dvm/reg_alloc.hpp — Linear-scan register allocator for native codegen.
//
// Spec citation: DVM-Performance-Priorities.md §22 (Register Allocation
// Deserves Obscene Amounts of Attention).
//
// "For traces: linear scan first, because traces are naturally linear."
//
// The allocator pre-computes live intervals for each DGW SSA value by
// scanning the graph's use-def chains. It then assigns physical x86-64
// registers using a greedy linear scan: when a value is defined, pick a
// free register; when its last use has passed, free the register. If all
// registers are exhausted, the value with the furthest next use is spilled
// to the stack.
//
// The first 4 entry-register values map to the System V AMD64 argument
// registers (rdi, rsi, rdx, rcx) since the caller passes them there.
//
#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "dgw/arena.hpp"
#include "dgw/weaver.hpp"
#include "dgw/kinds.hpp"
#include "dgw/signatures.hpp"
#include "dgw/ids.hpp"
#include "dvm/x64_encoder.hpp"

namespace dvm {

using dgw::NodeId;
using dgw::NodeKind;
using dgw::Weaver;
using dgw::GraphArena;
using dgw::NodeSignature;
using dgw::EdgeId;
using dgw::PortId;

// ---- Live interval for a single SSA value --------------------------------
struct LiveInterval {
  std::uint32_t node_id;       // DGW node index
  std::uint32_t def_point;     // node index where defined
  std::uint32_t last_use;     // highest node index that reads this value
  bool is_entry_arg;          // true if this is an entry-register CONST
  Reg assigned_reg;            // allocated physical register (or RAX if spilled)
  bool spilled;                // true if spilled to stack
  std::int32_t spill_offset;  // stack offset if spilled (relative to rbp)
};

// ---- Linear-scan register allocator ---------------------------------------
class LinearScanAllocator {
 public:
  // Available registers (caller-saved + rax, excluding rsp/rbp).
  // The first 4 are also the System V argument registers.
  static constexpr Reg kRegisters[] = {
    Reg::RDI, Reg::RSI, Reg::RDX, Reg::RCX,  // args (entry regs)
    Reg::R8,  Reg::R9,  Reg::R10, Reg::R11,   // caller-saved temps
    Reg::RAX,                                   // return value / temp
  };
  static constexpr int kNumRegisters = 9;

  // Build the allocator from a DGW graph.
  explicit LinearScanAllocator(const GraphArena& arena, const Weaver& w)
    : arena_(arena), w_(w) {
    compute_live_intervals();
    allocate();
  }

  // Get the physical register for a DGW node. Returns RAX if spilled.
  Reg get_reg(std::uint32_t node_id) const {
    auto it = intervals_.find(node_id);
    return it != intervals_.end() ? it->second.assigned_reg : Reg::RAX;
  }

  // Is this node spilled to the stack?
  bool is_spilled(std::uint32_t node_id) const {
    auto it = intervals_.find(node_id);
    return it != intervals_.end() && it->second.spilled;
  }

  // Get the stack offset for a spilled value (relative to rbp).
  std::int32_t get_spill_offset(std::uint32_t node_id) const {
    auto it = intervals_.find(node_id);
    return it != intervals_.end() ? it->second.spill_offset : 0;
  }

  // Get the total stack frame size needed for spills.
  std::int32_t frame_size() const noexcept { return frame_size_; }

  // Get all intervals (for debugging).
  const std::vector<LiveInterval>& intervals() const noexcept {
    return interval_list_;
  }

 private:
  const GraphArena& arena_;
  const Weaver& w_;
  std::unordered_map<std::uint32_t, LiveInterval> intervals_;
  std::vector<LiveInterval> interval_list_;
  std::int32_t frame_size_{0};

  // ---- Step 1: Compute live intervals --------------------------------
  void compute_live_intervals() {
    // For each non-DEAD/non-FWD node, record its definition point.
    // For each VALUE edge, update the source node's last_use.
    for (std::uint32_t n = 0; n < arena_.node_count(); ++n) {
      NodeKind kind = arena_.node_kinds[n];
      if (kind == NodeKind::DEAD || kind == NodeKind::FWD) continue;

      // Create or update the interval for this node.
      if (intervals_.find(n) == intervals_.end()) {
        LiveInterval li;
        li.node_id = n;
        li.def_point = n;
        li.last_use = n;  // default: used immediately (or not at all)
        li.is_entry_arg = false;
        li.assigned_reg = Reg::RAX;
        li.spilled = false;
        li.spill_offset = 0;
        intervals_[n] = li;
      }

      // Walk this node's VALUE inputs and update their last_use.
      const NodeSignature sig = signature_of(kind);
      const std::uint32_t off = arena_.node_port_offset[n];
      const std::uint16_t cnt = arena_.node_port_count[n];
      const std::uint16_t in_count = static_cast<std::uint16_t>(sig.inputs.size());
      for (std::uint16_t p = 0; p < in_count && p < cnt; ++p) {
        if (sig.inputs[p].kind != dgw::EdgeKind::VALUE) continue;
        EdgeId e{arena_.port_connected_edge[off + p]};
        if (!e.valid()) continue;
        std::uint32_t src = arena_.edge_source_node[e.value].value;
        // Update src's last_use to max(current, n).
        auto it = intervals_.find(src);
        if (it != intervals_.end()) {
          if (n > it->second.last_use) it->second.last_use = n;
        } else {
          // Source node might be DEAD/FWD — skip.
        }
      }
    }

    // Mark entry-argument CONST nodes (first 4 CONSTs after START).
    int entry_count = 0;
    for (std::uint32_t n = 0; n < arena_.node_count(); ++n) {
      if (arena_.node_kinds[n] == NodeKind::CONST) {
        if (entry_count < 4) {
          intervals_[n].is_entry_arg = true;
          entry_count++;
        } else {
          break;
        }
      }
    }
  }

  // ---- Step 2: Linear scan allocation --------------------------------
  void allocate() {
    // Sort intervals by def_point (they're already roughly in node order,
    // but let's be explicit).
    interval_list_.reserve(intervals_.size());
    for (auto& [id, li] : intervals_) {
      interval_list_.push_back(li);
    }
    std::sort(interval_list_.begin(), interval_list_.end(),
              [](const LiveInterval& a, const LiveInterval& b) {
                return a.def_point < b.def_point;
              });

    // Active list: intervals currently holding a register.
    // Each entry is an index into interval_list_.
    std::vector<std::size_t> active;
    // Free register pool.
    std::vector<Reg> free_regs(kRegisters, kRegisters + kNumRegisters);
    // Spill counter for stack offsets.
    std::int32_t next_spill_offset = -8;  // below rbp

    for (std::size_t i = 0; i < interval_list_.size(); ++i) {
      LiveInterval& li = interval_list_[i];

      // Expire old intervals: free registers whose last_use < def_point.
      for (auto it = active.begin(); it != active.end();) {
        if (interval_list_[*it].last_use < li.def_point) {
          // Free this register.
          Reg freed = interval_list_[*it].assigned_reg;
          free_regs.push_back(freed);
          it = active.erase(it);
        } else {
          ++it;
        }
      }

      // Assign a register.
      if (li.is_entry_arg && free_regs.size() >= 4) {
        // Entry-arg CONSTs: assign the corresponding argument register.
        // The first entry arg → RDI, second → RSI, etc.
        int arg_idx = 0;
        for (std::size_t j = 0; j < i; ++j) {
          if (interval_list_[j].is_entry_arg) arg_idx++;
        }
        if (arg_idx < 4) {
          Reg want = kRegisters[arg_idx];
          // Remove from free pool.
          auto fit = std::find(free_regs.begin(), free_regs.end(), want);
          if (fit != free_regs.end()) free_regs.erase(fit);
          li.assigned_reg = want;
        } else {
          // Too many entry args — fall through to normal allocation.
          if (!free_regs.empty()) {
            li.assigned_reg = free_regs.back();
            free_regs.pop_back();
          } else {
            spill(li, next_spill_offset);
          }
        }
      } else if (!free_regs.empty()) {
        li.assigned_reg = free_regs.back();
        free_regs.pop_back();
      } else {
        // No free registers — spill the interval with the furthest last_use.
        // Find the active interval with the furthest last_use.
        std::size_t spill_idx = 0;
        std::uint32_t furthest = 0;
        for (std::size_t j = 0; j < active.size(); ++j) {
          if (interval_list_[active[j]].last_use > furthest) {
            furthest = interval_list_[active[j]].last_use;
            spill_idx = j;
          }
        }
        // Spill the furthest one if it's further than the current.
        if (!active.empty() && furthest > li.last_use) {
          LiveInterval& spill_li = interval_list_[active[spill_idx]];
          spill(spill_li, next_spill_offset);
          li.assigned_reg = spill_li.assigned_reg;
          spill_li.assigned_reg = Reg::RAX;
          active.erase(active.begin() + static_cast<std::ptrdiff_t>(spill_idx));
        } else {
          // Spill the current interval.
          spill(li, next_spill_offset);
        }
      }

      if (!li.spilled) {
        active.push_back(i);
      }
    }

    // Write back the allocated registers to the map.
    for (const auto& li : interval_list_) {
      intervals_[li.node_id] = li;
    }

    frame_size_ = (-next_spill_offset - 8) + 8;  // total spill area
  }

  void spill(LiveInterval& li, std::int32_t& next_offset) {
    li.spilled = true;
    li.spill_offset = next_offset;
    next_offset -= 8;  // each spill slot is 8 bytes
  }
};

}  // namespace dvm
