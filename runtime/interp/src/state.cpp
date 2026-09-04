// src/state.cpp — InterpState frame push/pop implementation.
//
#include "dvm/state.hpp"

namespace dvm {

// free_heap() is implemented in opcodes_object.cpp where ObjStorage is
// fully defined (needed for `delete` to call the destructor).

void InterpState::push_frame(const crb::FunctionEntry& fn) {
  Frame f;
  f.function = &fn;
  if (module) {
    f.code = module->function_code(fn);
  }
  f.registers.assign(fn.register_count, Value{});
  if (!frames.empty()) {
    // Save the caller's resume state.
    Frame& caller = frames.back();
    f.return_pc = caller.pc;
    f.caller_function = caller.function;
    f.caller_code = caller.code;
  }
  frames.push_back(std::move(f));
}

Frame* InterpState::pop_frame() {
  if (frames.empty()) {
    exited = true;
    return nullptr;
  }
  Frame popped = std::move(frames.back());
  frames.pop_back();
  if (frames.empty()) {
    // Outermost frame returning: the interpreter is done.
    exited = true;
    return nullptr;
  }
  // Restore caller's PC. We do NOT restore caller's code because the
  // caller's frame already holds its own code; we just resume at
  // return_pc in the caller's code.
  Frame& caller = frames.back();
  caller.pc = popped.return_pc;
  return &caller;
}

}  // namespace dvm
