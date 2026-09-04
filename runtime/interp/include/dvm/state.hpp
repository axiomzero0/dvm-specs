// dvm/state.hpp — Interpreter state.
//
// Spec citation: DVM-CRB.md Section 4 "Execution Model" (Program Counter,
// Instruction Cell, Register Space, Function Entry Convention).
//
// The interpreter state holds the per-execution registers, the program
// counter (a pointer into the code span), the call frame stack, and the
// currently-executing function entry. Each CALL pushes a frame; each RET
// pops one.
//
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "dvm/crb.hpp"
#include "dvm/value.hpp"

namespace dvm {

// ---- Forward declaration for the object storage ------------------------
struct ObjStorage;

// ---- Forward declaration for the trace recorder ------------------------
class TraceRecorder;

// ---- Forward declaration for the hotness tracker -----------------------
class HotnessTracker;

// ---- Call frame ----------------------------------------------------------
// A frame holds the function being executed, the register file, and the
// return PC (where to resume after RET). The register file is per-frame
// because CRB functions have their own register spaces (§4.3).
struct Frame {
  const crb::FunctionEntry* function{nullptr};
  std::span<const crb::InstrCell> code;       // this function's code
  std::uint32_t pc{0};                        // instruction index into code
  std::vector<Value> registers;               // size = function->register_count
  std::uint32_t return_pc{0};                  // caller's PC (in caller's code)
  const crb::FunctionEntry* caller_function{nullptr};
  std::span<const crb::InstrCell> caller_code{};
  std::uint16_t caller_ret_reg{0};             // where to put the return value
};

// ---- Interpreter state ---------------------------------------------------
struct InterpState {
  // The module being executed (set by interpret()).
  const crb::Module* module{nullptr};

  // Frame stack. The top of stack is `frames.back()`.
  std::vector<Frame> frames;

  // Pending exception (set by THROW; cleared by handler). Null-tagged
  // Value means no pending exception.
  Value pending_exception{};

  // Exit flag. Set by RET from the outermost frame or by TRAP.
  bool exited{false};

  // Exit value (the value returned from the outermost frame, or the
  // exception value if the interpreter exited via uncaught exception).
  Value exit_value{};

  // Heap: tracks all ObjStorage allocations so they can be freed after
  // execution. A real GC would manage this; the minimal interpreter just
  // frees everything at the end.
  std::vector<ObjStorage*> heap;

  // Free all heap-allocated objects. Called by interpret() after the
  // dispatch loop exits.
  void free_heap() noexcept;

  // ---- Trace recording --------------------------------------------------
  // Optional trace recorder. When non-null and recording, the interpreter
  // calls record() on every executed instruction and branch handlers
  // call mark_exit() / mark_loop_close().
  TraceRecorder* recorder{nullptr};

  // Optional hotness tracker. When non-null, backward branches are
  // counted; when a counter reaches the threshold, the tracker signals
  // that recording should start at the branch target (the loop header).
  HotnessTracker* hotness{nullptr};

  // The function ID of the currently executing function (for the
  // hotness tracker to pass to recorder->start()).
  std::uint32_t current_function_id{0};

  // ---- Frame helpers -----------------------------------------------------
  // Push a new frame for `fn`. The frame's register file is sized to
  // fn.register_count and zero-initialized (registers start as Null).
  void push_frame(const crb::FunctionEntry& fn);

  // Pop the top frame. Sets `exited` if this was the outermost frame.
  // Returns the caller's frame (or nullptr if exited).
  Frame* pop_frame();

  // Return the current (top) frame.
  Frame& current() { return frames.back(); }
  const Frame& current() const { return frames.back(); }

  // Read/write a register in the current frame.
  Value& reg(std::uint16_t idx) { return current().registers[idx]; }
  const Value& reg(std::uint16_t idx) const { return current().registers[idx]; }

  // ---- PC helpers --------------------------------------------------------
  // Fetch the current instruction cell.
  const crb::InstrCell& fetch() const {
    return current().code[current().pc];
  }
  // Advance PC by N cells.
  void advance(std::uint32_t n = 1) { current().pc += n; }

  // Branch by a signed delta (in instruction cells, not bytes).
  // delta is the 32-bit immediate from BR_TRUE/BR_FALSE/JMP, interpreted
  // as a signed two's-complement integer.
  void branch(std::int32_t delta_cells) {
    const auto cur = static_cast<std::int64_t>(current().pc);
    const auto next = cur + delta_cells;
    current().pc = static_cast<std::uint32_t>(next);
  }
};

}  // namespace dvm
