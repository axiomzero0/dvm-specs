// src/signatures.cpp — Implementation of signature_of, matches_signature,
// expected_input_kind, expected_output_kind, node_kind_name.
//
// Spec citation: DGW-Core-IR.md Part 2.2 "Port Signatures".
//
#include "dgw/signatures.hpp"

#include <array>

namespace dgw {

namespace sig_detail {
// We expose the canonical arrays here as static getters so signature_of()
// can return std::span over them.
// (They are declared `inline constexpr` in the header; we just point at
// them. No need to redefine.)
}  // namespace sig_detail

// ---- Helper macro to build a signature entry -----------------------------
#define DGW_SIG(KIND, IN_ARR, OUT_ARR, FLAGS, THROW, OBS, PURE)              \
  case NodeKind::KIND: {                                                      \
    NodeSignature s;                                                          \
    s.node_kind = NodeKind::KIND;                                             \
    s.inputs  = std::span<const PortSlot>(sig_detail::IN_ARR.data(),         \
                                          sig_detail::IN_ARR.size());          \
    s.outputs = std::span<const PortSlot>(sig_detail::OUT_ARR.data(),         \
                                          sig_detail::OUT_ARR.size());         \
    s.default_flags = (FLAGS);                                                \
    s.can_throw  = (THROW);                                                   \
    s.observable = (OBS);                                                      \
    s.pure       = (PURE);                                                    \
    return s;                                                                  \
  }

NodeSignature signature_of(NodeKind k) noexcept {
  switch (k) {
    DGW_SIG(START,        start_in,       start_out,       NodeFlags::None, false, false, true)
    DGW_SIG(CONST,        const_in,       const_out,       NodeFlags::Pure, false, false, true)
    DGW_SIG(REF,          ref_in,         ref_out,         NodeFlags::Pure, false, false, true)
    DGW_SIG(ALLOC,        alloc_in,       alloc_out,       NodeFlags::None, false, false, false)
    DGW_SIG(MATERIALIZE,  materialize_in, materialize_out, NodeFlags::None, false, false, false)
    DGW_SIG(LOAD,         load_in,        load_out,        NodeFlags::None, true,  false, false)
    DGW_SIG(STORE,        store_in,       store_out,       NodeFlags::None, true,  true,  false)
    DGW_SIG(MEMORY_JOIN,  mjoin_in,       mjoin_out,       NodeFlags::None, false, false, false)
    DGW_SIG(ADD,          arith_in,       arith_out,       NodeFlags::Pure, false, false, true)
    DGW_SIG(SUB,          arith_in,       arith_out,       NodeFlags::Pure, false, false, true)
    DGW_SIG(MUL,          arith_in,       arith_out,       NodeFlags::Pure, false, false, true)
    DGW_SIG(DIV,          arith_in,       arith_out,       NodeFlags::None, true,  false, false)
    DGW_SIG(MOD,          arith_in,       arith_out,       NodeFlags::None, true,  false, false)
    DGW_SIG(FADD,         arith_in,       arith_out,       NodeFlags::Pure, false, false, true)
    DGW_SIG(FSUB,         arith_in,       arith_out,       NodeFlags::Pure, false, false, true)
    DGW_SIG(FMUL,         arith_in,       arith_out,       NodeFlags::Pure, false, false, true)
    DGW_SIG(FDIV,         arith_in,       arith_out,       NodeFlags::Pure, false, false, true)
    DGW_SIG(AND,          arith_in,       arith_out,       NodeFlags::Pure, false, false, true)
    DGW_SIG(OR,           arith_in,       arith_out,       NodeFlags::Pure, false, false, true)
    DGW_SIG(XOR,          arith_in,       arith_out,       NodeFlags::Pure, false, false, true)
    DGW_SIG(SHL,          arith_in,       arith_out,       NodeFlags::Pure, false, false, true)
    DGW_SIG(SHR,          arith_in,       arith_out,       NodeFlags::Pure, false, false, true)
    DGW_SIG(SAR,          arith_in,       arith_out,       NodeFlags::Pure, false, false, true)
    DGW_SIG(CMP_EQ,       cmp_in,         cmp_out,         NodeFlags::Pure, false, false, true)
    DGW_SIG(CMP_NE,       cmp_in,         cmp_out,         NodeFlags::Pure, false, false, true)
    DGW_SIG(CMP_LT,       cmp_in,         cmp_out,         NodeFlags::Pure, false, false, true)
    DGW_SIG(CMP_LE,       cmp_in,         cmp_out,         NodeFlags::Pure, false, false, true)
    DGW_SIG(CMP_GT,       cmp_in,         cmp_out,         NodeFlags::Pure, false, false, true)
    DGW_SIG(CMP_GE,       cmp_in,         cmp_out,         NodeFlags::Pure, false, false, true)
    DGW_SIG(CAST,         cast_in,        cast_out,        NodeFlags::Pure, false, false, true)
    DGW_SIG(BRANCH,       branch_in,      branch_out,      NodeFlags::None, false, false, false)
    DGW_SIG(JOIN,         join_in,        join_out,        NodeFlags::None, false, false, false)
    DGW_SIG(STATE,        state_in,       state_out,       NodeFlags::None, false, false, false)
    DGW_SIG(HANDLER,      handler_in,     handler_out,     NodeFlags::None, false, false, false)
    DGW_SIG(RETURN,       return_in,      return_out,     NodeFlags::Observable, false, true,  false)
    DGW_SIG(DEOPT_TRAP,   trap_in,        trap_out,        NodeFlags::Observable, false, true,  false)
    DGW_SIG(UNCOMMON_TRAP, trap_in,      trap_out,        NodeFlags::Observable, false, true,  false)
    DGW_SIG(CALL,         call_in,        call_out,        NodeFlags::None, true,  false, false)
    DGW_SIG(GUARD,        guard_in,       guard_out,       NodeFlags::Guarded, false, false, false)
    DGW_SIG(FWD,          fwd_in,         fwd_out,         NodeFlags::Pure, false, false, true)
    case NodeKind::DEAD: {
      NodeSignature s;
      s.node_kind = NodeKind::DEAD;
      return s;
    }
  }
  return NodeSignature{};
}

#undef DGW_SIG

// ---- matches_signature ---------------------------------------------------
// For variadic kinds (JOIN, CALL, STATE), the actual port count may exceed
// the canonical. We accept anything >= canonical here; the verifier checks
// the tail slots have the right EdgeKind.
bool matches_signature(NodeKind k, std::uint16_t actual_in_count,
                       std::uint16_t actual_out_count) noexcept {
  const auto s = signature_of(k);
  const std::uint16_t sig_in  = static_cast<std::uint16_t>(s.inputs.size());
  const std::uint16_t sig_out = static_cast<std::uint16_t>(s.outputs.size());
  switch (k) {
    case NodeKind::JOIN:
      // JOIN: >= 4 (2 control + 2 value), must be even (equal ctrl/value).
      return actual_in_count >= sig_in && actual_in_count >= 4 &&
             (actual_in_count % 2) == 0 &&
             actual_out_count == sig_out;
    case NodeKind::CALL:
      // CALL: >= 4 (control + mem + 2 args); out: 2 (ret + mem) or 3 (with EXCEPT).
      return actual_in_count >= sig_in &&
             (actual_out_count == 2 || actual_out_count == 3);
    case NodeKind::STATE:
      // STATE: >= 2 (init + backedge), in/out must match (one value per loop var).
      return actual_in_count >= 2 && (actual_in_count % 2) == 0 &&
             actual_out_count == 1;
    default:
      return actual_in_count == sig_in && actual_out_count == sig_out;
  }
}

// ---- expected_input_kind / expected_output_kind --------------------------
EdgeKind expected_input_kind(NodeKind k, std::uint16_t idx) noexcept {
  const auto s = signature_of(k);
  if (idx < s.inputs.size()) return s.inputs[idx].kind;
  // Variadic tail: all VALUE for CALL/JOIN/STATE.
  switch (k) {
    case NodeKind::CALL:
    case NodeKind::JOIN:
    case NodeKind::STATE:
      return EdgeKind::VALUE;
    default:
      return EdgeKind::VALUE;  // lenient
  }
}

EdgeKind expected_output_kind(NodeKind k, std::uint16_t idx) noexcept {
  const auto s = signature_of(k);
  if (idx < s.outputs.size()) return s.outputs[idx].kind;
  // Variadic tail: for CALL, the optional EXCEPT output (port 2).
  if (k == NodeKind::CALL && idx == 2) return EdgeKind::EXCEPT;
  return EdgeKind::VALUE;
}

// ---- node_kind_name -------------------------------------------------------
std::string_view node_kind_name(NodeKind k) noexcept {
  switch (k) {
    case NodeKind::CONST:        return "CONST";
    case NodeKind::REF:          return "REF";
    case NodeKind::ALLOC:        return "ALLOC";
    case NodeKind::MATERIALIZE:  return "MATERIALIZE";
    case NodeKind::LOAD:         return "LOAD";
    case NodeKind::STORE:        return "STORE";
    case NodeKind::MEMORY_JOIN:  return "MEMORY_JOIN";
    case NodeKind::ADD:          return "ADD";
    case NodeKind::SUB:          return "SUB";
    case NodeKind::MUL:          return "MUL";
    case NodeKind::DIV:          return "DIV";
    case NodeKind::MOD:          return "MOD";
    case NodeKind::FADD:         return "FADD";
    case NodeKind::FSUB:         return "FSUB";
    case NodeKind::FMUL:         return "FMUL";
    case NodeKind::FDIV:         return "FDIV";
    case NodeKind::AND:          return "AND";
    case NodeKind::OR:           return "OR";
    case NodeKind::XOR:          return "XOR";
    case NodeKind::SHL:          return "SHL";
    case NodeKind::SHR:          return "SHR";
    case NodeKind::SAR:          return "SAR";
    case NodeKind::CMP_EQ:       return "CMP_EQ";
    case NodeKind::CMP_NE:       return "CMP_NE";
    case NodeKind::CMP_LT:       return "CMP_LT";
    case NodeKind::CMP_LE:       return "CMP_LE";
    case NodeKind::CMP_GT:       return "CMP_GT";
    case NodeKind::CMP_GE:       return "CMP_GE";
    case NodeKind::CAST:         return "CAST";
    case NodeKind::START:        return "START";
    case NodeKind::BRANCH:       return "BRANCH";
    case NodeKind::JOIN:         return "JOIN";
    case NodeKind::STATE:        return "STATE";
    case NodeKind::HANDLER:      return "HANDLER";
    case NodeKind::RETURN:       return "RETURN";
    case NodeKind::DEOPT_TRAP:   return "DEOPT_TRAP";
    case NodeKind::UNCOMMON_TRAP:return "UNCOMMON_TRAP";
    case NodeKind::CALL:         return "CALL";
    case NodeKind::GUARD:        return "GUARD";
    case NodeKind::FWD:          return "FWD";
    case NodeKind::DEAD:         return "DEAD";
  }
  return "?";
}

}  // namespace dgw
