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
  // Find the entry function.
  const crb::FunctionEntry* fn = module.find_function(entry_function_id);
  if (!fn) return Value::null();

  // Set up interpreter state.
  InterpState s;
  s.module = &module;
  s.push_frame(*fn);

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
    dispatch[crb::op::CMP_LT_S]        = &&op_cmp_lt_s;
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
  // Fetch the current instruction's opcode, jump to the handler label.
  // Each handler calls the appropriate function, checks the result,
  // and either dispatches the next instruction or returns.

#define DISPATCH_NEXT() \
  goto *dispatch[s.fetch().opcode()]

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
op_cmp_lt_s: {
    OpResult r = op_cmp_lt_s(s, s.fetch());
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
  return s.exit_value;

#undef DISPATCH_NEXT
}

}  // namespace dvm
