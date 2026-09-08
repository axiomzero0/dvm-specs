// src/jit_buffer.cpp — Executable memory allocator for JIT code.
//
#include "dvm/jit_buffer.hpp"

#include <cstdlib>
#include <cstring>

#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace dvm {

JitBuffer::~JitBuffer() {
  if (data_) {
#if defined(__linux__)
    munmap(data_, capacity_);
#else
    std::free(data_);
#endif
  }
}

bool JitBuffer::allocate(std::size_t capacity) {
  capacity_ = capacity;
#if defined(__linux__)
  // Allocate a page-aligned region with RW permissions initially.
  // PROT_READ | PROT_WRITE for encoding; we'll switch to R+X later.
  data_ = mmap(nullptr, capacity, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (data_ == MAP_FAILED) {
    data_ = nullptr;
    return false;
  }
#else
  data_ = std::malloc(capacity);
  if (!data_) return false;
#endif
  size_ = 0;
  executable_ = false;
  return true;
}

bool JitBuffer::make_executable() {
  if (!data_) return false;
#if defined(__linux__)
  // Flush instruction cache, then change to R+X.
  // On x86-64, instruction cache is coherent, so no explicit flush needed.
  if (mprotect(data_, capacity_, PROT_READ | PROT_EXEC) != 0) {
    return false;
  }
#endif
  executable_ = true;
  return true;
}

}  // namespace dvm
