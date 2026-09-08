// dvm/hotness.hpp — Hot-loop detection via backedge counting.
//
// Spec citation: DVM-Hybrid-Tracing-Architecture.md §12.1:
//   "A root trace usually starts at a loop header.
//    Conditions:
//    - backedge count exceeds threshold"
//
// The HotnessTracker counts backward-branch targets. When a backedge
// counter exceeds the threshold, the tracker signals that recording
// should start at that PC. This is the Tier 0 → Tier 1 trigger.
//
#pragma once

#include <cstdint>
#include <unordered_map>

#include "dvm/crb.hpp"
#include "dvm/trace.hpp"

namespace dvm {

// ---- Hotness tracker ----------------------------------------------------
class HotnessTracker {
 public:
  static constexpr std::uint32_t kDefaultThreshold = 3;

  explicit HotnessTracker(std::uint32_t threshold = kDefaultThreshold)
    : threshold_(threshold) {}

  // Called by the interpreter on every backward branch (delta < 0).
  // `target_pc` is the branch destination (the loop header).
  // Returns true if the hot threshold was reached for the first time
  // (i.e., the interpreter should start recording a trace at target_pc).
  bool on_backedge(std::uint32_t target_pc) {
    auto& count = counters_[target_pc];
    ++count;
    if (count == threshold_) {
      // Hot for the first time. Return true so the caller can start
      // recording. Subsequent backedges will increment but return false.
      return true;
    }
    return false;
  }

  // Query the counter for a given PC (0 if not seen).
  std::uint32_t count(std::uint32_t pc) const {
    auto it = counters_.find(pc);
    return it != counters_.end() ? it->second : 0;
  }

  // Reset all counters (e.g., after a deoptimization).
  void reset() { counters_.clear(); }

  std::uint32_t threshold() const noexcept { return threshold_; }

 private:
  std::uint32_t threshold_;
  std::unordered_map<std::uint32_t, std::uint32_t> counters_;
};

}  // namespace dvm
