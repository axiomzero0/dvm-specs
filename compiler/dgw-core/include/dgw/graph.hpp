// dgw/graph.hpp — Top-level Graph facade.
//
// Owns the memory_resource, the GraphArena, the Weaver, and the epoch
// counter. Passes and the verifier take `Graph&` and reach into
// `graph.arena`, `graph.weaver`, `graph.verifier` as needed.
//
// This is the only object that needs to be passed around; everything
// else is non-owning.
//
#pragma once

#include <memory_resource>
#include <vector>

#include "dgw/ids.hpp"
#include "dgw/arena.hpp"
#include "dgw/weaver.hpp"
#include "dgw/verifier.hpp"
#include "dgw/pass_gvn.hpp"
#include "dgw/pass_dce.hpp"
#include "dgw/pass_licm.hpp"
#include "dgw/pass_cleanup.hpp"
#include "dgw/scheduler.hpp"
#include "dgw/util.hpp"

namespace dgw {

class Graph {
 public:
  // Allocate a graph backed by a monotonic_buffer_resource sized for
  // `reserved_nodes` nodes and `reserved_edges` edges. The resource grows
  // if needed (the spec says nothing forbids this).
  explicit Graph(std::size_t reserved_nodes = 4096,
                 std::size_t reserved_edges = 16384)
    : buffer_(reserved_nodes * 64 + reserved_edges * 32 + (1u << 20)),
      resource_(buffer_.data(), buffer_.size(), std::pmr::null_memory_resource()),
      arena_(&resource_),
      weaver_(&arena_),
      verifier_(&arena_, &weaver_),
      epoch_(kInitialEpoch) {}

  // ---- Accessors the spec needs ----
  GraphArena&   arena()    noexcept { return arena_; }
  Weaver&       weaver()   noexcept { return weaver_; }
  WebVerifier& verifier() noexcept { return verifier_; }
  Epoch         epoch()    const noexcept { return epoch_; }

  // ---- Epoch management ----
  // Begin a new compilation epoch. Per DVM Rules 7 and 14, dead nodes from
  // prior epochs may be reclaimed at this boundary. The Weaver's
  // reclaim_dead_nodes() does the actual compaction.
  void begin_epoch() noexcept { ++epoch_; }

  // Convenience: run the verifier and return the report.
  VerifyReport verify() { return verifier_.verify_all(); }

  // Convenience: run GVN, DCE, Cleanup with verification between passes
  // (DGW_ALWAYS_VERIFY gates the actual verifier call).
  struct OptStats {
    GvnStats gvn;
    DceStats dce;
    CleanupStats cleanup;
    VerifyReport post_verify;
  };
  OptStats optimize_default();

 private:
  // Backing buffer + monotonic resource. Order matters: buffer_ first, then
  // resource_, then arena_ (which references resource_).
  std::vector<std::byte>        buffer_;
  std::pmr::monotonic_buffer_resource resource_;
  GraphArena                    arena_;
  Weaver                        weaver_;
  mutable WebVerifier           verifier_;
  Epoch                         epoch_;
};

}  // namespace dgw
