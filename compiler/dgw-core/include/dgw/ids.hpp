// dgw/ids.hpp — DGW-Core Core Identifiers (Spec Part 1.1).
//
// Spec citation: DGW-Core-IR.md Part 1.1 "The Core Identifiers".
//
// "Everything in DGW-Core is a 32-bit integer index. This guarantees a
//  4-byte memory footprint per reference, allows trivial serialization,
//  and prevents pointer invalidation during arena reallocation
//  (DVM Rule 15)."
//
// We use strong typedefs so the type system prevents mixups like passing
// a NodeId where an EdgeId is expected. Each id is a struct wrapping a
// single uint32_t (or uint16_t for PortId) with default-construct-to-null,
// explicit construct-from-raw, comparison, and std::hash support.
//
#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>  // std::hash

#include "dgw/util.hpp"

namespace dgw {

// ---- Strong 32-bit index types -------------------------------------------
// We define the macro to expand to BOTH the struct and the static_assert;
// the caller terminates the invocation with a `;` which closes the
// static_assert declaration.
#define DGW_STRONG_INDEX(Name, Null)                                          \
  struct Name {                                                               \
    std::uint32_t value{Null};                                                \
    Name() = default;                                                         \
    explicit constexpr Name(std::uint32_t v) noexcept : value(v) {}          \
    constexpr bool operator==(const Name&) const = default;                   \
    constexpr auto operator<=>(const Name&) const = default;                  \
    constexpr bool is_null() const noexcept { return value == Null; }         \
    constexpr bool valid() const noexcept { return value != Null; }           \
  };                                                                          \
  static_assert(sizeof(Name) == sizeof(std::uint32_t),                       \
                "DGW strong index must be exactly 4 bytes (DVM Rule 15)")

DGW_STRONG_INDEX(NodeId,      kNullNode);
DGW_STRONG_INDEX(EdgeId,      kNullEdge);
DGW_STRONG_INDEX(RegionId,    kNullRegion);
DGW_STRONG_INDEX(SymbolId,    kNullSymbol);
DGW_STRONG_INDEX(TypeId,      kNullType);
DGW_STRONG_INDEX(FrameStateId, kNull32);

#undef DGW_STRONG_INDEX

// ---- PortId (16-bit per spec Part 1.1) -----------------------------------
struct PortId {
  std::uint16_t value{kNullPort};
  PortId() = default;
  explicit constexpr PortId(std::uint16_t v) noexcept : value(v) {}
  constexpr bool operator==(const PortId&) const = default;
  constexpr auto operator<=>(const PortId&) const = default;
  constexpr bool is_null() const noexcept { return value == kNullPort; }
  constexpr bool valid() const noexcept { return value != kNullPort; }
};
static_assert(sizeof(PortId) == sizeof(std::uint16_t),
              "DGW PortId must be exactly 2 bytes per spec Part 1.1.");

}  // namespace dgw

// ---- std::hash specializations --------------------------------------------
namespace std {
template <> struct hash<dgw::NodeId> {
  std::size_t operator()(const dgw::NodeId& id) const noexcept {
    return std::hash<std::uint32_t>{}(id.value);
  }
};
template <> struct hash<dgw::EdgeId> {
  std::size_t operator()(const dgw::EdgeId& id) const noexcept {
    return std::hash<std::uint32_t>{}(id.value);
  }
};
template <> struct hash<dgw::RegionId> {
  std::size_t operator()(const dgw::RegionId& id) const noexcept {
    return std::hash<std::uint32_t>{}(id.value);
  }
};
template <> struct hash<dgw::SymbolId> {
  std::size_t operator()(const dgw::SymbolId& id) const noexcept {
    return std::hash<std::uint32_t>{}(id.value);
  }
};
template <> struct hash<dgw::TypeId> {
  std::size_t operator()(const dgw::TypeId& id) const noexcept {
    return std::hash<std::uint32_t>{}(id.value);
  }
};
template <> struct hash<dgw::FrameStateId> {
  std::size_t operator()(const dgw::FrameStateId& id) const noexcept {
    return std::hash<std::uint32_t>{}(id.value);
  }
};
}  // namespace std
