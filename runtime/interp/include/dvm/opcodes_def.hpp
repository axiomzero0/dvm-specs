// dvm/opcodes_def.hpp — CRB opcode constants.
//
// Spec citation: DVM-CRB.md Sections 8–24 — the opcode space 0x0000–0x10FF.
//
// Each opcode is a 16-bit value. The opcode space is divided into groups
// (§8). This header defines the normative constants for every opcode
// the spec assigns a hex value to.
//
// Integer arithmetic opcodes are encoded via the formula
// (§11.4): opcode = 0x0200 + (variant * 0x10) + (width * 0x04) + mode
//
#pragma once

#include <cstdint>

namespace dvm::crb::op {

// ---- §9  System / control opcodes (0x0000–0x00FF) -----------------------
constexpr std::uint16_t NOP             = 0x0000;
constexpr std::uint16_t TRAP            = 0x0001;
constexpr std::uint16_t UNREACHABLE     = 0x0002;
constexpr std::uint16_t SAFEPOINT       = 0x0003;
constexpr std::uint16_t PROFILE_POINT   = 0x0004;
constexpr std::uint16_t DEBUG_PROBE     = 0x0005;

// ---- §10 Move / constant opcodes (0x0100–0x01FF) -----------------------
constexpr std::uint16_t MOV_CONST       = 0x0102;
constexpr std::uint16_t MOV_NULL        = 0x0103;
constexpr std::uint16_t MOV_TRUE        = 0x0104;
constexpr std::uint16_t MOV_FALSE       = 0x0105;
constexpr std::uint16_t MOV_UNDEF       = 0x0106;

// ---- §11 Integer arithmetic (0x0200–0x03FF) — encoded by formula -------
constexpr std::uint16_t int_arith(std::uint16_t variant,
                                    std::uint16_t width,
                                    std::uint16_t mode) noexcept {
  return static_cast<std::uint16_t>(0x0200u
      + (variant * 0x10u) + (width * 0x04u) + mode);
}
constexpr std::uint16_t V_ADD=0, V_SUB=1, V_MUL=2, V_DIV_S=3, V_DIV_U=4,
                         V_REM_S=5, V_REM_U=6, V_NEG=7, V_NOT=8, V_AND=9,
                         V_OR=10, V_XOR=11, V_SHL=12, V_SHR_S=13, V_SHR_U=14;
constexpr std::uint16_t W_I8=0, W_I16=1, W_I32=2, W_I64=3;
constexpr std::uint16_t M_WRAP=0, M_CHECKED=1, M_ASSUME=2, M_SAT=3;

constexpr std::uint16_t ADD_I64_WRAP      = int_arith(V_ADD,  W_I64, M_WRAP);
constexpr std::uint16_t ADD_I64_CHECKED  = int_arith(V_ADD,  W_I64, M_CHECKED);
constexpr std::uint16_t SUB_I64_WRAP      = int_arith(V_SUB,  W_I64, M_WRAP);
constexpr std::uint16_t MUL_I64_WRAP      = int_arith(V_MUL,  W_I64, M_WRAP);
constexpr std::uint16_t NEG_I64_WRAP      = int_arith(V_NEG,  W_I64, M_WRAP);

// ---- §12 Floating-point arithmetic (0x0400–0x04FF) ----------------------
constexpr std::uint16_t FADD_F64         = 0x0400;
constexpr std::uint16_t FSUB_F64         = 0x0401;
constexpr std::uint16_t FMUL_F64         = 0x0402;
constexpr std::uint16_t FDIV_F64         = 0x0403;

// ---- §13 Conversion opcodes (0x0500–0x05FF) ----------------------------
constexpr std::uint16_t I64_TO_F64       = 0x0500;
constexpr std::uint16_t F64_TO_I64_WRAP  = 0x0501;

// ---- §14 Comparison opcodes (0x0600–0x06FF) ----------------------------
constexpr std::uint16_t CMP_EQ           = 0x0600;
constexpr std::uint16_t CMP_NE           = 0x0601;
constexpr std::uint16_t CMP_LT_S         = 0x0602;
constexpr std::uint16_t CMP_LT_U         = 0x0603;
constexpr std::uint16_t CMP_LE_S         = 0x0604;
constexpr std::uint16_t CMP_LE_U         = 0x0605;
constexpr std::uint16_t CMP_GT_S         = 0x0606;
constexpr std::uint16_t CMP_GT_U         = 0x0607;
constexpr std::uint16_t CMP_GE_S         = 0x0608;
constexpr std::uint16_t CMP_GE_U         = 0x0609;

// ---- §15 Control flow (0x0700–0x07FF) -----------------------------------
constexpr std::uint16_t JMP              = 0x0700;
constexpr std::uint16_t BR_TRUE          = 0x0701;
constexpr std::uint16_t BR_FALSE         = 0x0702;
constexpr std::uint16_t BR_NULL          = 0x0703;
constexpr std::uint16_t BR_NONNULL       = 0x0704;
constexpr std::uint16_t SWITCH           = 0x0710;

// ---- §16 Calls / returns (0x0800–0x08FF) -------------------------------
constexpr std::uint16_t RET              = 0x0800;
constexpr std::uint16_t RET_VOID         = 0x0801;
constexpr std::uint16_t CALL_DIRECT      = 0x0810;
constexpr std::uint16_t CALL_INDIRECT    = 0x0811;
constexpr std::uint16_t CALL_INTRINSIC   = 0x0812;
constexpr std::uint16_t TAIL_CALL_DIRECT = 0x0820;
constexpr std::uint16_t TAIL_CALL_INDIRECT = 0x0821;

// ---- §17 Raw memory (0x0900–0x09FF) -------------------------------------
constexpr std::uint16_t LOAD_MEM         = 0x0900;
constexpr std::uint16_t STORE_MEM        = 0x0901;

// ---- §18 Object model (0x0A00–0x0AFF) ------------------------------------
constexpr std::uint16_t OBJ_GET          = 0x0A00;
constexpr std::uint16_t OBJ_SET          = 0x0A01;
constexpr std::uint16_t IDX_GET           = 0x0A10;
constexpr std::uint16_t IDX_SET           = 0x0A11;
constexpr std::uint16_t OBJ_CAST_CHECKED = 0x0A20;
constexpr std::uint16_t OBJ_IS_INSTANCE  = 0x0A21;
constexpr std::uint16_t CLOSURE_NEW      = 0x0A30;

// ---- §19 Allocation (0x0B00–0x0BFF) --------------------------------------
constexpr std::uint16_t ALLOC            = 0x0B00;
constexpr std::uint16_t ALLOC_ARRAY      = 0x0B01;

// ---- §20 Exceptions (0x0C00–0x0CFF) --------------------------------------
constexpr std::uint16_t THROW            = 0x0C00;

// ---- §21 Suspension (0x0D00–0x0DFF) -------------------------------------
constexpr std::uint16_t SUSPEND_YIELD    = 0x0D00;
constexpr std::uint16_t SUSPEND_AWAIT    = 0x0D01;
constexpr std::uint16_t SUSPEND_CLOSE    = 0x0D02;

// ---- §22 Atomics (0x0E00–0x0EFF) -----------------------------------------
constexpr std::uint16_t ATOMIC_LOAD      = 0x0E00;
constexpr std::uint16_t ATOMIC_STORE     = 0x0E01;
constexpr std::uint16_t ATOMIC_ADD       = 0x0E02;

// ---- §23 Vector (0x0F00–0x0FFF) -----------------------------------------
constexpr std::uint16_t V_LOAD_F32X4     = 0x0F00;

// ---- §24 Trace / debug (0x1000–0x10FF) ----------------------------------
constexpr std::uint16_t TRACE_PROMOTE_HINT  = 0x1000;
constexpr std::uint16_t TRACE_VIRTUAL_HINT  = 0x1001;
constexpr std::uint16_t DEBUG_BREAKPOINT    = 0x1002;
constexpr std::uint16_t MONITOR_EVENT       = 0x1003;

// ---- Sentinel ------------------------------------------------------------
constexpr std::uint16_t UNKNOWN = 0xFFFF;

}  // namespace dvm::crb::op
