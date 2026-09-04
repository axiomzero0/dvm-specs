// src/interp.cpp — Main interpreter dispatch loop with computed goto.
//
// Spec citation: DVM-CRB.md §4 (Execution Model), §29 (Interpreter
// Implementation Notes — direct threading).
//
// This file uses GCC computed goto (labels-as-values) for the dispatch
// loop. Computed goto is the standard interpreter dispatch idiom and
// the user explicitly required it. It is a GNU extension flagged by
// -Wpedantic; the Makefile adds -Wno-pedantic for this file only.
//
// The dispatch table has 65536 entries (one per possible 16-bit opcode).
// Most entries point to the `op_unknown` label. Known opcodes point to
// their handler labels. Each handler calls the appropriate function
// from opcodes_*.cpp, checks the result, and either dispatches the next
// instruction or returns.
//
#include "dvm/interp.hpp"
#include "dvm/opcodes.hpp"
#include "dvm/opcodes_def.hpp"
#include "dvm/trace.hpp"
#include "dvm/hotness.hpp"

namespace dvm {

namespace {

// The dispatch table: 65536 entries, each a void* (label address).
// Initialized once on first use.
struct DispatchTable {
  void* entries[65536];
  DispatchTable() noexcept {
    for (int i = 0; i < 65536; ++i) entries[i] = nullptr;
    // Will be filled in by init_dispatch() below.
  }
};

}  // namespace

Value interpret(const crb::Module& module, std::uint32_t entry_function_id) {
  return interpret(module, entry_function_id, nullptr, nullptr);
}

Value interpret(const crb::Module& module, std::uint32_t entry_function_id,
                 TraceRecorder* recorder) {
  return interpret(module, entry_function_id, recorder, nullptr);
}

Value interpret(const crb::Module& module, std::uint32_t entry_function_id,
                 TraceRecorder* recorder, HotnessTracker* hotness) {
  // Find the entry function.
  const crb::FunctionEntry* fn = module.find_function(entry_function_id);
  if (!fn) return Value::null();

  // Set up interpreter state.
  InterpState s;
  s.module = &module;
  s.push_frame(*fn);
  s.current_function_id = entry_function_id;

  // ---- Trace recorder + hotness setup -----------------------------------
  // If a recorder is provided (non-null) AND no hotness tracker is
  // provided, start recording at the entry function's PC=0 immediately.
  //
  // If a hotness tracker IS provided, do NOT start recording yet — the
  // hotness tracker will auto-start the recorder when a loop header's
  // backedge count exceeds the threshold (triggered from branch handlers).
  if (recorder && !hotness) {
    s.recorder = recorder;
    recorder->start(entry_function_id, 0, s.current().registers);
  }
  if (recorder && hotness) {
    s.recorder = recorder;
  }
  if (hotness) {
    s.hotness = hotness;
  }

  // ---- Dispatch table (computed goto via GCC labels-as-values) ----------
  // Each label is a dispatch target. The dispatch table maps opcode
  // values to label addresses. We use a local static to initialize once.
  //
  // NOTE: This uses the GCC computed-goto extension. The dispatch
  // table must be initialized AFTER the labels are defined (because
  // label addresses are only valid within the function). We do this
  // by initializing on first call via a lambda.

  static void* dispatch[65536] = {};
  static bool dispatch_init = false;
  if (!dispatch_init) {
    for (int i = 0; i < 65536; ++i) dispatch[i] = &&op_unknown;
    dispatch[crb::op::NOP]              = &&op_nop;
    dispatch[crb::op::TRAP]            = &&op_trap;
    dispatch[crb::op::UNREACHABLE]      = &&op_unreachable;
    dispatch[crb::op::SAFEPOINT]        = &&op_safepoint;
    dispatch[crb::op::RET]              = &&op_ret;
    dispatch[crb::op::RET_VOID]        = &&op_ret_void;
    dispatch[crb::op::MOV_CONST]       = &&op_mov_const;
    dispatch[crb::op::MOV_NULL]        = &&op_mov_null;
    dispatch[crb::op::MOV_TRUE]        = &&op_mov_true;
    dispatch[crb::op::MOV_FALSE]       = &&op_mov_false;
    dispatch[crb::op::MOV_UNDEF]       = &&op_mov_undef;
    dispatch[crb::op::ADD_I64_WRAP]    = &&op_add_i64_wrap;
    dispatch[crb::op::SUB_I64_WRAP]    = &&op_sub_i64_wrap;
    dispatch[crb::op::MUL_I64_WRAP]    = &&op_mul_i64_wrap;
    dispatch[crb::op::NEG_I64_WRAP]    = &&op_neg_i64_wrap;
    dispatch[crb::op::ADD_I64_CHECKED] = &&op_add_i64_checked;
    dispatch[crb::op::CMP_EQ]          = &&op_cmp_eq;
    dispatch[crb::op::CMP_NE]          = &&op_cmp_ne;
    dispatch[crb::op::CMP_LT_S]        = &&op_cmp_lt_s;
    dispatch[crb::op::CMP_LE_S]        = &&op_cmp_le_s;
    dispatch[crb::op::CMP_GT_S]        = &&op_cmp_gt_s;
    dispatch[crb::op::CMP_GE_S]        = &&op_cmp_ge_s;
    dispatch[crb::op::FADD_F64]       = &&op_fadd_f64;
    dispatch[crb::op::FSUB_F64]       = &&op_fsub_f64;
    dispatch[crb::op::FMUL_F64]       = &&op_fmul_f64;
    dispatch[crb::op::FDIV_F64]       = &&op_fdiv_f64;
    dispatch[crb::op::I64_TO_F64]     = &&op_i64_to_f64;
    dispatch[crb::op::F64_TO_I64_WRAP] = &&op_f64_to_i64_wrap;
    dispatch[crb::op::ALLOC]           = &&op_alloc;
    dispatch[crb::op::OBJ_GET]        = &&op_obj_get;
    dispatch[crb::op::OBJ_SET]        = &&op_obj_set;
    dispatch[crb::op::JMP]              = &&op_jmp;
    dispatch[crb::op::BR_TRUE]         = &&op_br_true;
    dispatch[crb::op::BR_FALSE]        = &&op_br_false;
    dispatch[crb::op::BR_NULL]         = &&op_br_null;
    dispatch[crb::op::BR_NONNULL]      = &&op_br_nonnull;
    dispatch[crb::op::CALL_DIRECT]     = &&op_call_direct;
    dispatch[crb::op::THROW]           = &&op_throw;
    dispatch_init = true;
  }

  // ---- Main dispatch loop -----------------------------------------------
  // Fetch the current instruction's opcode, record it (if recording),
  // then jump to the handler label. Each handler calls the appropriate
  // function, checks the result, and either dispatches the next
  // instruction or returns.

#define DISPATCH_NEXT() do {                                   \
    if (s.recorder && s.recorder->is_recording()) {            \
      s.recorder->record(s, s.fetch());                         \
    }                                                          \
    goto *dispatch[s.fetch().opcode()];                        \
  } while (0)

  DISPATCH_NEXT();

  // ---- §9 System opcodes --------------------------------------------------
op_nop: {
    OpResult r = op_nop(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_trap: {
    OpResult r = op_trap(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_unreachable: {
    OpResult r = op_unreachable(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_safepoint: {
    OpResult r = op_safepoint(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_ret: {
    OpResult r = op_ret(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_ret_void: {
    OpResult r = op_ret_void(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }

  // ---- §10 Move/constant opcodes -----------------------------------------
op_mov_const: {
    OpResult r = op_mov_const(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_mov_null: {
    OpResult r = op_mov_null(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_mov_true: {
    OpResult r = op_mov_true(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_mov_false: {
    OpResult r = op_mov_false(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_mov_undef: {
    OpResult r = op_mov_undef(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }

  // ---- §11 Integer arithmetic --------------------------------------------
op_add_i64_wrap: {
    OpResult r = op_add_i64_wrap(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_sub_i64_wrap: {
    OpResult r = op_sub_i64_wrap(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_mul_i64_wrap: {
    OpResult r = op_mul_i64_wrap(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_neg_i64_wrap: {
    OpResult r = op_neg_i64_wrap(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_add_i64_checked: {
    OpResult r = op_add_i64_checked(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }

  // ---- §14 Comparisons ---------------------------------------------------
op_cmp_eq: {
    OpResult r = op_cmp_eq(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_cmp_ne: {
    OpResult r = op_cmp_ne(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_cmp_lt_s: {
    OpResult r = op_cmp_lt_s(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_cmp_le_s: {
    OpResult r = op_cmp_le_s(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_cmp_gt_s: {
    OpResult r = op_cmp_gt_s(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_cmp_ge_s: {
    OpResult r = op_cmp_ge_s(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }

  // ---- §12 Floating-point arithmetic -------------------------------------
op_fadd_f64: {
    OpResult r = op_fadd_f64(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_fsub_f64: {
    OpResult r = op_fsub_f64(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_fmul_f64: {
    OpResult r = op_fmul_f64(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_fdiv_f64: {
    OpResult r = op_fdiv_f64(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }

  // ---- §13 Conversion ----------------------------------------------------
op_i64_to_f64: {
    OpResult r = op_i64_to_f64(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_f64_to_i64_wrap: {
    OpResult r = op_f64_to_i64_wrap(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }

  // ---- §19 Allocation ----------------------------------------------------
op_alloc: {
    OpResult r = op_alloc(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }

  // ---- §18 Object model --------------------------------------------------
op_obj_get: {
    OpResult r = op_obj_get(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_obj_set: {
    OpResult r = op_obj_set(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }

  // ---- §15 Control flow --------------------------------------------------
op_jmp: {
    OpResult r = op_jmp(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_br_true: {
    OpResult r = op_br_true(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_br_false: {
    OpResult r = op_br_false(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_br_null: {
    OpResult r = op_br_null(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }
op_br_nonnull: {
    OpResult r = op_br_nonnull(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }

  // ---- §16 Calls ----------------------------------------------------------
op_call_direct: {
    OpResult r = op_call_direct(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }

  // ---- §20 Exceptions -----------------------------------------------------
op_throw: {
    OpResult r = op_throw(s, s.fetch());
    if (r != OpResult::Continue) goto interp_exit;
    DISPATCH_NEXT();
  }

  // ---- Unknown opcode -----------------------------------------------------
op_unknown: {
    // An unknown or unimplemented opcode was encountered. This is a
    // fatal error in the minimal interpreter.
    s.exit_value = Value::null();
    goto interp_exit;
  }

interp_exit:
  // Free all heap-allocated objects before returning.
  s.free_heap();
  return s.exit_value;

#undef DISPATCH_NEXT
}

}  // namespace dvm
