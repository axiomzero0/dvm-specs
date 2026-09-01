// dgw/signatures.hpp — Port Signature Table (Spec Part 2.2).
//
// Spec citation: DGW-Core-IR.md Part 2.2 "Port Signatures".
//
// "Every NodeKind has a strict, compile-time verified port signature.
//  The verifier (Part 9) ensures edges only connect to compatible ports."
//
// This header provides the static descriptor of every NodeKind's ports:
//   - how many input/output ports,
//   - what EdgeKind each port accepts,
//   - whether the port requires a VALUE of a specific TypeKind.
//
// The Weaver consults this table before any edge mutation to refuse
// ill-typed connections, and the WebVerifier (Part 8.2) re-checks it after
// every pass.
//
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "dgw/ids.hpp"
#include "dgw/kinds.hpp"

namespace dgw {

// ---- PortSlot: descriptor for one port on a node -------------------------
// A port is either an input port or an output port, and accepts exactly
// one EdgeKind. VALUE ports additionally carry a TypeKind hint that the
// verifier uses for type-compat checks (Part 8.2).
enum class PortDir : std::uint8_t { In, Out };

enum class TypeKind : std::uint8_t {
  Bottom,   // Uninitialized / unreachable. Subtype of everything.
  Top,      // Unknown / poly. Supertype of everything.
  I1,       // Boolean.
  I8, I16, I32, I64,
  F32, F64,
  Ref,      // Reference to a Region (Part 3.2).
  Region,   // First-class Region identity (Part 3.1).
  Void,     // No value (used for pure CONTROL outputs).
};

struct PortSlot {
  PortDir    dir{PortDir::In};
  EdgeKind   kind{EdgeKind::VALUE};
  TypeKind   type{TypeKind::Top};   // Only meaningful when kind == VALUE.
  std::string_view name{};          // Human-readable label (debug only).
};

// ---- NodeSignature: full descriptor for one NodeKind ---------------------
struct NodeSignature {
  NodeKind                 node_kind{NodeKind::DEAD};
  std::span<const PortSlot> inputs{};
  std::span<const PortSlot> outputs{};
  NodeFlags                default_flags{NodeFlags::None};
  bool                    can_throw{false};   // has an EXCEPT output?
  bool                    observable{false};  // is a DCE seed? (Part 6.2)
  bool                    pure{false};         // GVN-eligible? (Part 6.1)
};

// ---- Canonical signatures -------------------------------------------------
// We define the spec's example signatures (LOAD, GUARD) verbatim, and a
// consistent set for the other NodeKinds listed in kinds.hpp.

namespace sig_detail {

// Input/output port tables. Stored as `inline constexpr` arrays so we can
// take a std::span over them without copying.

// ---- LOAD (spec example) ----
// Input[0]: CONTROL
// Input[1]: MEMORY
// Input[2]: VALUE (REF)
// Output[0]: VALUE (loaded data)
// Output[1]: MEMORY (new memory state)
// Output[2]: EXCEPT (optional — present when HasExcept flag is set)
//
// The runtime selects the actual port count per instance: when can_throw is
// false, the LOAD has 2 outputs; when true, it has 3 (adds EXCEPT).
// `expected_output_kind` handles the EXCEPT-at-index-2 special case.
inline constexpr std::array<PortSlot, 3> load_in = {{
    {PortDir::In,  EdgeKind::CONTROL, TypeKind::Void,    "in_control"},
    {PortDir::In,  EdgeKind::MEMORY,  TypeKind::Void,    "in_mem"},
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::Ref,     "ref"},
}};
inline constexpr std::array<PortSlot, 3> load_out = {{
    {PortDir::Out, EdgeKind::VALUE,   TypeKind::Top,     "data"},
    {PortDir::Out, EdgeKind::MEMORY,  TypeKind::Void,    "out_mem"},
    {PortDir::Out, EdgeKind::EXCEPT,  TypeKind::Void,    "except"},
}};

// ---- STORE (mirror of LOAD) ----
// Input[0]: CONTROL, Input[1]: MEMORY, Input[2]: VALUE (REF),
// Input[3]: VALUE (data), Output[0]: MEMORY
// STOREs to non-global regions do not throw; we omit EXCEPT for STORE.
inline constexpr std::array<PortSlot, 4> store_in = {{
    {PortDir::In,  EdgeKind::CONTROL, TypeKind::Void,    "in_control"},
    {PortDir::In,  EdgeKind::MEMORY,  TypeKind::Void,    "in_mem"},
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::Ref,     "ref"},
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::Top,     "value"},
}};
inline constexpr std::array<PortSlot, 1> store_out = {{
    {PortDir::Out, EdgeKind::MEMORY,  TypeKind::Void,    "out_mem"},
}};

// ---- GUARD (spec example) ----
// Input[0]: CONTROL
// Input[1]: VALUE (the boolean condition to check)
// Output[0]: CONTROL (Success path, assumption holds)
// Output[1]: CONTROL (Failure path, routes to DEOPT_TRAP)
inline constexpr std::array<PortSlot, 2> guard_in = {{
    {PortDir::In,  EdgeKind::CONTROL, TypeKind::Void,    "in_control"},
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::I1,     "cond"},
}};
inline constexpr std::array<PortSlot, 2> guard_out = {{
    {PortDir::Out, EdgeKind::CONTROL, TypeKind::Void,    "ok_control"},
    {PortDir::Out, EdgeKind::CONTROL, TypeKind::Void,    "fail_control"},
}};

// ---- BRANCH ----
inline constexpr std::array<PortSlot, 2> branch_in = {{
    {PortDir::In,  EdgeKind::CONTROL, TypeKind::Void,    "in_control"},
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::I1,      "cond"},
}};
inline constexpr std::array<PortSlot, 2> branch_out = {{
    {PortDir::Out, EdgeKind::CONTROL, TypeKind::Void,    "true_control"},
    {PortDir::Out, EdgeKind::CONTROL, TypeKind::Void,    "false_control"},
}};

// ---- JOIN (variadic in practice; canonical 2-input form here) ----
// The runtime supports N-way joins by allocating N input CONTROL slots
// and N input VALUE slots. For the static signature we declare the 2-input
// form; the Weaver queries the actual port_count on the node instance.
inline constexpr std::array<PortSlot, 4> join_in = {{
    {PortDir::In,  EdgeKind::CONTROL, TypeKind::Void,    "in_control_0"},
    {PortDir::In,  EdgeKind::CONTROL, TypeKind::Void,    "in_control_1"},
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::Top,     "in_value_0"},
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::Top,     "in_value_1"},
}};
inline constexpr std::array<PortSlot, 1> join_out = {{
    {PortDir::Out, EdgeKind::VALUE,   TypeKind::Top,     "out_value"},
}};

// ---- STATE (loop header, Part 4.2) ----
// Two inputs per "loop variable": init (from outside) and backedge.
inline constexpr std::array<PortSlot, 2> state_in = {{
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::Top,     "init"},
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::Top,     "backedge"},
}};
inline constexpr std::array<PortSlot, 1> state_out = {{
    {PortDir::Out, EdgeKind::VALUE,   TypeKind::Top,     "current"},
}};

// ---- CALL (variadic; canonical 2-arg form) ----
// Input[0]: CONTROL
// Input[1]: MEMORY
// Input[2..2+N): VALUE (arguments)
// Output[0]: VALUE (return value)
// Output[1]: MEMORY (new memory state)
// Output[2]: EXCEPT (optional — present when HasExcept flag is set)
inline constexpr std::array<PortSlot, 4> call_in = {{
    {PortDir::In,  EdgeKind::CONTROL, TypeKind::Void,    "in_control"},
    {PortDir::In,  EdgeKind::MEMORY,  TypeKind::Void,    "in_mem"},
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::Top,     "arg0"},
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::Top,     "arg1"},
}};
inline constexpr std::array<PortSlot, 3> call_out = {{
    {PortDir::Out, EdgeKind::VALUE,   TypeKind::Top,     "ret"},
    {PortDir::Out, EdgeKind::MEMORY,  TypeKind::Void,    "out_mem"},
    {PortDir::Out, EdgeKind::EXCEPT,  TypeKind::Void,    "except"},
}};

// ---- CONST ----
inline constexpr std::array<PortSlot, 0> const_in{};
inline constexpr std::array<PortSlot, 1> const_out = {{
    {PortDir::Out, EdgeKind::VALUE,   TypeKind::Top,     "value"},
}};

// ---- REF ----
inline constexpr std::array<PortSlot, 0> ref_in{};
inline constexpr std::array<PortSlot, 1> ref_out = {{
    {PortDir::Out, EdgeKind::VALUE,   TypeKind::Ref,     "ref"},
}};

// ---- ALLOC ----
inline constexpr std::array<PortSlot, 0> alloc_in{};
inline constexpr std::array<PortSlot, 1> alloc_out = {{
    {PortDir::Out, EdgeKind::VALUE,   TypeKind::Region, "region"},
}};

// ---- MATERIALIZE (Part 3.4) ----
// Input[0]: CONTROL
// Input[1]: MEMORY
// Input[2]: VALUE (the virtual Region view)
// Output[0]: VALUE (real heap pointer)
// Output[1]: MEMORY (new memory state)
inline constexpr std::array<PortSlot, 3> materialize_in = {{
    {PortDir::In,  EdgeKind::CONTROL, TypeKind::Void,    "in_control"},
    {PortDir::In,  EdgeKind::MEMORY,  TypeKind::Void,    "in_mem"},
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::Ref,     "virtual_ref"},
}};
inline constexpr std::array<PortSlot, 2> materialize_out = {{
    {PortDir::Out, EdgeKind::VALUE,   TypeKind::Ref,     "heap_ptr"},
    {PortDir::Out, EdgeKind::MEMORY,  TypeKind::Void,    "out_mem"},
}};

// ---- ARITHMETIC (binary; same shape for ADD/SUB/MUL/DIV/...) ----
inline constexpr std::array<PortSlot, 2> arith_in = {{
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::Top,     "lhs"},
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::Top,     "rhs"},
}};
inline constexpr std::array<PortSlot, 1> arith_out = {{
    {PortDir::Out, EdgeKind::VALUE,   TypeKind::Top,     "out"},
}};

// ---- CMP (binary; outputs I1) ----
inline constexpr std::array<PortSlot, 2> cmp_in = {{
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::Top,     "lhs"},
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::Top,     "rhs"},
}};
inline constexpr std::array<PortSlot, 1> cmp_out = {{
    {PortDir::Out, EdgeKind::VALUE,   TypeKind::I1,      "out"},
}};

// ---- START ----
inline constexpr std::array<PortSlot, 0> start_in{};
inline constexpr std::array<PortSlot, 2> start_out = {{
    {PortDir::Out, EdgeKind::CONTROL, TypeKind::Void,    "entry_control"},
    {PortDir::Out, EdgeKind::MEMORY,  TypeKind::Void,    "entry_memory"},
}};

// ---- RETURN ----
inline constexpr std::array<PortSlot, 2> return_in = {{
    {PortDir::In,  EdgeKind::CONTROL, TypeKind::Void,    "in_control"},
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::Top,     "value"},
}};
inline constexpr std::array<PortSlot, 0> return_out{};

// ---- HANDLER ----
inline constexpr std::array<PortSlot, 1> handler_in = {{
    {PortDir::In,  EdgeKind::EXCEPT,  TypeKind::Void,    "incoming_except"},
}};
inline constexpr std::array<PortSlot, 1> handler_out = {{
    {PortDir::Out, EdgeKind::CONTROL, TypeKind::Void,    "out_control"},
}};

// ---- DEOPT_TRAP / UNCOMMON_TRAP ----
inline constexpr std::array<PortSlot, 1> trap_in = {{
    {PortDir::In,  EdgeKind::CONTROL, TypeKind::Void,    "in_control"},
}};
inline constexpr std::array<PortSlot, 0> trap_out{};

// ---- FWD (Part 5.2) ----
// FWD has exactly one input (the new value) and one output (a transparent
// forwarding of that input). CleanupPass collapses FWD chains.
inline constexpr std::array<PortSlot, 1> fwd_in = {{
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::Top,     "forward_to"},
}};
inline constexpr std::array<PortSlot, 1> fwd_out = {{
    {PortDir::Out, EdgeKind::VALUE,   TypeKind::Top,     "value"},
}};

// ---- MEMORY_JOIN ----
inline constexpr std::array<PortSlot, 2> mjoin_in = {{
    {PortDir::In,  EdgeKind::MEMORY,  TypeKind::Void,    "in_mem_0"},
    {PortDir::In,  EdgeKind::MEMORY,  TypeKind::Void,    "in_mem_1"},
}};
inline constexpr std::array<PortSlot, 1> mjoin_out = {{
    {PortDir::Out, EdgeKind::MEMORY,  TypeKind::Void,    "out_mem"},
}};

// ---- CAST (Part 8.2 implicit-cast node) ----
inline constexpr std::array<PortSlot, 1> cast_in = {{
    {PortDir::In,  EdgeKind::VALUE,   TypeKind::Top,     "in"},
}};
inline constexpr std::array<PortSlot, 1> cast_out = {{
    {PortDir::Out, EdgeKind::VALUE,   TypeKind::Top,     "out"},
}};

}  // namespace sig_detail

// ---- The signature lookup function ----------------------------------------
// Returns a NodeSignature view for the given NodeKind. The spans point at
// static constant storage; the caller does not own them.
//
// Variadic node kinds (JOIN, CALL, STATE) have port counts that depend on
// the instance, not on the signature. The signature returned here is the
// *canonical* form; the Weaver allocates extra port slots when creating
// variadic instances (and the verifier uses the actual port_count stored
// on the node, not the signature's port count, when checking those).

NodeSignature signature_of(NodeKind k) noexcept;

// Returns true if the actual port layout (count, kinds) of a node matches
// the canonical signature, treating variadic node kinds specially.
bool matches_signature(NodeKind k, std::uint16_t actual_in_count,
                       std::uint16_t actual_out_count) noexcept;

// Returns the EdgeKind expected at the given input port index, per the
// canonical signature. Returns EdgeKind::VALUE (lenient default) for
// variadic tails.
EdgeKind expected_input_kind(NodeKind k, std::uint16_t idx) noexcept;
EdgeKind expected_output_kind(NodeKind k, std::uint16_t idx) noexcept;

// Returns a short human-readable name for a NodeKind (debug only).
std::string_view node_kind_name(NodeKind k) noexcept;

}  // namespace dgw
