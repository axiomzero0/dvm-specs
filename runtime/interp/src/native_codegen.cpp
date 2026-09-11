// src/native_codegen.cpp — Native code generation: DGW graph → x86-64.
//
// Spec citation: DGW-Core-IR.md Part 7.3 (Instruction Selection).
//
// Walks the DGW graph's node table in order and emits x86-64 machine code
// for each non-DEAD node. The lifter creates nodes in SSA order, so
// walking the node table in index order is a valid topological order.
//
// Calling convention: System V AMD64.
//   Arguments: rdi, rsi, rdx, rcx (first 4 DGW registers)
//   Result: rax
//
#include "dvm/native_codegen.hpp"
#include "dvm/x64_encoder.hpp"

#include <cstring>
#include <print>
#include <unordered_map>

#include "dgw/arena.hpp"
#include "dgw/weaver.hpp"
#include "dgw/kinds.hpp"
#include "dgw/signatures.hpp"
#include "dgw/graph.hpp"

namespace dvm {

using dgw::NodeKind;
using dgw::GraphArena;
using dgw::Weaver;
using dgw::NodeId;
using dgw::PortId;
using dgw::EdgeKind;

namespace {
// Trivial register allocator: assigns the first 4 DGW nodes to System V
// argument registers (rdi/rsi/rdx/rcx), extras to r8-r11, overflow to rax.
// Uses in-place update for arithmetic ops (result → src0's register).
// This is replaced by LinearScanAllocator (reg_alloc.hpp) for future use
// once the lifter properly models STATE backedges in loop traces.
struct RegAllocator {
  static constexpr Reg arg_regs[] = {Reg::RDI, Reg::RSI, Reg::RDX, Reg::RCX};
  static constexpr Reg extra_regs[] = {Reg::R8, Reg::R9, Reg::R10, Reg::R11};
  static constexpr int kMaxArgRegs = 4;
  static constexpr int kMaxExtraRegs = 4;

  std::unordered_map<std::uint32_t, Reg> map;
  int next_arg = 0;
  int next_extra = 0;

  Reg alloc(std::uint32_t node_id) {
    auto it = map.find(node_id);
    if (it != map.end()) return it->second;
    Reg r = (next_arg < kMaxArgRegs) ? arg_regs[next_arg++]
            : (next_extra < kMaxExtraRegs) ? extra_regs[next_extra++]
            : Reg::RAX;
    map[node_id] = r;
    return r;
  }

  Reg get(std::uint32_t node_id) const {
    auto it = map.find(node_id);
    return it != map.end() ? it->second : Reg::RAX;
  }

  bool has(std::uint32_t node_id) const {
    return map.find(node_id) != map.end();
  }
};
}  // namespace

NativeTrace compile_to_native(const dgw::Graph& graph) {
  NativeTrace nt;
  nt.code.allocate(4096);

  JitBuffer& buf = nt.code;
  RegAllocator ra;

  const GraphArena& arena = const_cast<dgw::Graph&>(graph).arena();
  const Weaver& w = const_cast<dgw::Graph&>(graph).weaver();

  // Function prologue
  encode_push(buf, Reg::RBP);
  encode_mov_reg(buf, Reg::RBP, Reg::RSP);

  // Track branch target for loop backedge patching
  std::size_t loop_start_offset = 0;
  std::size_t branch_patch_offset = 0;
  bool saw_branch = false;

  // Walk all nodes in index order (lifter creates them in SSA order)
  for (std::uint32_t n = 0; n < arena.node_count(); ++n) {
    if (arena.node_kinds[n] == NodeKind::DEAD) continue;

    NodeKind kind = arena.node_kinds[n];

    switch (kind) {
      case NodeKind::START:
        break;

      case NodeKind::CONST: {
        // The linear-scan allocator assigns entry-arg CONSTs to the
        // System V argument registers (rdi/rsi/rdx/rcx). Skip mov imm64
        // for those — the caller provides the values.
        // Non-entry CONSTs (created inside the trace) get mov imm64.
        Reg r = ra.alloc(n);
        if (r != Reg::RDI && r != Reg::RSI && r != Reg::RDX && r != Reg::RCX) {
          std::uint32_t pidx = arena.node_payload_idx[n];
          if (pidx < arena.consts.size()) {
            const auto& cp = arena.consts[pidx];
            if (std::holds_alternative<std::int64_t>(cp.value)) {
              encode_mov_imm64(buf, r, std::get<std::int64_t>(cp.value));
            }
          }
        }
        break;
      }

      case NodeKind::ADD:
      case NodeKind::SUB:
      case NodeKind::MUL: {
        if (loop_start_offset == 0) {
          loop_start_offset = buf.offset();
        }
        NodeId src0 = w.input_node(NodeId{n}, PortId{0});
        NodeId src1 = w.input_node(NodeId{n}, PortId{1});
        // Coalesce: the ADD writes in-place to src0's register (matches
        // CRB register-machine semantics: r0 = r0 + r1). This is
        // essential for loop traces where the ADD's output feeds back
        // through the STATE node into the ADD's input on the next
        // iteration. The linear-scan allocator assigns registers, but
        // for arithmetic ops we override: result goes to src0's reg.
        Reg dst = ra.has(src0.value) ? ra.get(src0.value) : ra.alloc(n); if (!ra.has(n)) ra.map[n] = dst;
        Reg r1 = ra.has(src1.value) ? ra.get(src1.value) : Reg::RSI;
        if (kind == NodeKind::ADD) encode_add_reg(buf, dst, r1);
        else if (kind == NodeKind::SUB) encode_sub_reg(buf, dst, r1);
        else if (kind == NodeKind::MUL) encode_add_reg(buf, dst, r1);
        break;
      }

      case NodeKind::CMP_LT:
      case NodeKind::CMP_EQ:
      case NodeKind::CMP_GT:
      case NodeKind::CMP_LE:
      case NodeKind::CMP_GE:
      case NodeKind::CMP_NE: {
        NodeId src0 = w.input_node(NodeId{n}, PortId{0});
        NodeId src1 = w.input_node(NodeId{n}, PortId{1});
        Reg dst = ra.alloc(n);
        Reg r0 = ra.has(src0.value) ? ra.get(src0.value) : Reg::RDI;
        Reg r1 = ra.has(src1.value) ? ra.get(src1.value) : Reg::RSI;

        // Zero the dst register first so setCC only sets the low byte
        // and the full 64-bit register is clean for test.
        encode_mov_imm64(buf, dst, 0);

        encode_cmp_reg(buf, r0, r1);
        switch (kind) {
          case NodeKind::CMP_LT: encode_setl(buf, dst); break;
          case NodeKind::CMP_EQ: encode_sete(buf, dst); break;
          case NodeKind::CMP_GT: {
            encode_cmp_reg(buf, r1, r0);
            encode_setl(buf, dst);
            break;
          }
          case NodeKind::CMP_LE: {
            buf.emit_byte(rex(false, false, false, static_cast<std::uint8_t>(dst) >= 8));
            buf.emit_byte(0x0F);
            buf.emit_byte(0x9E);
            buf.emit_byte(modrm(0, static_cast<std::uint8_t>(dst)));
            break;
          }
          case NodeKind::CMP_GE: {
            buf.emit_byte(rex(false, false, false, static_cast<std::uint8_t>(dst) >= 8));
            buf.emit_byte(0x0F);
            buf.emit_byte(0x9D);
            buf.emit_byte(modrm(0, static_cast<std::uint8_t>(dst)));
            break;
          }
          case NodeKind::CMP_NE: encode_setne(buf, dst); break;
          default: break;
        }
        break;
      }

      case NodeKind::BRANCH: {
        // BRANCH: the condition is input port 1 (VALUE).
        NodeId cond = w.input_node(NodeId{n}, PortId{1});
        Reg cond_r = ra.has(cond.value) ? ra.get(cond.value) : Reg::RCX;

        // Record the loop start offset (for the backedge target).
        // The loop body starts at the first non-START/CONST node,
        // which is the ADD. We record it before the first value op.
        // Actually: the loop start is the offset AFTER the prologue
        // and constant loads, where the ADD begins. We need to save
        // the offset of the first ADD/CMP/etc. op.
        // For now, we use the offset of the BRANCH instruction itself
        // as the branch target (since the trace is: ADD, CMP, BR_TRUE).
        // But the backedge should go to the START of the loop body,
        // which is the ADD — not the BRANCH.
        //
        // Solution: we record the offset of the first non-prologue,
        // non-CONST instruction as loop_start_offset.
        if (!saw_branch) {
          saw_branch = true;
          // test cond, cond; jnz loop_start (backedge if condition true)
          encode_test_reg(buf, cond_r, cond_r);
          branch_patch_offset = encode_jnz(buf);
        }
        break;
      }

      case NodeKind::DEOPT_TRAP:
        // Side exit: instead of ud2 (which would SIGILL), jump to the
        // epilogue. A full implementation would transfer to the deopt
        // handler. For the minimal JIT, we fall through to the epilogue.
        // Emit a jmp to a placeholder that will be patched to the epilogue.
        // For now, just emit nothing — the epilogue is emitted after all ops.
        break;

      case NodeKind::RETURN: {
        // Move the result (input port 1) to rax, then return.
        NodeId result = w.input_node(NodeId{n}, PortId{1});
        Reg result_r = ra.has(result.value) ? ra.get(result.value) : Reg::RDI;
        encode_mov_reg(buf, Reg::RAX, result_r);
        encode_pop(buf, Reg::RBP);
        encode_ret(buf);
        break;
      }

      default:
        buf.emit_byte(0x90);
        break;
    }
  }

  // Patch the branch backedge to jump to the loop start.
  // The loop start is the offset of the first value-producing op.
  if (branch_patch_offset != 0 && loop_start_offset != 0) {
    std::int32_t rel = static_cast<std::int32_t>(loop_start_offset) -
                        static_cast<std::int32_t>(branch_patch_offset + 4);
    buf.patch_u32(branch_patch_offset, static_cast<std::uint32_t>(rel));
  }

  // If no RETURN was emitted, add a default epilogue that returns rdi (r0).
  if (buf.size() > 0) {
    const auto* last = static_cast<const std::uint8_t*>(buf.entry()) + buf.size() - 1;
    if (*last != 0xC3) {
      encode_mov_reg(buf, Reg::RAX, Reg::RDI);
      encode_pop(buf, Reg::RBP);
      encode_ret(buf);
    }
  } else {
    encode_mov_reg(buf, Reg::RAX, Reg::RDI);
    encode_pop(buf, Reg::RBP);
    encode_ret(buf);
  }

  buf.make_executable();

  auto fn_ptr = reinterpret_cast<std::int64_t(*)(std::int64_t, std::int64_t,
                                                   std::int64_t, std::int64_t)>(
      buf.entry());
  nt.entry = [fn_ptr](std::int64_t r0, std::int64_t r1, std::int64_t r2,
                       std::int64_t r3) -> std::int64_t {
    return fn_ptr(r0, r1, r2, r3);
  };

  return nt;
}

void print_native_trace(const NativeTrace& nt) {
  std::println("NativeTrace: {} bytes of x86-64 machine code", nt.code_size());
  const auto* bytes = static_cast<const std::uint8_t*>(nt.code.entry());
  std::size_t dump = nt.code_size() < 64 ? nt.code_size() : 64;
  std::print("  hex: ");
  for (std::size_t i = 0; i < dump; ++i) {
    std::print("{:02X} ", bytes[i]);
    if ((i + 1) % 16 == 0) std::print("\n        ");
  }
  std::println();
}

}  // namespace dvm
