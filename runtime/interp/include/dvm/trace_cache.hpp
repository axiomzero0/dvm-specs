// dvm/trace_cache.hpp — Trace cache: maps (function_id, loop_pc) → CompiledTrace.
//
// Spec citation: DVM-Hybrid-Tracing-Architecture.md §12.4 (trace cache),
// §13 (trace execution).
//
// After a trace is compiled, the cache stores it keyed by (function_id,
// loop_header_pc). When the hotness tracker fires again for the same loop,
// the interpreter checks the cache: if a compiled trace exists, it calls
// the native code directly instead of re-recording.
//
// This is what makes the JIT actually produce speedup: the first time a
// loop is hot, we record + lift + compile. Every subsequent time, we
// call the compiled native code directly.
//
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>

#include "dvm/trace.hpp"
#include "dvm/trace_compiler.hpp"
#include "dvm/native_codegen.hpp"
#include "dvm/lifter.hpp"

namespace dvm {

// ---- Cache key: (function_id, loop_header_pc) ---------------------------
struct TraceKey {
  std::uint32_t function_id;
  std::uint32_t loop_pc;
  bool operator==(const TraceKey& o) const noexcept {
    return function_id == o.function_id && loop_pc == o.loop_pc;
  }
};

struct TraceKeyHash {
  std::size_t operator()(const TraceKey& k) const noexcept {
    return (std::hash<std::uint32_t>{}(k.function_id) * 31) ^
           std::hash<std::uint32_t>{}(k.loop_pc);
  }
};

// ---- A cached, compiled trace --------------------------------------------
struct CachedTrace {
  TraceFragment fragment;        // the recorded trace
  CompiledTrace compiled;        // the optimized + scheduled graph
  NativeTrace native;            // the native x86-64 code
  std::int64_t execution_count{0};  // how many times the native code was called
};

// ---- The trace cache ------------------------------------------------------
class TraceCache {
 public:
  TraceCache() = default;

  // Look up a compiled trace by key. Returns nullptr if not cached.
  const CachedTrace* lookup(std::uint32_t function_id,
                             std::uint32_t loop_pc) const {
    auto it = cache_.find({function_id, loop_pc});
    return it != cache_.end() ? it->second.get() : nullptr;
  }

  // Store a compiled trace. Takes ownership of the fragment and graph.
  void store(std::uint32_t function_id, std::uint32_t loop_pc,
              TraceFragment frag, CompiledTrace&& compiled, NativeTrace&& native) {
    auto ct = std::make_unique<CachedTrace>();
    ct->fragment = std::move(frag);
    ct->compiled = std::move(compiled);
    ct->native = std::move(native);
    cache_[{function_id, loop_pc}] = std::move(ct);
  }

  // Check if a key is cached.
  bool has(std::uint32_t function_id, std::uint32_t loop_pc) const {
    return cache_.find({function_id, loop_pc}) != cache_.end();
  }

  // Get the number of cached traces.
  std::size_t size() const noexcept { return cache_.size(); }

  // Call the cached native trace for the given key. Increments execution_count.
  // Returns the native result, or a default-constructed Value if not cached.
  std::int64_t call_native(std::uint32_t function_id, std::uint32_t loop_pc,
                            std::int64_t r0, std::int64_t r1,
                            std::int64_t r2, std::int64_t r3) {
    auto it = cache_.find({function_id, loop_pc});
    if (it == cache_.end()) return 0;
    it->second->execution_count++;
    return it->second->native.entry(r0, r1, r2, r3);
  }

 private:
  std::unordered_map<TraceKey, std::unique_ptr<CachedTrace>, TraceKeyHash> cache_;
};

}  // namespace dvm
