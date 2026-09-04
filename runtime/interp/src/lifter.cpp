// src/lifter.cpp — DGW lifter: TraceFragment → DGW-Core IR graph.
//
// Spec citation: DVM-Hybrid-Tracing-Architecture.md §12.3 (trace
// validation), §T-008 (Guest Bytecode Lifting). DGW-Core-IR.md Parts 1-7.
//
// The lifter converts a recorded TraceFragment into a DGW-Core IR graph.
// Each CRB instruction becomes one or more DGW nodes. CRB registers are
// renamed to DGW SSA values: each register write produces a new SSA value
// (a new NodeId); each register read uses the most recent SSA value for
// that register index.
//
#include "dvm/lifter.hpp"

#include <print>
#include <unordered_map>
#include <memory>

#include "dgw/graph.hpp"
#include "dgw/weaver.hpp"
#include "dgw/kinds.hpp"
#include "dgw/signatures.hpp"

// We need the CRB opcode constants from the interpreter's own header.
#include "dvm/opcodes_def.hpp"

namespace dvm {

using dgw::Graph;
using dgw::NodeId;
using dgw::Weaver;
using dgw::NodeKind;
using InstrCell = crb::InstrCell;
namespace crb_op = crb::op;

// ---- Register map: CRB register index → DGW SSA value (NodeId) ----------
// This is the "register renaming" that converts the linear register machine
// trace into SSA form. Each write to a CRB register creates a new NodeId.
struct RegMap {
  std::unordered_map<std::uint16_t, NodeId> regs;

  // Read a register's current SSA value. Returns kNullNode if the register
  // has not been written (uninitialized — the interpreter's default is Null).
  NodeId read(std::uint16_t idx) const {
    auto it = regs.find(idx);
    return it != regs.end() ? it->second : NodeId{};
  }

  // Write a new SSA value to a register.
  void write(std::uint16_t idx, NodeId v) {
    regs[idx] = v;
  }
};

// ---- The main lifter ----------------------------------------------------
dgw::Graph* lift_trace(const TraceFragment& frag) {
  auto* graph = new Graph();
  Weaver& w = graph->weaver();

  // Create the START node — source of CONTROL and MEMORY edges.
  NodeId start = w.create_start();
  NodeId ctrl = start;  // current control token
  NodeId mem = start;   // current memory token (START also produces MEMORY)

  // Register map: CRB register index → DGW SSA value.
  RegMap regmap;

  // Initialize registers from the trace's entry_registers snapshot.
  // The entry registers are the live values at the trace head.
  for (std::size_t i = 0; i < frag.entry_registers.size(); ++i) {
    const Value& v = frag.entry_registers[i];
    NodeId n;
    switch (v.tag) {
      case TypeTag::Int64:
        n = w.create_const(v.as_i64());
        break;
      case TypeTag::Float64:
        n = w.create_const(v.as_f64());
        break;
      case TypeTag::Null:
        n = w.create_const(static_cast<std::int64_t>(0));
        break;
      case TypeTag::Bool:
        n = w.create_const(static_cast<std::int64_t>(v.as_bool() ? 1 : 0));
        break;
      default:
        n = w.create_const(static_cast<std::int64_t>(0));
        break;
    }
    regmap.write(static_cast<std::uint16_t>(i), n);
  }

  // Walk the trace's instructions and create DGW nodes for each.
  for (const auto& entry : frag.instructions) {
    const InstrCell& cell = entry.cell;
    std::uint16_t op_val = cell.opcode();

    // ---- §10 Move/constant opcodes --------------------------------------
    if (op_val == crb_op::MOV_CONST) {
      // dst = const(pool_idx). We don't have the constant pool here, so
      // we create a CONST node with a placeholder value. A full lifter
      // would look up the constant from the module's pool.
      NodeId n = w.create_const(static_cast<std::int64_t>(0));
      regmap.write(cell.s1(), n);
    }
    // ---- §11 Integer arithmetic ----------------------------------------
    else if (op_val == crb_op::ADD_I64_WRAP) {
      NodeId a = regmap.read(cell.s2());
      NodeId b = regmap.read(cell.s3());
      NodeId n = w.create_arith(NodeKind::ADD, a, b);
      regmap.write(cell.s1(), n);
    }
    else if (op_val == crb_op::SUB_I64_WRAP) {
      NodeId a = regmap.read(cell.s2());
      NodeId b = regmap.read(cell.s3());
      NodeId n = w.create_arith(NodeKind::SUB, a, b);
      regmap.write(cell.s1(), n);
    }
    else if (op_val == crb_op::MUL_I64_WRAP) {
      NodeId a = regmap.read(cell.s2());
      NodeId b = regmap.read(cell.s3());
      NodeId n = w.create_arith(NodeKind::MUL, a, b);
      regmap.write(cell.s1(), n);
    }
    else if (op_val == crb_op::NEG_I64_WRAP) {
      NodeId a = regmap.read(cell.s2());
      // NEG is unary; create a CONST(0) and SUB.
      NodeId zero = w.create_const(static_cast<std::int64_t>(0));
      NodeId n = w.create_arith(NodeKind::SUB, zero, a);
      regmap.write(cell.s1(), n);
    }
    // ---- §14 Comparisons ------------------------------------------------
    else if (op_val == crb_op::CMP_EQ) {
      NodeId a = regmap.read(cell.s2());
      NodeId b = regmap.read(cell.s3());
      NodeId n = w.create_cmp(NodeKind::CMP_EQ, a, b);
      regmap.write(cell.s1(), n);
    }
    else if (op_val == crb_op::CMP_NE) {
      NodeId a = regmap.read(cell.s2());
      NodeId b = regmap.read(cell.s3());
      NodeId n = w.create_cmp(NodeKind::CMP_NE, a, b);
      regmap.write(cell.s1(), n);
    }
    else if (op_val == crb_op::CMP_LT_S) {
      NodeId a = regmap.read(cell.s2());
      NodeId b = regmap.read(cell.s3());
      NodeId n = w.create_cmp(NodeKind::CMP_LT, a, b);
      regmap.write(cell.s1(), n);
    }
    else if (op_val == crb_op::CMP_LE_S) {
      NodeId a = regmap.read(cell.s2());
      NodeId b = regmap.read(cell.s3());
      NodeId n = w.create_cmp(NodeKind::CMP_LE, a, b);
      regmap.write(cell.s1(), n);
    }
    else if (op_val == crb_op::CMP_GT_S) {
      NodeId a = regmap.read(cell.s2());
      NodeId b = regmap.read(cell.s3());
      NodeId n = w.create_cmp(NodeKind::CMP_GT, a, b);
      regmap.write(cell.s1(), n);
    }
    else if (op_val == crb_op::CMP_GE_S) {
      NodeId a = regmap.read(cell.s2());
      NodeId b = regmap.read(cell.s3());
      NodeId n = w.create_cmp(NodeKind::CMP_GE, a, b);
      regmap.write(cell.s1(), n);
    }
    // ---- §15 Control flow ------------------------------------------------
    else if (op_val == crb_op::BR_TRUE || op_val == crb_op::BR_FALSE ||
             op_val == crb_op::BR_NULL || op_val == crb_op::BR_NONNULL ||
             op_val == crb_op::JMP) {
      // For a loop trace, the last branch is the backedge that closes the
      // loop. We create a BRANCH + STATE (loop header) + GUARD.
      // For a side-exit trace, we create a GUARD + DEOPT_TRAP.
      NodeId cond = (op_val == crb_op::BR_TRUE || op_val == crb_op::BR_FALSE)
                        ? regmap.read(cell.s1())
                        : w.create_const(static_cast<std::int64_t>(1));

      if (frag.is_loop() && &entry == &frag.instructions.back()) {
        // This is the backedge — create a STATE node (loop header) + BRANCH.
        NodeId state = w.create_state();
        w.connect_value(cond, state, dgw::PortId{0});
        // The loop continues: connect state's output back to the trace head.
        // (A full implementation would connect to the first node of the trace.)
        NodeId br = w.create_branch(ctrl, cond);
        ctrl = br;
      } else {
        // Side exit: create a GUARD + DEOPT_TRAP.
        NodeId guard = w.create_guard(dgw::FrameStateId{1},
                                       dgw::DeoptReason::NullCheck);
        w.connect_control(ctrl, guard);
        w.connect_value(cond, guard, dgw::PortId{1});
        // Guard's success path continues; failure goes to DEOPT_TRAP.
        NodeId trap = w.create_deopt_trap();
        // Connect guard's failure output to the trap.
        const dgw::NodeSignature gs = dgw::signature_of(dgw::NodeKind::GUARD);
        const std::uint16_t g_in = static_cast<std::uint16_t>(gs.inputs.size());
        w.connect(guard, dgw::PortId{static_cast<std::uint16_t>(g_in + 1)},
                   trap, dgw::PortId{0}, dgw::EdgeKind::CONTROL);
        // Success path continues from guard.
        ctrl = guard;
      }
    }
    // ---- §16 Calls ------------------------------------------------------
    else if (op_val == crb_op::CALL_DIRECT) {
      // Minimal: create a CALL node. A full lifter would look up the
      // call site table for the function ID and argument layout.
      NodeId call = w.create_call(dgw::SymbolId{cell.s2()},
                                    dgw::CallConv::Guest, 0, true,
                                    ctrl, mem);
      ctrl = call;
      regmap.write(cell.s1(), call);
    }
    // ---- §16 Return ------------------------------------------------------
    else if (op_val == crb_op::RET) {
      NodeId ret_val = regmap.read(cell.s1());
      NodeId ret = w.create_return(ctrl, ret_val);
      ctrl = ret;
    }
    else if (op_val == crb_op::RET_VOID) {
      NodeId ret = w.create_return(ctrl, w.create_const(static_cast<std::int64_t>(0)));
      ctrl = ret;
    }
    // ---- §9 System opcodes -----------------------------------------------
    else if (op_val == crb_op::NOP || op_val == crb_op::SAFEPOINT) {
      // No-op: just thread the control.
    }
    else if (op_val == crb_op::TRAP || op_val == crb_op::UNREACHABLE) {
      NodeId trap = w.create_deopt_trap();
      w.connect_control(ctrl, trap);
      ctrl = trap;
    }
    // ---- §20 Exceptions -------------------------------------------------
    else if (op_val == crb_op::THROW) {
      NodeId trap = w.create_deopt_trap();
      w.connect_control(ctrl, trap);
      ctrl = trap;
    }
    // ---- §19+18 Allocation + Object model --------------------------------
    else if (op_val == crb_op::ALLOC) {
      std::uint32_t field_count = InstrCell::imm32(cell.s2(), cell.s3());
      NodeId n = w.create_alloc(dgw::RegionKind::HEAP, field_count, 8);
      regmap.write(cell.s1(), n);
    }
    else if (op_val == crb_op::OBJ_GET) {
      (void)regmap.read(cell.s2()); // obj (unused in minimal lifter)
      // Minimal: OBJ_GET becomes a LOAD. A full lifter would create
      // a REF + LOAD from the object's region.
      NodeId ref = w.create_ref(dgw::RegionId{0}, cell.s3(),
                                  dgw::AccessPerm::ReadOnly);
      NodeId load = w.create_load(ctrl, mem, ref);
      ctrl = load;
      mem = load;
      regmap.write(cell.s1(), load);
    }
    else if (op_val == crb_op::OBJ_SET) {
      (void)regmap.read(cell.s1()); // obj (unused in minimal lifter)
      NodeId val = regmap.read(cell.s3());
      NodeId ref = w.create_ref(dgw::RegionId{0}, cell.s2(),
                                  dgw::AccessPerm::ReadWrite);
      NodeId store = w.create_store(ctrl, mem, ref, val);
      ctrl = store;
      mem = store;
    }
    // ---- Unhandled opcodes -----------------------------------------------
    else {
      // For unhandled opcodes, create a NOP-equivalent (just thread control).
      // A full lifter would handle every CRB opcode.
    }
  }

  return graph;
}

void print_lifted_graph(const Graph& g) {
  auto& arena = const_cast<Graph&>(g).arena();
  std::println("Lifted DGW graph: {} nodes, {} edges",
               arena.node_count(), arena.edge_count());
  for (std::uint32_t n = 0; n < arena.node_count(); ++n) {
    if (arena.node_kinds[n] == dgw::NodeKind::DEAD) continue;
    std::println("  node #{}: kind={} flags=0x{:x} uses={}",
                 n, dgw::node_kind_name(arena.node_kinds[n]).data(),
                 static_cast<unsigned>(arena.node_flags[n]),
                 const_cast<Graph&>(g).weaver().use_count(dgw::NodeId{n}));
  }
  // Run the verifier on the lifted graph.
  auto report = const_cast<Graph&>(g).verify();
  std::println("  verifier: ok={} pass={} fail={}",
               report.ok, report.pass_count, report.fail_count);
  if (!report.ok) {
    dgw::print_report(report);
  }
}

}  // namespace dvm
