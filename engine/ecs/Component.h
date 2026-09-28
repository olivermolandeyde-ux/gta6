#pragma once

#include "core/Assert.h"
#include "core/Hash.h"
#include "core/Types.h"

#include <cstring>
#include <new>
#include <type_traits>

namespace engine {

struct ComponentType {
    u32         id        = kInvalidIndex;
    u32         size      = 0;
    u32         alignment = 0;
    u32         hash      = 0;
    const char* name      = "unknown";

    using ConstructFn = void (*)(void* dst);
    using DestroyFn   = void (*)(void* dst);
    using MoveFn      = void (*)(void* dst, void* src);
    using CopyFn      = void (*)(void* dst, const void* src);

    ConstructFn construct = nullptr;
    DestroyFn   destroy   = nullptr;
    MoveFn      move      = nullptr;
    CopyFn      copy      = nullptr;
};

template <typename T>
[[nodiscard]] constexpr const char* type_name_pretty() noexcept {
#if defined(_MSC_VER)
    return __FUNCSIG__;
#else
    return __PRETTY_FUNCTION__;
#endif
}

// Compile-time contract every component must satisfy.
//
//  - No virtual functions (no vptr in SoA columns).
//  - No std::vector / std::string / shared_ptr. Those allocate on the CRT
//    heap and hitch. Store handles, IDs, or pointers into engine arenas.
//  - Trivially relocatable preferred. If not, provide a well-defined move.
template <typename T>
struct ComponentTraits {
    static_assert(std::is_trivially_copyable_v<T> || std::is_nothrow_move_constructible_v<T>,
                  "components must be trivially copyable or nothrow-moveable");
    static_assert(!std::is_polymorphic_v<T>,
                  "components must not be polymorphic — no vtables in chunks");
    static_assert(sizeof(T) > 0, "components cannot be empty sentinels; use a tag struct of 1 byte");
    static_assert(alignof(T) <= kCacheLineBytes, "component alignment exceeds cache line");

    static constexpr u32 hash      = type_hash32<T>();
    static constexpr u32 size      = static_cast<u32>(sizeof(T));
    static constexpr u32 alignment = static_cast<u32>(alignof(T));

    static void construct(void* dst) { new (dst) T(); }

    static void destroy(void* dst) { static_cast<T*>(dst)->~T(); }

    static void move(void* dst, void* src) {
        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memcpy(dst, src, sizeof(T));
        } else {
            new (dst) T(static_cast<T&&>(*static_cast<T*>(src)));
            static_cast<T*>(src)->~T();
        }
    }

    static void copy(void* dst, const void* src) {
        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memcpy(dst, src, sizeof(T));
        } else {
            new (dst) T(*static_cast<const T*>(src));
        }
    }
};

} // namespace engine
