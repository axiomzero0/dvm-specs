// src/trace.cpp — Trace recorder implementation + pretty-printer.
//
#include "dvm/trace.hpp"

#include <print>

namespace dvm {

namespace {

const char* exit_reason_str(ExitReason r) noexcept {
  switch (r) {
    case ExitReason::Return:         return "Return";
    case ExitReason::BranchNotTaken: return "BranchNotTaken";
    case ExitReason::BranchTaken:    return "BranchTaken";
    case ExitReason::Trap:           return "Trap";
    case ExitReason::MaxLength:      return "MaxLength";
    case ExitReason::LoopClose:      return "LoopClose";
  }
  return "?";
}

}  // namespace

void print_trace(const TraceFragment& frag) {
  std::println("TraceFragment: fn={}, entry_pc={}, len={}, exit={}, loop={}",
               frag.entry_function_id, frag.entry_pc, frag.length(),
               exit_reason_str(frag.exit_reason),
               frag.is_loop() ? "yes" : "no");
  if (frag.is_loop()) {
    std::println("  loop_head_pc={}", frag.loop_head_pc);
  }
  std::println("  entry_registers: {} values", frag.entry_registers.size());
  for (std::size_t i = 0; i < frag.instructions.size(); ++i) {
    const auto& e = frag.instructions[i];
    std::println("  [{}] pc={} depth={} op=0x{:04X} s1={} s2={} s3={}",
                 i, e.pc, e.frame_depth,
                 static_cast<unsigned>(e.cell.opcode()),
                 static_cast<unsigned>(e.cell.s1()),
                 static_cast<unsigned>(e.cell.s2()),
                 static_cast<unsigned>(e.cell.s3()));
  }
  std::println("  exit_pc={} exit_depth={}", frag.exit_pc, frag.exit_frame_depth);
}

}  // namespace dvm
