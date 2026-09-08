// dvm/jit_buffer.hpp — Executable memory allocator for JIT code.
//
// Uses mmap + mprotect to allocate a block of memory with read+execute
// permissions. The encoder writes machine code into this buffer; once
// final, the buffer's function pointer can be called directly.
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dvm {

class JitBuffer {
 public:
  JitBuffer() = default;
  ~JitBuffer();

  // Allocate a writable buffer of `capacity` bytes. Returns false on failure.
  bool allocate(std::size_t capacity);

  // Make the buffer executable (call after encoding is complete).
  // After this call, the buffer is read+execute only — no more writes.
  bool make_executable();

  // Get the function pointer to the start of the buffer.
  void* entry() const noexcept { return data_; }

  // Current write offset.
  std::size_t size() const noexcept { return size_; }
  std::size_t capacity() const noexcept { return capacity_; }

  // Write a byte at the current position and advance.
  void emit_byte(std::uint8_t b) {
    if (size_ < capacity_) {
      static_cast<std::uint8_t*>(data_)[size_++] = b;
    }
  }

  // Write multiple bytes.
  void emit_bytes(std::span<const std::uint8_t> bytes) {
    for (auto b : bytes) emit_byte(b);
  }

  // Write a 32-bit little-endian value.
  void emit_u32(std::uint32_t v) {
    emit_byte(static_cast<std::uint8_t>(v & 0xFF));
    emit_byte(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    emit_byte(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    emit_byte(static_cast<std::uint8_t>((v >> 24) & 0xFF));
  }

  // Write a 64-bit little-endian value.
  void emit_u64(std::uint64_t v) {
    emit_u32(static_cast<std::uint32_t>(v & 0xFFFFFFFF));
    emit_u32(static_cast<std::uint32_t>(v >> 32));
  }

  // Record the current offset (for patching branch targets later).
  std::size_t offset() const noexcept { return size_; }

  // Patch a 32-bit value at a given offset (for branch target fixup).
  void patch_u32(std::size_t offset, std::uint32_t value) {
    if (offset + 4 <= capacity_) {
      auto* p = static_cast<std::uint8_t*>(data_);
      p[offset]     = static_cast<std::uint8_t>(value & 0xFF);
      p[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
      p[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
      p[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    }
  }

 private:
  void*    data_{nullptr};
  std::size_t size_{0};
  std::size_t capacity_{0};
  bool     executable_{false};
};

}  // namespace dvm
