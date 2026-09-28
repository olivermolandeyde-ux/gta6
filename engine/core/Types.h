#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace engine {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;
using usize = std::size_t;
using isize = std::ptrdiff_t;
using byte = std::byte;

struct float3 {
    f32 x;
    f32 y;
    f32 z;
};

[[nodiscard]] inline float3 float3_add(float3 a, float3 b) noexcept {
    return float3{a.x + b.x, a.y + b.y, a.z + b.z};
}
[[nodiscard]] inline float3 float3_sub(float3 a, float3 b) noexcept {
    return float3{a.x - b.x, a.y - b.y, a.z - b.z};
}
[[nodiscard]] inline float3 float3_scale(float3 a, f32 s) noexcept {
    return float3{a.x * s, a.y * s, a.z * s};
}
[[nodiscard]] inline f32 float3_dot(float3 a, float3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
[[nodiscard]] inline f32 float3_length_sq(float3 a) noexcept {
    return float3_dot(a, a);
}
[[nodiscard]] inline f32 clampf(f32 v, f32 lo, f32 hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

[[nodiscard]] inline float3 float3_cross(float3 a, float3 b) noexcept {
    return float3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

[[nodiscard]] inline f32 float3_length(float3 a) noexcept {
    return std::sqrt(float3_length_sq(a));
}

[[nodiscard]] inline float3 float3_normalize_or(float3 a, float3 fallback) noexcept {
    const f32 ls = float3_length_sq(a);
    if (ls < 1.0e-12f) {
        return fallback;
    }
    return float3_scale(a, 1.0f / std::sqrt(ls));
}

// Column-major 3x3. Columns are chassis right (X), up (Y), forward (Z).
struct float3x3 {
    float3 c0;
    float3 c1;
    float3 c2;
};

[[nodiscard]] inline float3x3 float3x3_identity() noexcept {
    return float3x3{float3{1.f, 0.f, 0.f}, float3{0.f, 1.f, 0.f}, float3{0.f, 0.f, 1.f}};
}

[[nodiscard]] inline float3 float3x3_mul(const float3x3& r, float3 v) noexcept {
    return float3_add(float3_add(float3_scale(r.c0, v.x), float3_scale(r.c1, v.y)),
                      float3_scale(r.c2, v.z));
}

[[nodiscard]] inline float3 float3x3_mul_transpose(const float3x3& r, float3 v) noexcept {
    return float3{float3_dot(r.c0, v), float3_dot(r.c1, v), float3_dot(r.c2, v)};
}

inline constexpr u32 kCacheLineBytes     = 64;
inline constexpr u32 kOsPageBytes        = 4096;
inline constexpr u32 kChunkBytes         = 16 * 1024;   // L1-resident archetype slab
inline constexpr u32 kHugePageBytes      = 2 * 1024 * 1024;
inline constexpr u32 kMaxComponents      = 256;
inline constexpr u32 kMaxArchetypes      = 4096;
inline constexpr u32 kMaxEntitiesHint    = 1u << 20;    // 1,048,576 live entities
inline constexpr u32 kInvalidIndex       = 0xFFFFFFFFu;

template <typename T>
[[nodiscard]] constexpr T align_up(T value, T alignment) noexcept {
    static_assert(std::is_unsigned_v<T>);
    return static_cast<T>((value + (alignment - 1)) & ~(alignment - 1));
}

template <typename T>
[[nodiscard]] constexpr bool is_aligned(T value, T alignment) noexcept {
    return (value & (alignment - 1)) == 0;
}

template <typename T>
[[nodiscard]] constexpr T max_of(T a, T b) noexcept {
    return a > b ? a : b;
}

template <typename T>
[[nodiscard]] constexpr T min_of(T a, T b) noexcept {
    return a < b ? a : b;
}

[[nodiscard]] inline void* align_pointer(void* ptr, usize alignment) noexcept {
    const usize address = reinterpret_cast<usize>(ptr);
    const usize aligned = align_up(address, alignment);
    return reinterpret_cast<void*>(aligned);
}

// Cache-line padded wrapper. Use on atomics / counters shared across threads
// so false sharing cannot hitch a 60 Hz simulation tick.
template <typename T>
struct alignas(kCacheLineBytes) CacheLinePadded {
    T value;
};

} // namespace engine
