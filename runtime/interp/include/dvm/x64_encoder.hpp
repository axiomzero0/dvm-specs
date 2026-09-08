// dvm/x64_encoder.hpp — Minimal x86-64 instruction encoder.
//
// Encodes a subset of x86-64 instructions sufficient for the DVM trace
// compiler's native code generation:
//   - mov reg, imm64       (load a 64-bit constant)
//   - mov reg, reg          (register-to-register copy)
//   - add reg, reg          (integer addition)
//   - sub reg, reg          (integer subtraction)
//   - cmp reg, reg          (compare two registers)
//   - setl reg8             (set byte if less-than, signed)
//   - sete reg8             (set byte if equal)
//   - test reg, reg         (test a register for zero)
//   - jnz rel32             (jump if not zero, near relative)
//   - jz rel32              (jump if zero, near relative)
//   - jmp rel32             (unconditional near relative jump)
//   - ret                   (return from function)
//   - push reg               (push onto stack)
//   - pop reg                (pop from stack)
//   - ud2                   (undefined instruction — trap/deopt)
//
#pragma once

#include <cstdint>
#include "dvm/jit_buffer.hpp"

namespace dvm {

// x86-64 general-purpose registers (64-bit).
enum class Reg : std::uint8_t {
  RAX = 0, RCX = 1, RDX = 2, RBX = 3,
  RSP = 4, RBP = 5, RSI = 6, RDI = 7,
  R8  = 8, R9  = 9, R10 = 10, R11 = 11,
  R12 = 12, R13 = 13, R14 = 14, R15 = 15,
};

// ---- Encoder helpers ----------------------------------------------------
// REX prefix: 0x48 for 64-bit operand size, plus R/B bits for extended regs.
inline std::uint8_t rex(bool w, bool r, bool x, bool b) {
  return static_cast<std::uint8_t>(0x40 | (w ? 8 : 0) | (r ? 4 : 0) | (x ? 2 : 0) | (b ? 1 : 0));
}

// ModRM byte: mod=11 (register-direct), reg, rm.
inline std::uint8_t modrm(std::uint8_t reg, std::uint8_t rm) {
  return static_cast<std::uint8_t>(0xC0 | ((reg & 7) << 3) | (rm & 7));
}

// ---- Instruction encoders -------------------------------------------------

// mov reg, imm64 — REX.W + 0xB8+rxb + imm64
inline void encode_mov_imm64(JitBuffer& buf, Reg dst, std::int64_t imm) {
  buf.emit_byte(rex(true, false, false, static_cast<std::uint8_t>(dst) >= 8));
  buf.emit_byte(0xB8 + (static_cast<std::uint8_t>(dst) & 7));
  buf.emit_u64(static_cast<std::uint64_t>(imm));
}

// mov reg, reg — REX.W + 0x89 + ModRM
inline void encode_mov_reg(JitBuffer& buf, Reg dst, Reg src) {
  buf.emit_byte(rex(true,
                     static_cast<std::uint8_t>(src) >= 8,
                     false,
                     static_cast<std::uint8_t>(dst) >= 8));
  buf.emit_byte(0x89);
  buf.emit_byte(modrm(static_cast<std::uint8_t>(src), static_cast<std::uint8_t>(dst)));
}

// add reg, reg — REX.W + 0x01 + ModRM
inline void encode_add_reg(JitBuffer& buf, Reg dst, Reg src) {
  buf.emit_byte(rex(true,
                     static_cast<std::uint8_t>(src) >= 8,
                     false,
                     static_cast<std::uint8_t>(dst) >= 8));
  buf.emit_byte(0x01);
  buf.emit_byte(modrm(static_cast<std::uint8_t>(src), static_cast<std::uint8_t>(dst)));
}

// sub reg, reg — REX.W + 0x29 + ModRM
inline void encode_sub_reg(JitBuffer& buf, Reg dst, Reg src) {
  buf.emit_byte(rex(true,
                     static_cast<std::uint8_t>(src) >= 8,
                     false,
                     static_cast<std::uint8_t>(dst) >= 8));
  buf.emit_byte(0x29);
  buf.emit_byte(modrm(static_cast<std::uint8_t>(src), static_cast<std::uint8_t>(dst)));
}

// cmp reg, reg — REX.W + 0x39 + ModRM
inline void encode_cmp_reg(JitBuffer& buf, Reg lhs, Reg rhs) {
  buf.emit_byte(rex(true,
                     static_cast<std::uint8_t>(rhs) >= 8,
                     false,
                     static_cast<std::uint8_t>(lhs) >= 8));
  buf.emit_byte(0x39);
  buf.emit_byte(modrm(static_cast<std::uint8_t>(rhs), static_cast<std::uint8_t>(lhs)));
}

// setl r8 — 0x0F 0x9C + ModRM(11, reg8) (sets a byte to 1 if SF != OF)
inline void encode_setl(JitBuffer& buf, Reg dst) {
  buf.emit_byte(rex(false, false, false, static_cast<std::uint8_t>(dst) >= 8));
  buf.emit_byte(0x0F);
  buf.emit_byte(0x9C);
  buf.emit_byte(modrm(0, static_cast<std::uint8_t>(dst)));
}

// sete r8 — 0x0F 0x94 + ModRM
inline void encode_sete(JitBuffer& buf, Reg dst) {
  buf.emit_byte(rex(false, false, false, static_cast<std::uint8_t>(dst) >= 8));
  buf.emit_byte(0x0F);
  buf.emit_byte(0x94);
  buf.emit_byte(modrm(0, static_cast<std::uint8_t>(dst)));
}

// setne r8 — 0x0F 0x95 + ModRM
inline void encode_setne(JitBuffer& buf, Reg dst) {
  buf.emit_byte(rex(false, false, false, static_cast<std::uint8_t>(dst) >= 8));
  buf.emit_byte(0x0F);
  buf.emit_byte(0x95);
  buf.emit_byte(modrm(0, static_cast<std::uint8_t>(dst)));
}

// test reg, reg — REX.W + 0x85 + ModRM
inline void encode_test_reg(JitBuffer& buf, Reg r1, Reg r2) {
  buf.emit_byte(rex(true,
                     static_cast<std::uint8_t>(r2) >= 8,
                     false,
                     static_cast<std::uint8_t>(r1) >= 8));
  buf.emit_byte(0x85);
  buf.emit_byte(modrm(static_cast<std::uint8_t>(r2), static_cast<std::uint8_t>(r1)));
}

// jnz rel32 — 0x0F 0x85 + rel32 (jump if not zero / not equal)
// Returns the offset of the rel32 field for patching.
inline std::size_t encode_jnz(JitBuffer& buf) {
  buf.emit_byte(0x0F);
  buf.emit_byte(0x85);
  std::size_t patch_offset = buf.offset();
  buf.emit_u32(0);  // placeholder, patched later
  return patch_offset;
}

// jz rel32 — 0x0F 0x84 + rel32
inline std::size_t encode_jz(JitBuffer& buf) {
  buf.emit_byte(0x0F);
  buf.emit_byte(0x84);
  std::size_t patch_offset = buf.offset();
  buf.emit_u32(0);
  return patch_offset;
}

// jmp rel32 — 0xE9 + rel32
inline std::size_t encode_jmp(JitBuffer& buf) {
  buf.emit_byte(0xE9);
  std::size_t patch_offset = buf.offset();
  buf.emit_u32(0);
  return patch_offset;
}

// ret — 0xC3
inline void encode_ret(JitBuffer& buf) {
  buf.emit_byte(0xC3);
}

// push reg — 0x50+rxb
inline void encode_push(JitBuffer& buf, Reg r) {
  buf.emit_byte(rex(false, false, false, static_cast<std::uint8_t>(r) >= 8));
  buf.emit_byte(0x50 + (static_cast<std::uint8_t>(r) & 7));
}

// pop reg — 0x58+rxb
inline void encode_pop(JitBuffer& buf, Reg r) {
  buf.emit_byte(rex(false, false, false, static_cast<std::uint8_t>(r) >= 8));
  buf.emit_byte(0x58 + (static_cast<std::uint8_t>(r) & 7));
}

// ud2 — 0x0F 0x0B (undefined instruction, used for deopt/trap)
inline void encode_ud2(JitBuffer& buf) {
  buf.emit_byte(0x0F);
  buf.emit_byte(0x0B);
}

}  // namespace dvm
