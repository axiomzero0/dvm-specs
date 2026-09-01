// dgw/util.hpp — Small runtime helpers for DGW-Core.
//
// Spec citation: DGW-Core-IR.md Part 1 (General), and DVM-Compiler-Laws.md
// (Rules 7, 14, 15 — epoch reclamation and index stability).
//
// This header provides:
//   * `dgw::panic` — fatal abort with source_location, used for invariant
//     violations the verifier should have caught.
//   * `dgw::dgw_assert` — debug-only assertion (compiled out under NDEBUG
//     unless DGW_ALWAYS_VERIFY is defined).
//   * `dgw::Epoch` — monotonic counter used by the Weaver (Part 5.4) and
//     by the epoch reclamation logic (DVM Rules 7, 14).
//   * `dgw::kNull*` sentinel constants (kNullNode, kNullEdge, ...).
//
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <string_view>

namespace dgw {

// ---- Sentinels -------------------------------------------------------------
// All DGW-Core IDs are dense 32-bit integers; we use 0xFFFFFFFF as the
// universal "null" sentinel. This is safe because the arena grows by index
// and we never allocate index 0xFFFFFFFF in practice (the arena would have
// to hold 4 billion entries, at which point we have other problems).

inline constexpr std::uint32_t kNull32 = 0xFFFFFFFFu;

inline constexpr std::uint32_t kNullNode   = kNull32;
inline constexpr std::uint32_t kNullEdge   = kNull32;
inline constexpr std::uint32_t kNullRegion = kNull32;
inline constexpr std::uint32_t kNullSymbol = kNull32;
inline constexpr std::uint32_t kNullType   = kNull32;
inline constexpr std::uint16_t kNullPort   = 0xFFFFu;

// ---- Panic -----------------------------------------------------------------

[[noreturn]] inline void panic(
    std::string_view msg,
    std::source_location loc = std::source_location::current()) {
  std::fprintf(stderr, "DGW panic at %s:%u in %s: %.*s\n",
               loc.file_name(), loc.line(), loc.function_name(),
               static_cast<int>(msg.size()), msg.data());
  std::fflush(stderr);
  std::abort();
}

// ---- Debug assertions ------------------------------------------------------
// The verifier (Part 8) runs after every pass in debug builds. dgw_assert is
// the lightweight per-operation precondition check that runs in every build
// when DGW_ALWAYS_VERIFY is defined, and is compiled out under NDEBUG
// otherwise. Invariants the verifier is responsible for MUST NOT be
// expressed as dgw_assert — they belong in the verifier.

#ifdef NDEBUG
#  ifndef DGW_ALWAYS_VERIFY
#    define DGW_ALWAYS_VERIFY 0
#  endif
#else
#  ifndef DGW_ALWAYS_VERIFY
#    define DGW_ALWAYS_VERIFY 1
#  endif
#endif

inline void dgw_assert(bool cond, std::string_view msg,
                      std::source_location loc = std::source_location::current()) {
#if DGW_ALWAYS_VERIFY
  if (!cond) panic(msg, loc);
#else
  (void)cond; (void)msg; (void)loc;
#endif
}

// ---- Epoch -----------------------------------------------------------------
// The Weaver (Part 5.4) and the epoch-reclamation logic (DVM Rule 7, 14)
// need a monotonic counter that ticks once per "compilation epoch". A pass
// that begins a new epoch (e.g. a full graph rewrite) bumps this. Dead nodes
// from older epochs are reclaimed in bulk when the epoch they died in is no
// longer reachable from any live pass.

using Epoch = std::uint64_t;

inline constexpr Epoch kInitialEpoch = 1;

}  // namespace dgw
