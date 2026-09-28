#pragma once

#include "core/Types.h"

namespace engine {

// FNV-1a 32/64. Used for compile-time component type IDs and archetype keys.
// These hashes are stable for a given compiler + type, never used on disk.
inline constexpr u32  kFnv1a32Offset = 2166136261u;
inline constexpr u32  kFnv1a32Prime  = 16777619u;
inline constexpr u64  kFnv1a64Offset = 14695981039346656037ull;
inline constexpr u64  kFnv1a64Prime  = 1099511628211ull;

[[nodiscard]] constexpr u32 fnv1a32(const char* data, usize length) noexcept {
    u32 hash = kFnv1a32Offset;
    for (usize i = 0; i < length; ++i) {
        hash ^= static_cast<u8>(data[i]);
        hash *= kFnv1a32Prime;
    }
    return hash;
}

[[nodiscard]] constexpr u64 fnv1a64(const char* data, usize length) noexcept {
    u64 hash = kFnv1a64Offset;
    for (usize i = 0; i < length; ++i) {
        hash ^= static_cast<u8>(data[i]);
        hash *= kFnv1a64Prime;
    }
    return hash;
}

[[nodiscard]] constexpr u32 fnv1a32(const char* cstr) noexcept {
    u32 hash = kFnv1a32Offset;
    while (*cstr) {
        hash ^= static_cast<u8>(*cstr++);
        hash *= kFnv1a32Prime;
    }
    return hash;
}

// Compiler-provided pretty function string is unique per instantiated type.
template <typename T>
[[nodiscard]] constexpr u32 type_hash32() noexcept {
#if defined(_MSC_VER)
    constexpr const char* sig = __FUNCSIG__;
#else
    constexpr const char* sig = __PRETTY_FUNCTION__;
#endif
    u32 hash = kFnv1a32Offset;
    for (const char* p = sig; *p; ++p) {
        hash ^= static_cast<u8>(*p);
        hash *= kFnv1a32Prime;
    }
    return hash;
}

template <typename T>
[[nodiscard]] constexpr u64 type_hash64() noexcept {
#if defined(_MSC_VER)
    constexpr const char* sig = __FUNCSIG__;
#else
    constexpr const char* sig = __PRETTY_FUNCTION__;
#endif
    u64 hash = kFnv1a64Offset;
    for (const char* p = sig; *p; ++p) {
        hash ^= static_cast<u8>(*p);
        hash *= kFnv1a64Prime;
    }
    return hash;
}

} // namespace engine
