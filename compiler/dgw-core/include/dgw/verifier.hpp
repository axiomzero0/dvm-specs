// dgw/verifier.hpp — WebVerifier (Spec Part 8).
//
// Spec citation: DGW-Core-IR.md Part 8 "Verification Invariants
//                (The 'Loom Inspector')".
//
// "To satisfy DVM Rule 40, the WebVerifier runs after every pass in debug
//  builds. It checks four layers of validity."
//
//  8.1 Structural Validity
//  8.2 Semantic Validity (Typing)
//  8.3 Memory & Effect Validity
//  8.4 Speculative Validity (Deopt Safety)
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <source_location>

#include "dgw/ids.hpp"
#include "dgw/kinds.hpp"
#include "dgw/arena.hpp"
#include "dgw/weaver.hpp"

namespace dgw {

// ---- Verifier verdict ----------------------------------------------------
enum class Verdict : std::uint8_t {
  Pass,
  Fail,
  NA,
};

// ---- One finding ---------------------------------------------------------
struct Finding {
  std::uint8_t     layer;     // 1=structural, 2=semantic, 3=memory, 4=speculative
  Verdict          verdict;
  std::string      rule;      // e.g. "8.1.port_in_bounds"
  NodeId           node;
  EdgeId           edge;
  std::string      message;
};

// ---- Verifier report -----------------------------------------------------
struct VerifyReport {
  bool ok{true};                       // true iff zero FAIL findings.
  std::uint32_t pass_count{0};
  std::uint32_t fail_count{0};
  std::uint32_t na_count{0};
  std::vector<Finding> findings;
};

// ---- The verifier --------------------------------------------------------
class WebVerifier {
 public:
  explicit WebVerifier(const GraphArena* arena, const Weaver* weaver)
    : arena_(arena), weaver_(weaver) {}

  // Runs all four layers and returns the report.
  VerifyReport verify_all();

  // Individual layers (also called by verify_all).
  void check_structural(VerifyReport& r);
  void check_semantic(VerifyReport& r);
  void check_memory_and_effect(VerifyReport& r);
  void check_speculative(VerifyReport& r);

 private:
  void add_fail_(VerifyReport& r, std::uint8_t layer, std::string rule,
                 NodeId n, EdgeId e, std::string msg);
  void add_pass_(VerifyReport& r, std::uint8_t layer, std::string rule);

  const GraphArena* arena_;
  const Weaver* weaver_;
};

// Pretty-print a verifier report to stderr (used by the smoke binary).
void print_report(const VerifyReport& r);

}  // namespace dgw
