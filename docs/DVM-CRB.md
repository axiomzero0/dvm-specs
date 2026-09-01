# CRB — Common Register Bytecode

**CRB** is the standard, language-neutral, register-based bytecode consumed by the DVM interpreter and used as the canonical lower-tier execution state for deoptimization, tracing, and DGW lifting.

This is not the optimizing IR.  
This is the **stable semantic bytecode**.

---

# CRB v1.0 Standard

**Status:** Draft Standard  
**Owner:** DVM Bytecode Working Group  
**Related Subsystems:** DVM Interpreter, DGW/Trace Lifter, Verifier, PGO, Deopt, AOT Loader  
**Encoding:** Little-endian, fixed-width 64-bit instruction cells  
**Primary Use:** Tier 0 register interpreter, trace recording baseline, deopt target

---

# 1. Purpose of CRB

CRB exists to give DVM a single portable execution format that can represent many guest languages while remaining:

- fast to interpret
- easy to verify
- easy to profile
- easy to meta-trace
- easy to lift into DGW/Trace
- precise enough for deoptimization
- expressive enough for dynamic languages
- typed enough for static languages
- safe for untrusted modules

CRB is the reference state for Tier 0.

When JIT code deoptimizes, execution must be reconstructible as if CRB were being executed directly.

---

# 2. Core Design Principles

## 2.1 Register-based, not stack-based

CRB uses virtual registers.

This reduces interpreter traffic, simplifies inline caches, and makes OSR/deopt mapping cleaner.

---

## 2.2 Fixed-width instruction cells

CRB uses 64-bit instruction words.

This allows:

- simple fetch
- simple direct threading
- predictable decoding
- large opcode space
- large register space
- 32-bit operand payloads without variable-length decoding

---

## 2.3 Language-neutral but guest-profile-aware

CRB does not hard-code any guest language semantics.

Guest semantics are supplied by:

- Guest Language Profile
- type descriptors
- object model descriptors
- exception mapping
- numeric semantics
- capability flags

---

## 2.4 Typed where useful, dynamic where necessary

CRB supports:

- typed primitive operations
- dynamic `Any` operations
- object/member/index access operations
- typed memory operations for systems-style guests
- optional vector and atomic operations

---

## 2.5 Verifier-first

No CRB module may be executed unless it passes verification.

Invalid modules are rejected.

CRB does not rely on runtime “best effort” validation.

---

## 2.6 Trace-friendly

Every CRB opcode must have:

- a normal interpreter handler
- a trace classification
- an effect class
- a FrameState policy
- a guest projection mapping

This allows meta-tracing and normal tracing to lift CRB execution into DGW/Trace.

---

# 3. Module Layout

A CRB module is a binary container.

```text
CRB Module
├── Header
├── Extension Table
├── String Table
├── Symbol Table
├── Type Table
├── Constant Pool
├── Function Table
├── Code Section
├── Call Site Table
├── Access Site Table
├── Exception Table
├── Block Table
├── Source Map
├── Register Map
├── Dependency Table
├── Trace Hint Table
├── Debug Section
├── Static Proof Section
├── Signature Section
```

Only the header, function table, code section, constant pool, and required metadata sections are mandatory.

---

## 3.1 CRB Header

```cpp
struct CRBHeader {
    uint32_t magic;              // 'CRB1' little-endian: 0x31425243
    uint16_t version_major;      // 1
    uint16_t version_minor;      // 0
    uint32_t flags;
    uint64_t guest_profile_hash_lo;
    uint64_t guest_profile_hash_hi;
    uint32_t dvm_abi_version;
    uint32_t extension_count;
    uint64_t extension_table_offset;
    uint32_t section_count;
    uint64_t section_table_offset;
    uint32_t header_crc32;
    uint32_t module_crc32;
    uint32_t reserved_0;
    uint64_t reserved_1;
};
```

### Header flags

```cpp
enum CRBModuleFlags : uint32_t {
    CRB_MODULE_THREAD_SAFE          = 1 << 0,
    CRB_MODULE_HAS_DYNAMIC_FEATURES = 1 << 1,
    CRB_MODULE_HAS_SUSPENSION       = 1 << 2,
    CRB_MODULE_HAS_RAW_MEMORY       = 1 << 3,
    CRB_MODULE_HAS_FFI              = 1 << 4,
    CRB_MODULE_STATIC_PROOFED       = 1 << 5,
    CRB_MODULE_AOT_COMPATIBLE       = 1 << 6,
    CRB_MODULE_DEBUG_INFO_PRESENT   = 1 << 7,
};
```

---

## 3.2 Section Table Entry

```cpp
struct CRBSectionEntry {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t size;
    uint64_t alignment;
};
```

### Section types

```cpp
enum CRBSectionType : uint32_t {
    CRB_SECTION_STRINGS,
    CRB_SECTION_SYMBOLS,
    CRB_SECTION_TYPES,
    CRB_SECTION_CONSTANTS,
    CRB_SECTION_FUNCTIONS,
    CRB_SECTION_CODE,
    CRB_SECTION_CALL_SITES,
    CRB_SECTION_ACCESS_SITES,
    CRB_SECTION_EXCEPTIONS,
    CRB_SECTION_BLOCKS,
    CRB_SECTION_SOURCE_MAP,
    CRB_SECTION_REGISTER_MAP,
    CRB_SECTION_DEPENDENCIES,
    CRB_SECTION_TRACE_HINTS,
    CRB_SECTION_DEBUG,
    CRB_SECTION_STATIC_PROOFS,
    CRB_SECTION_SIGNATURE,
};
```

---

# 4. Execution Model

CRB executes as a register virtual machine.

Each function has:

- a virtual register file
- a code array
- a block table
- an exception table
- call-site metadata
- access-site metadata
- register type metadata

The interpreter maintains:

- current function
- current instruction index
- register file
- exception state
- guest frame metadata
- profiling counters
- tracing state
- runtime tooling state

---

## 4.1 Program Counter

The CRB program counter, `pc`, is an **instruction index**, not a byte offset.

Because all instruction cells are 64-bit:

```text
byte_offset = pc * 8
```

Branch targets are expressed as signed instruction deltas relative to the next instruction:

```text
target_pc = current_pc + 1 + delta
```

All branch targets must be the start of a valid block.

---

## 4.2 Instruction Cell

```cpp
struct CRBInst {
    uint64_t bits;
};
```

Field extraction:

```cpp
uint16_t opcode = bits & 0xFFFF;
uint16_t op0    = (bits >> 16) & 0xFFFF;
uint16_t op1    = (bits >> 32) & 0xFFFF;
uint16_t op2    = (bits >> 48) & 0xFFFF;
```

---

## 4.3 Register Space

CRB v1.0 uses 16-bit register indices.

```text
0x0000 .. 0xFFFE : valid registers
0xFFFF           : CRB_REG_NONE
```

A function may declare up to 65535 virtual registers.

Registers are not implicitly initialized. The verifier must prove that no register is read before being written, unless the guest language profile explicitly allows an undefined initial value.

---

## 4.4 Function Entry Convention

At function entry:

- arguments are placed in `r0 .. rN-1`
- remaining registers are uninitialized unless explicitly declared otherwise
- return value is conventionally produced in a register specified by `RET`
- for single-return functions, `RET rX` returns `rX`
- for multi-return functions, the optional multi-return extension must be used

---

# 5. Type System

CRB has a minimal portable type system.

Guest languages may provide richer type descriptors, but CRB core understands these classes.

```cpp
enum CRBTypeKind : uint32_t {
    CRB_TYPE_VOID,
    CRB_TYPE_ANY,
    CRB_TYPE_BOOL,
    CRB_TYPE_I8,
    CRB_TYPE_I16,
    CRB_TYPE_I32,
    CRB_TYPE_I64,
    CRB_TYPE_U8,
    CRB_TYPE_U16,
    CRB_TYPE_U32,
    CRB_TYPE_U64,
    CRB_TYPE_F32,
    CRB_TYPE_F64,
    CRB_TYPE_REF,
    CRB_TYPE_RAWPTR,
    CRB_TYPE_SYMBOL,
    CRB_TYPE_CLOSURE,
    CRB_TYPE_VECTOR,
    CRB_TYPE_GUEST_DESCRIBED,
};
```

## 5.1 Primitive Type IDs

The following TypeIDs are reserved:

```text
0  void
1  any
2  bool
3  i8
4  i16
5  i32
6  i64
7  u8
8  u16
9  u32
10 u64
11 f32
12 f64
13 ref
14 rawptr
15 symbol
16 closure
17 vector
```

Guest-defined types begin after the reserved range.

---

## 5.2 Register Type Table

Each function contains a register type table:

```cpp
struct CRBRegisterMapEntry {
    uint32_t type_id;
    uint32_t flags;
};
```

Flags:

```cpp
enum CRBRegisterFlags : uint32_t {
    CRB_REG_ARGUMENT       = 1 << 0,
    CRB_REG_CLOSURE_CELL   = 1 << 1,
    CRB_REG_GC_REF         = 1 << 2,
    CRB_REG_INTROSPECTABLE = 1 << 3,
    CRB_REG_TOOLING_VISIBLE = 1 << 4,
};
```

The interpreter may specialize storage, but semantics must behave as if each register holds a value of its declared CRB type class.

---

# 6. Constant Pool

Constants are stored in a constant pool.

```cpp
struct CRBConstantEntry {
    uint32_t kind;
    uint32_t flags;
    uint64_t payload_lo;
    uint64_t payload_hi;
};
```

### Constant kinds

```cpp
enum CRBConstantKind : uint32_t {
    CRB_CONST_NULL,
    CRB_CONST_BOOL,
    CRB_CONST_I8,
    CRB_CONST_I16,
    CRB_CONST_I32,
    CRB_CONST_I64,
    CRB_CONST_U8,
    CRB_CONST_U16,
    CRB_CONST_U32,
    CRB_CONST_U64,
    CRB_CONST_F32,
    CRB_CONST_F64,
    CRB_CONST_SYMBOL,
    CRB_CONST_STRING,
    CRB_CONST_BYTES,
    CRB_CONST_TYPE_ID,
    CRB_CONST_FUNCTION_ID,
    CRB_CONST_OBJECT_HANDLE,
    CRB_CONST_VECTOR_DESC,
};
```

String and byte constants reference the string table.

Object handles are only permitted if the module is trusted or the runtime explicitly supports embedded object constants.

---

# 7. Instruction Formats

CRB defines several canonical operand formats.

---

## 7.1 `R_R_R`

```text
opcode | dst | src0 | src1
```

Used for most binary operations.

Example:

```text
add.i64.wrap r3, r1, r2
```

---

## 7.2 `R_R`

```text
opcode | dst | src | unused
```

Used for unary operations.

Example:

```text
neg.i64 r2, r1
```

---

## 7.3 `R_IMM32`

```text
opcode | dst | imm32_low | imm32_high
```

Used for loading 32-bit immediates or indices.

Example:

```text
mov.const r4, const(1234)
```

---

## 7.4 `BRANCH`

```text
opcode | cond_reg | delta32_low | delta32_high
```

Used for conditional branches.

Example:

```text
br.true r5, +12
```

---

## 7.5 `JUMP`

```text
opcode | unused | delta32_low | delta32_high
```

Example:

```text
jmp -4
```

---

## 7.6 `CALL`

```text
opcode | ret_reg | call_site32_low | call_site32_high
```

Example:

```text
call.direct r8, callsite(42)
```

---

## 7.7 `CALL_INDIRECT`

```text
opcode | ret_reg | target_reg | call_site16
```

Example:

```text
call.indirect r8, r3, callsite(19)
```

---

## 7.8 `ACCESS`

Used for object/member/index operations.

```text
opcode | dst_or_obj | obj_or_value | access_site16
```

Examples:

```text
obj.get r6, r2, access(7)
obj.set r2, r6, access(7)
```

---

## 7.9 `SWITCH`

```text
opcode | selector_reg | switch_table32_low | switch_table32_high
```

Example:

```text
switch r3, switch_table(11)
```

---

## 7.10 `R`

```text
opcode | dst | unused | unused
```

Single-register format. Used for opcodes that operate on exactly one register:
loading sentinel values (`MOV_NULL`, `MOV_TRUE`, `MOV_FALSE`, `MOV_UNDEF`),
returning a value (`RET`), throwing an exception (`THROW`).

The `dst` field names the single register that is either loaded or read;
the two remaining 16-bit slots are reserved (`unused`) and must be zero on
encode and ignored on decode.

Example:

```text
mov_null r3       ; r3 := null
ret r8            ; return r8
throw r2          ; throw r2
```

---

## 7.11 `R_R_SITE16`

```text
opcode | dst | src | site16
```

Two-register plus 16-bit site-descriptor format. Used for raw memory
operations that carry an access-site descriptor for runtime checks,
tracing, and bound recording. The `site16` field is an index into the
module's Site Table (see §17).

Example:

```text
load_mem r3, r1, site(7)    ; r3 := *r1 at site #7
```

---

## 7.12 `R_R_IMM32`

```text
opcode | dst | src | imm32_low | imm32_high
```

Two-register plus 32-bit immediate format. Used for opcodes that take
two register operands plus a 32-bit immediate payload (e.g., a `type_id`
or `class_id` for checked casts). The 32-bit immediate is split across
two 16-bit slots in the same little-endian convention as `R_IMM32`
(§7.3) and `BRANCH` (§7.4).

Example:

```text
obj_cast_checked r3, r1, type(0x42)   ; r3 := cast r1 to type 0x42
```

---

# 8. Opcode Space

CRB uses a 16-bit opcode space divided into groups.

```text
0x0000 - 0x00FF : system/control
0x0100 - 0x01FF : move/constant
0x0200 - 0x03FF : integer arithmetic
0x0400 - 0x04FF : floating-point arithmetic
0x0500 - 0x05FF : conversion
0x0600 - 0x06FF : comparison
0x0700 - 0x07FF : control flow
0x0800 - 0x08FF : calls/returns
0x0900 - 0x09FF : raw memory
0x0A00 - 0x0AFF : object model
0x0B00 - 0x0BFF : allocation
0x0C00 - 0x0CFF : exceptions
0x0D00 - 0x0DFF : suspension
0x0E00 - 0x0EFF : atomics
0x0F00 - 0x0FFF : vector extension
0x1000 - 0x10FF : trace/debug/profiling
0x1100 - 0xFFFF : reserved
```

---

# 9. Core System Opcodes

## 9.1 `NOP`

```text
opcode: 0x0000
format: none
```

Does nothing.

---

## 9.2 `TRAP`

```text
opcode: 0x0001
```

Raises a guest-visible trap or runtime error according to the Guest Language Profile.

Used for unreachable-but-executed states.

---

## 9.3 `UNREACHABLE`

```text
opcode: 0x0002
```

Marks code that should never execute.

If executed, this is a VM bug or invalid module.

---

## 9.4 `SAFEPOINT`

```text
opcode: 0x0003
```

Explicit safepoint.

Backward branches, calls, and allocations are implicit safepoints.

---

## 9.5 `PROFILE_POINT`

```text
opcode: 0x0004
operand: profile_id32
```

Optional profiling hook.

Interpreter may ignore in non-profiling mode.

---

## 9.6 `DEBUG_PROBE`

```text
opcode: 0x0005
format: R_IMM32
operand: probe_id32
```

Used for debugger/monitoring hooks.

Must be semantically neutral unless tooling is active.

---

# 10. Move and Constant Opcodes

## 10.1 `MOV_RR`

```text
opcode: 0x0100
format: R_R
```

Semantics:

```text
dst = src
```

---

## 10.2 `MOV_IMM32`

```text
opcode: 0x0101
format: R_IMM32
```

Loads a signed or unsigned 32-bit immediate depending on register type.

---

## 10.3 `MOV_CONST`

```text
opcode: 0x0102
format: R_IMM32
operand: constant_pool_index
```

Loads a constant from the constant pool.

---

## 10.4 `MOV_NULL`

```text
opcode: 0x0103
format: R
```

Loads the null reference value.

---

## 10.5 `MOV_TRUE`

```text
opcode: 0x0104
format: R
```

---

## 10.6 `MOV_FALSE`

```text
opcode: 0x0105
format: R
```

---

## 10.7 `MOV_UNDEF`

```text
opcode: 0x0106
format: R
```

Writes an explicit undefined marker if supported by the guest profile.

This is not the same as an uninitialized register.

---

# 11. Integer Arithmetic Opcodes

Integer operations are explicitly typed and explicitly specify overflow semantics.

This is mandatory for multi-language correctness.

## 11.1 Operation variants

For each integer width:

```text
add
sub
mul
div_s
div_u
rem_s
rem_u
neg
not
and
or
xor
shl
shr_s
shr_u
```

## 11.2 Overflow modes

Every arithmetic opcode includes one of these semantic modes:

### `.wrap`

Two’s complement wrapping.

Example:

```text
add.i32.wrap r3, r1, r2
```

---

### `.checked`

Raises a guest-defined exception on overflow, division by zero, or invalid operation.

Example:

```text
add.i64.checked r3, r1, r2
```

The exact exception type is determined by the Guest Language Profile.

---

### `.assume`

Assumes the invalid condition cannot occur.

In the interpreter, `.assume` violations must still be detected and reported as a trap unless the guest profile explicitly defines the operation as undefined.

In JIT code, `.assume` may become a guard.

Example:

```text
add.i64.assume r3, r1, r2
```

---

### `.sat`

Optional saturating arithmetic extension.

Example:

```text
add.i32.sat r3, r1, r2
```

---

## 11.3 Example semantics

### `ADD_I64_WRAP`

```text
dst = (src0 + src1) modulo 2^64
```

### `ADD_I64_CHECKED`

```text
if signed_overflow(src0 + src1):
    raise guest arithmetic exception
else:
    dst = src0 + src1
```

### `DIV_I64_CHECKED`

```text
if src1 == 0:
    raise guest division exception
if src0 == INT64_MIN and src1 == -1:
    raise guest overflow exception
dst = src0 / src1
```

---

## 11.4 Opcode Assignments

The integer-arithmetic opcode space `0x0200 - 0x03FF` is divided into
15 operation variants × 4 widths × 4 overflow modes. The full assignment
table below is normative; every integer-arithmetic opcode MUST be encoded
using the formula:

```text
opcode = 0x0200
       + (variant_index * 0x10)        // 15 variants, 16 slots each
       + (width_index  * 0x04)        // 4 widths (i8/i16/i32/i64)
       + (overflow_index * 0x01)      // 4 modes (wrap/checked/assume/sat)
```

The variant indices are:

```text
0: add       1: sub       2: mul       3: div_s     4: div_u
5: rem_s     6: rem_u     7: neg       8: not       9: and
10: or       11: xor      12: shl      13: shr_s    14: shr_u
```

The width indices are:

```text
0: i8        1: i16       2: i32       3: i64
```

The overflow-mode indices are:

```text
0: .wrap     1: .checked  2: .assume   3: .sat
```

### Canonical examples

```text
ADD_I64_WRAP       = 0x0200 + (0*0x10) + (3*0x04) + 0 = 0x020C
ADD_I64_CHECKED    = 0x0200 + (0*0x10) + (3*0x04) + 1 = 0x020D
SUB_I32_WRAP       = 0x0200 + (1*0x10) + (2*0x04) + 0 = 0x0218
MUL_I32_CHECKED    = 0x0200 + (2*0x10) + (2*0x04) + 1 = 0x0221
DIV_S_I64_CHECKED  = 0x0200 + (3*0x10) + (3*0x04) + 1 = 0x022D
NEG_I64_WRAP       = 0x0200 + (7*0x10) + (3*0x04) + 0 = 0x027C
NEG_I64_CHECKED    = 0x0200 + (7*0x10) + (3*0x04) + 1 = 0x027D
NOT_I64_WRAP       = 0x0200 + (8*0x10) + (3*0x04) + 0 = 0x028C
SHR_U_I64_WRAP     = 0x0200 + (14*0x10) + (3*0x04) + 0 = 0x02EC
```

All canonical examples land within the `0x0200 - 0x03FF` integer-arithmetic
range. The `NOT`, `AND`, `OR`, `XOR`, `SHL`, `SHR_S`, `SHR_U` variants have
no overflow concept, so their `.checked`/`.assume`/`.sat` sub-slots are
reserved and MUST decode as a trap if executed. For these variants only
the `.wrap` mode (index 0) is meaningful; the other three slots per width
are reserved (this does not consume extra opcode space — the slots exist
but trap on execution).

### Format

All integer-arithmetic opcodes use the `R_R_R` format (§7.1) for binary
operations (`add`, `sub`, `mul`, `div_s`, `div_u`, `rem_s`, `rem_u`,
`and`, `or`, `xor`, `shl`, `shr_s`, `shr_u`) and the `R_R` format (§7.2)
for unary operations (`neg`, `not`).

### Range check

The 15 × 4 × 4 = 240 slot allocation fits within the `0x0200 - 0x03FF`
range (which is 0x0200 = 512 slots). The highest used slot is
`0x0200 + (14 * 0x10) + (3 * 0x04) + 3 = 0x02EF`; the remaining
`0x02F0 - 0x03FF` (272 slots) in the integer-arithmetic range are reserved
for future variants (e.g., wider integers, packed SIMD-style variants
that are not part of the dedicated `0x0F00` vector extension).

---

# 12. Floating-Point Opcodes

Floating-point operations are also typed and explicit.

Operations:

```text
add.f32
add.f64
sub.f32
sub.f64
mul.f32
mul.f64
div.f32
div.f64
rem.f32
rem.f64
neg.f32
neg.f64
abs.f32
abs.f64
sqrt.f32
sqrt.f64
min.f32
min.f64
max.f32
max.f64
```

Floating-point operations have two modes:

### `.strict`

Default.

Preserves IEEE-754 behavior as required by the guest profile, including NaN behavior where required.

### `.fast`

Optional.

Only allowed if the Guest Language Profile certifies fast-math semantics for the function or region.

Example:

```text
mul.f64.fast r4, r2, r3
```

---

# 13. Conversion Opcodes

Conversions are explicit.

CRB forbids implicit conversions.

```text
trunc.i64.i32
sext.i32.i64
zext.i32.i64
bitcast.i32.f32
bitcast.i64.f64
int_to_float.i32.f32
int_to_float.i64.f64
uint_to_float.u32.f32
uint_to_float.u64.f64
float_to_int.f32.i32
float_to_int.f64.i64
float_to_uint.f32.u32
float_to_uint.f64.u64
box
unbox
any_to_ref
ref_to_any
any_to_i64.checked
any_to_f64.checked
```

Dynamic conversion operations such as `any_to_i64.checked` may invoke guest conversion rules and may raise exceptions.

---

# 14. Comparison Opcodes

Comparisons produce boolean results.

For typed primitives:

```text
cmp_eq.i32
cmp_ne.i32
cmp_lt_s.i32
cmp_lt_u.i32
cmp_le_s.i32
cmp_le_u.i32
cmp_gt_s.i32
cmp_gt_u.i32
cmp_ge_s.i32
cmp_ge_u.i32
cmp_eq.i64
cmp_ne.i64
...
cmp_eq.f32
cmp_lt.f32
cmp_le.f32
cmp_gt.f32
cmp_ge.f32
cmp_ne.f32
```

For dynamic values:

```text
cmp_eq.any
cmp_ne.any
cmp_lt.any
cmp_le.any
cmp_gt.any
cmp_ge.any
```

Dynamic comparisons may invoke guest operator hooks and may raise exceptions.

---

# 15. Control Flow Opcodes

## 15.1 `JMP`

```text
opcode: 0x0700
format: JUMP
```

Unconditional jump.

---

## 15.2 `BR_TRUE`

```text
opcode: 0x0701
format: BRANCH
```

Branch if condition register is true.

---

## 15.3 `BR_FALSE`

```text
opcode: 0x0702
format: BRANCH
```

Branch if condition register is false.

---

## 15.4 `BR_NULL`

```text
opcode: 0x0703
```

Branch if register is null.

---

## 15.5 `BR_NONNULL`

```text
opcode: 0x0704
```

Branch if register is not null.

---

## 15.6 `SWITCH`

```text
opcode: 0x0710
format: SWITCH
```

Uses a switch table.

```cpp
struct CRBSwitchEntry {
    uint64_t value;
    int32_t  delta;
    uint32_t flags;
};

struct CRBSwitchTable {
    uint32_t case_count;
    uint32_t default_delta;
    uint32_t flags;
    // entries follow
};
```

Switch implementation may use:

- jump table
- binary search
- linear scan

depending on density and runtime policy.

---

# 16. Call Opcodes

## 16.1 `RET`

```text
opcode: 0x0800
format: R
```

Returns the value in `dst` register.

---

## 16.2 `RET_VOID`

```text
opcode: 0x0801
```

Returns no value.

---

## 16.3 `CALL_DIRECT`

```text
opcode: 0x0810
format: CALL
```

Calls a function identified by a call site.

---

## 16.4 `CALL_INDIRECT`

```text
opcode: 0x0811
format: CALL_INDIRECT
```

Calls a callable object stored in a register.

---

## 16.5 `CALL_VIRTUAL`

```text
opcode: 0x0812
format: CALL
```

Uses call-site metadata for receiver and method identity.

---

## 16.6 `CALL_INTERFACE`

```text
opcode: 0x0813
format: CALL
```

Uses interface/dynamic dispatch metadata.

---

## 16.7 `CALL_NATIVE`

```text
opcode: 0x0814
format: CALL
```

Calls an FFI/native descriptor.

This is opaque unless proven otherwise.

---

## 16.8 `CALL_INTRINSIC`

```text
opcode: 0x0815
format: CALL
```

Calls a DVM intrinsic.

---

## 16.9 `TAIL_CALL_DIRECT`

Optional extension.

```text
opcode: 0x0820
```

Must not increase logical call depth.

---

## 16.10 `TAIL_CALL_INDIRECT`

Optional extension.

```text
opcode: 0x0821
format: CALL_INDIRECT
```

Same operand layout as `CALL_INDIRECT` (§7.7); the difference is that the
caller's frame is discarded before the callee is entered (tail position).

---

# 17. Call Site Table

Every call instruction references a call site.

```cpp
struct CRBCallSite {
    uint32_t kind;
    uint32_t flags;
    uint32_t target_id;
    uint32_t arg_base;
    uint16_t arg_count;
    uint16_t ret_count;
    uint32_t receiver_reg;
    uint32_t dependency_set;
    uint32_t trace_hint_id;
};
```

Call site kinds:

```cpp
enum CRBCallKind : uint32_t {
    CRB_CALL_DIRECT,
    CRB_CALL_INDIRECT,
    CRB_CALL_VIRTUAL,
    CRB_CALL_INTERFACE,
    CRB_CALL_NATIVE,
    CRB_CALL_INTRINSIC,
};
```

Call site flags:

```cpp
enum CRBCallFlags : uint32_t {
    CRB_CALL_LEAF            = 1 << 0,
    CRB_CALL_NO_THROW        = 1 << 1,
    CRB_CALL_NO_SIDE_EFFECT  = 1 << 2,
    CRB_CALL_READONLY        = 1 << 3,
    CRB_CALL_INLINE_CANDIDATE = 1 << 4,
    CRB_CALL_MONOMORPHIC_HINT = 1 << 5,
    CRB_CALL_FINAL_TARGET_HINT = 1 << 6,
    CRB_CALL_SUSPENDABLE     = 1 << 7,
};
```

---

# 18. Raw Memory Opcodes

Raw memory operations are optional and only permitted if the module declares `CRB_MODULE_HAS_RAW_MEMORY` and the Guest Language Profile allows them.

## 18.1 `LOAD_MEM`

```text
opcode: 0x0900
format: R_R_SITE16
```

Loads from a raw pointer using a memory site descriptor.

---

## 18.2 `STORE_MEM`

```text
opcode: 0x0901
format: R_R_SITE16
```

Stores to a raw pointer using a memory site descriptor. Symmetric with
`LOAD_MEM` (§18.1): `dst` is the source value register, `src` is the
raw pointer register, `site16` is the memory-site descriptor index.

---

## 18.3 Memory Site Descriptor

```cpp
struct CRBMemorySite {
    uint32_t type_id;
    uint16_t alignment;
    uint16_t flags;
    uint32_t address_space;
    uint32_t dependency_set;
};
```

Memory flags:

```cpp
enum CRBMemoryFlags : uint16_t {
    CRB_MEM_VOLATILE   = 1 << 0,
    CRB_MEM_ACQUIRE    = 1 << 1,
    CRB_MEM_RELEASE    = 1 << 2,
    CRB_MEM_SEQ_CST    = 1 << 3,
    CRB_MEM_READONLY   = 1 << 4,
    CRB_MEM_NOALIAS    = 1 << 5,
    CRB_MEM_IMMUTABLE  = 1 << 6,
};
```

---

# 19. Object Model Opcodes

CRB does not assume a single object layout.

Object operations are described through **Access Sites**.

---

## 19.1 Access Site Descriptor

```cpp
struct CRBAccessSite {
    uint32_t kind;
    uint32_t member_symbol;
    uint32_t type_hint;
    uint32_t shape_hint;
    uint32_t flags;
    uint32_t dependency_set;
    uint32_t inline_cache_id;
};
```

Access kinds:

```cpp
enum CRBAccessKind : uint32_t {
    CRB_ACCESS_FIELD,
    CRB_ACCESS_PROPERTY,
    CRB_ACCESS_METHOD,
    CRB_ACCESS_INDEX,
    CRB_ACCESS_SLOT,
    CRB_ACCESS_DYNAMIC,
};
```

Access flags:

```cpp
enum CRBAccessFlags : uint32_t {
    CRB_ACCESS_READONLY       = 1 << 0,
    CRB_ACCESS_WRITEONLY      = 1 << 1,
    CRB_ACCESS_MONOMORPHIC_HINT = 1 << 2,
    CRB_ACCESS_NO_SIDE_EFFECT = 1 << 3,
    CRB_ACCESS_CAN_INVOKE_HOOK = 1 << 4,
    CRB_ACCESS_BOUNDS_CHECKED = 1 << 5,
    CRB_ACCESS_NULL_CHECKED   = 1 << 6,
    CRB_ACCESS_DIRECT_OFFSET  = 1 << 7,
};
```

---

## 19.2 `OBJ_NEW`

```text
opcode: 0x0B00
format: R_IMM32
operand: type_id
```

Allocates a new object of the given type.

May trigger GC.

May raise a guest memory exception.

---

## 19.3 `OBJ_GET`

```text
opcode: 0x0A00
format: ACCESS
```

Semantics:

```text
dst = obj.member
```

May invoke getter hooks depending on Guest Language Profile.

May update inline cache.

---

## 19.4 `OBJ_SET`

```text
opcode: 0x0A01
format: ACCESS
```

Semantics:

```text
obj.member = value
```

May invoke setter hooks.

May require GC write barrier.

---

## 19.5 `IDX_GET`

```text
opcode: 0x0A10
```

Semantics:

```text
dst = obj[index]
```

Bounds checked by default.

---

## 19.6 `IDX_SET`

```text
opcode: 0x0A11
```

Semantics:

```text
obj[index] = value
```

Bounds checked by default.

---

## 19.7 `OBJ_CAST_CHECKED`

```text
opcode: 0x0A20
format: R_R_IMM32
operand: type_id
```

Checked cast.

Raises guest type exception on failure.

---

## 19.8 `OBJ_IS_INSTANCE`

```text
opcode: 0x0A21
```

Produces boolean.

Does not raise on type mismatch.

---

## 19.9 `CLOSURE_NEW`

```text
opcode: 0x0A30
format: R_R_IMM32
```

Creates a closure object. `dst` is the closure register, `src` is the
environment register (the captured environment, possibly null), and the
32-bit immediate is the index into the Closure Descriptor Table.

Uses a closure descriptor:

```cpp
struct CRBClosureDesc {
    uint32_t function_id;
    uint16_t capture_count;
    uint16_t flags;
    uint32_t capture_list_offset;
};
```

Captures may be by value or by reference according to guest semantics.

---

# 20. Exception Opcodes

CRB exceptions are guest-language values and control-flow transfers.

They are not native C++ exceptions.

---

## 20.1 `THROW`

```text
opcode: 0x0C00
format: R
```

Throws the exception object in the register.

The runtime searches for a handler.

If no handler exists, the exception propagates according to guest semantics.

---

## 20.2 Exception Table Entry

```cpp
struct CRBExceptionEntry {
    uint32_t try_start_pc;
    uint32_t try_end_pc;
    uint32_t handler_pc;
    uint32_t catch_type_id;
    uint32_t catch_reg;
    uint32_t flags;
};
```

Exception flags:

```cpp
enum CRBExceptionFlags : uint32_t {
    CRB_EXCEPTION_CATCH_ALL    = 1 << 0,
    CRB_EXCEPTION_FINALLY      = 1 << 1,
    CRB_EXCEPTION_CLEANUP      = 1 << 2,
    CRB_EXCEPTION_SUSPEND_SAFE = 1 << 3,
};
```

At handler entry:

- the exception value is placed in `catch_reg`
- the PC is `handler_pc`
- the interpreter frame must be reconstructable
- traceback state must be preserved

---

# 21. Suspension Opcodes

Optional extension for generators, coroutines, async functions, fibers, and continuations.

## 21.1 `SUSPEND_YIELD`

```text
opcode: 0x0D00
operand: suspension_site_id
```

Suspends current execution and returns a value to the resumer.

---

## 21.2 `SUSPEND_AWAIT`

```text
opcode: 0x0D01
```

Suspends waiting for an awaited value.

---

## 21.3 `SUSPEND_CLOSE`

```text
opcode: 0x0D02
format: R
```

Requests cooperative close/cancel on the suspension referenced by the
single register operand.

---

## 21.4 Suspension Site Descriptor

```cpp
struct CRBSuspensionSite {
    uint32_t kind;
    uint32_t state_desc_id;
    uint32_t resume_pc;
    uint32_t cleanup_pc;
    uint32_t flags;
};
```

Suspension sites must be valid safepoints and deopt points.

---

# 22. Atomic Opcodes

Optional extension.

Atomic operations use atomic site descriptors.

```cpp
struct CRBAtomicSite {
    uint32_t type_id;
    uint16_t ordering;
    uint16_t flags;
};
```

Orderings:

```cpp
enum CRBAtomicOrder : uint16_t {
    CRB_ATOMIC_RELAXED,
    CRB_ATOMIC_ACQUIRE,
    CRB_ATOMIC_RELEASE,
    CRB_ATOMIC_ACQ_REL,
    CRB_ATOMIC_SEQ_CST,
};
```

Opcodes:

```text
ATOMIC_LOAD
ATOMIC_STORE
ATOMIC_ADD
ATOMIC_SUB
ATOMIC_AND
ATOMIC_OR
ATOMIC_XOR
ATOMIC_XCHG
ATOMIC_CMPXCHG
```

---

# 23. Vector Opcodes

Optional extension.

Vector operations are explicitly typed.

Examples:

```text
V_LOAD_F32X4
V_STORE_F32X4
V_ADD_F32X4
V_MUL_F32X4
V_ADD_I32X4
V_SPLAT_F32X4
V_EXTRACT_F32
V_INSERT_F32
```

Vector operations must preserve guest numeric semantics and must not enable fast-math behavior unless explicitly certified.

---

# 24. Trace and Debug Opcodes

These opcodes support tracing and tooling.

They must not alter guest semantics unless tooling semantics require it.

The trace/debug opcode range is `0x1000 - 0x10FF` (see §8).

## 24.1 `TRACE_PROMOTE_HINT`

Optional metadata opcode.

Suggests that a value is a promotion candidate.

Ignored by normal execution.

```text
opcode: 0x1000
format: R_IMM32
operand: hint_id32
```

The `dst` register holds the value being suggested for promotion; the
32-bit immediate is a hint identifier the trace recorder may use to
correlate the hint with downstream profile data.

---

## 24.2 `TRACE_VIRTUAL_HINT`

Suggests that an allocation is virtualizable.

Ignored by normal execution.

```text
opcode: 0x1001
format: R_IMM32
operand: hint_id32
```

The `dst` register holds the reference to the allocation being suggested
as virtualizable; the 32-bit immediate is a hint identifier.

---

## 24.3 `DEBUG_BREAKPOINT`

Debugger breakpoint hook.

```text
opcode: 0x1002
format: R_IMM32
operand: breakpoint_id32
```

The 32-bit immediate is the breakpoint identifier. The `dst` register
field is reserved (zero on encode, ignored on decode) — this opcode
takes no value operand; it is a pure control hook for the debugger.
Execution under a debugger transfers control to the debugger; execution
without a debugger is a no-op.

---

## 24.4 `MONITOR_EVENT`

Monitoring hook.

```text
opcode: 0x1003
format: R_IMM32
operand: event_id32
```

The 32-bit immediate is the event identifier (an index into the module's
Monitor Event Table). The `dst` register field is reserved (zero on
encode, ignored on decode). The runtime may emit a monitor event for
profiling or tracing; semantically neutral unless tooling is active.

---

# 25. Block Table

CRB code is organized into blocks.

```cpp
struct CRBBlockEntry {
    uint32_t start_pc;
    uint32_t flags;
    uint32_t source_pos_id;
};
```

Block flags:

```cpp
enum CRBBlockFlags : uint32_t {
    CRB_BLOCK_FUNCTION_ENTRY = 1 << 0,
    CRB_BLOCK_LOOP_HEADER    = 1 << 1,
    CRB_BLOCK_EXCEPTION_HANDLER = 1 << 2,
    CRB_BLOCK_SUSPENSION_RESUME = 1 << 3,
    CRB_BLOCK_COLD           = 1 << 4,
    CRB_BLOCK_OS_ENTRY       = 1 << 5,
};
```

All branch targets must be block starts.

---

# 26. Source Map

CRB supports source position mapping.

```cpp
struct CRBSourcePos {
    uint32_t file_symbol;
    uint32_t line;
    uint32_t column;
    uint32_t function_symbol;
    uint32_t flags;
};
```

The source map maps instruction ranges to source positions.

This is required for:

- tracebacks
- debugging
- profiling
- monitoring
- deopt reconstruction

---

# 27. Dependency Table

For JIT invalidation, CRB modules may declare dependencies.

```cpp
struct CRBDependency {
    uint32_t kind;
    uint32_t id;
    uint64_t version;
};
```

Dependency kinds:

```cpp
enum CRBDependencyKind : uint32_t {
    CRB_DEP_GUEST_LANGUAGE,
    CRB_DEP_MODULE,
    CRB_DEP_TYPE,
    CRB_DEP_SHAPE,
    CRB_DEP_GLOBAL_VERSION,
    CRB_DEP_BUILTIN_VERSION,
    CRB_DEP_FUNCTION,
    CRB_DEP_METHOD,
    CRB_DEP_PROFILE,
    CRB_DEP_RUNTIME_CONFIG,
};
```

---

# 28. Trace Hint Table

Trace hints are not required for execution.

They improve meta-tracing and normal tracing.

```cpp
struct CRBTraceHint {
    uint32_t kind;
    uint32_t target_id;
    uint32_t confidence_class;
    uint32_t flags;
};
```

Hint kinds:

```cpp
enum CRBTraceHintKind : uint32_t {
    CRB_HINT_PROMOTE_CONSTANT,
    CRB_HINT_PROMOTE_TYPE,
    CRB_HINT_PROMOTE_SHAPE,
    CRB_HINT_PROMOTE_RANGE,
    CRB_HINT_VIRTUALIZABLE_ALLOC,
    CRB_HINT_MONOMORPHIC_CALL,
    CRB_HINT_MONOMORPHIC_ACCESS,
    CRB_HINT_LOOP_INVARIANT,
    CRB_HINT_COLD_PATH,
};
```

Trace hints are advisory only.

They never change required semantics.

---

# 29. Interpreter Requirements

A conforming DVM CRB interpreter must:

1. verify modules before execution
2. execute all required CRB opcodes correctly
3. reject unsupported required extensions
4. preserve guest-visible effects
5. maintain exact exception state
6. maintain exact register state for deopt
7. support safepoints
8. support profiling hooks
9. support tracing hooks
10. fall back safely when JIT code deoptimizes

---

## 29.1 Direct-threaded implementation

For performance, the interpreter should use direct threading.

Each CRB opcode maps to a handler label or handler function pointer.

Handlers should be generated from opcode descriptors.

---

## 29.2 Opcode descriptor

Every opcode handler should have metadata:

```cpp
struct CRBOpcodeDescriptor {
    uint16_t opcode;
    const char* name;
    CRBOpcodeClass opcode_class;
    CRBEffectClass effect_class;
    CRBTraceClass trace_class;
    CRBFrameStatePolicy frame_state_policy;
    bool can_trap;
    bool can_throw;
    bool can_allocate;
    bool can_suspend;
    bool can_side_exit;
    bool is_pure;
};
```

Trace classes:

```cpp
enum class CRBTraceClass {
    Pure,
    Guard,
    Load,
    Store,
    Alloc,
    Call,
    Return,
    Branch,
    Throw,
    Suspend,
    VMInternal,
    Opaque,
};
```

Effect classes:

```cpp
enum class CRBEffectClass {
    None,
    Pure,
    MemoryRead,
    MemoryWrite,
    GuestVisible,
    Allocation,
    Exception,
    IO,
    FFI,
    GCBarrier,
    Tooling,
    Suspension,
};
```

---

# 30. Verification Rules

CRB verification is mandatory.

## 30.1 Structural checks

- header magic/version valid
- section offsets/sizes valid
- all indices in range
- branch targets are block starts
- switch targets are block starts
- exception ranges are well-formed
- code length is multiple of instruction size
- no overlapping sections

---

## 30.2 Register checks

- register indices in range
- register types declared
- no read before write unless allowed
- argument registers initialized by entry convention
- call arguments compatible with call site

---

## 30.3 Type checks

- operands type-compatible with opcode
- conversions explicit
- object operations have reference-like receivers
- raw memory operations have raw pointer operands
- atomic operations have compatible type/ordering

---

## 30.4 Effect checks

- pure operations do not reference opaque side-effecting descriptors
- memory operations have valid memory sites
- FFI calls use declared native descriptors
- suspension only occurs in suspendable functions

---

## 30.5 Guest capability checks

- raw memory allowed only if module and profile allow it
- vector ops allowed only if extension present
- atomics allowed only if extension present
- suspension allowed only if extension present
- FFI allowed only if policy permits

---

# 31. Runtime Traps

CRB distinguishes verifier errors from runtime traps.

Verifier errors reject the module.

Runtime traps occur during valid execution.

Examples:

- checked arithmetic overflow
- division by zero
- null dereference
- bounds check failure
- cast failure
- stack overflow
- out-of-memory
- unsupported dynamic feature
- invalid indirect call target
- native call failure
- suspended frame misuse

All runtime traps must produce guest-appropriate behavior.

---

# 32. Conformance Levels

## 32.1 CRB-Interp

Required for all DVM runtimes.

Supports:

- core opcodes
- verifier
- exception handling
- profiling
- tracing hooks
- Tier 0 execution

---

## 32.2 CRB-JIT

Required for optimizing runtimes.

Adds:

- DGW lifting compatibility
- FrameState reconstruction requirements
- dependency validation
- OSR support
- deopt target fidelity

---

## 32.3 CRB-AOT

Required for static compilation.

Adds:

- static proof section support
- manifest validation
- dependency fingerprinting
- deterministic code generation requirements

---

# 33. CRB Assembly Syntax

CRB should have a canonical textual form.

Example:

```crb
.module "example"
.guest_profile "examplelang@1.0"
.require CRB_BASE

.func public @add(a: i64, b: i64) -> i64 regs=4 args=2 {
.entry:
    add.i64.wrap r2, r0, r1
    ret r2
}
```

Branch example:

```crb
.func @sum(n: i64) -> i64 regs=6 args=1 {
.entry:
    mov.const r1, const(0)       ; sum
    mov.const r2, const(0)       ; i
.loop:
    cmp.lt.s.i64 r3, r2, r0
    br.false r3, .exit
.body:
    add.i64.checked r1, r1, r2
    add.i64.checked r2, r2, const(1)
    jmp .loop
.exit:
    ret r1
}
```

Object access example:

```crb
.func @get_x(obj: ref) -> any regs=3 args=1 {
.entry:
    obj.get r1, r0, access(@x)
    ret r1
}
```

---

# 34. Example Encoding

Given:

```crb
add.i64.wrap r2, r0, r1
```

Assume:

```text
opcode ADD_I64_WRAP = 0x0200
dst = 2
src0 = 0
src1 = 1
```

64-bit instruction:

```text
opcode: 0x0200
op0:    0x0002
op1:    0x0000
op2:    0x0001
```

Bits:

```text
(op2 << 48) | (op1 << 32) | (op0 << 16) | opcode
```

Result:

```text
0x0001_0000_0002_0200
```

Little-endian byte storage:

```text
00 02 02 00 00 00 01 00
```

---

# 35. CRB and DGW Relationship

CRB is not DGW.

CRB is:

- stable
- interpreter-oriented
- linear
- register-based
- block-structured
- deopt-reference

DGW/Trace is:

- optimizing
- graph-based
- trace-first
- speculative
- region-aware
- mutable within the Weaver API

The lifting relationship is:

```text
CRB bytecode
    ↓
interpreter execution / trace recording
    ↓
CRB semantic operations
    ↓
DGW/Trace graph
    ↓
optimized native trace
```

When deopt occurs, DVM must be able to return to the equivalent CRB state:

```text
CRB function ID
CRB pc
CRB register file
CRB exception state
CRB tooling state
CRB suspension state
```

---

# 36. Security Rules

CRB modules are untrusted unless signed and validated.

A conforming runtime must:

- reject malformed modules
- not resolve native symbols outside an allowlist
- not permit self-modifying bytecode
- not permit arbitrary embedded object handles in untrusted modules
- validate constant pool entries
- validate switch tables
- validate exception tables
- validate dependencies
- enforce W^X for any generated code derived from CRB

---

# 37. Performance Requirements

CRB is designed for fast interpretation.

Recommended interpreter implementation rules:

1. use direct threading where possible
2. precompute dispatch table from opcode descriptors
3. cache inline cache state per access site
4. batch profile counter updates
5. avoid global locks on dispatch paths
6. keep register file frame-local
7. special-case monomorphic access sites
8. generate superinstructions optionally
9. keep exception handling out of fast paths
10. keep tracing hooks out-of-line unless recording

---

# 38. Standardization Checklist

For CRB v1.0 to be considered stable, the following must exist:

- [ ] binary format spec
- [ ] opcode registry
- [ ] textual assembly grammar
- [ ] verifier suite
- [ ] conformance test suite
- [ ] differential interpreter tests
- [ ] golden disassembly tests
- [ ] fuzzing harness
- [ ] Guest Language Profile binding guide
- [ ] DGW lifting mapping table
- [ ] deopt reconstruction tests
- [ ] trace recording tests
- [ ] exception semantics tests
- [ ] suspension semantics tests
- [ ] FFI safety tests
- [ ] versioning and compatibility policy

---

# 39. Final Definition

**CRB — Common Register Bytecode** is DVM’s standard lower-tier bytecode.

It is:

- register-based
- fixed-width 64-bit
- language-neutral
- verifier-first
- trace-aware
- deopt-exact
- guest-profile-driven
- interpreter-optimized
- JIT-liftable

CRB is the ground truth for interpreter execution and deoptimization.

DGW optimizes above CRB.

CRB preserves the semantic floor.
