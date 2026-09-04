// dvm/crb.hpp — CRB module structures (header, section table, instruction cell).
//
// Spec citation: DVM-CRB.md Sections 3 (Module Layout), 4 (Execution Model),
// 7 (Instruction Formats), 8 (Opcode Space).
//
// CRB is a fixed-width-64-bit-cell bytecode format. Each instruction cell
// is 8 bytes = 4 × 16-bit slots. The opcode occupies slot 0; slots 1–3
// hold operands (register indices, immediates split across two 16-bit
// halves, or branch offsets).
//
// Format R_R_IMM32 (§7.12) occupies TWO consecutive 64-bit cells:
//   cell 0: opcode | dst | src | unused
//   cell 1: imm32_low | imm32_high | unused | unused
// This preserves the "fixed-width 64-bit instruction cell" property
// while accommodating the 32-bit immediate payload that does not fit
// in a single 4-slot cell.
//
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace dvm::crb {

// ---- Instruction cell (64 bits, 4 × 16-bit slots) -----------------------
// Stored as a single uint64_t; slots are little-endian within the cell.
// Slot 0: opcode (16-bit)
// Slots 1–3: operand slots (register indices, immediate halves, etc.)
//
// We use uint16_t array indexing to make slot access explicit. The
// little-endian layout means slot[i] is at byte offset i*2 within the
// 8-byte cell.
struct alignas(8) InstrCell {
  std::uint16_t slot0;  // opcode
  std::uint16_t slot1;
  std::uint16_t slot2;
  std::uint16_t slot3;

  constexpr std::uint16_t opcode() const noexcept { return slot0; }
  constexpr std::uint16_t s1() const noexcept { return slot1; }
  constexpr std::uint16_t s2() const noexcept { return slot2; }
  constexpr std::uint16_t s3() const noexcept { return slot3; }

  // 32-bit immediate reconstructed from two 16-bit slots (little-endian).
  static constexpr std::uint32_t imm32(std::uint16_t low,
                                        std::uint16_t high) noexcept {
    return (static_cast<std::uint32_t>(high) << 16) |
           static_cast<std::uint32_t>(low);
  }
};
static_assert(sizeof(InstrCell) == 8, "InstrCell must be 8 bytes");

// ---- CRB module header (§3.1) -------------------------------------------
// 88 bytes with natural C++ alignment. Magic is 'CRB1' little-endian
// (0x31425243 = bytes 0x43, 0x52, 0x42, 0x31).
struct alignas(8) ModuleHeader {
  std::uint32_t magic;                  // 0x31425243 ("CRB1" LE)
  std::uint16_t version_major;          // 1
  std::uint16_t version_minor;          // 0
  std::uint32_t flags;                   // §3.1 Header flags
  std::uint64_t guest_profile_hash_lo;
  std::uint64_t guest_profile_hash_hi;
  std::uint32_t dvm_abi_version;
  std::uint32_t extension_count;
  std::uint64_t extension_table_offset;
  std::uint32_t section_count;
  std::uint32_t _pad0;                   // align section_table_offset to 8
  std::uint64_t section_table_offset;
  std::uint32_t header_crc32;
  std::uint32_t module_crc32;
  std::uint32_t reserved_0;
  std::uint32_t _pad1;                   // align reserved_1 to 8
  std::uint64_t reserved_1;
};
static_assert(sizeof(ModuleHeader) == 88, "ModuleHeader must be 88 bytes per CRB §3.1");

// The magic value: 'CRB1' as a little-endian uint32_t.
constexpr std::uint32_t kMagicValue = 0x31425243u;

// ---- Section table entry (§3.2) -----------------------------------------
struct alignas(4) SectionEntry {
  std::uint32_t type;     // §3.2 Section types
  std::uint32_t offset;   // byte offset into the module
  std::uint32_t size;     // byte size
  std::uint32_t reserved; // must be 0
};
static_assert(sizeof(SectionEntry) == 16, "SectionEntry must be 16 bytes");

// ---- Section types (§3.2) -----------------------------------------------
enum class SectionType : std::uint32_t {
  Code         = 1,
  ConstantPool = 2,
  StringPool   = 3,
  FunctionTable = 4,
  BlockTable    = 5,
  CallSiteTable = 6,
  AccessSiteTable = 7,
  SwitchTable    = 8,
  TypeTable      = 9,
  RegisterTypes  = 10,
  ClosureDescriptors = 11,
  SiteTable          = 12,  // memory site descriptors (§18.3)
  SuspensionDescriptors = 13,
  TraceHints     = 14,
  DebugInfo      = 15,
  SourceMap      = 16,
  ModuleMetadata = 17,
};

// ---- Function table entry (§13.1) ---------------------------------------
struct alignas(4) FunctionEntry {
  std::uint32_t function_id;
  std::uint32_t name_string_id;  // index into StringPool
  std::uint32_t code_offset;      // byte offset into Code section
  std::uint32_t code_length;     // byte length
  std::uint16_t param_count;
  std::uint16_t register_count;  // total registers this function uses
  std::uint16_t return_count;    // 0 or 1
  std::uint16_t flags;
};
static_assert(sizeof(FunctionEntry) == 24, "FunctionEntry must be 24 bytes");

// ---- Constant pool entry (§6) ------------------------------------------
// 24 bytes, fixed layout.
struct alignas(8) ConstantEntry {
  std::uint32_t kind;       // §6 CRBConstantKind
  std::uint32_t flags;
  std::uint64_t payload_lo;
  std::uint64_t payload_hi;
};
static_assert(sizeof(ConstantEntry) == 24, "ConstantEntry must be 24 bytes");

// ---- Constant kinds (§6) -------------------------------------------------
enum class ConstantKind : std::uint32_t {
  Null       = 0,
  Bool       = 1,
  I8         = 2,
  I16        = 3,
  I32        = 4,
  I64        = 5,
  U8         = 6,
  U16        = 7,
  U32        = 8,
  U64        = 9,
  F32        = 10,
  F64        = 11,
  Symbol     = 12,
  String     = 13,
  Bytes      = 14,
  TypeId     = 15,
  FunctionId = 16,
  ObjectHandle = 17,
  VectorDesc   = 18,
};

// ---- Module: parsed view over a CRB binary ------------------------------
// A Module holds a pointer to the raw binary plus parsed views of each
// section. The loader (loader.hpp) constructs a Module from raw bytes.
struct Module {
  std::span<const std::byte> raw;        // the whole binary
  ModuleHeader               header{};
  std::span<const SectionEntry> sections;
  // Section views (set by the loader; empty if the section is absent):
  std::span<const InstrCell>      code;
  std::span<const FunctionEntry>  functions;
  std::span<const ConstantEntry>  constants;
  std::span<const std::byte>      constant_pool;
  std::span<const std::byte>      string_pool;

  // Lookup a function by ID. Returns nullptr if not found.
  const FunctionEntry* find_function(std::uint32_t id) const noexcept;

  // Lookup a function's code as a span of InstrCells.
  std::span<const InstrCell> function_code(const FunctionEntry& fn) const noexcept;
};

// ---- Magic / version helpers --------------------------------------------
constexpr std::uint16_t kVersionMajor = 1;
constexpr std::uint16_t kVersionMinor = 0;

}  // namespace dvm::crb
