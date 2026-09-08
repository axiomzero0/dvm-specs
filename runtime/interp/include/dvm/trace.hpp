// dvm/trace.hpp — Trace recorder for Tier 1 meta-tracing.
//
// Spec citation: DVM-Hybrid-Tracing-Architecture.md §1.1 (meta-tracing),
// §12.1 (starting a root trace), §12.2 (recording).
//
// The trace recorder captures a sequence of CRB instructions executed by
// the interpreter, starting from a hot loop header and ending at a side
// exit (a branch not taken, a return, or a throw). The recorded trace is
// a linear sequence of InstrCells + metadata that the Tier 1 trace
// compiler can optimize (eliminate dispatch, promote registers, etc.).
//
// Recording is opt-in: the interpreter calls TraceRecorder::record() on
// every executed instruction. When not recording, record() is a no-op
// (checked via a single bool flag — zero overhead when disabled).
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dvm/crb.hpp"
#include "dvm/state.hpp"
#include "dvm/value.hpp"

namespace dvm {

// ---- Side-exit reason --------------------------------------------------
// Why the trace ended at this point.
enum class ExitReason : std::uint8_t {
  Return,          // RET / RET_VOID from the outermost frame
  BranchNotTaken,  // BR_TRUE/BR_FALSE condition was false → fall through
  BranchTaken,      // BR_TRUE/BR_FALSE condition was true → side exit
  Trap,            // TRAP / THROW / uncaught exception
  MaxLength,       // trace exceeded the max recording length
  LoopClose,       // backedge to the trace head — loop closed
};

// ---- A single recorded instruction ---------------------------------------
struct TraceEntry {
  crb::InstrCell cell;     // the instruction that executed
  std::uint32_t pc;        // PC at which it executed (within the function's code)
  std::uint32_t frame_depth;  // call frame depth at execution time
};

// ---- A recorded trace fragment -------------------------------------------
struct TraceFragment {
  // Entry metadata (§12.1)
  std::uint32_t entry_function_id{0};
  std::uint32_t entry_pc{0};          // the loop header PC
  std::vector<Value> entry_registers;  // snapshot of registers at entry

  // The recorded instruction sequence
  std::vector<TraceEntry> instructions;

  // Exit metadata
  ExitReason exit_reason{ExitReason::Return};
  std::uint32_t exit_pc{0};          // PC at the exit instruction
  std::uint32_t exit_frame_depth{0};

  // Loop detection: the PC of the backedge target (if the trace closed
  // via LoopClose). If non-zero, the trace is a loop.
  std::uint32_t loop_head_pc{0};

  // ---- Helpers ---------------------------------------------------------
  bool is_loop() const noexcept { return loop_head_pc != 0; }
  std::size_t length() const noexcept { return instructions.size(); }
};

// ---- The trace recorder --------------------------------------------------
class TraceRecorder {
 public:
  static constexpr std::size_t kMaxTraceLength = 4096;

  TraceRecorder() = default;

  // ---- Control ---------------------------------------------------------
  // Start recording a trace from the given function + PC.
  void start(std::uint32_t function_id, std::uint32_t pc,
             std::span<const Value> registers) {
    fragment_ = TraceFragment{};
    fragment_.entry_function_id = function_id;
    fragment_.entry_pc = pc;
    fragment_.entry_registers.assign(registers.begin(), registers.end());
    recording_ = true;
  }

  // Stop recording and return the fragment. Resets the recorder.
  TraceFragment stop() {
    recording_ = false;
    return std::move(fragment_);
  }

  // ---- Recording -------------------------------------------------------
  // Called by the interpreter on every executed instruction. No-op when
  // not recording.
  void record(InterpState& s, const crb::InstrCell& cell) {
    if (!recording_) return;
    if (fragment_.instructions.size() >= kMaxTraceLength) {
      fragment_.exit_reason = ExitReason::MaxLength;
      recording_ = false;
      return;
    }
    fragment_.instructions.push_back(TraceEntry{
        cell, s.current().pc,
        static_cast<std::uint32_t>(s.frames.size())});
  }

  // Mark a side exit. Called by branch handlers when the branch is taken
  // (or not taken, ending the trace).
  void mark_exit(ExitReason reason, std::uint32_t pc, std::uint32_t depth) {
    if (!recording_) return;
    fragment_.exit_reason = reason;
    fragment_.exit_pc = pc;
    fragment_.exit_frame_depth = depth;
    recording_ = false;
  }

  // Mark a loop close (backedge to the trace head).
  void mark_loop_close(std::uint32_t head_pc) {
    if (!recording_) return;
    fragment_.exit_reason = ExitReason::LoopClose;
    fragment_.loop_head_pc = head_pc;
    recording_ = false;
  }

  // ---- Queries ---------------------------------------------------------
  bool is_recording() const noexcept { return recording_; }
  const TraceFragment& fragment() const noexcept { return fragment_; }

 private:
  bool recording_{false};
  TraceFragment fragment_{};
};

// ---- Pretty-print a trace fragment (for debugging) ---------------------
void print_trace(const TraceFragment& frag);

}  // namespace dvm
